// Copyright (c) 2024, The Mevacoin Project
// bft_consensus.h — BFT-style block commit protocol (Tendermint-like)
// Manages round/phase transitions, vote accumulation, and proposer election.
// Activated at HF_VERSION_MEVATRUST_CONSENSUS (v14).

#pragma once

#include <map>
#include <set>
#include <mutex>
#include <vector>
#include <cstdint>
#include <memory>
#include <optional>
#include <chrono>
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_config.h"
#include "cryptonote_protocol/cryptonote_protocol_defs.h"

namespace cryptonote {

class MevaTrustManager;

// ─────────────────────────────────────────────────────────────────────────────
// BftConsensus — BFT state machine for block production and finalization
//
// Protocol (per height):
//   1. Proposer for round 0 is selected from eligible validators (score-weighted)
//   2. Proposer builds block and broadcasts NOTIFY_MEVATRUST_PROPOSE_BLOCK
//   3. Validators receive proposal → broadcast NOTIFY_MEVATRUST_VOTE (PREVOTE)
//   4. When ≥2/3 prevotes collected for the block → broadcast NOTIFY_MEVATRUST_VOTE (PRECOMMIT)
//   5. When ≥2/3 precommits collected → execute block, move to next height
//   6. If timeout without ≥2/3 → advance round, new proposer, repeat from step 1
// ─────────────────────────────────────────────────────────────────────────────
class BftConsensus {
public:
    enum Phase : uint8_t {
        PHASE_NEW_HEIGHT = 0,
        PHASE_PROPOSE,
        PHASE_PREVOTE,
        PHASE_PRECOMMIT,
        PHASE_COMMITTED,
    };

    // State for a specific (height, round)
    struct RoundState {
        uint64_t height{0};
        uint64_t round{0};
        Phase phase{PHASE_NEW_HEIGHT};
        crypto::hash proposed_block_hash{};  // Hash of the proposed block
        std::vector<uint8_t> proposed_block_blob;
        crypto::public_key proposer_pubkey;
        crypto::signature  proposer_sig;

                struct HashCompare {
            bool operator()(const crypto::hash& a, const crypto::hash& b) const {
                return memcmp(a.data, b.data, sizeof(crypto::hash)) < 0;
            }
        };
        std::map<crypto::hash, std::set<crypto::public_key>, HashCompare> prevotes;
        std::map<crypto::hash, std::set<crypto::public_key>, HashCompare> precommits;
    };

    BftConsensus(MevaTrustManager* manager);

    // ── Core state machine ──────────────────────────────────────────────────

    /// Called when a new block proposal arrives via P2P
    /// Returns: 0=accepted, 1=rejected (invalid), 2=ignored (old height/round)
    int on_propose(
        uint64_t height,
        uint64_t round,
        const crypto::public_key& proposer_pk,
        const std::vector<uint8_t>& block_blob,
        const crypto::signature& proposer_sig);

    /// Called when a vote arrives via P2P
    /// Returns: 0=accepted, 1=rejected, 2=ignored
    int on_vote(
        uint64_t height,
        uint64_t round,
        const crypto::public_key& voter_pk,
        uint8_t vote_type,        // mevatrust_vote_type
        const crypto::hash& block_hash,
        const crypto::signature& vote_sig);

    /// Called when a view-change message arrives
    /// Returns: 0=accepted, 1=rejected, 2=ignored
    int on_new_view(
        uint64_t height,
        uint64_t new_round,
        const crypto::public_key& proposer_pk,
        const std::vector<uint8_t>& justification);

    // ── Queries ─────────────────────────────────────────────────────────────

    /// Check if a block has been committed at a given height
    bool is_height_committed(uint64_t height) const;

    /// Check if a proposal is pending for (height, round) (proposed but not committed)
    bool has_pending_proposal(uint64_t height, uint64_t round) const;

    /// Get committed block hash for a height (empty hash if not committed)
    crypto::hash get_committed_block_hash(uint64_t height) const;

    /// Get current consensus state
    uint64_t current_height() const { return m_current_height; }
    uint64_t current_round() const { return m_current_round; }
    Phase current_phase() const { return m_current_phase; }

    /// Get the proposer for a given (height, round)
    crypto::public_key get_proposer_for(uint64_t height, uint64_t round) const;

    /// Whether we are the proposer for the current (height, round)
    bool is_current_proposer() const;

    // ── Configuration ───────────────────────────────────────────────────────

    void set_node_key(const crypto::secret_key& sk, const crypto::public_key& pk) {
        m_node_sk = sk; m_node_pk = pk; m_node_key_set = true;
    }

    void set_broadcast_propose_func(std::function<void(const NOTIFY_MEVATRUST_PROPOSE_BLOCK::request&)> fn) {
        m_broadcast_propose = std::move(fn);
    }
    void set_broadcast_vote_func(std::function<void(const NOTIFY_MEVATRUST_VOTE::request&)> fn) {
        m_broadcast_vote = std::move(fn);
    }
    void set_broadcast_new_view_func(std::function<void(const NOTIFY_MEVATRUST_NEW_VIEW::request&)> fn) {
        m_broadcast_new_view = std::move(fn);
    }

    /// Callback when a block is committed: (height, block_blob)
    void set_commit_callback(std::function<void(uint64_t, const std::vector<uint8_t>&)> fn) {
        m_commit_callback = std::move(fn);
    }

    /// Initialize consensus state from chain tip
    void init_from_chain(uint64_t chain_height, const crypto::hash& chain_tip_hash);

    /// Try to build and broadcast a proposal if we are the designated proposer
    /// for the current (height, round). Returns true if proposal was broadcast.
    bool try_propose();

    /// Check if the current round has timed out and advance if so.
    /// Returns true if a view-change was triggered.
    bool check_timeout();

    /// Set callback that builds a block proposal for the given (height, round).
    /// Returns the request to broadcast, or empty optional if building failed.
    void set_block_proposer_func(std::function<std::optional<NOTIFY_MEVATRUST_PROPOSE_BLOCK::request>(uint64_t, uint64_t)> fn) {
        m_block_proposer = std::move(fn);
    }

    /// Round timeout in milliseconds (30s). After this, if no quorum, advance round.
    static constexpr uint64_t BFT_ROUND_TIMEOUT_MS = 30000;

private:
    // ── Internal helpers ────────────────────────────────────────────────────

    /// Check if voter is an active validator
    bool is_validator(const crypto::public_key& pk) const;

    /// Get current validator count
    size_t validator_count() const;

    /// Check if a set of votes meets ≥2/3 quorum
    bool has_quorum(const std::set<crypto::public_key>& votes) const;

    /// Ensure RoundState exists for (height, round)
    RoundState& get_or_create_round(uint64_t height, uint64_t round);

    /// Transition phase and trigger actions
    void advance_phase(RoundState& rs);

    /// Check timeout while already holding m_lock. Returns true if view-change triggered.
    bool check_timeout_locked();

    // ── State ───────────────────────────────────────────────────────────────

    MevaTrustManager* m_manager;

    uint64_t m_current_height{1};
    uint64_t m_current_round{0};
    Phase m_current_phase{PHASE_NEW_HEIGHT};
    crypto::hash m_prev_id{};

    // All round states (keep last ~100 for safety during reorgs)
    std::map<std::pair<uint64_t, uint64_t>, RoundState> m_rounds;

    // Committed blocks: height → block_hash
    std::map<uint64_t, crypto::hash> m_committed;

    // Timestamp of last phase change (for timeout detection)
    std::chrono::steady_clock::time_point m_last_phase_change;

    // Node identity
    crypto::secret_key  m_node_sk{};
    crypto::public_key  m_node_pk{};
    bool                m_node_key_set{false};

    // Broadcast callbacks (set by manager)
    std::function<void(const NOTIFY_MEVATRUST_PROPOSE_BLOCK::request&)> m_broadcast_propose;
    std::function<void(const NOTIFY_MEVATRUST_VOTE::request&)> m_broadcast_vote;
    std::function<void(const NOTIFY_MEVATRUST_NEW_VIEW::request&)> m_broadcast_new_view;

    // Commit callback
    std::function<void(uint64_t, const std::vector<uint8_t>&)> m_commit_callback;

    // Block proposer callback: builds a block proposal for (height, round)
    std::function<std::optional<NOTIFY_MEVATRUST_PROPOSE_BLOCK::request>(uint64_t, uint64_t)> m_block_proposer;

    mutable std::mutex m_lock;
};

} // namespace cryptonote
