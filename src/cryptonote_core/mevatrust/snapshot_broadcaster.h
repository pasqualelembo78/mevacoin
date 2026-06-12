// Copyright (c) 2024, The Mevacoin Project
// snapshot_broadcaster.h -- C4-FIX: Quorum multi-proposer ballot (3/5)
// DEST (nome esatto): src/cryptonote_core/mevatrust/snapshot_broadcaster.h
//
// DESIGN QUORUM:
//   1. propose_snapshot() firma il payload e lo aggiunge al ballot_box_ locale
//   2. broadcast_snapshot() (backward-compat) chiama propose_snapshot() internamente
//   3. on_receive_vote() accumula i voti degli altri nodi nel ballot_box_
//   4. Quando >= QUORUM_THRESHOLD (3) proposer distinti concordano:
//      try_finalize() -> m_apply_fn (persistenza on-chain) -> m_broadcast_fn (P2P)
//   5. prune_expired_ballots() da chiamare ogni blocco per evitare memory leak

#pragma once
#include <vector>
#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include "crypto/crypto.h"
#include "cryptonote_basic/tx_extra.h"

namespace cryptonote {

class SnapshotBroadcaster {
public:
  // ── Configurazione quorum ─────────────────────────────────────────────────
  static constexpr uint32_t QUORUM_THRESHOLD     = 3;   // firme minime per finalizzare
  static constexpr uint32_t QUORUM_MAX_PROPOSERS = 5;   // firme massime salvate on-chain
  static constexpr uint64_t BALLOT_TTL_SECONDS   = 300; // 5 min prima di scartare

  // ── Callback types ────────────────────────────────────────────────────────
  /// P2P broadcast: invia il blob extra via NOTIFY_MEVATRUST_SNAPSHOT
  using BroadcastFunc = std::function<bool(const std::vector<uint8_t>&, uint64_t)>;

  /// Apply callback: chiamata quando il quorum e' raggiunto.
  /// Riceve la snapshot aggregata e la accoda in pending_badge_extras_
  /// per includerla nel prossimo miner_tx.extra (tag 0xA2).
  using ApplyFunc = std::function<bool(const tx_extra_mevatrust_snapshot&)>;

  SnapshotBroadcaster() = default;

  void set_broadcast_func(BroadcastFunc fn) { m_broadcast_fn = std::move(fn); }
  void set_apply_func(ApplyFunc fn)          { m_apply_fn     = std::move(fn); }
  void set_node_key(const crypto::secret_key& sk, const crypto::public_key& pk) {
    m_node_sk = sk; m_node_pk = pk; m_has_key = true;
  }

  /// Propone uno snapshot firmato. Lo aggiunge al ballot locale e lo broadcasta via P2P.
  bool propose_snapshot(
    uint64_t height, uint32_t period, uint32_t active_node_count,
    const std::vector<std::pair<crypto::hash, uint8_t>>& awards);

  /// Riceve un voto da un altro proposer (chiamata dal protocol handler).
  /// Ritorna true se il voto e' valido. Se raggiunge il quorum, finalizza.
  bool on_receive_vote(const tx_extra_mevatrust_snapshot& snap_vote);

  /// Backward-compatible: chiama propose_snapshot() internamente.
  bool broadcast_snapshot(
    uint64_t height, uint32_t period, uint32_t active_node_count,
    const std::vector<std::pair<crypto::hash, uint8_t>>& awards);

  /// Costruisce il blob 0xA2 con firma singola (usata da on_receive per verify).
  static bool build_snapshot_extra(
    uint64_t height, uint32_t period, uint32_t active_node_count,
    const std::vector<std::pair<crypto::hash, uint8_t>>& awards,
    const crypto::secret_key& sk, const crypto::public_key& pk,
    std::vector<uint8_t>& extra_out);

  /// Rimuove ballot scaduti/finalizzati. Chiamare ogni blocco.
  void prune_expired_ballots();

  /// Diagnostica: numero di ballot in attesa di quorum.
  size_t pending_ballot_count() const;

private:
  struct BallotEntry {
    tx_extra_mevatrust_snapshot snapshot;    // snapshot proposto (con award aggregati)
    std::vector<crypto::public_key> proposers;   // proposer distinti gia' votati
    uint64_t first_seen_ts{0};                   // unix timestamp per TTL
    bool     finalized{false};                   // true = gia' applicato
  };

  std::string make_ballot_key(const tx_extra_mevatrust_snapshot& snap) const;
  bool verify_single_vote(const tx_extra_mevatrust_snapshot& snap) const;
  bool try_finalize(const std::string& ballot_key, BallotEntry& entry);

  std::unordered_map<std::string, BallotEntry> ballot_box_;
  mutable std::mutex ballot_mutex_;

  BroadcastFunc      m_broadcast_fn;
  ApplyFunc          m_apply_fn;
  crypto::secret_key m_node_sk{};
  crypto::public_key m_node_pk{};
  bool               m_has_key{false};
};

} // namespace cryptonote


