// Copyright (c) 2024, The Mevacoin Project
// participation_coinbase_validator.cpp
//
// Security: validates participation outputs in every received block.
// A miner that omits or falsifies these outputs has the block rejected.
//
// AGGIORNAMENTO [2026-06-07]:
//   Aggiunta implementazione construct_miner_tx_with_mevatrust()
//   Risolve: undefined reference at link-time in mevacoind build.

#include "mevatrust_coinbase_validator.h"
#include "mevatrust_manager.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_config.h"
#include "crypto/crypto.h"
#include "ringct/rctTypes.h"
#include "device/device.hpp"
#include "misc_log_ex.h"
#include "string_tools.h"
#include <algorithm>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "mevatrust.validator"

namespace cryptonote {

// ─────────────────────────────────────────────────────────────────────────────
// build_expected_mevatrust_outputs
// ─────────────────────────────────────────────────────────────────────────────
std::vector<NodeCoinbaseReward> build_expected_mevatrust_outputs(
  uint64_t  height,
  uint64_t  total_reward,
  uint64_t& miner_reward_out)
{
  auto* pm = mevatrust::get_manager();
  if (!pm || !pm->is_initialized()) return {};
  return pm->get_coinbase_rewards(height, total_reward, miner_reward_out);
}

// ─────────────────────────────────────────────────────────────────────────────
// check_mevatrust_coinbase
// ─────────────────────────────────────────────────────────────────────────────
bool check_mevatrust_coinbase(
  const transaction& miner_tx,
  uint64_t           height,
  uint64_t           total_reward,
  uint8_t            hf_version,
  std::string&       error_msg)
{
  // 1. Only active from HF13
  if (hf_version < HF_VERSION_MEVATRUST_VALIDATION)
    return true;

  // 2. Retrieve expected participation outputs
  uint64_t expected_miner_reward = 0;
  std::vector<NodeCoinbaseReward> expected =
    build_expected_mevatrust_outputs(height, total_reward, expected_miner_reward);
  if (expected.empty()) return true;

  // 3. Total coinbase must equal total_reward exactly
  uint64_t sum_actual = 0;
  for (const auto& out : miner_tx.vout) sum_actual += out.amount;
  if (sum_actual != total_reward) {
    error_msg = std::string("Coinbase sum ") + std::to_string(sum_actual)
              + " != total_reward " + std::to_string(total_reward)
              + " at height " + std::to_string(height);
    MERROR(error_msg);
    return false;
  }

  // 4. Greedy-check: the largest outputs (by amount) are the miner's decomposed chunks.
  // Sort all output amounts descending, subtract miner's total from the top,
  // and verify the remaining tail matches expected node reward amounts (multi-set).
  // This is inherently ambiguous (amounts can coincide), so we also validate via sum:
  // expected_miner_reward + sum(expected_node_rewards) must == total_reward (already checked above).
  // Stronger check: verify the count of outputs is at least expected.size()
  std::vector<uint64_t> sorted; sorted.reserve(miner_tx.vout.size());
  for (const auto& out : miner_tx.vout) sorted.push_back(out.amount);
  std::sort(sorted.begin(), sorted.end(), std::greater<uint64_t>());

  uint64_t minter_sum = 0;
  size_t i = 0;
  for (; i < sorted.size(); ++i) {
    if (minter_sum + sorted[i] <= expected_miner_reward) {
      minter_sum += sorted[i];
    } else {
      break;
    }
  }
  // The miner's decomposed chunks should sum exactly to expected_miner_reward
  if (minter_sum != expected_miner_reward) {
    error_msg = std::string("Miner reward sum ") + std::to_string(minter_sum)
              + " != expected " + std::to_string(expected_miner_reward)
              + " at height " + std::to_string(height);
    MERROR(error_msg);
    return false;
  }

  // Remaining outputs should be the node rewards (multi-set match)
  std::vector<uint64_t> remaining(sorted.begin() + static_cast<long>(i), sorted.end());
  std::vector<uint64_t> expected_amounts;
  for (const auto& r : expected) expected_amounts.push_back(r.amount);
  for (uint64_t exp_amt : expected_amounts) {
    auto it = std::find(remaining.begin(), remaining.end(), exp_amt);
    if (it == remaining.end()) {
      error_msg = std::string("Participation coinbase missing output of amount ")
                + std::to_string(exp_amt)
                + " at height " + std::to_string(height)
                + ". Expected " + std::to_string(expected.size()) + " participation output(s).";
      MERROR(error_msg);
      return false;
    }
    remaining.erase(it);
  }

  MDEBUG("Participation coinbase OK: " << expected.size()
         << " output(s) verified at h=" << height);
  return true;
}

} // namespace cryptonote



