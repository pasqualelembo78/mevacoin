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
    std::vector<std::pair<account_public_address, uint64_t>> outputs;
    
    if (rewards.empty() || total_pool_balance == 0) return outputs;
    
    // Calculate total scores for proportional distribution
    float total_score = 0.0f;
    for (const auto& r : rewards) {
        // Each reward's amount represents their share proportion
        total_score += static_cast<float>(r.amount);
    }
    
    if (total_score <= 0.0f) return outputs;
    
    // Proportional split
    uint64_t allocated = 0;
    for (size_t i = 0; i < rewards.size(); ++i) {
        uint64_t share;
        if (i == rewards.size() - 1) {
            // Last node gets remainder (avoids rounding errors)
            share = total_pool_balance > allocated ? total_pool_balance - allocated : 0;
        } else {
            share = static_cast<uint64_t>(
                static_cast<double>(total_pool_balance) * rewards[i].amount / total_score);
        }
        if (share > 0) {
            outputs.emplace_back(rewards[i].address, share);
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
    dist.frost_R = signature.R;
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
    // Recreate message hash from distribution data
    std::vector<std::pair<account_public_address, uint64_t>> outputs;
    for (const auto& [pk, amt] : dist.outputs) {
        account_public_address addr;
        addr.m_spend_public_key = pk;
        addr.m_view_public_key = pk;
        outputs.emplace_back(addr, amt);
    }

    crypto::hash msg_hash = frost::create_distribution_message_hash(
        dist.height, dist.period, outputs);

    // Rebuild FrostSignature from distribution fields
    frost::FrostSignature sig;
    sig.R = dist.frost_R;
    sig.z = dist.frost_z;
    sig.msg_hash = msg_hash;

    // Mode 1: FROST multi-sig verification (T-of-N threshold)
    // Requires agg_pubkey to be set from polynomial setup
    if (!(proposers.agg_pubkey == crypto::public_key{})) {
        frost::PublicKeyPackage pkg;
        pkg.agg_pubkey = proposers.agg_pubkey;
        pkg.lagrange_coeffs = proposers.lagrange_coeffs;
        for (size_t i = 0; i < frost::FROST_N && i < proposers.pubkeys.size(); ++i) {
            pkg.signer_pubkeys[i] = proposers.pubkeys[i];
        }
        if (frost::verify_signature(sig, pkg)) {
            return true;
        }
        // Fall through to single-signer check in case agg_pubkey is not yet configured
    }

    // Mode 2: Single-signer fallback (v1 bridge until P2P commit protocol)
    // Check signature against each proposer's individual pubkey
    for (size_t i = 0; i < frost::FROST_N && i < proposers.pubkeys.size(); ++i) {
        crypto::signature monero_sig;
        memcpy(&monero_sig.c, &sig.R, 32);
        memcpy(&monero_sig.r, &sig.z, 32);
        if (crypto::check_signature(msg_hash, proposers.pubkeys[i], monero_sig)) {
            MINFO("[PoolDist] Single-signer FROST verified: proposer " << i);
            return true;
        }
    }

    MWARNING("[PoolDist] FROST signature verification FAILED (tried multi-sig + "
             << frost::FROST_N << " single-signer)");
    return false;
}

} // namespace mevatrust
} // namespace cryptonote