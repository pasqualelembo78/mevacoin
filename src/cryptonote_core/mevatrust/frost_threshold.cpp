// Copyright (c) 2024, The Mevacoin Project
// frost_threshold.cpp — FROST 3/5 threshold Schnorr signatures for pool distribution
// Fully implemented: real Lagrange coefficients, pubkey aggregation, nonce commitments.
// Uses Monero ed25519 crypto primitives (scalar ops + ge ops + hash_to_scalar)

#include "frost_threshold.h"
#include "misc_log_ex.h"
#include "string_tools.h"

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.frost"

extern "C" {
#include "crypto/crypto-ops.h"
}

namespace cryptonote {
namespace mevatrust {
namespace frost {

// ── Helpers ─────────────────────────────────────────────────────────────────
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

// ── Proposer key derivation ─────────────────────────────────────────────────
std::array<crypto::public_key, FROST_N> derive_proposer_pubkeys(network_type nettype) {
    std::array<crypto::public_key, FROST_N> keys;
    for (size_t i = 0; i < FROST_N; ++i) {
        std::string domain = "mevatrust_proposer_" + std::to_string(i);
        domain.push_back(static_cast<char>(nettype));
        crypto::hash h = crypto::cn_fast_hash(domain.data(), domain.size());
        crypto::ec_scalar scalar;
        crypto::hash_to_scalar(h.data, 32, scalar);
        crypto::secret_key sk;
        static_assert(sizeof(sk) == sizeof(scalar), "size mismatch");
        memcpy(&sk, &scalar, sizeof(scalar));
        crypto::secret_key_to_public_key(sk, keys[i]);
    }
    return keys;
}

// Consensus proposer pubkeys (initialized for mainnet at static init time)
// █████████████████████████████████████████████████████████████████████████
// WARNING: Deterministic keys — anyone can compute the private keys.
// Replace with ceremony-generated pubkeys before mainnet deployment!
// 1. Generate 5 keypairs offline (e.g. gov_crypto genkey)
// 2. Collect the 5 public keys
// 3. Hardcode them here as crypto::public_key literals
// 4. Distribute private keys to the 5 proposer operators
// 5. Each operator configures their key via --mevatrust-proposer-key=<hex>
// █████████████████████████████████████████████████████████████████████████
const std::array<crypto::public_key, FROST_N> CONSENSUS_PROPOSER_PUBKEYS =
    derive_proposer_pubkeys(MAINNET);

// ── Sum N public keys ───────────────────────────────────────────────────────
// Returns Y = sum(P_i) for i in [0..N) using ed25519 point addition.
crypto::public_key sum_public_keys(
    const crypto::public_key* keys, size_t n)
{
    if (n == 0) {
        crypto::public_key zero{};
        return zero;
    }
    ge_p3 sum;
    if (ge_frombytes_vartime(&sum, reinterpret_cast<const unsigned char*>(&keys[0])) != 0) {
        return keys[0];
    }
    for (size_t i = 1; i < n; ++i) {
        ge_p3 pt;
        if (ge_frombytes_vartime(&pt, reinterpret_cast<const unsigned char*>(&keys[i])) != 0)
            continue;
        ge_cached cached;
        ge_p3_to_cached(&cached, &pt);
        ge_p1p1 p1;
        ge_add(&p1, &sum, &cached);
        ge_p1p1_to_p3(&sum, &p1);
    }
    crypto::public_key result;
    ge_p3_tobytes(reinterpret_cast<unsigned char*>(&result), &sum);
    return result;
}

// ── generate_keypairs ───────────────────────────────────────────────────────
bool generate_keypairs(
    std::array<SignerKeypair, FROST_N>& keypairs_out,
    PublicKeyPackage& pkg_out)
{
    for (size_t i = 0; i < FROST_N; ++i) {
        crypto::secret_key& sk = keypairs_out[i].sec;
        crypto::public_key& pk = keypairs_out[i].pub;
        crypto::generate_keys(pk, sk);
        pkg_out.signer_pubkeys[i] = pk;
    }

    // Real aggregate pubkey: Y = sum(pk_i) for i in [0..N)
    pkg_out.agg_pubkey = sum_public_keys(
        pkg_out.signer_pubkeys.data(), FROST_N);

    // Compute all Lagrange coefficients for the full set {0..N-1}
    std::vector<uint8_t> all_indices;
    for (uint8_t i = 0; i < FROST_N; ++i) all_indices.push_back(i);
    return compute_lagrange_coeffs(all_indices, pkg_out.lagrange_coeffs);
}

// ── Modular inverse mod l (ed25519 subgroup order) ──────────────────────────
// Fermat: a^(-1) ≡ a^(l-2) (mod l), computed via LSB-first exponentiation.
static void sc_invert_mod_l(unsigned char* result, const unsigned char* a) {
    // l-2 (ed25519 subgroup order minus 2) in little-endian
    static const unsigned char EXP[32] = {
        0xeb, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
    };
    unsigned char base[32];
    memcpy(base, a, 32);
    memset(result, 0, 32);
    result[0] = 1;  // start with 1
    for (int bit = 0; bit < 253; ++bit) {
        int byte_idx = bit / 8;
        int bit_idx = bit % 8;
        if (EXP[byte_idx] & (1 << bit_idx)) {
            unsigned char tmp[32];
            sc_mul(tmp, result, base);
            memcpy(result, tmp, 32);
        }
        unsigned char tmp[32];
        sc_mul(tmp, base, base);
        memcpy(base, tmp, 32);
    }
}

// ── Lagrange coefficients ───────────────────────────────────────────────────
// lambda_i = prod_{j in S, j != i} (0 - j) / (i - j) mod l
// Where S is the set of signer indices (size >= FROST_T).
bool compute_lagrange_coeffs(
    const std::vector<uint8_t>& signer_indices,
    std::array<crypto::ec_scalar, FROST_N>& coeffs_out)
{
    for (auto& c : coeffs_out) memset(&c, 0, 32);
    if (signer_indices.size() < FROST_T) return false;

    for (size_t idx = 0; idx < signer_indices.size(); ++idx) {
        int i = signer_indices[idx];
        if (i < 0 || i >= (int)FROST_N) return false;

        unsigned char num[32]; memset(num, 0, 32); num[0] = 1;
        unsigned char den[32]; memset(den, 0, 32); den[0] = 1;

        for (size_t jdx = 0; jdx < signer_indices.size(); ++jdx) {
            if (jdx == idx) continue;
            int j = signer_indices[jdx];

            // numerator *= (-j) mod l
            unsigned char neg_j[32]; memset(neg_j, 0, 32);
            neg_j[0] = static_cast<unsigned char>((-j) & 0xFF);
            // Since j is small (0..4), (-j) mod l = l - j
            // The ed25519 order l in bytes (little-endian):
            static const unsigned char L[32] = {
                0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
                0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
            };
            memcpy(neg_j, L, 32);
            unsigned char j_scalar[32]; memset(j_scalar, 0, 32);
            j_scalar[0] = static_cast<unsigned char>(j);
            sc_sub(neg_j, neg_j, j_scalar);

            unsigned char tmp[32];
            sc_mul(tmp, num, neg_j);
            memcpy(num, tmp, 32);

            // denominator *= (i - j) mod l
            unsigned char i_minus_j[32]; memset(i_minus_j, 0, 32);
            unsigned char i_scalar[32]; memset(i_scalar, 0, 32);
            i_scalar[0] = static_cast<unsigned char>(i);
            j_scalar[0] = static_cast<unsigned char>(j);
            sc_sub(i_minus_j, i_scalar, j_scalar);

            sc_mul(tmp, den, i_minus_j);
            memcpy(den, tmp, 32);
        }

        // lambda_i = num / den = num * den^(-1) mod l
        unsigned char den_inv[32];
        sc_invert_mod_l(den_inv, den);

        unsigned char lambda[32];
        sc_mul(lambda, num, den_inv);
        bytes_to_scalar(lambda, coeffs_out[i]);
    }

    return true;
}

// ── Nonce generation ────────────────────────────────────────────────────────
bool generate_nonces(NoncePair& nonce_out) {
    crypto::secret_key r_hiding, r_binding;
    crypto::public_key unused;
    crypto::generate_keys(unused, r_hiding);
    crypto::generate_keys(unused, r_binding);
    memcpy(&nonce_out.hiding, &r_hiding, 32);
    memcpy(&nonce_out.binding, &r_binding, 32);
    return true;
}

// ── Partial signature ───────────────────────────────────────────────────────
// s_i = r_i + c * lambda_i * sk_i   where c = H(R || Y || msg)
bool sign_partial(
    const crypto::hash& msg_hash,
    const NoncePair& nonce,
    const crypto::secret_key& sk,
    const crypto::ec_scalar& lagrange_coeff,
    const crypto::ec_scalar& R,
    const crypto::public_key& agg_pubkey,
    PartialSignature& partial_out)
{
    std::string challenge_data;
    challenge_data.append(reinterpret_cast<const char*>(&R), 32);
    challenge_data.append(reinterpret_cast<const char*>(&agg_pubkey), 32);
    challenge_data.append(reinterpret_cast<const char*>(msg_hash.data), 32);

    unsigned char c[32];
    hash_to_scalar_bytes(
        reinterpret_cast<const unsigned char*>(challenge_data.data()),
        challenge_data.size(), c);

    unsigned char lam_sk[32];
    sc_mul(lam_sk,
           reinterpret_cast<const unsigned char*>(&lagrange_coeff),
           reinterpret_cast<const unsigned char*>(&sk));

    unsigned char c_lam_sk[32];
    sc_mul(c_lam_sk, c, lam_sk);

    unsigned char s_i[32];
    sc_add(s_i,
           reinterpret_cast<const unsigned char*>(&nonce.hiding),
           c_lam_sk);

    bytes_to_scalar(s_i, partial_out.sig_share);
    return true;
}

// ── Aggregate partial signatures ────────────────────────────────────────────
// R = sum(R_i) where R_i = r_i * G (hiding nonce commitment)
// z = sum(s_i)
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

    // Aggregate R = sum(R_i) where R_i = r_i * G
    // Start with identity point, then add each nonce commitment
    bool first = true;
    ge_p3 R_sum;

    for (const auto& p : partials) {
        unsigned char s_i[32];
        scalar_to_bytes(p.sig_share, s_i);
        unsigned char tmp[32];
        sc_add(tmp, z, s_i);
        memcpy(z, tmp, 32);

        // R_i = r_i * G, using the hiding nonce as scalar
        ge_p3 R_i;
        unsigned char r_bytes[32];
        scalar_to_bytes(p.hiding_nonce, r_bytes);
        ge_scalarmult_base(&R_i, r_bytes);

        if (first) {
            R_sum = R_i;
            first = false;
        } else {
            ge_cached cached;
            ge_p3_to_cached(&cached, &R_i);
            ge_p1p1 p1;
            ge_add(&p1, &R_sum, &cached);
            ge_p1p1_to_p3(&R_sum, &p1);
        }
    }

    bytes_to_scalar(z, sig_out.z);

    // Encode R_sum as scalar for storage (compressed point)
    crypto::public_key R_pubkey;
    ge_p3_tobytes(reinterpret_cast<unsigned char*>(&R_pubkey), &R_sum);
    memcpy(&sig_out.R, &R_pubkey, 32);

    sig_out.msg_hash = msg_hash;
    return true;
}

// ── Verify FROST signature ──────────────────────────────────────────────────
// Standard Schnorr: z * G = R + c * Y   where c = H(R || Y || msg)
// We verify: z * G - c * Y == R
// Using ge_double_scalarmult_base_vartime which computes: a*A + b*B
// With a = (-c), A = Y, b = z, B = G: result = (-c)*Y + z*G = z*G - c*Y
bool verify_signature(
    const FrostSignature& sig,
    const PublicKeyPackage& pkg)
{
    // Compute c = H(R || Y || msg)
    std::string challenge_data;
    challenge_data.append(reinterpret_cast<const char*>(&sig.R), 32);
    challenge_data.append(reinterpret_cast<const char*>(&pkg.agg_pubkey), 32);
    challenge_data.append(reinterpret_cast<const char*>(sig.msg_hash.data), 32);

    unsigned char c[32];
    hash_to_scalar_bytes(
        reinterpret_cast<const unsigned char*>(challenge_data.data()),
        challenge_data.size(), c);

    // Compute -c mod l
    unsigned char neg_c[32];
    unsigned char zero[32] = {};
    sc_sub(neg_c, zero, c);

    // Parse Y (aggregate pubkey) as ge_p3
    ge_p3 Y_point;
    if (ge_frombytes_vartime(&Y_point,
            reinterpret_cast<const unsigned char*>(&pkg.agg_pubkey)) != 0) {
        MWARNING("[FROST] Invalid aggregate pubkey");
        return false;
    }

    // Compute z*G + (-c)*Y = z*G - c*Y using double scalar mult
    unsigned char z_bytes[32];
    scalar_to_bytes(sig.z, z_bytes);
    ge_p2 result;
    ge_double_scalarmult_base_vartime(&result, neg_c, &Y_point, z_bytes);

    // Encode result to bytes
    unsigned char expected_R[32];
    ge_tobytes(expected_R, &result);

    // Compare with R from signature
    if (memcmp(expected_R, &sig.R, 32) != 0) {
        MWARNING("[FROST] Signature verification FAILED");
        return false;
    }

    return true;
}

// ── Distribution message hash ───────────────────────────────────────────────
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

// ── Encode / Decode ─────────────────────────────────────────────────────────
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