// Copyright (c) 2024, The Mevacoin Project
// SPDX-License-Identifier: BSD-3-Clause
//
// participation_lmdb.h — Shared LMDB environment for the Participation subsystem.
// DROP-IN: src/cryptonote_core/participation/participation_lmdb.h
//
// Design: ogni componente (NodeRegistry, RewardDistributor, BadgeSystem) condivide
// lo STESSO environment LMDB aperto in {data_dir}/participation/lmdb/.
// L'env e' reference-counted: l'ultimo a chiuderlo chiama mdb_env_close().
//
// Tabelle (named DBs) usate:
//   PART_NODES    — NodeRegistry       key=node_id[32]         value=packed entry
//   PART_POOL     — RewardDistributor  key="pool"[4]           value=PoolState
//   PART_REWARDS  — RewardDistributor  key=node_id[32]         value=RewardRecord (DUPSORT)
//   PART_BADGES   — BadgeSystem        key=node_id[32]         value=packed badges
//   PART_ANTIREPLAY — SnapshotAntireplay key=period[4]||h[8]   value=height[8]

#pragma once
#include <string>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <cstring>
#include <sys/stat.h>
extern "C" {
#include "lmdb.h"
}

namespace cryptonote { namespace mevatrust {

// ── Serialization helpers ────────────────────────────────────────────────────

inline void lmdb_write_str(std::string& buf, const std::string& s) {
    uint16_t l = static_cast<uint16_t>(s.size());
    buf.append(reinterpret_cast<const char*>(&l), 2);
    if (l) buf.append(s.data(), l);
}
inline bool lmdb_read_str(const char*& p, const char* end, std::string& s) {
    if (p + 2 > end) return false;
    uint16_t l; memcpy(&l, p, 2); p += 2;
    if (p + l > end) return false;
    s.assign(p, l); p += l; return true;
}
template<typename T> inline void lmdb_write_pod(std::string& buf, const T& v) {
    buf.append(reinterpret_cast<const char*>(&v), sizeof(T));
}
template<typename T> inline bool lmdb_read_pod(const char*& p, const char* end, T& v) {
    if (p + (ptrdiff_t)sizeof(T) > end) return false;
    memcpy(&v, p, sizeof(T)); p += sizeof(T); return true;
}

// ── Environment names (one per table) ────────────────────────────────────────
static constexpr const char* DB_MT_NODES      = "MT_NODES";
static constexpr const char* DB_MT_POOL       = "MT_POOL";
static constexpr const char* DB_MT_REWARDS    = "MT_REWARDS";
static constexpr const char* DB_MT_BADGES     = "MT_BADGES";
static constexpr const char* DB_MT_ANTIREPLAY = "MT_ANTIREPLAY";
static constexpr const char* DB_MT_WELCOME    = "MT_WELCOME";
static constexpr const char* DB_MT_CIRCLES   = "MT_CIRCLES";
static constexpr const char* DB_MT_PENALTIES = "MT_PENALTIES";
static constexpr const char* DB_MT_STORES    = "MT_STORES";
static constexpr const char* DB_MT_ITEMS     = "MT_ITEMS";
static constexpr const char* DB_MT_PURCHASES = "MT_PURCHASES";
static constexpr const char* DB_MT_CIRCLE_PROPOSALS = "MT_CIRCLE_PROPOSALS";
static constexpr const char* DB_MT_CIRCLE_VOTES     = "MT_CIRCLE_VOTES";

// ── MevaTrustLMDB — singleton env per base_dir ───────────────────────────
class MevaTrustLMDB {
public:
    /// Open (or reuse) the env at {base_dir}/lmdb/.
    static MDB_env* open(const std::string& base_dir,
                         unsigned int maxdbs   = 12,
                         size_t mapsize        = 512ULL << 20) // 512 MiB
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        std::string p = base_dir + "/lmdb";
        mkdir(base_dir.c_str(), 0755);
        mkdir(p.c_str(),        0755);
        auto it = s_envs.find(p);
        if (it != s_envs.end()) { ++it->second.ref; return it->second.env; }
        MDB_env* env = nullptr; int rc;
        if ((rc = mdb_env_create(&env)) != 0) throw std::runtime_error(mdb_strerror(rc));
        if ((rc = mdb_env_set_maxdbs(env, maxdbs)) != 0) throw std::runtime_error(mdb_strerror(rc));
        if ((rc = mdb_env_set_mapsize(env, mapsize)) != 0) throw std::runtime_error(mdb_strerror(rc));
        if ((rc = mdb_env_open(env, p.c_str(), MDB_NOTLS, 0664)) != 0)
            throw std::runtime_error(std::string("mdb_env_open ") + p + ": " + mdb_strerror(rc));
        s_envs[p] = {env, 1}; return env;
    }

    /// Release reference; close env when refcount reaches zero.
    static void release(const std::string& base_dir) {
        std::lock_guard<std::mutex> lk(s_mtx);
        std::string p = base_dir + "/lmdb";
        auto it = s_envs.find(p);
        if (it == s_envs.end()) return;
        if (--it->second.ref == 0) { mdb_env_close(it->second.env); s_envs.erase(it); }
    }

    /// Open a named DBI (flags: MDB_CREATE, MDB_DUPSORT, MDB_DUPFIXED, etc.).
    static MDB_dbi open_dbi(MDB_txn* txn, const char* name, unsigned int flags = MDB_CREATE) {
        MDB_dbi dbi; int rc = mdb_dbi_open(txn, name, flags, &dbi);
        if (rc != 0) throw std::runtime_error(std::string("mdb_dbi_open ") + name + ": " + mdb_strerror(rc));
        return dbi;
    }

    static MDB_txn* begin_write(MDB_env* e) {
        MDB_txn* t; int rc = mdb_txn_begin(e, nullptr, 0, &t);
        if (rc != 0) throw std::runtime_error(std::string("mdb_txn_begin(W): ") + mdb_strerror(rc));
        return t;
    }
    static MDB_txn* begin_read(MDB_env* e) {
        MDB_txn* t; int rc = mdb_txn_begin(e, nullptr, MDB_RDONLY, &t);
        if (rc != 0) throw std::runtime_error(std::string("mdb_txn_begin(R): ") + mdb_strerror(rc));
        return t;
    }
private:
    struct Slot { MDB_env* env; int ref; };
    static std::mutex s_mtx;
    static std::unordered_map<std::string, Slot> s_envs;
};

inline std::mutex MevaTrustLMDB::s_mtx;
inline std::unordered_map<std::string, MevaTrustLMDB::Slot> MevaTrustLMDB::s_envs;

}} // namespace

