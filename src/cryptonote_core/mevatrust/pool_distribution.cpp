// Copyright (c) 2024, The Mevacoin Project
// pool_distribution.cpp — On-chain pool distribution validation

#include "pool_distribution.h"
#include "pool_address.h"
#include "mevatrust_tx_parser.h"
#include "misc_log_ex.h"
#include <algorithm>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.pool_dist"

namespace cryptonote {
namespace mevatrust {

std::vector<std::pair<account_public_address, uint64_t>>
build_distribution_outputs(
    const std::vector<NodeCoinbaseReward>& rewards,
    uint64_t total_pool_balance)
{
    // Proportional split with pure integer arithmetic (deterministic across
    // platforms, unlike the float version).  The LAST qualifying reward absorbs
    // the rounding remainder so sum(outputs) == total_pool_balance exactly.
    std::vector<std::pair<account_public_address, uint64_t>> outputs;

    if (rewards.empty() || total_pool_balance == 0) return outputs;

    // Score is each reward's amount; skip zero-amount entries.
    std::vector<std::pair<account_public_address, uint64_t>> q;
    uint64_t total_score = 0;
    for (const auto& r : rewards) {
        if (r.amount == 0) continue;
        q.emplace_back(r.address, r.amount);
        total_score += r.amount;
    }
    if (q.empty()) return outputs;

    uint64_t allocated = 0;
    for (size_t i = 0; i < q.size(); ++i) {
        uint64_t share;
        if (i == q.size() - 1) {
            share = total_pool_balance > allocated ? total_pool_balance - allocated : 0;
        } else {
            share = (total_pool_balance * q[i].second) / total_score;
        }
        if (share > 0 || outputs.empty()) {
            outputs.emplace_back(q[i].first, share);
            allocated += share;
        }
    }

    return outputs;
}

bool construct_pool_distribution_extra(
    uint64_t height,
    uint32_t period,
    uint64_t total_pool_balance,
    const std::vector<NodeCoinbaseReward>& rewards,
    const frost::FrostSignature& signature,
    std::vector<uint8_t>& extra_out)
{
    auto outputs = build_distribution_outputs(rewards, total_pool_balance);
    if (outputs.empty()) {
        MWARNING("[PoolDist] No outputs to distribute");
        return false;
    }
    
    uint64_t total_distributed = 0;
    for (const auto& [addr, amt] : outputs) total_distributed += amt;
    
    if (total_distributed > total_pool_balance) {
        MERROR("[PoolDist] Distributed > pool balance");
        return false;
    }
    
    // Build distribution struct
    tx_extra_mevatrust_pool_distribution dist;
    dist.height = height;
    dist.period = period;
    dist.total_pool_balance = total_pool_balance;
    dist.total_distributed = total_distributed;
    // frost_R is stored as raw 32-byte point bytes; FrostSignature.R is a point.
    memcpy(&dist.frost_R, &signature.R, sizeof(dist.frost_R));
    dist.frost_z = signature.z;
    
    for (const auto& [addr, amt] : outputs) {
        dist.outputs.emplace_back(addr.m_spend_public_key, amt);
    }
    
    // Serialize to extra blob
    return mevatrust::build_mevatrust_pool_distribution_extra(dist, extra_out);
}

bool validate_pool_distribution(
    const tx_extra_mevatrust_pool_distribution& dist,
    uint64_t current_height,
    uint32_t current_period,
    uint64_t current_pool_balance,
    const ProposerState& proposers,
    std::string& error_msg)
{
    // 1. Period check
    if (dist.period != current_period) {
        error_msg = "Pool distribution period mismatch: tx=" + std::to_string(dist.period) +
                    " current=" + std::to_string(current_period);
        MERROR(error_msg);
        return false;
    }
    
    // 2. Height must be at period boundary
    if (dist.height < current_height) {
        error_msg = "Pool distribution from future height";
        MERROR(error_msg);
        return false;
    }
    
    // 3. Sum(outputs) ≤ pool_balance
    if (dist.total_distributed > current_pool_balance) {
        error_msg = "Pool distribution exceeds balance: " +
                    std::to_string(dist.total_distributed) + " > " +
                    std::to_string(current_pool_balance);
        MERROR(error_msg);
        return false;
    }
    
    // 4. Sum(outputs) must match individual outputs
    uint64_t output_sum = 0;
    for (const auto& [addr, amt] : dist.outputs) {
        output_sum += amt;
    }
    if (output_sum != dist.total_distributed) {
        error_msg = "Pool distribution output sum mismatch";
        MERROR(error_msg);
        return false;
    }
    
    // 5. Verify FROST signature
    if (!verify_distribution_signature(dist, proposers)) {
        error_msg = "Pool distribution invalid FROST signature";
        MERROR(error_msg);
        return false;
    }
    
    MINFO("[PoolDist] Distribution validated: period=" << dist.period
          << " total=" << dist.total_distributed
          << " outputs=" << dist.outputs.size());
    return true;
}

bool verify_distribution_signature(
    const tx_extra_mevatrust_pool_distribution& dist,
    const ProposerState& proposers)
{
    // Consensus verification MUST use the ceremony keys.  If the runtime
    // proposer set doesn't match the ceremony set we refuse (they are not the
    // authorized signers).
    for (size_t i = 0; i < frost::FROST_N; ++i) {
        if (memcmp(&proposers.pubkeys[i],
                   &frost::CONSENSUS_PROPOSER_PUBKEYS[i], 32) != 0) {
            MERROR("[PoolDist] Proposer set does NOT match ceremony keys "
                   "- refusing to verify");
            return false;
        }
    }

    frost::PublicKeyPackage pkg;
    pkg.signer_pubkeys = frost::CONSENSUS_PROPOSER_PUBKEYS;
    pkg.agg_pubkey = frost::CONSENSUS_GROUP_PUBKEY;

    frost::FrostSignature sig;
    sig.R = {};
    memcpy(&sig.R, &dist.frost_R, sizeof(dist.frost_R));  // scalar bytes -> point bytes
    sig.z = dist.frost_z;

    // Message hash over the spend-key output list (0xAA stores spend keys only)
    std::vector<std::pair<crypto::public_key, uint64_t>> spend_outputs;
    for (const auto& [pk, amt] : dist.outputs)
        spend_outputs.emplace_back(pk, amt);

    sig.msg_hash = frost::create_distribution_message_hash(
        dist.height, dist.period, spend_outputs);

    std::string error_out;
    return frost::verify_signature(sig, pkg, error_out);
}

} // namespace mevatrust
} // namespace cryptonote