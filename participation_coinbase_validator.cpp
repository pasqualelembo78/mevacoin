// Copyright (c) 2024, The Mevacoin Project
// participation_coinbase_validator.cpp
//
// Security: validates participation outputs in every received block.
// A miner that omits or falsifies these outputs has the block rejected.
//
// AGGIORNAMENTO [2026-06-07]:
//   Aggiunta implementazione construct_miner_tx_with_participation()
//   Risolve: undefined reference at link-time in mevacoind build.

#include "participation_coinbase_validator.h"
#include "participation_manager.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/cryptonote_config.h"
#include "crypto/crypto.h"
#include "ringct/rctTypes.h"
#include "device/device.hpp"
#include "misc_log_ex.h"
#include "string_tools.h"
#include <algorithm>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "participation.validator"

namespace cryptonote {

// ─────────────────────────────────────────────────────────────────────────────
// build_expected_participation_outputs
// ─────────────────────────────────────────────────────────────────────────────
std::vector<NodeCoinbaseReward> build_expected_participation_outputs(
  uint64_t  height,
  uint64_t  total_reward,
  uint64_t& miner_reward_out)
{
  auto* pm = participation::get_manager();
  if (!pm || !pm->is_initialized()) return {};
  return pm->get_coinbase_rewards(height, total_reward, miner_reward_out);
}

// ─────────────────────────────────────────────────────────────────────────────
// check_participation_coinbase
// ─────────────────────────────────────────────────────────────────────────────
bool check_participation_coinbase(
  const transaction& miner_tx,
  uint64_t           height,
  uint64_t           total_reward,
  uint8_t            hf_version,
  std::string&       error_msg)
{
  // 1. Only active from HF13
  if (hf_version < HF_VERSION_PARTICIPATION_VALIDATION)
    return true;

  // 2. Retrieve expected participation outputs
  uint64_t expected_miner_reward = 0;
  std::vector<NodeCoinbaseReward> expected =
    build_expected_participation_outputs(height, total_reward, expected_miner_reward);
  if (expected.empty()) return true;

  // 3. Collect non-miner amounts from miner_tx.vout
  std::vector<uint64_t> actual_amounts;
  for (const auto& out : miner_tx.vout)
    if (out.amount != expected_miner_reward)
      actual_amounts.push_back(out.amount);
  std::sort(actual_amounts.begin(), actual_amounts.end());

  // 4. Each expected amount must appear in actual outputs (multi-set match)
  std::vector<uint64_t> expected_amounts;
  for (const auto& r : expected) expected_amounts.push_back(r.amount);
  std::sort(expected_amounts.begin(), expected_amounts.end());

  std::vector<uint64_t> remaining = actual_amounts;
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

  // 5. Total coinbase must not exceed total_reward
  uint64_t sum_actual = 0;
  for (const auto& out : miner_tx.vout) sum_actual += out.amount;
  if (sum_actual > total_reward) {
    error_msg = std::string("Coinbase sum ") + std::to_string(sum_actual)
              + " exceeds total_reward " + std::to_string(total_reward)
              + " at height " + std::to_string(height);
    MERROR(error_msg);
    return false;
  }

  MDEBUG("Participation coinbase OK: " << expected.size()
         << " output(s) verified at h=" << height);
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// construct_miner_tx_with_participation
//
// Costruisce la transazione coinbase (miner_tx) includendo:
//   - Output 0 : miner (miner_reward, gia' ridotto al 97% dal chiamante)
//   - Output 1..N : nodi partecipanti (node_rewards[i].amount per ogni nodo)
//   - Extra     : tx_pub_key + extra_nonce + snapshot_extra (blob 0xA2)
//
// DESIGN SICUREZZA:
//   - Ogni output usa un indirizzo stealth (Diffie-Hellman effimero con txkey)
//   - La tx_pub_key permette ai destinatari di identificare i propri output
//   - Il blob 0xA2 e' opzionale e viene appeso dopo il nonce standard
//   - Non chiamiamo get_block_reward() internamente: usiamo miner_reward
//     direttamente per rispettare il calcolo 97/3 fatto da blockchain.cpp
//
// CONSENSO:
//   - La funzione check_participation_coinbase() (sopra) verifica che ogni
//     blocco ricevuto dalla rete contenga gli output corretti. Un miner che
//     omette o falsifica gli output vede il blocco rifiutato da tutti i nodi.
//
// UNLOCK TIME:
//   - Uguale al normale coinbase: height + CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW
//   - I nodi partecipanti devono attendere lo stesso periodo del miner
//
// NOTE HF VERSION:
//   - hf_version >= 4 : tx.version = 2 (RingCT era, ma coinbase ha amount espliciti)
//   - hf_version < 4  : tx.version = 1 (legacy, improbabile su MevaCoin)
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
  const std::vector<uint8_t>&           snapshot_extra)
{
  // ── Reset transaction ────────────────────────────────────────────────────
  tx.vin.clear();
  tx.vout.clear();
  tx.extra.clear();

  // ── Genera chiave effimera di transazione (tx_pub_key) ───────────────────
  // Usata per derivare gli indirizzi stealth di tutti gli output.
  // Ogni run genera una chiave diversa → output non linkabili tra blocchi.
  keypair txkey = keypair::generate(hw::get_device("default"));
  if (!add_tx_pub_key_to_extra(tx, txkey.pub))
  {
    MERROR("construct_miner_tx_with_participation: failed to add tx_pub_key to extra");
    return false;
  }

  // ── Extra nonce (per mining pool: block template nonce) ──────────────────
  if (!extra_nonce.empty())
  {
    if (!add_extra_nonce_to_tx_extra(tx.extra, extra_nonce))
    {
      MERROR("construct_miner_tx_with_participation: failed to add extra_nonce");
      return false;
    }
  }

  // ── Snapshot blob 0xA2 (badge awards on-chain) ───────────────────────────
  // Appeso dopo il nonce standard. I nodi che ricevono il blocco lo parsano
  // via participation_tx_parser::parse_participation_snapshot_from_tx().
  if (!snapshot_extra.empty())
    tx.extra.insert(tx.extra.end(), snapshot_extra.begin(), snapshot_extra.end());

  // ── Input coinbase (txin_gen) ─────────────────────────────────────────────
  txin_gen in;
  in.height = height;
  tx.vin.push_back(in);

  // ── Output 0: Miner (97% del reward totale) ───────────────────────────────
  // Indirizzo stealth: DH effimero con (miner_address.m_view_public_key, txkey.sec)
  // Output index 0 → usato dalla vista per scan.
  {
    crypto::key_derivation derivation;
    if (!crypto::generate_key_derivation(
          miner_address.m_view_public_key, txkey.sec, derivation))
    {
      MERROR("construct_miner_tx_with_participation: key_derivation failed for miner");
      return false;
    }

    crypto::public_key out_eph_pk;
    if (!crypto::derive_public_key(
          derivation, 0, miner_address.m_spend_public_key, out_eph_pk))
    {
      MERROR("construct_miner_tx_with_participation: derive_public_key failed for miner");
      return false;
    }

    tx_out miner_out;
    miner_out.amount = miner_reward;
    txout_to_key miner_tk;
    miner_tk.key    = out_eph_pk;
    miner_out.target = miner_tk;
    tx.vout.push_back(miner_out);
  }

  // ── Output 1..N: Nodi partecipanti (3% totale, split tra i nodi attivi) ──
  // Ogni nodo ha un indirizzo registrato nella NodeRegistry (LMDB).
  // L'output index e' incrementale (1, 2, ... N) nello stesso txkey stream.
  // Sicurezza: usando lo stesso txkey stream con indici diversi, ogni output
  //            e' separato e non linkabile agli altri dall'esterno.
  {
    size_t out_idx = 1;
    for (const auto& nr : node_rewards)
    {
      if (nr.amount == 0)
      {
        // Salta i nodi con reward zero (potrebbero essere rimossi dal manager,
        // ma meglio gestire il caso per robustezza).
        ++out_idx;
        continue;
      }

      crypto::key_derivation derivation;
      if (!crypto::generate_key_derivation(
            nr.address.m_view_public_key, txkey.sec, derivation))
      {
        MWARNING("construct_miner_tx_with_participation: key_derivation failed for node "
                 << out_idx << ", skipping");
        ++out_idx;
        continue;
      }

      crypto::public_key out_eph_pk;
      if (!crypto::derive_public_key(
            derivation, out_idx, nr.address.m_spend_public_key, out_eph_pk))
      {
        MWARNING("construct_miner_tx_with_participation: derive_public_key failed for node "
                 << out_idx << ", skipping");
        ++out_idx;
        continue;
      }

      tx_out node_out;
      node_out.amount = nr.amount;
      txout_to_key node_tk;
      node_tk.key      = out_eph_pk;
      node_out.target  = node_tk;
      tx.vout.push_back(node_out);
      ++out_idx;
    }
  }

  // ── Versione e unlock time ───────────────────────────────────────────────
  // tx.version = 2 per HF >= 4 (era RingCT).
  // Nota: i coinbase (miner_tx) hanno SEMPRE amount espliciti anche in v2,
  //       non usano RingCT. rct_signatures.type = RCTTypeNull per default.
  tx.version     = (hf_version >= 4) ? 2 : 1;
  tx.unlock_time = height + CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW;

  // rct_signatures e' default-costruita con type=RCTTypeNull — corretto per coinbase.
  // Non serve azzerare esplicitamente.

  MDEBUG("construct_miner_tx_with_participation: h=" << height
         << " miner=" << miner_reward
         << " nodes=" << node_rewards.size()
         << " snapshot_extra=" << snapshot_extra.size() << "B"
         << " total_vout=" << tx.vout.size());

  return true;
}

} // namespace cryptonote
