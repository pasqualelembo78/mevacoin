// Copyright (c) 2024, The Mevacoin Project
// MevaTrustEngine Implementation

#include "mevatrust_engine.h"
#include "node_registry.h"
#include "string_tools.h"
#include "misc_log_ex.h"
#include <ctime>
#include <algorithm>
#include <fstream>
#include <sys/stat.h>

namespace cryptonote {

static std::string hk(const crypto::hash& h) {
    return epee::string_tools::pod_to_hex(h);
}

MevaTrustEngine::MevaTrustEngine(
    const std::string& db_path,
    std::shared_ptr<NodeRegistry> node_registry)
    : db_path_(db_path),
      node_registry_(node_registry),
      current_period_start_height_(0),
      last_period_processed_height_(0)
{
    scoring_weights_.uptime_weight          = 0.40f;
    scoring_weights_.sync_weight            = 0.30f;
    scoring_weights_.responsiveness_weight  = 0.20f;
    scoring_weights_.activity_weight        = 0.10f;

    parameters_.minimum_uptime_for_rewards     = 360;
    parameters_.period_length                  = 240;
    parameters_.challenges_per_period          = 3;
    parameters_.challenge_timeout_ms           = 2000;
    parameters_.minimum_score_for_rewards      = 0.5f;
    parameters_.sync_threshold_percentage      = 99;
    parameters_.reputation_penalty_per_failure  = 0.05f;
    parameters_.reputation_recovery_per_success = 0.02f;

    decay_factor_ = 0.80f;

    mkdir(db_path_.c_str(), 0755);
}

MevaTrustEngine::~MevaTrustEngine() { save_to_disk(); }

// ============================================================================
// SCORE CALCULATION
// ============================================================================

MevaTrustScoreSnapshot MevaTrustEngine::calculate_node_score(
    const crypto::hash& node_id,
    uint64_t current_height)
{
    std::lock_guard<std::mutex> lock(cache_lock_);
    std::string key = hk(node_id);

    MevaTrustScoreSnapshot score;
    score.node_id             = node_id;
    score.period_height       = current_height;
    score.period_start_height = (current_height / parameters_.period_length)
                                * parameters_.period_length;
    score.recorded_at         = static_cast<uint64_t>(std::time(nullptr));

    score.uptime_score         = calculate_uptime_score(node_id, current_height);
    score.sync_score           = calculate_sync_score(node_id, current_height, current_height);
    score.responsiveness_score = calculate_responsiveness_score(node_id);
    score.activity_score       = calculate_activity_score(node_id, current_height);

    score.total_score =
        scoring_weights_.uptime_weight         * score.uptime_score +
        scoring_weights_.sync_weight           * score.sync_score +
        scoring_weights_.responsiveness_weight * score.responsiveness_score +
        scoring_weights_.activity_weight       * score.activity_score;

    // Validator bonus: +0.1 al punteggio finale
    {
        NodeRegistryEntry entry;
        if (node_registry_ && node_registry_->get_node_by_id(node_id, entry) && entry.is_validator)
            score.total_score = std::min(1.0f, score.total_score + 0.10f);
    }

    score.challenges_passed = 0;
    score.challenges_total  = 0;
    auto it = uptime_history_.find(key);
    if (it != uptime_history_.end()) {
        for (auto& ev : it->second) {
            if (ev.response_time_ms > 0) {
                score.challenges_total++;
                if (ev.online) score.challenges_passed++;
            }
        }
    }

    score_cache_[key] = score;
    return score;
}

std::map<std::string, MevaTrustScoreSnapshot>
MevaTrustEngine::calculate_all_scores(uint64_t current_height)
{
    std::map<std::string, MevaTrustScoreSnapshot> scores;
    if (!node_registry_) return scores;
    auto nodes = node_registry_->get_active_nodes();
    for (auto& entry : nodes)
        scores[hk(entry.node_id)] = calculate_node_score(entry.node_id, current_height);
    return scores;
}

bool MevaTrustEngine::get_historical_score(
    const crypto::hash& node_id,
    uint64_t period_height,
    MevaTrustScoreSnapshot& score)
{
    std::lock_guard<std::mutex> lock(cache_lock_);

    // Se period_height == 0, ritorna il piu' recente
    if (period_height == 0) {
        auto it = score_cache_.find(hk(node_id));
        if (it == score_cache_.end()) return false;
        score = it->second;
        return true;
    }

    // Trova il periodo piu' vicino <= period_height
    auto period_it = historical_scores_.upper_bound(period_height);
    if (period_it != historical_scores_.begin())
        --period_it;
    else
        return false;

    auto node_it = period_it->second.find(hk(node_id));
    if (node_it == period_it->second.end()) return false;
    score = node_it->second;
    return true;
}

// ============================================================================
// UPTIME TRACKING
// ============================================================================

bool MevaTrustEngine::record_uptime_event(
    const crypto::hash& node_id,
    const UptimeEvent& event)
{
    std::lock_guard<std::mutex> lock(cache_lock_);
    uptime_history_[hk(node_id)].push_back(event);
    track_activity(node_id, "peer_update");
    if (node_registry_)
        node_registry_->record_uptime_event(node_id, event.online,
            event.block_height, event.peer_count, event.response_time_ms);
    return true;
}

float MevaTrustEngine::get_uptime_percentage(const crypto::hash& node_id, uint64_t) {
    return calculate_uptime_score(node_id, 0) * 100.0f;
}

float MevaTrustEngine::get_sync_percentage(
    const crypto::hash& node_id, uint64_t h, uint64_t) {
    return calculate_sync_score(node_id, h, h) * 100.0f;
}

uint64_t MevaTrustEngine::get_consecutive_uptime_seconds(const crypto::hash& node_id) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    auto it = uptime_history_.find(hk(node_id));
    if (it == uptime_history_.end()) return 0;
    uint64_t seconds = 0, last_online = 0;
    for (auto& ev : it->second) {
        if (ev.online) last_online = ev.timestamp;
        else if (last_online > 0) { seconds += ev.timestamp - last_online; last_online = 0; }
    }
    if (last_online > 0)
        seconds += static_cast<uint64_t>(std::time(nullptr)) - last_online;
    return seconds;
}

uint64_t MevaTrustEngine::get_total_uptime_seconds(const crypto::hash& node_id) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    auto it = uptime_history_.find(hk(node_id));
    if (it == uptime_history_.end()) return 0;
    uint64_t total = 0, last_online_ts = 0;
    for (auto& ev : it->second) {
        if (ev.online) {
            last_online_ts = ev.timestamp;
        } else if (last_online_ts > 0) {
            total += ev.timestamp - last_online_ts;
            last_online_ts = 0;
        }
    }
    if (last_online_ts > 0)
        total += static_cast<uint64_t>(std::time(nullptr)) - last_online_ts;
    return total;
}

// ============================================================================
// CHALLENGE & RESPONSIVENESS
// ============================================================================

bool MevaTrustEngine::record_challenge_response(
    const crypto::hash& node_id, uint32_t response_time_ms, bool success)
{
    std::lock_guard<std::mutex> lock(cache_lock_);
    UptimeEvent ev;
    ev.timestamp        = static_cast<uint64_t>(std::time(nullptr));
    ev.online           = success;
    ev.block_height     = 0;
    ev.peer_count       = 0;
    ev.response_time_ms = response_time_ms;
    uptime_history_[hk(node_id)].push_back(ev);
    track_activity(node_id, "challenge");
    if (node_registry_) node_registry_->update_node_challenge_result(node_id, success);
    return true;
}

float MevaTrustEngine::get_average_response_time(const crypto::hash& node_id) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    auto it = uptime_history_.find(hk(node_id));
    if (it == uptime_history_.end()) return 0.0f;
    float total = 0.0f; uint32_t count = 0;
    for (auto& ev : it->second)
        if (ev.response_time_ms > 0) { total += ev.response_time_ms; count++; }
    return count > 0 ? total / count : 0.0f;
}

float MevaTrustEngine::get_challenge_success_rate(const crypto::hash& node_id) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    auto it = uptime_history_.find(hk(node_id));
    if (it == uptime_history_.end()) return 0.0f;
    uint32_t passed = 0, total = 0;
    for (auto& ev : it->second)
        if (ev.response_time_ms > 0) { total++; if (ev.online) passed++; }
    return total > 0 ? static_cast<float>(passed) / total : 0.0f;
}

// ============================================================================
// ACTIVITY
// ============================================================================

bool MevaTrustEngine::increment_activity_counter(
    const crypto::hash& node_id, const std::string& activity_type)
{
    std::lock_guard<std::mutex> lock(cache_lock_);
    ActivityEntry ev;
    ev.type         = activity_type;
    ev.timestamp    = static_cast<uint64_t>(std::time(nullptr));
    ev.block_height = 0;
    ev.count        = 1;
    activity_history_[hk(node_id)].push_back(ev);
    return true;
}

float MevaTrustEngine::get_activity_score(const crypto::hash& node_id, uint64_t) {
    return calculate_activity_score(node_id, 0);
}

// ============================================================================
// PERIOD MANAGEMENT
// ============================================================================

bool MevaTrustEngine::process_reward_period(uint64_t current_height) {
    if (current_height - last_period_processed_height_ < parameters_.period_length)
        return false;

    uint64_t period_id = current_height / parameters_.period_length;

    // Calcola tutti gli score correnti
    auto scores = calculate_all_scores(current_height);

    // Applica decay: ogni periodo gli score pregressi contano meno
    apply_decay(current_height);

    // Salva storico per questo periodo
    {
        std::lock_guard<std::mutex> lock(cache_lock_);
        historical_scores_[period_id] = scores;
        // Pulisce periodi vecchi (> 90 giorni)
        uint64_t oldest = current_height > 64800 ? current_height - 64800 : 0;
        auto it = historical_scores_.begin();
        while (it != historical_scores_.end() && it->first * parameters_.period_length < oldest)
            it = historical_scores_.erase(it);
    }

    last_period_processed_height_ = current_height;
    current_period_start_height_  = current_height;
    return true;
}

void MevaTrustEngine::apply_decay(uint64_t current_height) {
    uint64_t period_id = current_height / parameters_.period_length;
    if (period_id == 0) return;

    // Se esiste uno score del periodo precedente, applica decay
    auto prev_it = historical_scores_.find(period_id - 1);
    if (prev_it == historical_scores_.end()) return;

    std::lock_guard<std::mutex> lock(cache_lock_);
    for (auto& [key, prev_score] : prev_it->second) {
        auto current_it = score_cache_.find(key);
        if (current_it != score_cache_.end()) {
            // Media mobile esponenziale: new = decay * old + (1-decay) * current
            auto& cur = current_it->second;
            cur.uptime_score         = decay_factor_ * prev_score.uptime_score         + (1.0f - decay_factor_) * cur.uptime_score;
            cur.sync_score           = decay_factor_ * prev_score.sync_score           + (1.0f - decay_factor_) * cur.sync_score;
            cur.responsiveness_score = decay_factor_ * prev_score.responsiveness_score + (1.0f - decay_factor_) * cur.responsiveness_score;
            cur.activity_score       = decay_factor_ * prev_score.activity_score       + (1.0f - decay_factor_) * cur.activity_score;
            cur.total_score =
                scoring_weights_.uptime_weight         * cur.uptime_score +
                scoring_weights_.sync_weight           * cur.sync_score +
                scoring_weights_.responsiveness_weight * cur.responsiveness_score +
                scoring_weights_.activity_weight       * cur.activity_score;
        }
    }
}

void MevaTrustEngine::track_activity(const crypto::hash& node_id, const std::string& type) {
    increment_activity_counter(node_id, type);
}

uint64_t MevaTrustEngine::get_current_period_start() const { return current_period_start_height_; }
uint64_t MevaTrustEngine::get_current_period_end() const { return current_period_start_height_ + parameters_.period_length; }
uint64_t MevaTrustEngine::get_blocks_since_last_period(uint64_t h) const { return h - last_period_processed_height_; }

// ============================================================================
// AGGREGATES
// ============================================================================

MevaTrustEngine::NetworkStats
MevaTrustEngine::get_network_statistics(uint64_t current_height) {
    NetworkStats stats{};
    if (!node_registry_) return stats;
    auto nodes = node_registry_->get_active_nodes();
    stats.total_active_nodes = static_cast<uint32_t>(nodes.size());
    float us = 0, ss = 0, sc = 0; uint32_t eligible = 0;
    for (auto& e : nodes) {
        auto score = calculate_node_score(e.node_id, current_height);
        us += score.uptime_score; ss += score.sync_score; sc += score.total_score;
        if (score.total_score >= parameters_.minimum_score_for_rewards) eligible++;
    }
    uint32_t n = stats.total_active_nodes;
    if (n > 0) { stats.average_uptime = us/n*100; stats.average_sync_percentage = ss/n*100; stats.average_score = sc/n; }
    stats.total_eligible_nodes = eligible;
    stats.reward_pool_percentage = 0.03f;
    return stats;
}

MevaTrustEngine::NodeStats
MevaTrustEngine::get_node_statistics(const crypto::hash& node_id, uint64_t current_height) {
    NodeStats stats{};
    stats.node_id = node_id;
    auto score = calculate_node_score(node_id, current_height);
    stats.current_score     = score.total_score;
    stats.uptime_percentage = score.uptime_score * 100.0f;
    stats.sync_percentage   = score.sync_score   * 100.0f;
    stats.responsiveness    = score.responsiveness_score;
    stats.activity          = score.activity_score;
    stats.uptime_hours      = get_total_uptime_seconds(node_id) / 3600;
    stats.challenges_passed = score.challenges_passed;
    stats.challenges_total  = score.challenges_total;
    NodeRegistryEntry entry;
    if (node_registry_ && node_registry_->get_node_by_id(node_id, entry))
        stats.wallet_address = entry.wallet_address;
    return stats;
}

std::vector<MevaTrustEngine::NodeStats>
MevaTrustEngine::get_top_nodes(uint32_t count, uint64_t current_height) {
    std::vector<NodeStats> result;
    if (!node_registry_) return result;
    auto nodes = node_registry_->get_active_nodes();
    for (auto& e : nodes) result.push_back(get_node_statistics(e.node_id, current_height));
    std::sort(result.begin(), result.end(),
        [](const NodeStats& a, const NodeStats& b){ return a.current_score > b.current_score; });
    if (result.size() > count) result.resize(count);
    return result;
}

// ============================================================================
// WEIGHTS & PARAMS
// ============================================================================

void MevaTrustEngine::set_scoring_weights(const ScoringWeights& w) {
    std::lock_guard<std::mutex> lock(cache_lock_); scoring_weights_ = w;
}
MevaTrustEngine::ScoringWeights MevaTrustEngine::get_scoring_weights() const {
    std::lock_guard<std::mutex> lock(cache_lock_); return scoring_weights_;
}
void MevaTrustEngine::set_parameters(const MevaTrustParameters& p) {
    std::lock_guard<std::mutex> lock(cache_lock_); parameters_ = p;
}
MevaTrustEngine::MevaTrustParameters MevaTrustEngine::get_parameters() const {
    std::lock_guard<std::mutex> lock(cache_lock_); return parameters_;
}

// ============================================================================
// PERSISTENCE & PRUNING
// ============================================================================

static void write_u64(std::ofstream& f, uint64_t v) { f.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
static void write_u32(std::ofstream& f, uint32_t v) { f.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
static void write_u8(std::ofstream& f, uint8_t v)   { f.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
static void write_float(std::ofstream& f, float v)   { f.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
static void write_bool(std::ofstream& f, bool v)     { uint8_t b = v ? 1 : 0; f.write(reinterpret_cast<const char*>(&b), sizeof(b)); }
static void write_str(std::ofstream& f, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    write_u32(f, len);
    f.write(s.data(), len);
}

static uint64_t read_u64(std::ifstream& f) { uint64_t v{}; f.read(reinterpret_cast<char*>(&v), sizeof(v)); return v; }
static uint32_t read_u32(std::ifstream& f) { uint32_t v{}; f.read(reinterpret_cast<char*>(&v), sizeof(v)); return v; }
static uint8_t  read_u8(std::ifstream& f)  { uint8_t v{};  f.read(reinterpret_cast<char*>(&v), sizeof(v));  return v; }
static float    read_float(std::ifstream& f) { float v{}; f.read(reinterpret_cast<char*>(&v), sizeof(v)); return v; }
static bool     read_bool(std::ifstream& f)  { uint8_t b{}; f.read(reinterpret_cast<char*>(&b), sizeof(b)); return b != 0; }
static std::string read_str(std::ifstream& f) {
    uint32_t len = read_u32(f);
    std::string s(len, '\0');
    f.read(s.data(), len);
    return s;
}

bool MevaTrustEngine::save_to_disk() const {
    std::lock_guard<std::mutex> lock(cache_lock_);
    try {
        // Save uptime_history_
        {
            std::string path = db_path_ + "/uptime.dat";
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) { MERROR("save_to_disk: cannot open " << path); return false; }
            write_u32(f, static_cast<uint32_t>(uptime_history_.size()));
            for (const auto& [key, events] : uptime_history_) {
                write_u8(f, static_cast<uint8_t>(key.size()));
                f.write(key.data(), key.size());
                write_u32(f, static_cast<uint32_t>(events.size()));
                for (const auto& ev : events) {
                    write_u64(f, ev.timestamp);
                    write_bool(f, ev.online);
                    write_u64(f, ev.block_height);
                    write_u32(f, ev.peer_count);
                    write_u32(f, ev.response_time_ms);
                    write_str(f, ev.ip_address);
                }
            }
        }
        // Save score_cache_
        {
            std::string path = db_path_ + "/scores.dat";
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) { MERROR("save_to_disk: cannot open " << path); return false; }
            write_u32(f, static_cast<uint32_t>(score_cache_.size()));
            for (const auto& [key, score] : score_cache_) {
                write_u8(f, static_cast<uint8_t>(key.size()));
                f.write(key.data(), key.size());
                write_u64(f, score.period_height);
                write_u64(f, score.period_start_height);
                write_float(f, score.uptime_score);
                write_float(f, score.sync_score);
                write_float(f, score.responsiveness_score);
                write_float(f, score.activity_score);
                write_float(f, score.total_score);
                write_u32(f, score.challenges_passed);
                write_u32(f, score.challenges_total);
                write_u64(f, score.recorded_at);
            }
        }
        // Save activity_history_
        {
            std::string path = db_path_ + "/activity.dat";
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) { MERROR("save_to_disk: cannot open " << path); return false; }
            write_u32(f, static_cast<uint32_t>(activity_history_.size()));
            for (const auto& [key, entries] : activity_history_) {
                write_u8(f, static_cast<uint8_t>(key.size()));
                f.write(key.data(), key.size());
                write_u32(f, static_cast<uint32_t>(entries.size()));
                for (const auto& ev : entries) {
                    write_str(f, ev.type);
                    write_u64(f, ev.timestamp);
                    write_u64(f, ev.block_height);
                    write_u32(f, ev.count);
                }
            }
        }
        // Save historical_scores_
        {
            std::string path = db_path_ + "/historical_scores.dat";
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) { MERROR("save_to_disk: cannot open " << path); return false; }
            write_u32(f, static_cast<uint32_t>(historical_scores_.size()));
            for (const auto& [period_id, period_scores] : historical_scores_) {
                write_u64(f, period_id);
                write_u32(f, static_cast<uint32_t>(period_scores.size()));
                for (const auto& [key, score] : period_scores) {
                    write_u8(f, static_cast<uint8_t>(key.size()));
                    f.write(key.data(), key.size());
                    write_u64(f, score.period_height);
                    write_u64(f, score.period_start_height);
                    write_float(f, score.uptime_score);
                    write_float(f, score.sync_score);
                    write_float(f, score.responsiveness_score);
                    write_float(f, score.activity_score);
                    write_float(f, score.total_score);
                    write_u32(f, score.challenges_passed);
                    write_u32(f, score.challenges_total);
                    write_u64(f, score.recorded_at);
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        MERROR("save_to_disk failed: " << e.what());
        return false;
    }
}

bool MevaTrustEngine::load_from_disk() {
    std::lock_guard<std::mutex> lock(cache_lock_);
    try {
        // Load uptime_history_
        {
            std::string path = db_path_ + "/uptime.dat";
            std::ifstream f(path, std::ios::binary);
            if (!f) { MDEBUG("load_from_disk: no uptime.dat, clean start"); }
            else {
                uptime_history_.clear();
                uint32_t count = read_u32(f);
                for (uint32_t i = 0; i < count; ++i) {
                    uint8_t klen = read_u8(f);
                    std::string key(klen, '\0');
                    f.read(key.data(), klen);
                    uint32_t evcount = read_u32(f);
                    std::vector<UptimeEvent> events(evcount);
                    for (uint32_t j = 0; j < evcount; ++j) {
                        UptimeEvent ev;
                        ev.timestamp        = read_u64(f);
                        ev.online           = read_bool(f);
                        ev.block_height     = read_u64(f);
                        ev.peer_count       = read_u32(f);
                        ev.response_time_ms = read_u32(f);
                        ev.ip_address       = read_str(f);
                        events[j] = ev;
                    }
                    uptime_history_[key] = std::move(events);
                }
            }
        }
        // Load score_cache_
        {
            std::string path = db_path_ + "/scores.dat";
            std::ifstream f(path, std::ios::binary);
            if (!f) { MDEBUG("load_from_disk: no scores.dat, clean start"); }
            else {
                score_cache_.clear();
                uint32_t count = read_u32(f);
                for (uint32_t i = 0; i < count; ++i) {
                    uint8_t klen = read_u8(f);
                    std::string key(klen, '\0');
                    f.read(key.data(), klen);
                    MevaTrustScoreSnapshot score;
                    score.period_height        = read_u64(f);
                    score.period_start_height  = read_u64(f);
                    score.uptime_score         = read_float(f);
                    score.sync_score           = read_float(f);
                    score.responsiveness_score = read_float(f);
                    score.activity_score       = read_float(f);
                    score.total_score          = read_float(f);
                    score.challenges_passed    = read_u32(f);
                    score.challenges_total     = read_u32(f);
                    score.recorded_at          = read_u64(f);
                    score_cache_[key] = score;
                }
            }
        }
        // Load activity_history_
        {
            std::string path = db_path_ + "/activity.dat";
            std::ifstream f(path, std::ios::binary);
            if (!f) { MDEBUG("load_from_disk: no activity.dat, clean start"); }
            else {
                activity_history_.clear();
                uint32_t count = read_u32(f);
                for (uint32_t i = 0; i < count; ++i) {
                    uint8_t klen = read_u8(f);
                    std::string key(klen, '\0');
                    f.read(key.data(), klen);
                    uint32_t evcount = read_u32(f);
                    std::vector<ActivityEntry> entries(evcount);
                    for (uint32_t j = 0; j < evcount; ++j) {
                        ActivityEntry ev;
                        ev.type         = read_str(f);
                        ev.timestamp    = read_u64(f);
                        ev.block_height = read_u64(f);
                        ev.count        = read_u32(f);
                        entries[j] = ev;
                    }
                    activity_history_[key] = std::move(entries);
                }
            }
        }
        // Load historical_scores_
        {
            std::string path = db_path_ + "/historical_scores.dat";
            std::ifstream f(path, std::ios::binary);
            if (!f) { MDEBUG("load_from_disk: no historical_scores.dat, clean start"); }
            else {
                historical_scores_.clear();
                uint32_t num_periods = read_u32(f);
                for (uint32_t p = 0; p < num_periods; ++p) {
                    uint64_t period_id = read_u64(f);
                    uint32_t num_scores = read_u32(f);
                    std::map<std::string, MevaTrustScoreSnapshot> period_scores;
                    for (uint32_t s = 0; s < num_scores; ++s) {
                        uint8_t klen = read_u8(f);
                        std::string key(klen, '\0');
                        f.read(key.data(), klen);
                        MevaTrustScoreSnapshot score;
                        score.period_height        = read_u64(f);
                        score.period_start_height  = read_u64(f);
                        score.uptime_score         = read_float(f);
                        score.sync_score           = read_float(f);
                        score.responsiveness_score = read_float(f);
                        score.activity_score       = read_float(f);
                        score.total_score          = read_float(f);
                        score.challenges_passed    = read_u32(f);
                        score.challenges_total     = read_u32(f);
                        score.recorded_at          = read_u64(f);
                        period_scores[key] = score;
                    }
                    historical_scores_[period_id] = std::move(period_scores);
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        MERROR("load_from_disk failed: " << e.what());
        return false;
    }
}

bool MevaTrustEngine::sync_database()       { return save_to_disk(); }
bool MevaTrustEngine::open_database()       { return true; }
bool MevaTrustEngine::close_database()      { return save_to_disk(); }
bool MevaTrustEngine::load_uptime_history(const crypto::hash&) { return true; }
bool MevaTrustEngine::save_uptime_history(const crypto::hash&) { return true; }

bool MevaTrustEngine::prune_old_data(uint64_t keep_before_height) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    const uint64_t max_age_secs = 90 * 86400; // 90 days

    // Prune uptime events
    for (auto& kv : uptime_history_) {
        auto& ev = kv.second;
        ev.erase(std::remove_if(ev.begin(), ev.end(),
            [keep_before_height, now, max_age_secs](const UptimeEvent& e) {
                if (e.block_height > 0)
                    return e.block_height < keep_before_height;
                return (now > e.timestamp) && (now - e.timestamp) > max_age_secs;
            }), ev.end());
    }

    // Prune activity history
    for (auto& kv : activity_history_) {
        auto& ev = kv.second;
        ev.erase(std::remove_if(ev.begin(), ev.end(),
            [keep_before_height, now, max_age_secs](const ActivityEntry& e) {
                if (e.block_height > 0)
                    return e.block_height < keep_before_height;
                return (now > e.timestamp) && (now - e.timestamp) > max_age_secs;
            }), ev.end());
    }

    // Prune historical scores (> 90 days old)
    uint64_t oldest_period = keep_before_height / parameters_.period_length;
    auto it = historical_scores_.begin();
    while (it != historical_scores_.end() && it->first < oldest_period)
        it = historical_scores_.erase(it);

    return true;
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

float MevaTrustEngine::calculate_uptime_score(const crypto::hash& node_id, uint64_t period_end_height) {
    auto it = uptime_history_.find(hk(node_id));
    if (it == uptime_history_.end()) return 0.0f;

    const uint64_t lookback_blocks = 14400;
    const uint64_t min_height = period_end_height > lookback_blocks
                                ? period_end_height - lookback_blocks : 0;

    // Durata totale online nel periodo divisa per durata totale del periodo
    uint64_t online_secs = 0;
    uint64_t prev_ts = 0;
    bool was_online = false;
    uint64_t window_start_ts = 0;
    uint64_t window_end_ts = static_cast<uint64_t>(std::time(nullptr));

    // Trova il timestamp di inizio finestra dal primo evento nella finestra
    for (auto& ev : it->second) {
        if (ev.block_height >= min_height || ev.block_height == 0) {
            if (window_start_ts == 0 || ev.timestamp < window_start_ts)
                window_start_ts = ev.timestamp;
        }
    }

    // Calcola secondi online nella finestra
    for (auto& ev : it->second) {
        if (ev.block_height > 0 && ev.block_height < min_height)
            continue; // fuori finestra
        if (was_online && prev_ts > 0)
            online_secs += ev.timestamp - prev_ts;
        was_online = ev.online;
        prev_ts = ev.timestamp;
    }

    // Se ancora online, aggiungi fino ad ora
    if (was_online && prev_ts > 0)
        online_secs += window_end_ts - prev_ts;

    uint64_t window_duration = window_end_ts - window_start_ts;
    if (window_duration == 0) return 0.0f;
    return std::min(1.0f, static_cast<float>(online_secs) / static_cast<float>(window_duration));
}

float MevaTrustEngine::calculate_sync_score(
    const crypto::hash& node_id, uint64_t period_end_height, uint64_t current_chain_height) {
    if (!node_registry_) return 0.0f;
    NodeRegistryEntry entry;
    if (!node_registry_->get_node_by_id(node_id, entry)) return 0.0f;
    if (entry.is_synchronized) return 1.0f;
    if (current_chain_height == 0) return 0.5f;
    uint64_t gap = current_chain_height > entry.last_sync_height
                   ? current_chain_height - entry.last_sync_height : 0;
    // gap di 1000 blocchi -> score 0, lineare fra 0 e 1000
    const uint64_t max_gap = 1000;
    float ratio = std::min(1.0f, static_cast<float>(gap) / static_cast<float>(max_gap));
    float score = 1.0f - ratio * 0.5f; // minimo 0.5
    return std::max(0.5f, score);
}

float MevaTrustEngine::calculate_responsiveness_score(const crypto::hash& node_id) {
    float rate  = get_challenge_success_rate(node_id);
    float avg   = get_average_response_time(node_id);
    float rt_sc = avg > 0 ? std::max(0.0f, 1.0f - avg / 2000.0f) : 1.0f;
    return (rate + rt_sc) / 2.0f;
}

float MevaTrustEngine::calculate_activity_score(const crypto::hash& node_id, uint64_t period_end_height) {
    auto it = activity_history_.find(hk(node_id));
    if (it == activity_history_.end()) return 0.0f;

    const uint64_t lookback_blocks = 14400;
    const uint64_t min_height = period_end_height > lookback_blocks
                                ? period_end_height - lookback_blocks : 0;

    float score = 0.0f;
    for (auto& ev : it->second) {
        if (ev.block_height > 0 && ev.block_height < min_height)
            continue;
        if (ev.type == "block_relay")  score += 0.30f;
        else if (ev.type == "tx_relay") score += 0.10f;
        else if (ev.type == "peer_update") score += 0.20f;
        else if (ev.type == "challenge") score += 0.15f;
    }
    return std::min(1.0f, score);
}

} // namespace cryptonote


