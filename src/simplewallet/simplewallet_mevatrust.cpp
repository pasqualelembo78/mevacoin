// Copyright (c) 2024, The Mevacoin Project
// simplewallet_participation.cpp
//
// FIX [2026-06-07]:
//   L117: pod_to_hex(view_sk) -> pod_to_hex(static_cast<const crypto::ec_scalar&>(view_sk))
//   Motivo: crypto::secret_key = epee::mlocked<tools::scrubbed<ec_scalar>>
//           ha distruttore custom (munlock/zeroing) =>
//           static_assert in pod_to_hex fallisce a compile-time.
//   Soluzione: cast al tipo base ec_scalar (struct { char data[32]; }) che e' POD puro.
//   mlocked e scrubbed ereditano pubblicamente da T => cast e' legale e sicuro.
//
// FIX [2026-06-08]: invoke_http_json -> invoke_http_json_rpc
//   Motivo: il server JSON-RPC su /json_rpc richiede l'envelope
//   {"jsonrpc":"2.0","method":"...","params":{...},"id":"0"}
//   invoke_http_json invia il body JSON senza envelope => "Invalid Request"
//   invoke_http_json_rpc costruisce l'envelope automaticamente.
//
// FIX [2026-06-08]: crypto_sign_ed25519_seed_keypair -> secret_key_to_public_key
//   Motivo: il demone genera la node_signing_key con crypto::generate_keys()
//   (chiave MevaCoin). crypto_sign_ed25519_seed_keypair deriva una chiave
//   Ed25519 DIVERSA dallo stesso seed. Usando secret_key_to_public_key
//   si ottiene la stessa public key del demone, necessaria per PoA.
#include "simplewallet.h"
#include <fstream>
#include <cstdlib>
#include "rpc/mevatrust_rpc_commands.h"
#include "common/scoped_message_writer.h"
#include "cryptonote_core/mevatrust/mevatrust_tx_parser.h"
#include "cryptonote_core/mevatrust/pool_address.h"

// ── Helper: build self-send tx with custom tx_extra and submit ──────────────
static std::string submit_mevatrust_tx(tools::wallet2* w, const std::vector<uint8_t>& extra)
{
    try {
    cryptonote::tx_destination_entry de;
    de.addr   = w->get_account().get_keys().m_account_address;
    de.amount = 1000000000ULL;  // 0.001 MVC — minimo per coprire fee
    de.is_subaddress = false;
    std::vector<cryptonote::tx_destination_entry> dsts = {de};
    std::set<uint32_t> subaddr_indices;

    uint64_t blocks_to_unlock = 0, time_to_unlock = 0;
    uint64_t unlocked = w->unlocked_balance(0, false, &blocks_to_unlock, &time_to_unlock);
    uint64_t total    = w->balance(0, false);
    if (unlocked < 1000000000ULL) {
        tools::fail_msg_writer() << tr("Fondi insufficienti per la TX (occorre ~0.001 MVC sbloccato).");
        tools::fail_msg_writer() << tr("  Balance unlocked: ") << cryptonote::print_money(unlocked);
        tools::fail_msg_writer() << tr("  Balance locked:   ") << cryptonote::print_money(total - unlocked);
        if (blocks_to_unlock > 0)
            tools::fail_msg_writer() << tr("  Blocchi allo sblocco: ~") << blocks_to_unlock
                                     << tr(" (circa ") << time_to_unlock / 60 << tr(" min)");
        return {};
    }

    const size_t fake_outs_count = w->get_min_ring_size() - 1;
    auto ptx_vector = w->create_transactions_2(dsts, fake_outs_count, tools::fee_priority::Normal,
                                                extra, 0, subaddr_indices);
    if (ptx_vector.empty()) {
        tools::fail_msg_writer() << tr("Errore creazione tx (saldo insufficiente per la fee?)");
        return {};
    }
    const crypto::hash tx_hash = cryptonote::get_transaction_hash(ptx_vector[0].tx);
    w->commit_tx(ptx_vector);
    return epee::string_tools::pod_to_hex(tx_hash);
    } catch (const std::exception& e) {
        tools::fail_msg_writer() << tr("ERRORE TX: ") << e.what();
        return {};
    }
}

//! Helper: invia MVC all'indirizzo indicato con tx_extra personalizzato (per acquisti store)
static std::string submit_mevatrust_tx_to(tools::wallet2* w, const std::string& dst_address,
                                           uint64_t amount, const std::vector<uint8_t>& extra)
{
    try {
    cryptonote::address_parse_info info;
    if (!cryptonote::get_account_address_from_str(info, w->nettype(), dst_address)) {
        tools::fail_msg_writer() << tr("Indirizzo destinazione non valido: ") << dst_address;
        return {};
    }
    cryptonote::tx_destination_entry de;
    de.addr   = info.address;
    de.amount = amount;
    de.is_subaddress = info.is_subaddress;
    std::vector<cryptonote::tx_destination_entry> dsts = {de};
    std::set<uint32_t> subaddr_indices;

    uint64_t blocks_to_unlock = 0, time_to_unlock = 0;
    uint64_t unlocked = w->unlocked_balance(0, false, &blocks_to_unlock, &time_to_unlock);
    uint64_t total    = w->balance(0, false);
    uint64_t needed = amount + 1000000000ULL; // amount + fee buffer
    if (unlocked < needed) {
        tools::fail_msg_writer() << tr("Fondi insufficienti per la TX.");
        tools::fail_msg_writer() << tr("  Occorrono: ") << cryptonote::print_money(needed);
        tools::fail_msg_writer() << tr("  Balance unlocked: ") << cryptonote::print_money(unlocked);
        return {};
    }

    const size_t fake_outs_count = w->get_min_ring_size() - 1;
    auto ptx_vector = w->create_transactions_2(dsts, fake_outs_count, tools::fee_priority::Normal,
                                                 extra, 0, subaddr_indices);
    if (ptx_vector.empty()) {
        tools::fail_msg_writer() << tr("Errore creazione tx (saldo insufficiente per la fee?)");
        return {};
    }
    const crypto::hash tx_hash = cryptonote::get_transaction_hash(ptx_vector[0].tx);
    w->commit_tx(ptx_vector);
    return epee::string_tools::pod_to_hex(tx_hash);
    } catch (const std::exception& e) {
        tools::fail_msg_writer() << tr("ERRORE TX: ") << e.what();
        return {};
    }
}
// ─────────────────────────────────────────────────────────────────────────────

namespace cryptonote {
static const char* RESET_COLOR = "\033[0m";
static const char* badge_color(const std::string& n) {
  if (n.find("LONG_UPTIME") != std::string::npos) return "\033[35m";
  if (n.find("STABLE")      != std::string::npos) return "\033[34m";
  if (n.find("CORE")        != std::string::npos) return "\033[33m";
  if (n.find("ACTIVE")      != std::string::npos) return "\033[32m";
  if (n.find("EARLY")       != std::string::npos) return "\033[31m";
  if (n.find("WELCOME")     != std::string::npos) return "\033[33m\033[1m";
  return "\033[36m";
}
bool read_node_pubkey(std::string& node_pk_hex) {
  const char* h = std::getenv("HOME");
  std::vector<std::string> paths = {
    std::string(h ? h : "") + "/.mevacoin/mevatrust/node_signing_key",
    "/root/.mevacoin/mevatrust/node_signing_key",
  };
  for (const auto& kp : paths) {
    std::ifstream kf(kp, std::ios::binary);
    if (!kf.is_open()) continue;
    unsigned char seed[32] = {};
    kf.read(reinterpret_cast<char*>(seed), 32);
    if (kf.gcount() != 32) continue;
    kf.close();
    // Deriva la public key MevaCoin (stessa derivazione usata dal demone)
    crypto::secret_key node_sk;
    memcpy(node_sk.data, seed, 32);
    crypto::public_key node_pk;
    crypto::secret_key_to_public_key(node_sk, node_pk);
    node_pk_hex = epee::string_tools::pod_to_hex(node_pk);
    return true;
  }
  return false;
}
static std::string node_id_path() {
  const char* h = std::getenv("HOME");
  std::string dir = std::string(h ? h : "/root") + "/.mevacoin/mevatrust";
  return dir + "/node_id";
}
static bool read_saved_node_id(std::string& out) {
  std::ifstream f(node_id_path());
  if (!f.is_open()) return false;
  std::getline(f, out);
  return !out.empty();
}
static void save_node_id(const std::string& id) {
  std::ofstream f(node_id_path());
  if (f.is_open()) { f << id; f.close(); }
}
static void remove_node_id() {
  std::remove(node_id_path().c_str());
}
static bool resolve_node_id(std::string& node_id, const std::vector<std::string>& args) {
  if (!args.empty()) { node_id = args[0]; return true; }
  if (read_saved_node_id(node_id)) return true;
  if (read_node_pubkey(node_id)) return true;
  tools::fail_msg_writer() << tr("Nessun node_id fornito e chiave nodo non trovata.");
  tools::fail_msg_writer() << tr("  Specifica un node_id o avvia il daemon per generare ~/.mevacoin/mevatrust/node_signing_key");
  return false;
}
bool simple_wallet::cmd_get_node_status(const std::vector<std::string>& args) {
  std::string node_id;
  if (!resolve_node_id(node_id, args)) return true;
  rpc::COMMAND_RPC_GET_NODE_STATUS::request req;
  rpc::COMMAND_RPC_GET_NODE_STATUS::response res;
  req.node_id = node_id;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "get_node_status", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC get_node_status"); return true; }
  tools::fail_msg_writer() << "\n=== Stato Nodo ===";
  tools::fail_msg_writer() << "Node ID : " << res.node_id;
  tools::fail_msg_writer() << "Attivo  : " << (res.is_active?"SI":"NO");
  tools::fail_msg_writer() << "Sync    : " << (res.is_synced?"SI":"NO");
  tools::fail_msg_writer() << "Visto   : " << res.last_seen << " (unix timestamp)";
  return true;
}
bool simple_wallet::cmd_get_badges(const std::vector<std::string>& args) {
  std::string node_id;
  if (!resolve_node_id(node_id, args)) return true;
  rpc::COMMAND_RPC_GET_BADGES::request req;
  rpc::COMMAND_RPC_GET_BADGES::response res;
  req.node_id = node_id;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "get_badges", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC get_badges"); return true; }
  if (res.badges.empty()) { tools::fail_msg_writer() << tr("Nessun badge per: ") << node_id; return true; }
  tools::fail_msg_writer() << "\n=== Badge del Nodo ===";
  tools::fail_msg_writer() << "Node ID: " << res.node_id;
  for (const auto& b : res.badges)
    tools::fail_msg_writer() << "  " << badge_color(b) << "* " << b << RESET_COLOR;
  tools::fail_msg_writer() << "Totale: " << res.badges.size() << " badge attivi";
  return true;
}
bool simple_wallet::cmd_get_badge_requirements(const std::vector<std::string>& args) {
  std::string node_id;
  if (!resolve_node_id(node_id, args)) return true;
  rpc::COMMAND_RPC_GET_BADGE_REQUIREMENTS::request req;
  rpc::COMMAND_RPC_GET_BADGE_REQUIREMENTS::response res;
  req.node_id = node_id;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "get_badge_requirements", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC get_badge_requirements"); return true; }
  if (res.badges.empty()) { tools::fail_msg_writer() << tr("Nessun badge disponibile per: ") << node_id; return true; }
  tools::fail_msg_writer() << "\n=== Requisiti Badge per " << node_id << " ===";
  uint32_t earned_cnt = 0;
  for (const auto& b : res.badges) {
    tools::msg_writer() << badge_color(b.name) << "--- " << b.name << RESET_COLOR;
    if (!b.description.empty())
      tools::msg_writer() << "    Descrizione: " << b.description;
    if (b.earned) {
      tools::msg_writer() << "    " << "\033[32m[GUADAGNATO]\033[0m";
      ++earned_cnt;
    } else {
      tools::msg_writer() << "    " << "\033[31m[NON GUADAGNATO]\033[0m"
                          << (b.reason.empty() ? "" : " - " + b.reason);
      for (const auto& d : b.details) {
        tools::msg_writer() << "    " << (d.met ? "\033[32m\u2713\033[0m" : "\033[31m\u2717\033[0m")
                            << " " << d.metric << ": " << (d.met ? "" : "")
                            << d.current << " / richiesto " << d.required;
      }
      if (b.details.empty() && b.reason.empty())
        tools::msg_writer() << "    Registra il nodo con: mevatrust register";
    }
  }
  tools::fail_msg_writer() << "Attivi: " << earned_cnt << "/" << res.badges.size();
  return true;
}
bool simple_wallet::cmd_get_uptime(const std::vector<std::string>& args) {
  std::string node_id;
  if (!resolve_node_id(node_id, args)) return true;
  rpc::COMMAND_RPC_GET_NODE_UPTIME::request req;
  rpc::COMMAND_RPC_GET_NODE_UPTIME::response res;
  req.node_id = node_id;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "get_node_uptime", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC get_node_uptime"); return true; }
  tools::fail_msg_writer() << "\n=== Uptime Nodo ===";
  tools::fail_msg_writer() << "Node ID    : " << res.node_id;
  tools::fail_msg_writer() << "Uptime     : " << res.uptime_seconds << " sec (" << (res.uptime_seconds/3600) << " h)";
  tools::fail_msg_writer() << "Percentuale: " << static_cast<int>(res.uptime_percentage*100.0) << "%";
  return true;
}
bool simple_wallet::cmd_get_incentive_history(const std::vector<std::string>& args) {
  std::string node_id;
  if (!resolve_node_id(node_id, args)) return true;
  rpc::COMMAND_RPC_GET_INCENTIVE_HISTORY::request req;
  rpc::COMMAND_RPC_GET_INCENTIVE_HISTORY::response res;
  req.node_id = node_id;
  req.limit   = (args.size()>1) ? static_cast<uint32_t>(std::stoul(args[1])) : 20;
  req.offset  = 0;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "get_incentive_history", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC get_incentive_history"); return true; }
  if (res.entries.empty()) { tools::fail_msg_writer() << tr("Nessun incentivo per: ") << node_id; return true; }
  tools::fail_msg_writer() << "\n=== Storico Incentivi ===";
  tools::fail_msg_writer() << "Node ID: " << res.node_id << " | Totale: " << res.total << " record";
  uint64_t total_reward = 0;
  for (const auto& e : res.entries) {
    if (!e.badge_type.empty()) {
      tools::fail_msg_writer() << badge_color(e.badge_type) << "  [h=" << e.height << "] BADGE " << e.badge_type;
      if (!e.reason.empty()) tools::fail_msg_writer() << "    Motivo: " << e.reason;
      tools::fail_msg_writer() << RESET_COLOR;
    } else if (e.amount>0) {
      tools::fail_msg_writer() << "  [h=" << e.height << "] REWARD " << e.amount << " sat";
      total_reward += e.amount; } }
  if (total_reward > 0)
    tools::fail_msg_writer() << "Totale reward: " << total_reward << " sat";
  return true;
}
bool simple_wallet::cmd_register_node(const std::vector<std::string>& args) {
  try {
  // UX ZERO-ARGOMENTI: basta digitare register_node
  // Tutto automatico: wallet keys, node key da file, firma e tx_extra on-chain
  // 1. Wallet keys
  const crypto::public_key& w_spk =
      m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  const crypto::secret_key& w_ssk =
      m_wallet->get_account().get_keys().m_spend_secret_key;
  const std::string address_str =
      m_wallet->get_account().get_public_address_str(m_wallet->nettype());
  // 2. Node signing key dal file (generato dal daemon)
  std::string node_pk_hex;
  if (!read_node_pubkey(node_pk_hex)) {
    tools::fail_msg_writer() << tr("Errore: chiave nodo non trovata.");
    tools::fail_msg_writer() << tr("  Il daemon deve essere avviato almeno una volta.");
    tools::fail_msg_writer() << tr("  Cerca: ~/.mevacoin/mevatrust/node_signing_key");
    return true;
  }
  crypto::public_key node_pk{};
  if (!epee::string_tools::hex_to_pod(node_pk_hex, node_pk)) {
    tools::fail_msg_writer() << tr("node_pubkey hex non valida"); return true;
  }
  // 3. Computa node_id = H(wallet_pubkey || node_pubkey || timestamp)
  const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
  std::string nid_input;
  nid_input.append(reinterpret_cast<const char*>(&w_spk), sizeof(w_spk));
  nid_input.append(reinterpret_cast<const char*>(&node_pk), sizeof(node_pk));
  nid_input.append(reinterpret_cast<const char*>(&now), sizeof(now));
  const crypto::hash node_id = crypto::cn_fast_hash(nid_input.data(), nid_input.size());
  // 4. Firma: sign(H(node_id || node_pubkey)) con wallet spend key
  const crypto::hash msg_hash = cryptonote::mevatrust::registration_message_hash(node_id, node_pk);
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);
  // 5. Costruisce struct registration
  cryptonote::tx_extra_mevatrust_registration reg{};
  reg.node_id        = node_id;
  reg.wallet_pubkey  = w_spk;
  reg.node_pubkey    = node_pk;
  reg.wallet_address = address_str;
  reg.port           = 0;
  reg.signature      = sig;
  // 6. Build 0xA0 extra blob
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_registration_extra(reg, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione registration_extra"); return true;
  }
  // 7. Submit self-send tx
  tools::msg_writer() << "\nRegistrazione nodo on-chain in corso...";
  tools::msg_writer() << "  Node : " << node_pk_hex.substr(0, 16) << "...";
  tools::msg_writer() << "  Wallet: " << address_str.substr(0, 24) << "...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) {
    tools::fail_msg_writer() << tr("Invio tx fallito. Verifica saldo e riconnessione al daemon.");
    return true;
  }
  tools::success_msg_writer()
    << "\n\033[1;32m*** TX DI REGISTRAZIONE INVIATA! ***\033[0m"
    << "\n  TXID    : " << txid
    << "\n  Predicted Node ID: " << epee::string_tools::pod_to_hex(node_id).substr(0, 24) << "..."
    << "\n  Attendere 1-2 blocchi (~2 min) per conferma."
    << "\n  Dopo la conferma, il badge \033[1;33mWELCOME\033[0m verra' assegnato automaticamente."
    << "\n  Usa: \033[1mnode_status\033[0m  per verificare lo stato."
    << "\n  Usa: \033[1mbadges\033[0m       per vedere i badge ottenuti."
    << "\n  \033[2mNota: altri badge richiedono uptime, sync, peer e sfide.\033[0m";
  save_node_id(epee::string_tools::pod_to_hex(node_id));
  return true;
  } catch (const std::exception& e) {
    tools::fail_msg_writer() << tr("ERRORE cmd_register_node: ") << e.what();
    return true;
  }
}
bool simple_wallet::cmd_unregister_node(const std::vector<std::string>& args) {
  // UX ZERO-ARGOMENTI: basta digitare unregister_node
  // 1. Ottieni node_id (da args, file salvato, o chiave nodo)
  std::string node_id_str;
  if (!resolve_node_id(node_id_str, args)) {
    tools::fail_msg_writer() << tr("Nessun node_id. Usa: unregister_node <node_id>");
    return true;
  }
  crypto::hash node_id{};
  if (!epee::string_tools::hex_to_pod(node_id_str, node_id)) {
    tools::fail_msg_writer() << tr("node_id non valido"); return true;
  }
  // 2. Conferma interattiva - operazione irreversibile
  tools::msg_writer() << "\033[1;33m!  ATTENZIONE: stai per de-registrare il nodo dalla rete MevaCoin.\033[0m";
  tools::msg_writer() << "  Node ID : " << node_id_str.substr(0,16) << "...";
  std::string confirm;
  rdln::suspend_readline pause_readline;
  std::cout << "Conferma (digita 'si' per procedere): ";
  std::getline(std::cin, confirm);
  if (confirm != "si") {
    tools::msg_writer() << tr("Operazione annullata.");
    return true;
  }
  // 3. Firma: sign(H(node_id || "deregister")) con wallet spend key
  const crypto::secret_key& w_ssk =
      m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk =
      m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  const crypto::hash msg_hash = cryptonote::mevatrust::deregister_message_hash(node_id);
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);
  // 4. Costruisce struct deregister
  cryptonote::tx_extra_mevatrust_deregister dereg{};
  dereg.node_id       = node_id;
  dereg.wallet_pubkey = w_spk;
  dereg.signature     = sig;
  // 5. Build 0xA1 extra blob
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_deregister_extra(dereg, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione deregister_extra"); return true;
  }
  // 6. Submit self-send tx
  tools::msg_writer() << "De-registrazione nodo on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) {
    tools::fail_msg_writer() << tr("Invio tx fallito. Verifica saldo e riconnessione al daemon.");
    return true;
  }
  remove_node_id();
  tools::success_msg_writer()
    << "\n\033[1;32m*** TX DI DE-REGISTRAZIONE INVIATA! ***\033[0m"
    << "\n  TXID    : " << txid
    << "\n  Node ID : " << node_id_str
    << "\n  Attendere 1-2 blocchi per conferma on-chain."
    << "\n  Per ri-registrare: attendi 60 min, poi usa register_node";
  return true;
}
bool simple_wallet::cmd_list_nodes(const std::vector<std::string>& args) {
  rpc::COMMAND_RPC_GET_ALL_NODE_INCENTIVES::request req;
  rpc::COMMAND_RPC_GET_ALL_NODE_INCENTIVES::response res;
  req.limit = 200;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "get_all_node_incentives", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC get_all_node_incentives"); return true; }
  if (res.nodes.empty()) {
    tools::msg_writer() << tr("Nessun nodo registrato nella rete.");
    return true; }
  tools::msg_writer() << "\n=== Nodi Registrati (" << res.total_nodes << " totali) ===";
  uint32_t n = 1;
  for (const auto& nd : res.nodes) {
    tools::msg_writer() << n++ << ". Node: " << nd.node_id.substr(0,16) << "...";
    tools::msg_writer() << "   Wallet: " << nd.wallet_address.substr(0,24) << "...";
    tools::msg_writer() << "   Badge: " << nd.badge_count << " | Reward: " << (nd.total_rewards/1000000000000ULL) << " MVC | Score: " << nd.score;
    if (!nd.badge_types.empty()) {
      std::string joined;
      for (const auto& b : nd.badge_types) joined += b + " ";
      tools::msg_writer() << "   Badge: " << joined; }
  }
  return true;
}

void simple_wallet::show_mevatrust_info(const std::string& node_id) {
  rpc::COMMAND_RPC_GET_BADGES::request b_req;
  rpc::COMMAND_RPC_GET_BADGES::response b_res;
  b_req.node_id = node_id;
  if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_badges", b_req, b_res) && !b_res.badges.empty()) {
    tools::msg_writer() << "Badge attivi: " << b_res.badges.size();
    for (const auto& b : b_res.badges)
      tools::msg_writer() << "  " << badge_color(b) << "* " << b << RESET_COLOR; }
  rpc::COMMAND_RPC_GET_NODE_STATUS::request s_req;
  rpc::COMMAND_RPC_GET_NODE_STATUS::response s_res;
  s_req.node_id = node_id;
  if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_node_status", s_req, s_res)) {
    tools::msg_writer() << "Attivo: " << (s_res.is_active?"SI":"NO")
                        << " | Sync: " << (s_res.is_synced?"SI":"NO"); }
  rpc::COMMAND_RPC_GET_NODE_UPTIME::request u_req;
  rpc::COMMAND_RPC_GET_NODE_UPTIME::response u_res;
  u_req.node_id = node_id;
  if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_node_uptime", u_req, u_res)) {
    tools::msg_writer() << "Uptime: " << (u_res.uptime_seconds/3600) << "h ("
                        << static_cast<int>(u_res.uptime_percentage*100.0) << "%)"; }
  rpc::COMMAND_RPC_GET_INCENTIVE_HISTORY::request h_req;
  rpc::COMMAND_RPC_GET_INCENTIVE_HISTORY::response h_res;
  h_req.node_id = node_id; h_req.limit = 5; h_req.offset = 0;
  if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_incentive_history", h_req, h_res)) {
    uint64_t tr = 0;
    for (const auto& e : h_res.entries) { if (e.amount>0) tr += e.amount; }
    tools::msg_writer() << "Record incentivi: " << h_res.total
                        << " | Totale reward: " << (tr/1000000000000ULL) << " MVC"; }
}

void simple_wallet::show_welcome_badge_trophy()
{
  std::string node_id;
  if (!resolve_node_id(node_id, {})) return;

  rpc::COMMAND_RPC_GET_BADGES::request req;
  rpc::COMMAND_RPC_GET_BADGES::response res;
  req.node_id = node_id;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "get_badges", req, res)) return;

  bool has_welcome = false;
  for (const auto& b : res.badges) {
    if (b == "WELCOME") { has_welcome = true; break; }
  }
  if (!has_welcome) return;

  tools::msg_writer() << "\033[1;33m";  // bold yellow
  tools::msg_writer() << "  ╔══════════════════════════════════════════════╗";
  tools::msg_writer() << "  ║          ★  TRAGUARDO SBLOCCO!  ★           ║";
  tools::msg_writer() << "  ║                                              ║";
  tools::msg_writer() << "  ║              BADGE: WELCOME                 ║";
  tools::msg_writer() << "  ║                                              ║";
  tools::msg_writer() << "  ║   Benvenuto nella rete MevaCoin! Questo e'  ║";
  tools::msg_writer() << "  ║   il primo traguardo del tuo nodo. Altri   ║";
  tools::msg_writer() << "  ║   badge ti aspettano man mano che il nodo   ║";
  tools::msg_writer() << "  ║   guadagna uptime, peer e partecipazione!   ║";
  tools::msg_writer() << "  ║                                              ║";
  tools::msg_writer() << "  ║   Usa:  badges  per vedere tutti i badge    ║";
  tools::msg_writer() << "  ╚══════════════════════════════════════════════╝";
  tools::msg_writer() << "\033[0m";  // reset
}

// =============================================================================
// Circle Registry CLI Commands
// =============================================================================

bool simple_wallet::cmd_circle_create(const std::vector<std::string>& args) {
  if (args.empty()) {
    tools::fail_msg_writer() << tr("Uso: circle_create <nome_cerchia>"); return true;
  }
  const crypto::secret_key& w_ssk =
      m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk =
      m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  // Per CREATE, circle_id sconosciuto -> zero hash, target = admin (noi stessi)
  const crypto::hash cid_zero{};
  // Firma: sign(H(CREATE || 0x0...0 || admin_pk || name))
  const crypto::hash msg_hash = cryptonote::mevatrust::circle_message_hash(
      cid_zero, static_cast<uint8_t>(cryptonote::tx_extra_mevatrust_circle::CREATE),
      w_spk, args[0]);
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);
  // Costruisce struct circle (op CREATE)
  cryptonote::tx_extra_mevatrust_circle op{};
  op.op_type       = cryptonote::tx_extra_mevatrust_circle::CREATE;
  op.circle_id     = cid_zero;
  op.circle_name   = args[0];
  op.target_pubkey = w_spk;
  op.signer_pubkey = w_spk;
  op.signature     = sig;
  // Build 0xA3 extra
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_circle_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione circle_extra"); return true;
  }
  tools::msg_writer() << "Creazione cerchia on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) {
    tools::fail_msg_writer() << tr("Invio tx fallito."); return true;
  }
  tools::success_msg_writer() << "\033[1;32m*** TX CREAZIONE CERCHIA INVIATA! ***\033[0m"
    << "\n  TXID : " << txid
    << "\n  Nome : " << args[0]
    << "\n  Attendere 1-2 blocchi per conferma, poi usare circle_list";
  return true;
}

bool simple_wallet::cmd_circle_info(const std::vector<std::string>& args) {
  if (args.empty()) {
    tools::fail_msg_writer() << tr("Uso: circle_info <circle_id>"); return true;
  }
  rpc::COMMAND_RPC_CIRCLE_INFO::request req;
  rpc::COMMAND_RPC_CIRCLE_INFO::response res;
  req.circle_id = args[0];
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "circle_info", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC circle_info"); return true; }
  if (res.circle_id.empty()) {
    tools::fail_msg_writer() << tr("Cerchia non trovata"); return true; }
  tools::fail_msg_writer() << "\n=== Dettaglio Cerchia ===";
  tools::fail_msg_writer() << "ID    : " << res.circle_id;
  tools::fail_msg_writer() << "Nome  : " << res.name;
  tools::fail_msg_writer() << "Admin : " << res.admin_pubkey;
  tools::fail_msg_writer() << "Altezza creazione: " << res.created_height;
  tools::fail_msg_writer() << "Membri (" << res.members.size() << "):";
  for (const auto& m : res.members)
    tools::fail_msg_writer() << "  - " << m;
  return true;
}

bool simple_wallet::cmd_circle_list(const std::vector<std::string>& args) {
  rpc::COMMAND_RPC_CIRCLE_LIST::request req;
  rpc::COMMAND_RPC_CIRCLE_LIST::response res;
  req.wallet_pubkey = args.empty() ? "" : args[0];
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "circle_list", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC circle_list"); return true; }
  if (res.circles.empty()) {
    tools::msg_writer() << tr("Nessuna cerchia trovata."); return true; }
  tools::msg_writer() << "\n=== Cerchie (" << res.circles.size() << ") ===";
  uint32_t n = 1;
  for (const auto& c : res.circles) {
    tools::msg_writer() << n++ << ". " << c.name
      << " (membri: " << c.member_count << ")";
    tools::msg_writer() << "   ID: " << c.circle_id.substr(0,16) << "...";
    tools::msg_writer() << "   Admin: " << c.admin_pubkey.substr(0,16) << "...";
  }
  return true;
}

bool simple_wallet::cmd_circle_join(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    tools::fail_msg_writer() << tr("Uso: circle_join <circle_id> <member_pubkey>"); return true;
  }
  const crypto::secret_key& w_ssk =
      m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk =
      m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  crypto::hash cid{};
  if (!epee::string_tools::hex_to_pod(args[0], cid)) {
    tools::fail_msg_writer() << tr("circle_id non valido"); return true; }
  crypto::public_key target_pk{};
  if (!epee::string_tools::hex_to_pod(args[1], target_pk)) {
    tools::fail_msg_writer() << tr("member_pubkey non valido"); return true; }
  // Firma: sign(H(JOIN || circle_id || target || ""))
  const crypto::hash msg_hash = cryptonote::mevatrust::circle_message_hash(
      cid, static_cast<uint8_t>(cryptonote::tx_extra_mevatrust_circle::JOIN),
      target_pk, "");
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);
  // Costruisce struct circle (op JOIN)
  cryptonote::tx_extra_mevatrust_circle op{};
  op.op_type       = cryptonote::tx_extra_mevatrust_circle::JOIN;
  op.circle_id     = cid;
  op.circle_name   = "";
  op.target_pubkey = target_pk;
  op.signer_pubkey = w_spk;
  op.signature     = sig;
  // Build 0xA3 extra
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_circle_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione circle_extra"); return true;
  }
  tools::msg_writer() << "Join cerchia on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) {
    tools::fail_msg_writer() << tr("Invio tx fallito."); return true; }
  tools::success_msg_writer() << "\033[1;32m*** TX JOIN INVIATA! ***\033[0m"
    << "\n  TXID : " << txid
    << "\n  Attendere conferma on-chain.";
  return true;
}

bool simple_wallet::cmd_circle_leave(const std::vector<std::string>& args) {
  std::string usage = tr("Uso: circle_leave <circle_id> [member_pubkey]\n"
                         "  Se member_pubkey non specificato, esci dalla cerchia come te stesso.");
  if (args.empty()) {
    tools::fail_msg_writer() << usage; return true;
  }
  const crypto::secret_key& w_ssk =
      m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk =
      m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  crypto::hash cid{};
  if (!epee::string_tools::hex_to_pod(args[0], cid)) {
    tools::fail_msg_writer() << tr("circle_id non valido"); return true; }
  const std::string my_pk_hex = epee::string_tools::pod_to_hex(w_spk);
  crypto::public_key target_pk{};
  const std::string target_hex = (args.size() > 1) ? args[1] : my_pk_hex;
  if (!epee::string_tools::hex_to_pod(target_hex, target_pk)) {
    tools::fail_msg_writer() << tr("member_pubkey non valido"); return true; }
  // Firma
  const crypto::hash msg_hash = cryptonote::mevatrust::circle_message_hash(
      cid, static_cast<uint8_t>(cryptonote::tx_extra_mevatrust_circle::LEAVE),
      target_pk, "");
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);
  // Struct circle (op LEAVE)
  cryptonote::tx_extra_mevatrust_circle op{};
  op.op_type       = cryptonote::tx_extra_mevatrust_circle::LEAVE;
  op.circle_id     = cid;
  op.circle_name   = "";
  op.target_pubkey = target_pk;
  op.signer_pubkey = w_spk;
  op.signature     = sig;
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_circle_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione"); return true; }
  tools::msg_writer() << "Leave cerchia on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) { tools::fail_msg_writer() << tr("Invio tx fallito."); return true; }
  tools::success_msg_writer() << "\033[1;32m*** TX LEAVE INVIATA! ***\033[0m"
    << "\n  TXID : " << txid;
  return true;
}

bool simple_wallet::cmd_circle_change_admin(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    tools::fail_msg_writer() << tr("Uso: circle_change_admin <circle_id> <new_admin_pubkey>"); return true;
  }
  const crypto::secret_key& w_ssk =
      m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk =
      m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  crypto::hash cid{};
  if (!epee::string_tools::hex_to_pod(args[0], cid)) {
    tools::fail_msg_writer() << tr("circle_id non valido"); return true; }
  crypto::public_key target_pk{};
  if (!epee::string_tools::hex_to_pod(args[1], target_pk)) {
    tools::fail_msg_writer() << tr("new_admin_pubkey non valido"); return true; }
  // Firma
  const crypto::hash msg_hash = cryptonote::mevatrust::circle_message_hash(
      cid, static_cast<uint8_t>(cryptonote::tx_extra_mevatrust_circle::CHANGE_ADMIN),
      target_pk, "");
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);
  // Struct circle (op CHANGE_ADMIN)
  cryptonote::tx_extra_mevatrust_circle op{};
  op.op_type       = cryptonote::tx_extra_mevatrust_circle::CHANGE_ADMIN;
  op.circle_id     = cid;
  op.circle_name   = "";
  op.target_pubkey = target_pk;
  op.signer_pubkey = w_spk;
  op.signature     = sig;
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_circle_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione"); return true; }
  tools::msg_writer() << "Cambio admin on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) { tools::fail_msg_writer() << tr("Invio tx fallito."); return true; }
  tools::success_msg_writer() << "\033[1;32m*** TX CHANGE ADMIN INVIATA! ***\033[0m"
    << "\n  TXID : " << txid;
  return true;
}

bool simple_wallet::cmd_circle_disband(const std::vector<std::string>& args) {
  if (args.empty()) {
    tools::fail_msg_writer() << tr("Uso: circle_disband <circle_id>"); return true;
  }
  const crypto::secret_key& w_ssk =
      m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk =
      m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  crypto::hash cid{};
  if (!epee::string_tools::hex_to_pod(args[0], cid)) {
    tools::fail_msg_writer() << tr("circle_id non valido"); return true; }
  // Firma
  const crypto::hash msg_hash = cryptonote::mevatrust::circle_message_hash(
      cid, static_cast<uint8_t>(cryptonote::tx_extra_mevatrust_circle::DISBAND),
      w_spk, "");
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);
  // Struct circle (op DISBAND)
  cryptonote::tx_extra_mevatrust_circle op{};
  op.op_type       = cryptonote::tx_extra_mevatrust_circle::DISBAND;
  op.circle_id     = cid;
  op.circle_name   = "";
  op.target_pubkey = w_spk;
  op.signer_pubkey = w_spk;
  op.signature     = sig;
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_circle_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione"); return true; }
  tools::msg_writer() << "Scioglimento cerchia on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) { tools::fail_msg_writer() << tr("Invio tx fallito."); return true; }
  tools::success_msg_writer() << "\033[1;32m*** TX DISBAND INVIATA! ***\033[0m"
    << "\n  TXID : " << txid;
  return true;
}

// =============================================================================
// Store CLI Commands (on-chain)
// =============================================================================

bool simple_wallet::cmd_store_list(const std::vector<std::string>&) {
  rpc::COMMAND_RPC_STORE_LIST::request req;
  rpc::COMMAND_RPC_STORE_LIST::response res;
  req.active_only = true;
  req.limit = 20;
  req.top = true;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "store_list", req, res)) {
    tools::fail_msg_writer() << tr("Errore: impossibile contattare il demone.");
    return true;
  }
  if (res.stores.empty()) {
    tools::msg_writer() << tr("Nessun negozio disponibile. Creane uno con: store_create \"Nome\" \"Descrizione\" [url]");
    return true;
  }
  tools::msg_writer() << "\n=== Negozi On-Chain (top " << res.stores.size() << ") ===";
  for (const auto& s : res.stores) {
    tools::msg_writer() << "\n\033[1m" << s.name << "\033[0m  (" << s.item_count << " item)";
    tools::msg_writer() << "  ID: " << s.store_id.substr(0, 16) << "...";
    tools::msg_writer() << "  " << s.description;
    if (!s.url.empty()) tools::msg_writer() << "  URL: " << s.url;
    tools::msg_writer() << "  Proprietario: " << s.owner_pubkey.substr(0, 16) << "... | h=" << s.created_height;
  }
  tools::msg_writer() << "\nUsa store_search <keyword> per cercare, store_show <id> per dettagli.";
  return true;
}

bool simple_wallet::cmd_store_show(const std::vector<std::string>& args) {
  if (args.empty()) { tools::fail_msg_writer() << tr("Uso: store_show <store_id>"); return true; }
  rpc::COMMAND_RPC_STORE_SHOW::request req;
  rpc::COMMAND_RPC_STORE_SHOW::response res;
  req.store_id = args[0];
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "store_show", req, res)) {
    tools::fail_msg_writer() << tr("Errore: impossibile contattare il demone.");
    return true;
  }
  if (res.status != "OK") { tools::fail_msg_writer() << res.status; return true; }
  tools::msg_writer() << "\n=== " << res.name << " ===";
  tools::msg_writer() << "  " << res.description;
  tools::msg_writer() << "  Item: " << res.item_count << " | Creato a h=" << res.created_height;
  tools::msg_writer() << "  Proprietario: " << res.owner_pubkey.substr(0, 16) << "...";
  if (res.items.empty()) {
    tools::msg_writer() << "\n  Nessun item disponibile.";
  } else {
    tools::msg_writer() << "\n--- Item ---";
    for (const auto& item : res.items) {
      auto price_mvc = item.price / 1000000000000ULL;
      tools::msg_writer() << "  [" << item.item_id.substr(0, 16) << "...] " << item.name << " - " << price_mvc << " MVC";
      if (!item.category.empty()) tools::msg_writer() << "    Categoria: " << item.category;
    }
    tools::msg_writer() << "\nUsa: store_buy <store_id> <item_id>  per acquistare.";
  }
  return true;
}

bool simple_wallet::cmd_store_search(const std::vector<std::string>& args) {
  if (args.empty()) {
    tools::fail_msg_writer() << tr("Uso: store_search <keyword>"); return true;
  }
  rpc::COMMAND_RPC_STORE_SEARCH::request req;
  rpc::COMMAND_RPC_STORE_SEARCH::response res;
  req.keyword = args[0];
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "store_search", req, res)) {
    tools::fail_msg_writer() << tr("Errore: impossibile contattare il demone.");
    return true;
  }
  if (res.stores.empty()) {
    tools::msg_writer() << tr("Nessun negozio trovato per: ") << args[0];
    return true;
  }
  tools::msg_writer() << "\n=== Ricerca: \"" << args[0] << "\" (" << res.stores.size() << " risultati) ===";
  for (const auto& s : res.stores) {
    tools::msg_writer() << "\n[" << s.store_id.substr(0, 16) << "...] " << s.name;
    tools::msg_writer() << "  " << s.description;
    if (!s.url.empty()) tools::msg_writer() << "  URL: " << s.url;
    tools::msg_writer() << "  Item: " << s.item_count << " | Proprietario: " << s.owner_pubkey.substr(0, 16) << "...";
  }
  tools::msg_writer() << "\nUsa store_show <store_id> per i dettagli.";
  return true;
}

bool simple_wallet::cmd_store_create(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    tools::fail_msg_writer() << tr("Uso: store_create \"Nome\" \"Descrizione\" [url] [indirizzo_pagamento] [euro_enabled=0/1] [euro_details] [mvc_percent=100] [euro_percent=0]");
    return true;
  }
  const account_keys& keys = m_wallet->get_account().get_keys();
  cryptonote::tx_extra_mevatrust_store op{};
  op.op = cryptonote::tx_extra_mevatrust_store::STORE_CREATE;
  op.store_id = crypto::hash{};
  op.name = args[0];
  op.description = args[1];
  op.url = (args.size() > 2) ? args[2] : "";
  op.payment_address = (args.size() > 3) ? args[3] : "";
  op.euro_enabled = (args.size() > 4) ? (args[4] == "1" || args[4] == "true") : false;
  op.euro_details = (args.size() > 5) ? args[5] : "";
  op.mvc_percent  = (args.size() > 6) ? static_cast<uint8_t>(std::stoul(args[6])) : 100;
  op.euro_percent = (args.size() > 7) ? static_cast<uint8_t>(std::stoul(args[7])) : 0;
  op.owner_pubkey = keys.m_account_address.m_spend_public_key;
  crypto::hash h = cryptonote::mevatrust::store_message_hash(op);
  crypto::generate_signature(h, op.owner_pubkey, keys.m_spend_secret_key, op.owner_sig);
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_store_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore creazione extra store."); return true;
  }
  std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) return true;
  tools::msg_writer() << tr("Negozio creato! TXID: ") << txid;
  return true;
}

bool simple_wallet::cmd_store_add_item(const std::vector<std::string>& args) {
  if (args.size() < 3) {
    tools::fail_msg_writer() << tr("Uso: store_add_item <store_id> \"Nome\" <price_mvc> [categoria] [payment_mode=mvc_only]");
    return true;
  }
  uint64_t price_atomic = std::stoull(args[2]) * 1000000000000ULL;
  const account_keys& keys = m_wallet->get_account().get_keys();
  cryptonote::tx_extra_mevatrust_store op{};
  op.op = cryptonote::tx_extra_mevatrust_store::ITEM_LIST;
  epee::string_tools::hex_to_pod(args[0], op.store_id);
  op.name = args[1];
  op.price = price_atomic;
  op.category = (args.size() > 3) ? args[3] : "";
  op.payment_mode = (args.size() > 4) ? args[4] : "mvc_only";
  op.owner_pubkey = keys.m_account_address.m_spend_public_key;
  crypto::hash h = cryptonote::mevatrust::store_message_hash(op);
  crypto::generate_signature(h, op.owner_pubkey, keys.m_spend_secret_key, op.owner_sig);
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_store_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore creazione extra item."); return true;
  }
  std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) return true;
  tools::msg_writer() << tr("Item listato! TXID: ") << txid;
  return true;
}

bool simple_wallet::cmd_store_buy(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    tools::fail_msg_writer() << tr("Uso: store_buy <store_id> <item_id> [payment_method=mvc_only] [euro_ref] [euro_amount_cent]");
    return true;
  }
  // Ricava store e item via RPC
  rpc::COMMAND_RPC_STORE_SHOW::request s_req;
  rpc::COMMAND_RPC_STORE_SHOW::response s_res;
  s_req.store_id = args[0];
  std::string seller_address;
  uint64_t item_price = 0;
  if (m_wallet->invoke_http_json_rpc("/json_rpc", "store_show", s_req, s_res) && s_res.status == "OK") {
    seller_address = s_res.payment_address;
    for (const auto& item : s_res.items) {
      if (item.item_id == args[1] || item.item_id.substr(0, 16) == args[1].substr(0, 16)) {
        item_price = item.price;
        tools::msg_writer() << "Acquisto: " << item.name << " - " << (item.price / 1000000000000ULL) << " MVC";
        break;
      }
    }
  }
  if (seller_address.empty()) {
    tools::fail_msg_writer() << tr("Impossibile trovare il negozio/venditore.");
    return true;
  }
  crypto::hash store_id, item_id;
  epee::string_tools::hex_to_pod(args[0], store_id);
  epee::string_tools::hex_to_pod(args[1], item_id);
  const account_keys& keys = m_wallet->get_account().get_keys();
  std::string payment_method = (args.size() > 2) ? args[2] : "mvc_only";
  std::string euro_ref = (args.size() > 3) ? args[3] : "";
  uint64_t euro_amount = (args.size() > 4) ? std::stoull(args[4]) : 0;

  cryptonote::tx_extra_mevatrust_store op{};
  op.op = cryptonote::tx_extra_mevatrust_store::ITEM_BUY;
  op.store_id = store_id;
  op.item_id = item_id;
  op.buyer_pubkey = keys.m_account_address.m_spend_public_key;
  op.buyer_payment_method = payment_method;
  op.euro_ref = euro_ref;
  op.euro_amount = euro_amount;
  op.owner_pubkey = keys.m_account_address.m_spend_public_key;
  crypto::hash h = cryptonote::mevatrust::store_message_hash(op);
  crypto::generate_signature(h, op.owner_pubkey, keys.m_spend_secret_key, op.owner_sig);
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_store_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore creazione extra acquisto."); return true;
  }
  // Invia pagamento al venditore (non self-send!)
  std::string txid = submit_mevatrust_tx_to(m_wallet.get(), seller_address, item_price, extra);
  if (txid.empty()) return true;
  tools::msg_writer() << tr("Acquisto inviato! TXID: ") << txid;
  tools::msg_writer() << tr("Pagamento di ") << cryptonote::print_money(item_price)
                      << tr(" MVC inviato a: ") << seller_address;
  return true;
}

bool simple_wallet::cmd_store_my_stores(const std::vector<std::string>&) {
  rpc::COMMAND_RPC_STORE_LIST::request req;
  rpc::COMMAND_RPC_STORE_LIST::response res;
  req.active_only = true;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "store_list", req, res)) {
    tools::fail_msg_writer() << tr("Errore: impossibile contattare il demone.");
    return true;
  }
  const auto my_pk = epee::string_tools::pod_to_hex(
    m_wallet->get_account().get_keys().m_account_address.m_spend_public_key);
  std::vector<decltype(res.stores)::value_type> mine;
  for (const auto& s : res.stores) {
    if (s.owner_pubkey == my_pk) mine.push_back(s);
  }
  if (mine.empty()) {
    tools::msg_writer() << tr("Non hai negozi. Creane uno con: store_create \"Nome\" \"Descrizione\"");
    return true;
  }
  tools::msg_writer() << "\n=== I Miei Negozi ===";
  for (const auto& s : mine) {
    tools::msg_writer() << "\nID:  " << s.store_id;
    tools::msg_writer() << "  " << s.name << " - " << s.item_count << " item";
  }
  return true;
}

bool simple_wallet::cmd_store_my_purchases(const std::vector<std::string>&) {
  rpc::COMMAND_RPC_STORE_MY_PURCHASES::request req;
  rpc::COMMAND_RPC_STORE_MY_PURCHASES::response res;
  req.buyer_pubkey = epee::string_tools::pod_to_hex(
    m_wallet->get_account().get_keys().m_account_address.m_spend_public_key);
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "store_my_purchases", req, res)) {
    tools::fail_msg_writer() << tr("Errore: impossibile contattare il demone.");
    return true;
  }
  if (res.purchases.empty()) {
    tools::msg_writer() << tr("Nessun acquisto effettuato.");
    return true;
  }
  tools::msg_writer() << "\n=== I Miei Acquisti ===";
  for (const auto& p : res.purchases) {
    tools::msg_writer() << "  Store: " << p.store_id.substr(0, 16) << "... | Item: " << p.item_id.substr(0, 16) << "... | h=" << p.purchase_height;
  }
  return true;
}

bool simple_wallet::cmd_store_delist(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    tools::fail_msg_writer() << tr("Uso: store_delist <store_id> <item_id>"); return true;
  }
  const account_keys& keys = m_wallet->get_account().get_keys();
  cryptonote::tx_extra_mevatrust_store op{};
  op.op = cryptonote::tx_extra_mevatrust_store::ITEM_DELIST;
  epee::string_tools::hex_to_pod(args[0], op.store_id);
  epee::string_tools::hex_to_pod(args[1], op.item_id);
  op.owner_pubkey = keys.m_account_address.m_spend_public_key;
  crypto::hash h = cryptonote::mevatrust::store_message_hash(op);
  crypto::generate_signature(h, op.owner_pubkey, keys.m_spend_secret_key, op.owner_sig);
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_store_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore creazione extra delist."); return true;
  }
  std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) return true;
  tools::msg_writer() << tr("Item delistato! TXID: ") << txid;
  return true;
}

bool simple_wallet::cmd_store_deactivate(const std::vector<std::string>& args) {
  if (args.size() < 1) {
    tools::fail_msg_writer() << tr("Uso: store_deactivate <store_id>"); return true;
  }
  const account_keys& keys = m_wallet->get_account().get_keys();
  cryptonote::tx_extra_mevatrust_store op{};
  op.op = cryptonote::tx_extra_mevatrust_store::STORE_DEACTIVATE;
  epee::string_tools::hex_to_pod(args[0], op.store_id);
  op.owner_pubkey = keys.m_account_address.m_spend_public_key;
  crypto::hash h = cryptonote::mevatrust::store_message_hash(op);
  crypto::generate_signature(h, op.owner_pubkey, keys.m_spend_secret_key, op.owner_sig);
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_store_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore creazione extra disattivazione negozio."); return true;
  }
  std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) return true;
  tools::msg_writer() << tr("Negozio disattivato! TXID: ") << txid;
  return true;
}

bool simple_wallet::cmd_store_update(const std::vector<std::string>& args) {
  if (args.size() < 3) {
    tools::fail_msg_writer() << tr("Uso: store_update <store_id> \"Nome\" \"Descrizione\" [url] [indirizzo_pagamento] [euro_enabled=0/1] [euro_details] [mvc_percent=100] [euro_percent=0]");
    return true;
  }
  const account_keys& keys = m_wallet->get_account().get_keys();
  cryptonote::tx_extra_mevatrust_store op{};
  op.op = cryptonote::tx_extra_mevatrust_store::STORE_UPDATE;
  epee::string_tools::hex_to_pod(args[0], op.store_id);
  op.name = args[1];
  op.description = args[2];
  op.url = (args.size() > 3) ? args[3] : "";
  op.payment_address = (args.size() > 4) ? args[4] : "";
  op.euro_enabled = (args.size() > 5) ? (args[5] == "1" || args[5] == "true") : false;
  op.euro_details = (args.size() > 6) ? args[6] : "";
  op.mvc_percent  = (args.size() > 7) ? static_cast<uint8_t>(std::stoul(args[7])) : 100;
  op.euro_percent = (args.size() > 8) ? static_cast<uint8_t>(std::stoul(args[8])) : 0;
  op.owner_pubkey = keys.m_account_address.m_spend_public_key;
  crypto::hash h = cryptonote::mevatrust::store_message_hash(op);
  crypto::generate_signature(h, op.owner_pubkey, keys.m_spend_secret_key, op.owner_sig);
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_store_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore creazione extra update."); return true;
  }
  std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) return true;
  tools::msg_writer() << tr("Negozio aggiornato! TXID: ") << txid;
  return true;
}

// =============================================================================
// Score / Penalty / Admin CLI Commands
// =============================================================================

bool simple_wallet::cmd_node_score(const std::vector<std::string>& args) {
  std::string node_id;
  if (!resolve_node_id(node_id, args)) return true;
  rpc::COMMAND_RPC_GET_MEVATRUST_SCORE::request req;
  rpc::COMMAND_RPC_GET_MEVATRUST_SCORE::response res;
  req.node_id = node_id;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "get_mevatrust_score", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC get_mevatrust_score"); return true; }
  tools::fail_msg_writer() << "\n=== Reputation Score ===";
  tools::fail_msg_writer() << "Node ID: " << res.node_id;
  tools::fail_msg_writer() << "Score  : " << res.score;
  // Colora in base allo score
  if (res.score >= 0.8) tools::msg_writer() << "  \033[32mAffidabilita: ALTA\033[0m";
  else if (res.score >= 0.5) tools::msg_writer() << "  \033[33mAffidabilita: MEDIA\033[0m";
  else tools::msg_writer() << "  \033[31mAffidabilita: BASSA\033[0m";
  return true;
}

bool simple_wallet::cmd_node_penalties(const std::vector<std::string>& args) {
  std::string node_id;
  if (!resolve_node_id(node_id, args)) return true;
  rpc::COMMAND_RPC_GET_PENALTY_HISTORY::request req;
  rpc::COMMAND_RPC_GET_PENALTY_HISTORY::response res;
  req.node_id = node_id;
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "get_penalty_history", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC get_penalty_history"); return true; }
  tools::fail_msg_writer() << "\n=== Penalita Nodo ===";
  tools::fail_msg_writer() << "Node ID         : " << res.node_id;
  tools::fail_msg_writer() << "Penalita totale : " << res.total_penalty;
  for (const auto& pe : res.entries) {
    tools::fail_msg_writer() << "  \033[31m* " << pe.offense_type
      << " (-" << pe.amount << ")\033[0m";
    if (!pe.reason.empty())
      tools::fail_msg_writer() << "    Motivo: " << pe.reason;
  }
  return true;
}

bool simple_wallet::cmd_ban_node(const std::vector<std::string>& args) {
  if (args.empty()) {
    tools::fail_msg_writer() << tr("Uso: ban_node <node_id> [reason]"); return true;
  }
  const crypto::secret_key& w_ssk =
      m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk =
      m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  crypto::hash nid{};
  if (!epee::string_tools::hex_to_pod(args[0], nid)) {
    tools::fail_msg_writer() << tr("node_id non valido"); return true; }
  std::string reason = (args.size() > 1) ? args[1] : "Violazione termini di servizio";
  // Costruisce struct penalty (op BAN)
  cryptonote::tx_extra_mevatrust_penalty pen{};
  pen.op_type       = cryptonote::tx_extra_mevatrust_penalty::BAN;
  pen.node_id       = nid;
  pen.offense_type  = "BAN";
  pen.amount        = -1000; // -1.00 score come ban
  pen.reason        = reason;
  pen.signer_pubkey = w_spk;
  pen.signature     = crypto::signature{};
  // Firma: sign(H(op_type || node_id || offense || amount || reason))
  const crypto::hash msg_hash = cryptonote::mevatrust::penalty_message_hash(pen);
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::generate_signature(msg_hash, w_pk, w_ssk, pen.signature);
  // Build 0xA4 extra
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_penalty_extra(pen, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione penalty_extra"); return true; }
  tools::msg_writer() << "Ban nodo on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) { tools::fail_msg_writer() << tr("Invio tx fallito."); return true; }
  tools::success_msg_writer() << "\033[1;31m*** TX BAN INVIATA! ***\033[0m"
    << "\n  TXID    : " << txid
    << "\n  Node ID : " << args[0]
    << "\n  Attendere conferma on-chain.";
  return true;
}

bool simple_wallet::cmd_unban_node(const std::vector<std::string>& args) {
  if (args.empty()) {
    tools::fail_msg_writer() << tr("Uso: unban_node <node_id>"); return true;
  }
  const crypto::secret_key& w_ssk =
      m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk =
      m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  crypto::hash nid{};
  if (!epee::string_tools::hex_to_pod(args[0], nid)) {
    tools::fail_msg_writer() << tr("node_id non valido"); return true; }
  // Costruisce struct penalty (op UNBAN)
  cryptonote::tx_extra_mevatrust_penalty pen{};
  pen.op_type       = cryptonote::tx_extra_mevatrust_penalty::UNBAN;
  pen.node_id       = nid;
  pen.offense_type  = "UNBAN";
  pen.amount        = 0;
  pen.reason        = "Unban su richiesta amministratore";
  pen.signer_pubkey = w_spk;
  pen.signature     = crypto::signature{};
  // Firma
  const crypto::hash msg_hash = cryptonote::mevatrust::penalty_message_hash(pen);
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::generate_signature(msg_hash, w_pk, w_ssk, pen.signature);
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_penalty_extra(pen, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione"); return true; }
  tools::msg_writer() << "Unban nodo on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) { tools::fail_msg_writer() << tr("Invio tx fallito."); return true; }
  tools::success_msg_writer() << "\033[1;32m*** TX UNBAN INVIATA! ***\033[0m"
    << "\n  TXID    : " << txid;
  return true;
}

bool simple_wallet::cmd_my_status(const std::vector<std::string>&) {
  std::string node_id;
  if (!resolve_node_id(node_id, {})) return true;
  const auto& keys = m_wallet->get_account().get_keys();
  const std::string my_pk = epee::string_tools::pod_to_hex(
    keys.m_account_address.m_spend_public_key);

  tools::msg_writer() << "\n\033[1;36m╔══════════════════════════════════════════════╗";
  tools::msg_writer() << "║        MevaTrust Dashboard — " << node_id.substr(0,16) << "...  ║";
  tools::msg_writer() << "╚══════════════════════════════════════════════╝\033[0m";

  // ── 1. Node Status ──────────────────────────────────────────────
  {
    rpc::COMMAND_RPC_GET_NODE_STATUS::request req;
    rpc::COMMAND_RPC_GET_NODE_STATUS::response res;
    req.node_id = node_id;
    if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_node_status", req, res)) {
      tools::msg_writer() << "\n\033[1m▸ Stato Nodo\033[0m";
      tools::msg_writer() << "  Attivo  : " << (res.is_active ? "\033[32mSI\033[0m" : "\033[31mNO\033[0m");
      tools::msg_writer() << "  Sync    : " << (res.is_synced ? "\033[32mSI\033[0m" : "\033[31mNO\033[0m");
      tools::msg_writer() << "  Visto   : " << res.last_seen << " (unix ts)";
    }
  }

  // ── 2. Score ────────────────────────────────────────────────────
  {
    rpc::COMMAND_RPC_GET_MEVATRUST_SCORE::request req;
    rpc::COMMAND_RPC_GET_MEVATRUST_SCORE::response res;
    req.node_id = node_id;
    if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_mevatrust_score", req, res)) {
      tools::msg_writer() << "\n\033[1m▸ Reputation Score\033[0m";
      const char* col = res.score >= 0.8 ? "\033[32m" : (res.score >= 0.5 ? "\033[33m" : "\033[31m");
      tools::msg_writer() << "  Score   : " << col << res.score << "\033[0m";
    }
  }

  // ── 3. Penalties ────────────────────────────────────────────────
  {
    rpc::COMMAND_RPC_GET_PENALTY_HISTORY::request req;
    rpc::COMMAND_RPC_GET_PENALTY_HISTORY::response res;
    req.node_id = node_id;
    if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_penalty_history", req, res)) {
      tools::msg_writer() << "\n\033[1m▸ Penalità\033[0m";
      tools::msg_writer() << "  Totale  : " << res.total_penalty << " | "
                          << res.total << " eventi";
    }
  }

  // ── 4. Badges ───────────────────────────────────────────────────
  {
    rpc::COMMAND_RPC_GET_BADGES::request req;
    rpc::COMMAND_RPC_GET_BADGES::response res;
    req.node_id = node_id;
    if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_badges", req, res)) {
      tools::msg_writer() << "\n\033[1m▸ Badge\033[0m";
      if (res.badges.empty()) {
        tools::msg_writer() << "  Nessun badge";
      } else {
        tools::msg_writer() << "  (" << res.badges.size() << " attivi)";
        for (const auto& b : res.badges)
          tools::msg_writer() << "  " << badge_color(b) << "* " << b << "\033[0m";
      }
    }
  }

  // ── 5. Uptime ───────────────────────────────────────────────────
  {
    rpc::COMMAND_RPC_GET_NODE_UPTIME::request req;
    rpc::COMMAND_RPC_GET_NODE_UPTIME::response res;
    req.node_id = node_id;
    if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_node_uptime", req, res)) {
      tools::msg_writer() << "\n\033[1m▸ Uptime\033[0m";
      tools::msg_writer() << "  " << res.uptime_seconds << " sec ("
                          << (res.uptime_seconds / 3600) << " h) — "
                          << static_cast<int>(res.uptime_percentage * 100.0) << "%";
    }
  }

  // ── 6. Incentivi (solo riepilogo) ───────────────────────────────
  {
    rpc::COMMAND_RPC_GET_INCENTIVE_HISTORY::request req;
    rpc::COMMAND_RPC_GET_INCENTIVE_HISTORY::response res;
    req.node_id = node_id;
    req.limit = 20;
    req.offset = 0;
    if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_incentive_history", req, res)) {
      uint64_t total_reward = 0;
      for (const auto& e : res.entries)
        if (e.amount > 0) total_reward += e.amount;
      tools::msg_writer() << "\n\033[1m▸ Incentivi\033[0m";
      tools::msg_writer() << "  " << res.total << " eventi | Rewards: "
                          << total_reward << " sat (ultimi 20)";
    }
  }

  // ── 7. Negozi posseduti ─────────────────────────────────────────
  {
    rpc::COMMAND_RPC_STORE_LIST::request req;
    rpc::COMMAND_RPC_STORE_LIST::response res;
    req.active_only = true;
    if (m_wallet->invoke_http_json_rpc("/json_rpc", "store_list", req, res)) {
      size_t count = 0;
      for (const auto& s : res.stores)
        if (s.owner_pubkey == my_pk) ++count;
      tools::msg_writer() << "\n\033[1m▸ Negozi\033[0m";
      tools::msg_writer() << "  Possiedi " << count << " negozio/i";
    }
  }

  // ── 8. Acquisti effettuati ─────────────────────────────────────
  {
    rpc::COMMAND_RPC_STORE_MY_PURCHASES::request req;
    rpc::COMMAND_RPC_STORE_MY_PURCHASES::response res;
    req.buyer_pubkey = my_pk;
    if (m_wallet->invoke_http_json_rpc("/json_rpc", "store_my_purchases", req, res)) {
      tools::msg_writer() << "\n\033[1m▸ Acquisti\033[0m";
      tools::msg_writer() << "  " << res.purchases.size() << " acquisto/i effettuato/i";
    }
  }

  tools::msg_writer() << "\n\033[1;36m╚══════════════════════════════════════════════╝\033[0m\n";
  return true;
}

// ── Validator commands ──────────────────────────────────────────────────
bool simple_wallet::cmd_become_validator(const std::vector<std::string>&) {
  std::string node_id;
  if (!resolve_node_id(node_id, {})) return true;

  // Verifica stato attuale
  rpc::COMMAND_RPC_GET_BADGES::request b_req;
  rpc::COMMAND_RPC_GET_BADGES::response b_res;
  b_req.node_id = node_id;
  bool has_val_badge = false;
  if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_badges", b_req, b_res)) {
    for (const auto& b : b_res.badges)
      if (b == "NETWORK_VALIDATOR") { has_val_badge = true; break; }
  }
  if (has_val_badge) {
    tools::fail_msg_writer() << tr("Sei gia' un validator! Usa: validator_info");
    return true;
  }

  const uint64_t STAKE = 1000'000'000'000'000ULL; // 1000 MVC
  tools::msg_writer() << "\n\033[1;33m╔══════════════════════════════════════════════╗\033[0m";
  tools::msg_writer() << "\033[1;33m║          DIVENTA VALIDATOR                   ║\033[0m";
  tools::msg_writer() << "\033[1;33m╚══════════════════════════════════════════════╝\033[0m";
  tools::msg_writer() << tr("Per diventare validator devi inviare ") << STAKE / 1'000'000'000'000ULL << " MVC al pool.";
  tools::msg_writer() << tr("Questi MVC andranno al fondo partecipazione per tutti.");
  tools::msg_writer() << tr("In cambio otterrai:");
  tools::msg_writer() << tr("  - Diritto di inviare challenge (solo i validator)");
  tools::msg_writer() << tr("  - +0.10 bonus score sui reward");
  tools::msg_writer() << tr("  - Badge NETWORK_VALIDATOR");
  tools::msg_writer() << "";

  // Conferma utente
  tools::msg_writer() << "\033[1;33m!  ATTENZIONE: questa operazione richiede " << (STAKE / 1'000'000'000'000ULL) << " MVC.\033[0m";
  std::string confirm;
  rdln::suspend_readline pause_readline;
  std::cout << "Conferma (digita 'si' per procedere): ";
  std::getline(std::cin, confirm);
  if (confirm != "si") {
    tools::msg_writer() << tr("Operazione annullata.");
    return true;
  }

  const crypto::public_key& w_spk = m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  const crypto::secret_key& w_ssk = m_wallet->get_account().get_keys().m_spend_secret_key;

  // Leggi chiave nodo per computare node_id uguale a register_node
  std::string node_pk_hex;
  if (!read_node_pubkey(node_pk_hex)) {
    tools::fail_msg_writer() << tr("Chiave nodo non trovata. Avvia il daemon prima.");
    return true;
  }
  crypto::public_key node_pk{};
  epee::string_tools::hex_to_pod(node_pk_hex, node_pk);

  crypto::hash nid{};
  epee::string_tools::hex_to_pod(node_id, nid);

  // Firma: sign(H(node_id || wallet_pubkey || "validator"))
  const crypto::hash msg_hash = mevatrust::validator_message_hash(nid, w_spk);
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);

  // Costruisci tx_extra 0xAB
  tx_extra_mevatrust_validator val{};
  val.node_id = nid;
  val.wallet_pubkey = w_spk;
  val.node_pubkey = node_pk;
  val.signature = sig;

  std::vector<uint8_t> extra;
  if (!mevatrust::build_mevatrust_validator_extra(val, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione validator_extra");
    return true;
  }

  // Crea TX self-send con stake al pool address
  try {
    cryptonote::tx_destination_entry self;
    self.addr   = m_wallet->get_account().get_keys().m_account_address;
    self.amount = 1000000000ULL;  // 0.001 MVC per la tassa
    self.is_subaddress = false;

    cryptonote::tx_destination_entry pool_de;
    pool_de.addr   = cryptonote::mevatrust::get_pool_address(m_wallet->nettype());
    pool_de.amount = STAKE;
    pool_de.is_subaddress = false;

    std::vector<cryptonote::tx_destination_entry> dsts = {self, pool_de};
    uint64_t unlocked = m_wallet->unlocked_balance(0, false, nullptr, nullptr);
    if (unlocked < STAKE + 1000000000ULL) {
      tools::fail_msg_writer() << tr("Saldo insufficiente. Servono ~")
                               << (STAKE + 1000000000ULL) / 1'000'000'000'000ULL << " MVC sbloccati.";
      return true;
    }

    const size_t fake_outs_count = m_wallet->get_min_ring_size() - 1;
    auto ptx_vector = m_wallet->create_transactions_2(dsts, fake_outs_count, tools::fee_priority::Normal,
                                                       extra, 0, std::set<uint32_t>());
    if (ptx_vector.empty()) {
      tools::fail_msg_writer() << tr("Errore creazione tx.");
      return true;
    }
    const crypto::hash tx_hash = cryptonote::get_transaction_hash(ptx_vector[0].tx);
    m_wallet->commit_tx(ptx_vector);
    tools::success_msg_writer()
      << "\n\033[1;32m*** TX VALIDATOR INVIATA! ***\033[0m"
      << "\n  TXID    : " << epee::string_tools::pod_to_hex(tx_hash)
      << "\n  Attendere la conferma on-chain."
      << "\n  Usa: \033[1mvalidator_info\033[0m  per verificare lo stato.";
  } catch (const std::exception& e) {
    tools::fail_msg_writer() << tr("ERRORE: ") << e.what();
  }
  return true;
}

bool simple_wallet::cmd_validator_info(const std::vector<std::string>&) {
  std::string node_id;
  if (!resolve_node_id(node_id, {})) return true;

  rpc::COMMAND_RPC_GET_BADGES::request b_req;
  rpc::COMMAND_RPC_GET_BADGES::response b_res;
  b_req.node_id = node_id;
  bool has_val = false;
  if (m_wallet->invoke_http_json_rpc("/json_rpc", "get_badges", b_req, b_res)) {
    for (const auto& b : b_res.badges) {
      if (b == "NETWORK_VALIDATOR") { has_val = true; break; }
    }
  }

  tools::msg_writer() << "\n\033[1m▸ Validator Status\033[0m";
  if (has_val) {
    tools::msg_writer() << "  Sei un \033[1;33mVALIDATOR\033[0m!";
    tools::msg_writer() << "  Vantaggi: +0.10 score bonus, invio challenge, badge esclusivo";
  } else {
    tools::msg_writer() << "  Non sei ancora un validator.";
    tools::msg_writer() << "  Diventalo con: \033[1mbecome_validator\033[0m (1000 MVC)";
    tools::msg_writer() << "  Oppure automaticamente dopo 30gg di uptime 95%.";
  }

  return true;
}

// =============================================================================
// Circle Vote CLI Commands (prima/seconda convocazione all'italiana)
// =============================================================================

bool simple_wallet::cmd_circle_propose_change_admin(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    tools::fail_msg_writer() << tr("Uso: circle_propose_change_admin <circle_id> <new_admin_pk> [reason]"); return true;
  }
  const crypto::secret_key& w_ssk = m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk = m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  crypto::hash cid{};
  if (!epee::string_tools::hex_to_pod(args[0], cid)) { tools::fail_msg_writer() << tr("circle_id non valido"); return true; }
  crypto::public_key target_pk{};
  if (!epee::string_tools::hex_to_pod(args[1], target_pk)) { tools::fail_msg_writer() << tr("new_admin_pk non valido"); return true; }
  std::string reason = (args.size() > 2) ? args[2] : "";
  // Firma: sign(PROPOSE_CHANGE_ADMIN || 0x0...0 || circle_id || target_pk || reason)
  crypto::hash zero_id{};
  std::string msg;
  msg.push_back(0); // PROPOSE_CHANGE_ADMIN = 0
  msg.append(reinterpret_cast<const char*>(zero_id.data), 32);
  msg.append(reinterpret_cast<const char*>(cid.data), 32);
  msg.append(reinterpret_cast<const char*>(&target_pk), sizeof(target_pk));
  msg.push_back(0); // vote_yes = false (ignored per PROPOSE)
  msg.append(reason);
  crypto::hash msg_hash = crypto::cn_fast_hash(msg.data(), msg.size());
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);
  // Costruisce struct vote op (PROPOSE_CHANGE_ADMIN)
  cryptonote::tx_extra_mevatrust_circle_vote op{};
  op.op_type = cryptonote::tx_extra_mevatrust_circle_vote::PROPOSE_CHANGE_ADMIN;
  op.proposal_id = zero_id;
  op.circle_id = cid;
  op.target_pubkey = target_pk;
  op.signer_pubkey = w_spk;
  op.vote_yes = false;
  op.reason = reason;
  op.signature = sig;
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_circle_vote_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione vote_extra"); return true;
  }
  tools::msg_writer() << "Proposta cambio admin on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) { tools::fail_msg_writer() << tr("Invio tx fallito."); return true; }
  tools::success_msg_writer() << "\033[1;32m*** TX PROPOSTA INVIATA! ***\033[0m"
    << "\n  TXID     : " << txid
    << "\n  Target   : " << args[1].substr(0,16) << "..."
    << "\n  Attendere 1-2 blocchi, poi usare circle_proposal_list per vedere la proposta";
  return true;
}

bool simple_wallet::cmd_circle_vote(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    tools::fail_msg_writer() << tr("Uso: circle_vote <proposal_id> yes|no"); return true;
  }
  const crypto::secret_key& w_ssk = m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk = m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  crypto::hash pid{};
  if (!epee::string_tools::hex_to_pod(args[0], pid)) { tools::fail_msg_writer() << tr("proposal_id non valido"); return true; }
  bool vote_yes;
  if (args[1] == "yes" || args[1] == "si") vote_yes = true;
  else if (args[1] == "no") vote_yes = false;
  else { tools::fail_msg_writer() << tr("Specificare yes o no"); return true; }
  // Recupera i dettagli della proposta (circle_id) via RPC
  tools::msg_writer() << "Recupero informazioni proposta...";
  rpc::COMMAND_RPC_CIRCLE_PROPOSAL_VOTES::request v_req;
  rpc::COMMAND_RPC_CIRCLE_PROPOSAL_VOTES::response v_res;
  v_req.proposal_id = args[0];
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "circle_proposal_votes", v_req, v_res)) {
    tools::fail_msg_writer() << tr("Errore RPC: proposta non trovata"); return true;
  }
  crypto::hash circle_id{};
  if (!epee::string_tools::hex_to_pod(v_res.circle_id, circle_id)) {
    tools::fail_msg_writer() << tr("Errore lettura circle_id dalla proposta"); return true;
  }
  // Firma: sign(CAST_VOTE=1 || proposal_id || circle_id || voter_pk || vote_yes || "")
  std::string msg;
  msg.push_back(1); // CAST_VOTE = 1
  msg.append(reinterpret_cast<const char*>(pid.data), 32);
  msg.append(reinterpret_cast<const char*>(circle_id.data), 32);
  msg.append(reinterpret_cast<const char*>(&w_spk), sizeof(w_spk));
  msg.push_back(vote_yes ? 1 : 0);
  msg.append("");
  crypto::hash msg_hash = crypto::cn_fast_hash(msg.data(), msg.size());
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);
  cryptonote::tx_extra_mevatrust_circle_vote op{};
  op.op_type = cryptonote::tx_extra_mevatrust_circle_vote::CAST_VOTE;
  op.proposal_id = pid;
  op.circle_id = circle_id;
  op.target_pubkey = w_spk;
  op.signer_pubkey = w_spk;
  op.vote_yes = vote_yes;
  op.reason = "";
  op.signature = sig;
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_circle_vote_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione vote_extra"); return true;
  }
  tools::msg_writer() << "Voto on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) { tools::fail_msg_writer() << tr("Invio tx fallito."); return true; }
  tools::success_msg_writer() << "\033[1;32m*** TX VOTO INVIATA! ***\033[0m"
    << "\n  TXID : " << txid
    << "\n  Voto : " << (vote_yes ? "SI" : "NO")
    << "\n  Attendere 1-2 blocchi per conferma.";
  return true;
}

bool simple_wallet::cmd_circle_finalize_vote(const std::vector<std::string>& args) {
  if (args.empty()) {
    tools::fail_msg_writer() << tr("Uso: circle_finalize_vote <proposal_id>"); return true;
  }
  const crypto::secret_key& w_ssk = m_wallet->get_account().get_keys().m_spend_secret_key;
  const crypto::public_key& w_spk = m_wallet->get_account().get_keys().m_account_address.m_spend_public_key;
  crypto::hash pid{};
  if (!epee::string_tools::hex_to_pod(args[0], pid)) { tools::fail_msg_writer() << tr("proposal_id non valido"); return true; }
  crypto::hash zero_id{};
  crypto::hash msg_hash;
  {
    std::string m;
    m.push_back(2); // FINALIZE_VOTE = 2
    m.append(reinterpret_cast<const char*>(pid.data), 32);
    m.append(reinterpret_cast<const char*>(zero_id.data), 32);
    m.append(reinterpret_cast<const char*>(&w_spk), sizeof(w_spk));
    m.push_back(0);
    m.append("");
    msg_hash = crypto::cn_fast_hash(m.data(), m.size());
  }
  crypto::public_key w_pk{}; crypto::secret_key_to_public_key(w_ssk, w_pk);
  crypto::signature sig{};
  crypto::generate_signature(msg_hash, w_pk, w_ssk, sig);
  cryptonote::tx_extra_mevatrust_circle_vote op{};
  op.op_type = cryptonote::tx_extra_mevatrust_circle_vote::FINALIZE_VOTE;
  op.proposal_id = pid;
  op.circle_id = zero_id;
  op.target_pubkey = w_spk;
  op.signer_pubkey = w_spk;
  op.vote_yes = false;
  op.reason = "";
  op.signature = sig;
  std::vector<uint8_t> extra;
  if (!cryptonote::mevatrust::build_mevatrust_circle_vote_extra(op, extra)) {
    tools::fail_msg_writer() << tr("Errore serializzazione vote_extra"); return true;
  }
  tools::msg_writer() << "Finalizzazione voto on-chain in corso...";
  const std::string txid = submit_mevatrust_tx(m_wallet.get(), extra);
  if (txid.empty()) { tools::fail_msg_writer() << tr("Invio tx fallito."); return true; }
  tools::success_msg_writer() << "\033[1;32m*** TX FINALIZZAZIONE INVIATA! ***\033[0m"
    << "\n  TXID : " << txid
    << "\n  Se la votazione ha superato il quorum, l'admin sara' cambiato.";
  return true;
}

bool simple_wallet::cmd_circle_proposal_list(const std::vector<std::string>& args) {
  if (args.empty()) {
    tools::fail_msg_writer() << tr("Uso: circle_proposal_list <circle_id>"); return true;
  }
  rpc::COMMAND_RPC_CIRCLE_PROPOSAL_LIST::request req;
  rpc::COMMAND_RPC_CIRCLE_PROPOSAL_LIST::response res;
  req.circle_id = args[0];
  if (!m_wallet->invoke_http_json_rpc("/json_rpc", "circle_proposal_list", req, res)) {
    tools::fail_msg_writer() << tr("Errore RPC circle_proposal_list"); return true;
  }
  if (res.proposals.empty()) {
    tools::msg_writer() << tr("Nessuna proposta per questa cerchia.");
    return true;
  }
  tools::msg_writer() << "\n=== Proposte per cerchia " << args[0].substr(0,16) << "... ===";
  for (const auto& p : res.proposals) {
    std::string stato;
    if (p.status == 0) stato = "APERTA (1ª convocazione)";
    else if (p.status == 1) stato = "APERTA (2ª convocazione)";
    else if (p.status == 2) stato = "APPROVATA";
    else if (p.status == 3) stato = "BOCCIATA";
    else stato = "SCONOSCIUTO";
    tools::msg_writer() << "  ID     : " << p.proposal_id.substr(0,16) << "...";
    tools::msg_writer() << "  Target : " << p.target_pk.substr(0,16) << "...";
    tools::msg_writer() << "  Stato  : " << stato;
    tools::msg_writer() << "  Voti   : SI " << p.yes_count << " / NO " << p.no_count;
  }
  return true;
}

} // namespace cryptonote


