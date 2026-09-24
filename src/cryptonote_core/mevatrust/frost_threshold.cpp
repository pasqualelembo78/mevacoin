// Copyright (c) 2024, The Mevacoin Project
// frost_threshold.cpp — Correct CFRG-FROST t-of-n threshold Schnorr (3/5)
//
// Security architecture (fixed 2026-09-23):
//   * REAL Shamir secret sharing: master secret s, polynomial of degree T-1=2,
//     shares evaluated at x = i for i in 1..5.  Index 0 NEVER appears in the
//     interpolation set, so Lagrange coefficients never degenerate to {1,0,0,0,0}.
//   * Binding factors rho_i = H(i, msg, commitments) per CFRG-FROST.
//   * Aggregate nonce R = sum(D_i + rho_i*E_i);  challenge c = H(R || Y || msg).
//   * Partial s_i = d_i + rho_i*e_i + c*lambda_i*sk_i;  per-partial verification
//     s_i*G == D_i + rho_i*E_i + c*lambda_i*P_i.
//   * Aggregate verifies as plain Schnorr:  z*G == R + c*Y with the ceremony
//     group key Y (NOT sum(P_i) — with Shamir sharing sum(P_i) != Y).
//
// Uses Monero ed25519 crypto primitives (scalar ops + ge ops + hash_to_scalar).

#include "frost_threshold.h"

extern "C" {
#include "crypto/crypto-ops.h"
}

namespace cryptonote {
namespace mevatrust {
namespace frost {

// ── ed25519 group order l (little-endian) ─────────────────────────────────
static const unsigned char L_BYTES[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 };

// ── Scalar helpers ────────────────────────────────────────────────────────
static void small_scalar(uint64_t v, unsigned char out[32]) {
    memset(out, 0, 32);
    // v as little-endian bytes (v is tiny, fits in first 8)
    for (int i = 0; i < 8 && v; ++i) { out[i] = v & 0xFF; v >>= 8; }
}

static void hash_to_scalar_bytes(const unsigned char* buf, size_t len, unsigned char out[32]) {
    crypto::hash h = crypto::cn_fast_hash(buf, len);
    crypto::hash_to_scalar(h.data, 32, *reinterpret_cast<crypto::ec_scalar*>(out));
}

// Fermat scalar inversion mod l: a^(l-2) via LSB-first square-and-multiply.
static void sc_invert_mod_l(unsigned char* result, const unsigned char* a) {
    unsigned char base[32]; memcpy(base, a, 32);
    memset(result, 0, 32); result[0] = 1;
    // exponent l-2
    static const unsigned char EXP[32] = {
        0xeb, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 };
    for (int bit = 0; bit < 253; ++bit) {
        if (EXP[bit / 8] & (1 << (bit % 8))) {
            unsigned char tmp[32]; sc_mul(tmp, result, base); memcpy(result, tmp, 32);
        }
        unsigned char tmp[32]; sc_mul(tmp, base, base); memcpy(base, tmp, 32);
    }
}

// ── Point helpers ─────────────────────────────────────────────────────────
static bool point_from_bytes(const unsigned char b[32], ge_p3& out) {
    return ge_frombytes_vartime(&out, b) == 0;
}
static void point_to_bytes(const ge_p3& pt, unsigned char out[32]) {
    ge_p3_tobytes(out, &pt);
}
static void point_add_p3(const ge_p3& a, const ge_p3& b, ge_p3& out) {
    ge_cached cached; ge_p3_to_cached(&cached, &b);
    ge_p1p1 p1; ge_add(&p1, &a, &cached); ge_p1p1_to_p3(&out, &p1);
}
static void scalar_mult_generic(const unsigned char a[32], const ge_p3& P, ge_p3& out) {
    ge_scalarmult_p3(&out, a, &P);
}
static void scalar_mult_base(const unsigned char a[32], ge_p3& out) {
    ge_scalarmult_base(&out, a);
}

// Sum N public keys (valid point addition, ignores parse failures)
static bool sum_public_keys(const crypto::public_key* keys, size_t n, crypto::public_key& out) {
    if (n == 0) return false;
    ge_p3 sum;
    if (!point_from_bytes(reinterpret_cast<const unsigned char*>(&keys[0]), sum)) return false;
    for (size_t i = 1; i < n; ++i) {
        ge_p3 pt;
        if (!point_from_bytes(reinterpret_cast<const unsigned char*>(&keys[i]), pt)) return false;
        ge_p3 tmp; point_add_p3(sum, pt, tmp); sum = tmp;
    }
    point_to_bytes(sum, reinterpret_cast<unsigned char*>(&out));
    return true;
}

// ── Lagrange coefficients (interpolation at x = 0, over set of indices 1..N) ──
// lambda_i = prod_{j in S, j != i} (0 - j) / (i - j)  (mod l)
// Because every x-coordinate is in 1..N (never 0) and i != j, no degenerate
// {1,0,0,...} result is produced.
bool compute_lagrange_coeffs(
    const std::vector<uint8_t>& signer_indices,
    std::vector<crypto::ec_scalar>& coeffs_out)
{
    coeffs_out.clear();
    const size_t S = signer_indices.size();
    if (S < FROST_T) return false;

    for (size_t k = 0; k < S; ++k) {
        const uint8_t i = signer_indices[k];
        if (i < 1 || i > FROST_N) return false;

        unsigned char num[32]; small_scalar(1, num);
        unsigned char den[32]; small_scalar(1, den);

        for (size_t m = 0; m < S; ++m) {
            if (m == k) continue;
            const uint8_t j = signer_indices[m];
            // numerator factor: (0 - j)  =  -j  (mod l)  =  l - j
            unsigned char jsc[32]; small_scalar(j, jsc);
            unsigned char neg_j[32]; sc_sub(neg_j, L_BYTES, jsc);
            unsigned char tmp[32]; sc_mul(tmp, num, neg_j); memcpy(num, tmp, 32);
            // denominator factor: (i - j)
            unsigned char isc[32]; small_scalar(i, isc);
            unsigned char i_minus_j[32]; sc_sub(i_minus_j, isc, jsc);
            sc_mul(tmp, den, i_minus_j); memcpy(den, tmp, 32);
        }

        unsigned char den_inv[32]; sc_invert_mod_l(den_inv, den);
        unsigned char lambda[32]; sc_mul(lambda, num, den_inv);
        crypto::ec_scalar ls; memcpy(&ls, lambda, 32);
        coeffs_out.push_back(ls);
    }
    return true;
}

// ── Binding factors ───────────────────────────────────────────────────────
// rho_i = H(FROST_BINDING_DOMAIN || i(1) || msg(32) || D_1||E_1 || D_2||E_2 ...)
// where the commitment list is sorted by signer index so every party derives
// the same factors.
bool compute_binding_factors(
    const std::vector<uint8_t>& signer_indices,
    const crypto::hash& msg,
    const std::vector<std::pair<crypto::public_key, crypto::public_key>>& commitments,
    std::vector<crypto::ec_scalar>& rho_out)
{
    rho_out.clear();
    if (signer_indices.size() < FROST_T || commitments.size() != signer_indices.size())
        return false;

    // Build the common commitment string sorted by index
    std::vector<std::pair<uint8_t, std::pair<crypto::public_key, crypto::public_key>>> pairs;
    for (size_t i = 0; i < signer_indices.size(); ++i)
        pairs.push_back({signer_indices[i], commitments[i]});
    std::sort(pairs.begin(), pairs.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    std::string all;
    for (const auto& [idx, de] : pairs) {
        all.append(reinterpret_cast<const char*>(&de.first), 32);
        all.append(reinterpret_cast<const char*>(&de.second), 32);
    }

    for (size_t i = 0; i < signer_indices.size(); ++i) {
        std::string in;
        in.append(FROST_BINDING_DOMAIN, strlen(FROST_BINDING_DOMAIN));
        in.push_back(static_cast<char>(signer_indices[i]));
        in.append(reinterpret_cast<const char*>(msg.data), 32);
        in.append(all);
        unsigned char rho[32];
        hash_to_scalar_bytes(reinterpret_cast<const unsigned char*>(in.data()), in.size(), rho);
        crypto::ec_scalar rs; memcpy(&rs, rho, 32);
        rho_out.push_back(rs);
    }
    return true;
}

// ── Ceremony (one-time trusted setup) ─────────────────────────────────────
// Generates master secret sk, polynomial f(x) = sk + a1*x + a2*x^2 (deg 2),
// shares sk_i = f(i) for i = 1..N, group key Y = sk*G, share keys P_i = sk_i*G.
bool frost_keygen(FrostKeyPackage& key_pkg)
{
    // Master secret
    crypto::secret_key sk;
    crypto::public_key dummy;
    crypto::generate_keys(dummy, sk);
    crypto::secret_key_to_public_key(sk, key_pkg.group_public_key);

    // Random coefficients a1, a2
    crypto::secret_key a1, a2;
    crypto::generate_keys(dummy, a1);
    crypto::generate_keys(dummy, a2);

    for (uint8_t i = 1; i <= FROST_N; ++i) {
        unsigned char isc[32]; small_scalar(i, isc);
        unsigned char i2sc[32]; small_scalar(static_cast<uint64_t>(i) * i, i2sc);

        unsigned char a1i[32]; sc_mul(a1i, reinterpret_cast<const unsigned char*>(&a1), isc);
        unsigned char a2i2[32]; sc_mul(a2i2, reinterpret_cast<const unsigned char*>(&a2), i2sc);
        unsigned char sk_i[32];
        sc_add(sk_i, reinterpret_cast<const unsigned char*>(&sk), a1i);
        sc_add(sk_i, sk_i, a2i2);

        key_pkg.signer_keypairs[i-1].index = i;
        memcpy(&key_pkg.signer_keypairs[i-1].sec, sk_i, 32);
        crypto::secret_key_to_public_key(
            key_pkg.signer_keypairs[i-1].sec, key_pkg.signer_keypairs[i-1].pub);
        key_pkg.signer_public_keys[i-1] = key_pkg.signer_keypairs[i-1].pub;
    }
    return true;
}

// Reconstruct the group public key Y from Lagrange-weighted signer pubkeys.
bool reconstruct_group_pubkey(
    const std::vector<std::pair<uint8_t, crypto::public_key>>& signer_pubkeys,
    crypto::public_key& Y_out)
{
    if (signer_pubkeys.size() < FROST_T) return false;
    std::vector<uint8_t> indices;
    for (const auto& e : signer_pubkeys) indices.push_back(e.first);
    std::vector<crypto::ec_scalar> lambdas;
    if (!compute_lagrange_coeffs(indices, lambdas)) return false;

    ge_p3 acc;
    bool first = true;
    for (size_t i = 0; i < signer_pubkeys.size(); ++i) {
        ge_p3 P;
        if (!point_from_bytes(reinterpret_cast<const unsigned char*>(&signer_pubkeys[i].second), P))
            return false;
        ge_p3 lamP;
        scalar_mult_generic(reinterpret_cast<const unsigned char*>(&lambdas[i]), P, lamP);
        if (first) { acc = lamP; first = false; }
        else { ge_p3 tmp; point_add_p3(acc, lamP, tmp); acc = tmp; }
    }
    point_to_bytes(acc, reinterpret_cast<unsigned char*>(&Y_out));
    return true;
}

// ── Nonces / commitments ──────────────────────────────────────────────────
bool generate_nonces(NoncePair& nonce_out) {
    crypto::secret_key a, b;
    crypto::public_key unused;
    crypto::generate_keys(unused, a);
    crypto::generate_keys(unused, b);
    memcpy(&nonce_out.hiding, &a, 32);
    memcpy(&nonce_out.binding, &b, 32);
    return true;
}

void nonce_commitments(const NoncePair& nonce,
                       crypto::public_key& D_out, crypto::public_key& E_out) {
    ge_p3 Dp, Ep;
    scalar_mult_base(reinterpret_cast<const unsigned char*>(&nonce.hiding), Dp);
    scalar_mult_base(reinterpret_cast<const unsigned char*>(&nonce.binding), Ep);
    ge_p3_tobytes(reinterpret_cast<unsigned char*>(&D_out), &Dp);
    ge_p3_tobytes(reinterpret_cast<unsigned char*>(&E_out), &Ep);
}

// Challenge c = H(R || Y || msg)
static bool compute_challenge(const crypto::public_key& R, const crypto::public_key& Y,
                              const crypto::hash& msg_hash, unsigned char c[32]) {
    std::string in;
    in.append(reinterpret_cast<const char*>(&R), 32);
    in.append(reinterpret_cast<const char*>(&Y), 32);
    in.append(reinterpret_cast<const char*>(msg_hash.data), 32);
    hash_to_scalar_bytes(reinterpret_cast<const unsigned char*>(in.data()), in.size(), c);
    return true;
}

// ── Partial signature ─────────────────────────────────────────────────────
// s_i = d_i + rho_i*e_i + c*lambda_i*sk_i   (mod l)
bool sign_partial(
    const crypto::hash& msg_hash,
    const NoncePair& nonce,
    const crypto::secret_key& share_sk,
    const crypto::ec_scalar& lambda_i,
    const crypto::ec_scalar& rho_i,
    const crypto::public_key& R,
    const crypto::public_key& Y,
    PartialSignature& partial_out)
{
    unsigned char c[32]; compute_challenge(R, Y, msg_hash, c);

    unsigned char rho_e[32];
    sc_mul(rho_e, reinterpret_cast<const unsigned char*>(&rho_i),
                   reinterpret_cast<const unsigned char*>(&nonce.binding));

    // d_i + rho_i*e_i
    unsigned char d_plus_re[32];
    sc_add(d_plus_re, reinterpret_cast<const unsigned char*>(&nonce.hiding), rho_e);

    // c * lambda_i * sk_i
    unsigned char lam_sk[32];
    sc_mul(lam_sk, reinterpret_cast<const unsigned char*>(&lambda_i),
                   reinterpret_cast<const unsigned char*>(&share_sk));
    unsigned char c_lam_sk[32]; sc_mul(c_lam_sk, c, lam_sk);

    unsigned char s_i[32]; sc_add(s_i, d_plus_re, c_lam_sk);
    memcpy(&partial_out.sig_share, s_i, 32);

    // store commitments for aggregate R recomputation & partial verification
    nonce_commitments(nonce, partial_out.D, partial_out.E);
    return true;
}

// ── Per-partial verification ──────────────────────────────────────────────
// check:  s_i*G == D_i + rho_i*E_i + c*lambda_i*P_i
bool verify_partial(
    const crypto::hash& msg_hash,
    const PartialSignature& partial,
    const crypto::ec_scalar& lambda_i,
    const crypto::ec_scalar& rho_i,
    const crypto::public_key& R,
    const crypto::public_key& Y,
    const crypto::public_key& signer_pubkey,
    std::string& error_out)
{
    unsigned char c[32];
    compute_challenge(R, Y, msg_hash, c);

    // LHS = s_i * G
    ge_p3 lhs;
    scalar_mult_base(reinterpret_cast<const unsigned char*>(&partial.sig_share), lhs);

    // rho_i * E_i
    ge_p3 E;
    if (!point_from_bytes(reinterpret_cast<const unsigned char*>(&partial.E), E)) { error_out = "bad E"; return false; }
    ge_p3 rhoE;
    scalar_mult_generic(reinterpret_cast<const unsigned char*>(&rho_i), E, rhoE);

    // D_i + rho_i*E_i
    ge_p3 D;
    if (!point_from_bytes(reinterpret_cast<const unsigned char*>(&partial.D), D)) { error_out = "bad D"; return false; }
    ge_p3 D_plus_rhoE; point_add_p3(D, rhoE, D_plus_rhoE);

    // c * lambda_i * P_i
    unsigned char c_lam[32];
    sc_mul(c_lam, c, reinterpret_cast<const unsigned char*>(&lambda_i));
    ge_p3 P;
    if (!point_from_bytes(reinterpret_cast<const unsigned char*>(&signer_pubkey), P)) { error_out = "bad P"; return false; }
    ge_p3 c_lam_P;
    scalar_mult_generic(c_lam, P, c_lam_P);

    ge_p3 rhs; point_add_p3(D_plus_rhoE, c_lam_P, rhs);

    unsigned char lhs_b[32]; point_to_bytes(lhs, lhs_b);
    unsigned char rhs_b[32]; point_to_bytes(rhs, rhs_b);
    if (memcmp(lhs_b, rhs_b, 32) != 0) {
        error_out = "partial signature check failed";
        return false;
    }
    return true;
}

// ── Aggregate nonce commitment R = sum(D_i + rho_i*E_i) ───────────────────
bool compute_aggregate_r(
    const std::vector<std::pair<crypto::public_key, crypto::public_key>>& commitments,
    const std::vector<crypto::ec_scalar>& rho,
    crypto::public_key& R_out)
{
    if (commitments.size() != rho.size() || commitments.empty()) return false;
    ge_p3 Racc;
    bool first = true;
    for (size_t i = 0; i < commitments.size(); ++i) {
        ge_p3 D, E;
        if (!point_from_bytes(reinterpret_cast<const unsigned char*>(&commitments[i].first), D)) return false;
        if (!point_from_bytes(reinterpret_cast<const unsigned char*>(&commitments[i].second), E)) return false;
        ge_p3 rhoE;
        scalar_mult_generic(reinterpret_cast<const unsigned char*>(&rho[i]), E, rhoE);
        ge_p3 D_plus; point_add_p3(D, rhoE, D_plus);
        if (first) { Racc = D_plus; first = false; }
        else { ge_p3 tmp; point_add_p3(Racc, D_plus, tmp); Racc = tmp; }
    }
    point_to_bytes(Racc, reinterpret_cast<unsigned char*>(&R_out));
    return true;
}

// ── Aggregate ─────────────────────────────────────────────────────────────
// R = sum(D_i + rho_i*E_i);  z = sum(s_i).  R is recomputed from commitments,
// never trusted from any partial.
bool aggregate_signatures(
    const crypto::hash& msg_hash,
    const std::vector<uint8_t>& signer_indices,
    const std::vector<PartialSignature>& partials,
    const std::vector<crypto::ec_scalar>& rho,
    const crypto::public_key& Y,
    FrostSignature& sig_out)
{
    const size_t S = signer_indices.size();
    if (S < FROST_T || partials.size() < S || rho.size() < S) return false;

    std::vector<crypto::ec_scalar> lambda_coeffs;
    if (!compute_lagrange_coeffs(signer_indices, lambda_coeffs) || lambda_coeffs.size() < S)
        return false;

    // Aggregate R
    std::vector<std::pair<crypto::public_key, crypto::public_key>> comms;
    for (size_t i = 0; i < S; ++i) comms.push_back({partials[i].D, partials[i].E});
    if (!compute_aggregate_r(comms, rho, sig_out.R)) return false;

    // Verify each partial against the freshly-computed aggregate R so a
    // corrupted signer/coordinator cannot inject a bogus R.
    for (size_t i = 0; i < S; ++i) {
        if (partials[i].signer_index != signer_indices[i]) return false;
        const uint8_t idx = partials[i].signer_index;
        if (idx < 1 || idx > FROST_N) return false;
        std::string perr;
        if (!verify_partial(msg_hash, partials[i], lambda_coeffs[i], rho[i],
                            sig_out.R, Y, CONSENSUS_PROPOSER_PUBKEYS[idx - 1], perr))
            return false;
    }

    // Aggregate z = sum(s_i)
    unsigned char z[32]; memset(z, 0, 32);
    for (size_t i = 0; i < S; ++i) {
        unsigned char tmp[32];
        sc_add(tmp, z, reinterpret_cast<const unsigned char*>(&partials[i].sig_share));
        memcpy(z, tmp, 32);
    }
    memcpy(&sig_out.z, z, 32);
    sig_out.msg_hash = msg_hash;
    return true;
}

// ── Verify aggregate FROST signature ──────────────────────────────────────
// z*G == R + c*Y ;  c = H(R || Y || msg)
bool verify_signature(
    const FrostSignature& sig,
    const PublicKeyPackage& pkg,
    std::string& error_out)
{
    unsigned char c[32];
    compute_challenge(sig.R, pkg.agg_pubkey, sig.msg_hash, c);

    // z*G
    ge_p3 zG;
    scalar_mult_base(reinterpret_cast<const unsigned char*>(&sig.z), zG);

    // c*Y
    ge_p3 Yp;
    if (!point_from_bytes(reinterpret_cast<const unsigned char*>(&pkg.agg_pubkey), Yp)) {
        error_out = "bad group pubkey"; return false;
    }
    ge_p3 cY; scalar_mult_generic(c, Yp, cY);

    // R + c*Y
    ge_p3 Rp;
    if (!point_from_bytes(reinterpret_cast<const unsigned char*>(&sig.R), Rp)) {
        error_out = "bad R"; return false;
    }
    ge_p3 rhs; point_add_p3(Rp, cY, rhs);

    unsigned char lhs_b[32]; point_to_bytes(zG, lhs_b);
    unsigned char rhs_b[32]; point_to_bytes(rhs, rhs_b);
    if (memcmp(lhs_b, rhs_b, 32) != 0) {
        error_out = "aggregate FROST signature verification failed";
        return false;
    }
    return true;
}

// ── Message hash ──────────────────────────────────────────────────────────
// msg = H(FROST_DOMAIN || height(LE64) || period(LE32)
//         || for each output sorted by (spend_key, amount): spend_key(32) || amount(LE64))
// Uses spend keys ONLY (exactly what the 0xAA record stores on-chain) so the
// signing node and every validating node derive identical bytes.
crypto::hash create_distribution_message_hash(
    uint64_t height,
    uint32_t period,
    const std::vector<std::pair<crypto::public_key, uint64_t>>& outputs)
{
    auto sorted = outputs;
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) {
            int cmp = memcmp(&a.first, &b.first, 32);
            if (cmp != 0) return cmp < 0;
            return a.second < b.second;
        });

    std::string msg;
    msg.append(FROST_DOMAIN, strlen(FROST_DOMAIN));
    msg.append(reinterpret_cast<const char*>(&height), 8);
    msg.append(reinterpret_cast<const char*>(&period), 4);
    for (const auto& [pk, amount] : sorted) {
        msg.append(reinterpret_cast<const char*>(&pk), 32);
        msg.append(reinterpret_cast<const char*>(&amount), 8);
    }
    return crypto::cn_fast_hash(msg.data(), msg.size());
}

// ── Encode / Decode ───────────────────────────────────────────────────────
bool encode_frost_signature(const FrostSignature& sig, std::vector<uint8_t>& out) {
    out.clear();
    out.resize(96);
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

// ── Consensus keys (ceremony output, hardcoded) ───────────────────────────
// Generated offline by `gov_crypto frost-keygen` on 2026-09-23; NEVER derived
// at runtime.  Group pubkey Y = sk*G and signer pubkeys P_i = sk_i*G.
// The matching PRIVATE shares live with the 5 operators (test deployment).
// Re-run `gov_crypto frost-keygen` atomically on ALL nodes to rotate.
static const std::array<crypto::public_key, FROST_N> CEREMONY_SIGNER_KEYS = []() {
    static const unsigned char P[FROST_N][32] = {
        {0x59,0x60,0x65,0x0d,0xb7,0x5d,0x06,0x6d,0x66,0xbe,0xf4,0x5e,0x4f,0x61,0xbb,0x1c,0x75,0x09,0xff,0x24,0xad,0x21,0xc5,0xda,0x39,0xea,0x96,0x13,0x1b,0x4e,0x89,0x9b},
        {0xb8,0x71,0xec,0x32,0x3d,0x2f,0x5d,0x58,0x0b,0x51,0xda,0xce,0x44,0x2b,0x4d,0x2e,0x94,0x6c,0x09,0xc5,0x37,0x96,0xd2,0x48,0x6a,0xae,0x83,0x19,0x86,0xbd,0x6a,0x95},
        {0xc7,0x03,0xc4,0xf0,0x90,0x9d,0xe2,0xfa,0x0f,0xe8,0x15,0xb4,0x69,0xb1,0x21,0xb8,0x4b,0x46,0xf6,0x64,0xaf,0x65,0xe5,0x3e,0x8f,0x27,0x12,0x9c,0x4c,0x84,0x0d,0xde},
        {0x97,0xd2,0x17,0x8d,0x90,0xe7,0xae,0x8c,0xa6,0x68,0x3a,0xd6,0x3f,0x30,0x59,0x57,0x76,0xc8,0xe3,0x61,0xc7,0xd7,0xd2,0x6e,0x81,0x49,0xfe,0xab,0x2f,0x76,0x75,0xef},
        {0x46,0xb2,0x63,0x80,0xfe,0x3b,0xdf,0x2e,0xfb,0xac,0x43,0xe4,0x78,0x10,0x29,0x89,0xd2,0x99,0xd7,0x89,0x71,0xa1,0x50,0x38,0xe5,0x6b,0x93,0xed,0xf8,0xb5,0x9b,0x03},
    };
    std::array<crypto::public_key, FROST_N> keys{};
    for (size_t i = 0; i < FROST_N; ++i)
        memcpy(keys[i].data, P[i], 32);
    return keys;
}();

static const crypto::public_key CEREMONY_GROUP_KEY = []() {
    static const unsigned char Y[32] = {
        0xb0,0x18,0x0b,0x6b,0x95,0x60,0x46,0xb8,0x9f,0x02,0x3f,0xc1,0xe8,0x02,0x2e,0x40,0x13,0xe6,0x8b,0x75,0x49,0x22,0xcf,0x1d,0x28,0xea,0x55,0xc2,0xb9,0x3d,0x04,0x06};
    crypto::public_key y;
    memcpy(y.data, Y, 32);
    return y;
}();

// Consensus ceremony keys (fixed, same on every node)
const std::array<crypto::public_key, FROST_N> CONSENSUS_PROPOSER_PUBKEYS = CEREMONY_SIGNER_KEYS;
const crypto::public_key CONSENSUS_GROUP_PUBKEY = CEREMONY_GROUP_KEY;

} // namespace frost
} // namespace mevatrust
} // namespace cryptonote