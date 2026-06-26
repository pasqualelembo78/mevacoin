#pragma once
#include <vector>
#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include "crypto/crypto.h"
#include "cryptonote_basic/tx_extra.h"
#include "frost_threshold.h"

namespace cryptonote {

/// P2P FROST coordinator for pool distribution signing.
/// Follows a 2-round threshold signature protocol:
///   Round 1: coordinator broadcasts nonce commitment → signers reply with theirs
///   Round 2: coordinator broadcasts aggregate R → signers reply with partial sig
///
/// Broadcast messages are encoded as blobs:
///   byte[0]     = msg_type (0=nonce_commit, 1=sign_request)
///   byte[1..8]  = height (LE)
///   byte[9..12] = period (LE)
///   byte[13]    = proposer_index
///   byte[14..]  = payload (32-byte R_commit or agg_R)
class FrostBroadcaster {
public:
  static constexpr uint32_t FROST_THRESHOLD  = 3;
  static constexpr uint64_t BALLOT_TTL_SECS  = 120;

  using BroadcastFunc = std::function<bool(const std::vector<uint8_t>&)>;
  using ApplyFunc = std::function<bool(const mevatrust::frost::FrostSignature&)>;

  FrostBroadcaster() = default;

  void set_broadcast_func(BroadcastFunc fn) { m_broadcast_fn = std::move(fn); }
  void set_apply_func(ApplyFunc fn)         { m_apply_fn     = std::move(fn); }
  void set_node_key(const crypto::secret_key& sk, const crypto::public_key& pk, uint8_t index) {
    m_node_sk = sk; m_node_pk = pk; m_node_index = index; m_has_key = true;
  }

  /// Called at period boundary by the coordinator proposer.
  /// Generates nonces, broadcasts commit, starts ballot.
  bool propose_distribution(uint64_t height, uint32_t period,
      const std::vector<std::pair<account_public_address, uint64_t>>& outputs);

  /// Handle incoming nonce commit (round 1). Returns optional reply to unicast back.
  bool on_receive_nonce(uint64_t height, uint32_t period,
      uint8_t proposer_index, const crypto::public_key& R_commit,
      crypto::public_key& reply_R_out);

  /// Handle incoming partial signature (round 2).
  bool on_receive_partial(uint64_t height, uint32_t period,
      uint8_t proposer_index, const crypto::public_key& R_hiding,
      const crypto::ec_scalar& partial_sig);

  /// Called when aggregate R is ready — broadcast sign request.
  bool request_signatures(uint64_t height, uint32_t period);

  /// Called when sign request (with agg_R) is received — generates partial sig.
  /// Returns true if reply should be unicast, and fills my_sig_out.
  bool on_receive_sign_request(uint64_t height, uint32_t period,
      const crypto::ec_scalar& agg_R, crypto::ec_scalar& my_sig_out);

  /// Attempt to finalize if enough partials collected.
  bool try_finalize(uint64_t height, uint32_t period);

  /// Retrieve aggregate R for a given ballot (for broadcasting sign request).
  bool get_agg_R(uint64_t height, uint32_t period, crypto::public_key& agg_R_out) const;

  /// Accessors for protocol handler.
  uint8_t node_index() const { return m_node_index; }
  bool    has_key() const    { return m_has_key; }
  bool    get_own_R(uint64_t height, uint32_t period, crypto::public_key& R_out) const;

  /// Prune stale ballots.
  void prune_expired_ballots();

  size_t pending_ballot_count() const;

private:
  struct NonceEntry {
    crypto::public_key R_hiding;
  };

  struct Ballot {
    uint64_t height{0};
    uint32_t period{0};
    std::vector<std::pair<account_public_address, uint64_t>> outputs;
    std::unordered_map<uint8_t, NonceEntry> nonces;   // proposer_index → R_commit
    std::unordered_map<uint8_t, crypto::ec_scalar> partials;  // proposer_index → s_i
    uint8_t coordinator_index{0};
    crypto::ec_scalar agg_R;  // computed after nonces collected
    crypto::ec_scalar my_R;   // this node's R_commit (if coordinator)
    crypto::ec_scalar my_hiding_scalar{};  // r_i (if signer)
    crypto::ec_scalar my_binding_scalar{};
    bool round1_done{false};
    bool round2_asked{false};
    bool finalized{false};
    uint64_t first_seen_ts{0};
    bool has_agg_R{false};
  };

  Ballot* get_or_create_ballot(uint64_t height, uint32_t period);
  std::string make_ballot_key(uint64_t height, uint32_t period) const;

  std::unordered_map<std::string, Ballot> ballot_box_;
  mutable std::mutex ballot_mutex_;

  BroadcastFunc m_broadcast_fn;
  ApplyFunc     m_apply_fn;
  crypto::secret_key m_node_sk{};
  crypto::public_key m_node_pk{};
  uint8_t m_node_index{0};
  bool m_has_key{false};
};

} // namespace cryptonote
