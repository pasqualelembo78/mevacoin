// Copyright (c) 2024, The Mevacoin Project
// node_registry.h  — NodeRegistry: registro persistente dei nodi in rete.
//
// FIXES applicati (rispetto alla versione precedente):
//   - extern "C" per lmdb.h gestito in mevatrust_lmdb.h (NON qui)
//   - Aggiunto MDB_env*/MDB_dbi come forward declarations per i membri privati
//   - get_all_nodes() rimossa definizione inline (ridefinita nel .cpp)
//   - clear_all() cambiato da void a bool (corrisponde a .cpp L392)
//   - Aggiunto migrate_legacy_file() come metodo privato (usato nel ctor)
//   - update_node_seen() -> update_node_heartbeat() (allineato al .cpp)
//   - update_node_sync_status() -> update_node_sync() (allineato al .cpp)
//   - record_challenge_attempt() -> update_node_challenge_result() (allineato)
//   - Aggiunto m_env, m_dbi come membri privati (usati in tutto il .cpp)
#pragma once

// Forward declarations LMDB (evita di includere lmdb.h in questo header)
struct MDB_env;                      // opaque LMDB environment handle
typedef unsigned int MDB_dbi;        // matches lmdb.h: typedef unsigned int MDB_dbi

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <mutex>
#include "crypto/crypto.h"
#include "cryptonote_basic/tx_extra.h"
#include "serialization/keyvalue_serialization.h"

namespace cryptonote {

enum class NodeStatus : uint8_t { ACTIVE=0, OFFLINE=1, SUSPENDED=2, BANNED=3 };

struct NodeRegistryEntry {
  crypto::hash node_id;
  std::string wallet_address;
  crypto::public_key wallet_pubkey, node_pubkey;
  uint64_t registered_height, registered_timestamp;
  crypto::signature registration_signature;
  std::string ip_address;
  uint16_t port;
  uint64_t last_seen_timestamp;
  uint32_t peer_count;
  NodeStatus status;
  uint64_t last_sync_height;
  bool is_synchronized;
  float reputation_score;
  uint64_t created_at, updated_at, last_challenge_time, next_challenge_time;
  uint32_t total_challenges, successful_challenges;
  uint64_t total_uptime_seconds;
  uint32_t disconnections;
  bool     is_validator{false};
  uint64_t validator_since_height{0};
  uint64_t validator_tx_amount{0};
  std::string metadata;
  // Alias per retrocompatibilità test (wallet_key/node_key -> wallet_pubkey/node_pubkey)
  const crypto::public_key& wallet_key() const { return wallet_pubkey; }
  const crypto::public_key& node_key()   const { return node_pubkey; }
  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(wallet_address) KV_SERIALIZE(registered_height) KV_SERIALIZE(registered_timestamp)
    KV_SERIALIZE(ip_address) KV_SERIALIZE(port) KV_SERIALIZE(last_seen_timestamp) KV_SERIALIZE(peer_count)
    KV_SERIALIZE_VAL_POD_AS_BLOB_N(status,"status") KV_SERIALIZE(last_sync_height)
    KV_SERIALIZE(is_synchronized) KV_SERIALIZE(reputation_score) KV_SERIALIZE(total_challenges)
    KV_SERIALIZE(successful_challenges) KV_SERIALIZE(total_uptime_seconds) KV_SERIALIZE(disconnections)
  END_KV_SERIALIZE_MAP()
};

class NodeRegistry {
public:
  NodeRegistry(const std::string& db_path);
  ~NodeRegistry();
  bool open();   // apre il LMDB (alias di open_database())

  // ── Registrazione nodi ────────────────────────────────────────────────────
  bool register_node(const crypto::public_key& wallet_pk,
                     const std::string& address,
                     const crypto::public_key& node_pk,
                     const crypto::signature& sig,
                     uint16_t port,
                     const std::string& ip,
                     uint64_t height);
  bool unregister_node(const crypto::hash& node_id);

  // ── Query ─────────────────────────────────────────────────────────────────
  bool get_node_by_id(const crypto::hash&, NodeRegistryEntry&);
  bool get_node_by_wallet(const std::string&, NodeRegistryEntry&);
  bool get_node_by_pubkey(const crypto::public_key&, NodeRegistryEntry&);

  // ── Listing ───────────────────────────────────────────────────────────────
  std::vector<NodeRegistryEntry> get_active_nodes(uint32_t limit=0, uint32_t offset=0);
  std::vector<NodeRegistryEntry> get_all_nodes();           // definito in .cpp
  std::vector<NodeRegistryEntry> get_synchronized_nodes();
  std::vector<NodeRegistryEntry> get_nodes_by_status(NodeStatus s);
  uint32_t count_active_nodes();
  uint32_t get_active_node_count() const;
  uint32_t get_total_registered_nodes() const;
  uint32_t get_nodes_registered_this_period() const;
  float    get_network_average_uptime() const;

  // ── Aggiornamenti stato nodo ──────────────────────────────────────────────
  bool update_node_status(const crypto::hash&, NodeStatus, const std::string& reason="");
  // FIX: update_node_heartbeat corrisponde a .cpp L249 (era update_node_seen in .h)
  bool update_node_heartbeat(const crypto::hash&, uint64_t timestamp_ms,
                             uint32_t peers, uint64_t sync_height);
  // FIX: update_node_sync corrisponde a .cpp L259 (era update_node_sync_status in .h)
  bool update_node_sync(const crypto::hash&, uint64_t sync_height, bool synced);
  // FIX: update_node_challenge_result corrisponde a .cpp L266 (era record_challenge_attempt)
  bool update_node_challenge_result(const crypto::hash&, bool success);
  bool update_node_challenge_time(const crypto::hash&, uint64_t next_ms);
  bool update_node_validator(const crypto::hash& n, bool is_val, uint64_t since_h, uint64_t tx_amt);
  bool update_reputation(const crypto::hash&, float delta);
  bool reset_reputation_suspect(const crypto::hash&);
  bool ban_node(const crypto::hash&, const std::string& reason);
  bool unban_node(const crypto::hash&);
  bool record_uptime_event(const crypto::hash&, bool online, uint64_t ts, uint32_t peers, uint32_t discon);
  bool verify_node_signature(const crypto::hash&, const crypto::signature&, const crypto::hash& msg);

  // ── Fase 2 (on-chain) ─────────────────────────────────────────────────────
  bool register_node_onchain(const tx_extra_mevatrust_registration&, uint64_t height);
  bool deregister_node_onchain(const tx_extra_mevatrust_deregister&);
  uint32_t expire_inactive_nodes(uint64_t current_height, uint64_t max_offline_blocks=720);

  // ── Persistenza ───────────────────────────────────────────────────────────
  bool save_to_disk() const;
  bool load_from_disk();
  bool sync_database();
  bool prune_old_data(uint64_t before_height);

  // ── Utility ───────────────────────────────────────────────────────────────
  // FIX: clear_all() ora restituisce bool (corrisponde a .cpp L392)
  bool clear_all();
  bool is_wallet_pubkey_registered(const crypto::public_key&) const;
  const std::string& get_db_path() const { return db_path_; }

private:
  std::string db_path_;
  std::map<std::string, NodeRegistryEntry> nodes_;
  mutable std::mutex nodes_lock_;
  static constexpr uint32_t MAX_NODES_PER_WALLET = 3;
  std::map<std::string, std::vector<std::string>> wallet_to_nodes_;
  std::map<std::string, std::string> ip_to_node_;

  // FIX: m_env e m_dbi usati nel .cpp — devono essere dichiarati qui
  MDB_env* m_env{nullptr};
  MDB_dbi  m_dbi{0};

  crypto::hash compute_node_id(const crypto::public_key& wallet_pk,
                               const crypto::public_key& node_pk,
                               uint64_t height);
  bool verify_registration_signature(const crypto::public_key&,
                                     const crypto::signature&,
                                     const std::string& address);
  bool open_database();
  bool close_database();
  // FIX: migrate_legacy_file() usata nel ctor del .cpp — deve essere dichiarata
  void migrate_legacy_file();
};

std::string node_status_to_string(NodeStatus s);
NodeStatus  string_to_node_status(const std::string& s);

} // namespace cryptonote

