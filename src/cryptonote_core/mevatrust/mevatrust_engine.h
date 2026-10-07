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
// MEVATRUST SCORE STRUCTURES
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
  
  // Serialization for storage (KV format for RPC, raw binary for persistence)
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

  // Serialization for storage (raw binary for persistence)
  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(timestamp)
    KV_SERIALIZE(online)
    KV_SERIALIZE(block_height)
    KV_SERIALIZE(peer_count)
    KV_SERIALIZE(response_time_ms)
    KV_SERIALIZE(ip_address)
  END_KV_SERIALIZE_MAP()
};

// ============================================================================
// MEVATRUST ENGINE
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

  // ========================================================================
  // UPTIME DERIVATO DALLA CATENA (regola 3% v3)
  // ========================================================================
  //
  // Quanto segue e' la grandezza su cui la v3 decide chi viene pagato, e sostituisce
  // lo score composed di calculate_node_score(). Vive in una struttura separata da
  // uptime_history_ per una ragione precisa: uptime_history_ alimenta lo score usato
  // dalla v2, e riempirla avrebbe cambiato il risultato dei blocchi gia' prodotti,
  // rendendo invalida la storia. Due strutture, due epoche, nessuna retroattivita'.

  /// Una prova di presenza accettata dalla catena.
  ///
  /// block_ts e' il timestamp del BLOCCO che contiene il 0xA5, non il timestamp che il
  /// nodo si e' firmato da solo. E' la differenza fra una prova e un'opinione: il primo
  /// e' un dato che il consenso gia' valida e che due nodi onesti leggono identico, il
  /// secondo e' un numero che il nodo sceglie e che potrebbe valere anche dieci anni.
  struct ChainUptimeSample {
    uint64_t height{0};
    uint64_t block_ts{0};
  };

  /// La posizione di un nodo nella finestra scorrevole, e quanto vale come quota.
  struct UptimeStanding {
    uint64_t credited_seconds{0};  ///< uptime accreditato dentro la finestra
    uint32_t samples{0};           ///< campioni considerati
    double days{0.0};              ///< credited_seconds / 86400
    double weight{0.0};            ///< min(days, tetto) — la chiave di divisione
    bool eligible{false};          ///< days >= soglia
  };

  /// Registra una prova di presenza. Idempotente per altezza: due 0xA5 dello stesso nodo
  /// nello stesso blocco valgono un campione solo, altrimenti un nodo che infila piu'
  /// firme in un blocco — cosa che puo' fare finche' i suoi 0xA5 ci stanno — guadagnerebbe
  /// crediti che non corrispondono a tempo realmente trascorso.
  bool record_chain_uptime_sample(
    const crypto::hash& node_id,
    uint64_t height,
    uint64_t block_ts
  );

  /// Calcola la posizione del nodo alla data altezza.
  ///
  /// Il credito di ogni coppia di campioni consecutivi e' il delta fra i loro timestamp
  /// di blocco, limitato a MAX_UPTIME_CREDIT_GAP_SECONDS: se il nodo e' sparito per un
  /// mese e torna, il salto non vale un mese. I campioni fuori dalla finestra non
  /// contano, ed e' questo che produce il decadimento — un nodo fermo vede il suo
  /// credito scorrere fuori dalla finestra un giorno alla volta invece di azzerarsi.
  UptimeStanding compute_uptime_standing(
    const crypto::hash& node_id,
    uint64_t height
  ) const;

  /// Ci sono campioni salvati? Al boot serve a capire se il nodo sta per leggere le
  /// quote da un archivio vuoto: in quel caso, se la catena non e' ai primi blocchi, i
  /// campioni vanno ricostruiti da lei prima di calcolare qualsiasi quota.
  bool has_chain_uptime_samples() const;

  /// Svuota i campioni. Serve al rebuild, che deve ripartire da zero come fa col registry.
  void clear_chain_uptime_samples();

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

  // Prove di presenza derivate dalla catena, per la regola 3% v3. Tenute separate da
  // uptime_history_ per non perturbare il percorso pre-v17: vedi compute_uptime_standing().
  std::map<std::string, std::vector<ChainUptimeSample>> chain_uptime_samples_;

  mutable std::recursive_mutex cache_lock_;

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


