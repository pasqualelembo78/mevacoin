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
#include "mevatrust_consensus.h"
#include <boost/variant/get.hpp>
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
namespace {

// ── vout_key_is ───────────────────────────────────────────────────────────────
//
// tx_out::target e' un boost::variant, non una chiave. Confrontarlo con memcmp
// contro una public_key funziona e non significa niente: si confronta il
// contenuto grezzo del variant con 32 byte di chiave, e la disposizione dei byte
// nel variant e' un dettaglio di implementazione.
//
// Il pericolo di questo errore e' che passa. La verifica tornava "diverso" per
// motivi sbagliati, quindi un blocco fraudolento veniva respinto per caso e un
// blocco onesto poteva venire respinto per caso: due bug diversi, un solo
// memcmp. Per questo la chiave si estrae con boost::get e si dice cosa si
// confronta.
bool vout_key_is(const tx_out& vout, const crypto::public_key& expected) {
  const auto* to_key = boost::get<txout_to_key>(&vout.target);
  if (!to_key) return false;   // output a script/tag, non a chiave: non e' un nodo
  return memcmp(&to_key->key, &expected, sizeof(crypto::public_key)) == 0;
}

} // namespace

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

  // 2. La somma della coinbase deve essere esattamente la ricompensa.
  //
  //    Questo e' l'unico controllo di questa funzione, ed e' un invariante
  //    condizionato: vale per OGNI blocco, indipendentemente da chi debba
  //    ricevere i soldi. Non ammette eccezioni e non ha bisogno di sapere chi
  //    sono i destinatari, quindi non puo' sbagliare perche' lo stato locale e'
  //    diverso da quello degli altri nodi.
  uint64_t sum_actual = 0;
  for (const auto& out : miner_tx.vout) sum_actual += out.amount;
  if (sum_actual != total_reward) {
    error_msg = std::string("Coinbase sum ") + std::to_string(sum_actual)
              + " != total_reward " + std::to_string(total_reward)
              + " at height " + std::to_string(height);
    MERROR(error_msg);
    return false;
  }

  // 3. Il contributo del pool (3% di ogni blocco) deve esserci, e all'indirizzo
  //    del pool.
  //
  //    Prima questo controllo cercava una vout con l'importo giusto e basta, e
  //    considerava quello un controllo riuscito. Non lo e': e' una verifica che
  //    l'importo esista da qualche parte, non che sia spendibile dall'indirizzo
  //    del pool. Un miner poteva mandare il 3% a se stesso e la vout "del pool"
  //    non compariva da nessuna parte, ma il controllo passava lo stesso perche'
  //    non guardava la chiave. Qui la chiave si guarda.
  const uint64_t expected_pool_amount =
      total_reward * mevatrust::consensus::NODE_REWARD_PERCENT / 100;
  if (expected_pool_amount > 0) {
    const account_public_address pool_addr = mevatrust::get_pool_address(MAINNET);
    bool pool_found = false;
    for (const auto& out : miner_tx.vout) {
      if (out.amount == expected_pool_amount &&
          vout_key_is(out, pool_addr.m_spend_public_key)) {
        pool_found = true;
        break;
      }
    }
    if (!pool_found) {
      error_msg = std::string("Pool output missing or misdirected at height ")
                + std::to_string(height) + ": expected "
                + std::to_string(expected_pool_amount)
                + " to the pool address";
      MERROR(error_msg);
      return false;
    }
  }

  MDEBUG("Coinbase structure OK at h=" << height);
  return true;
}


// ═══════════════════════════════════════════════════════════════════════════
// check_mevatrust_coinbase_pure_consensus
// ═══════════════════════════════════════════════════════════════════════════
//
// La regola che chiude la porta rimasta aperta.
//
// check_mevatrust_coinbase() verificava che il 3% fosse uscito dalla coinbase e
// che le chiavi delle uscite corrispondessero a quelle dichiarate nel manifesto.
// Il manifesto, pero', era un documento FIRMATO: verificava che chi lo aveva
// scritto fosse nel gruppo dei cinque, non che la lista fosse giusta. Cinque
// possessori di chiavi potevano scrivere un manifesto perfettamente firmato con
// un destinatario a loro scelta, e la verifica lo avrebbe accettato.
//
// Qui non c'e' nessuna firma da accertare. Il consenso ricalcola da solo chi
// doveva ricevere e quanto, confronta il manifesto voce per voce, e chiuderebbe
// la porta anche se il gruppo dei cinque decidesse di fare il furto insieme.
// E' esattamente il punto: l'autorita' non e' in cinque indirizzi, e' nella
// catena.
//
// L'ordine dei controlli non e' decorativo. Si guarda prima QUANTO e' stato
// dichiarato e solo dopo CHI lo incassa: un manifesto bugiardo che somma diverso
// dai numeri che contiene viene respinto come bugiardo, non come un furto, che e'
// la diagnosi giusta per un operatore che ci legge il log.
//
// Un validatore a cui manca lo stato non dice "va bene cosi'": dice che non puo'
// decidere, e rifiuta. Un validatore che accolta quando non sa e' permissivo, non
// tollerante.
bool check_mevatrust_coinbase_pure_consensus(
  const transaction& miner_tx,
  uint64_t           height,
  uint64_t           total_reward,
  uint8_t            /*hf_version*/,
  std::string&       error_msg)
{
  error_msg.clear();

  auto* pm = mevatrust::get_manager();
  if (!pm || !pm->is_initialized()) {
    error_msg = "MevaTrust state unavailable: cannot recompute the 3% rule";
    return false;
  }

  // Il ricalcolo e' una funzione dell'altezza e della ricompensa, non un'azione.
  // Non deve scrivere niente: vedi compute_expected_payouts().
  std::vector<MevaTrustManager::expected_payout> expected;
  std::string recompute_err;
  if (!pm->compute_expected_payouts(height, total_reward, expected, recompute_err)) {
    error_msg = "MevaTrust 3% rule not computable: " + recompute_err;
    return false;
  }

  // ── Quando la lista e' vuota non si pretende nulla ───────────────────────
  //
  // Se nessuno ha diritto a un pagamento, il miner tiene il blocco intero.
  // Pretenderlo comunque bloccherebbe la catena il giorno in cui la moneta si
  // avvia e non c'e' ancora nessuno registrato. La regola dice "questa lista",
  // non "sempre tre per cento": se la lista e' vuota, e' vuota anche in catena.
  if (expected.empty()) {
    tx_extra_mevatrust_pool_distribution stray;
    if (mevatrust::parse_mevatrust_pool_distribution_from_tx(miner_tx, stray) &&
        !stray.outputs.empty()) {
      error_msg = "MevaTrust: the block declares payouts but no node is due any";
      return false;
    }
    return true;
  }

  tx_extra_mevatrust_pool_distribution dist;
  if (!mevatrust::parse_mevatrust_pool_distribution_from_tx(miner_tx, dist)) {
    error_msg = "MevaTrust: the 3% is due but the block declares no manifest";
    return false;
  }

  if (dist.outputs.size() != expected.size()) {
    error_msg = "MevaTrust: the manifest pays " +
                std::to_string(dist.outputs.size()) + " nodes, the rule says " +
                std::to_string(expected.size());
    return false;
  }

  // Il totale dichiarato deve essere la somma delle voci che lo compongono. E' il
  // primo controllo e viene prima di guardare chi incassa: se il manifesto mente
  // sul numero, e' un manifesto bugiardo, e dire "il destinatario e' sbagliato"
  // sarebbe una diagnosi sbagliata.
  uint64_t entries_sum = 0;
  for (const auto& o : dist.outputs) entries_sum += o.second;
  if (entries_sum != dist.total_distributed) {
    error_msg = "MevaTrust: the manifest total (" +
                std::to_string(dist.total_distributed) +
                ") is not the sum of its own entries (" +
                std::to_string(entries_sum) + ")";
    return false;
  }

  // ── I soldi devono essere fisicamente lì ──────────────────────────────────
  //
  // Fin qui abbiamo controllato il manifesto: chi dice di ricevere e quanto. Il
  // manifesto e' un documento, e un documento puo' dire la cosa giusta mentre i
  // soldi vanno altrove: il miner sceglie anche le chiavi-output delle vout, e
  // nulla finora legava quelle scelte ai destinatari dichiarati.
  //
  // Il legame c'e' ed e' un'uguaglianza, non un calcolo: la vout del nodo deve
  // avere esattamente la chiave pubblica registrata del nodo. Non e' ricostruibile
  // e non e' ricostruibile perche' non deve esserlo - un output stealth non e'
  // verificabile da terzi, quindi il protocollo non usa output stealth per i
  // pagamenti ai nodi (vedi construct_miner_tx_with_mevatrust).
  //
  // Il miner non puo' aggirarlo: gli servirebbe una chiave-output che sia insieme
  // spendibile da lui e identica a quella del nodo previsto, e un'equazione del
  // genere non ha soluzioni da scegliere.
  {
    std::string missing;
    bool all_present = true;
    for (const auto& e : expected) {
      bool found = false;
      for (const auto& vout : miner_tx.vout) {
        if (vout.amount == e.amount &&
            vout_key_is(vout, e.address.m_spend_public_key)) {
          found = true;
          break;
        }
      }
      if (!found) {
        all_present = false;
        missing = epee::string_tools::pod_to_hex(e.address.m_spend_public_key).substr(0, 16);
        break;
      }
    }
    if (!all_present) {
      error_msg = "MevaTrust: the 3% is declared to the right node but the coinbase "
                  "has no spendable output to it (node " + missing + ") - the money "
                  "would go somewhere else";
      return false;
    }
  }

  // confronto voce per voce, in ordine canonico
  const auto& d = dist.outputs;
  const auto& e = expected;
  for (size_t i = 0; i < e.size(); ++i) {
    // dist.outputs e' un vector<pair<public_key, uint64_t>>: .first e' la chiave di
    // spesa del nodo, .second e' l'importo.
    if (memcmp(&d[i].first,
               &e[i].address.m_spend_public_key,
               sizeof(crypto::public_key)) != 0) {
      error_msg = "MevaTrust: payout mismatch at position " + std::to_string(i) +
                  " — the recipient is not the node the protocol chose";
      return false;
    }
    if (d[i].second != e[i].amount) {
      error_msg = "MevaTrust: amount mismatch at position " + std::to_string(i) +
                  " - " + std::to_string(d[i].second) + " declared, " +
                  std::to_string(e[i].amount) + " due";
      return false;
    }
  }

  return true;
}

} // namespace cryptonote