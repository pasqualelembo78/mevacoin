// Copyright (c) 2024, The Mevacoin Project
// mevatrust_tx_parser.h — FASE 2: On-chain registration scanner
#pragma once
#include <vector>
#include <cstdint>
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/tx_extra.h"

namespace cryptonote { namespace mevatrust {

bool parse_mevatrust_registration_from_tx(const transaction& tx, tx_extra_mevatrust_registration& out);
bool parse_mevatrust_deregister_from_tx(const transaction& tx, tx_extra_mevatrust_deregister& out);
bool parse_mevatrust_snapshot_from_tx(const transaction& tx, tx_extra_mevatrust_snapshot& out);

bool build_mevatrust_registration_extra(const tx_extra_mevatrust_registration& reg, std::vector<uint8_t>& extra_out);
bool build_mevatrust_deregister_extra(const tx_extra_mevatrust_deregister& dereg, std::vector<uint8_t>& extra_out);
bool build_mevatrust_snapshot_extra(const tx_extra_mevatrust_snapshot& snap, std::vector<uint8_t>& extra_out);

bool verify_registration_signature(const tx_extra_mevatrust_registration& reg);
bool verify_deregister_signature(const tx_extra_mevatrust_deregister& dereg);

bool parse_mevatrust_circle_from_tx(const transaction& tx, tx_extra_mevatrust_circle& out);
bool build_mevatrust_circle_extra(const tx_extra_mevatrust_circle& op, std::vector<uint8_t>& extra);
bool verify_circle_signature(const tx_extra_mevatrust_circle& op);

// ── Tag 0xA4: Penalty ──────────────────────────────────────────────────────
bool parse_mevatrust_penalty_from_tx(const transaction& tx, tx_extra_mevatrust_penalty& out);
bool build_mevatrust_penalty_extra(const tx_extra_mevatrust_penalty& p, std::vector<uint8_t>& extra);
bool verify_penalty_signature(const tx_extra_mevatrust_penalty& p);

// ── Tag 0xA5: Uptime Commitment ───────────────────────────────────────────
bool parse_mevatrust_uptime_from_tx(const transaction& tx, tx_extra_mevatrust_uptime& out);
bool build_mevatrust_uptime_extra(const tx_extra_mevatrust_uptime& u, std::vector<uint8_t>& extra);
bool verify_uptime_signature(const tx_extra_mevatrust_uptime& u);

// ── Tag 0xA6: Challenge Result ────────────────────────────────────────────
bool parse_mevatrust_challenge_from_tx(const transaction& tx, tx_extra_mevatrust_challenge& out);
bool build_mevatrust_challenge_extra(const tx_extra_mevatrust_challenge& c, std::vector<uint8_t>& extra);
bool verify_challenge_signatures(const tx_extra_mevatrust_challenge& c);

// ── Tag 0xA7: State Root ───────────────────────────────────────────────────
bool parse_mevatrust_state_root_from_tx(const transaction& tx, tx_extra_mevatrust_state_root& out);
bool build_mevatrust_state_root_extra(const tx_extra_mevatrust_state_root& sr, std::vector<uint8_t>& extra);

// ── Tag 0xA8: Store ─────────────────────────────────────────────────────────
bool parse_mevatrust_store_from_tx(const transaction& tx, tx_extra_mevatrust_store& out);
bool build_mevatrust_store_extra(const tx_extra_mevatrust_store& op, std::vector<uint8_t>& extra);
bool verify_store_signature(const tx_extra_mevatrust_store& op);

// ── Tag 0xA9: Circle Vote ───────────────────────────────────────────────────
bool parse_mevatrust_circle_vote_from_tx(const transaction& tx, tx_extra_mevatrust_circle_vote& out);
bool build_mevatrust_circle_vote_extra(const tx_extra_mevatrust_circle_vote& op, std::vector<uint8_t>& extra);
bool verify_circle_vote_signature(const tx_extra_mevatrust_circle_vote& op);

// ── Tag 0xAA: Pool Distribution ────────────────────────────────────────────
bool parse_mevatrust_pool_distribution_from_tx(const transaction& tx, tx_extra_mevatrust_pool_distribution& out);
bool build_mevatrust_pool_distribution_extra(const tx_extra_mevatrust_pool_distribution& dist, std::vector<uint8_t>& extra);

// ── Tag 0xAB: Validator ────────────────────────────────────────────────────
bool parse_mevatrust_validator_from_tx(const transaction& tx, tx_extra_mevatrust_validator& out);
bool build_mevatrust_validator_extra(const tx_extra_mevatrust_validator& v, std::vector<uint8_t>& extra);
bool verify_validator_signature(const tx_extra_mevatrust_validator& v);

inline crypto::hash store_message_hash(const tx_extra_mevatrust_store& s) {
    std::string m;
    m.push_back(static_cast<uint8_t>(s.op));
    m.append(reinterpret_cast<const char*>(s.store_id.data), sizeof(s.store_id.data));
    m.append(reinterpret_cast<const char*>(s.item_id.data), sizeof(s.item_id.data));
    m.append(s.name);
    m.append(s.description);
    m.append(s.url);
    m.append(reinterpret_cast<const char*>(&s.price), sizeof(s.price));
    m.append(s.category);
    return crypto::cn_fast_hash(m.data(), m.size());
}

// ── Hash helpers ────────────────────────────────────────────────────────────
inline crypto::hash registration_message_hash(const crypto::hash& node_id, const crypto::public_key& node_pubkey) {
    std::string m;
    m.append(reinterpret_cast<const char*>(node_id.data), sizeof(node_id.data));
    m.append(reinterpret_cast<const char*>(&node_pubkey), sizeof(node_pubkey));
    return crypto::cn_fast_hash(m.data(), m.size());
}
inline crypto::hash deregister_message_hash(const crypto::hash& node_id) {
    std::string m;
    m.append(reinterpret_cast<const char*>(node_id.data), sizeof(node_id.data));
    m.append("deregister", 10);
    return crypto::cn_fast_hash(m.data(), m.size());
}
inline crypto::hash circle_message_hash(const crypto::hash& cid, uint8_t op_type,
                                         const crypto::public_key& target,
                                         const std::string& name) {
    std::string m;
    m.push_back(op_type);
    m.append(reinterpret_cast<const char*>(cid.data), sizeof(cid.data));
    m.append(reinterpret_cast<const char*>(&target), sizeof(target));
    m.append(name);
    return crypto::cn_fast_hash(m.data(), m.size());
}
inline crypto::hash penalty_message_hash(const tx_extra_mevatrust_penalty& p) {
    std::string m;
    m.push_back(static_cast<uint8_t>(p.op_type));
    m.append(reinterpret_cast<const char*>(p.node_id.data), sizeof(p.node_id.data));
    m.append(p.offense_type);
    const int64_t amt = p.amount;
    m.append(reinterpret_cast<const char*>(&amt), sizeof(amt));
    m.append(p.reason);
    return crypto::cn_fast_hash(m.data(), m.size());
}
inline crypto::hash uptime_message_hash(const tx_extra_mevatrust_uptime& u) {
    std::string m;
    m.append(reinterpret_cast<const char*>(u.node_id.data), sizeof(u.node_id.data));
    m.append(reinterpret_cast<const char*>(&u.uptime_seconds), sizeof(u.uptime_seconds));
    m.append(reinterpret_cast<const char*>(&u.timestamp), sizeof(u.timestamp));
    m.append(reinterpret_cast<const char*>(&u.peer_count), sizeof(u.peer_count));
    m.push_back(u.is_synced ? 1 : 0);
    m.append(reinterpret_cast<const char*>(&u.sync_height), sizeof(u.sync_height));
    return crypto::cn_fast_hash(m.data(), m.size());
}
inline crypto::hash circle_vote_message_hash(const tx_extra_mevatrust_circle_vote& v) {
    std::string m;
    m.push_back(static_cast<uint8_t>(v.op_type));
    m.append(reinterpret_cast<const char*>(v.proposal_id.data), sizeof(v.proposal_id.data));
    m.append(reinterpret_cast<const char*>(v.circle_id.data), sizeof(v.circle_id.data));
    m.append(reinterpret_cast<const char*>(&v.target_pubkey), sizeof(v.target_pubkey));
    m.push_back(v.vote_yes ? 1 : 0);
    m.append(v.reason);
    return crypto::cn_fast_hash(m.data(), m.size());
}

inline crypto::hash challenge_message_hash(const tx_extra_mevatrust_challenge& c) {
    std::string m;
    m.append(reinterpret_cast<const char*>(c.challenger_node_id.data), sizeof(c.challenger_node_id.data));
    m.append(reinterpret_cast<const char*>(c.challenged_node_id.data), sizeof(c.challenged_node_id.data));
    m.push_back(c.success ? 1 : 0);
    m.append(reinterpret_cast<const char*>(&c.response_time_ms), sizeof(c.response_time_ms));
    m.append(reinterpret_cast<const char*>(&c.height), sizeof(c.height));
    return crypto::cn_fast_hash(m.data(), m.size());
}
inline crypto::hash validator_message_hash(const crypto::hash& node_id, const crypto::public_key& wallet_pubkey) {
    std::string m;
    m.append(reinterpret_cast<const char*>(node_id.data), sizeof(node_id.data));
    m.append(reinterpret_cast<const char*>(&wallet_pubkey), sizeof(wallet_pubkey));
    m.append("validator", 9);
    return crypto::cn_fast_hash(m.data(), m.size());
}
}} // namespace cryptonote::mevatrust



