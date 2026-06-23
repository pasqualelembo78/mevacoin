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

// ── Governance signer keys (placeholder — REPLACE with real keys before deploy) ──
inline std::vector<crypto::public_key> get_genesis_governance_signers()
{
    // ███████████████████████████████████████████████████████████████████████
    // WARNING: Replace these 3 placeholder keys with real wallet public keys.
    // Generate 3 wallets and copy their public spend keys here.
    // Each crypto::public_key is 32 bytes.
    // ███████████████████████████████████████████████████████████████████████
    std::vector<crypto::public_key> keys(GOVERNANCE_ORIGINAL_SIGNERS);
    // Signer 0
    memset(keys[0].data, 0, sizeof(keys[0].data));
    keys[0].data[31] = 0xAA;
    // Signer 1
    memset(keys[1].data, 0, sizeof(keys[1].data));
    keys[1].data[31] = 0xBB;
    // Signer 2
    memset(keys[2].data, 0, sizeof(keys[2].data));
    keys[2].data[31] = 0xCC;
    return keys;
}

// ── Premine output public key tracking ─────────────────────────────────
// Stores the one-time output public keys of genesis premine outputs.
// These are computed at genesis and used by validation code to detect
// when a premine output is being spent.
struct premine_output_keys
{
    crypto::public_key team;      // 200k team lock
    crypto::public_key treasury;  // 400k treasury governance
    crypto::public_key network;   // 400k network fund
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
