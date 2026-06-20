// Copyright (c) 2024, The Mevacoin Project
// bft_consensus.cpp — BFT-style block commit protocol implementation

#include "bft_consensus.h"
#include "mevatrust_manager.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_protocol/cryptonote_protocol_defs.h"
#include "misc_log_ex.h"
#include "string_tools.h"

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.bft"

namespace cryptonote {

// ── Constructor ──────────────────────────────────────────────────────────────

BftConsensus::BftConsensus(MevaTrustManager* manager)
    : m_manager(manager)
{
}

// ── Proposer selection ──────────────────────────────────────────────────────

crypto::public_key BftConsensus::get_proposer_for(uint64_t height, uint64_t round) const
{
    if (!m_manager) return {};

    // Round-robin within eligible proposers, seeded by height for determinism
    auto eligible = m_manager->get_eligible_proposers();
    if (eligible.empty()) return {};

    // For round 0, use the score-weighted deterministic selection
    if (round == 0) {
        crypto::hash prev_id = m_prev_id;
        return m_manager->next_proposer(height, prev_id);
    }

    // For subsequent rounds, rotate among eligible proposers deterministically
    // pick = (height + round) % eligible.size()
    size_t idx = static_cast<size_t>((height + round) % eligible.size());
    return eligible[idx].node_pubkey;
}

bool BftConsensus::is_current_proposer() const
{
    if (!m_node_key_set) return false;
    auto expected = get_proposer_for(m_current_height, m_current_round);
    return expected == m_node_pk;
}

// ── Validator checks ─────────────────────────────────────────────────────────

bool BftConsensus::is_validator(const crypto::public_key& pk) const
{
    if (!m_manager) return false;

    // Check if pubkey belongs to a known active validator node
    auto eligible = m_manager->get_eligible_proposers();
    for (const auto& e : eligible) {
        if (e.node_pubkey == pk) return true;
    }
    return false;
}

size_t BftConsensus::validator_count() const
{
    if (!m_manager) return 0;
    return m_manager->get_eligible_proposers().size();
}

bool BftConsensus::has_quorum(const std::set<crypto::public_key>& votes) const
{
    size_t total = validator_count();
    if (total == 0) return false;
    // ≥2/3 of active validators
    return (votes.size() * 3) >= (total * 2);
}

// ── Round state management ──────────────────────────────────────────────────

BftConsensus::RoundState& BftConsensus::get_or_create_round(uint64_t height, uint64_t round)
{
    auto key = std::make_pair(height, round);
    auto it = m_rounds.find(key);
    if (it != m_rounds.end()) return it->second;

    RoundState rs;
    rs.height = height;
    rs.round = round;
    rs.phase = PHASE_PROPOSE;
    if (height > m_current_height) {
        m_current_height = height;
        m_current_round = round;
        m_current_phase = PHASE_PROPOSE;
    }
    auto result = m_rounds.emplace(key, std::move(rs));
    return result.first->second;
}

// ── Phase advancement ───────────────────────────────────────────────────────

void BftConsensus::advance_phase(RoundState& rs)
{
    switch (rs.phase) {
        case PHASE_PROPOSE:
            // Proposal received → move to PREVOTE
            // (actual prevote is cast by validators upon receiving proposal)
            rs.phase = PHASE_PREVOTE;
            break;

        case PHASE_PREVOTE: {
            // Check if proposed block has ≥2/3 prevotes
            auto it = rs.prevotes.find(rs.proposed_block_hash);
            if (it != rs.prevotes.end() && has_quorum(it->second)) {
                rs.phase = PHASE_PRECOMMIT;
                MINFO("[BFT] h=" << rs.height << " r=" << rs.round
                      << " prevote quorum reached: " << it->second.size()
                      << "/" << validator_count());
            }
            break;
        }

        case PHASE_PRECOMMIT: {
            // Check if proposed block has ≥2/3 precommits
            auto it = rs.precommits.find(rs.proposed_block_hash);
            if (it != rs.precommits.end() && has_quorum(it->second)) {
                rs.phase = PHASE_COMMITTED;
                m_committed[rs.height] = rs.proposed_block_hash;
                MINFO("[BFT] h=" << rs.height << " COMMITTED block "
                      << epee::string_tools::pod_to_hex(rs.proposed_block_hash)
                      << " with " << it->second.size() << "/" << validator_count()
                      << " precommits");

                // Fire commit callback
                if (m_commit_callback && !rs.proposed_block_blob.empty()) {
                    m_commit_callback(rs.height, rs.proposed_block_blob);
                }

                // Advance to next height
                m_current_height = rs.height + 1;
                m_current_round = 0;
                m_current_phase = PHASE_NEW_HEIGHT;
            }
            break;
        }

        default:
            break;
    }
}

// ── Core state machine methods ──────────────────────────────────────────────

int BftConsensus::on_propose(
    uint64_t height,
    uint64_t round,
    const crypto::public_key& proposer_pk,
    const std::vector<uint8_t>& block_blob,
    const crypto::signature& proposer_sig)
{
    std::lock_guard<std::mutex> lk(m_lock);

    // Check round timeout before processing
    check_timeout_locked();

    // Must be current or future height
    if (height < m_current_height) {
        MDEBUG("[BFT] Ignore propose: height " << height << " < current " << m_current_height);
        return 2;
    }

    // Reject if already committed at this height
    if (is_height_committed(height)) {
        MDEBUG("[BFT] Ignore propose: height " << height << " already committed");
        return 2;
    }

    // Verify proposer is the expected one for this (height, round)
    crypto::public_key expected_proposer = get_proposer_for(height, round);
    if (expected_proposer == crypto::public_key{}) {
        MWARNING("[BFT] No eligible proposer for h=" << height << " r=" << round);
        return 1;
    }
    if (proposer_pk != expected_proposer) {
        MWARNING("[BFT] Wrong proposer: got "
                 << epee::string_tools::pod_to_hex(proposer_pk)
                 << " expected " << epee::string_tools::pod_to_hex(expected_proposer)
                 << " for h=" << height << " r=" << round);
        return 1;
    }

    // Verify proposer signature on the block blob
    crypto::hash block_hash;
    if (block_blob.size() >= 32) {
        block_hash = crypto::cn_fast_hash(block_blob.data(), block_blob.size());
    } else {
        MWARNING("[BFT] Empty block blob from proposer h=" << height);
        return 1;
    }

    crypto::signature sig_check;
    if (!crypto::check_signature(block_hash, proposer_pk, proposer_sig)) {
        MWARNING("[BFT] Invalid proposer signature h=" << height << " r=" << round);
        return 1;
    }

    // Accept the proposal
    RoundState& rs = get_or_create_round(height, round);
    rs.proposed_block_hash = block_hash;
    rs.proposed_block_blob = block_blob;
    rs.proposer_pubkey = proposer_pk;
    rs.proposer_sig = proposer_sig;
    rs.phase = PHASE_PREVOTE;

    if (height > m_current_height || (height == m_current_height && round >= m_current_round)) {
        m_current_height = height;
        m_current_round = round;
        m_current_phase = PHASE_PREVOTE;
    }

    MINFO("[BFT] Accepted proposal h=" << height << " r=" << round
          << " proposer=" << epee::string_tools::pod_to_hex(proposer_pk)
          << " block=" << epee::string_tools::pod_to_hex(block_hash));

    // Broadcast our PREVOTE if we are a validator
    if (m_node_key_set && is_validator(m_node_pk) && m_broadcast_vote) {
        NOTIFY_MEVATRUST_VOTE::request vote_req;
        vote_req.height = height;
        vote_req.round = round;
        vote_req.voter_pubkey = m_node_pk;
        vote_req.vote_type = MEVATRUST_VOTE_PREVOTE;
        vote_req.block_hash = block_hash;
        // Sign the vote
        std::string vote_msg;
        vote_msg.append(reinterpret_cast<const char*>(&height), 8);
        vote_msg.append(reinterpret_cast<const char*>(&round), 8);
        vote_msg.push_back(MEVATRUST_VOTE_PREVOTE);
        vote_msg.append(reinterpret_cast<const char*>(block_hash.data), 32);
        crypto::hash vote_hash = crypto::cn_fast_hash(vote_msg.data(), vote_msg.size());
        crypto::generate_signature(vote_hash, m_node_pk, m_node_sk, vote_req.vote_signature);
        m_broadcast_vote(vote_req);
        MINFO("[BFT] Broadcast PREVOTE h=" << height << " r=" << round);
    }

    return 0;
}

int BftConsensus::on_vote(
    uint64_t height,
    uint64_t round,
    const crypto::public_key& voter_pk,
    uint8_t vote_type,
    const crypto::hash& block_hash,
    const crypto::signature& vote_sig)
{
    std::unique_lock<std::mutex> lk(m_lock);

    // Check round timeout before processing
    check_timeout_locked();

    // Reject old heights
    if (height < m_current_height - 1) {
        return 2;
    }

    // Must be an active validator
    if (!is_validator(voter_pk)) {
        MDEBUG("[BFT] Ignore vote from non-validator h=" << height);
        return 1;
    }

    // Verify vote signature
    std::string vote_msg;
    vote_msg.append(reinterpret_cast<const char*>(&height), 8);
    vote_msg.append(reinterpret_cast<const char*>(&round), 8);
    vote_msg.push_back(static_cast<char>(vote_type));
    vote_msg.append(reinterpret_cast<const char*>(block_hash.data), 32);
    crypto::hash vote_hash = crypto::cn_fast_hash(vote_msg.data(), vote_msg.size());

    if (!crypto::check_signature(vote_hash, voter_pk, vote_sig)) {
        MWARNING("[BFT] Invalid vote signature from h=" << height << " r=" << round);
        return 1;
    }

    // Get or create round state
    RoundState& rs = get_or_create_round(height, round);

    // Store the vote (dedup by voter)
    switch (vote_type) {
        case MEVATRUST_VOTE_PREVOTE:
            rs.prevotes[block_hash].insert(voter_pk);
            MDEBUG("[BFT] PREVOTE h=" << height << " r=" << round
                   << " voter=" << epee::string_tools::pod_to_hex(voter_pk)
                   << " count=" << rs.prevotes[block_hash].size()
                   << "/" << validator_count());

            // Check if prevote quorum reached → auto-cast precommit
            if (m_node_key_set && is_validator(m_node_pk) && m_broadcast_vote) {
                if (has_quorum(rs.prevotes[block_hash])) {
                    // Broadcast our PRECOMMIT
                    NOTIFY_MEVATRUST_VOTE::request precommit_req;
                    precommit_req.height = height;
                    precommit_req.round = round;
                    precommit_req.voter_pubkey = m_node_pk;
                    precommit_req.vote_type = MEVATRUST_VOTE_PRECOMMIT;
                    precommit_req.block_hash = block_hash;

                    std::string pc_msg;
                    pc_msg.append(reinterpret_cast<const char*>(&height), 8);
                    pc_msg.append(reinterpret_cast<const char*>(&round), 8);
                    pc_msg.push_back(MEVATRUST_VOTE_PRECOMMIT);
                    pc_msg.append(reinterpret_cast<const char*>(block_hash.data), 32);
                    crypto::hash pc_hash = crypto::cn_fast_hash(pc_msg.data(), pc_msg.size());
                    crypto::generate_signature(pc_hash, m_node_pk, m_node_sk, precommit_req.vote_signature);
                    m_broadcast_vote(precommit_req);
                    MINFO("[BFT] Broadcast PRECOMMIT h=" << height << " r=" << round);
                }
            }
            break;

        case MEVATRUST_VOTE_PRECOMMIT:
            rs.precommits[block_hash].insert(voter_pk);
            MDEBUG("[BFT] PRECOMMIT h=" << height << " r=" << round
                   << " voter=" << epee::string_tools::pod_to_hex(voter_pk)
                   << " count=" << rs.precommits[block_hash].size()
                   << "/" << validator_count());

            // Check if precommit quorum reached → commit
            if (has_quorum(rs.precommits[block_hash]) && rs.phase < PHASE_COMMITTED) {
                rs.phase = PHASE_COMMITTED;
                m_committed[height] = block_hash;

                MINFO("[BFT] h=" << height << " COMMITTED block "
                      << epee::string_tools::pod_to_hex(block_hash)
                      << " with " << rs.precommits[block_hash].size()
                      << "/" << validator_count() << " precommits");

                if (m_commit_callback && !rs.proposed_block_blob.empty()) {
                    m_commit_callback(height, rs.proposed_block_blob);
                }

                // Update prev_id for deterministic proposer seeding
                m_prev_id = block_hash;

                m_current_height = height + 1;
                m_current_round = 0;
                m_current_phase = PHASE_NEW_HEIGHT;

                // Unlock and trigger the next proposer
                lk.unlock();
                try_propose();
                lk.lock();
            }
            break;

        default:
            MWARNING("[BFT] Unknown vote type: " << (int)vote_type);
            return 1;
    }

    return 0;
}

int BftConsensus::on_new_view(
    uint64_t height,
    uint64_t new_round,
    const crypto::public_key& proposer_pk,
    const std::vector<uint8_t>& justification)
{
    std::lock_guard<std::mutex> lk(m_lock);

    if (height < m_current_height) return 2;

    // Verify proposer for the new round
    crypto::public_key expected = get_proposer_for(height, new_round);
    if (proposer_pk != expected) {
        MWARNING("[BFT] Invalid NEW_VIEW proposer for h=" << height << " r=" << new_round);
        return 1;
    }

    // TODO: validate justification (proof that the previous round timed out)

    // Switch to new round
    if (new_round > m_current_round || height > m_current_height) {
        m_current_height = height;
        m_current_round = new_round;
        m_current_phase = PHASE_PROPOSE;
        MINFO("[BFT] View-change: h=" << height << " new_round=" << new_round);
    }

    return 0;
}

// ── Initialization ────────────────────────────────────────────────────────────

void BftConsensus::init_from_chain(uint64_t chain_height, const crypto::hash& chain_tip_hash)
{
    std::lock_guard<std::mutex> lk(m_lock);
    m_current_height = chain_height + 1;   // Next block to produce
    m_current_round = 0;
    m_current_phase = PHASE_NEW_HEIGHT;
    m_last_phase_change = std::chrono::steady_clock::now();
    m_prev_id = chain_tip_hash;
    m_rounds.clear();
    m_committed.clear();
    MINFO("[BFT] Initialized: chain_height=" << chain_height
          << " next_height=" << m_current_height
          << " tip=" << epee::string_tools::pod_to_hex(chain_tip_hash));
}

// ── Proposer trigger ─────────────────────────────────────────────────────────

bool BftConsensus::try_propose()
{
    std::unique_lock<std::mutex> lk(m_lock);
    if (!m_node_key_set || !m_block_proposer || !m_broadcast_propose)
        return false;
    if (!is_current_proposer())
        return false;

    auto req = m_block_proposer(m_current_height, m_current_round);
    if (!req)
    {
        MWARNING("[BFT] Block proposer callback returned nothing for h="
                 << m_current_height << " r=" << m_current_round);
        return false;
    }

    MINFO("[BFT] Proposing block h=" << m_current_height << " r=" << m_current_round
          << " hash=" << epee::string_tools::pod_to_hex(
               crypto::cn_fast_hash(req->block_blob.data(), req->block_blob.size())));

    // Release lock before triggering callbacks to avoid deadlock
    lk.unlock();
    // Process locally (our own proposal arrives via on_propose → vote)
    on_propose(req->height, req->round, req->proposer_pubkey,
               req->block_blob, req->proposer_signature);
    // Broadcast to peers
    m_broadcast_propose(*req);
    return true;
}

// ── Timeout / View-Change ──────────────────────────────────────────────────

bool BftConsensus::check_timeout()
{
    std::lock_guard<std::mutex> lk(m_lock);
    return check_timeout_locked();
}

bool BftConsensus::check_timeout_locked()
{
    // No timeout while at a fresh height or committed
    if (m_current_phase == PHASE_NEW_HEIGHT || m_current_phase == PHASE_COMMITTED)
        return false;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_last_phase_change).count();

    if (elapsed < static_cast<int64_t>(BFT_ROUND_TIMEOUT_MS))
        return false;

    // Phase has timed out → advance to next round
    uint64_t old_round = m_current_round;
    m_current_round++;
    m_current_phase = PHASE_PROPOSE;
    m_last_phase_change = now;

    MINFO("[BFT] Timeout h=" << m_current_height
          << " old_round=" << old_round
          << " new_round=" << m_current_round
          << " — advancing round");

    // If we are the proposer for the new round, broadcast NEW_VIEW
    if (m_node_key_set && m_broadcast_new_view)
    {
        crypto::public_key expected = get_proposer_for(m_current_height, m_current_round);
        if (expected == m_node_pk)
        {
            NOTIFY_MEVATRUST_NEW_VIEW::request req;
            req.height = m_current_height;
            req.new_round = m_current_round;
            req.proposer_pubkey = m_node_pk;
            // TODO: include justification (signed timeout proof)
            m_broadcast_new_view(req);
            MINFO("[BFT] Broadcasting NEW_VIEW h=" << m_current_height
                  << " r=" << m_current_round);
        }
    }

    return true;
}

// ── Queries ─────────────────────────────────────────────────────────────────

bool BftConsensus::is_height_committed(uint64_t height) const
{
    return m_committed.find(height) != m_committed.end();
}

bool BftConsensus::has_pending_proposal(uint64_t height, uint64_t round) const
{
    auto it = m_rounds.find(std::make_pair(height, round));
    if (it == m_rounds.end()) return false;
    return it->second.phase >= PHASE_PROPOSE && it->second.phase < PHASE_COMMITTED;
}

crypto::hash BftConsensus::get_committed_block_hash(uint64_t height) const
{
    auto it = m_committed.find(height);
    if (it != m_committed.end()) return it->second;
    return {};
}

} // namespace cryptonote
