// Copyright (c) 2024, The Mevacoin Project
// SPDX-License-Identifier: BSD-3-Clause
//
// participation_snapshot_antireplay.h — Anti-replay LMDB-backed
// DROP-IN: src/cryptonote_core/participation/participation_snapshot_antireplay.h
//
// LMDB table: PART_ANTIREPLAY
//   key  = period[4] || height[8]  (12 bytes, ordine little-endian del processo)
//   value= height[8]               (uint64_t per riferimento)
//
// Protezione multi-TU: Meyers singleton (C++11 thread-safe).
// Thread-safety: mutex interno protegge sia cache che transazioni LMDB.

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <string>
#include <cstring>
#include "mevatrust_lmdb.h"  // MevaTrustLMDB + DB_MT_ANTIREPLAY

namespace cryptonote { namespace mevatrust { namespace snapshot_antireplay {

namespace detail {

class AntiReplayDB {
public:
    static AntiReplayDB& instance() {
        static AntiReplayDB inst;
        return inst;
    }

    bool load_from_db(const std::string& base_dir);
    bool is_replay(uint64_t height, uint32_t period);
    void record_snapshot(uint64_t height, uint32_t period);
    bool save_to_db(const std::string& base_dir);
    void shutdown(const std::string& base_dir);

private:
    AntiReplayDB() = default;
    ~AntiReplayDB() = default;
    AntiReplayDB(const AntiReplayDB&) = delete;
    AntiReplayDB& operator=(const AntiReplayDB&) = delete;

    static std::string make_key(uint64_t h, uint32_t p);

    std::mutex                          m_lock;
    std::unordered_map<std::string, uint64_t> m_seen;
    MDB_env* m_env      = nullptr;
    MDB_dbi  m_dbi      = 0;
    bool     m_db_open  = false;
};

// ── Key helper ──────────────────────────────────────────────────────────────
inline std::string AntiReplayDB::make_key(uint64_t h, uint32_t p) {
    std::string k(12, '\0');
    memcpy(&k[0], &p, 4);
    memcpy(&k[4], &h, 8);
    return k;
}

// ── load_from_db ────────────────────────────────────────────────────────────
inline bool AntiReplayDB::load_from_db(const std::string& base_dir) {
    std::lock_guard<std::mutex> lk(m_lock);
    if (m_db_open) return true;

    try {
        m_env = MevaTrustLMDB::open(base_dir);
        {
            MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
            m_dbi = MevaTrustLMDB::open_dbi(txn, DB_MT_ANTIREPLAY);
            mdb_txn_commit(txn);
        }
        m_db_open = true;

        MDB_txn* rtxn = MevaTrustLMDB::begin_read(m_env);
        MDB_cursor* cur = nullptr;
        if (mdb_cursor_open(rtxn, m_dbi, &cur) == 0) {
            MDB_val k, v;
            while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
                if (k.mv_size == 12) {
                    std::string key(static_cast<const char*>(k.mv_data), 12);
                    uint64_t height = 0;
                    if (v.mv_size >= 8) memcpy(&height, v.mv_data, 8);
                    m_seen[key] = height;
                }
            }
            mdb_cursor_close(cur);
        }
        mdb_txn_abort(rtxn);
        return true;
    } catch (...) {
        m_db_open = false;
        return false;
    }
}

// ── is_replay ───────────────────────────────────────────────────────────────
inline bool AntiReplayDB::is_replay(uint64_t height, uint32_t period) {
    std::lock_guard<std::mutex> lk(m_lock);
    return m_seen.count(make_key(height, period)) > 0;
}

// ── record_snapshot ─────────────────────────────────────────────────────────
inline void AntiReplayDB::record_snapshot(uint64_t height, uint32_t period) {
    std::lock_guard<std::mutex> lk(m_lock);
    std::string key = make_key(height, period);

    if (m_seen.count(key)) return;

    // Pruning: mantieni almeno gli ultimi 2000 periodi (~480k blocchi)
    // NON rimuove mai entry con altezza < 10000 blocchi dalla current.
    if (m_seen.size() >= 100000) {
        auto it = m_seen.begin();
        uint64_t removed = 0;
        uint64_t const safety_margin = (height > 10000) ? height - 10000 : 0;
        while (it != m_seen.end() && removed < 200 && m_seen.size() - removed > 50000) {
            uint64_t entry_height = 0;
            memcpy(&entry_height, it->first.data() + 4, 8);
            if (entry_height >= safety_margin)
                break;
            if (m_db_open && m_env) {
                try {
                    MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
                    MDB_val dk{ it->first.size(), const_cast<char*>(it->first.data()) };
                    mdb_del(txn, m_dbi, &dk, nullptr);
                    mdb_txn_commit(txn);
                } catch (...) {}
            }
            it = m_seen.erase(it);
            ++removed;
        }
    }

    m_seen[key] = height;

    if (m_db_open && m_env) {
        try {
            MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
            MDB_val mk{ key.size(), const_cast<char*>(key.data()) };
            MDB_val mv{ sizeof(height), &height };
            int rc = mdb_put(txn, m_dbi, &mk, &mv, 0);
            if (rc == 0) mdb_txn_commit(txn);
            else         mdb_txn_abort(txn);
        } catch (...) {}
    }
}

inline bool AntiReplayDB::save_to_db(const std::string& /*base_dir*/) {
    std::lock_guard<std::mutex> lk(m_lock);
    if (!m_db_open || !m_env) return false;
    try {
        MDB_txn* txn = MevaTrustLMDB::begin_write(m_env);
        mdb_drop(txn, m_dbi, 0);
        for (const auto& kv : m_seen) {
            MDB_val mk{ kv.first.size(), const_cast<char*>(kv.first.data()) };
            MDB_val mv{ sizeof(uint64_t), const_cast<uint64_t*>(&kv.second) };
            if (mdb_put(txn, m_dbi, &mk, &mv, 0) != 0) {
                mdb_txn_abort(txn);
                return false;
            }
        }
        mdb_txn_commit(txn);
        return true;
    } catch (...) { return false; }
}

inline void AntiReplayDB::shutdown(const std::string& base_dir) {
    std::lock_guard<std::mutex> lk(m_lock);
    if (!m_db_open) return;
    save_to_db(base_dir);
    MevaTrustLMDB::release(base_dir);
    m_env     = nullptr;
    m_dbi     = 0;
    m_db_open = false;
    m_seen.clear();
}

} // namespace detail

// ── Public API (delega al singleton) ─────────────────────────────────────────
inline bool load_from_db(const std::string& base_dir) {
    return detail::AntiReplayDB::instance().load_from_db(base_dir);
}
inline bool is_replay(uint64_t height, uint32_t period) {
    return detail::AntiReplayDB::instance().is_replay(height, period);
}
inline void record_snapshot(uint64_t height, uint32_t period) {
    detail::AntiReplayDB::instance().record_snapshot(height, period);
}
inline bool save_to_db(const std::string& base_dir) {
    return detail::AntiReplayDB::instance().save_to_db(base_dir);
}
inline void shutdown(const std::string& base_dir) {
    detail::AntiReplayDB::instance().shutdown(base_dir);
}

}}} // namespace cryptonote::mevatrust::snapshot_antireplay



