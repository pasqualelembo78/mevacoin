// Copyright (c) 2024, The Mevacoin Project
// frost_threshold.cpp — FROST 3/5 threshold Schnorr signatures for pool distribution
// Uses Monero ed25519 crypto primitives (scalar ops + hash_to_scalar)

#include "frost_threshold.h"
#include "misc_log_ex.h"
#include "string_tools.h"

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.frost"

// Include ref10 scalar operations for ed25519
extern "C" {
#include "crypto/crypto-ops.h"
}

namespace cryptonote {
namespace mevatrust {
namespace frost {

// Hardcoded proposer public keys (governance-defined, transparent)
// First deployment: these are deterministic from domain separation
// In production: set via governance vote
const std::array<crypto::public_key, FROST_N> CONSENSUS_PROPOSER_PUBKEYS = {{
    // Proposer 0: H("mevatrust_proposer_0" || nettype) * G
    // Proposer 1
    // Proposer 2
    // Proposer 3
    // Proposer 4
    // These will be filled with actual governance-elected keys
    crypto::public_key{},
    crypto::public_key{},
    crypto::public_key{},
    crypto::public_key{},
    crypto::public_key{},
}};

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

bool generate_keypairs(
    std::array<SignerKeypair, FROST_N>& keypairs_out,
    PublicKeyPackage& pkg_out)
{
    // Trusted dealer setup (simplified for first deployment)
    // Each signer has a random secret + corresponding public key
    // Aggregate key = sum of all signer public keys (simple, not polynomial)
    // For threshold, we use Lagrange coefficients during signing
    
    // For each signer, generate a random keypair
    for (size_t i = 0; i < FROST_N; ++i) {
        crypto::secret_key& sk = keypairs_out[i].sec;
        crypto::public_key& pk = keypairs_out[i].pub;
        crypto::generate_keys(pk, sk);
        pkg_out.signer_pubkeys[i] = pk;
    }
    
    // For simplicity in v1, aggregate pubkey = first signer pubkey (placeholder)
    // Full implementation would sum all signer pubkeys using curve point addition
    pkg_out.agg_pubkey = keypairs_out[0].pub; // TODO: real sum
    
    // Pre-compute Lagrange coefficients for all signers
    for (size_t i = 0; i < FROST_N; ++i) {
        memset(&pkg_out.lagrange_coeffs[i], 0, 32);
        pkg_out.lagrange_coeffs[i] = *(reinterpret_cast<const crypto::ec_scalar*>(&crypto::null_skey));
        // TODO: compute proper Lagrange for i
    }
    
    return true;
}

bool compute_lagrange_coeffs(
    const std::vector<uint8_t>& signer_indices,
    std::array<crypto::ec_scalar, FROST_N>& coeffs_out)
{
    // Lagrange interpolation: lambda_i = prod_{j != i} (0 - j) / (i - j)
    // All operations mod l (ed25519 subgroup order)
    
    // Zero-out all coeffs first
    for (auto& c : coeffs_out) memset(&c, 0, 32);
    
    if (signer_indices.size() < FROST_T) return false;
    
    for (size_t idx = 0; idx < signer_indices.size(); ++idx) {
        int i = signer_indices[idx];
        if (i < 0 || i >= (int)FROST_N) return false;
        
        // lambda_i = prod_{j != i} (0 - j) / (i - j) mod l
        // For indices 0..4, threshold 3:
        // lambda_i already computed from fixed set
        
        // Simplified: set lambda_i to 1 for all signers (not secure, temporary)
        // Full implementation: proper Lagrange
        unsigned char one[32] = {1};
        bytes_to_scalar(one, coeffs_out[i]);
    }
    
    return true;
}

bool generate_nonces(NoncePair& nonce_out) {
    // Generate two random scalars for hiding (r) and binding (r')
    // r = random scalar mod l
    crypto::secret_key r_hiding, r_binding;
    crypto::public_key unused;
    crypto::generate_keys(unused, r_hiding);
    crypto::generate_keys(unused, r_binding);
    
    memcpy(&nonce_out.hiding, &r_hiding, 32);
    memcpy(&nonce_out.binding, &r_binding, 32);
    
    return true;
}

bool sign_partial(
    const crypto::hash& msg_hash,
    const NoncePair& nonce,
    const crypto::secret_key& sk,
    const crypto::ec_scalar& lagrange_coeff,
    const crypto::ec_scalar& R,
    PartialSignature& partial_out)
{
    // s_i = r_i + c * lambda_i * sk_i
    // where c = H(R || Y || msg)  (FROST challenge)
    
    // Compute challenge c = H(R || agg_pubkey || msg)
    std::string challenge_data;
    challenge_data.append(reinterpret_cast<const char*>(&R), 32);
    challenge_data.append(reinterpret_cast<const char*>(&CONSENSUS_PROPOSER_PUBKEYS[0]), 32);
    challenge_data.append(reinterpret_cast<const char*>(msg_hash.data), 32);
    
    unsigned char c[32];
    hash_to_scalar_bytes(
        reinterpret_cast<const unsigned char*>(challenge_data.data()),
        challenge_data.size(),
        c);
    
    // lambda_i * sk_i (scalar multiplication)
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
    
    // Aggregate z = sum(s_i) for all signers
    unsigned char z[32] = {};
    memset(z, 0, 32);
    
    std::vector<uint8_t> indices;
    for (size_t i = 0; i < partials.size(); ++i) {
        unsigned char s_i[32];
        scalar_to_bytes(partials[i].sig_share, s_i);
        
        // z = z + s_i
        unsigned char tmp[32];
        sc_add(tmp, z, s_i);
        memcpy(z, tmp, 32);
        
        indices.push_back(partials[i].signer_index);
    }
    
    bytes_to_scalar(z, sig_out.z);
    
    // Compute aggregated nonce R = sum(R_i) where R_i = r_i * G + r'_i * B
    // Simplified: R = sum of hiding nonce commitments
    // Full implementation: R = sum(H(c, i) * r_i + H'(c, i) * r'_i)
    unsigned char R[32] = {};
    memset(R, 0, 32);
    
    sig_out.msg_hash = msg_hash;
    
    return true;
}

bool verify_signature(
    const FrostSignature& sig,
    const PublicKeyPackage& pkg)
{
    // Standard Schnorr verification: z * G ?= R + c * Y
    // where c = H(R || Y || msg), Y = agg_pubkey
    
    // Compute challenge c = H(R || Y || msg)
    std::string challenge_data;
    challenge_data.append(reinterpret_cast<const char*>(&sig.R), 32);
    challenge_data.append(reinterpret_cast<const char*>(&pkg.agg_pubkey), 32);
    challenge_data.append(reinterpret_cast<const char*>(sig.msg_hash.data), 32);
    
    unsigned char c[32];
    hash_to_scalar_bytes(
        reinterpret_cast<const unsigned char*>(challenge_data.data()),
        challenge_data.size(),
        c);
    
    // Verify: z * G = R + c * Y
    // Using Monero's check_signature which does sig verification
    crypto::signature monero_sig;
    memcpy(&monero_sig.c, &sig.R, 32);
    memcpy(&monero_sig.r, &sig.z, 32);
    
    // Convert to monero signature format and verify
    bool ok = crypto::check_signature(sig.msg_hash, pkg.agg_pubkey, monero_sig);
    
    if (!ok) {
        MWARNING("[FROST] Signature verification FAILED");
    }
    return ok;
}

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