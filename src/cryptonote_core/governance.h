#pragma once

#include <cstdint>
#include <vector>
#include <set>
#include "crypto/crypto.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/tx_extra.h"
#include "foundation_vesting.h"

namespace cryptonote
{

// ── Governance state (persisted to LMDB properties table) ───────────────
struct governance_state
{
  // Current signers (first GOVERNANCE_ORIGINAL_SIGNERS are genesis)
  std::vector<crypto::public_key> signers;
  // Original genesis signer count
  unsigned int original_count{GOVERNANCE_ORIGINAL_SIGNERS};
  // Remaining treasury balance
  uint64_t balance{TREASURY_ALLOCATION};
  // Always 2 (threshold never changes)
  unsigned int threshold{GOVERNANCE_THRESHOLD};
};

// ── Save/load governance state from LMDB properties ─────────────────────
bool load_governance_state(governance_state& state, const std::function<bool(const std::string&, std::string&)>& db_get);
bool save_governance_state(const governance_state& state, const std::function<bool(const std::string&, const std::string&)>& db_set);

// ── Governance validation ───────────────────────────────────────────────

// Verify N-of-M signatures on a transaction prefix hash
bool verify_governance_signatures(
    const crypto::hash& tx_prefix_hash,
    const std::vector<governance_signature>& sigs,
    const std::vector<crypto::public_key>& allowed_signers,
    unsigned int threshold);

// Validate a governance transfer transaction
// Returns error string or empty on success
std::string validate_governance_transfer(
    const governance_state& state,
    const tx_extra_governance_transfer& transfer,
    const crypto::hash& tx_prefix_hash);

// Validate an add-signer governance action
std::string validate_governance_add_signer(
    const governance_state& state,
    const tx_extra_governance_add_signer& action,
    const crypto::hash& tx_prefix_hash);

// Validate a remove-signer governance action
std::string validate_governance_remove_signer(
    const governance_state& state,
    const tx_extra_governance_remove_signer& action,
    const crypto::hash& tx_prefix_hash);

} // namespace cryptonote
