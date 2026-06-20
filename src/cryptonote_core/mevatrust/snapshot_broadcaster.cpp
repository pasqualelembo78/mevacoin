// Copyright (c) 2024, The Mevacoin Project
// snapshot_broadcaster.cpp -- C4-FIX: Quorum multi-proposer ballot (3/5)
// DEST (nome esatto): src/cryptonote_core/mevatrust/snapshot_broadcaster.cpp
//
// FLUSSO:
//   1. Nodo A: propose_snapshot() -> firma -> on_receive_vote(self) -> ballot_box_
//              -> broadcast_fn(voto P2P con 1 firma)
//   2. Nodi B,C: on_receive_vote() -> verifica firma -> aggiunge al ballot
//   3. Al 3mo voto distinto: try_finalize() -> apply_fn + broadcast_fn(snapshot quorum)

#include "snapshot_broadcaster.h"
#include "mevatrust_tx_parser.h"
#include "misc_log_ex.h"
#include "string_tools.h"
#include "crypto/crypto.h"
#include <cstring>
#include <algorithm>
#include <ctime>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.snapshot"

namespace cryptonote {

// ── Helper: hash del messaggio firmato da ogni proposer ───────────────────────
static crypto::hash award_msg_hash(const crypto::hash& nid, uint8_t bt, uint64_t h) {
  std::string m;
  m.append(reinterpret_cast<const char*>(nid.data), 32);
  m.push_back(static_cast<char>(bt));
  m.append(reinterpret_cast<const char*>(&h), 8);
  return crypto::cn_fast_hash(m.data(), m.size());
}

// ── make_ballot_key ───────────────────────────────────────────────────────────
// Chiave deterministica: hash( height||period||sort(node_id||badge_type)* )
// Garantisce che due proposer con lo stesso set di award (in ordine diverso)
// producano la stessa chiave e contribuiscano allo stesso ballot.
std::string SnapshotBroadcaster::make_ballot_key(
    const tx_extra_mevatrust_snapshot& snap) const
{
  struct AK { unsigned char nid[32]; uint8_t bt;
    bool operator<(const AK& o) const {
      int c = memcmp(nid, o.nid, 32); return c ? c < 0 : bt < o.bt; } };
  std::vector<AK> sorted;
  for (const auto& ba : snap.badge_awards) {
    AK ak{}; memcpy(ak.nid, ba.node_id.data, 32); ak.bt = ba.badge_type;
    sorted.push_back(ak);
  }
  std::sort(sorted.begin(), sorted.end());
  std::string raw;
  raw.append(reinterpret_cast<const char*>(&snap.height), 8);
  raw.append(reinterpret_cast<const char*>(&snap.period), 4);
  for (const auto& ak : sorted) {
    raw.append(reinterpret_cast<const char*>(ak.nid), 32);
    raw.push_back(char(ak.bt));
  }
  const crypto::hash h = crypto::cn_fast_hash(raw.data(), raw.size());
  return std::string(reinterpret_cast<const char*>(h.data), 32);
}

// ── verify_single_vote ────────────────────────────────────────────────────────
// Verifica che lo snapshot-voto abbia badge_awards con firme valide
// dal singolo proposer dichiarato in proposer_pubkey.
bool SnapshotBroadcaster::verify_single_vote(
    const tx_extra_mevatrust_snapshot& snap) const
{
  if (snap.badge_awards.empty()) {
    MWARNING("[Snap] Voto con badge_awards vuoto -- rifiutato"); return false; }
  const crypto::public_key& pk0 = snap.badge_awards[0].proposer_pubkey;
  for (const auto& ba : snap.badge_awards) {
    if (memcmp(&ba.proposer_pubkey, &pk0, sizeof(crypto::public_key)) != 0) {
      MWARNING("[Snap] Voto con proposer multipli -- rifiutato"); return false; }
    if (!crypto::check_signature(award_msg_hash(ba.node_id, ba.badge_type, ba.awarded_height),
                                 ba.proposer_pubkey, ba.proposer_sig)) {
      MWARNING("[Snap] Firma non valida nid=" << epee::string_tools::pod_to_hex(ba.node_id));
      return false; }
  }
  return true;
}

// ── try_finalize ──────────────────────────────────────────────────────────────
// Quorum raggiunto: applica localmente e broadcasta il risultato aggregato.
bool SnapshotBroadcaster::try_finalize(const std::string& /*key*/, BallotEntry& entry) {
  if (entry.finalized) return true;
  MINFO("[Snap] QUORUM RAGGIUNTO h=" << entry.snapshot.height
        << " proposers=" << entry.proposers.size() << "/" << QUORUM_THRESHOLD
        << " badges=" << entry.snapshot.badge_awards.size());
  bool ok = true;
  if (m_apply_fn) {
    ok = m_apply_fn(entry.snapshot);
    if (!ok) MWARNING("[Snap] apply_fn fallita h=" << entry.snapshot.height);
  } else {
    MWARNING("[Snap] apply_fn non configurata -- chiamare set_apply_func() all'init"); }
  if (m_broadcast_fn) {
    std::vector<uint8_t> extra;
    if (mevatrust::build_mevatrust_snapshot_extra(entry.snapshot, extra))
      m_broadcast_fn(extra, entry.snapshot.height);
    else MERROR("[Snap] build_mevatrust_snapshot_extra fallita");
  }
  entry.finalized = true;
  return ok;
}

// ── on_receive_vote ───────────────────────────────────────────────────────────
bool SnapshotBroadcaster::on_receive_vote(const tx_extra_mevatrust_snapshot& snap_vote) {
  if (!verify_single_vote(snap_vote)) return false;
  const crypto::public_key& vote_pk = snap_vote.badge_awards[0].proposer_pubkey;
  const std::string key = make_ballot_key(snap_vote);
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  auto& e = ballot_box_[key];
  // No double-vote dallo stesso proposer
  for (const auto& pk : e.proposers)
    if (memcmp(&pk, &vote_pk, sizeof(crypto::public_key)) == 0) return true;
  // Primo voto: inizializza il ballot
  if (e.proposers.empty()) {
    e.snapshot = snap_vote; e.snapshot.badge_awards.clear();
    e.first_seen_ts = uint64_t(std::time(nullptr)); e.finalized = false;
  }
  // Accumula le award firmate di questo proposer (dedup per node_id+badge_type)
  for (const auto& ba : snap_vote.badge_awards) {
    bool already = false;
    for (const auto& existing : e.snapshot.badge_awards) {
      if (memcmp(existing.node_id.data, ba.node_id.data, 32) == 0 &&
          existing.badge_type == ba.badge_type) {
        already = true; break;
      }
    }
    if (!already) e.snapshot.badge_awards.push_back(ba);
  }
  e.proposers.push_back(vote_pk);
  MINFO("[Snap] Voto accettato h=" << snap_vote.height
        << " proposers=" << e.proposers.size() << "/" << QUORUM_THRESHOLD);
  if (e.proposers.size() >= QUORUM_THRESHOLD && !e.finalized)
    return try_finalize(key, e);
  return true;
}

// ── propose_snapshot ──────────────────────────────────────────────────────────
bool SnapshotBroadcaster::propose_snapshot(
    uint64_t height, uint32_t period, uint32_t active_node_count,
    const std::vector<std::pair<crypto::hash, uint8_t>>& awards)
{
  if (!m_has_key) { MWARNING("[Snap] node_key non configurata h=" << height); return false; }
  if (awards.empty()) { MINFO("[Snap] Nessun badge da proporre h=" << height); return true; }
  // Costruisce lo snapshot-voto con la singola firma di questo nodo
  tx_extra_mevatrust_snapshot vote{};
  vote.height = height; vote.period = period; vote.node_count = active_node_count;
  for (const auto& [nid, bt] : awards) {
    tx_extra_mevatrust_snapshot::BadgeAward ba{};
    ba.node_id = nid; ba.badge_type = bt; ba.awarded_height = height;
    ba.proposer_pubkey = m_node_pk;
    crypto::generate_signature(award_msg_hash(nid, bt, height), m_node_pk, m_node_sk, ba.proposer_sig);
    vote.badge_awards.push_back(std::move(ba));
  }
  // 1. Conta come voto del proposer stesso
  on_receive_vote(vote);
  // 2. Broadcasta via P2P (altri nodi chiameranno on_receive_vote)
  if (m_broadcast_fn) {
    std::vector<uint8_t> extra;
    if (!mevatrust::build_mevatrust_snapshot_extra(vote, extra)) {
      MERROR("[Snap] build_snapshot_extra fallita h=" << height); return false; }
    if (!m_broadcast_fn(extra, height)) {
      MWARNING("[Snap] broadcast_fn fallita h=" << height); return false; }
  } else {
    MWARNING("[Snap] broadcast_fn non configurata h=" << height); }
  MINFO("[Snap] Proposta inviata h=" << height << " badges=" << awards.size()
        << " active_nodes=" << active_node_count);
  return true;
}

// ── broadcast_snapshot (backward-compat) ─────────────────────────────────────
bool SnapshotBroadcaster::broadcast_snapshot(
    uint64_t height, uint32_t period, uint32_t active_node_count,
    const std::vector<std::pair<crypto::hash, uint8_t>>& awards)
{ return propose_snapshot(height, period, active_node_count, awards); }

// ── build_snapshot_extra (statica — invariata) ────────────────────────────────
bool SnapshotBroadcaster::build_snapshot_extra(
    uint64_t height, uint32_t period, uint32_t active_node_count,
    const std::vector<std::pair<crypto::hash, uint8_t>>& awards,
    const crypto::secret_key& sk, const crypto::public_key& pk,
    std::vector<uint8_t>& extra_out)
{
  tx_extra_mevatrust_snapshot snap{};
  snap.height = height; snap.period = period; snap.node_count = active_node_count;
  for (const auto& [nid, bt] : awards) {
    tx_extra_mevatrust_snapshot::BadgeAward ba{};
    ba.node_id = nid; ba.badge_type = bt; ba.awarded_height = height;
    ba.proposer_pubkey = pk;
    crypto::generate_signature(award_msg_hash(nid, bt, height), pk, sk, ba.proposer_sig);
    snap.badge_awards.push_back(std::move(ba));
  }
  return mevatrust::build_mevatrust_snapshot_extra(snap, extra_out);
}

// ── prune_expired_ballots ─────────────────────────────────────────────────────
void SnapshotBroadcaster::prune_expired_ballots() {
  const uint64_t now = uint64_t(std::time(nullptr));
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  uint32_t pruned = 0;
  for (auto it = ballot_box_.begin(); it != ballot_box_.end(); ) {
    if (it->second.finalized || (now - it->second.first_seen_ts) > BALLOT_TTL_SECONDS)
      { it = ballot_box_.erase(it); ++pruned; } else ++it;
  }
  if (pruned) MINFO("[Snap] Pruned " << pruned << " ballot scaduti/finalizzati");
}

// ── pending_ballot_count ──────────────────────────────────────────────────────
size_t SnapshotBroadcaster::pending_ballot_count() const {
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  size_t n = 0;
  for (const auto& kv : ballot_box_) if (!kv.second.finalized) ++n;
  return n;
}

} // namespace cryptonote


