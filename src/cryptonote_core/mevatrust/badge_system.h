// Copyright (c) 2024, The Mevacoin Project
// All rights reserved.

#pragma once

// Forward declarations LMDB (evita di includere lmdb.h in questo header)
struct MDB_env;
typedef unsigned int MDB_dbi;


#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include "crypto/crypto.h"
#include "serialization/keyvalue_serialization.h"

namespace cryptonote {

// Forward declarations
class NodeRegistry;
class MevaTrustEngine;

// ============================================================================
// BADGE DEFINITIONS
// ============================================================================

enum class BadgeType : uint8_t {
  ACTIVE_MINER = 0,
  FULL_NODE_OPERATOR = 1,
  STABLE_NODE = 2,
  CORE_NETWORK_NODE = 3,
  LONG_UPTIME_NODE = 4,
  EARLY_SUPPORTER = 5,
  NETWORK_VALIDATOR = 6,
  BRIDGE_NODE = 7,
  PRIVACY_GUARDIAN = 8,
  RELAY_MASTER = 9,
  WELCOME = 10,
  BADGE_UNKNOWN = 255
};

struct Badge {
  BadgeType type;
  std::string name;
  std::string description;
  std::string icon_data;
  uint64_t awarded_height;
  uint64_t awarded_timestamp;
  bool is_active;
  std::string revocation_reason;
  std::map<std::string, std::string> metadata;

  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE_VAL_POD_AS_BLOB_N(type, "type")
    KV_SERIALIZE(name)
    KV_SERIALIZE(awarded_height)
    KV_SERIALIZE(awarded_timestamp)
    KV_SERIALIZE(is_active)
  END_KV_SERIALIZE_MAP()
};

struct BadgeRequirements {
  BadgeType type;
  std::string name;
  std::string description;
  uint64_t minimum_uptime_hours;
  float minimum_uptime_percentage;
  uint64_t minimum_days_registered;
  float minimum_sync_percentage;
  uint32_t minimum_block_mined;
  uint32_t minimum_peer_connections;
  uint32_t minimum_challenges_validated;
  uint32_t minimum_transactions_relayed;
  float revoke_if_uptime_below;
  float revoke_if_sync_below;
  uint32_t revoke_after_days_offline;
  bool is_permanent;
  bool is_renewable;
  uint64_t renewal_period_days;
};

// ============================================================================
// BADGE SYSTEM
// ============================================================================

class BadgeSystem {
public:
  // C2-FIX: On-chain callback
  using OnChainCallback = std::function<bool(const crypto::hash&,uint8_t,uint64_t,const std::string&)>;
  void set_on_chain_callback(OnChainCallback cb){std::lock_guard<std::mutex> lk(cache_lock_);on_chain_cb_=std::move(cb);}
  // ─────────────────────────────────────────────────────────────────────────
public:
  BadgeSystem(
    const std::string& db_path,
    std::shared_ptr<NodeRegistry> node_registry,
    std::shared_ptr<MevaTrustEngine> mevatrust_engine
  );
  ~BadgeSystem();

  std::vector<Badge> get_node_badges(const crypto::hash& node_id);
  std::vector<Badge> get_active_badges(const crypto::hash& node_id);
  bool has_badge(const crypto::hash& node_id, BadgeType badge_type);
  bool get_badge_details(BadgeType badge_type, Badge& badge_out);
  std::vector<BadgeType> get_badge_types_for_wallet(const std::string& wallet_address);

  bool award_badge(
    const crypto::hash& node_id,
    BadgeType badge_type,
    uint64_t current_height,
    const std::string& reason = ""
  );
  bool revoke_badge(
    const crypto::hash& node_id,
    BadgeType badge_type,
    const std::string& reason
  );
  bool auto_evaluate_badges(const crypto::hash& node_id, uint64_t current_height);
  uint32_t evaluate_all_badges(uint64_t current_height);

  bool qualifies_for_badge(
    const crypto::hash& node_id,
    BadgeType badge_type,
    uint64_t current_height
  );
  std::string get_disqualification_reason(
    const crypto::hash& node_id,
    BadgeType badge_type,
    uint64_t current_height
  );

  bool get_badge_requirements(BadgeType badge_type, BadgeRequirements& requirements);
  bool set_badge_requirements(const BadgeRequirements& requirements);
  std::vector<BadgeRequirements> get_all_requirements();

  struct BadgeStatistics {
    BadgeType type;
    std::string name;
    uint32_t total_awarded;
    uint32_t total_active;
    uint32_t total_revoked;
    float percentage_of_nodes;
    std::vector<crypto::hash> holders;
  };
  std::vector<BadgeStatistics> get_badge_statistics();

  struct NodeBadgeInfo {
    crypto::hash node_id;
    std::string wallet_address;
    std::vector<Badge> badges;
    uint32_t badge_count;
    uint32_t active_badge_count;
    float badge_score;
  };
  NodeBadgeInfo get_node_badge_info(const crypto::hash& node_id);
  std::vector<crypto::hash> get_nodes_with_badge(BadgeType badge_type);

  /// Ritorna (node_id, BadgeType) per badge assegnati in [from_height, to_height].
  /// Usato da SnapshotBroadcaster per costruire il payload della TX 0xA2.
  std::vector<std::pair<crypto::hash, BadgeType>>
  get_recently_awarded(uint64_t from_height, uint64_t to_height);

  struct TopHolder {
    crypto::hash node_id;
    std::string wallet_address;
    uint32_t active_badge_count;
    std::vector<BadgeType> badge_types;
  };
  std::vector<TopHolder> get_top_badge_holders(uint32_t count = 100);

  bool save_to_disk() const;
  bool load_from_disk();
  bool sync_database();
  uint32_t refresh_badge_validity(uint64_t current_height);
  bool prune_revoked_badges(uint64_t keep_before_height);

private:
  std::string db_path_;
  std::shared_ptr<NodeRegistry> node_registry_;
  std::shared_ptr<MevaTrustEngine> mevatrust_engine_;

  // std::map is fine for BadgeType (uint8_t enum, has operator<)
  // std::unordered_map required for crypto::hash (has std::hash but NOT operator<)
  std::map<BadgeType, BadgeRequirements> badge_requirements_;
  std::unordered_map<crypto::hash, std::vector<Badge>> badge_cache_;
  mutable std::mutex cache_lock_;
  OnChainCallback on_chain_cb_;

  bool initialize_default_badges();
  bool check_active_miner_criteria(const crypto::hash& node_id);
  bool check_full_node_operator_criteria(const crypto::hash& node_id, uint64_t current_height);
  bool check_stable_node_criteria(const crypto::hash& node_id, uint64_t current_height);
  bool check_core_network_node_criteria(const crypto::hash& node_id, uint64_t current_height);
  bool check_long_uptime_node_criteria(const crypto::hash& node_id, uint64_t current_height);
  bool check_early_supporter_criteria(const crypto::hash& node_id);
  bool check_network_validator_criteria(const crypto::hash& node_id);
  bool open_database();
  bool close_database();
  std::string badge_type_to_string(BadgeType type) const;
  BadgeType string_to_badge_type(const std::string& str) const;
  void migrate_legacy_file();
  // LMDB handles
  MDB_env* m_env = nullptr;
  MDB_dbi  m_dbi = 0;
};

// ============================================================================
// BADGE DISPLAY/UI HELPERS
// ============================================================================

struct BadgeDisplayInfo {
  std::string type_name;
  std::string display_name;
  std::string description;
  std::string icon_url;
  std::string color_hex;
  bool is_active;
};

BadgeDisplayInfo get_badge_display_info(BadgeType badge_type);

} // namespace cryptonote


