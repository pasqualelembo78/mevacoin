#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include "cryptonote_basic/cryptonote_basic.h"
#include "foundation_vesting.h"

namespace cryptonote
{

// ── Network fund spend event ────────────────────────────────────────────
struct network_spend_event
{
    uint64_t height;
    uint64_t amount;
};

// ── Network fund state ──────────────────────────────────────────────────
struct network_fund_state
{
    uint64_t balance{NETWORK_FUND_ALLOCATION};
    // Spend events in the rolling 30-day window
    std::vector<network_spend_event> recent_spends;
};

// ── Persistence ─────────────────────────────────────────────────────────
bool load_network_fund_state(network_fund_state& state,
    const std::function<bool(const std::string&, std::string&)>& db_get,
    const std::function<bool(const std::string&, const std::string&, const std::string&)>& db_iter);
bool save_network_fund_state(const network_fund_state& state,
    const std::function<bool(const std::string&, const std::string&)>& db_set,
    uint64_t current_height);

// ── Validation ──────────────────────────────────────────────────────────
// Check if a network fund spend is within the rate limit
// Returns error string or empty on success
std::string validate_network_fund_spend(
    const network_fund_state& state,
    uint64_t amount,
    uint64_t current_height);

} // namespace cryptonote
