// Copyright (c) 2024, The Mevacoin Project
// mevatrust_coinbase_validator.cpp
//
// Security: validates MevaTrust outputs in every received block.
// A miner that omits or falsifies these outputs has the block rejected.
//
// AGGIORNAMENTO [2026-06-13]:
//   - Pool on-chain: valida vout pool (3% hardcoded, indirizzo deterministico)
//   - State root 0xA7 include pool_balance per fork detection

#include "mevatrust_coinbase_validator.h"
#include "mevatrust_manager.h"
#include "mevatrust_tx_parser.h"
#include "pool_distribution.h"
#include "pool_address.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_config.h"
#include "crypto/crypto.h"
#include "ringct/rctTypes.h"
#include "device/device.hpp"
#include "misc_log_ex.h"
#include "string_tools.h"
#include <algorithm>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.validator"

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

  // 2. Retrieve expected MevaTrust outputs (node rewards from pool distribution)
  uint64_t expected_miner_reward = 0;
  std::vector<NodeCoinbaseReward> expected =
    build_expected_mevatrust_outputs(height, total_reward, expected_miner_reward);

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

  // 4. Parse pool distribution tag 0xAA from tx_extra
  tx_extra_mevatrust_pool_distribution dist;
  bool has_dist = mevatrust::parse_mevatrust_pool_distribution_from_tx(miner_tx, dist);

  // If no node rewards expected (distribution period not active)
  if (expected.empty()) {
    // Tag 0xAA must NOT be present when no distribution is expected
    if (has_dist) {
      error_msg = "Unexpected pool distribution tag 0xAA at height " + std::to_string(height);
      MERROR(error_msg);
      return false;
    }
    MDEBUG("Coinbase OK: no pool distribution expected at h=" << height);
    return true;
  }

  // Node rewards expected => tag 0xAA MUST be present
  if (!has_dist) {
    error_msg = "Missing pool distribution tag 0xAA at height " + std::to_string(height);
    MERROR(error_msg);
    return false;
  }

  // 5. Verify pool output exists (3% pool contribution) — created by construct_miner_tx_with_mevatrust
  constexpr uint32_t POOL_FRACTION = 3;
  uint64_t expected_pool_amount = total_reward * POOL_FRACTION / 100;
  if (expected_pool_amount > 0) {
    bool pool_found = false;
    for (const auto& out : miner_tx.vout) {
      if (out.amount == expected_pool_amount) {
        pool_found = true;
        break;
      }
    }
    if (!pool_found) {
      error_msg = std::string("Pool output missing: expected ") + std::to_string(expected_pool_amount)
                + " at height " + std::to_string(height);
      MERROR(error_msg);
      return false;
    }
  }

  // 6. Validate pool distribution via existing validator
  auto* pm = mevatrust::get_manager();
  if (!pm) {
    error_msg = "MevaTrust manager not initialized";
    MERROR(error_msg);
    return false;
  }

  uint64_t pool_balance = pm->compute_pool_balance_from_chain();
  uint32_t period = static_cast<uint32_t>(height / pm->period_length());

  mevatrust::ProposerState proposers;
  for (size_t i = 0; i < mevatrust::frost::FROST_N; ++i) {
    proposers.pubkeys[i] = mevatrust::frost::CONSENSUS_PROPOSER_PUBKEYS[i];
  }

  if (!mevatrust::validate_pool_distribution(dist, height, period, pool_balance, proposers, error_msg)) {
    MERROR("Pool distribution validation failed: " << error_msg);
    return false;
  }

  MDEBUG("MevaTrust coinbase OK: pool distribution validated at h=" << height);
  return true;
}

} // namespace cryptonote