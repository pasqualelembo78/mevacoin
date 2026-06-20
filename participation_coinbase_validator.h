// Copyright (c) 2024, The Mevacoin Project
// participation_coinbase_validator.h
// DROP IN: src/cryptonote_core/participation/
//
// AGGIORNAMENTO [2026-06-07]:
//   Aggiunta dichiarazione construct_miner_tx_with_participation()
//   Chiamata da blockchain.cpp L1787+L1803 — implementazione in .cpp

#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "participation_types.h"
#include "cryptonote_basic/cryptonote_basic.h"

namespace cryptonote {

constexpr uint8_t HF_VERSION_PARTICIPATION_VALIDATION = 13;

/// Validates participation outputs in a received block's miner_tx.
/// Called from Blockchain::check_miner_transaction() for HF13+.
bool check_participation_coinbase(
  const transaction& miner_tx,
  uint64_t           height,
  uint64_t           total_reward,
  uint8_t            hf_version,
  std::string&       error_msg
);

/// Returns expected participation outputs for unit testing.
std::vector<NodeCoinbaseReward> build_expected_participation_outputs(
  uint64_t  height,
  uint64_t  total_reward,
  uint64_t& miner_reward_out
);

// ─────────────────────────────────────────────────────────────────────────────
// construct_miner_tx_with_participation
//
// Costruisce il coinbase (miner_tx) con split del reward per il sistema
// di incentivi (HF13+). Chiamata da blockchain.cpp::create_block_template_internal().
//
// Parametri:
//   height              — altezza del blocco
//   median_weight       — peso mediano (per get_block_reward interno, non usato
//                         poiche' miner_reward e' gia' calcolato dal chiamante)
//   already_generated_coins — coin emesse fino ad ora
//   current_block_weight — peso del blocco corrente (txs_weight al momento della call)
//   miner_reward        — reward gia' ridotto al 97% dal chiamante
//                         (blockchain.cpp usa get_coinbase_rewards() che modifica
//                          effective_miner_reward in-place prima di chiamare qui)
//   miner_address       — indirizzo stealth del miner
//   node_rewards        — vettore di premi per i nodi attivi (3% totale split)
//                         ogni entry contiene l'indirizzo del nodo e l'importo
//   tx                  — transazione da costruire (output)
//   extra_nonce         — nonce extra opzionale (per mining pool)
//   max_outs            — numero massimo di output (normalmente 1 per HF>=4)
//   hf_version          — versione hard fork corrente
//   snapshot_extra      — blob 0xA2 partecipazione (vuoto se no badge awards)
//
// La funzione NON chiama get_block_reward() internamente: usa miner_reward
// direttamente come importo del primo output. Questo garantisce che la somma
// degli output (miner + nodi) == total_reward originale.
// ─────────────────────────────────────────────────────────────────────────────
bool construct_miner_tx_with_participation(
  uint64_t                              height,
  size_t                                median_weight,
  uint64_t                              already_generated_coins,
  size_t                                current_block_weight,
  uint64_t                              miner_reward,
  const account_public_address&         miner_address,
  const std::vector<NodeCoinbaseReward>& node_rewards,
  transaction&                          tx,
  const blobdata&                       extra_nonce,
  size_t                                max_outs,
  uint8_t                               hf_version,
  const std::vector<uint8_t>&           snapshot_extra
);

} // namespace cryptonote
