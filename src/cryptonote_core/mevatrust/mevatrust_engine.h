// Copyright (c) 2024, The Mevacoin Project
// All rights reserved.

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <map>
#include "crypto/crypto.h"
#include "node_registry.h"

namespace cryptonote {

// ============================================================================
// PARTICIPATION SCORE STRUCTURES
// ============================================================================

struct MevaTrustScoreSnapshot {
  crypto::hash node_id;
  uint64_t period_height;          // Ending block height of period
  uint64_t period_start_height;    // Starting block height
  
  // Component scores (0.0 to 1.0)
  float uptime_score;              // Percentage of time node was online
  float sync_score;                // Blockchain synchronization percentage
  float responsiveness_score;      // Avg response time to PoA challenges
  float activity_score;            // Transaction relay, peer updates, etc
  
  // Aggregates
  float total_score;               // Weighted sum
  uint32_t challenges_passed;      // PoA challenges succeeded
  uint32_t challenges_total;       // PoA challenges attempted
  
  // Timestamps
  uint64_t recorded_at;
  
  // Serialization for storage
  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(period_height)
    KV_SERIALIZE(period_start_height)
    KV_SERIALIZE(uptime_score)
    KV_SERIALIZE(sync_score)
    KV_SERIALIZE(responsiveness_score)
    KV_SERIALIZE(activity_score)
    KV_SERIALIZE(total_score)
    KV_SERIALIZE(challenges_passed)
    KV_SERIALIZE(challenges_total)
  END_KV_SERIALIZE_MAP()
};

// ============================================================================
// UPTIME EVENT
// ============================================================================

struct UptimeEvent {
  uint64_t timestamp;
  bool online;                     // true = came online, false = went offline
  uint64_t block_height;
  uint32_t peer_count;
  uint32_t response_time_ms;
  std::string ip_address;
};

// ============================================================================
// PARTICIPATION ENGINE
// ============================================================================

class MevaTrustEngine {
public:
  MevaTrustEngine(
    const std::string& db_path,
    std::shared_ptr<NodeRegistry> node_registry
  );
  ~MevaTrustEngine();

  // ========================================================================
  // SCORE CALCULATION
  // ========================================================================

  // Calculate current score for a node (based on recent activity)
  MevaTrustScoreSnapshot calculate_node_score(
    const crypto::hash& node_id,
    uint64_t current_height
  );

  // Batch calculation for all active nodes
  std::map<std::string, MevaTrustScoreSnapshot> calculate_all_scores(
    uint64_t current_height
  );

  // Get historical score for a specific period
  bool get_historical_score(
    const crypto::hash& node_id,
    uint64_t period_height,
    MevaTrustScoreSnapshot& score
  );

  // ========================================================================
  // UPTIME TRACKING
  // ========================================================================

  bool record_uptime_event(
    const crypto::hash& node_id,
    const UptimeEvent& event
  );

  float get_uptime_percentage(
    const crypto::hash& node_id,
    uint64_t lookback_height = 14400  // ~4 hours
  );

  float get_sync_percentage(
    const crypto::hash& node_id,
    uint64_t current_height,
    uint64_t lookback_height = 14400
  );

  uint64_t get_consecutive_uptime_seconds(const crypto::hash& node_id);
  uint64_t get_total_uptime_seconds(const crypto::hash& node_id);

  // ========================================================================
  // CHALLENGE & RESPONSIVENESS TRACKING
  // ========================================================================

  bool record_challenge_response(
    const crypto::hash& node_id,
    uint32_t response_time_ms,
    bool success
  );

  float get_average_response_time(const crypto::hash& node_id);
  float get_challenge_success_rate(const crypto::hash& node_id);

  // ========================================================================
  // ACTIVITY TRACKING
  // ========================================================================

  bool increment_activity_counter(const crypto::hash& node_id, const std::string& activity_type);
  float get_activity_score(const crypto::hash& node_id, uint64_t lookback_height = 14400);

  // ========================================================================
  // PERIOD MANAGEMENT
  // ========================================================================

  // Called every 240 blocks (~4 hours)
  bool process_reward_period(uint64_t current_height);

  // Get current period info
  uint64_t get_current_period_start() const;
  uint64_t get_current_period_end() const;
  uint64_t get_blocks_since_last_period(uint64_t current_height) const;

  // ========================================================================
  // AGGREGATES & REPORTING
  // ========================================================================

  struct NetworkStats {
    uint32_t total_active_nodes;
    uint32_t total_eligible_nodes;
    float average_uptime;
    float average_sync_percentage;
    float average_score;
    uint64_t total_reward_pool;
    float reward_pool_percentage;
  };

  NetworkStats get_network_statistics(uint64_t current_height);

  struct NodeStats {
    crypto::hash node_id;
    std::string wallet_address;
    float current_score;
    float uptime_percentage;
    float sync_percentage;
    float responsiveness;
    float activity;
    uint64_t uptime_hours;
    uint32_t challenges_passed;
    uint32_t challenges_total;
    float estimated_next_reward;
  };

  NodeStats get_node_statistics(const crypto::hash& node_id, uint64_t current_height);
  std::vector<NodeStats> get_top_nodes(uint32_t count, uint64_t current_height);

  // ========================================================================
  // SCORING WEIGHTS (Configurable)
  // ========================================================================

  struct ScoringWeights {
    float uptime_weight;           // Default: 0.40
    float sync_weight;             // Default: 0.30
    float responsiveness_weight;   // Default: 0.20
    float activity_weight;         // Default: 0.10
  };

  void set_scoring_weights(const ScoringWeights& weights);
  ScoringWeights get_scoring_weights() const;

  // ========================================================================
  // THRESHOLDS & PARAMETERS (Configurable)
  // ========================================================================

  struct MevaTrustParameters {
    uint64_t minimum_uptime_for_rewards;     // Blocks: 360 (1 hour)
    uint64_t period_length;                  // Blocks: 240 (~4 hours)
    uint32_t challenges_per_period;          // 3-5 challenges per node
    uint32_t challenge_timeout_ms;           // 2000ms default
    float minimum_score_for_rewards;         // 0.5 (50%)
    uint32_t sync_threshold_percentage;      // 99%
    float reputation_penalty_per_failure;    // 0.05
    float reputation_recovery_per_success;   // 0.02
  };

  void set_parameters(const MevaTrustParameters& params);
  MevaTrustParameters get_parameters() const;

  // ========================================================================
  // PERSISTENCE
  // ========================================================================

  bool save_to_disk() const;
  bool load_from_disk();
  bool sync_database();

  // ========================================================================
  // DATA CLEANUP
  // ========================================================================

  // Prune old uptime events and scores (keep last 90 days)
  bool prune_old_data(uint64_t keep_before_height);

private:
  std::string db_path_;
  std::shared_ptr<NodeRegistry> node_registry_;

  // In-memory caches
  std::map<std::string, std::vector<UptimeEvent>> uptime_history_;
  std::map<std::string, MevaTrustScoreSnapshot> score_cache_;
  mutable std::mutex cache_lock_;

  // Configuration
  ScoringWeights scoring_weights_;
  MevaTrustParameters parameters_;

  // Current period tracking
  uint64_t current_period_start_height_;
  uint64_t last_period_processed_height_;

  // Helper methods
  float calculate_uptime_score(
    const crypto::hash& node_id,
    uint64_t period_end_height
  );

  float calculate_sync_score(
    const crypto::hash& node_id,
    uint64_t period_end_height,
    uint64_t current_chain_height
  );

  float calculate_responsiveness_score(const crypto::hash& node_id);

  float calculate_activity_score(
    const crypto::hash& node_id,
    uint64_t period_end_height
  );

  bool load_uptime_history(const crypto::hash& node_id);
  bool save_uptime_history(const crypto::hash& node_id);

  bool open_database();
  bool close_database();
};

} // namespace cryptonote


