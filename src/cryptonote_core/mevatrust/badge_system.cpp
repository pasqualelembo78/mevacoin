// Copyright (c) 2024, The Mevacoin Project
// SPDX-License-Identifier: BSD-3-Clause
//
// badge_system.cpp — LMDB-backed (replaces flat binary persistence)
// DROP-IN: src/cryptonote_core/participation/badge_system.cpp
//
// LMDB table:
//   PART_BADGES  key=node_id[32]  value=packed badges array
//
// Format del value:
//   num_badges[4]
//   per ogni badge:
//     type[1] | awarded_height[8] | awarded_timestamp[8] | is_active[1]
//     name_len[1] | name[name_len]
//     reason_len[1] | reason[reason_len]
//
// Ogni award/revoke scrive IMMEDIATAMENTE su LMDB (non solo in memoria).
// Auto-migration: badge_cache.dat importato e rinominato .migrated.

#include "badge_system.h"
#include "node_registry.h"
#include "mevatrust_engine.h"
#include "mevatrust_lmdb.h"
#include "misc_log_ex.h"
#include "string_tools.h"
#include <ctime>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <sys/stat.h>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.badges"

static constexpr uint64_t LEGACY_BADGE_MAGIC   = 0x4D455641424144FFULL;
static constexpr uint32_t LEGACY_BADGE_VERSION = 1;
static constexpr uint64_t BLOCKS_PER_DAY       = 720;
static constexpr uint64_t EARLY_HEIGHT_MAX      = 100000;

namespace cryptonote {
using namespace mevatrust;

// ── Serialization ─────────────────────────────────────────────────────────────
static std::string pack_badges(const std::vector<Badge>& bv) {
    std::string b;
    uint32_t n = (uint32_t)bv.size();
    b.append((const char*)&n, 4);
    for (const auto& bg : bv) {
        uint8_t t = (uint8_t)bg.type;                   b += (char)t;
        b.append((const char*)&bg.awarded_height, 8);
        b.append((const char*)&bg.awarded_timestamp, 8);
        b += (char)(bg.is_active ? 1 : 0);
        uint8_t nl = (uint8_t)std::min(bg.name.size(), (size_t)255);
        b += (char)nl; if (nl) b.append(bg.name.data(), nl);
        uint8_t rl = (uint8_t)std::min(bg.revocation_reason.size(), (size_t)255);
        b += (char)rl; if (rl) b.append(bg.revocation_reason.data(), rl);
    }
    return b;
}

static bool unpack_badges(const void* data, size_t sz, std::vector<Badge>& out) {
    const uint8_t* p = (const uint8_t*)data, *end = p + sz;
    if (p + 4 > end) return false;
    uint32_t n; memcpy(&n, p, 4); p += 4;
    for (uint32_t i = 0; i < n; ++i) {
        if (p >= end) break;
        Badge bg{};
        bg.type = (BadgeType)(*p++);
        if (p + 16 > end) break;
        memcpy(&bg.awarded_height,    p, 8); p += 8;
        memcpy(&bg.awarded_timestamp, p, 8); p += 8;
        if (p >= end) break;
        bg.is_active = (*p++ == 1);
        if (p >= end) break;
        uint8_t nl = *p++; if (p + nl > end) break;
        bg.name.assign((const char*)p, nl); p += nl;
        if (p >= end) break;
        uint8_t rl = *p++; if (p + rl > end) break;
        bg.revocation_reason.assign((const char*)p, rl); p += rl;
        out.push_back(bg);
    }
    return true;
}

static bool db_put_badges(MDB_env* env, MDB_dbi dbi, const crypto::hash& nid, const std::vector<Badge>& bv) {
    if (!env) return true;
    std::string val = pack_badges(bv);
    MDB_val k{32, const_cast<void*>((const void*)nid.data)};
    MDB_val v{val.size(), val.data()};
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_write(env);
        int rc = mdb_put(txn, dbi, &k, &v, 0);
        if (rc == 0) { mdb_txn_commit(txn); return true; }
        mdb_txn_abort(txn);
        MERROR("[BadgeSystem] db_put_badges: " << mdb_strerror(rc));
    } catch(const std::exception& ex) { MERROR("[BadgeSystem] " << ex.what()); }
    return false;
}

// ── Constructor / Destructor ──────────────────────────────────────────────────
BadgeSystem::BadgeSystem(const std::string& db, std::shared_ptr<NodeRegistry> nr,
                         std::shared_ptr<MevaTrustEngine> pe)
    : db_path_(db), node_registry_(nr), mevatrust_engine_(pe)
{
    mkdir(db_path_.c_str(), 0755);
    try {
        m_env = MevaTrustLMDB::open(db_path_);
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
        m_dbi = MevaTrustLMDB::open_dbi(txn, DB_MT_BADGES);
        mdb_txn_commit(txn);
    } catch (const std::exception& ex) {
        MERROR("[BadgeSystem] LMDB init: " << ex.what()); m_env = nullptr;
    }
    initialize_default_badges();
    load_from_disk();
    migrate_legacy_file();
}
BadgeSystem::~BadgeSystem() {
    if (m_env) { MevaTrustLMDB::release(db_path_); m_env = nullptr; }
}

// ── Default badge criteria ────────────────────────────────────────────────────
bool BadgeSystem::initialize_default_badges() {
    struct R { BadgeType t; const char* nm,*dsc; uint64_t uh; float up; uint64_t days; float sp;
               uint32_t blk,peers,chal,txs; float rup,rsp; uint32_t rod; bool perm,ren; uint64_t rdays; };
    static const R T[] = {
     {BadgeType::ACTIVE_MINER,      "Active Miner",      "24h+, 10+ challenges",     24,.50f,3,.80f,10,0,0,0,.20f,.40f,14,false,true,30},
     {BadgeType::FULL_NODE_OPERATOR,"Full Node Operator", "100h+, 95%+ sync, 7d",   100,.80f,7,.95f,0,5,5,0,.50f,.85f,7,false,true,30},
     {BadgeType::STABLE_NODE,       "Stable Node",        "90d, 90%+ uptime",       2160,.90f,90,.90f,0,3,50,0,.70f,.80f,14,false,true,90},
     {BadgeType::CORE_NETWORK_NODE, "Core Network Node",  "180d, rep>=0.8",         4320,.92f,180,.97f,0,8,200,0,.80f,.90f,7,false,true,60},
     {BadgeType::LONG_UPTIME_NODE,  "Long Uptime Node",   "365d, 95%+ uptime",      8760,.95f,365,.95f,0,3,100,0,.85f,.90f,7,false,true,90},
     {BadgeType::EARLY_SUPPORTER,   "Early Supporter",    "First 100k blocks",         0,.00f,0,.00f,0,0,0,0,.00f,.00f,365,true,false,0},
     {BadgeType::NETWORK_VALIDATOR, "Network Validator",  "100+ challenges",           0,.60f,14,.85f,0,3,100,0,.30f,.60f,30,false,true,60},
     {BadgeType::BRIDGE_NODE,       "Bridge Node",        "20+ peers",               720,.80f,30,.90f,0,20,50,0,.50f,.75f,14,false,true,30},
     {BadgeType::PRIVACY_GUARDIAN,  "Privacy Guardian",   "rep>=0.85, 98%+ sync",    336,.85f,14,.98f,0,3,30,0,.60f,.90f,10,false,true,30},
     {BadgeType::RELAY_MASTER,      "Relay Master",       "activity>=50%",           168,.75f,7,.85f,0,5,20,100,.40f,.70f,21,false,true,30},
    };
    for (const auto& r : T) {
        BadgeRequirements req{};
        req.type=r.t; req.name=r.nm; req.description=r.dsc;
        req.minimum_uptime_hours=r.uh; req.minimum_uptime_percentage=r.up;
        req.minimum_days_registered=r.days; req.minimum_sync_percentage=r.sp;
        req.minimum_block_mined=r.blk; req.minimum_peer_connections=r.peers;
        req.minimum_challenges_validated=r.chal; req.minimum_transactions_relayed=r.txs;
        req.revoke_if_uptime_below=r.rup; req.revoke_if_sync_below=r.rsp;
        req.revoke_after_days_offline=r.rod; req.is_permanent=r.perm;
        req.is_renewable=r.ren; req.renewal_period_days=r.rdays;
        badge_requirements_[req.type]=req;
    }
    return true;
}

// ── Persistence ───────────────────────────────────────────────────────────────
bool BadgeSystem::load_from_disk() {
    if (!m_env) return false;
    MDB_txn* txn = nullptr;
    try { txn = MevaTrustLMDB::begin_read(m_env); } catch(...) { return false; }
    MDB_cursor* cur = nullptr;
    if (mdb_cursor_open(txn, m_dbi, &cur) != 0) { mdb_txn_abort(txn); return false; }
    MDB_val k, v; uint32_t loaded = 0;
    while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
        if (k.mv_size != 32) continue;
        crypto::hash nid; memcpy(nid.data, k.mv_data, 32);
        std::vector<Badge> bv;
        if (unpack_badges(v.mv_data, v.mv_size, bv)) {
            std::lock_guard<std::mutex> lk(cache_lock_);
            badge_cache_[nid] = bv;
            ++loaded;
        }
    }
    mdb_cursor_close(cur); mdb_txn_abort(txn);
    MINFO("[BadgeSystem] Loaded badges for " << loaded << " nodes from LMDB");
    return true;
}

bool BadgeSystem::save_to_disk() const {
    if (!m_env) return false;
    std::lock_guard<std::mutex> lk(cache_lock_);
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
        for (const auto& kv : badge_cache_) {
            if (kv.second.empty()) continue;
            std::string val = pack_badges(kv.second);
            MDB_val mk{32, const_cast<void*>((const void*)kv.first.data)};
            MDB_val mv{val.size(), val.data()};
            if (mdb_put(txn, m_dbi, &mk, &mv, 0) != 0) { mdb_txn_abort(txn); return false; }
        }
        mdb_txn_commit(txn);
    } catch(const std::exception& ex) { MERROR("[BadgeSystem] save_to_disk: " << ex.what()); return false; }
    return true;
}

// ── Migration ─────────────────────────────────────────────────────────────────
void BadgeSystem::migrate_legacy_file() {
    std::string old_f = db_path_ + "/badge_cache.dat";
    std::ifstream f(old_f, std::ios::binary);
    if (!f.is_open()) return;
    MINFO("[BadgeSystem] Migrating legacy badge_cache.dat → LMDB");
    uint64_t magic=0; uint32_t ver=0,cnt=0;
    f.read((char*)&magic,8); f.read((char*)&ver,4); f.read((char*)&cnt,4);
    if (!f.good() || magic!=LEGACY_BADGE_MAGIC || ver!=LEGACY_BADGE_VERSION) return;
    uint32_t imported=0;
    for (uint32_t n=0;n<cnt;++n) {
        crypto::hash nid{}; f.read((char*)nid.data,32);
        uint32_t bc=0; f.read((char*)&bc,4);
        std::vector<Badge> bv;
        for (uint32_t b=0;b<bc;++b) {
            Badge bg{};
            uint8_t tv=0,ia=0,nl=0,rl=0;
            f.read((char*)&tv,1); f.read((char*)&bg.awarded_height,8);
            f.read((char*)&bg.awarded_timestamp,8); f.read((char*)&ia,1);
            f.read((char*)&nl,1); if(nl){bg.name.resize(nl);f.read(&bg.name[0],nl);}
            f.read((char*)&rl,1); if(rl){bg.revocation_reason.resize(rl);f.read(&bg.revocation_reason[0],rl);}
            if (!f.good()) break;
            bg.type=(BadgeType)tv; bg.is_active=(ia==1);
            if (bg.name.empty()) bg.name=badge_type_to_string(bg.type);
            bv.push_back(bg);
        }
        if (!bv.empty()) {
            std::lock_guard<std::mutex> lk(cache_lock_);
            if (badge_cache_.find(nid)==badge_cache_.end()) {
                badge_cache_[nid]=bv;
                db_put_badges(m_env, m_dbi, nid, bv);
                ++imported;
            }
        }
    }
    f.close();
    rename(old_f.c_str(), (old_f+".migrated").c_str());
    MINFO("[BadgeSystem] Migrated badges for " << imported << " nodes.");
}

// ── award_badge — writes immediately to LMDB ─────────────────────────────────
bool BadgeSystem::award_badge(const crypto::hash& nid, BadgeType bt, uint64_t h, const std::string& reason) {
    std::lock_guard<std::mutex> lk(cache_lock_);
    auto& bv = badge_cache_[nid];
    // Check if already active
    for (auto& b : bv) if (b.type==bt && b.is_active) return true;
    Badge bg{};
    bg.type             = bt;
    bg.name             = badge_type_to_string(bt);
    bg.awarded_height   = h;
    bg.awarded_timestamp= (uint64_t)std::time(nullptr);
    bg.is_active        = true;
    if (!reason.empty()) bg.revocation_reason = reason;
    bv.push_back(bg);
    bool ok = db_put_badges(m_env, m_dbi, nid, bv);
    if (ok) {
        MINFO("[BadgeSystem] Awarded " << bg.name << " to " << epee::string_tools::pod_to_hex(nid));
        // C2-FIX: pubblica badge on-chain via callback (0xA2)
        if (on_chain_cb_) {
            if (!on_chain_cb_(nid, static_cast<uint8_t>(bt), h, reason))
                MWARNING("[BadgeSystem] on_chain_cb_ fallita badge=" << bg.name);
        } else {
            MWARNING("[BadgeSystem] on_chain_cb_ non configurata -- badge non on-chain.");
        }
    }
    return ok;
}

bool BadgeSystem::revoke_badge(const crypto::hash& nid, BadgeType bt, const std::string& reason) {
    std::lock_guard<std::mutex> lk(cache_lock_);
    auto it = badge_cache_.find(nid);
    if (it == badge_cache_.end()) return false;
    bool found = false;
    for (auto& b : it->second) {
        if (b.type==bt && b.is_active) {
            b.is_active=false; b.revocation_reason=reason; found=true; break;
        }
    }
    if (!found) return false;
    bool ok = db_put_badges(m_env, m_dbi, nid, it->second);
    if (ok) MINFO("[BadgeSystem] Revoked badge type=" << (int)bt
                  << " from " << epee::string_tools::pod_to_hex(nid));
    return ok;
}

// ── Criteria checks (unchanged logic) ────────────────────────────────────────
bool BadgeSystem::check_active_miner_criteria(const crypto::hash& nid) {
    if (!node_registry_) return false;
    NodeRegistryEntry e; if (!node_registry_->get_node_by_id(nid,e)) return false;
    auto it=badge_requirements_.find(BadgeType::ACTIVE_MINER); if (it==badge_requirements_.end()) return false;
    const auto& req=it->second;
    uint64_t uptime_h=e.total_uptime_seconds/3600;
    if (uptime_h < req.minimum_uptime_hours) return false;
    if (e.total_challenges < 10) return false;
    if (e.reputation_score < 0.30f) return false;
    return true;
}
bool BadgeSystem::check_full_node_operator_criteria(const crypto::hash& nid, uint64_t h) {
    if (!node_registry_||!mevatrust_engine_) return false;
    NodeRegistryEntry e; if (!node_registry_->get_node_by_id(nid,e)) return false;
    auto it=badge_requirements_.find(BadgeType::FULL_NODE_OPERATOR); if (it==badge_requirements_.end()) return false;
    const auto& req=it->second;
    uint64_t days=(uint64_t)std::time(nullptr)>e.registered_timestamp?((uint64_t)std::time(nullptr)-e.registered_timestamp)/86400:0;
    if (days<req.minimum_days_registered) return false;
    if (e.peer_count<req.minimum_peer_connections) return false;
    if (e.total_challenges<req.minimum_challenges_validated) return false;
    if (mevatrust_engine_->get_uptime_percentage(nid)<req.minimum_uptime_percentage) return false;
    if (mevatrust_engine_->get_sync_percentage(nid,h)<req.minimum_sync_percentage) return false;
    return true;
}
bool BadgeSystem::check_stable_node_criteria(const crypto::hash& nid, uint64_t h) {
    if (!node_registry_||!mevatrust_engine_) return false;
    NodeRegistryEntry e; if (!node_registry_->get_node_by_id(nid,e)) return false;
    auto it=badge_requirements_.find(BadgeType::STABLE_NODE); if (it==badge_requirements_.end()) return false;
    const auto& req=it->second;
    uint64_t days=((uint64_t)std::time(nullptr)-e.registered_timestamp)/86400;
    if (days<req.minimum_days_registered) return false;
    if (mevatrust_engine_->get_uptime_percentage(nid)<req.minimum_uptime_percentage) return false;
    if (mevatrust_engine_->get_sync_percentage(nid,h)<req.minimum_sync_percentage) return false;
    return true;
}
bool BadgeSystem::check_core_network_node_criteria(const crypto::hash& nid, uint64_t h) {
    if (!node_registry_||!mevatrust_engine_) return false;
    NodeRegistryEntry e; if (!node_registry_->get_node_by_id(nid,e)) return false;
    auto it=badge_requirements_.find(BadgeType::CORE_NETWORK_NODE); if (it==badge_requirements_.end()) return false;
    const auto& req=it->second;
    uint64_t days=((uint64_t)std::time(nullptr)-e.registered_timestamp)/86400;
    if (days<req.minimum_days_registered) return false;
    if (e.reputation_score<0.80f) return false;
    if (e.peer_count<req.minimum_peer_connections) return false;
    if (mevatrust_engine_->get_uptime_percentage(nid)<req.minimum_uptime_percentage) return false;
    if (mevatrust_engine_->get_sync_percentage(nid,h)<req.minimum_sync_percentage) return false;
    return true;
}
bool BadgeSystem::check_long_uptime_node_criteria(const crypto::hash& nid, uint64_t h) {
    if (!node_registry_||!mevatrust_engine_) return false;
    NodeRegistryEntry e; if (!node_registry_->get_node_by_id(nid,e)) return false;
    auto it=badge_requirements_.find(BadgeType::LONG_UPTIME_NODE); if (it==badge_requirements_.end()) return false;
    const auto& req=it->second;
    uint64_t days=((uint64_t)std::time(nullptr)-e.registered_timestamp)/86400;
    if (days<req.minimum_days_registered) return false;
    if (mevatrust_engine_->get_uptime_percentage(nid)<req.minimum_uptime_percentage) return false;
    if (mevatrust_engine_->get_sync_percentage(nid,h)<req.minimum_sync_percentage) return false;
    return true;
}
bool BadgeSystem::check_early_supporter_criteria(const crypto::hash& nid) {
    if (!node_registry_) return false;
    NodeRegistryEntry e; if (!node_registry_->get_node_by_id(nid,e)) return false;
    return e.registered_height <= EARLY_HEIGHT_MAX;
}
bool BadgeSystem::check_network_validator_criteria(const crypto::hash& nid) {
    if (!node_registry_||!mevatrust_engine_) return false;
    NodeRegistryEntry e; if (!node_registry_->get_node_by_id(nid,e)) return false;
    auto it=badge_requirements_.find(BadgeType::NETWORK_VALIDATOR); if (it==badge_requirements_.end()) return false;
    const auto& req=it->second;
    if (e.successful_challenges<req.minimum_challenges_validated) return false;
    if (mevatrust_engine_->get_challenge_success_rate(nid)<0.60f) return false;
    if (mevatrust_engine_->get_uptime_percentage(nid)<req.minimum_uptime_percentage) return false;
    return true;
}

bool BadgeSystem::qualifies_for_badge(const crypto::hash& nid, BadgeType bt, uint64_t h) {
    if (!node_registry_) return false;
    NodeRegistryEntry e; if (!node_registry_->get_node_by_id(nid,e)) return false;
    if (e.status==NodeStatus::BANNED) return false;
    switch (bt) {
        case BadgeType::ACTIVE_MINER:       return check_active_miner_criteria(nid);
        case BadgeType::FULL_NODE_OPERATOR: return check_full_node_operator_criteria(nid,h);
        case BadgeType::STABLE_NODE:        return check_stable_node_criteria(nid,h);
        case BadgeType::CORE_NETWORK_NODE:  return check_core_network_node_criteria(nid,h);
        case BadgeType::LONG_UPTIME_NODE:   return check_long_uptime_node_criteria(nid,h);
        case BadgeType::EARLY_SUPPORTER:    return check_early_supporter_criteria(nid);
        case BadgeType::NETWORK_VALIDATOR:  return check_network_validator_criteria(nid);
        case BadgeType::BRIDGE_NODE: {
            auto it=badge_requirements_.find(bt); if(it==badge_requirements_.end()) return false;
            if (e.peer_count<it->second.minimum_peer_connections) return false;
            if (!mevatrust_engine_) return false;
            return mevatrust_engine_->get_uptime_percentage(nid)>=it->second.minimum_uptime_percentage;
        }
        case BadgeType::PRIVACY_GUARDIAN: {
            auto it=badge_requirements_.find(bt); if(it==badge_requirements_.end()) return false;
            if (e.reputation_score<0.85f) return false;
            if (!mevatrust_engine_) return false;
            return mevatrust_engine_->get_sync_percentage(nid,h)>=it->second.minimum_sync_percentage;
        }
        case BadgeType::RELAY_MASTER: {
            auto it=badge_requirements_.find(bt); if(it==badge_requirements_.end()) return false;
            if (!mevatrust_engine_) return false;
            if (mevatrust_engine_->get_activity_score(nid)<0.50f) return false;
            return mevatrust_engine_->get_uptime_percentage(nid)>=it->second.minimum_uptime_percentage;
        }
        default: return false;
    }
}

bool BadgeSystem::auto_evaluate_badges(const crypto::hash& nid, uint64_t h) {
    static const BadgeType ALL[]={BadgeType::ACTIVE_MINER,BadgeType::FULL_NODE_OPERATOR,BadgeType::STABLE_NODE,
        BadgeType::CORE_NETWORK_NODE,BadgeType::LONG_UPTIME_NODE,BadgeType::EARLY_SUPPORTER,
        BadgeType::NETWORK_VALIDATOR,BadgeType::BRIDGE_NODE,BadgeType::PRIVACY_GUARDIAN,BadgeType::RELAY_MASTER};
    bool changed=false;
    for (BadgeType bt:ALL) {
        bool q=qualifies_for_badge(nid,bt,h), has=has_badge(nid,bt);
        if (q && !has) { award_badge(nid,bt,h,"auto-evaluation"); changed=true; }
    }
    return changed;
}

uint32_t BadgeSystem::evaluate_all_badges(uint64_t h) {
    if (!node_registry_) return 0;
    auto nodes=node_registry_->get_active_nodes(0,0); uint32_t n=0;
    for (const auto& e:nodes) if (auto_evaluate_badges(e.node_id,h)) ++n;
    return n;
}

// ── Query methods ─────────────────────────────────────────────────────────────
std::vector<Badge> BadgeSystem::get_node_badges(const crypto::hash& nid) {
    std::lock_guard<std::mutex> lk(cache_lock_);
    auto it=badge_cache_.find(nid); return it!=badge_cache_.end()?it->second:std::vector<Badge>{};
}
std::vector<Badge> BadgeSystem::get_active_badges(const crypto::hash& nid) {
    std::lock_guard<std::mutex> lk(cache_lock_);
    std::vector<Badge> a; auto it=badge_cache_.find(nid);
    if (it!=badge_cache_.end()) for(const auto& b:it->second) if(b.is_active) a.push_back(b);
    return a;
}
bool BadgeSystem::has_badge(const crypto::hash& nid, BadgeType bt) {
    std::lock_guard<std::mutex> lk(cache_lock_);
    auto it=badge_cache_.find(nid); if(it==badge_cache_.end()) return false;
    for (const auto& b:it->second) { if(b.type==bt&&b.is_active) return true; }
    return false;
}
bool BadgeSystem::get_badge_details(BadgeType bt, Badge& out) {
    out = {};
    out.type=bt; out.name=badge_type_to_string(bt); out.is_active=false; return true;
}
bool BadgeSystem::get_badge_requirements(BadgeType t, BadgeRequirements& r) {
    auto it=badge_requirements_.find(t); if(it==badge_requirements_.end()) return false; r=it->second; return true;
}
std::vector<BadgeRequirements> BadgeSystem::get_all_requirements() {
    std::vector<BadgeRequirements> v; for(const auto& kv:badge_requirements_) v.push_back(kv.second); return v;
}
std::vector<BadgeType> BadgeSystem::get_badge_types_for_wallet(const std::string& wa) {
    std::vector<BadgeType> t; if(!node_registry_) return t;
    NodeRegistryEntry e; if(!node_registry_->get_node_by_wallet(wa,e)) return t;
    for(const auto& b:get_active_badges(e.node_id)) t.push_back(b.type);
    return t;
}
std::vector<crypto::hash> BadgeSystem::get_nodes_with_badge(BadgeType bt) {
    std::vector<crypto::hash> r; std::lock_guard<std::mutex> lk(cache_lock_);
    for(const auto& kv:badge_cache_) for(const auto& b:kv.second) if(b.type==bt&&b.is_active){r.push_back(kv.first);break;}
    return r;
}
BadgeSystem::NodeBadgeInfo BadgeSystem::get_node_badge_info(const crypto::hash& nid) {
    NodeBadgeInfo info; info.node_id=nid; info.badges=get_node_badges(nid);
    info.badge_count=(uint32_t)info.badges.size(); info.active_badge_count=0; info.badge_score=0.0f;
    for(const auto& b:info.badges) if(b.is_active){++info.active_badge_count; info.badge_score+=1.0f;}
    return info;
}
std::vector<BadgeSystem::TopHolder> BadgeSystem::get_top_badge_holders(uint32_t cnt) {
    std::vector<TopHolder> r; std::lock_guard<std::mutex> lk(cache_lock_);
    for(const auto& kv:badge_cache_) {
        uint32_t n=0; for(const auto& b:kv.second) if(b.is_active) ++n;
        if (n>0) { TopHolder th{}; th.node_id=kv.first; th.active_badge_count=n; r.push_back(th); }
    }
    std::sort(r.begin(),r.end(),[](const TopHolder& a,const TopHolder& b){return a.active_badge_count>b.active_badge_count;});
    if (cnt>0&&r.size()>cnt) r.resize(cnt);
    return r;
}
std::vector<BadgeSystem::BadgeStatistics> BadgeSystem::get_badge_statistics() { return {}; }

std::vector<std::pair<crypto::hash,BadgeType>> BadgeSystem::get_recently_awarded(uint64_t from_h, uint64_t to_h) {
    std::vector<std::pair<crypto::hash,BadgeType>> result;
    std::lock_guard<std::mutex> lk(cache_lock_);
    for(const auto& kv:badge_cache_) for(const auto& b:kv.second)
        if(b.is_active&&b.awarded_height>=from_h&&b.awarded_height<=to_h)
            result.push_back({kv.first,b.type});
    return result;
}

bool BadgeSystem::open_database()  { return m_env!=nullptr; }
bool BadgeSystem::close_database() { return true; }

// ── String conversions ────────────────────────────────────────────────────────
std::string BadgeSystem::badge_type_to_string(BadgeType t) const {
    switch(t) {
        case BadgeType::ACTIVE_MINER:       return "ACTIVE_MINER";
        case BadgeType::FULL_NODE_OPERATOR: return "FULL_NODE_OPERATOR";
        case BadgeType::STABLE_NODE:        return "STABLE_NODE";
        case BadgeType::CORE_NETWORK_NODE:  return "CORE_NETWORK_NODE";
        case BadgeType::LONG_UPTIME_NODE:   return "LONG_UPTIME_NODE";
        case BadgeType::EARLY_SUPPORTER:    return "EARLY_SUPPORTER";
        case BadgeType::NETWORK_VALIDATOR:  return "NETWORK_VALIDATOR";
        case BadgeType::BRIDGE_NODE:        return "BRIDGE_NODE";
        case BadgeType::PRIVACY_GUARDIAN:   return "PRIVACY_GUARDIAN";
        case BadgeType::RELAY_MASTER:       return "RELAY_MASTER";
        case BadgeType::WELCOME:            return "WELCOME";
        default: return "UNKNOWN";
    }
}
BadgeType BadgeSystem::string_to_badge_type(const std::string& s) const {
    if(s=="ACTIVE_MINER")       return BadgeType::ACTIVE_MINER;
    if(s=="FULL_NODE_OPERATOR") return BadgeType::FULL_NODE_OPERATOR;
    if(s=="STABLE_NODE")        return BadgeType::STABLE_NODE;
    if(s=="CORE_NETWORK_NODE")  return BadgeType::CORE_NETWORK_NODE;
    if(s=="LONG_UPTIME_NODE")   return BadgeType::LONG_UPTIME_NODE;
    if(s=="EARLY_SUPPORTER")    return BadgeType::EARLY_SUPPORTER;
    if(s=="NETWORK_VALIDATOR")  return BadgeType::NETWORK_VALIDATOR;
    if(s=="BRIDGE_NODE")        return BadgeType::BRIDGE_NODE;
    if(s=="PRIVACY_GUARDIAN")   return BadgeType::PRIVACY_GUARDIAN;
    if(s=="RELAY_MASTER")       return BadgeType::RELAY_MASTER;
    if(s=="WELCOME")            return BadgeType::WELCOME;
    return BadgeType::BADGE_UNKNOWN;
}
std::string BadgeSystem::get_disqualification_reason(const crypto::hash& nid, BadgeType bt, uint64_t h) {
    if (!node_registry_) return "NodeRegistry not available";
    NodeRegistryEntry e; if (!node_registry_->get_node_by_id(nid,e)) return "Node not found";
    if (e.status==NodeStatus::BANNED) return "Node is banned";
    auto it=badge_requirements_.find(bt); if (it==badge_requirements_.end()) return "Unknown badge";
    const auto& req=it->second;
    uint64_t days=((uint64_t)std::time(nullptr)-e.registered_timestamp)/86400;
    if (days<req.minimum_days_registered) return "Insufficient days registered ("+std::to_string(days)+"/"+std::to_string(req.minimum_days_registered)+")";
    if (mevatrust_engine_) {
        float up=mevatrust_engine_->get_uptime_percentage(nid);
        if (up<req.minimum_uptime_percentage) return "Uptime too low ("+std::to_string((int)(up*100))+"%)";
        float sp=mevatrust_engine_->get_sync_percentage(nid,h);
        if (sp<req.minimum_sync_percentage) return "Sync too low ("+std::to_string((int)(sp*100))+"%)";
    }
    return "Criteria not met";
}

} // namespace cryptonote





