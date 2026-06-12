// Copyright (c) 2024, The Mevacoin Project
// SPDX-License-Identifier: BSD-3-Clause
//
// circle_registry.cpp — CircleRegistry: LMDB-backed per cerchie (micro-DAO).

#include "circle_registry.h"
#include "mevatrust_lmdb.h"
#include "misc_log_ex.h"
#include "string_tools.h"
#include <ctime>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "mevatrust.circle"

namespace cryptonote {
using namespace mevatrust;

bool CircleEntry::is_admin(const crypto::public_key& pk) const {
    return memcmp(&admin_pubkey, &pk, sizeof(pk)) == 0;
}

bool CircleEntry::has_member(const crypto::public_key& pk) const {
    for (const auto& m : members)
        if (memcmp(&m, &pk, sizeof(pk)) == 0) return true;
    return false;
}

static std::string hk(const crypto::hash& h) {
    return epee::string_tools::pod_to_hex(h);
}
static std::string pk_hex(const crypto::public_key& pk) {
    return epee::string_tools::pod_to_hex(pk);
}

static std::string pack_entry(const CircleEntry& e) {
    std::string b; b.reserve(256);
    b.append(reinterpret_cast<const char*>(e.circle_id.data), 32);
    lmdb_write_str(b, e.name);
    b.append(reinterpret_cast<const char*>(&e.admin_pubkey), sizeof(e.admin_pubkey));
    uint32_t mc = (uint32_t)e.members.size(); lmdb_write_pod(b, mc);
    for (auto& m : e.members)
        b.append(reinterpret_cast<const char*>(&m), sizeof(m));
    lmdb_write_pod(b, e.created_height);
    lmdb_write_pod(b, e.created_timestamp);
    lmdb_write_pod(b, e.updated_at);
    lmdb_write_str(b, e.metadata);
    return b;
}

static bool unpack_entry(const void* data, size_t sz, CircleEntry& e) {
    const char* p = (const char*)data, *end = p + sz;
    if (p + 32 > end) return false;
    memcpy(e.circle_id.data, p, 32); p += 32;
    if (!lmdb_read_str(p, end, e.name)) return false;
    if (p + (int)sizeof(e.admin_pubkey) > end) return false;
    memcpy(&e.admin_pubkey, p, sizeof(e.admin_pubkey)); p += sizeof(e.admin_pubkey);
    uint32_t mc = 0; if (!lmdb_read_pod(p, end, mc)) return false;
    e.members.resize(mc);
    for (uint32_t i = 0; i < mc; ++i) {
        if (p + 32 > end) return false;
        memcpy(&e.members[i], p, 32); p += 32;
    }
    if (!lmdb_read_pod(p, end, e.created_height)) return false;
    if (!lmdb_read_pod(p, end, e.created_timestamp)) return false;
    if (!lmdb_read_pod(p, end, e.updated_at)) return false;
    if (!lmdb_read_str(p, end, e.metadata)) return false;
    return true;
}

static crypto::hash compute_circle_id(const std::string& name,
                                       const crypto::public_key& admin,
                                       uint64_t nonce) {
    std::string m;
    m.append(name);
    m.append(reinterpret_cast<const char*>(&admin), sizeof(admin));
    m.append(reinterpret_cast<const char*>(&nonce), sizeof(nonce));
    return crypto::cn_fast_hash(m.data(), m.size());
}

// ── Constructor / Destructor ─────────────────────────────────────────────────
CircleRegistry::CircleRegistry(const std::string& db_path) : db_path_(db_path) {
    mkdir(db_path_.c_str(), 0755);
    try {
        m_env_ = MevaTrustLMDB::open(db_path_);
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
        m_dbi_ = MevaTrustLMDB::open_dbi(txn, DB_MT_CIRCLES);
        m_dbi_proposals_ = MevaTrustLMDB::open_dbi(txn, DB_MT_CIRCLE_PROPOSALS);
        m_dbi_votes_ = MevaTrustLMDB::open_dbi(txn, DB_MT_CIRCLE_VOTES);
        mdb_txn_commit(txn);
    } catch (const std::exception& ex) {
        MERROR("[CircleRegistry] LMDB init: " << ex.what()); m_env_ = nullptr;
    }
    load_from_disk();
}

CircleRegistry::~CircleRegistry() {
    if (m_env_) { MevaTrustLMDB::release(db_path_); m_env_ = nullptr; }
}

// ── LMDB helpers ──────────────────────────────────────────────────────────────
bool CircleRegistry::db_put(const CircleEntry& e) {
    if (!m_env_) return true;
    std::string val = pack_entry(e);
    MDB_val k{32, const_cast<void*>((const void*)e.circle_id.data)};
    MDB_val v{val.size(), const_cast<char*>(val.data())};
    MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
    int rc = mdb_put(txn, m_dbi_, &k, &v, 0);
    if (rc == 0) { mdb_txn_commit(txn); return true; }
    mdb_txn_abort(txn);
    MERROR("[CircleRegistry] mdb_put: " << mdb_strerror(rc));
    return false;
}

bool CircleRegistry::db_del(const crypto::hash& circle_id) {
    if (!m_env_) return true;
    MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
    MDB_val k{32, const_cast<void*>((const void*)circle_id.data)};
    int rc = mdb_del(txn, m_dbi_, &k, nullptr);
    if (rc == 0 || rc == MDB_NOTFOUND) { mdb_txn_commit(txn); return true; }
    mdb_txn_abort(txn);
    MERROR("[CircleRegistry] mdb_del: " << mdb_strerror(rc));
    return false;
}

// ── CRUD ─────────────────────────────────────────────────────────────────────
crypto::hash CircleRegistry::create_circle(const std::string& name,
                                           const crypto::public_key& admin_pubkey,
                                           uint64_t height) {
    std::lock_guard<std::mutex> lk(lock_);
    if (name.empty() || name.size() > 32) {
        MWARNING("[CircleRegistry] create_circle: name vuoto o >32 char");
        return crypto::hash{};
    }
    if (name_exists(name)) {
        MWARNING("[CircleRegistry] create_circle: name gia' esistente: " << name);
        return crypto::hash{};
    }
    uint64_t nonce = (uint64_t)std::time(nullptr);
    crypto::hash cid = compute_circle_id(name, admin_pubkey, nonce);
    CircleEntry e;
    e.circle_id = cid;
    e.name = name;
    e.admin_pubkey = admin_pubkey;
    e.members.push_back(admin_pubkey);
    e.created_height = height;
    e.created_timestamp = nonce;
    e.updated_at = nonce;
    if (!db_put(e)) {
        MERROR("[CircleRegistry] create_circle: db_put fallito");
        return crypto::hash{};
    }
    std::string key = hk(cid);
    circles_[key] = e;
    name_to_id_[name] = key;
    MINFO("[CircleRegistry] Circle creata: " << name
          << " id=" << epee::string_tools::pod_to_hex(cid));
    return cid;
}

bool CircleRegistry::add_member(const crypto::hash& circle_id,
                                const crypto::public_key& member_pubkey,
                                const crypto::public_key& caller_pubkey) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = circles_.find(hk(circle_id));
    if (it == circles_.end()) return false;
    if (!it->second.is_admin(caller_pubkey)) {
        MWARNING("[CircleRegistry] add_member: caller non e' admin");
        return false;
    }
    if (it->second.has_member(member_pubkey)) return false;
    if (it->second.members.size() >= MAX_MEMBERS) {
        MWARNING("[CircleRegistry] add_member: cerchia piena (max " << MAX_MEMBERS << ")");
        return false;
    }
    it->second.members.push_back(member_pubkey);
    it->second.updated_at = (uint64_t)std::time(nullptr);
    return db_put(it->second);
}

bool CircleRegistry::remove_member(const crypto::hash& circle_id,
                                   const crypto::public_key& member_pubkey,
                                   const crypto::public_key& caller_pubkey) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = circles_.find(hk(circle_id));
    if (it == circles_.end()) return false;
    if (!it->second.is_admin(caller_pubkey) &&
        memcmp(&caller_pubkey, &member_pubkey, sizeof(caller_pubkey)) != 0) {
        MWARNING("[CircleRegistry] remove_member: caller non autorizzato");
        return false;
    }
    if (it->second.is_admin(member_pubkey)) return false;
    auto& members = it->second.members;
    members.erase(std::remove_if(members.begin(), members.end(),
        [&](const crypto::public_key& pk){
            return memcmp(&pk, &member_pubkey, sizeof(pk)) == 0;
        }), members.end());
    it->second.updated_at = (uint64_t)std::time(nullptr);
    return db_put(it->second);
}

bool CircleRegistry::change_admin(const crypto::hash& circle_id,
                                  const crypto::public_key& new_admin,
                                  const crypto::public_key& caller_pubkey) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = circles_.find(hk(circle_id));
    if (it == circles_.end()) return false;
    if (!it->second.is_admin(caller_pubkey)) {
        MWARNING("[CircleRegistry] change_admin: caller non e' admin");
        return false;
    }
    if (!it->second.has_member(new_admin)) return false;
    it->second.admin_pubkey = new_admin;
    it->second.updated_at = (uint64_t)std::time(nullptr);
    return db_put(it->second);
}

bool CircleRegistry::disband_circle(const crypto::hash& circle_id,
                                    const crypto::public_key& caller_pubkey) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = circles_.find(hk(circle_id));
    if (it == circles_.end()) return false;
    if (!it->second.is_admin(caller_pubkey)) {
        MWARNING("[CircleRegistry] disband: caller non e' admin");
        return false;
    }
    if (!db_del(circle_id)) return false;
    name_to_id_.erase(it->second.name);
    circles_.erase(it);
    return true;
}

// ── Query ────────────────────────────────────────────────────────────────────
bool CircleRegistry::get_circle(const crypto::hash& circle_id, CircleEntry& out) const {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = circles_.find(hk(circle_id));
    if (it == circles_.end()) return false;
    out = it->second;
    return true;
}

bool CircleRegistry::get_circle_by_name(const std::string& name, CircleEntry& out) const {
    std::lock_guard<std::mutex> lk(lock_);
    auto ni = name_to_id_.find(name);
    if (ni == name_to_id_.end()) return false;
    auto it = circles_.find(ni->second);
    if (it == circles_.end()) return false;
    out = it->second;
    return true;
}

std::vector<CircleEntry> CircleRegistry::list_circles() const {
    std::lock_guard<std::mutex> lk(lock_);
    std::vector<CircleEntry> r; r.reserve(circles_.size());
    for (const auto& kv : circles_) r.push_back(kv.second);
    return r;
}

std::vector<CircleEntry> CircleRegistry::get_circles_for_member(
    const crypto::public_key& pubkey) const {
    std::lock_guard<std::mutex> lk(lock_);
    std::vector<CircleEntry> r;
    for (const auto& kv : circles_) {
        if (kv.second.has_member(pubkey))
            r.push_back(kv.second);
    }
    return r;
}

bool CircleRegistry::is_member(const crypto::hash& circle_id,
                               const crypto::public_key& pubkey) const {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = circles_.find(hk(circle_id));
    if (it == circles_.end()) return false;
    return it->second.has_member(pubkey);
}

bool CircleRegistry::name_exists(const std::string& name) const {
    return name_to_id_.find(name) != name_to_id_.end();
}

// ── Anti-replay tx_hash ───────────────────────────────────────────────────────
bool CircleRegistry::is_tx_processed(const crypto::hash& circle_id, const crypto::hash& tx_hash) const {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = m_processed_txs.find(hk(circle_id));
    if (it == m_processed_txs.end()) return false;
    return it->second.count(epee::string_tools::pod_to_hex(tx_hash)) > 0;
}

void CircleRegistry::mark_tx_processed(const crypto::hash& circle_id, const crypto::hash& tx_hash) {
    std::lock_guard<std::mutex> lk(lock_);
    m_processed_txs[hk(circle_id)].insert(epee::string_tools::pod_to_hex(tx_hash));
}

// ── Forward declarations for static pack/unpack helpers ─────────────────────
static std::string pack_proposal(const ProposalEntry& e);
static bool unpack_proposal(const void* data, size_t sz, ProposalEntry& e);
static std::string pack_vote(const VoteEntry& e);
static bool unpack_vote(const void* data, size_t sz, VoteEntry& e);

// ── Persistence ──────────────────────────────────────────────────────────────
bool CircleRegistry::load_from_disk() {
    if (!m_env_) return false;
    MDB_txn* txn = nullptr;
    try { txn = MevaTrustLMDB::begin_read(m_env_); } catch (...) { return false; }
    MDB_cursor* cur = nullptr;
    if (mdb_cursor_open(txn, m_dbi_, &cur) != 0) { mdb_txn_abort(txn); return false; }
    MDB_val k, v; uint32_t loaded = 0;
    while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
        CircleEntry e{};
        if (!unpack_entry(v.mv_data, v.mv_size, e)) {
            MWARNING("[CircleRegistry] Entry corrotta saltata");
            continue;
        }
        std::string key = hk(e.circle_id);
        circles_[key] = e;
        name_to_id_[e.name] = key;
        ++loaded;
    }
    mdb_cursor_close(cur);
    // Carica proposte
    MDB_cursor* pcur = nullptr;
    if (mdb_cursor_open(txn, m_dbi_proposals_, &pcur) == 0) {
        uint32_t ploaded = 0;
        while (mdb_cursor_get(pcur, &k, &v, MDB_NEXT) == 0) {
            ProposalEntry pe{};
            if (!unpack_proposal(v.mv_data, v.mv_size, pe)) continue;
            std::string pk = hk(pe.proposal_id);
            proposals_[pk] = pe;
            circle_proposals_[hk(pe.circle_id)].insert(pk);
            ++ploaded;
        }
        mdb_cursor_close(pcur);
        MINFO("[CircleRegistry] Caricate " << ploaded << " proposte da LMDB");
    }
    // Carica voti
    MDB_cursor* vcur = nullptr;
    if (mdb_cursor_open(txn, m_dbi_votes_, &vcur) == 0) {
        uint32_t vloaded = 0;
        while (mdb_cursor_get(vcur, &k, &v, MDB_NEXT) == 0) {
            VoteEntry ve{};
            if (!unpack_vote(v.mv_data, v.mv_size, ve)) continue;
            std::string vk = hk(ve.proposal_id) + pk_hex(ve.voter_pk);
            votes_[vk] = ve;
            ++vloaded;
        }
        mdb_cursor_close(vcur);
        MINFO("[CircleRegistry] Caricati " << vloaded << " voti da LMDB");
    }
    mdb_txn_abort(txn);
    MINFO("[CircleRegistry] Caricate " << loaded << " cerchie da LMDB");
    return true;
}

bool CircleRegistry::save_to_disk() const {
    if (!m_env_) return false;
    std::lock_guard<std::mutex> lk(lock_);
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
        for (const auto& kv : circles_) {
            std::string val = pack_entry(kv.second);
            MDB_val mk{32, const_cast<void*>((const void*)kv.second.circle_id.data)};
            MDB_val mv{val.size(), const_cast<char*>(val.data())};
            if (mdb_put(txn, m_dbi_, &mk, &mv, 0) != 0) { mdb_txn_abort(txn); return false; }
        }
        // Salva proposte
        for (const auto& kv : proposals_) {
            std::string val = pack_proposal(kv.second);
            MDB_val mk{32, const_cast<void*>((const void*)kv.second.proposal_id.data)};
            MDB_val mv{val.size(), const_cast<char*>(val.data())};
            if (mdb_put(txn, m_dbi_proposals_, &mk, &mv, 0) != 0) { mdb_txn_abort(txn); return false; }
        }
        // Salva voti
        for (const auto& kv : votes_) {
            std::string val = pack_vote(kv.second);
            std::string key_buf;
            key_buf.append(reinterpret_cast<const char*>(kv.second.proposal_id.data), 32);
            key_buf.append(reinterpret_cast<const char*>(&kv.second.voter_pk), sizeof(kv.second.voter_pk));
            MDB_val mk{key_buf.size(), const_cast<char*>(key_buf.data())};
            MDB_val mv{val.size(), const_cast<char*>(val.data())};
            if (mdb_put(txn, m_dbi_votes_, &mk, &mv, 0) != 0) { mdb_txn_abort(txn); return false; }
        }
        mdb_txn_commit(txn);
    } catch (const std::exception& ex) {
        MERROR("[CircleRegistry] save_to_disk: " << ex.what()); return false;
    }
    return true;
}

// ── Reorg helpers ─────────────────────────────────────────────────────────────
std::string CircleRegistry::pack_circle_entry(const CircleEntry& e) {
    return pack_entry(e);
}

bool CircleRegistry::unpack_circle_entry(const std::string& data, CircleEntry& e) {
    return unpack_entry(data.data(), data.size(), e);
}

bool CircleRegistry::restore_circle(const CircleEntry& e) {
    std::lock_guard<std::mutex> lk(lock_);
    std::string key = hk(e.circle_id);
    circles_[key] = e;
    name_to_id_[e.name] = key;
    return db_put(e);
}

bool CircleRegistry::set_admin(const crypto::hash& circle_id,
                                const crypto::public_key& new_admin) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = circles_.find(hk(circle_id));
    if (it == circles_.end()) return false;
    it->second.admin_pubkey = new_admin;
    it->second.updated_at = (uint64_t)std::time(nullptr);
    return db_put(it->second);
}

bool CircleRegistry::clear_all() {
    std::lock_guard<std::mutex> lk(lock_);
    circles_.clear();
    name_to_id_.clear();
    proposals_.clear();
    votes_.clear();
    circle_proposals_.clear();
    if (m_env_) {
        try {
            MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
            mdb_drop(txn, m_dbi_, 0);
            mdb_drop(txn, m_dbi_proposals_, 0);
            mdb_drop(txn, m_dbi_votes_, 0);
            mdb_txn_commit(txn);
        } catch (...) {}
    }
    return true;
}

// ── Vote packing helpers ───────────────────────────────────────────────────────
static std::string pack_proposal(const ProposalEntry& e) {
    std::string b; b.reserve(128);
    b.append(reinterpret_cast<const char*>(e.proposal_id.data), 32);
    b.append(reinterpret_cast<const char*>(e.circle_id.data), 32);
    b.append(reinterpret_cast<const char*>(&e.proposer_pk), sizeof(e.proposer_pk));
    b.append(reinterpret_cast<const char*>(&e.target_pk), sizeof(e.target_pk));
    lmdb_write_pod(b, e.created_height);
    b.push_back(e.convocation);
    b.push_back(e.status);
    lmdb_write_pod(b, e.finalize_height);
    return b;
}

static bool unpack_proposal(const void* data, size_t sz, ProposalEntry& e) {
    const char* p = (const char*)data, *end = p + sz;
    if (p + 32 > end) return false;
    memcpy(e.proposal_id.data, p, 32); p += 32;
    if (p + 32 > end) return false;
    memcpy(e.circle_id.data, p, 32); p += 32;
    if (p + (int)sizeof(e.proposer_pk) > end) return false;
    memcpy(&e.proposer_pk, p, sizeof(e.proposer_pk)); p += sizeof(e.proposer_pk);
    if (p + (int)sizeof(e.target_pk) > end) return false;
    memcpy(&e.target_pk, p, sizeof(e.target_pk)); p += sizeof(e.target_pk);
    if (!lmdb_read_pod(p, end, e.created_height)) return false;
    if (p + 1 > end) return false;
    e.convocation = (uint8_t)*p++;
    if (p + 1 > end) return false;
    e.status = (uint8_t)*p++;
    if (!lmdb_read_pod(p, end, e.finalize_height)) return false;
    return true;
}

static std::string pack_vote(const VoteEntry& e) {
    std::string b; b.reserve(128);
    b.append(reinterpret_cast<const char*>(e.proposal_id.data), 32);
    b.append(reinterpret_cast<const char*>(&e.voter_pk), sizeof(e.voter_pk));
    b.push_back(e.vote_yes ? 1 : 0);
    lmdb_write_pod(b, e.height);
    return b;
}

static bool unpack_vote(const void* data, size_t sz, VoteEntry& e) {
    const char* p = (const char*)data, *end = p + sz;
    if (p + 32 > end) return false;
    memcpy(e.proposal_id.data, p, 32); p += 32;
    if (p + (int)sizeof(e.voter_pk) > end) return false;
    memcpy(&e.voter_pk, p, sizeof(e.voter_pk)); p += sizeof(e.voter_pk);
    if (p + 1 > end) return false;
    e.vote_yes = (*p++ != 0);
    if (!lmdb_read_pod(p, end, e.height)) return false;
    return true;
}

// ── Proposal/Vote LMDB helpers ─────────────────────────────────────────────────
bool CircleRegistry::db_put_proposal(const ProposalEntry& e) {
    if (!m_env_) return true;
    std::string val = pack_proposal(e);
    MDB_val k{32, const_cast<void*>((const void*)e.proposal_id.data)};
    MDB_val v{val.size(), const_cast<char*>(val.data())};
    MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
    int rc = mdb_put(txn, m_dbi_proposals_, &k, &v, 0);
    if (rc == 0) { mdb_txn_commit(txn); return true; }
    mdb_txn_abort(txn);
    MERROR("[CircleRegistry] db_put_proposal: " << mdb_strerror(rc));
    return false;
}

bool CircleRegistry::db_put_vote(const VoteEntry& e) {
    if (!m_env_) return true;
    std::string val = pack_vote(e);
    // Key = proposal_id (32) + voter_pk (32) = 64 bytes
    std::string key_buf;
    key_buf.append(reinterpret_cast<const char*>(e.proposal_id.data), 32);
    key_buf.append(reinterpret_cast<const char*>(&e.voter_pk), sizeof(e.voter_pk));
    MDB_val k{key_buf.size(), const_cast<char*>(key_buf.data())};
    MDB_val v{val.size(), const_cast<char*>(val.data())};
    MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
    int rc = mdb_put(txn, m_dbi_votes_, &k, &v, 0);
    if (rc == 0) { mdb_txn_commit(txn); return true; }
    mdb_txn_abort(txn);
    MERROR("[CircleRegistry] db_put_vote: " << mdb_strerror(rc));
    return false;
}

bool CircleRegistry::db_get_proposal(const crypto::hash& proposal_id, ProposalEntry& out) const {
    if (!m_env_) return false;
    MDB_txn* txn = MevaTrustLMDB::begin_read(m_env_);
    MDB_val k{32, const_cast<void*>((const void*)proposal_id.data)};
    MDB_val v{0, nullptr};
    int rc = mdb_get(txn, m_dbi_proposals_, &k, &v);
    mdb_txn_abort(txn);
    if (rc != 0) return false;
    return unpack_proposal(v.mv_data, v.mv_size, out);
}

bool CircleRegistry::db_del_proposal(const crypto::hash& proposal_id) {
    if (!m_env_) return true;
    MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
    MDB_val k{32, const_cast<void*>((const void*)proposal_id.data)};
    int rc = mdb_del(txn, m_dbi_proposals_, &k, nullptr);
    if (rc == 0 || rc == MDB_NOTFOUND) { mdb_txn_commit(txn); return true; }
    mdb_txn_abort(txn);
    return false;
}

// ── Voting Logic (prima/seconda convocazione all'italiana) ─────────────────────

crypto::hash CircleRegistry::create_proposal(const crypto::hash& circle_id,
                                              const crypto::public_key& proposer_pk,
                                              const crypto::public_key& target_pk,
                                              uint64_t height) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = circles_.find(hk(circle_id));
    if (it == circles_.end()) { MWARNING("[CircleRegistry] create_proposal: cerchia inesistente"); return crypto::hash{}; }
    if (!it->second.has_member(proposer_pk)) {
        MWARNING("[CircleRegistry] create_proposal: proposer non membro"); return crypto::hash{}; }
    if (it->second.is_admin(target_pk)) {
        MWARNING("[CircleRegistry] create_proposal: target e' gia' admin"); return crypto::hash{}; }
    if (!it->second.has_member(target_pk)) {
        MWARNING("[CircleRegistry] create_proposal: target non e' membro"); return crypto::hash{}; }
    // Genera proposal_id = H(circle_id || target_pk || height)
    std::string m;
    m.append(reinterpret_cast<const char*>(circle_id.data), 32);
    m.append(reinterpret_cast<const char*>(&target_pk), sizeof(target_pk));
    m.append(reinterpret_cast<const char*>(&height), sizeof(height));
    crypto::hash pid = crypto::cn_fast_hash(m.data(), m.size());
    ProposalEntry pe;
    pe.proposal_id = pid;
    pe.circle_id = circle_id;
    pe.proposer_pk = proposer_pk;
    pe.target_pk = target_pk;
    pe.created_height = height;
    pe.convocation = 0;  // Parte in prima convocazione
    pe.status = 0;       // open_first
    pe.finalize_height = 0;
    std::string pk = hk(pid);
    proposals_[pk] = pe;
    circle_proposals_[hk(circle_id)].insert(pk);
    if (!db_put_proposal(pe)) {
        MWARNING("[CircleRegistry] create_proposal: db_put fallito"); return crypto::hash{}; }
    MINFO("[CircleRegistry] Proposta creata id=" << epee::string_tools::pod_to_hex(pid)
          << " per cerchia=" << epee::string_tools::pod_to_hex(circle_id)
          << " target=" << epee::string_tools::pod_to_hex(target_pk));
    return pid;
}

bool CircleRegistry::cast_vote(const crypto::hash& proposal_id,
                                const crypto::public_key& voter_pk,
                                bool vote_yes, uint64_t height) {
    std::lock_guard<std::mutex> lk(lock_);
    auto pit = proposals_.find(hk(proposal_id));
    if (pit == proposals_.end()) { MWARNING("[CircleRegistry] cast_vote: proposta inesistente"); return false; }
    if (!pit->second.is_open()) { MWARNING("[CircleRegistry] cast_vote: proposta non aperta"); return false; }
    auto cit = circles_.find(hk(pit->second.circle_id));
    if (cit == circles_.end()) { MWARNING("[CircleRegistry] cast_vote: cerchia inesistente"); return false; }
    if (!cit->second.has_member(voter_pk)) {
        MWARNING("[CircleRegistry] cast_vote: voter non membro"); return false; }
    // Verifica voto duplicato
    std::string vk = hk(proposal_id) + pk_hex(voter_pk);
    if (votes_.find(vk) != votes_.end()) {
        MWARNING("[CircleRegistry] cast_vote: voto gia' registrato"); return false; }
    VoteEntry ve;
    ve.proposal_id = proposal_id;
    ve.voter_pk = voter_pk;
    ve.vote_yes = vote_yes;
    ve.height = height;
    votes_[vk] = ve;
    if (!db_put_vote(ve)) {
        MWARNING("[CircleRegistry] cast_vote: db_put fallito"); return false; }
    MINFO("[CircleRegistry] Voto registrato: prop=" << epee::string_tools::pod_to_hex(proposal_id)
          << " voter=" << epee::string_tools::pod_to_hex(voter_pk)
          << " voto=" << (vote_yes ? "SI" : "NO"));
    return true;
}

uint8_t CircleRegistry::finalize_vote(const crypto::hash& proposal_id, uint64_t current_height) {
    std::lock_guard<std::mutex> lk(lock_);
    auto pit = proposals_.find(hk(proposal_id));
    if (pit == proposals_.end()) { MWARNING("[CircleRegistry] finalize_vote: proposta inesistente"); return 0xFF; }
    if (!pit->second.is_open()) { MWARNING("[CircleRegistry] finalize_vote: proposta gia' finalizzata"); return pit->second.status; }

    // Deadline enforcement
    uint64_t blocks_since_creation = current_height - pit->second.created_height;
    if (pit->second.convocation == 0) {
        // Prima convocazione: serve almeno VOTE_WINDOW_BLOCKS
        if (blocks_since_creation < VOTE_WINDOW_BLOCKS) {
            MWARNING("[CircleRegistry] finalize_vote: prima convocazione ancora aperta ("
                     << blocks_since_creation << "/" << VOTE_WINDOW_BLOCKS << ")");
            return 0; // ancora aperta
        }
    } else {
        // Seconda convocazione: serve 2 * VOTE_WINDOW_BLOCKS
        if (blocks_since_creation < 2 * VOTE_WINDOW_BLOCKS) {
            MWARNING("[CircleRegistry] finalize_vote: seconda convocazione ancora aperta ("
                     << blocks_since_creation << "/" << 2*VOTE_WINDOW_BLOCKS << ")");
            return 1; // ancora aperta in seconda convocazione
        }
    }

    auto cit = circles_.find(hk(pit->second.circle_id));
    if (cit == circles_.end()) { MWARNING("[CircleRegistry] finalize_vote: cerchia inesistente"); return 0xFF; }

    const size_t total_members = cit->second.members.size();
    if (total_members == 0) { pit->second.status = 3; return 3; } // failed

    // Conta voti SI
    size_t yes_count = 0, total_votes = 0;
    for (const auto& kv : votes_) {
        // La key e' hk(proposal_id) + hk(voter_pk) — verifichiamo inizio
        if (kv.first.compare(0, 64, hk(proposal_id)) == 0) {
            total_votes++;
            if (kv.second.vote_yes) yes_count++;
        }
    }

    bool majority_yes = yes_count > (total_votes - yes_count);
    size_t threshold_first  = (size_t)(total_members * FIRST_CONVOCATION_QUORUM + 0.5);
    size_t threshold_second = (size_t)(total_members * SECOND_CONVOCATION_QUORUM + 0.5);

    if (pit->second.convocation == 0) {
        // Prima convocazione
        if (total_votes >= threshold_first && majority_yes) {
            // APPROVATA in prima convocazione
            cit->second.admin_pubkey = pit->second.target_pk;
            cit->second.updated_at = (uint64_t)std::time(nullptr);
            pit->second.status = 2; // passed
            pit->second.finalize_height = current_height;
            db_put(cit->second);
            db_put_proposal(pit->second);
            MINFO("[CircleRegistry] Proposta APPROVATA in prima convocazione! Nuovo admin: "
                  << epee::string_tools::pod_to_hex(pit->second.target_pk));
            return 2;
        } else if (total_votes >= threshold_first) {
            // Quorum raggiunto ma maggioranza NO -> bocciata
            pit->second.status = 3; // failed
            pit->second.finalize_height = current_height;
            db_put_proposal(pit->second);
            return 3;
        } else {
            // Quorum prima convocazione non raggiunto -> passa a seconda
            pit->second.convocation = 1;
            pit->second.status = 1; // open_second
            MINFO("[CircleRegistry] Prima convocazione fallita (voti=" << total_votes
                  << "/" << threshold_first << "). Passa a seconda convocazione.");
            db_put_proposal(pit->second);
            return 0; // ancora aperta (in seconda)
        }
    } else {
        // Seconda convocazione
        if (total_votes >= threshold_second && majority_yes) {
            cit->second.admin_pubkey = pit->second.target_pk;
            cit->second.updated_at = (uint64_t)std::time(nullptr);
            pit->second.status = 2; // passed
            pit->second.finalize_height = current_height;
            db_put(cit->second);
            db_put_proposal(pit->second);
            MINFO("[CircleRegistry] Proposta APPROVATA in SECONDA convocazione! Nuovo admin: "
                  << epee::string_tools::pod_to_hex(pit->second.target_pk));
            return 2;
        } else if (total_votes >= threshold_second) {
            pit->second.status = 3; // failed
            pit->second.finalize_height = current_height;
            db_put_proposal(pit->second);
            return 3;
        } else {
            // Quorum seconda convocazione non raggiunto -> bocciata
            pit->second.status = 3; // failed
            pit->second.finalize_height = current_height;
            db_put_proposal(pit->second);
            MINFO("[CircleRegistry] Seconda convocazione fallita (voti=" << total_votes
                  << "/" << threshold_second << "). Proposta bocciata.");
            return 3;
        }
    }
}

bool CircleRegistry::get_proposal(const crypto::hash& proposal_id, ProposalEntry& out) const {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = proposals_.find(hk(proposal_id));
    if (it == proposals_.end()) return false;
    out = it->second;
    return true;
}

std::vector<ProposalEntry> CircleRegistry::list_proposals(const crypto::hash& circle_id) const {
    std::lock_guard<std::mutex> lk(lock_);
    std::vector<ProposalEntry> r;
    auto ci = circle_proposals_.find(hk(circle_id));
    if (ci == circle_proposals_.end()) return r;
    for (const auto& pk : ci->second) {
        auto it = proposals_.find(pk);
        if (it != proposals_.end()) r.push_back(it->second);
    }
    return r;
}

std::vector<VoteEntry> CircleRegistry::list_votes(const crypto::hash& proposal_id) const {
    std::lock_guard<std::mutex> lk(lock_);
    std::vector<VoteEntry> r;
    std::string prefix = hk(proposal_id);
    for (const auto& kv : votes_) {
        if (kv.first.compare(0, 64, prefix) == 0) r.push_back(kv.second);
    }
    return r;
}

} // namespace cryptonote
