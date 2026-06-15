// Copyright (c) 2024, The Mevacoin Project
// pool_distribution.h — On-chain pool distribution via FROST threshold signatures
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/tx_extra.h"
#include "mevatrust_types.h"
#include "frost_threshold.h"

namespace cryptonote {
namespace mevatrust {

/// Proposer set state (who are the active proposers)
struct ProposerState {
    std::array<crypto::public_key, frost::FROST_N> pubkeys;
    uint32_t current_epoch{0};
};

/// Build a pool distribution transaction output set
/// Returns: list of (node_address, amount) to distribute
std::vector<std::pair<account_public_address, uint64_t>>
build_distribution_outputs(
    const std::vector<NodeCoinbaseReward>& rewards,
    uint64_t total_pool_balance
);

/// Construct the full distribution special TX with FROST signature
/// Returns the tx_extra blob ready to embed
bool construct_pool_distribution_extra(
    uint64_t height,
    uint32_t period,
    uint64_t total_pool_balance,
    const std::vector<NodeCoinbaseReward>& rewards,
    const frost::FrostSignature& signature,
    std::vector<uint8_t>& extra_out
);

/// Validate a pool distribution transaction at consensus level
/// Checks:
///   1. sum(outputs) <= pool_balance
///   2. FROST signature verified against proposer set
///   3. Period is correct (distribution only at period boundary)
///   4. No duplicate distribution in same period
bool validate_pool_distribution(
    const tx_extra_mevatrust_pool_distribution& dist,
    uint64_t current_height,
    uint32_t current_period,
    uint64_t current_pool_balance,
    const ProposerState& proposers,
    std::string& error_msg
);

/// Verify FROST signature on distribution data
bool verify_distribution_signature(
    const tx_extra_mevatrust_pool_distribution& dist,
    const ProposerState& proposers
);

} // namespace mevatrust
} // namespace cryptonote