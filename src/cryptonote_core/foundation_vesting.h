#pragma once

#include <cstdint>
#include <string>
#include <cstring>
#include <vector>
#include "crypto/crypto.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_config.h"

namespace cryptonote
{

// ── Premine allocation amounts (atomic units) ────────────────────────────
constexpr uint64_t TEAM_LOCK_ALLOCATION       = 200000ULL * COIN; // 200,000 MVC
constexpr uint64_t TREASURY_ALLOCATION        = 400000ULL * COIN; // 400,000 MVC
constexpr uint64_t NETWORK_FUND_ALLOCATION    = 400000ULL * COIN; // 400,000 MVC
constexpr uint64_t PREMINE_TOTAL              = TEAM_LOCK_ALLOCATION + TREASURY_ALLOCATION + NETWORK_FUND_ALLOCATION;

// ── Vesting schedule ────────────────────────────────────────────────────
// At 120s per block: ~720 blocks/day, ~21,600/month, ~259,200/year
constexpr uint64_t TEAM_LOCK_BLOCKS           = 518400; // 24 months

// ── Network fund rate limit ─────────────────────────────────────────────
constexpr uint64_t NETWORK_FUND_MONTHLY_LIMIT = 10000ULL * COIN; // 10,000 MVC per 30-day window
constexpr uint64_t NETWORK_FUND_WINDOW_BLOCKS = 21600;          // ~30 days (720 blocks/day)

// ── Governance constants ────────────────────────────────────────────────
constexpr unsigned int GOVERNANCE_ORIGINAL_SIGNERS = 3;
constexpr unsigned int GOVERNANCE_THRESHOLD        = 2;

// ── Deterministic address derivation (like mevatrust pool_address.h) ───
// These addresses have NO private key — they are protocol-controlled.
// Funds at these addresses can only be spent through governance transactions.

inline crypto::public_key derive_premine_address(const std::string& domain, network_type nettype)
{
    std::string data = domain;
    data.push_back(static_cast<char>(nettype));
    crypto::hash h = crypto::cn_fast_hash(data.data(), data.size());
    crypto::secret_key sk;
    crypto::hash_to_scalar(h.data, 32, sk);
    crypto::public_key spend_key;
    crypto::secret_key_to_public_key(sk, spend_key);
    return spend_key;
}

inline account_public_address get_governance_address(network_type nettype)
{
    crypto::public_key spend_key = derive_premine_address("mevacoin_governance", nettype);
    return account_public_address{spend_key, spend_key};
}

inline account_public_address get_network_fund_address(network_type nettype)
{
    crypto::public_key spend_key = derive_premine_address("mevacoin_network_fund", nettype);
    return account_public_address{spend_key, spend_key};
}

// ── Governance signer keys ──────────────────────────────────────────────
// ███████████████████████████████████████████████████████████████████████
// WARNING: The corresponding PRIVATE keys are NOT stored in this repository.
// They must be kept by the 3 governance operators out-of-band.
// Governance spending (0xB0) requires GOVERNANCE_THRESHOLD (2/3) valid
// signatures. If a private key is ever leaked, rotate IMMEDIATELY before
// any premine spend is broadcast on chain.
// ███████████████████████████████████████████████████████████████████████
inline std::vector<crypto::public_key> get_genesis_governance_signers()
{
    std::vector<crypto::public_key> keys(GOVERNANCE_ORIGINAL_SIGNERS);
    static const unsigned char signer0[32] = {0x2b, 0x4b, 0xc2, 0xec, 0x2b, 0xa0, 0xc9, 0x06, 0xc9, 0x76, 0x30, 0xe6, 0xbc, 0x00, 0xbd, 0x0f, 0x94, 0xec, 0x15, 0xf8, 0x52, 0x87, 0xdb, 0x45, 0xa3, 0xc1, 0x69, 0xc3, 0xcc, 0xf3, 0x8c, 0x50};
    static const unsigned char signer1[32] = {0x5f, 0xfc, 0x6b, 0x4d, 0xd5, 0x96, 0x0e, 0xea, 0x33, 0x77, 0x17, 0xfe, 0x45, 0x24, 0xf6, 0x93, 0x3c, 0xff, 0x64, 0x2e, 0xae, 0x11, 0x4b, 0x46, 0x3c, 0x8f, 0xe7, 0x76, 0x38, 0x98, 0xeb, 0x27};
    static const unsigned char signer2[32] = {0x59, 0x0b, 0xed, 0xad, 0xa0, 0xf2, 0x68, 0x3a, 0x71, 0xee, 0xd8, 0x13, 0xb1, 0x21, 0xa5, 0x51, 0xe2, 0xf0, 0xf4, 0x22, 0x6c, 0x5b, 0x60, 0xde, 0x2b, 0x8a, 0x0d, 0xd0, 0x52, 0x46, 0x39, 0x56};
    memcpy(keys[0].data, signer0, sizeof(keys[0].data));
    memcpy(keys[1].data, signer1, sizeof(keys[1].data));
    memcpy(keys[2].data, signer2, sizeof(keys[2].data));
    return keys;
}

// ── Deterministic secret key derivation ─────────────────────────────────
// Mirrors derive_premine_address() but returns the secret key
inline crypto::secret_key derive_premine_secret_key(const std::string& domain, network_type nettype)
{
    std::string data = domain;
    data.push_back(static_cast<char>(nettype));
    crypto::hash h = crypto::cn_fast_hash(data.data(), data.size());
    crypto::secret_key sk;
    crypto::hash_to_scalar(h.data, 32, sk);
    return sk;
}

// Compute the one-time output SECRET key corresponding to
// compute_premine_output_key().  Needed to generate the key_image
// for spend-detection in check_premine_spend().
inline crypto::secret_key compute_premine_output_secret_key(
    const crypto::secret_key& tx_secret_key,
    const account_public_address& recipient,
    size_t output_index,
    const crypto::secret_key& recipient_spend_secret_key)
{
    crypto::key_derivation derivation;
    crypto::generate_key_derivation(recipient.m_view_public_key, tx_secret_key, derivation);
    crypto::secret_key out_sec;
    crypto::derive_secret_key(derivation, output_index, recipient_spend_secret_key, out_sec);
    return out_sec;
}

// ── Premine output key tracking ────────────────────────────────────────
// Stores the one-time output keys (public + key_image) of genesis premine
// outputs.  These are computed at genesis and used by validation code to
// detect when a premine output is being spent.
struct premine_output_keys
{
    crypto::public_key team;      // 200k team lock
    crypto::public_key treasury;  // 400k treasury governance
    crypto::public_key network;   // 400k network fund
    crypto::key_image treasury_k_image;  // key image of treasury output (for spend detection)
    crypto::key_image network_k_image;   // key image of network fund output (for spend detection)
};

// Compute the one-time output public key given:
//   - tx_secret_key: the foundation tx secret key (hash_to_scalar of FOUNDATION_ADDRESS)
//   - recipient_address: the address the output is sent to
//   - output_index: the index of this output in the transaction's vout array
inline crypto::public_key compute_premine_output_key(
    const crypto::secret_key& tx_secret_key,
    const account_public_address& recipient,
    size_t output_index)
{
    crypto::key_derivation derivation;
    crypto::public_key out_key;
    crypto::generate_key_derivation(recipient.m_view_public_key, tx_secret_key, derivation);
    crypto::derive_public_key(derivation, output_index, recipient.m_spend_public_key, out_key);
    return out_key;
}

} // namespace cryptonote
