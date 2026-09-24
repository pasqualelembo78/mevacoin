// Copyright (c) 2024, The Mevacoin Project
// frost_threshold.h — FROST t-of-n threshold Schnorr signatures for pool distribution
//
// Correct CFRG-FROST (draft-irtf-cfrg-frost) with real Shamir secret sharing.
//
//   * N = 5 signer slots, threshold T = 3.
//   * Each signer holds a Shamir share sk_i of a master secret sk, where the share
//     is the polynomial evaluated at x = i (i in 1..N, NEVER 0).  Interpolating the
//     polynomial at x = 0 yields the master secret.
//   * The group public key is Y = sk * G.  Individual shares satisfy P_i = sk_i * G.
//   * Round 1: each signer publishes hiding commitment D_i = d_i*G and binding
//     commitment E_i = e_i*G.
//   * Round 2: the coordinator publishes binding factors rho_i = H(i, msg, R_D, R_E)
//     and the aggregate R = sum(D_i + rho_i*E_i).  Each signer produces
//         s_i = d_i + rho_i*e_i + c*lambda_i*sk_i  (mod l)
//     with c = H(R || Y || msg) and lambda_i the Lagrange coefficient of signer i
//     for interpolation at x = 0 over the participating set.
//   * Aggregate (z = sum s_i) verifies as a plain Schnorr signature:  z*G == R + c*Y.
//
// This module is the ONLY place where the math lives; everything else (P2P,
// broadcaster, coinbase validator) consumes these primitives.  No party ever
// derives any signer secret from public data — the 5 secrets are produced by a
// one-time ceremony (see frost_keygen/`gov_crypto frost-keygen`).
//
// Uses Monero ed25519 crypto primitives (scalar ops + ge ops + hash_to_scalar).

#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include <string>

#include "crypto/crypto.h"
#include "crypto/hash.h"

namespace cryptonote {
namespace mevatrust {
namespace frost {

// Configuration: 5 proposers, threshold 3
static constexpr size_t FROST_N = 5;
static constexpr size_t FROST_T = 3;

// Domain separation for FROST
static constexpr const char FROST_DOMAIN[] = "mevatrust_frost_pool_dist_v2";
static constexpr const char FROST_BINDING_DOMAIN[] = "mevatrust_frost_binding_v2";

/// Individual signer's long-term keypair (one Shamir share)
struct SignerKeypair {
    crypto::public_key  pub;
    crypto::secret_key  sec;
    uint8_t             index;   // 1..FROST_N (the polynomial x-coordinate)
};

/// Result of a one-time FROST key ceremony (dealer output).
struct FrostKeyPackage {
    crypto::public_key              group_public_key;                  // Y = sk*G
    std::array<crypto::public_key, FROST_N> signer_public_keys;        // P_i = sk_i*G, index 1..N
    std::array<SignerKeypair, FROST_N>      signer_keypairs;           // private shares (contains secrets!)
};

/// Partial signature / response from one signer
struct PartialSignature {
    uint8_t             signer_index;    // 1..FROST_N
    crypto::public_key  D;               // hiding commitment d_i*G
    crypto::public_key  E;               // binding commitment e_i*G
    crypto::ec_scalar   sig_share;       // s_i = d_i + rho_i*e_i + c*lambda_i*sk_i
};

/// Aggregated FROST signature (verifiable as standard Schnorr)
struct FrostSignature {
    crypto::public_key  R;   // aggregated nonce commitment  sum(D_i + rho_i*E_i)
    crypto::ec_scalar   z;   // aggregated signature scalar
    crypto::hash        msg_hash;  // message that was signed
};

/// Public key package for verification
struct PublicKeyPackage {
    crypto::public_key agg_pubkey;   // group public key Y
    std::array<crypto::public_key, FROST_N> signer_pubkeys;  // P_i (index 1..N)
};

/// One-time trusted setup: generates master secret, Shamir polynomial of degree
/// T-1, the N shares and the group public key Y.  `key_pkg.signer_keypairs`
/// contains all secret shares — MUST be distributed to the N operators and then
/// destroyed/deleted by the dealer.
bool frost_keygen(FrostKeyPackage& key_pkg);

/// Reconstruct the group public key Y from a subset of signer pubkeys P_i and the
/// Lagrange coefficients for interpolation at x = 0 over that subset.  Used for
/// cross-checking Y (this equals the master Y for any subset of size >= T).
bool reconstruct_group_pubkey(
    const std::vector<std::pair<uint8_t, crypto::public_key>>& signer_pubkeys,
    crypto::public_key& Y_out);

/// Lagrange coefficients for interpolation at x = 0 over `signer_indices` (each in
/// 1..N).  These are the correct, non-degenerate coefficients: the index set
/// NEVER contains 0.  lambda_i = prod_{j != i} (0 - j) / (i - j)  (mod l).
bool compute_lagrange_coeffs(
    const std::vector<uint8_t>& signer_indices,
    std::vector<crypto::ec_scalar>& coeffs_out);  // same order as signer_indices

/// Binding factors rho_i = H(i, msg, commitments) as in CFRG-FROST.  Inputs:
///   signer_indices, msg (32-byte hash), list of (D_i, E_i) pairs for those
///   signers, in the same order.
bool compute_binding_factors(
    const std::vector<uint8_t>& signer_indices,
    const crypto::hash& msg,
    const std::vector<std::pair<crypto::public_key, crypto::public_key>>& commitments,
    std::vector<crypto::ec_scalar>& rho_out);  // same order as signer_indices

/// Round 1: each signer generates hiding/binding nonces.
struct NoncePair {
    crypto::ec_scalar hiding;  // d_i
    crypto::ec_scalar binding; // e_i
};

bool generate_nonces(NoncePair& nonce_out);

/// Compute this signer's commitments D_i = d_i*G, E_i = e_i*G.
void nonce_commitments(const NoncePair& nonce,
                       crypto::public_key& D_out,
                       crypto::public_key& E_out);

/// Coordinator: compute the aggregate nonce commitment
///   R = sum(D_i + rho_i*E_i)
/// from commitments and binding factors in matching order.
bool compute_aggregate_r(
    const std::vector<std::pair<crypto::public_key, crypto::public_key>>& commitments,
    const std::vector<crypto::ec_scalar>& rho,
    crypto::public_key& R_out);

/// Round 2: create partial signature.  Requires the signer's share secret, its
/// Lagrange coefficient lambda_i for the participating set, its binding factor
/// rho_i, the aggregate R and the group pubkey Y.
bool sign_partial(
    const crypto::hash& msg_hash,
    const NoncePair& nonce,
    const crypto::secret_key& share_sk,       // sk_i
    const crypto::ec_scalar& lambda_i,
    const crypto::ec_scalar& rho_i,
    const crypto::public_key& R,              // aggregate nonce commitment
    const crypto::public_key& Y,              // group public key
    PartialSignature& partial_out);

/// Verify a single partial signature against the signer's public key P_i.
///   check: s_i*G == D_i + rho_i*E_i + c*lambda_i*P_i   with c = H(R || Y || msg)
/// `R` is the aggregate nonce the coordinator computed; verifying against it
/// keeps the partial consistent with the group round.
bool verify_partial(
    const crypto::hash& msg_hash,
    const PartialSignature& partial,
    const crypto::ec_scalar& lambda_i,
    const crypto::ec_scalar& rho_i,
    const crypto::public_key& R,
    const crypto::public_key& Y,
    const crypto::public_key& signer_pubkey,
    std::string& error_out);

/// Coordinator: aggregate partial signatures into a final FROST signature.
/// Requires at least FROST_T valid partials.  R is recomputed from the
/// commitments and binding factors so a corrupted R in any partial is rejected.
bool aggregate_signatures(
    const crypto::hash& msg_hash,
    const std::vector<uint8_t>& signer_indices,                      // same order as partials
    const std::vector<PartialSignature>& partials,
    const std::vector<crypto::ec_scalar>& rho,                       // binding factors, same order
    const crypto::public_key& Y,
    FrostSignature& sig_out);

/// Verify an aggregated FROST signature: z*G == R + c*Y.
bool verify_signature(
    const FrostSignature& sig,
    const PublicKeyPackage& pkg,
    std::string& error_out);

/// Create the canonical message hash for a pool distribution.
/// The message is the CANONICAL byte string that both the signing node and any
/// verifying node derive from the on-chain 0xAA record:
///   msg = H(FROST_DOMAIN || height(LE64) || period(LE32)
///           || for each output sorted by (spend_key, amount):
///              spend_key(32) || amount(LE64))
/// `outputs` carries ONLY the spend public key (the 0xAA record stores
/// spend keys, never view keys), so signer and verifier always agree
/// regardless of any address/view-key on the signing side.
/// The output list is sorted byte-wise before hashing so signer and verifier
/// always agree regardless of input order.
crypto::hash create_distribution_message_hash(
    uint64_t height,
    uint32_t period,
    const std::vector<std::pair<crypto::public_key, uint64_t>>& outputs);

/// Encode FrostSignature to binary for tx extra (R || z || msg_hash).
bool encode_frost_signature(const FrostSignature& sig, std::vector<uint8_t>& out);

/// Decode FrostSignature from binary.
bool decode_frost_signature(const std::vector<uint8_t>& in, FrostSignature& sig_out);

/// Consensus proposer public keys (ceremony output) + group public key Y.
/// Fixed at build time by a one-time ceremony; NEVER derived.
extern const std::array<crypto::public_key, FROST_N> CONSENSUS_PROPOSER_PUBKEYS;
extern const crypto::public_key CONSENSUS_GROUP_PUBKEY;

} // namespace frost
} // namespace mevatrust
} // namespace cryptonote