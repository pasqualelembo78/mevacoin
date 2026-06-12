// Copyright (c) 2024, The Mevacoin Project
// SPDX-License-Identifier: BSD-3-Clause
//
// node_registry.cpp — LMDB-backed (replaces flat binary persistence)
// DROP-IN: src/cryptonote_core/participation/node_registry.cpp
//
// Cambiamenti rispetto alla versione flat-file:
//  - LMDB ACID transazionale: nessuna corruzione su crash/power-loss
//  - Every write (register/update/unregister) committed atomically
//  - Auto-migration: node_registry.dat importato e rinominato .migrated
//  - Stessa API pubblica invariata

#include "node_registry.h"
#include "mevatrust_lmdb.h"
#include "misc_log_ex.h"
#include "string_tools.h"
#include <ctime>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <sys/stat.h>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.registry"

static constexpr uint64_t LEGACY_MAGIC_V1  = 0x4D455641524547FFULL;
static constexpr uint32_t LEGACY_VER1      = 1;
static constexpr uint32_t LEGACY_VER2      = 2;
static constexpr uint32_t MAX_NODES_PER_WALLET = 3;

namespace cryptonote {
using namespace mevatrust;

// ── helpers ──────────────────────────────────────────────────────────────────
static std::string hk(const crypto::hash& h) { return epee::string_tools::pod_to_hex(h); }

static std::string pack_entry(const NodeRegistryEntry& e) {
    std::string b; b.reserve(256);
    b.append(reinterpret_cast<const char*>(e.node_id.data), 32);
    b.append(reinterpret_cast<const char*>(&e.wallet_pubkey),  sizeof(e.wallet_pubkey));
    b.append(reinterpret_cast<const char*>(&e.node_pubkey),    sizeof(e.node_pubkey));
    lmdb_write_pod(b, e.registered_height);
    lmdb_write_pod(b, e.registered_timestamp);
    lmdb_write_pod(b, e.port);
    lmdb_write_pod(b, e.last_seen_timestamp);
    lmdb_write_pod(b, e.peer_count);
    uint8_t sv = (uint8_t)e.status; lmdb_write_pod(b, sv);
    lmdb_write_pod(b, e.last_sync_height);
    uint8_t is = e.is_synchronized ? 1 : 0; lmdb_write_pod(b, is);
    lmdb_write_pod(b, e.reputation_score);
    lmdb_write_pod(b, e.created_at);   lmdb_write_pod(b, e.updated_at);
    lmdb_write_pod(b, e.total_challenges); lmdb_write_pod(b, e.successful_challenges);
    lmdb_write_pod(b, e.total_uptime_seconds); lmdb_write_pod(b, e.disconnections);
    lmdb_write_pod(b, e.last_challenge_time); lmdb_write_pod(b, e.next_challenge_time);
    lmdb_write_str(b, e.wallet_address); lmdb_write_str(b, e.ip_address); lmdb_write_str(b, e.metadata);
    return b;
}

static bool unpack_entry(const void* data, size_t sz, NodeRegistryEntry& e) {
    const char* p = (const char*)data, *end = p + sz;
    if (p + 32 > end) return false;
    memcpy(e.node_id.data, p, 32); p += 32;
    if (p + (int)sizeof(e.wallet_pubkey) > end) return false;
    memcpy(&e.wallet_pubkey, p, sizeof(e.wallet_pubkey)); p += sizeof(e.wallet_pubkey);
    if (p + (int)sizeof(e.node_pubkey) > end) return false;
    memcpy(&e.node_pubkey, p, sizeof(e.node_pubkey)); p += sizeof(e.node_pubkey);
    if (!lmdb_read_pod(p,end,e.registered_height))    return false;
    if (!lmdb_read_pod(p,end,e.registered_timestamp)) return false;
    if (!lmdb_read_pod(p,end,e.port))                 return false;
    if (!lmdb_read_pod(p,end,e.last_seen_timestamp))  return false;
    if (!lmdb_read_pod(p,end,e.peer_count))           return false;
    uint8_t sv=0; if (!lmdb_read_pod(p,end,sv)) return false; e.status=(NodeStatus)sv;
    if (!lmdb_read_pod(p,end,e.last_sync_height))     return false;
    uint8_t is=0; if (!lmdb_read_pod(p,end,is)) return false; e.is_synchronized=(is==1);
    if (!lmdb_read_pod(p,end,e.reputation_score))    return false;
    if (!lmdb_read_pod(p,end,e.created_at))          return false;
    if (!lmdb_read_pod(p,end,e.updated_at))          return false;
    if (!lmdb_read_pod(p,end,e.total_challenges))    return false;
    if (!lmdb_read_pod(p,end,e.successful_challenges)) return false;
    if (!lmdb_read_pod(p,end,e.total_uptime_seconds)) return false;
    if (!lmdb_read_pod(p,end,e.disconnections))      return false;
    if (!lmdb_read_pod(p,end,e.last_challenge_time)) return false;
    if (!lmdb_read_pod(p,end,e.next_challenge_time)) return false;
    if (!lmdb_read_str(p,end,e.wallet_address)) return false;
    if (!lmdb_read_str(p,end,e.ip_address))     return false;
    if (!lmdb_read_str(p,end,e.metadata))       return false;
    return true;
}

static bool db_put(MDB_env* env, MDB_dbi dbi, const NodeRegistryEntry& e) {
    if (!env) return true;
    std::string val = pack_entry(e);
    MDB_val k{32, const_cast<void*>((const void*)e.node_id.data)};
    MDB_val v{val.size(), val.data()};
    MDB_txn* txn = MevaTrustLMDB::begin_write(env);
    int rc = mdb_put(txn, dbi, &k, &v, 0);
    if (rc == 0) { mdb_txn_commit(txn); return true; }
    mdb_txn_abort(txn);
    MERROR("[NodeRegistry] mdb_put: " << mdb_strerror(rc)); return false;
}

// ── Constructor / Destructor ─────────────────────────────────────────────────
NodeRegistry::NodeRegistry(const std::string& db) : db_path_(db) {
    mkdir(db_path_.c_str(), 0755);
    try {
        m_env = MevaTrustLMDB::open(db_path_);
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
        m_dbi = MevaTrustLMDB::open_dbi(txn, DB_MT_NODES);
        mdb_txn_commit(txn);
    } catch (const std::exception& ex) {
        MERROR("[NodeRegistry] LMDB init: " << ex.what()); m_env = nullptr;
    }
    load_from_disk();
    migrate_legacy_file();
}

NodeRegistry::~NodeRegistry() {
    if (m_env) { MevaTrustLMDB::release(db_path_); m_env = nullptr; }
}
bool NodeRegistry::open_database()  { return m_env != nullptr; }
bool NodeRegistry::close_database() { return true; }

// ── Migration from flat file ─────────────────────────────────────────────────
void NodeRegistry::migrate_legacy_file() {
    std::string old_f = db_path_ + "/node_registry.dat";
    std::ifstream f(old_f, std::ios::binary);
    if (!f.is_open()) return;
    MINFO("[NodeRegistry] Migrating legacy flat file → LMDB");
    uint64_t magic=0; uint32_t ver=0,cnt=0;
    f.read((char*)&magic,8); f.read((char*)&ver,4); f.read((char*)&cnt,4);
    if (!f.good() || magic != LEGACY_MAGIC_V1 || (ver!=LEGACY_VER1 && ver!=LEGACY_VER2)) return;
    bool has_ct = (ver == LEGACY_VER2);
    auto rs = [&](std::string& s){ uint16_t l=0; f.read((char*)&l,2); if(l){s.resize(l);f.read(&s[0],l);}else s.clear(); };
    uint32_t n=0;
    for (uint32_t i=0;i<cnt;++i) {
        NodeRegistryEntry e{};
        f.read((char*)e.node_id.data,32);
        f.read((char*)&e.wallet_pubkey,sizeof(e.wallet_pubkey));
        f.read((char*)&e.node_pubkey,sizeof(e.node_pubkey));
        f.read((char*)&e.registered_height,8); f.read((char*)&e.registered_timestamp,8);
        f.read((char*)&e.port,2); f.read((char*)&e.last_seen_timestamp,8);
        f.read((char*)&e.peer_count,4);
        uint8_t sv=0; f.read((char*)&sv,1); e.status=(NodeStatus)sv;
        f.read((char*)&e.last_sync_height,8);
        uint8_t is=0; f.read((char*)&is,1); e.is_synchronized=(is==1);
        f.read((char*)&e.reputation_score,4);
        f.read((char*)&e.created_at,8); f.read((char*)&e.updated_at,8);
        f.read((char*)&e.total_challenges,4); f.read((char*)&e.successful_challenges,4);
        f.read((char*)&e.total_uptime_seconds,8); f.read((char*)&e.disconnections,4);
        if (has_ct) { f.read((char*)&e.last_challenge_time,8); f.read((char*)&e.next_challenge_time,8); }
        rs(e.wallet_address); rs(e.ip_address); rs(e.metadata);
        if (!f.good()) break;
        std::string key = hk(e.node_id);
        if (nodes_.find(key) == nodes_.end()) {
            if (db_put(m_env, m_dbi, e)) {
                nodes_[key]=e;
                if (!e.wallet_address.empty()) wallet_to_nodes_[e.wallet_address].push_back(key);
                if (!e.ip_address.empty() && e.ip_address!="0.0.0.0") ip_to_node_[e.ip_address]=key;
                ++n;
            }
        }
    }
    f.close();
    rename(old_f.c_str(), (old_f+".migrated").c_str());
    MINFO("[NodeRegistry] Migrated " << n << " nodes.");
}

// ── Compute node ID ──────────────────────────────────────────────────────────
crypto::hash NodeRegistry::compute_node_id(const crypto::public_key& wp,
                                            const crypto::public_key& np, uint64_t ts) {
    std::string m; m.reserve(72);
    m.append((const char*)&wp,sizeof(wp)); m.append((const char*)&np,sizeof(np));
    m.append((const char*)&ts,sizeof(ts));
    return crypto::cn_fast_hash(m.data(), m.size());
}

// ── register_node — atomic LMDB write ───────────────────────────────────────
bool NodeRegistry::register_node(const crypto::public_key& wpk, const std::string& wa,
    const crypto::public_key& npk, const crypto::signature& sig,
    uint16_t port, const std::string& ip, uint64_t height)
{
    std::lock_guard<std::mutex> lk(nodes_lock_);
    // Anti-Sybil: max 3 nodes per wallet
    auto wit = wallet_to_nodes_.find(wa);
    if (wit != wallet_to_nodes_.end()) {
        uint32_t active=0;
        for (const auto& k:wit->second) {
            auto ni=nodes_.find(k);
            if (ni!=nodes_.end() && ni->second.status!=NodeStatus::BANNED && ni->second.status!=NodeStatus::OFFLINE) ++active;
        }
        if (active >= MAX_NODES_PER_WALLET) {
            MWARNING("[NodeRegistry] Anti-Sybil: wallet=" << wa << " has " << active << " nodes"); return false;
        }
    }
    // Anti-Sybil: one IP
    if (!ip.empty() && ip!="0.0.0.0") {
        auto iit = ip_to_node_.find(ip);
        if (iit!=ip_to_node_.end()) {
            auto ni=nodes_.find(iit->second);
            if (ni!=nodes_.end() && ni->second.status!=NodeStatus::BANNED && ni->second.status!=NodeStatus::OFFLINE) {
                MWARNING("[NodeRegistry] Anti-Sybil: IP=" << ip << " already active"); return false;
            }
        }
    }
    uint64_t now=(uint64_t)std::time(nullptr);
    crypto::hash nid=compute_node_id(wpk,npk,now);
    std::string key=hk(nid);
    NodeRegistryEntry e{};
    e.node_id=nid; e.wallet_pubkey=wpk; e.node_pubkey=npk;
    e.wallet_address=wa; e.registered_height=height; e.registered_timestamp=now;
    e.registration_signature=sig; e.ip_address=ip; e.port=port;
    e.last_seen_timestamp=now; e.status=NodeStatus::ACTIVE;
    e.last_sync_height=height; e.reputation_score=1.0f;
    e.created_at=now; e.updated_at=now;
    if (!db_put(m_env, m_dbi, e)) return false;
    nodes_[key]=e;
    wallet_to_nodes_[wa].push_back(key);
    if (!ip.empty() && ip!="0.0.0.0") ip_to_node_[ip]=key;
    MINFO("[NodeRegistry] Registered " << epee::string_tools::pod_to_hex(nid));
    return true;
}

bool NodeRegistry::unregister_node(const crypto::hash& nid) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    std::string key=hk(nid);
    auto it=nodes_.find(key);
    if (it==nodes_.end()) return false;
    if (m_env) {
        MDB_txn* txn=MevaTrustLMDB::begin_write(m_env);
        MDB_val k{32, const_cast<void*>((const void*)nid.data)};
        int rc=mdb_del(txn,m_dbi,&k,nullptr);
        if (rc==0||rc==MDB_NOTFOUND) { mdb_txn_commit(txn); } else { mdb_txn_abort(txn); }
    }
    auto& wv=wallet_to_nodes_[it->second.wallet_address];
    wv.erase(std::remove(wv.begin(),wv.end(),key),wv.end());
    if (!it->second.ip_address.empty()) ip_to_node_.erase(it->second.ip_address);
    nodes_.erase(it);
    return true;
}

// ── Update helpers (all write to LMDB immediately) ───────────────────────────
bool NodeRegistry::update_node_status(const crypto::hash& n, NodeStatus s, const std::string& r) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    auto it=nodes_.find(hk(n)); if(it==nodes_.end()) return false;
    it->second.status=s; it->second.updated_at=(uint64_t)std::time(nullptr);
    if (!r.empty()) it->second.metadata=r;
    return db_put(m_env,m_dbi,it->second);
}
bool NodeRegistry::update_node_heartbeat(const crypto::hash& n, uint64_t ts, uint32_t peers, uint64_t sh) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    auto it=nodes_.find(hk(n)); if(it==nodes_.end()) return false;
    auto& e=it->second;
    uint64_t prev=e.last_seen_timestamp;
    e.last_seen_timestamp=ts; e.peer_count=peers; e.last_sync_height=sh;
    if (prev>0 && ts>prev) e.total_uptime_seconds+=(ts-prev);
    e.updated_at=ts;
    return db_put(m_env,m_dbi,e);
}
bool NodeRegistry::update_node_sync(const crypto::hash& n, uint64_t sh, bool synced) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    auto it=nodes_.find(hk(n)); if(it==nodes_.end()) return false;
    it->second.last_sync_height=sh; it->second.is_synchronized=synced;
    it->second.updated_at=(uint64_t)std::time(nullptr);
    return db_put(m_env,m_dbi,it->second);
}
bool NodeRegistry::update_node_challenge_result(const crypto::hash& n, bool ok) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    auto it=nodes_.find(hk(n)); if(it==nodes_.end()) return false;
    ++it->second.total_challenges; if (ok) ++it->second.successful_challenges;
    it->second.updated_at=(uint64_t)std::time(nullptr);
    return db_put(m_env,m_dbi,it->second);
}
bool NodeRegistry::update_node_challenge_time(const crypto::hash& n, uint64_t nxt_ms) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    auto it=nodes_.find(hk(n)); if(it==nodes_.end()) return false;
    it->second.last_challenge_time=(uint64_t)std::time(nullptr)*1000;
    it->second.next_challenge_time=nxt_ms;
    return db_put(m_env,m_dbi,it->second);
}
bool NodeRegistry::update_reputation(const crypto::hash& n, float d) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    auto it=nodes_.find(hk(n)); if(it==nodes_.end()) return false;
    it->second.reputation_score=std::max(0.0f,std::min(1.0f,it->second.reputation_score+d));
    it->second.updated_at=(uint64_t)std::time(nullptr);
    return db_put(m_env,m_dbi,it->second);
}
bool NodeRegistry::reset_reputation_suspect(const crypto::hash& n) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    auto it=nodes_.find(hk(n)); if(it==nodes_.end()) return false;
    it->second.reputation_score=0.5f; it->second.updated_at=(uint64_t)std::time(nullptr);
    return db_put(m_env,m_dbi,it->second);
}

// ── Queries ──────────────────────────────────────────────────────────────────
bool NodeRegistry::get_node_by_id(const crypto::hash& n, NodeRegistryEntry& e) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    auto it=nodes_.find(hk(n)); if(it==nodes_.end()) return false; e=it->second; return true;
}
bool NodeRegistry::get_node_by_wallet(const std::string& wa, NodeRegistryEntry& e) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    for (auto& kv:nodes_) { if(kv.second.wallet_address==wa){e=kv.second;return true;} }
    return false;
}
bool NodeRegistry::get_node_by_pubkey(const crypto::public_key& pk, NodeRegistryEntry& e) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    for (auto& kv:nodes_) { if(memcmp(&kv.second.node_pubkey,&pk,sizeof(pk))==0){e=kv.second;return true;} }
    return false;
}
std::vector<NodeRegistryEntry> NodeRegistry::get_active_nodes(uint32_t limit, uint32_t offset) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    std::vector<NodeRegistryEntry> r; uint32_t sk=0;
    for (auto& kv:nodes_) {
        if (kv.second.status!=NodeStatus::ACTIVE) continue;
        if (sk<offset){++sk;continue;}
        r.push_back(kv.second);
        if (limit>0 && r.size()>=limit) break;
    }
    return r;
}
std::vector<NodeRegistryEntry> NodeRegistry::get_all_nodes() {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    std::vector<NodeRegistryEntry> r; r.reserve(nodes_.size());
    for (auto& kv:nodes_) { r.push_back(kv.second); } return r;
}
std::vector<NodeRegistryEntry> NodeRegistry::get_synchronized_nodes() {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    std::vector<NodeRegistryEntry> r;
    for (auto& kv:nodes_) if(kv.second.status==NodeStatus::ACTIVE&&kv.second.is_synchronized) r.push_back(kv.second);
    return r;
}
std::vector<NodeRegistryEntry> NodeRegistry::get_nodes_by_status(NodeStatus s) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    std::vector<NodeRegistryEntry> r;
    for (auto& kv:nodes_) { if(kv.second.status==s) r.push_back(kv.second); } return r;
}
uint32_t NodeRegistry::get_active_node_count() const {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    uint32_t n=0; for(auto& kv:nodes_) if(kv.second.status==NodeStatus::ACTIVE) ++n; return n;
}
uint32_t NodeRegistry::count_active_nodes() {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    uint32_t n=0; for(auto& kv:nodes_) if(kv.second.status==NodeStatus::ACTIVE) ++n; return n;
}
uint32_t NodeRegistry::get_total_registered_nodes() const {
    std::lock_guard<std::mutex> lk(nodes_lock_); return (uint32_t)nodes_.size();
}
uint32_t NodeRegistry::get_nodes_registered_this_period() const {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    uint64_t ps=(uint64_t)std::time(nullptr)-86400; uint32_t n=0;
    for (auto& kv:nodes_) { if(kv.second.registered_timestamp>=ps) ++n; } return n;
}
float NodeRegistry::get_network_average_uptime() const {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    if (nodes_.empty()) return 0.0f;
    float t=0.0f; for(auto& kv:nodes_) t+=kv.second.reputation_score;
    return t/(float)nodes_.size();
}

// ── Persistence ──────────────────────────────────────────────────────────────
bool NodeRegistry::load_from_disk() {
    if (!m_env) return false;
    MDB_txn* txn=nullptr;
    try { txn=MevaTrustLMDB::begin_read(m_env); } catch(...) { return false; }
    MDB_cursor* cur=nullptr;
    if (mdb_cursor_open(txn,m_dbi,&cur)!=0) { mdb_txn_abort(txn); return false; }
    MDB_val k,v; uint32_t loaded=0;
    while (mdb_cursor_get(cur,&k,&v,MDB_NEXT)==0) {
        NodeRegistryEntry e{};
        if (!unpack_entry(v.mv_data,v.mv_size,e)) { MWARNING("[NodeRegistry] Corrupt LMDB entry skipped"); continue; }
        std::string key=hk(e.node_id);
        nodes_[key]=e;
        if (!e.wallet_address.empty()) wallet_to_nodes_[e.wallet_address].push_back(key);
        if (!e.ip_address.empty() && e.ip_address!="0.0.0.0") ip_to_node_[e.ip_address]=key;
        ++loaded;
    }
    mdb_cursor_close(cur); mdb_txn_abort(txn);
    MINFO("[NodeRegistry] Loaded " << loaded << " nodes from LMDB");
    return true;
}

bool NodeRegistry::save_to_disk() const {
    if (!m_env) return false;
    std::lock_guard<std::mutex> lk(nodes_lock_);
    try {
        MDB_txn* txn=MevaTrustLMDB::begin_write(m_env);
        for (const auto& kv:nodes_) {
            std::string val=pack_entry(kv.second);
            MDB_val mk{32,const_cast<void*>((const void*)kv.second.node_id.data)};
            MDB_val mv{val.size(),val.data()};
            if (mdb_put(txn,m_dbi,&mk,&mv,0)!=0) { mdb_txn_abort(txn); return false; }
        }
        mdb_txn_commit(txn);
    } catch(const std::exception& ex) { MERROR("[NodeRegistry] save_to_disk: " << ex.what()); return false; }
    return true;
}
bool NodeRegistry::sync_database() { return save_to_disk(); }
bool NodeRegistry::clear_all() {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    nodes_.clear(); wallet_to_nodes_.clear(); ip_to_node_.clear();
    if (m_env) {
        try { MDB_txn* txn=MevaTrustLMDB::begin_write(m_env); mdb_drop(txn,m_dbi,0); mdb_txn_commit(txn); } catch(...) {}
    }
    return true;
}


// ── Utility ────────────────────────────────────────────────────────────────────
std::string node_status_to_string(NodeStatus s) {
    switch (s) {
        case NodeStatus::ACTIVE:    return "ACTIVE";
        case NodeStatus::OFFLINE:   return "OFFLINE";
        case NodeStatus::SUSPENDED: return "SUSPENDED";
        case NodeStatus::BANNED:    return "BANNED";
        default: return "UNKNOWN";
    }
}
NodeStatus string_to_node_status(const std::string& s) {
    if (s == "ACTIVE")    return NodeStatus::ACTIVE;
    if (s == "OFFLINE")   return NodeStatus::OFFLINE;
    if (s == "SUSPENDED") return NodeStatus::SUSPENDED;
    if (s == "BANNED")    return NodeStatus::BANNED;
    return NodeStatus::ACTIVE;
}

// ── On-chain registration helpers ────────────────────────────────────────────
bool NodeRegistry::register_node_onchain(const tx_extra_mevatrust_registration& reg, uint64_t height) {
    // Must use reg.node_id directly for consensus:
    // register_node() recomputes node_id from time(nullptr) which is non-deterministic
    std::lock_guard<std::mutex> lk(nodes_lock_);
    // Anti-Sybil: max 3 nodes per wallet
    auto wit = wallet_to_nodes_.find(reg.wallet_address);
    if (wit != wallet_to_nodes_.end()) {
        uint32_t active = 0;
        for (const auto& k : wit->second) {
            auto ni = nodes_.find(k);
            if (ni != nodes_.end() && ni->second.status != NodeStatus::BANNED && ni->second.status != NodeStatus::OFFLINE) ++active;
        }
        if (active >= MAX_NODES_PER_WALLET) {
            MWARNING("[NodeRegistry] Anti-Sybil: wallet=" << reg.wallet_address << " has " << active << " nodes");
            return false;
        }
    }
    uint64_t now = (uint64_t)std::time(nullptr);
    crypto::hash nid = reg.node_id;
    std::string key = hk(nid);
    if (nodes_.find(key) != nodes_.end()) {
        MWARNING("[NodeRegistry] Node gia' registrato: " << key);
        return false;
    }
    NodeRegistryEntry e{};
    e.node_id = nid; e.wallet_pubkey = reg.wallet_pubkey; e.node_pubkey = reg.node_pubkey;
    e.wallet_address = reg.wallet_address; e.registered_height = height; e.registered_timestamp = now;
    e.registration_signature = reg.signature; e.ip_address = ""; e.port = (uint16_t)reg.port;
    e.last_seen_timestamp = now; e.status = NodeStatus::ACTIVE;
    e.last_sync_height = height; e.reputation_score = 1.0f;
    e.created_at = now; e.updated_at = now;
    if (!db_put(m_env, m_dbi, e)) return false;
    nodes_[key] = e;
    wallet_to_nodes_[reg.wallet_address].push_back(key);
    MINFO("[NodeRegistry] On-chain registrato " << epee::string_tools::pod_to_hex(nid) << " h=" << height);
    return true;
}

bool NodeRegistry::deregister_node_onchain(const tx_extra_mevatrust_deregister& dereg) {
    // Verify wallet key matches stored entry, then unregister
    NodeRegistryEntry existing{};
    if (!get_node_by_id(dereg.node_id, existing)) return false;
    // Basic sanity check: wallet pubkey must match
    if (memcmp(&existing.wallet_pubkey, &dereg.wallet_pubkey, sizeof(dereg.wallet_pubkey)) != 0) {
        MWARNING("[NodeRegistry] deregister_node_onchain: wallet_pubkey mismatch for "
                 << epee::string_tools::pod_to_hex(dereg.node_id));
        return false;
    }
    return unregister_node(dereg.node_id);
}

// ── Uptime / heartbeat ────────────────────────────────────────────────────────
bool NodeRegistry::record_uptime_event(const crypto::hash& node_id, bool online,
                                        uint64_t ts, uint32_t peers, uint32_t /*discon_unused*/) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    auto it = nodes_.find(hk(node_id));
    if (it == nodes_.end()) return false;
    auto& e = it->second;
    const uint64_t prev = e.last_seen_timestamp;
    e.last_seen_timestamp = ts;
    e.peer_count          = peers;
    e.updated_at          = ts;
    if (online) {
        e.status = NodeStatus::ACTIVE;
        if (prev > 0 && ts > prev) e.total_uptime_seconds += (ts - prev);
    } else {
        e.status = NodeStatus::OFFLINE;
        ++e.disconnections;
    }
    return db_put(m_env, m_dbi, e);
}

// ── Ban / Unban ───────────────────────────────────────────────────────────────
bool NodeRegistry::ban_node(const crypto::hash& nid, const std::string& reason) {
    return update_node_status(nid, NodeStatus::BANNED, reason);
}
bool NodeRegistry::unban_node(const crypto::hash& nid) {
    return update_node_status(nid, NodeStatus::ACTIVE, "unbanned");
}

// ── Wallet pubkey lookup ──────────────────────────────────────────────────────
bool NodeRegistry::is_wallet_pubkey_registered(const crypto::public_key& pk) const {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    for (const auto& kv : nodes_) {
        if (memcmp(&kv.second.wallet_pubkey, &pk, sizeof(pk)) == 0)
            return true;
    }
    return false;
}

// ── Expiry sweep ──────────────────────────────────────────────────────────────
uint32_t NodeRegistry::expire_inactive_nodes(uint64_t /*current_height*/, uint64_t max_offline_blocks) {
    std::lock_guard<std::mutex> lk(nodes_lock_);
    const uint64_t now            = (uint64_t)std::time(nullptr);
    const uint64_t max_offline_s  = max_offline_blocks * 120ULL; // ~2 min/block
    uint32_t expired = 0;
    for (auto& kv : nodes_) {
        if (kv.second.status != NodeStatus::ACTIVE) continue;
        const uint64_t last = kv.second.last_seen_timestamp;
        if (last == 0 || (now - last) < max_offline_s) continue;
        kv.second.status     = NodeStatus::OFFLINE;
        kv.second.updated_at = now;
        db_put(m_env, m_dbi, kv.second);
        ++expired;
    }
    if (expired > 0)
        MINFO("[NodeRegistry] expire_inactive_nodes: marked " << expired << " nodes OFFLINE");
    return expired;
}

} // namespace cryptonote





