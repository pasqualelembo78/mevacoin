// Copyright (c) 2024, The Mevacoin Project
// mevatrust_manager.h -- Orchestrates all MevaTrust subsystems
//
// [C2 FIX] Badge On-Chain:
//   - m_node_sk / m_node_pk / m_node_key_set : chiave nodo per firmare badge awards
//   - m_pending_snapshot_extra / m_has_pending_snapshot : buffer 0xA2 per miner_tx
//   - consume_pending_snapshot_extra() : restituisce e svuota il buffer
//   Flusso: on_new_block() period boundary -> build snapshot -> m_pending_snapshot_extra
//           blockchain.cpp chiama consume_pending_snapshot_extra() prima di construct_miner_tx
//           -> 0xA2 finisce nel miner_tx -> tutti i nodi lo parsano via process_mevatrust_txs

#pragma once
#include <string>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <cstdint>
#include <atomic>
#include <functional>
#include "node_registry.h"
#include "circle_registry.h"
#include "mevatrust_engine.h"
#include "availability_proof.h"
#include "badge_system.h"
#include "reward_distributor.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "mevatrust_types.h"
#include "snapshot_broadcaster.h"
#include "mevatrust_store_registry.h"
#include "frost_broadcaster.h"
#include <memory>

namespace cryptonote {

class MevaTrustManager {
public:
  MevaTrustManager() = default;
  ~MevaTrustManager();
  MevaTrustManager(const MevaTrustManager&) = delete;
  MevaTrustManager& operator=(const MevaTrustManager&) = delete;

  bool init(const std::string& data_dir, network_type nettype, uint8_t hf_version = 1);
  void shutdown();
  bool is_initialized() const { return m_initialized.load(); }

  void on_new_block(uint64_t height, uint64_t block_reward, uint8_t hf_version);

  std::vector<NodeCoinbaseReward> get_coinbase_rewards(
    uint64_t height, uint64_t total_block_reward, uint64_t& miner_reward_out);

  std::shared_ptr<NodeRegistry>              node_registry()        const { return m_node_registry; }
  std::shared_ptr<CircleRegistry>            circle_registry()      const { return m_circle_registry; }
  std::shared_ptr<MevaTrustEngine>       mevatrust_engine() const { return m_mevatrust_engine; }
  std::shared_ptr<AvailabilityProofEngine>   availability_proof()   const { return m_availability_proof; }
  std::shared_ptr<BadgeSystem>               badge_system()         const { return m_badge_system; }
  std::shared_ptr<RewardDistributor>         reward_distributor()   const { return m_reward_distributor; }
  std::shared_ptr<StoreRegistry>             store_registry()       const { return m_store_registry; }

  // ?? Fase 2: callback types ?????????????????????????????????????????????
  using GetBlockFunc    = std::function<bool(uint64_t height, cryptonote::block& b)>;
  using GetTxFunc       = std::function<bool(const crypto::hash& txid, cryptonote::transaction& tx)>;
  using BroadcastTxFunc = std::function<bool(const std::vector<uint8_t>&, uint64_t)>;

  // ?? Fase 2: On-Chain processing ????????????????????????????????????????
  void process_mevatrust_txs(const cryptonote::block& bl, uint64_t height);
  void rebuild_registry_from_chain(uint64_t start_height, uint64_t end_height);
  void set_get_block_func(GetBlockFunc fn);
  void set_get_tx_func(GetTxFunc fn);

  // ?? Fase 3: Snapshot Broadcaster ?????????????????????????????????????
  void set_broadcast_tx_func(BroadcastTxFunc fn);
  void set_node_key(const crypto::secret_key& sk, const crypto::public_key& pk);

  // [C4] Multi-proposer quorum ballot
  // Badge applicato via P2P SOLO quando >= QUORUM_THRESHOLD proposer distinti concordano.
  // La path on-chain usa il consensus blockchain come quorum implicito.
  struct QuorumConfig {
    static constexpr uint32_t QUORUM_THRESHOLD        = 3;   // min proposer distinti
    static constexpr uint32_t MAX_PERIODS_KEPT        = 4;   // purge automatico
    static constexpr uint32_t MAX_PROPOSERS_PER_BADGE = 64;  // anti-spam
  };
  struct QuorumVoteResult {
    crypto::hash node_id;
    uint8_t      badge_type;
    uint64_t     awarded_height;
  };
  // Accumula il voto del proposer. Restituisce badge che raggiungono il quorum.
  // Thread-safe.
  std::vector<QuorumVoteResult> submit_snapshot_for_quorum(
    const tx_extra_mevatrust_snapshot& snap
  );

  // ?? [C2] Badge On-Chain: pending 0xA2 snapshot per miner_tx ??????????
  // Restituisce il blob 0xA2 pronto da appendere al miner_tx extra.
  // Chiamare PRIMA di construct_miner_tx_with_mevatrust().
  // Dopo la chiamata il buffer e' svuotato (consume semantics).
  std::vector<uint8_t> consume_pending_snapshot_extra();

  // ?? Pool Distribution: pending 0xAA blob (FROST-authorized) ??
  // Restituisce il blob 0xAA da appendere al miner_tx extra.
  std::vector<uint8_t> consume_pending_pool_distribution_extra();

  // ── Reorg rollback ──────────────────────────────────────────────────────
  // Chiamato quando un blocco viene rimosso dalla catena principale (reorg).
  // Inverte le modifiche allo stato MevaTrust applicate da quel blocco.
  void on_mevatrust_block_popped(const cryptonote::block& bl, uint64_t height);

  // ── State commitment (fork resistance) ───────────────────────────────────
  // Calcola un hash Merkle dello stato MevaTrust corrente (nodi + cerchie + badge + pool_balance).
  // Questo root viene incluso nella coinbase tx_extra (tag 0xA7) di ogni blocco.
  crypto::hash compute_mevatrust_state_root() const;

  // Verifica che lo state root calcolato localmente matchi quello nel blocco.
  // Se non matcha => fork detection => blocco rifiutato.
  bool verify_mevatrust_state_root(const cryptonote::block& bl, uint64_t height) const;

  // ── Validator system ─────────────────────────────────────────────────────
  void promote_to_validator(const crypto::hash& node_id, uint64_t height, uint64_t stake = 0);
  bool check_auto_validator_promotion(uint64_t height);
  bool is_validator(const crypto::hash& node_id) const;
  uint32_t get_validator_count() const;

  // Attiva/disattiva verifica state root allo switch HF
  // ── Node public key accessor (per RPC get_node_pubkey) ────────────
  crypto::public_key get_node_pk() const { std::lock_guard<std::mutex> lk(m_lock); return m_node_pk; }
  bool has_node_key() const { std::lock_guard<std::mutex> lk(m_lock); return m_node_key_set; }

  void set_state_root_verification_enabled(bool en) { m_state_root_verification_enabled = en; }
  bool is_state_root_verification_enabled() const { return m_state_root_verification_enabled; }

  // Calcola pool_balance on-chain: somma 3% contributi - distribuzioni eseguite
  uint64_t compute_pool_balance_from_chain() const;

  // Proposer key management for FROST signing
  void set_proposer_keypairs(const std::array<mevatrust::frost::SignerKeypair, mevatrust::frost::FROST_N>& kp);
  bool has_proposer_keys() const { return m_has_proposer_keys; }

  // P2P FROST coordination
  void set_frost_broadcast_func(FrostBroadcaster::BroadcastFunc fn);
  FrostBroadcaster* frost_broadcaster() { return m_frost_broadcaster.get(); }
  const FrostBroadcaster* frost_broadcaster() const { return m_frost_broadcaster.get(); }

  void set_pool_fraction_percent(uint32_t pct);
  void set_min_score_threshold(float s);
  void set_period_length(uint32_t blocks);
  uint32_t period_length() const { return m_period_length; }

private:
  std::atomic<bool>  m_initialized{false};
  network_type       m_nettype{MAINNET};
  uint8_t            m_hf_version{1};
  std::string        m_data_dir;
  mutable std::mutex m_lock;

  std::shared_ptr<NodeRegistry>              m_node_registry;
  std::shared_ptr<CircleRegistry>            m_circle_registry;
  std::shared_ptr<MevaTrustEngine>       m_mevatrust_engine;
  std::shared_ptr<AvailabilityProofEngine>   m_availability_proof;
  std::shared_ptr<BadgeSystem>               m_badge_system;
  std::shared_ptr<RewardDistributor>         m_reward_distributor;
  std::shared_ptr<StoreRegistry>             m_store_registry;

  std::vector<NodeCoinbaseReward> m_resolved_rewards;
  uint64_t m_resolved_at_height{0};
  uint64_t m_resolved_pool_balance{0};
  uint32_t m_period_length{240};
  uint64_t m_last_period_height{0};

  GetBlockFunc                 m_get_block_func;
  GetTxFunc                    m_get_tx_func;
  std::unique_ptr<SnapshotBroadcaster> m_snapshot_broadcaster;

  // [C4] Quorum accumulator state
  // Key   = period(4B) || node_id(32B) || badge_type(1B)
  // Value = set of proposer_pubkey(32B) that voted for this badge this period
  std::map<std::string, std::set<std::string>> m_quorum_votes;
  mutable std::mutex m_quorum_lock;
  uint32_t           m_last_quorum_period{0};

  // [C2] Node key for signing badge awards embedded in miner_tx
  crypto::secret_key    m_node_sk{};
  crypto::public_key    m_node_pk{};
  bool                  m_node_key_set{false};

  // [C2] Pending 0xA2 snapshot blob to embed in next miner_tx
  std::vector<uint8_t>  m_pending_snapshot_extra;
  bool                  m_has_pending_snapshot{false};

  // ?? Pool Distribution: pending 0xAA blob (FROST-authorized) ??
  std::vector<uint8_t>  m_pending_pool_distribution_extra;
  bool                  m_has_pending_pool_distribution{false};

  // Proposer keypairs for FROST signing
  std::array<mevatrust::frost::SignerKeypair, mevatrust::frost::FROST_N> m_proposer_keypairs{};
  bool m_has_proposer_keys{false};

  // P2P FROST coordinator
  std::unique_ptr<FrostBroadcaster> m_frost_broadcaster;

  // ── State commitment (fork resistance) ──────────────────────────────────
  bool                  m_state_root_verification_enabled{true};
  // Reorg rollback: tiene traccia delle operazioni per ogni blocco
  struct PoppedOp {
    enum Op { REGISTER, DEREGISTER, BADGE_AWARD, BADGE_REVOKE,
              CIRCLE_CREATE, CIRCLE_DISBAND, CIRCLE_JOIN, CIRCLE_LEAVE,
              CIRCLE_CHANGE_ADMIN, PENALTY, UPTIME, CHALLENGE, STORE,
              CIRCLE_PROPOSE, CIRCLE_VOTE, CIRCLE_FINALIZE, POOL_DISTRIBUTION,
              VALIDATOR_PROMOTION };
    Op op;
    crypto::hash node_id;
    crypto::hash circle_id;
    crypto::public_key pubkey;
    uint8_t badge_type{0};
    std::string reorg_data;  // dati extra per rollback (es. CircleEntry serializzata)
  };
  std::map<uint64_t, std::vector<PoppedOp>> m_reorg_log;

  void trigger_distribution(uint64_t height);
  bool parse_wallet_address(const std::string& addr_str, account_public_address& out) const;

  // [C2] Internal: builds and stores pending snapshot at period boundary
  void build_pending_snapshot(uint64_t height);
};

namespace mevatrust {
  void                  set_manager(std::shared_ptr<MevaTrustManager> mgr);
  MevaTrustManager* get_manager();
  void set_my_node_id(const crypto::hash& id);
  bool get_my_node_id(crypto::hash& out);
}

} // namespace cryptonote




