#include "frost_broadcaster.h"
#include "misc_log_ex.h"
#include "string_tools.h"
#include "crypto/crypto.h"
#include "cryptonote_basic/cryptonote_basic_impl.h"
#include <cstring>
#include <algorithm>
#include <ctime>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.frost.p2p"

namespace cryptonote {

// ── Serialization helpers ────────────────────────────────────────────────
// outputs blob:  u8 count || (spend_key(32) || amount LE64)*
static bool serialize_outputs(
    const std::vector<std::pair<crypto::public_key, uint64_t>>& outputs,
    std::string& out)
{
  out.clear();
  if (outputs.size() > 255) return false;
  out.push_back(static_cast<char>(outputs.size()));
  for (const auto& [pk, amt] : outputs) {
    out.append(reinterpret_cast<const char*>(&pk), 32);
    out.append(reinterpret_cast<const char*>(&amt), 8);
  }
  return true;
}

static bool deserialize_outputs(
    const std::string& in,
    std::vector<std::pair<crypto::public_key, uint64_t>>& outputs)
{
  outputs.clear();
  if (in.size() < 1) return false;
  const size_t n = static_cast<uint8_t>(in[0]);
  const size_t need = 1 + n * 40;
  if (in.size() != need) return false;
  for (size_t i = 0; i < n; ++i) {
    crypto::public_key pk;
    uint64_t amt;
    memcpy(&pk, in.data() + 1 + i * 40, 32);
    memcpy(&amt, in.data() + 1 + i * 40 + 32, 8);
    outputs.emplace_back(pk, amt);
  }
  return true;
}

// indices blob:  u8 count || u8 index*  (each in 1..N, ascending)
static bool serialize_indices(const std::vector<uint8_t>& indices, std::string& out)
{
  out.clear();
  if (indices.size() > 255) return false;
  out.push_back(static_cast<char>(indices.size()));
  for (uint8_t i : indices) out.push_back(static_cast<char>(i));
  return true;
}

static bool deserialize_indices(const std::string& in, std::vector<uint8_t>& indices)
{
  indices.clear();
  if (in.size() < 1) return false;
  const size_t n = static_cast<uint8_t>(in[0]);
  if (in.size() != 1 + n) return false;
  for (size_t i = 0; i < n; ++i)
    indices.push_back(static_cast<uint8_t>(in[1 + i]));
  return true;
}

// ── Ballot helpers ─────────────────────────────────────────────────────
std::string FrostBroadcaster::make_ballot_key(uint64_t height, uint32_t period) const
{
  std::string raw;
  raw.append(reinterpret_cast<const char*>(&height), 8);
  raw.append(reinterpret_cast<const char*>(&period), 4);
  crypto::hash h = crypto::cn_fast_hash(raw.data(), raw.size());
  return std::string(reinterpret_cast<const char*>(h.data), 32);
}

FrostBroadcaster::Ballot* FrostBroadcaster::get_or_create_ballot(
    uint64_t height, uint32_t period)
{
  std::string key = make_ballot_key(height, period);
  auto& e = ballot_box_[key];
  if (e.height == 0) {
    e.height = height;
    e.period = period;
    e.first_seen_ts = uint64_t(std::time(nullptr));
  }
  return &e;
}

// ── propose_distribution (coordinator, round 1) ──────────────────────────
bool FrostBroadcaster::propose_distribution(uint64_t height, uint32_t period,
    const std::vector<std::pair<crypto::public_key, uint64_t>>& outputs)
{
  if (!m_has_key) { MWARNING("[FROST:P2P] No proposer key set"); return false; }

  mevatrust::frost::NoncePair nonces;
  if (!mevatrust::frost::generate_nonces(nonces)) { MERROR("[FROST:P2P] generate_nonces failed"); return false; }
  crypto::public_key D_commit, E_commit;
  mevatrust::frost::nonce_commitments(nonces, D_commit, E_commit);

  std::string outputs_blob;
  if (!serialize_outputs(outputs, outputs_blob)) { MERROR("[FROST:P2P] serialize_outputs failed"); return false; }

  {
    std::lock_guard<std::mutex> lk(ballot_mutex_);
    Ballot* b = get_or_create_ballot(height, period);
    if (b->round1_done) return true;
    b->outputs = outputs;
    b->outputs_blob = outputs_blob;
    b->coordinator_index = m_node_index;
    b->my_nonces = nonces;
    b->my_D = D_commit;
    b->my_E = E_commit;
    b->nonces[m_node_index] = NonceEntry{D_commit, E_commit};
    MINFO("[FROST:P2P] Coordinator proposing h=" << height
          << " period=" << period << " index=" << (int)m_node_index
          << " outputs=" << outputs.size());
  }

  // Broadcast nonce commit (msg_type=0, blob: type(1) height(8) period(4) idx(1) D(32) E(32))
  if (m_broadcast_fn) {
    std::vector<uint8_t> blob;
    blob.reserve(78);
    blob.push_back(0);
    blob.insert(blob.end(), reinterpret_cast<const char*>(&height),
                reinterpret_cast<const char*>(&height) + 8);
    blob.insert(blob.end(), reinterpret_cast<const char*>(&period),
                reinterpret_cast<const char*>(&period) + 4);
    blob.push_back(m_node_index);
    blob.insert(blob.end(), reinterpret_cast<const char*>(&D_commit),
                reinterpret_cast<const char*>(&D_commit) + 32);
    blob.insert(blob.end(), reinterpret_cast<const char*>(&E_commit),
                reinterpret_cast<const char*>(&E_commit) + 32);
    m_broadcast_fn(blob);
  }
  return true;
}

// ── on_receive_nonce (signer, round 1) ───────────────────────────────────
bool FrostBroadcaster::on_receive_nonce(uint64_t height, uint32_t period,
    uint8_t proposer_index, const crypto::public_key& D_commit,
    const crypto::public_key& E_commit,
    crypto::public_key& reply_D, crypto::public_key& reply_E)
{
  if (!m_has_key) return false;
  if (proposer_index < 1 || proposer_index > mevatrust::frost::FROST_N) return false;

  std::lock_guard<std::mutex> lk(ballot_mutex_);
  Ballot* b = get_or_create_ballot(height, period);

  // Store the received commitment
  b->nonces[proposer_index] = NonceEntry{D_commit, E_commit};

  // If we haven't contributed yet, generate our own nonce and reply
  if (b->nonces.find(m_node_index) == b->nonces.end()) {
    mevatrust::frost::NoncePair my_nonces;
    if (!mevatrust::frost::generate_nonces(my_nonces)) { MERROR("[FROST:P2P] generate_nonces failed"); return false; }
    crypto::public_key my_D, my_E;
    mevatrust::frost::nonce_commitments(my_nonces, my_D, my_E);
    b->my_nonces = my_nonces;
    b->my_D = my_D;
    b->my_E = my_E;
    b->nonces[m_node_index] = NonceEntry{my_D, my_E};
    b->coordinator_index = proposer_index;
    reply_D = my_D;
    reply_E = my_E;

    MINFO("[FROST:P2P] Signer replying to h=" << height
          << " coordinator=" << (int)proposer_index
          << " our_index=" << (int)m_node_index);
    return true;  // caller will unicast reply back
  }
  return false;
}

// ── request_signatures (coordinator, round 2 broadcast) ──────────────────
bool FrostBroadcaster::request_signatures(uint64_t height, uint32_t period)
{
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  Ballot* b = get_or_create_ballot(height, period);
  if (b->round1_done) return true;
  if (b->nonces.size() < FROST_THRESHOLD) {
    MINFO("[FROST:P2P] Not enough nonces yet: " << b->nonces.size() << "/" << FROST_THRESHOLD);
    return false;
  }

  // Participant set: of the indices that committed, keep those in 1..N (already validated)
  std::vector<uint8_t> participants;
  for (const auto& [idx, ne] : b->nonces) participants.push_back(idx);
  std::sort(participants.begin(), participants.end());

  // Message hash from our stored outputs
  crypto::hash msg_hash = mevatrust::frost::create_distribution_message_hash(
      height, period, b->outputs);
  if (msg_hash == crypto::null_hash) { MERROR("[FROST:P2P] null msg hash"); return false; }

  // Binding factors (sorted by index internally)
  std::vector<std::pair<crypto::public_key, crypto::public_key>> comms;
  for (uint8_t idx : participants) {
    auto it = b->nonces.find(idx);
    if (it == b->nonces.end()) return false;
    comms.emplace_back(it->second.D, it->second.E);
  }
  std::vector<crypto::ec_scalar> rho;
  if (!mevatrust::frost::compute_binding_factors(participants, msg_hash, comms, rho))
    return false;

  // Aggregate R = sum(D_i + rho_i*E_i)
  crypto::public_key agg_R;
  if (!mevatrust::frost::compute_aggregate_r(comms, rho, agg_R)) return false;

  b->agg_R = agg_R;
  b->has_agg_R = true;
  b->round1_done = true;
  b->participants = participants;
  b->rho = rho;

  MINFO("[FROST:P2P] Aggregate R computed h=" << height
        << " participants=" << participants.size());

  // Broadcast sign request (msg_type=2):
  //   type(1) height(8) period(4) idx(1) agg_R(32) outputs_blob indices_blob
  if (m_broadcast_fn) {
    std::string indices_blob;
    if (!serialize_indices(participants, indices_blob)) return false;
    std::vector<uint8_t> blob;
    blob.reserve(46 + b->outputs_blob.size() + indices_blob.size());
    blob.push_back(2);
    blob.insert(blob.end(), reinterpret_cast<const char*>(&height),
                reinterpret_cast<const char*>(&height) + 8);
    blob.insert(blob.end(), reinterpret_cast<const char*>(&period),
                reinterpret_cast<const char*>(&period) + 4);
    blob.push_back(m_node_index);
    blob.insert(blob.end(), reinterpret_cast<const char*>(&agg_R),
                reinterpret_cast<const char*>(&agg_R) + 32);
    blob.insert(blob.end(), b->outputs_blob.begin(), b->outputs_blob.end());
    blob.insert(blob.end(), indices_blob.begin(), indices_blob.end());
    m_broadcast_fn(blob);
  }
  return true;
}

// ── on_receive_sign_request (signer, round 2) ────────────────────────────
bool FrostBroadcaster::on_receive_sign_request(uint64_t height, uint32_t period,
    const crypto::public_key& agg_R,
    const std::string& outputs_blob,
    const std::string& signer_indices_blob,
    mevatrust::frost::PartialSignature& my_partial_out)
{
  if (!m_has_key) return false;

  std::vector<std::pair<crypto::public_key, uint64_t>> outputs;
  if (!deserialize_outputs(outputs_blob, outputs)) return false;
  std::vector<uint8_t> participants;
  if (!deserialize_indices(signer_indices_blob, participants)) return false;
  if (participants.size() < FROST_THRESHOLD) return false;

  std::lock_guard<std::mutex> lk(ballot_mutex_);
  Ballot* b = get_or_create_ballot(height, period);
  if (b->partials.find(m_node_index) != b->partials.end()) return false;  // already signed

  // Store what the coordinator decided (authoritative participant set + R)
  b->outputs = outputs;
  b->outputs_blob = outputs_blob;
  b->agg_R = agg_R;
  b->has_agg_R = true;
  b->participants = participants;

  // We must already hold commitments for every participant from round 1
  std::vector<std::pair<crypto::public_key, crypto::public_key>> comms;
  for (uint8_t idx : participants) {
    auto it = b->nonces.find(idx);
    if (it == b->nonces.end()) {
      MINFO("[FROST:P2P] Missing commitment for participant " << (int)idx
            << " — retry after round 1 completes");
      return false;
    }
    comms.emplace_back(it->second.D, it->second.E);
  }

  crypto::hash msg_hash = mevatrust::frost::create_distribution_message_hash(
      height, period, outputs);

  std::vector<crypto::ec_scalar> rho;
  if (!mevatrust::frost::compute_binding_factors(participants, msg_hash, comms, rho))
    return false;
  b->rho = rho;

  // Sanity: recompute R from this viewpoint — must equal the coordinator's
  crypto::public_key local_R;
  if (!mevatrust::frost::compute_aggregate_r(comms, rho, local_R)) return false;
  if (memcmp(&local_R, &agg_R, 32) != 0) {
    MERROR("[FROST:P2P] R mismatch between coordinator and local view");
    return false;
  }

  // Lagrange coefficient for our own index over this participant set
  std::vector<crypto::ec_scalar> lambdas;
  if (!mevatrust::frost::compute_lagrange_coeffs(participants, lambdas)) return false;
  size_t own_pos = std::find(participants.begin(), participants.end(), m_node_index)
                   - participants.begin();
  if (own_pos >= participants.size()) return false;

  // Sign
  mevatrust::frost::PartialSignature ps;
  ps.signer_index = m_node_index;
  if (!mevatrust::frost::sign_partial(
          msg_hash, b->my_nonces, m_node_sk,
          lambdas[own_pos], rho[own_pos], agg_R,
          mevatrust::frost::CONSENSUS_GROUP_PUBKEY, ps)) {
    MERROR("[FROST:P2P] sign_partial failed");
    return false;
  }

  my_partial_out = ps;
  b->partials[m_node_index] = ps;
  MINFO("[FROST:P2P] Partial sig created idx=" << (int)m_node_index
        << " h=" << height);
  return true;
}

// ── on_receive_partial (coordinator, round 2 reply) ──────────────────────
bool FrostBroadcaster::on_receive_partial(uint64_t height, uint32_t period,
    uint8_t signer_index, const mevatrust::frost::PartialSignature& partial)
{
  if (signer_index < 1 || signer_index > mevatrust::frost::FROST_N) return false;

  std::lock_guard<std::mutex> lk(ballot_mutex_);
  Ballot* b = get_or_create_ballot(height, period);
  if (b->finalized) return true;
  if (b->partials.find(signer_index) != b->partials.end()) return true;  // already received
  if (!b->has_agg_R || b->participants.empty()) return false;

  // Verify we received this signer's commitments in round 1
  auto nit = b->nonces.find(signer_index);
  if (nit == b->nonces.end()) return false;
  if (memcmp(&nit->second.D, &partial.D, 32) != 0 ||
      memcmp(&nit->second.E, &partial.E, 32) != 0) {
    MERROR("[FROST:P2P] Partial D/E mismatch with round-1 commitment");
    return false;
  }

  // Verify the partial immediately using its own stored commitments
  std::vector<crypto::ec_scalar> lambdas;
  if (!mevatrust::frost::compute_lagrange_coeffs(b->participants, lambdas)) return false;
  size_t pos = std::find(b->participants.begin(), b->participants.end(), signer_index)
               - b->participants.begin();
  if (pos >= b->participants.size()) return false;

  crypto::hash msg_hash = mevatrust::frost::create_distribution_message_hash(
      height, period, b->outputs);
  std::string err;
  if (!mevatrust::frost::verify_partial(
          msg_hash, partial, lambdas[pos], b->rho[pos], b->agg_R,
          mevatrust::frost::CONSENSUS_GROUP_PUBKEY,
          mevatrust::frost::CONSENSUS_PROPOSER_PUBKEYS[signer_index - 1], err)) {
    MERROR("[FROST:P2P] Partial from signer " << (int)signer_index
           << " INVALID: " << err);
    return false;
  }

  b->partials[signer_index] = partial;
  MINFO("[FROST:P2P] Partial sig verified from signer " << (int)signer_index
        << " total=" << b->partials.size() << "/" << FROST_THRESHOLD);
  return true;
}

// ── try_finalize ───────────────────────────────────────────────────────
bool FrostBroadcaster::try_finalize(uint64_t height, uint32_t period)
{
  mevatrust::frost::FrostSignature final_sig;
  {
    std::lock_guard<std::mutex> lk(ballot_mutex_);
    Ballot* b = get_or_create_ballot(height, period);
    if (b->finalized) return true;
    if (b->partials.size() < FROST_THRESHOLD) return false;
    if (!b->has_agg_R || b->participants.empty()) return false;

    // Order partials by participant order
    std::vector<mevatrust::frost::PartialSignature> partials;
    for (uint8_t idx : b->participants) {
      auto it = b->partials.find(idx);
      if (it == b->partials.end()) continue;
      partials.push_back(it->second);
    }
    if (partials.size() < FROST_THRESHOLD) return false;

    crypto::hash msg_hash = mevatrust::frost::create_distribution_message_hash(
        height, period, b->outputs);

    std::string err;
    if (!mevatrust::frost::aggregate_signatures(
            msg_hash, b->participants, partials, b->rho,
            mevatrust::frost::CONSENSUS_GROUP_PUBKEY, final_sig)) {
      MERROR("[FROST:P2P] aggregate_signatures failed");
      return false;
    }
    b->finalized = true;
    MINFO("[FROST:P2P] Final signature ready h=" << height
          << " partials=" << partials.size());
  }

  // Apply OUTSIDE the ballot lock (avoids AB-BA deadlock with manager locks)
  if (m_apply_fn)
    m_apply_fn(final_sig);
  return true;
}

// ── get_own_commit ───────────────────────────────────────────────────────
bool FrostBroadcaster::get_own_commit(uint64_t height, uint32_t period,
    crypto::public_key& D, crypto::public_key& E) const
{
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  auto it = ballot_box_.find(make_ballot_key(height, period));
  if (it == ballot_box_.end()) return false;
  D = it->second.my_D;
  E = it->second.my_E;
  return true;
}

// ── get_agg_R ───────────────────────────────────────────────────────────
bool FrostBroadcaster::get_agg_R(uint64_t height, uint32_t period,
    crypto::public_key& agg_R_out) const
{
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  auto it = ballot_box_.find(make_ballot_key(height, period));
  if (it == ballot_box_.end()) return false;
  if (!it->second.has_agg_R) return false;
  agg_R_out = it->second.agg_R;
  return true;
}

// ── get_sign_request_data ───────────────────────────────────────────────
bool FrostBroadcaster::get_sign_request_data(uint64_t height, uint32_t period,
    crypto::public_key& agg_R_out, std::string& outputs_data, std::string& signer_indices) const
{
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  auto it = ballot_box_.find(make_ballot_key(height, period));
  if (it == ballot_box_.end()) return false;
  const Ballot& b = it->second;
  if (!b.has_agg_R || b.round1_done == false) return false;
  agg_R_out = b.agg_R;
  outputs_data = b.outputs_blob;
  if (!serialize_indices(b.participants, signer_indices)) return false;
  return true;
}

// ── prune_expired_ballots ───────────────────────────────────────────────
void FrostBroadcaster::prune_expired_ballots()
{
  const uint64_t now = uint64_t(std::time(nullptr));
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  uint32_t pruned = 0;
  for (auto it = ballot_box_.begin(); it != ballot_box_.end();) {
    if (it->second.finalized || (now - it->second.first_seen_ts) > BALLOT_TTL_SECS)
      { it = ballot_box_.erase(it); ++pruned; } else ++it;
  }
  if (pruned) MINFO("[FROST:P2P] Pruned " << pruned << " ballots");
}

size_t FrostBroadcaster::pending_ballot_count() const
{
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  size_t n = 0;
  for (const auto& kv : ballot_box_) if (!kv.second.finalized) ++n;
  return n;
}

} // namespace cryptonote