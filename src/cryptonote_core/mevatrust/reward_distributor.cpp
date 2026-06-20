// Copyright (c) 2024, The Mevacoin Project
// SPDX-License-Identifier: BSD-3-Clause
//
// reward_distributor.cpp — LMDB-backed (replaces flat binary persistence)
// DROP-IN: src/cryptonote_core/mevatrust/reward_distributor.cpp
//
// LMDB tables:
//   PART_POOL    key="pool"[4]           value=PoolState (8+8+8+8+4 = 36 bytes)
//   PART_REWARDS key=node_id[32]         value=packed RewardRecord (DUPSORT, ordered by height)
//                dup key=block_height[8] (big-endian for correct ordering)
//
// Migration: se pool.dat e/o rewards.bin esistono,
//            vengono importati e rinominati *.migrated.

#include "reward_distributor.h"
#include "mevatrust_lmdb.h"
#include "mevatrust_engine.h"
#include "misc_log_ex.h"
#include "string_tools.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.rewards"

// Legacy file constants
static constexpr uint64_t LEGACY_POOL_MAGIC = 0x4D455641524577FFULL;

// Packed RewardRecord for LMDB dup-value (fixed 44 bytes)
// Layout: amount[8] | timestamp[8] | node_score[4] | total_score[4] | padding[4]
// Key: block_height as big-endian uint64 (so cursor gives ascending height order)
static void pack_reward(const cryptonote::RewardRecord& r, uint8_t key_be[8], uint8_t val[24]) {
    // key: block_height big-endian
    for (int i=0;i<8;++i) key_be[i] = (r.block_height >> (56 - 8*i)) & 0xFF;
    // value: amount(8) | timestamp(8) | node_score(4) | total_score(4)
    memcpy(val+0,  &r.amount,     8);
    memcpy(val+8,  &r.timestamp,  8);
    memcpy(val+16, &r.node_score, 4);
    memcpy(val+20, &r.total_score,4);
}
static void unpack_reward(const uint8_t key_be[8], const uint8_t val[24],
                           const crypto::hash& nid, cryptonote::RewardRecord& r) {
    r.node_id = nid;
    uint64_t h=0; for(int i=0;i<8;++i) h=(h<<8)|key_be[i];
    r.block_height = h;
    memcpy(&r.amount,     val+0,  8);
    memcpy(&r.timestamp,  val+8,  8);
    memcpy(&r.node_score, val+16, 4);
    memcpy(&r.total_score,val+20, 4);
}

// Pool state packed (36 bytes)
struct PoolState {
    uint64_t pool_balance;
    uint64_t total_distributed;
    uint64_t last_distribution_height;
    uint32_t distribution_count;
};
static constexpr const char POOL_KEY[] = "pool";

namespace cryptonote {
using namespace mevatrust;

RewardDistributor::RewardDistributor(const std::string& db_path)
    : m_db_path(db_path), m_pool_balance(0), m_total_distributed(0),
      m_last_distribution_height(0), m_distribution_count(0),
      m_distribution_period(240), m_maturation_blocks(10),
      m_pool_fraction(3), m_min_score_for_reward(0.5f)
{
    mkdir(m_db_path.c_str(), 0755);
    try {
        m_env = MevaTrustLMDB::open(m_db_path);
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
        m_dbi_pool    = MevaTrustLMDB::open_dbi(txn, DB_MT_POOL);
        m_dbi_rewards = MevaTrustLMDB::open_dbi(txn, DB_MT_REWARDS, MDB_CREATE|MDB_DUPSORT|MDB_DUPFIXED);
        m_dbi_welcome = MevaTrustLMDB::open_dbi(txn, DB_MT_WELCOME);
        mdb_txn_commit(txn);
    } catch (const std::exception& ex) {
        MERROR("[RewardDistributor] LMDB init: " << ex.what()); m_env=nullptr;
    }
    load_pool_state();
    migrate_legacy_files();
    MINFO("[RewardDistributor] init pool=" << m_pool_balance);
}

RewardDistributor::~RewardDistributor() {
    save_pool_state();
    if (m_env) { MevaTrustLMDB::release(m_db_path); m_env=nullptr; }
}

// ── Pool state persistence ────────────────────────────────────────────────────
bool RewardDistributor::save_pool_state() const {
    if (!m_env) return false;
    PoolState ps{ m_pool_balance, m_total_distributed, m_last_distribution_height, m_distribution_count };
    MDB_val k{4, const_cast<char*>(POOL_KEY)};
    MDB_val v{sizeof(ps), &ps};
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
        int rc = mdb_put(txn, m_dbi_pool, &k, &v, 0);
        if (rc==0) { mdb_txn_commit(txn); return true; }
        mdb_txn_abort(txn);
        MERROR("[RewardDistributor] save_pool_state mdb_put: " << mdb_strerror(rc));
    } catch(const std::exception& ex) { MERROR("[RewardDistributor] " << ex.what()); }
    return false;
}

bool RewardDistributor::load_pool_state() {
    if (!m_env) return false;
    MDB_val k{4, const_cast<char*>(POOL_KEY)}, v{};
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_read(m_env);
        int rc = mdb_get(txn, m_dbi_pool, &k, &v);
        mdb_txn_abort(txn);
        if (rc==MDB_NOTFOUND) return true; // first start
        if (rc!=0) { MERROR("[RewardDistributor] load_pool mdb_get: " << mdb_strerror(rc)); return false; }
        if (v.mv_size < sizeof(PoolState)) return false;
        PoolState ps; memcpy(&ps, v.mv_data, sizeof(ps));
        m_pool_balance              = ps.pool_balance;
        m_total_distributed         = ps.total_distributed;
        m_last_distribution_height  = ps.last_distribution_height;
        m_distribution_count        = ps.distribution_count;
        return true;
    } catch(...) { return false; }
}

// ── Append reward record ──────────────────────────────────────────────────────
bool RewardDistributor::append_reward_record(const RewardRecord& rec) {
    if (!m_env) return false;
    uint8_t key_be[8], val[24];
    pack_reward(rec, key_be, val);
    MDB_val mk{32, const_cast<void*>((const void*)rec.node_id.data)};
    // dup key is the 8-byte big-endian block_height
    // but for MDB_DUPSORT the data IS the block_height+val concatenated:
    // We store 8+24 = 32 bytes as dup-data: height_BE[8] + payload[24]
    uint8_t dup_data[32];
    memcpy(dup_data, key_be, 8);
    memcpy(dup_data+8, val, 24);
    MDB_val mv{32, dup_data};
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
        int rc = mdb_put(txn, m_dbi_rewards, &mk, &mv, MDB_NODUPDATA);
        if (rc==0 || rc==MDB_KEYEXIST) { mdb_txn_commit(txn); return true; }
        mdb_txn_abort(txn);
        MERROR("[RewardDistributor] append_reward mdb_put: " << mdb_strerror(rc));
    } catch(const std::exception& ex) { MERROR("[RewardDistributor] " << ex.what()); }
    return false;
}

// ── accumulate_reward ─────────────────────────────────────────────────────────
bool RewardDistributor::accumulate_reward(uint64_t amount, uint64_t block_height) {
    std::lock_guard<std::mutex> lk(m_pool_lock);
    m_pool_balance += amount;
    // Periodic pool-state save every 60 blocks
    if (block_height % 60 == 0) save_pool_state();
    return true;
}

// ── Welcome bonus LMDB ────────────────────────────────────────────────────────
bool RewardDistributor::schedule_welcome_bonus(const crypto::hash& nid, uint64_t maturity_height, uint64_t amount) {
    if (!m_env) return false;
    uint8_t val[16];
    memcpy(val, &amount, 8);
    memcpy(val+8, &maturity_height, 8);
    MDB_val mk{32, const_cast<void*>((const void*)nid.data)};
    MDB_val mv{16, val};
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
        int rc = mdb_put(txn, m_dbi_welcome, &mk, &mv, 0);
        if (rc==0) { mdb_txn_commit(txn); return true; }
        mdb_txn_abort(txn);
    } catch(...) {}
    return false;
}
bool RewardDistributor::remove_welcome_bonus(const crypto::hash& nid) {
    if (!m_env) return false;
    MDB_val mk{32, const_cast<void*>((const void*)nid.data)};
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
        int rc = mdb_del(txn, m_dbi_welcome, &mk, nullptr);
        if (rc==0 || rc==MDB_NOTFOUND) { mdb_txn_commit(txn); return true; }
        mdb_txn_abort(txn);
    } catch(...) {}
    return false;
}
std::vector<PendingCoinbaseOutput> RewardDistributor::process_welcome_bonuses(uint64_t current_height) {
    std::vector<PendingCoinbaseOutput> outputs;
    uint64_t total_deduction = 0;
    if (!m_env) return outputs;
    MDB_txn* txn = nullptr;
    try { txn = MevaTrustLMDB::begin_write(m_env); } catch(...) { return outputs; }
    if (!txn) return outputs;
    MDB_cursor* cur = nullptr;
    if (mdb_cursor_open(txn, m_dbi_welcome, &cur) != 0) { mdb_txn_abort(txn); return outputs; }
    MDB_val k, v;
    while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
        if (k.mv_size != 32 || v.mv_size < 16) continue;
        crypto::hash nid; memcpy(nid.data, k.mv_data, 32);
        uint64_t amount, mat_height;
        memcpy(&amount, v.mv_data, 8);
        memcpy(&mat_height, (const uint8_t*)v.mv_data+8, 8);
        if (current_height > 0 && mat_height > current_height) continue;

        std::string nid_hex = epee::string_tools::pod_to_hex(nid);
        PendingCoinbaseOutput pco;
        pco.node_id_str    = nid_hex;
        pco.wallet_address = "";
        pco.amount         = amount;
        pco.score          = 1.0f;
        outputs.push_back(std::move(pco));
        total_deduction += amount;

        // Record reward
        {
            uint8_t key_be[8], val[24];
            for (int i=0;i<8;++i) key_be[i] = (current_height >> (56 - 8*i)) & 0xFF;
            memcpy(val+0,  &amount,      8);
            uint64_t ts = (uint64_t)std::time(nullptr); memcpy(val+8, &ts, 8);
            float ns=1.0f, ts2=1.0f; memcpy(val+16, &ns, 4); memcpy(val+20, &ts2, 4);
            uint8_t dup_data[32]; memcpy(dup_data, key_be, 8); memcpy(dup_data+8, val, 24);
            MDB_val mk{32, const_cast<void*>((const void*)nid.data)};
            MDB_val mv{32, dup_data};
            mdb_put(txn, m_dbi_rewards, &mk, &mv, MDB_NODUPDATA);
        }

        mdb_cursor_del(cur, 0);
        MINFO("[RewardDistributor] Welcome bonus matured: nid=" << nid_hex
              << " amount=" << amount << " h=" << current_height);
    }
    mdb_cursor_close(cur);

    // Deduct from pool balance
    if (total_deduction > 0) {
        PoolState ps;
        MDB_val pk{4, const_cast<char*>(POOL_KEY)}, pv{};
        if (mdb_get(txn, m_dbi_pool, &pk, &pv) == 0 && pv.mv_size >= sizeof(PoolState)) {
            memcpy(&ps, pv.mv_data, sizeof(PoolState));
            uint64_t deduct = std::min(total_deduction, ps.pool_balance);
            ps.pool_balance -= deduct;
            ps.total_distributed += deduct;
            ps.last_distribution_height = current_height;
            ++ps.distribution_count;
            MDB_val nv{sizeof(ps), &ps};
            mdb_put(txn, m_dbi_pool, &pk, &nv, 0);
            std::lock_guard<std::mutex> lk(m_pool_lock);
            uint64_t actual_deduct = std::min(deduct, m_pool_balance);
            m_pool_balance         -= actual_deduct;
            m_total_distributed    += actual_deduct;
            m_last_distribution_height = ps.last_distribution_height;
            m_distribution_count   = ps.distribution_count;
        }
    }

    mdb_txn_commit(txn);
    return outputs;
}

// ── distribute_rewards ────────────────────────────────────────────────────────
bool RewardDistributor::distribute_rewards(std::shared_ptr<MevaTrustEngine> engine, uint64_t height) {
    if (!engine) return false;
    std::lock_guard<std::mutex> lk(m_pool_lock);
    if (m_pool_balance == 0) return true;

    // calculate_all_scores returns map<string,MevaTrustScoreSnapshot>
    auto score_map = engine->calculate_all_scores(height);
    std::vector<MevaTrustScoreSnapshot> scores;
    scores.reserve(score_map.size());
    for (auto& kv : score_map) scores.push_back(kv.second);
    if (scores.empty()) return true;

    float total_score = 0.0f;
    for (const auto& s : scores)
        if (s.total_score >= m_min_score_for_reward) total_score += s.total_score;
    if (total_score <= 0.0f) return true;

    uint64_t allocated = 0;
    m_pending_outputs.clear();

    for (const auto& snap : scores) {
        if (snap.total_score < m_min_score_for_reward) continue;
        uint64_t share = static_cast<uint64_t>(
            static_cast<double>(m_pool_balance) * snap.total_score / total_score);
        if (share == 0) continue;

        // Get wallet address from registry
        // (MevaTrustEngine stores the node_id; wallet is in NodeRegistry)
        std::string nid = epee::string_tools::pod_to_hex(snap.node_id);

        PendingCoinbaseOutput pco;
        pco.node_id_str   = nid;
        pco.wallet_address= ""; // filled by mevatrust_manager using node_registry
        pco.amount        = share;
        pco.score         = snap.total_score;
        m_pending_outputs.push_back(std::move(pco));
        allocated += share;

        RewardRecord rec;
        rec.node_id      = snap.node_id;
        rec.amount       = share;
        rec.block_height = snap.period_height;
        rec.timestamp    = static_cast<uint64_t>(std::time(nullptr));
        rec.node_score   = snap.total_score;
        rec.total_score  = total_score;
        append_reward_record(rec);
    }

    m_pool_balance         -= allocated;
    m_total_distributed    += allocated;
    ++m_distribution_count;
    m_last_distribution_height = height;
    save_pool_state();
    MINFO("[RewardDistributor] Distribution #" << m_distribution_count
          << " allocated=" << allocated << " outputs=" << m_pending_outputs.size());
    return true;
}

std::vector<PendingCoinbaseOutput> RewardDistributor::get_pending_coinbase_outputs() {
    std::lock_guard<std::mutex> lk(m_pool_lock);
    std::vector<PendingCoinbaseOutput> out; out.swap(m_pending_outputs); return out;
}
bool RewardDistributor::has_pending_outputs() const {
    std::lock_guard<std::mutex> lk(m_pool_lock); return !m_pending_outputs.empty();
}

// ── Query helpers ─────────────────────────────────────────────────────────────
uint64_t RewardDistributor::get_node_reward(const crypto::hash& nid, uint64_t period_height) {
    if (!m_env) return 0;
    MDB_val mk{32, const_cast<void*>((const void*)nid.data)}, mv{};
    uint64_t total=0;
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_read(m_env);
        MDB_cursor* cur; mdb_cursor_open(txn, m_dbi_rewards, &cur);
        int rc = mdb_cursor_get(cur, &mk, &mv, MDB_SET);
        while (rc==0) {
            if (mv.mv_size >= 32) {
                uint8_t* d = (uint8_t*)mv.mv_data;
                uint64_t h=0; for(int i=0;i<8;++i) h=(h<<8)|d[i];
                if (h == period_height) {
                    uint64_t a; memcpy(&a, d+8, 8); total += a;
                }
            }
            rc = mdb_cursor_get(cur, &mk, &mv, MDB_NEXT_DUP);
        }
        mdb_cursor_close(cur); mdb_txn_abort(txn);
    } catch(...) {}
    return total;
}

std::vector<RewardRecord> RewardDistributor::get_reward_history(const crypto::hash& nid, uint64_t limit) {
    std::vector<RewardRecord> hist;
    if (!m_env) return hist;
    MDB_val mk{32, const_cast<void*>((const void*)nid.data)}, mv{};
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_read(m_env);
        MDB_cursor* cur; mdb_cursor_open(txn, m_dbi_rewards, &cur);
        int rc = mdb_cursor_get(cur, &mk, &mv, MDB_SET);
        while (rc==0 && (limit==0 || hist.size()<limit)) {
            if (mv.mv_size >= 32) {
                uint8_t* d = (uint8_t*)mv.mv_data;
                RewardRecord r;
                unpack_reward(d, d+8, nid, r);
                hist.push_back(r);
            }
            rc = mdb_cursor_get(cur, &mk, &mv, MDB_NEXT_DUP);
        }
        mdb_cursor_close(cur); mdb_txn_abort(txn);
    } catch(...) {}
    return hist;
}

uint64_t RewardDistributor::get_pool_balance() {
    std::lock_guard<std::mutex> lk(m_pool_lock); return m_pool_balance;
}
uint64_t RewardDistributor::get_total_distributed() const {
    std::lock_guard<std::mutex> lk(m_pool_lock); return m_total_distributed;
}
uint64_t RewardDistributor::get_last_distribution_height() const {
    std::lock_guard<std::mutex> lk(m_pool_lock); return m_last_distribution_height;
}
bool RewardDistributor::is_distribution_due(uint64_t current_height) const {
    std::lock_guard<std::mutex> lk(m_pool_lock);
    return (current_height - m_last_distribution_height) >= m_distribution_period;
}
bool RewardDistributor::calculate_node_share(const crypto::hash&, float score,
                                              float total, uint64_t pool, uint64_t& out) {
    if (total <= 0.0f || pool == 0) { out=0; return false; }
    out = static_cast<uint64_t>((double)pool * score / total);
    return out > 0;
}

// ── Migration from legacy flat files ─────────────────────────────────────────
void RewardDistributor::migrate_legacy_files() {
    // 1. Pool state
    std::string pool_f = m_db_path + "/mevatrust_pool.dat";
    {
        std::ifstream f(pool_f, std::ios::binary);
        if (f.is_open()) {
            uint64_t magic=0,pool=0,total=0,last=0;
            f.read((char*)&magic,8); f.read((char*)&pool,8);
            f.read((char*)&total,8); f.read((char*)&last,8);
            if (f.good() && magic==LEGACY_POOL_MAGIC && m_pool_balance==0) {
                std::lock_guard<std::mutex> lk(m_pool_lock);
                m_pool_balance=pool; m_total_distributed=total; m_last_distribution_height=last;
                save_pool_state();
                MINFO("[RewardDistributor] Migrated pool state: balance=" << pool);
            }
            f.close();
            rename(pool_f.c_str(), (pool_f+".migrated").c_str());
        }
    }
    // 2. Reward records (binary append file, 32+8+8+8+4+4 = 64 bytes per record)
    std::string rew_f = m_db_path + "/mevatrust_rewards.bin";
    {
        std::ifstream f(rew_f, std::ios::binary);
        if (f.is_open()) {
            uint32_t imported=0;
            while (!f.eof()) {
                RewardRecord r{};
                f.read((char*)r.node_id.data, 32);
                f.read((char*)&r.amount,       8);
                f.read((char*)&r.block_height, 8);
                f.read((char*)&r.timestamp,    8);
                f.read((char*)&r.node_score,   4);
                f.read((char*)&r.total_score,  4);
                if (!f.good()) break;
                append_reward_record(r);
                ++imported;
            }
            f.close();
            rename(rew_f.c_str(), (rew_f+".migrated").c_str());
            MINFO("[RewardDistributor] Migrated " << imported << " reward records.");
        }
    }
}

} // namespace cryptonote





