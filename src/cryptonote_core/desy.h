// Copyright (c) 2024, The MevaCoin Project
// All rights reserved.
//
// DESY Memorial System — hash-based deterministic event selection

#pragma once

#include <ctime>
#include <cstdint>
#include <string>
#include "cryptonote_basic/cryptonote_basic.h"
#include "crypto/hash.h"
#include "foundation_vesting.h"

namespace cryptonote
{

// ── Constants ──────────────────────────────────────────────────────
constexpr int DESY_MEMORIAL_MONTH   = 12;   // December
constexpr int DESY_MEMORIAL_DAY     = 18;   // Day 18
constexpr int DESY_BONUS_PERCENT    = 10;   // +10% reward bonus
constexpr int DESY_BONUS_DIVISOR    = 100 / DESY_BONUS_PERCENT; // 10
constexpr int DESY_VISUAL_EVENTS_MAX = 5;   // max visual events per day

// Legacy total — sum of all premine allocations (now defined in foundation_vesting.h)
constexpr uint64_t FOUNDATION_ALLOCATION = PREMINE_TOTAL;

// Team lock address (the original foundation address)
constexpr const char* FOUNDATION_ADDRESS = "MDZy9bSnW2UVhjZL7w3NBUNX3fYf5HeTFZ1Po2M8cvk8ifE5E7Bb8NH3koFByJWDdKB5jkm3ZDUpQLbpJBzrsfEWDypTjoH";

// ── Functions ──────────────────────────────────────────────────────

// Returns true if the given timestamp falls on DESY Memorial Day (Dec 18)
inline bool is_desy_memorial_day(time_t ts)
{
    struct tm tm;
    gmtime_r(&ts, &tm);
    return tm.tm_mon + 1 == DESY_MEMORIAL_MONTH && tm.tm_mday == DESY_MEMORIAL_DAY;
}

// Deterministic block selection for +10% reward bonus.
// Uses block hash to select ~10% of blocks on DESY Memorial Day.
// Returns true if this block qualifies for the bonus.
inline bool desy_qualifies_for_bonus(const crypto::hash& block_hash)
{
    // Interpret first 8 bytes of hash as uint64_t
    uint64_t h;
    memcpy(&h, block_hash.data, sizeof(h));
    // Scale to 0..99 range, select if < 10 (i.e., ~10% of blocks)
    return (h % 100) < DESY_BONUS_PERCENT;
}

// Compute DESY visual event rank for a block.
// Returns a value 0..99; the top DESY_VISUAL_EVENTS_MAX blocks
// (per day) are considered visual event blocks.
inline unsigned int desy_visual_rank(const crypto::hash& block_hash)
{
    uint64_t h;
    memcpy(&h, block_hash.data, sizeof(h));
    return static_cast<unsigned int>(h % 100);
}

} // namespace cryptonote
