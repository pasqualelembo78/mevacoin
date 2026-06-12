// Copyright (c) 2024, The Mevacoin Project
// All rights reserved.

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <map>
#include <unordered_map>
#include <mutex>
#include "crypto/crypto.h"
#include "node_registry.h"

namespace cryptonote {

// ============================================================================
// AVAILABILITY PROOF PROTOCOL
// ============================================================================

enum class ChallengeType : uint8_t {
  BLOCK_HASH = 0,               // What is block hash at height X?
  BLOCK_HEIGHT = 1,             // What is the height of block with hash Y?
  TX_HASH = 2,                  // What is transaction hash at index X?
  TX_EXISTS = 3,                // Does transaction Y exist in chain?
  UTXO_EXISTS = 4,              // Does UTXO Y exist (Merkle proof)?
  LAST_BLOCK_HASH = 5           // What is current tip block hash?
};

struct AvailabilityChallenge {
  crypto::hash challenge_id;     // Unique ID for this challenge
  crypto::hash node_id;          // Target node
  ChallengeType challenge_type;
  
  // Challenge parameters
  union {
    uint64_t block_height;       // For BLOCK_HASH, BLOCK_HEIGHT
    uint32_t tx_index;           // For TX_HASH, TX_EXISTS
  } parameter;
  
  std::string additional_data;   // For complex challenges (serialized)
  
  // Challenge lifecycle
  uint64_t sent_timestamp;
  uint64_t timeout_ms;           // Default: 2000
  uint64_t sender_id;            // ID of validator sending challenge
  std::string sender_ip;         // For routing response
  uint16_t sender_port;          // For routing response
  
  // Tracking
  bool responded;
  uint64_t response_timestamp;
  uint32_t response_time_ms;
  bool response_correct;
  std::string response_data;
  std::string rejection_reason;

  // Serialization
  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE_VAL_POD_AS_BLOB_N(challenge_type, "challenge_type")
    KV_SERIALIZE(parameter.block_height)
    KV_SERIALIZE(sent_timestamp)
    KV_SERIALIZE(timeout_ms)
    KV_SERIALIZE(responded)
    KV_SERIALIZE(response_correct)
  END_KV_SERIALIZE_MAP()
};

struct AvailabilityResponse {
  crypto::hash challenge_id;
  crypto::hash node_id;
  
  // Response data
  std::string response_data;     // The actual answer (block hash, tx hash, etc)
  crypto::signature node_signature;  // Signed by node's keypair
  
  // Timing
  uint64_t response_timestamp;
  uint32_t processing_time_ms;   // How long it took node to compute
  
  // Proof data (if needed)
  std::string merkle_proof;      // For UTXO validation
  
  // Serialization
  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(response_data)
    KV_SERIALIZE(response_timestamp)
    KV_SERIALIZE(processing_time_ms)
  END_KV_SERIALIZE_MAP()
};

// ============================================================================
// PROOF OF AVAILABILITY ENGINE
// ============================================================================

class AvailabilityProofEngine {
public:
  AvailabilityProofEngine(
    const std::string& db_path,
    std::shared_ptr<NodeRegistry> node_registry
  );
  ~AvailabilityProofEngine();

  // ========================================================================
  // CHALLENGE GENERATION & MANAGEMENT
  // ========================================================================

  // Generate a new random challenge for a node
  AvailabilityChallenge generate_challenge(
    const crypto::hash& node_id,
    uint64_t current_height,
    uint64_t current_timestamp
  );

  // Get all pending challenges
  std::vector<AvailabilityChallenge> get_pending_challenges(const crypto::hash& node_id);

  // Check if node has expired challenges
  bool has_expired_challenges(const crypto::hash& node_id, uint64_t current_timestamp);

  // Get challenge by ID
  bool get_challenge(const crypto::hash& challenge_id, AvailabilityChallenge& challenge);

  // ========================================================================
  // RESPONSE HANDLING
  // ========================================================================

  // Record a response to a challenge
  bool record_response(
    const AvailabilityChallenge& challenge,
    const AvailabilityResponse& response,
    uint64_t current_timestamp
  );

  // Validate response correctness
  enum class ResponseValidation {
    VALID = 0,
    INVALID_DATA = 1,
    INVALID_SIGNATURE = 2,
    TIMEOUT = 3,
    NOT_FOUND = 4,
    INTEGRITY_ERROR = 5
  };

  ResponseValidation validate_response(
    const AvailabilityChallenge& challenge,
    const AvailabilityResponse& response,
    uint64_t current_height
  );

  // ========================================================================
  // CHALLENGE SCHEDULING
  // ========================================================================

  // Get nodes due for challenge
  std::vector<crypto::hash> get_nodes_due_for_challenge(uint64_t current_timestamp);

  // Schedule challenges for period
  uint32_t schedule_period_challenges(
    uint64_t current_height,
    uint64_t current_timestamp,
    uint32_t challenges_per_node = 3
  );

  // Get next challenge time for node
  uint64_t get_next_challenge_time(const crypto::hash& node_id);

  // ========================================================================
  // STATISTICS & REPORTING
  // ========================================================================

  struct ChallengeStatistics {
    crypto::hash node_id;
    uint32_t total_challenges;
    uint32_t successful_responses;
    uint32_t failed_responses;
    uint32_t expired_challenges;
    uint32_t invalid_responses;
    float success_rate;
    float average_response_time_ms;
    uint64_t last_challenge_time;
    uint64_t next_challenge_time;
    uint32_t consecutive_failures;
    uint32_t consecutive_successes;
  };

  ChallengeStatistics get_node_challenge_statistics(const crypto::hash& node_id);

  // Get network-wide challenge statistics
  struct NetworkChallengeStats {
    uint32_t total_challenges_sent;
    uint32_t total_responses_received;
    uint32_t total_expired;
    float average_success_rate;
    float average_response_time_ms;
    uint32_t nodes_with_perfect_score;
    uint32_t nodes_with_failing_score;
  };

  NetworkChallengeStats get_network_challenge_statistics();

  // ========================================================================
  // RESPONSE TIME TRACKING
  // ========================================================================

  bool record_response_time(const crypto::hash& node_id, uint32_t response_time_ms);
  float get_average_response_time(const crypto::hash& node_id, uint32_t lookback_count = 10);

  // ========================================================================
  // REPUTATION IMPACT
  // ========================================================================

  // Calculate reputation delta based on challenge results
  float calculate_reputation_impact(
    const ChallengeStatistics& stats,
    float current_reputation
  );

  // ========================================================================
  // CHALLENGE PARAMETERS (Configurable)
  // ========================================================================

  struct AvailabilityParameters {
    uint64_t challenge_interval_hours;         // Default: 10 hours
    uint32_t challenges_per_period;            // Default: 3-5
    uint32_t response_timeout_ms;              // Default: 2000ms
    float success_rate_threshold;              // Default: 0.66 (2/3)
    uint32_t max_consecutive_failures;         // Default: 3
    float reputation_penalty_per_failure;      // Default: 0.05
    float reputation_reward_per_success;       // Default: 0.02
    bool verify_block_hash;                    // Verify responses against chain
  };

  void set_parameters(const AvailabilityParameters& params);
  AvailabilityParameters get_parameters() const;

  // ========================================================================
  // CHAIN DATA ACCESS (For validation)
  // ========================================================================

  // These are called to validate responses
  // Implemented in blockchain/core layer
  typedef std::function<bool(uint64_t, crypto::hash&)> GetBlockHashFunc;
  typedef std::function<bool(const crypto::hash&, uint64_t&)> GetBlockHeightFunc;
  typedef std::function<bool(uint32_t, crypto::hash&)> GetTxHashFunc;

  void set_block_hash_func(GetBlockHashFunc func);
  void set_block_height_func(GetBlockHeightFunc func);
  void set_tx_hash_func(GetTxHashFunc func);

  // ========================================================================
  // PERSISTENCE
  // ========================================================================

  bool save_to_disk() const;
  bool load_from_disk();
  bool sync_database();

  // ========================================================================
  // DATA CLEANUP
  // ========================================================================

  // Prune old challenge history (keep last 30 days)
  bool prune_old_challenges(uint64_t keep_before_timestamp);

  typedef std::function<bool(const AvailabilityChallenge& challenge)> SendChallengeFunc;
  void set_send_challenge_func(SendChallengeFunc func);
  bool send_challenge(const crypto::hash& target_node_id, uint64_t current_height, uint64_t current_timestamp_ms);
  bool process_challenge_response(const AvailabilityResponse& response, uint64_t current_height);

private:
  std::string db_path_;
  std::shared_ptr<NodeRegistry> node_registry_;

  // In-memory caches
  std::unordered_map<crypto::hash, std::vector<AvailabilityChallenge>> pending_challenges_;
  std::unordered_map<crypto::hash, std::vector<AvailabilityChallenge>> challenge_history_;
  std::unordered_map<crypto::hash, std::vector<uint32_t>> response_times_;
  mutable std::mutex cache_lock_;

  // Configuration
  AvailabilityParameters parameters_;

  // Blockchain data access functions
  GetBlockHashFunc get_block_hash_;
  GetBlockHeightFunc get_block_height_;
  GetTxHashFunc get_tx_hash_;

  // Helper methods
  std::string generate_random_data(size_t bytes);
  crypto::hash compute_challenge_id(
    const crypto::hash& node_id,
    uint64_t timestamp,
    ChallengeType type
  );

  bool verify_block_response(
    uint64_t block_height,
    const std::string& response_data,
    const crypto::hash& node_id
  );

  bool open_database();
  bool close_database();

  std::string challenge_type_to_string(ChallengeType type) const;
  SendChallengeFunc send_challenge_func_;
};

} // namespace cryptonote

