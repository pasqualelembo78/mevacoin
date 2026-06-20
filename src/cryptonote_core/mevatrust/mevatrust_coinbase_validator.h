// Copyright (c) 2024, The Mevacoin Project
// mevatrust_coinbase_validator.h
//
// AGGIORNAMENTO [2026-06-07]:
//   Aggiunta dichiarazione construct_miner_tx_with_mevatrust()
//   Chiamata da blockchain.cpp L1787+L1803 — implementazione in .cpp

#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "mevatrust_types.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/blobdatatype.h"

namespace cryptonote {

constexpr uint8_t HF_VERSION_MEVATRUST_VALIDATION = 13;

/// Validates MevaTrust outputs in a received block's miner_tx.
/// Called from Blockchain::check_miner_transaction() for HF13+.
bool check_mevatrust_coinbase(
  const transaction& miner_tx,
  uint64_t           height,
  const crypto::hash& prev_id,
  uint64_t           total_reward,
  uint8_t            hf_version,
  std::string&       error_msg
);

/// Returns expected MevaTrust outputs for unit testing.
std::vector<NodeCoinbaseReward> build_expected_mevatrust_outputs(
  uint64_t  height,
  uint64_t  total_reward,
  uint64_t& miner_reward_out
);

// ─────────────────────────────────────────────────────────────────────────────
// construct_miner_tx_with_mevatrust
//
// Costruisce il coinbase (miner_tx) con split del reward per il sistema
// di incentivi (HF13+). Chiamata da blockchain.cpp::create_block_template_internal().
//
// Parametri:
//   height                 — altezza del blocco
//   median_weight          — peso mediano (per get_block_reward interno)
//   already_generated_coins — coin emesse fino ad ora
//   current_block_weight    — peso del blocco corrente (txs_weight)
//   total_block_reward      — reward TOTALE (base_reward + fees)
//                             Il pool riceve 3% hardcoded, miner 97% - node_rewards
//   miner_address          — indirizzo stealth del miner
//   node_rewards           — vettore di premi per i nodi attivi (da pool, periodico)
//   tx                     — transazione da costruire (output)
//   extra_nonce            — nonce extra opzionale (per mining pool)
//   max_outs               — numero massimo di output (normalmente 1 per HF>=4)
//   hf_version             — versione hard fork corrente
//   snapshot_extra         — blob 0xA2 badge awards (vuoto se no awards)
//   custom_unlock_window   — finestra unlock custom (elite miners)
//   nettype                — MAINNET/TESTNET/STAGENET (per indirizzo pool deterministico)
//
// Pool address = H("mevatrust_pool" || nettype) — NO private key exists.
// ─────────────────────────────────────────────────────────────────────────────
bool construct_miner_tx_with_mevatrust(
  size_t                                height,
  size_t                                median_weight,
  uint64_t                              already_generated_coins,
  size_t                                current_block_weight,
  uint64_t                              total_block_reward,
  const account_public_address&         miner_address,
  const std::vector<NodeCoinbaseReward>& node_rewards,
  transaction&                          tx,
  const blobdata&                       extra_nonce,
  size_t                                max_outs,
  uint8_t                               hf_version,
  const std::vector<uint8_t>&           snapshot_extra,
  uint64_t                              custom_unlock_window,
  network_type                          nettype
);

} // namespace cryptonote



