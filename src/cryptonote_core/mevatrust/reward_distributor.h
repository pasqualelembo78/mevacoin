// Copyright (c) 2024, The Mevacoin Project
// All rights reserved.
// reward_distributor.h — Extended header with on-chain coinbase support

#pragma once

// Forward declarations LMDB
struct MDB_env;
typedef unsigned int MDB_dbi;

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <memory>
#include "crypto/crypto.h"
#include "mevatrust_engine.h"

namespace cryptonote {

struct RewardRecord {
  crypto::hash node_id;
  uint64_t     amount;
  uint64_t     block_height;
  uint64_t     timestamp;
  float        node_score;
  float        total_score;
};

struct PendingCoinbaseOutput {
  std::string node_id_str;
  std::string wallet_address;
  uint64_t    amount;
  float       score;
};

struct DistributionEvent {
  uint64_t block_height;
  uint64_t total_amount;
  uint32_t node_count;
  uint64_t timestamp;
};

class RewardDistributor {
public:
  explicit RewardDistributor(const std::string& db_path);
  ~RewardDistributor();

  bool distribute_rewards(std::shared_ptr<MevaTrustEngine> engine, uint64_t height = 0);
  bool schedule_welcome_bonus(const crypto::hash& nid, uint64_t maturity_height, uint64_t amount);
  // Restituisce i bonus di benvenuto maturati a questa altezza, senza consumarli.
  //
  // E' il fratello non distruttivo di process_welcome_bonones(). La differenza
  // non e' un dettaglio: e' tutta la funzione.
  std::vector<PendingCoinbaseOutput> peek_welcome_bonuses(uint64_t current_height) const;

  // Paga i bonus maturati e CANCELLA le voci dal database: e' un'azione, va
  // chiamata una volta sola per ogni bonus e solo dal percorso che mette davvero
  // i soldi in una coinbase. Un validatore non deve mai chiamarla: si tenerebbe
  // il bonus e gli risponderebbe che il bonus non esiste piu'.
  std::vector<PendingCoinbaseOutput> process_welcome_bonuses(uint64_t current_height);
  std::vector<PendingCoinbaseOutput> get_pending_coinbase_outputs();
  bool has_pending_outputs() const;
  std::vector<RewardRecord> get_reward_history(const crypto::hash& node_id, uint64_t limit = 100);
  std::vector<DistributionEvent> get_distribution_history(uint64_t limit = 50) const;
  uint64_t get_pool_balance();
  void set_pool_balance(uint64_t b) { std::lock_guard<std::mutex> lk(m_pool_lock); m_pool_balance = b; }
  uint64_t get_total_distributed() const;
  uint64_t get_last_distribution_height() const;
  bool     is_distribution_due(uint64_t current_height) const;
  void set_pool_fraction(uint32_t p)    { m_pool_fraction = p; }
  void set_min_score(float s)           { m_min_score_for_reward = s; }
  // Lettura della soglia: serve al ricalcolo del consenso, che deve filtrare i
  // nodi esattamente come fa distribute_rewards(). Senza questo accessor la soglia
  // sarebbe un dettaglio privato e il validatore taglierebbe su un numero diverso.
  float min_score_for_reward() const     { return m_min_score_for_reward; }
  void set_distribution_period(uint32_t p) { m_distribution_period = p; }
  uint32_t distribution_period() const { return m_distribution_period; }
  uint32_t get_pool_fraction() const    { return m_pool_fraction; }

private:
  std::string m_db_path;
  mutable std::mutex m_pool_lock;
  uint64_t  m_pool_balance;
  uint64_t  m_total_distributed;
  uint64_t  m_last_distribution_height;
  uint32_t  m_distribution_count;
  std::vector<PendingCoinbaseOutput> m_pending_outputs;
  uint32_t  m_distribution_period;
  uint32_t  m_maturation_blocks;
  uint32_t  m_pool_fraction;
  float     m_min_score_for_reward;
  std::vector<DistributionEvent> m_distribution_events;
  bool append_reward_record(const RewardRecord&);
  bool save_pool_state() const;
  bool load_pool_state();
  void migrate_legacy_files();
  // LMDB handles
  MDB_env* m_env = nullptr;
  MDB_dbi  m_dbi_pool    = 0;
  MDB_dbi  m_dbi_rewards = 0;
  MDB_dbi  m_dbi_welcome = 0;
};

} // namespace cryptonote

