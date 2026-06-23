#include "network_fund.h"
#include "misc_log_ex.h"
#include <sstream>
#include <algorithm>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "network_fund"

namespace cryptonote
{

static void serialize_spend_event(const network_spend_event& e, std::ostream& os)
{
    os.write(reinterpret_cast<const char*>(&e.height), sizeof(e.height));
    os.write(reinterpret_cast<const char*>(&e.amount), sizeof(e.amount));
}

static bool deserialize_spend_event(std::istream& is, network_spend_event& e)
{
    is.read(reinterpret_cast<char*>(&e.height), sizeof(e.height));
    is.read(reinterpret_cast<char*>(&e.amount), sizeof(e.amount));
    return !is.fail();
}

static bool serialize_state(const network_fund_state& state, std::string& out)
{
    std::ostringstream oss;
    oss.write(reinterpret_cast<const char*>(&state.balance), sizeof(state.balance));
    uint32_t count = static_cast<uint32_t>(state.recent_spends.size());
    oss.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& e : state.recent_spends)
        serialize_spend_event(e, oss);
    out = oss.str();
    return true;
}

static bool deserialize_state(const std::string& in, network_fund_state& state)
{
    if (in.size() < sizeof(uint64_t) + sizeof(uint32_t))
        return false;
    std::istringstream iss(in);
    iss.read(reinterpret_cast<char*>(&state.balance), sizeof(state.balance));
    uint32_t count = 0;
    iss.read(reinterpret_cast<char*>(&count), sizeof(count));
    state.recent_spends.resize(count);
    for (auto& e : state.recent_spends)
    {
        if (!deserialize_spend_event(iss, e))
            return false;
    }
    return true;
}

bool load_network_fund_state(network_fund_state& state,
    const std::function<bool(const std::string&, std::string&)>& db_get,
    const std::function<bool(const std::string&, const std::string&, const std::string&)>& db_iter)
{
    std::string blob;
    if (!db_get("network_fund_state", blob))
    {
        state.balance = NETWORK_FUND_ALLOCATION;
        state.recent_spends.clear();
        return true;
    }
    return deserialize_state(blob, state);
}

bool save_network_fund_state(const network_fund_state& state,
    const std::function<bool(const std::string&, const std::string&)>& db_set,
    uint64_t current_height)
{
    // Prune old entries outside the window
    network_fund_state pruned;
    pruned.balance = state.balance;
    uint64_t cutoff = current_height >= NETWORK_FUND_WINDOW_BLOCKS
                      ? current_height - NETWORK_FUND_WINDOW_BLOCKS : 0;
    for (const auto& e : state.recent_spends)
    {
        if (e.height >= cutoff)
            pruned.recent_spends.push_back(e);
    }

    std::string blob;
    if (!serialize_state(pruned, blob))
        return false;
    return db_set("network_fund_state", blob);
}

std::string validate_network_fund_spend(
    const network_fund_state& state,
    uint64_t amount,
    uint64_t current_height)
{
    if (amount == 0)
        return "Network fund spend amount cannot be zero";

    if (amount > state.balance)
    {
        std::ostringstream oss;
        oss << "Network fund spend " << amount
            << " exceeds remaining balance " << state.balance;
        return oss.str();
    }

    // Sum spends in the rolling window
    uint64_t cutoff = current_height >= NETWORK_FUND_WINDOW_BLOCKS
                      ? current_height - NETWORK_FUND_WINDOW_BLOCKS : 0;
    uint64_t window_total = 0;
    for (const auto& e : state.recent_spends)
    {
        if (e.height >= cutoff)
            window_total += e.amount;
    }

    if (window_total + amount > NETWORK_FUND_MONTHLY_LIMIT)
    {
        std::ostringstream oss;
        oss << "Network fund: spend " << amount
            << " would exceed monthly limit. Window total: "
            << window_total << ", limit: " << NETWORK_FUND_MONTHLY_LIMIT;
        return oss.str();
    }

    return {};
}

} // namespace cryptonote
