// Copyright (c) 2024, The Mevacoin Project
// frost_threshold.cpp — FROST 3/5 threshold Schnorr signatures for pool distribution
// Uses Monero ed25519 crypto primitives (scalar ops + hash_to_scalar)
//
// Based on FROST (Flexible Round-Optimized Schnorr Threshold) - CFRG draft
// Uses trusted-dealer polynomial setup: f(x) of degree T-1, each signer gets f(i).
// Aggregate pubkey Y = f(0) * G.

#include "frost_threshold.h"
#include "misc_log_ex.h"
#include "string_tools.h"

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.frost"

extern "C" {
#include "crypto/crypto-ops.h"
#include "crypto/random.h"
}

namespace cryptonote {
namespace mevatrust {
namespace frost {

// ── Internal helpers ──────────────────────────────────────────────────────────

static void scalar_to_bytes(const crypto::ec_scalar& s, unsigned char bytes[32]) {
    memcpy(bytes, &s, 32);
}

static void bytes_to_scalar(const unsigned char bytes[32], crypto::ec_scalar& s) {
    memcpy(&s, bytes, 32);
}

static void hash_to_scalar_bytes(const unsigned char* data, size_t len, unsigned char out[32]) {
    crypto::hash h = crypto::cn_fast_hash(data, len);
    crypto::hash_to_scalar(h.data, 32, *reinterpret_cast<crypto::ec_scalar*>(out));
}

// Convert a small integer (0..255) to an ed25519 scalar
static void int_to_scalar(uint8_t val, unsigned char out[32]) {
    memset(out, 0, 32);
    out[0] = val;
    sc_reduce32(out);
}

// Evaluate polynomial f(x) = a_0 + a_1*x + a_2*x^2 + ... + a_{T-1}*x^{T-1} at x = eval_at
// All operations mod l. coeffs[0] = a_0 (the secret), coeffs[T-1] = a_{T-1}
static void eval_poly(const unsigned char coeffs[][32], size_t degree,
                      uint8_t eval_at, unsigned char result[32])
{
    // Horner's method: f(x) = a_0 + x*(a_1 + x*(a_2 + ... + x*a_{T-1}))
    unsigned char x[32];
    int_to_scalar(eval_at, x);

    memcpy(result, coeffs[degree], 32);
    for (int i = (int)degree - 1; i >= 0; --i) {
        // result = coeffs[i] + x * result
        unsigned char tmp[32];
        sc_mul(tmp, x, result);
        sc_add(result, coeffs[i], tmp);
    }
}

// ── Hardcoded proposer public keys ──────────────────────────────────────────
// Derived deterministically from domain string so all validators agree on the
// proposer set without external configuration.
//
// Secret keys are deterministically derived from the same seed so that each
// proposer can independently regenerate their keypair.
//
// In production, these would be governance-elected and distributed out-of-band.
static crypto::secret_key proposer_sk(size_t i) {
    std::string seed = "mevatrust_proposer_key_";
    seed.push_back('0' + (char)i);
    seed += "_v1";
    crypto::hash h = crypto::cn_fast_hash(seed.data(), seed.size());
    crypto::secret_key sk;
    // Reduce hash to valid scalar
    memcpy(&sk, &h, 32);
    sc_reduce32(reinterpret_cast<unsigned char*>(&sk));
    return sk;
}

const std::array<crypto::public_key, FROST_N> CONSENSUS_PROPOSER_PUBKEYS = {{
    []() { crypto::public_key pk; crypto::secret_key sk = proposer_sk(0);
           crypto::generate_keys(pk, sk); return pk; }(),
    []() { crypto::public_key pk; crypto::secret_key sk = proposer_sk(1);
           crypto::generate_keys(pk, sk); return pk; }(),
    []() { crypto::public_key pk; crypto::secret_key sk = proposer_sk(2);
           crypto::generate_keys(pk, sk); return pk; }(),
    []() { crypto::public_key pk; crypto::secret_key sk = proposer_sk(3);
           crypto::generate_keys(pk, sk); return pk; }(),
    []() { crypto::public_key pk; crypto::secret_key sk = proposer_sk(4);
           crypto::generate_keys(pk, sk); return pk; }(),
}};

// ── Key generation: trusted dealer with polynomial ─────────────────────────

bool generate_keypairs(
    std::array<SignerKeypair, FROST_N>& keypairs_out,
    PublicKeyPackage& pkg_out)
{
    // Generate random polynomial of degree T-1
    // f(x) = a_0 + a_1*x + ... + a_{T-1}*x^{T-1}
    unsigned char coeffs[FROST_T][32];
    for (size_t i = 0; i < FROST_T; ++i) {
        crypto::secret_key sk;
        crypto::public_key unused;
        crypto::generate_keys(unused, sk);
        memcpy(coeffs[i], &sk, 32);
        sc_reduce32(coeffs[i]);
    }

    // Compute each signer's keypair: sk_i = f(i), pk_i = sk_i * G
    for (size_t i = 0; i < FROST_N; ++i) {
        unsigned char sk_bytes[32];
        eval_poly(coeffs, FROST_T - 1, static_cast<uint8_t>(i + 1), sk_bytes);
        memcpy(&keypairs_out[i].sec, sk_bytes, 32);
        ge_p3 pubkey_point;
        ge_scalarmult_base(&pubkey_point, sk_bytes);
        ge_p3_tobytes(reinterpret_cast<unsigned char*>(&keypairs_out[i].pub), &pubkey_point);
        pkg_out.signer_pubkeys[i] = keypairs_out[i].pub;
    }

    // Aggregate pubkey Y = a_0 * G  (the constant term of the polynomial)
    {
        ge_p3 agg_point;
        ge_scalarmult_base(&agg_point, coeffs[0]);
        ge_p3_tobytes(reinterpret_cast<unsigned char*>(&pkg_out.agg_pubkey), &agg_point);
    }

    // Pre-compute Lagrange coefficients for the FULL proposer set (indices 1..N)
    // lambda_i = prod_{j != i, j=1..N} (0 - j) / (i - j) mod l
    // These are used to verify the aggregate public key from individual pubkeys
    for (size_t i = 0; i < FROST_N; ++i) {
        std::vector<uint8_t> all_indices;
        for (size_t j = 0; j < FROST_N; ++j) all_indices.push_back(static_cast<uint8_t>(j + 1));

        unsigned char num[32] = {1}, den[32] = {1};
        int idx_i = (int)(i + 1);
        for (size_t j = 0; j < FROST_N; ++j) {
            if (j == i) continue;
            int idx_j = (int)(j + 1);
            // num *= (0 - j) = -j
            unsigned char neg_j[32];
            int_to_scalar(static_cast<uint8_t>(idx_j), neg_j);
            sc_sub(neg_j, (const unsigned char[32]){0}, neg_j);
            sc_mul(num, num, neg_j);
            // den *= (i - j)
            unsigned char diff[32];
            int_to_scalar(static_cast<uint8_t>(idx_i), diff);
            unsigned char j_scalar[32];
            int_to_scalar(static_cast<uint8_t>(idx_j), j_scalar);
            sc_sub(diff, diff, j_scalar);
            sc_mul(den, den, diff);
        }
        // lambda_i = num * den^{-1} mod l
        // For ed25519, sc_invert would be needed, but we don't have it directly.
        // Compute den^{-1} mod l using Fermat inversion: den^{l-2} mod l
        // This is correct because l is prime (ed25519 subgroup order).
        // Since we don't have sc_invert, we use the fact that for fixed N=5,T=3
        // the Lagrange coefficients are small constants:
        // We compute den^{-1} using repeated squaring
        // l = 2^252 + 27742317777372353535851937790883648493
        // For simplicity, pre-compute:
        // lambda = num * den^{-1} mod l
        // We use the pre-computed values for indices 1..5 with T=3
        unsigned char lambda[32];
        // Compute den_inv = den^(l-2) mod l using pow256
        unsigned char l_minus_2[32];
        // ed25519 subgroup order: l = 2^252 + 27742317777372353535851937790883648493
        // We store l in bytes (little-endian)
        // l = 0x1000000000000000000000000000000014def9dea2f79cd65812631a5cf5d3ed
        // Actually let's compute it properly:
        // den^(l-2) mod l via Fermat
        unsigned char base[32], result_f[32] = {1};
        memcpy(base, den, 32);
        // l = ed25519 subgroup order
        const unsigned char ed25519_order[32] = {
            0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
            0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
        };
        // Compute l-2 by subtracting 2
        unsigned char exp[32];
        memcpy(exp, ed25519_order, 32);
        exp[0] -= 2;
        if (exp[0] > 0xed) { /* borrow */ for (size_t b = 1; b < 32; ++b) { exp[b] -= 1; if (exp[b] < 0xff) break; } }
        // Square-and-multiply: result = base^exp mod l
        for (int bit = 255; bit >= 0; --bit) {
            unsigned char tmp[32];
            memcpy(tmp, result_f, 32);
            sc_mul(result_f, tmp, tmp);
            if ((exp[bit / 8] >> (bit % 8)) & 1) {
                memcpy(tmp, result_f, 32);
                sc_mul(result_f, tmp, base);
            }
        }
        sc_mul(lambda, num, result_f);
        bytes_to_scalar(lambda, pkg_out.lagrange_coeffs[i]);
    }

    return true;
}

// ── Lagrange coefficients for signing ──────────────────────────────────────

bool compute_lagrange_coeffs(
    const std::vector<uint8_t>& signer_indices,
    std::array<crypto::ec_scalar, FROST_N>& coeffs_out)
{
    for (auto& c : coeffs_out) memset(&c, 0, 32);

    if (signer_indices.size() < FROST_T) return false;

    // For each signer i in the signing set:
    // lambda_i = prod_{j in signers, j != i} (0 - j) / (i - j) mod l
    for (size_t idx = 0; idx < signer_indices.size(); ++idx) {
        int i = signer_indices[idx];
        if (i < 0 || i >= (int)FROST_N) return false;

        unsigned char num[32] = {1};
        unsigned char den[32] = {1};

        for (size_t jdx = 0; jdx < signer_indices.size(); ++jdx) {
            if (jdx == idx) continue;
            int j = signer_indices[jdx];

            // num *= (0 - j) = -j
            unsigned char neg_j[32];
            int_to_scalar(static_cast<uint8_t>(j), neg_j);
            sc_sub(neg_j, (const unsigned char[32]){0}, neg_j);
            sc_mul(num, num, neg_j);

            // den *= (i - j)
            unsigned char i_scalar[32], j_scalar[32];
            int_to_scalar(static_cast<uint8_t>(i), i_scalar);
            int_to_scalar(static_cast<uint8_t>(j), j_scalar);
            unsigned char diff[32];
            sc_sub(diff, i_scalar, j_scalar);
            sc_mul(den, den, diff);
        }

        // lambda_i = num * den^{-1} mod l
        // Use Fermat: den^{-1} = den^{l-2} mod l
        const unsigned char ed25519_order[32] = {
            0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
            0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
        };
        unsigned char exp[32];
        memcpy(exp, ed25519_order, 32);
        exp[0] -= 2;
        if (exp[0] > 0xed) {
            for (size_t b = 1; b < 32; ++b) { exp[b] -= 1; if (exp[b] < 0xff) break; }
        }

        unsigned char base[32], result_f[32] = {1};
        memcpy(base, den, 32);
        for (int bit = 255; bit >= 0; --bit) {
            unsigned char tmp[32];
            memcpy(tmp, result_f, 32);
            sc_mul(result_f, tmp, tmp);
            if ((exp[bit / 8] >> (bit % 8)) & 1) {
                memcpy(tmp, result_f, 32);
                sc_mul(result_f, tmp, base);
            }
        }
        unsigned char lambda[32];
        sc_mul(lambda, num, result_f);
        bytes_to_scalar(lambda, coeffs_out[i]);
    }

    return true;
}

// ── Nonce generation ───────────────────────────────────────────────────────

bool generate_nonces(NoncePair& nonce_out) {
    crypto::secret_key r_hiding, r_binding;
    crypto::public_key unused;
    crypto::generate_keys(unused, r_hiding);
    crypto::generate_keys(unused, r_binding);
    memcpy(&nonce_out.hiding, &r_hiding, 32);
    memcpy(&nonce_out.binding, &r_binding, 32);
    return true;
}

// ── Challenge hash: H(R || Y || msg) ───────────────────────────────────────

static void compute_challenge(
    const crypto::ec_scalar& R,
    const crypto::public_key& agg_pubkey,
    const crypto::hash& msg_hash,
    unsigned char c[32])
{
    std::string challenge_data;
    challenge_data.append(reinterpret_cast<const char*>(&R), 32);
    challenge_data.append(reinterpret_cast<const char*>(&agg_pubkey), 32);
    challenge_data.append(reinterpret_cast<const char*>(msg_hash.data), 32);
    hash_to_scalar_bytes(
        reinterpret_cast<const unsigned char*>(challenge_data.data()),
        challenge_data.size(), c);
}

// ── Partial signing ────────────────────────────────────────────────────────

bool sign_partial(
    const crypto::hash& msg_hash,
    const NoncePair& nonce,
    const crypto::secret_key& sk,
    const crypto::ec_scalar& lagrange_coeff,
    const crypto::public_key& agg_pubkey,
    const crypto::ec_scalar& R,
    PartialSignature& partial_out)
{
    // Store signer info
    partial_out.hiding_nonce = nonce.hiding;
    partial_out.binding_nonce = nonce.binding;

    // Compute challenge c = H(R || Y || msg)
    unsigned char c[32];
    compute_challenge(R, agg_pubkey, msg_hash, c);

    // lambda_i * sk_i
    unsigned char lam_sk[32];
    sc_mul(lam_sk,
           reinterpret_cast<const unsigned char*>(&lagrange_coeff),
           reinterpret_cast<const unsigned char*>(&sk));

    // c * (lambda_i * sk_i)
    unsigned char c_lam_sk[32];
    sc_mul(c_lam_sk, c, lam_sk);

    // s_i = r_i + c * lambda_i * sk_i
    unsigned char s_i[32];
    sc_add(s_i,
           reinterpret_cast<const unsigned char*>(&nonce.hiding),
           c_lam_sk);

    bytes_to_scalar(s_i, partial_out.sig_share);
    return true;
}

// ── Aggregate partial signatures ───────────────────────────────────────────

bool aggregate_signatures(
    const crypto::hash& msg_hash,
    const std::vector<PartialSignature>& partials,
    const PublicKeyPackage& pkg,
    FrostSignature& sig_out)
{
    if (partials.size() < FROST_T) {
        MERROR("[FROST] Not enough partial signatures: " << partials.size());
        return false;
    }

    // Aggregate z = sum(s_i)
    unsigned char z[32] = {};
    memset(z, 0, 32);

    // Aggregate nonce R = sum(R_i) where R_i = r_i * G
    ge_p3 R_agg_point;
    bool first_R = true;

    for (size_t i = 0; i < partials.size(); ++i) {
        // Sum s_i
        unsigned char s_i[32];
        scalar_to_bytes(partials[i].sig_share, s_i);
        unsigned char tmp[32];
        sc_add(tmp, z, s_i);
        memcpy(z, tmp, 32);

        // Compute and sum R_i = r_i * G
        ge_p3 R_i_point;
        ge_scalarmult_base(&R_i_point,
            reinterpret_cast<const unsigned char*>(&partials[i].hiding_nonce));
        if (first_R) {
            R_agg_point = R_i_point;
            first_R = false;
        } else {
            ge_cached R_i_cached;
            ge_p3_to_cached(&R_i_cached, &R_i_point);
            ge_p1p1 R_sum;
            ge_add(&R_sum, &R_agg_point, &R_i_cached);
            ge_p1p1_to_p3(&R_agg_point, &R_sum);
        }
    }

    bytes_to_scalar(z, sig_out.z);

    // Store aggregated R
    crypto::public_key R_bytes;
    ge_p3_tobytes(reinterpret_cast<unsigned char*>(&R_bytes), &R_agg_point);
    memcpy(&sig_out.R, &R_bytes, 32);

    sig_out.msg_hash = msg_hash;
    return true;
}

// ── Verify aggregated FROST signature ─────────────────────────────────────

bool verify_signature(
    const FrostSignature& sig,
    const PublicKeyPackage& pkg)
{
    // Verify: z * G = R + c * Y
    // using Monero's crypto::check_signature which checks s*G = R + c*Y
    crypto::signature monero_sig;
    memcpy(&monero_sig.c, &sig.R, 32);
    memcpy(&monero_sig.r, &sig.z, 32);

    bool ok = crypto::check_signature(sig.msg_hash, pkg.agg_pubkey, monero_sig);
    if (!ok) {
        MWARNING("[FROST] Signature verification FAILED");
    }
    return ok;
}

// ── Message hash for pool distribution ────────────────────────────────────

crypto::hash create_distribution_message_hash(
    uint64_t height,
    uint32_t period,
    const std::vector<std::pair<account_public_address, uint64_t>>& outputs)
{
    std::string msg;
    msg.append(FROST_DOMAIN, strlen(FROST_DOMAIN));
    msg.append(reinterpret_cast<const char*>(&height), 8);
    msg.append(reinterpret_cast<const char*>(&period), 4);

    for (const auto& [addr, amount] : outputs) {
        msg.append(reinterpret_cast<const char*>(&addr.m_spend_public_key), 32);
        msg.append(reinterpret_cast<const char*>(&addr.m_view_public_key), 32);
        msg.append(reinterpret_cast<const char*>(&amount), 8);
    }

    return crypto::cn_fast_hash(msg.data(), msg.size());
}

// ── Encode/decode ─────────────────────────────────────────────────────────

bool encode_frost_signature(const FrostSignature& sig, std::vector<uint8_t>& out) {
    out.clear();
    out.resize(32 + 32 + 32);
    memcpy(out.data(), &sig.R, 32);
    memcpy(out.data() + 32, &sig.z, 32);
    memcpy(out.data() + 64, sig.msg_hash.data, 32);
    return true;
}

bool decode_frost_signature(const std::vector<uint8_t>& in, FrostSignature& sig_out) {
    if (in.size() < 96) return false;
    memcpy(&sig_out.R, in.data(), 32);
    memcpy(&sig_out.z, in.data() + 32, 32);
    memcpy(sig_out.msg_hash.data, in.data() + 64, 32);
    return true;
}

} // namespace frost
} // namespace mevatrust
} // namespace cryptonote
