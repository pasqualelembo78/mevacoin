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

extern "C" {
#include "crypto/crypto-ops.h"
}

namespace cryptonote {

// ── Helpers ─────────────────────────────────────────────────────────────
static crypto::hash calc_msg_hash(uint64_t height, uint32_t period,
    const std::vector<std::pair<account_public_address, uint64_t>>& outputs)
{
  return mevatrust::frost::create_distribution_message_hash(height, period, outputs);
}

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

// ── propose_distribution ────────────────────────────────────────────────
// Called by the coordinator proposer at period boundary.
bool FrostBroadcaster::propose_distribution(uint64_t height, uint32_t period,
    const std::vector<std::pair<account_public_address, uint64_t>>& outputs)
{
  if (!m_has_key) { MWARNING("[FROST:P2P] No proposer key set"); return false; }
  
  mevatrust::frost::NoncePair nonces;
  if (!mevatrust::frost::generate_nonces(nonces)) { MERROR("[FROST:P2P] generate_nonces failed"); return false; }
  
  // Compute R_commit = hiding_nonce * G
  ge_p3 R_point;
  unsigned char r_bytes[32];
  memcpy(r_bytes, &nonces.hiding, 32);
  ge_scalarmult_base(&R_point, r_bytes);
  crypto::public_key R_commit;
  ge_p3_tobytes(reinterpret_cast<unsigned char*>(&R_commit), &R_point);

  std::lock_guard<std::mutex> lk(ballot_mutex_);
  Ballot* b = get_or_create_ballot(height, period);
  b->outputs = outputs;
  b->coordinator_index = m_node_index;
  b->my_R = nonces.hiding;
  b->my_hiding_scalar = nonces.hiding;
  b->my_binding_scalar = nonces.binding;
  
  // Store own nonce as received
  NonceEntry ne;
  ne.R_hiding = R_commit;
  b->nonces[m_node_index] = ne;

  MINFO("[FROST:P2P] Coordinator proposing h=" << height
        << " period=" << period << " index=" << (int)m_node_index);

  // Broadcast nonce commit via P2P
  if (m_broadcast_fn) {
    std::vector<uint8_t> blob;
    blob.reserve(46);
    blob.push_back(0); // msg_type = nonce_commit
    blob.insert(blob.end(), reinterpret_cast<const char*>(&height),
                reinterpret_cast<const char*>(&height) + 8);
    blob.insert(blob.end(), reinterpret_cast<const char*>(&period),
                reinterpret_cast<const char*>(&period) + 4);
    blob.push_back(m_node_index);
    blob.insert(blob.end(), reinterpret_cast<const char*>(&R_commit),
                reinterpret_cast<const char*>(&R_commit) + 32);
    m_broadcast_fn(blob);
  }
  return true;
}

// ── on_receive_nonce ────────────────────────────────────────────────────
// Called by protocol handler when a NONCE message arrives.
// If we're a signer, generates our own nonce and sets reply_R_out.
bool FrostBroadcaster::on_receive_nonce(uint64_t height, uint32_t period,
    uint8_t proposer_index, const crypto::public_key& R_commit,
    crypto::public_key& reply_R_out)
{
  if (!m_has_key) return false;

  std::lock_guard<std::mutex> lk(ballot_mutex_);
  Ballot* b = get_or_create_ballot(height, period);
  
  // Store the received nonce
  NonceEntry ne;
  ne.R_hiding = R_commit;
  b->nonces[proposer_index] = ne;

  // If we haven't contributed yet, generate our nonce
  if (b->nonces.find(m_node_index) == b->nonces.end()) {
    mevatrust::frost::NoncePair my_nonces;
    if (!mevatrust::frost::generate_nonces(my_nonces)) { MERROR("[FROST:P2P] generate_nonces failed"); return false; }
    
    ge_p3 R_point;
    unsigned char r_bytes[32];
    memcpy(r_bytes, &my_nonces.hiding, 32);
    ge_scalarmult_base(&R_point, r_bytes);
    ge_p3_tobytes(reinterpret_cast<unsigned char*>(&reply_R_out), &R_point);
    
    NonceEntry my_ne;
    my_ne.R_hiding = reply_R_out;
    b->nonces[m_node_index] = my_ne;
    b->my_hiding_scalar = my_nonces.hiding;
    b->my_binding_scalar = my_nonces.binding;

    MINFO("[FROST:P2P] Signer replying to h=" << height
          << " proposer=" << (int)proposer_index
          << " our_index=" << (int)m_node_index);
    return true;  // caller will unicast reply_R_out back
  }

  return false;
}

// ── request_signatures ──────────────────────────────────────────────────
// Called when coordinator has enough nonces (>= FROST_THRESHOLD).
// Computes aggregate R = sum(R_i) and broadcasts sign request.
bool FrostBroadcaster::request_signatures(uint64_t height, uint32_t period)
{
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  Ballot* b = get_or_create_ballot(height, period);
  if (b->round1_done) return true;
  if (b->nonces.size() < FROST_THRESHOLD) {
    MINFO("[FROST:P2P] Not enough nonces yet: " << b->nonces.size() << "/" << FROST_THRESHOLD);
    return false;
  }

  // Compute aggregate R = sum(R_i) for all received nonces
  bool first = true;
  ge_p3 R_sum;
  for (const auto& [idx, ne] : b->nonces) {
    ge_p3 R_i;
    if (ge_frombytes_vartime(&R_i, reinterpret_cast<const unsigned char*>(&ne.R_hiding)) != 0)
      continue;
    if (first) {
      R_sum = R_i;
      first = false;
    } else {
      ge_cached cached;
      ge_p3_to_cached(&cached, &R_i);
      ge_p1p1 p1;
      ge_add(&p1, &R_sum, &cached);
      ge_p1p1_to_p3(&R_sum, &p1);
    }
  }

  crypto::public_key R_pubkey;
  ge_p3_tobytes(reinterpret_cast<unsigned char*>(&R_pubkey), &R_sum);
  memcpy(&b->agg_R, &R_pubkey, 32);
  b->has_agg_R = true;
  b->round1_done = true;

  MINFO("[FROST:P2P] Aggregate R computed h=" << height
        << " nonces=" << b->nonces.size());

  // Broadcast sign request via P2P
  if (m_broadcast_fn) {
    std::vector<uint8_t> blob;
    blob.reserve(46);
    blob.push_back(1); // msg_type = sign_request
    blob.insert(blob.end(), reinterpret_cast<const char*>(&height),
                reinterpret_cast<const char*>(&height) + 8);
    blob.insert(blob.end(), reinterpret_cast<const char*>(&period),
                reinterpret_cast<const char*>(&period) + 4);
    blob.push_back(m_node_index);
    blob.insert(blob.end(), reinterpret_cast<const char*>(R_pubkey.data),
                reinterpret_cast<const char*>(R_pubkey.data) + 32);
    m_broadcast_fn(blob);
  }
  return true;
}

// ── on_receive_partial ──────────────────────────────────────────────────
// Called when a partial signature message arrives.
// Aggregate verification happens in try_finalize; individual partial verification
// would require an exported per-signer verify function from frost_threshold.
bool FrostBroadcaster::on_receive_partial(uint64_t height, uint32_t period,
    uint8_t proposer_index, const crypto::public_key& R_hiding,
    const crypto::ec_scalar& partial_sig)
{
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  Ballot* b = get_or_create_ballot(height, period);

  if (b->partials.find(proposer_index) != b->partials.end())
    return true;  // already received

  // Verify we have a nonce from this signer
  auto it = b->nonces.find(proposer_index);
  if (it == b->nonces.end()) return false;

  b->partials[proposer_index] = partial_sig;
  MINFO("[FROST:P2P] Partial sig received from proposer " << (int)proposer_index
        << " total=" << b->partials.size() << "/" << FROST_THRESHOLD);
  return true;
}

// ── get_own_R ─────────────────────────────────────────────────────────────
// Returns this node's R_hiding for the given ballot.
bool FrostBroadcaster::get_own_R(uint64_t height, uint32_t period,
    crypto::public_key& R_out) const
{
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  auto it = ballot_box_.find(make_ballot_key(height, period));
  if (it == ballot_box_.end()) return false;
  auto nit = it->second.nonces.find(m_node_index);
  if (nit == it->second.nonces.end()) return false;
  R_out = nit->second.R_hiding;
  return true;
}

// ── on_receive_sign_request ─────────────────────────────────────────────
// Called by protocol handler when sign request (round 2) with agg_R arrives.
bool FrostBroadcaster::on_receive_sign_request(uint64_t height, uint32_t period,
    const crypto::ec_scalar& agg_R, crypto::ec_scalar& my_sig_out)
{
  if (!m_has_key) return false;
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  Ballot* b = get_or_create_ballot(height, period);
  if (b->partials.find(m_node_index) != b->partials.end())
    return false;  // already signed

  b->has_agg_R = true;
  memcpy(&b->agg_R, &agg_R, 32);

  // Compute message hash from stored outputs
  crypto::hash msg_hash = calc_msg_hash(height, period, b->outputs);

  // Build public key package
  cryptonote::mevatrust::frost::PublicKeyPackage pkg;
  auto pubkeys = cryptonote::mevatrust::frost::CONSENSUS_PROPOSER_PUBKEYS;
  for (size_t i = 0; i < cryptonote::mevatrust::frost::FROST_N; ++i)
    pkg.signer_pubkeys[i] = pubkeys[i];
  pkg.agg_pubkey = cryptonote::mevatrust::frost::sum_public_keys(
      pubkeys.data(), cryptonote::mevatrust::frost::FROST_N);

  // Compute Lagrange coefficient for our index
  std::vector<uint8_t> all_indices;
  for (uint8_t i = 0; i < cryptonote::mevatrust::frost::FROST_N; ++i)
    all_indices.push_back(i);
  cryptonote::mevatrust::frost::compute_lagrange_coeffs(
      all_indices, pkg.lagrange_coeffs);

  // Build nonce pair
  cryptonote::mevatrust::frost::NoncePair nonce;
  nonce.hiding = b->my_hiding_scalar;
  nonce.binding = b->my_binding_scalar;

  // Sign
  cryptonote::mevatrust::frost::PartialSignature ps;
  ps.signer_index = m_node_index;
  cryptonote::mevatrust::frost::sign_partial(
      msg_hash, nonce, m_node_sk,
      pkg.lagrange_coeffs[m_node_index],
      agg_R, pkg.agg_pubkey, ps);

  my_sig_out = ps.sig_share;
  b->partials[m_node_index] = ps.sig_share;

  MINFO("[FROST:P2P] Partial sig created idx=" << (int)m_node_index
        << " h=" << height);
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
  memcpy(&agg_R_out, &it->second.agg_R, 32);
  return true;
}

// ── try_finalize ────────────────────────────────────────────────────────
bool FrostBroadcaster::try_finalize(uint64_t height, uint32_t period)
{
  std::lock_guard<std::mutex> lk(ballot_mutex_);
  Ballot* b = get_or_create_ballot(height, period);
  if (b->finalized) return true;
  if (b->partials.size() < FROST_THRESHOLD) return false;
  if (!b->has_agg_R) return false;

  // Build the final FROST signature
  // Compute message hash
  crypto::hash msg_hash = calc_msg_hash(height, period, b->outputs);

  // Aggregate R
  mevatrust::frost::FrostSignature sig;
  memcpy(&sig.R, &b->agg_R, 32);
  
  // Aggregate z = sum(s_i)
  unsigned char z[32] = {};
  memset(z, 0, 32);
  for (const auto& [idx, s] : b->partials) {
    unsigned char tmp[32];
    unsigned char s_bytes[32];
    memcpy(s_bytes, &s, 32);
    sc_add(tmp, z, s_bytes);
    memcpy(z, tmp, 32);
  }
  memcpy(&sig.z, z, 32);
  sig.msg_hash = msg_hash;

  // Verify the aggregate signature before accepting
  auto pubkeys = cryptonote::mevatrust::frost::CONSENSUS_PROPOSER_PUBKEYS;
  crypto::public_key agg_pubkey = cryptonote::mevatrust::frost::sum_public_keys(pubkeys.data(), cryptonote::mevatrust::frost::FROST_N);
  
  cryptonote::mevatrust::frost::PublicKeyPackage pkg;
  for (size_t i = 0; i < cryptonote::mevatrust::frost::FROST_N; ++i)
    pkg.signer_pubkeys[i] = pubkeys[i];
  pkg.agg_pubkey = agg_pubkey;

  if (!cryptonote::mevatrust::frost::verify_signature(sig, pkg)) {
    MERROR("[FROST:P2P] Aggregate signature VERIFICATION FAILED");
    return false;
  }

  b->finalized = true;
  MINFO("[FROST:P2P] Final signature ready h=" << height
        << " partials=" << b->partials.size());

  if (m_apply_fn)
    m_apply_fn(sig);

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
