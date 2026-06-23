#include "governance.h"
#include "serialization/binary_utils.h"
#include "misc_log_ex.h"
#include <sstream>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "governance"

namespace cryptonote
{

// ── Serialization helpers ───────────────────────────────────────────────

static bool serialize_state(const governance_state& state, std::string& out)
{
    std::ostringstream oss;
    // Format: count | original_count | balance | threshold | keys...
    uint32_t count = static_cast<uint32_t>(state.signers.size());
    uint32_t orig  = static_cast<uint32_t>(state.original_count);
    oss.write(reinterpret_cast<const char*>(&count), sizeof(count));
    oss.write(reinterpret_cast<const char*>(&orig), sizeof(orig));
    oss.write(reinterpret_cast<const char*>(&state.balance), sizeof(state.balance));
    oss.write(reinterpret_cast<const char*>(&state.threshold), sizeof(state.threshold));
    for (const auto& key : state.signers)
        oss.write(reinterpret_cast<const char*>(key.data), sizeof(key.data));
    out = oss.str();
    return true;
}

static bool deserialize_state(const std::string& in, governance_state& state)
{
    if (in.size() < sizeof(uint32_t) * 2 + sizeof(uint64_t) + sizeof(unsigned int))
        return false;
    std::istringstream iss(in);
    uint32_t count = 0, orig = 0;
    iss.read(reinterpret_cast<char*>(&count), sizeof(count));
    iss.read(reinterpret_cast<char*>(&orig), sizeof(orig));
    iss.read(reinterpret_cast<char*>(&state.balance), sizeof(state.balance));
    iss.read(reinterpret_cast<char*>(&state.threshold), sizeof(state.threshold));
    if (in.size() != sizeof(count) + sizeof(orig) + sizeof(state.balance) + sizeof(state.threshold) + count * sizeof(crypto::public_key))
        return false;
    state.signers.resize(count);
    for (auto& key : state.signers)
        iss.read(reinterpret_cast<char*>(key.data), sizeof(key.data));
    state.original_count = orig;
    return true;
}

bool load_governance_state(governance_state& state, const std::function<bool(const std::string&, std::string&)>& db_get)
{
    std::string blob;
    if (!db_get("governance_state", blob))
    {
        // No governance state yet — initialize genesis state
        state.original_count = GOVERNANCE_ORIGINAL_SIGNERS;
        state.balance = TREASURY_ALLOCATION;
        state.threshold = GOVERNANCE_THRESHOLD;
        return true;
    }
    return deserialize_state(blob, state);
}

bool save_governance_state(const governance_state& state, const std::function<bool(const std::string&, const std::string&)>& db_set)
{
    std::string blob;
    if (!serialize_state(state, blob))
        return false;
    return db_set("governance_state", blob);
}

// ── Signature verification ──────────────────────────────────────────────

bool verify_governance_signatures(
    const crypto::hash& tx_prefix_hash,
    const std::vector<governance_signature>& sigs,
    const std::vector<crypto::public_key>& allowed_signers,
    unsigned int threshold)
{
    if (sigs.size() < threshold)
    {
        MERROR("Governance: not enough signatures (" << sigs.size() << " < " << threshold << ")");
        return false;
    }

    std::set<crypto::public_key> seen;
    unsigned int valid = 0;

    for (const auto& gs : sigs)
    {
        // Check this signer is in the allowed set
        bool found = false;
        for (const auto& allowed : allowed_signers)
        {
            if (allowed == gs.signer_key)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            MERROR("Governance: signer key not in allowed set");
            return false;
        }

        // Check for duplicate signer
        if (!seen.insert(gs.signer_key).second)
        {
            MERROR("Governance: duplicate signer");
            return false;
        }

        // Verify signature
        if (!crypto::check_signature(tx_prefix_hash, gs.signer_key, gs.sig))
        {
            MERROR("Governance: invalid signature from " << gs.signer_key);
            return false;
        }

        ++valid;
    }

    if (valid < threshold)
    {
        MERROR("Governance: only " << valid << " valid signatures, need " << threshold);
        return false;
    }

    return true;
}

// ── Validation functions ────────────────────────────────────────────────

std::string validate_governance_transfer(
    const governance_state& state,
    const tx_extra_governance_transfer& transfer,
    const crypto::hash& tx_prefix_hash)
{
    if (transfer.amount == 0)
        return "Governance transfer amount cannot be zero";

    if (transfer.amount > state.balance)
    {
        std::ostringstream oss;
        oss << "Governance transfer " << transfer.amount
            << " exceeds remaining balance " << state.balance;
        return oss.str();
    }

    if (!verify_governance_signatures(tx_prefix_hash, transfer.signatures,
                                      state.signers, state.threshold))
    {
        return "Governance transfer: invalid signatures";
    }

    return {};
}

std::string validate_governance_add_signer(
    const governance_state& state,
    const tx_extra_governance_add_signer& action,
    const crypto::hash& tx_prefix_hash)
{
    // Check that new signer isn't already present
    for (const auto& existing : state.signers)
    {
        if (existing == action.new_signer_key)
            return "Governance add signer: key already exists";
    }

    if (!verify_governance_signatures(tx_prefix_hash, action.signatures,
                                      state.signers, state.threshold))
    {
        return "Governance add signer: invalid signatures";
    }

    return {};
}

std::string validate_governance_remove_signer(
    const governance_state& state,
    const tx_extra_governance_remove_signer& action,
    const crypto::hash& tx_prefix_hash)
{
    if (action.signer_index >= state.signers.size())
    {
        std::ostringstream oss;
        oss << "Governance remove signer: index " << (int)action.signer_index
            << " out of range (0.." << state.signers.size() - 1 << ")";
        return oss.str();
    }

    // Can't remove original genesis signers
    if (action.signer_index < state.original_count)
    {
        std::ostringstream oss;
        oss << "Governance remove signer: cannot remove genesis signer at index "
            << (int)action.signer_index;
        return oss.str();
    }

    if (!verify_governance_signatures(tx_prefix_hash, action.signatures,
                                      state.signers, state.threshold))
    {
        return "Governance remove signer: invalid signatures";
    }

    return {};
}

} // namespace cryptonote
