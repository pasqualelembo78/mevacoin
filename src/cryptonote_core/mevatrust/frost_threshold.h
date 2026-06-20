// Copyright (c) 2024, The Mevacoin Project
// frost_threshold.h — FROST threshold signatures for pool distribution
// t-of-n (3-of-5) threshold Schnorr signatures using ed25519
// Based on FROST (Flexible Round-Optimized Schnorr Threshold) - CFRG draft

#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include <string>
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "cryptonote_basic/cryptonote_basic.h"

namespace cryptonote {
namespace mevatrust {
namespace frost {

// Configuration: 5 proposers, threshold 3
static constexpr size_t FROST_N = 5;
static constexpr size_t FROST_T = 3;

// Domain separation for FROST
static constexpr const char FROST_DOMAIN[] = "mevatrust_frost_pool_dist";

/// Individual signer's long-term keypair
struct SignerKeypair {
    crypto::public_key pub;
    crypto::secret_key sec;
};

/// Partial signature from one signer
struct PartialSignature {
    uint8_t signer_index;           // 0..4
    crypto::ec_scalar hiding_nonce; // r_i (hiding)
    crypto::ec_scalar binding_nonce; // r'_i (binding)
    crypto::ec_scalar sig_share;    // s_i = r_i + c * lambda_i * sk_i
};

/// Aggregated FROST signature (verifiable as standard Schnorr)
struct FrostSignature {
    crypto::ec_scalar R;  // Aggregated nonce commitment
    crypto::ec_scalar z;  // Aggregated signature scalar
    crypto::hash msg_hash; // Message that was signed
};

/// Public key package for verification (aggregated public key + coeffs)
struct PublicKeyPackage {
    crypto::public_key agg_pubkey;           // Aggregated public key
    std::array<crypto::public_key, FROST_N> signer_pubkeys; // Individual pubkeys
    std::array<crypto::ec_scalar, FROST_N> lagrange_coeffs; // lambda_i for each signer
};

/// Generate FROST keypairs for all proposers (run once at setup)
/// Returns array of SignerKeypair and the PublicKeyPackage for verification
bool generate_keypairs(
    std::array<SignerKeypair, FROST_N>& keypairs_out,
    PublicKeyPackage& pkg_out
);

/// Compute Lagrange coefficients for given signer indices
/// Used during signing and verification
bool compute_lagrange_coeffs(
    const std::vector<uint8_t>& signer_indices,
    std::array<crypto::ec_scalar, FROST_N>& coeffs_out
);

/// Round 1: Each signer generates nonces (hiding + binding)
/// Caller must store hiding_nonce and binding_nonce secretly
struct NoncePair {
    crypto::ec_scalar hiding;
    crypto::ec_scalar binding;
};

bool generate_nonces(NoncePair& nonce_out);

/// Round 2: Each signer creates partial signature
/// Input: message hash, nonce pair, signer's secret key, Lagrange coeff, aggregated R
bool sign_partial(
    const crypto::hash& msg_hash,
    const NoncePair& nonce,
    const crypto::secret_key& sk,
    const crypto::ec_scalar& lagrange_coeff,
    const crypto::public_key& agg_pubkey,  // Aggregate public key Y
    const crypto::ec_scalar& R,            // Aggregated R from coordinator
    PartialSignature& partial_out
);

/// Coordinator: aggregate partial signatures into final FROST signature
/// Requires at least FROST_T valid partial signatures
bool aggregate_signatures(
    const crypto::hash& msg_hash,
    const std::vector<PartialSignature>& partials,
    const PublicKeyPackage& pkg,
    FrostSignature& sig_out
);

/// Verify FROST signature (standard Schnorr verification with aggregated pubkey)
bool verify_signature(
    const FrostSignature& sig,
    const PublicKeyPackage& pkg
);

/// Create message hash for pool distribution transaction
/// msg = H(domain || height || period || outputs...)
crypto::hash create_distribution_message_hash(
    uint64_t height,
    uint32_t period,
    const std::vector<std::pair<account_public_address, uint64_t>>& outputs
);

/// Encode FrostSignature to binary for tx extra
bool encode_frost_signature(const FrostSignature& sig, std::vector<uint8_t>& out);

/// Decode FrostSignature from binary
bool decode_frost_signature(const std::vector<uint8_t>& in, FrostSignature& sig_out);

/// Proposer public keys (hardcoded in consensus for transparency)
/// In production: these would be governance-defined or elected
extern const std::array<crypto::public_key, FROST_N> CONSENSUS_PROPOSER_PUBKEYS;

} // namespace frost
} // namespace mevatrust
} // namespace cryptonote