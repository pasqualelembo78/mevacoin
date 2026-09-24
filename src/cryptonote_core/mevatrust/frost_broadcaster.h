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

/// P2P FROST coordinator for pool distribution signing (correct CFRG-FROST).
///
/// Protocol (2 rounds, coordinator = node that proposes the distribution):
///   Round 1 (NONCE exchange):
///     coordinator broadcast  NONCE(msg_type=0, D, E)
///     signers reply          NONCE(msg_type=1, own D, own E)
///   Round 2 (SIGN exchange):
///     coordinator broadcast  SIGN(msg_type=0, agg_R, outputs, participant indices)
///                            (the OUTPUTS are transmitted so every signer derives
///                             the exact same message hash as the verifier)
///     signers reply          SIGN(msg_type=1, s_i, D, E)
///
/// Per-partial verification happens immediately on receipt (uses the signer's
/// ceremony public key).  Aggregate R is recomputed from commitments — a rogue
/// partial carrying a forged R is rejected.  Final signature verifies against
/// the ceremony group key CONSENSUS_GROUP_PUBKEY via plain Schnorr.
///
/// Wire blobs for verify_partial / aggregation:
///   outputs serialization:  u8 count || (spend_key(32) || amount LE64)*
///   participant indices:    u8 count || u8 index*
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
    // Match our pubkey against the ceremony set to derive the true share index.
    // If no match (test-only keys), keep the caller-provided index.
    for (size_t i = 0; i < mevatrust::frost::FROST_N; ++i)
      if (memcmp(&pk, &mevatrust::frost::CONSENSUS_PROPOSER_PUBKEYS[i], 32) == 0) {
        m_node_index = static_cast<uint8_t>(i + 1);
        break;
      }
  }

  /// Called at period boundary by the coordinator proposer.
  /// Generates own nonces, broadcasts commit (msg_type=0), starts ballot.
  bool propose_distribution(uint64_t height, uint32_t period,
      const std::vector<std::pair<crypto::public_key, uint64_t>>& outputs);

  /// Handle incoming nonce commit / reply (round 1).  Returns true if a reply
  /// should be unicast back, and fills reply_D/reply_E with our commitments.
  bool on_receive_nonce(uint64_t height, uint32_t period,
      uint8_t proposer_index, const crypto::public_key& D_commit,
      const crypto::public_key& E_commit,
      crypto::public_key& reply_D, crypto::public_key& reply_E);

  /// Called when aggregate R is ready — broadcast sign request
  /// (msg_type=0 with agg_R + outputs + participant indices).
  bool request_signatures(uint64_t height, uint32_t period);

  /// Called when sign request (with agg_R + outputs) is received — generates
  /// partial sig.  Returns true if a reply should be unicast, fills my partial.
  bool on_receive_sign_request(uint64_t height, uint32_t period,
      const crypto::public_key& agg_R,
      const std::string& outputs_blob,      // serialized outputs (spend_key||amount)*
      const std::string& signer_indices_blob,
      mevatrust::frost::PartialSignature& my_partial_out);

  /// Handle incoming partial signature (round 2 reply).  Verifies it instantly.
  bool on_receive_partial(uint64_t height, uint32_t period,
      uint8_t signer_index, const mevatrust::frost::PartialSignature& partial);

  /// Attempt to finalize if enough valid partials collected.
  /// Calls the apply callback OUTSIDE the ballot lock (no deadlock).
  bool try_finalize(uint64_t height, uint32_t period);

  /// Retrieve aggregate R for a given ballot (for broadcasting sign request).
  bool get_agg_R(uint64_t height, uint32_t period, crypto::public_key& agg_R_out) const;

  /// Retrieve the serialized sign-request payload (outputs + participant
  /// indices) for a ballot, so the protocol layer can broadcast round 2.
  bool get_sign_request_data(uint64_t height, uint32_t period,
      crypto::public_key& agg_R_out,
      std::string& outputs_data, std::string& signer_indices) const;

  /// Accessors for protocol handler.
  uint8_t node_index() const { return m_node_index; }
  bool    has_key() const    { return m_has_key; }
  bool    get_own_commit(uint64_t height, uint32_t period,
                         crypto::public_key& D, crypto::public_key& E) const;

  /// Prune stale ballots.
  void prune_expired_ballots();

  size_t pending_ballot_count() const;

private:
  struct NonceEntry {
    crypto::public_key D;   // hiding commitment d_i*G
    crypto::public_key E;   // binding commitment e_i*G
  };

  struct Ballot {
    uint64_t height{0};
    uint32_t period{0};
    std::vector<std::pair<crypto::public_key, uint64_t>> outputs;
    std::string outputs_blob;
    std::unordered_map<uint8_t, NonceEntry> nonces;      // signer index (1..N) → commitments
    std::unordered_map<uint8_t, mevatrust::frost::PartialSignature> partials;  // 1..N → s_i
    std::vector<uint8_t> participants;                                   // round-2 authorized set (ascending)
    std::vector<crypto::ec_scalar> rho;                                  // binding factors (same order as participants)
    uint8_t coordinator_index{0};
    crypto::public_key agg_R;                              // round-2 aggregate R
    mevatrust::frost::NoncePair my_nonces{};              // our round-1 nonces
    crypto::public_key my_D{}, my_E{};                    // our round-1 commitments
    bool round1_done{false};
    bool round2_asked{false};
    bool finalized{false};
    bool has_agg_R{false};
    uint64_t first_seen_ts{0};
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