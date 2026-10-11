// Copyright (c) 2024, The Mevacoin Project
// MevaTrustEngine Implementation

#include "mevatrust_engine.h"
#include "mevatrust_consensus.h"
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
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
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
    // get_eligible_nodes(), non get_active_nodes(): la differenza e' un nodo che
    // si e' registrato via RPC e non esiste sulla catena. Pagarlo qui significa
    // che questo nodo mette nel blocco una lista di quote che nessun altro
    // ricostruisce, e il blocco viene respinto dalla rete. Il posto dove si
    // sceglie chi viene pagato e' l'unico che deve chiedere se il nodo e' on-chain.
    auto nodes = node_registry_->get_eligible_nodes();
    for (auto& entry : nodes)
        scores[hk(entry.node_id)] = calculate_node_score(entry.node_id, current_height);
    return scores;
}

// ============================================================================
// UPTIME TRACKING
// ============================================================================

bool MevaTrustEngine::record_uptime_event(
    const crypto::hash& node_id,
    const UptimeEvent& event)
{
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    auto& hist = uptime_history_[hk(node_id)];
    // F3: idemperenza per altezza. uptime.dat persiste al restart e i rebuild
    // (rebuild_registry_from_chain / rebuild_uptime_samples_from_chain) riprocessano
    // tutti i 0xA5: senza questo controllo ogni replay raddoppierebbe gli eventi e
    // altererebbe l'online ratio dell'uptime score. Un evento di altezza 0 (fuori
    // catena) non viene deduppato.
    bool dup = false;
    if (event.block_height > 0) {
        for (const auto& e : hist)
            if (e.block_height == event.block_height) { dup = true; break; }
    }
    if (!dup)
        hist.push_back(event);
    // F2: al registry passiamo il timestamp (come faceva il chiamante diretto), non
    // l'altezza: registry lo usa per last_seen/updated_at/total_uptime_seconds.
    if (node_registry_)
        node_registry_->record_uptime_event(node_id, event.online,
            event.timestamp, event.peer_count, event.response_time_ms);
    return true;
}

float MevaTrustEngine::get_uptime_percentage(const crypto::hash& node_id, uint64_t) {
    return calculate_uptime_score(node_id, 0) * 100.0f;
}

// ============================================================================
// UPTIME DERIVATO DALLA CATENA — regola 3% v3
// ============================================================================

bool MevaTrustEngine::record_chain_uptime_sample(
    const crypto::hash& node_id,
    uint64_t height,
    uint64_t block_ts)
{
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    auto& v = chain_uptime_samples_[hk(node_id)];

    // Idempotente per altezza: piu' 0xA5 dello stesso nodo nello stesso blocco sono un
    // campione solo. Senza questo un nodo potrebbe infilare firme multiple nel proprio
    // blocco e farsi accreditare tempo che non e' passato.
    //
    // I blocchi vengono processati in ordine di altezza, quindi l'ultimo elemento e'
    // gia' quello di altezza maggiore o uguale: se arriva un blocco indietro — durante
    // un reorg — non si inserisce, perche' altrimenti l'elenco perderebbe l'ordinamento
    // su cui si regge il calcolo a coppie.
    if (!v.empty() && height <= v.back().height)
        return false;

    v.push_back(ChainUptimeSample{height, block_ts});

    // Potatura: la finestra non guarda oltre UPTIME_WINDOW_BLOCKS, quindi i campioni piu'
    // vecchi non possono influenzare nessuna quota futura. Si potra durante la finestra,
    // non prima: potare un campione ancora dentro la finestra cambierebbe una quota gia'
    // calcolata e romperebbe il consenso.
    const uint64_t window = mevatrust::consensus::UPTIME_WINDOW_BLOCKS;
    while (!v.empty() && height >= window && v.front().height + window <= height)
        v.erase(v.begin());
    return true;
}

bool MevaTrustEngine::has_chain_uptime_samples() const {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    return !chain_uptime_samples_.empty();
}

void MevaTrustEngine::clear_chain_uptime_samples() {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    chain_uptime_samples_.clear();
}

// ── Chain proof samples (Anti-Sybil Fase 2, tag 0xAC) ──────────────────────

bool MevaTrustEngine::record_chain_proof_sample(
    const crypto::hash& node_id,
    uint64_t height,
    uint64_t target_height)
{
    using mevatrust::consensus::CHAIN_PROOF_VALIDITY_BLOCKS;
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    auto& v = chain_proof_samples_[hk(node_id)];

    // Idempotente per altezza, come l'uptime: piu' 0xAC dello stesso nodo nello
    // stesso blocco sono un campione solo. E come l'uptime, i blocchi arrivano in
    // ordine crescente: un'altezza non maggiore dell'ultima (reorg) non si inserisce,
    // per non rompere l'ordinamento su cui si regge la finestra.
    if (!v.empty() && height <= v.back().height)
        return false;

    v.push_back(ChainProofSample{height, target_height});

    // Potatura simmetrica a quella dell'uptime: la finestra non guarda oltre
    // CHAIN_PROOF_VALIDITY_BLOCKS, quindi i campioni piu' vecchi non possono
    // influenzare nessuna eleggibilita' futura.
    const uint64_t window = CHAIN_PROOF_VALIDITY_BLOCKS;
    while (!v.empty() && height >= window && v.front().height + window <= height)
        v.erase(v.begin());
    return true;
}

bool MevaTrustEngine::has_recent_chain_proof(const crypto::hash& node_id, uint64_t height) const {
    using mevatrust::consensus::CHAIN_PROOF_VALIDITY_BLOCKS;
    if (height < CHAIN_PROOF_VALIDITY_BLOCKS)
        return false;
    const uint64_t window_start = height - CHAIN_PROOF_VALIDITY_BLOCKS;
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    auto it = chain_proof_samples_.find(hk(node_id));
    if (it == chain_proof_samples_.end() || it->second.empty())
        return false;
    // La finestra guarda anche oltre height (difensivo sui reorg): conta se l'ultimo
    // campione e' dentro la finestra. I campioni sono ordinati, quindi il piu' recente
    // e' l'ultimo.
    return it->second.back().height >= window_start;
}

bool MevaTrustEngine::has_chain_proof_samples() const {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    return !chain_proof_samples_.empty();
}

void MevaTrustEngine::clear_chain_proof_samples() {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    chain_proof_samples_.clear();
}

MevaTrustEngine::UptimeStanding MevaTrustEngine::compute_uptime_standing(    const crypto::hash& node_id,
    uint64_t height) const
{
    using mevatrust::consensus::MAX_UPTIME_CREDIT_GAP_SECONDS;
    using mevatrust::consensus::MIN_ELIGIBLE_UPTIME_SECONDS;
    using mevatrust::consensus::UPTIME_WEIGHT_CAP_DAYS;
    using mevatrust::consensus::UPTIME_WINDOW_BLOCKS;

    UptimeStanding st;

    // Sotto la finestra non si e' ancora entrati: niente credito. Il nodo non e' "in
    // attesa", e' appena arrivato, e la differenza e' la stessa che gia' vale per la
    // soglia: due giorni non si dimezzano perche' il tempo e' poco.
    if (height < UPTIME_WINDOW_BLOCKS)
        return st;

    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    auto it = chain_uptime_samples_.find(hk(node_id));
    if (it == chain_uptime_samples_.end() || it->second.size() < 2)
        return st;

    const uint64_t window_start = height - UPTIME_WINDOW_BLOCKS;
    uint64_t credited = 0;
    uint32_t considered = 0;

    for (size_t i = 1; i < it->second.size(); ++i) {
        const ChainUptimeSample& prev = it->second[i - 1];
        const ChainUptimeSample& cur  = it->second[i];

        // Coppia interamente dentro la finestra: se il campione precedente e' caduto
        // fuori, il salto che lo collega a questo non e' piu' dentro la finestra e non
        // viene contato. Il credito che ne era derivato e' gia' uscito con lui.
        if (prev.height < window_start) continue;
        if (cur.height > height) break;   // difensivo: campioni oltre l'altezza richiesta

        ++considered;

        // Il credito e' il tempo di blocco realmente trascorso fra le due prove, non
        // quello che il nodo dichiara. Il limite sul singolo salto e' quello che distingue
        // "era su" da "era su fino a un certo punto e poi e' sparito": un nodo che manca
        // per una settimana e torna non accumula una settimana.
        if (cur.block_ts > prev.block_ts) {
            const uint64_t gap = cur.block_ts - prev.block_ts;
            credited += (gap > MAX_UPTIME_CREDIT_GAP_SECONDS)
                            ? MAX_UPTIME_CREDIT_GAP_SECONDS
                            : gap;
        }
    }

    st.credited_seconds = credited;
    st.samples = considered;
    st.days = static_cast<double>(credited) / 86400.0;

    if (credited < MIN_ELIGIBLE_UPTIME_SECONDS)
        return st;   // days e weight restano a zero, eligible resta false

    st.eligible = true;
    // Tetto sui giorni che contano. Oltre, il peso smette di crescere: il nodo piu'
    // longevo smette di guadagnare e la differenza resta nei badge.
    const double cap = static_cast<double>(UPTIME_WEIGHT_CAP_DAYS);
    st.weight = (st.days > cap) ? cap : st.days;
    return st;
}

float MevaTrustEngine::get_sync_percentage(
    const crypto::hash& node_id, uint64_t h, uint64_t) {
    return calculate_sync_score(node_id, h, h) * 100.0f;
}

uint64_t MevaTrustEngine::get_consecutive_uptime_seconds(const crypto::hash& node_id) {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
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
    return get_consecutive_uptime_seconds(node_id);
}

// ============================================================================
// CHALLENGE & RESPONSIVENESS
// ============================================================================

float MevaTrustEngine::get_average_response_time(const crypto::hash& node_id) {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    auto it = uptime_history_.find(hk(node_id));
    if (it == uptime_history_.end()) return 0.0f;
    float total = 0.0f; uint32_t count = 0;
    for (auto& ev : it->second)
        if (ev.response_time_ms > 0) { total += ev.response_time_ms; count++; }
    return count > 0 ? total / count : 0.0f;
}

float MevaTrustEngine::get_challenge_success_rate(const crypto::hash& node_id) {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
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

float MevaTrustEngine::get_activity_score(const crypto::hash& node_id, uint64_t) {
    return calculate_activity_score(node_id, 0);
}

// ============================================================================
// PERIOD MANAGEMENT
// ============================================================================

bool MevaTrustEngine::process_reward_period(uint64_t current_height) {
    if (current_height - last_period_processed_height_ < parameters_.period_length)
        return false;
    calculate_all_scores(current_height);
    last_period_processed_height_ = current_height;
    current_period_start_height_  = current_height;
    return true;
}

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

void MevaTrustEngine::set_parameters(const MevaTrustParameters& p) {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_); parameters_ = p;
}
MevaTrustEngine::MevaTrustParameters MevaTrustEngine::get_parameters() const {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_); return parameters_;
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
// ── Disk-read sanity (defense-in-depth: file corrotto/vecchio/attaccato ═
//    NON deve mai causare allocazioni bomba (len=0xFFFFFFFF → OOM) né
//    letture che restituiscono dati spazzatura. Il FORMATO su disco resta
//    immutato: stessa dimensione, stesso no. di campi — solo validazione
//    in lettura prima di ogni allocazione guidata dal file. ─────────────
static constexpr uint32_t kMaxDiskStringLen = 1u << 20;   // 1 MiB: sanity floor per read_str
static constexpr uint32_t kMaxDiskEntryCount = 1u << 22;  // 4M: sanity floor per vector-loop

static std::string read_str(std::ifstream& f) {
    uint32_t len = read_u32(f);
    if (len > kMaxDiskStringLen) { f.setstate(std::ios::failbit); return {}; }
    std::string s(len, '\0');
    f.read(s.data(), len);
    if (static_cast<size_t>(f.gcount()) != len) f.setstate(std::ios::failbit);
    return s;
}

bool MevaTrustEngine::save_to_disk() const {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
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
        // Save chain_uptime_samples_ (regola 3% v3)
        //
        // File separato, non aggiunto a uptime.dat: il formato di uptime.dat e' quello
        // che troverebbe un nodo lanciato con un binario di prima della v3, e riderlo per
        // aggiungere un campo in fondo farebbe fallire la lettura su tutta la storia.
        // Un file in piu' costa qualche inode e non rompe niente.
        {
            std::string path = db_path_ + "/chain_uptime.dat";
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) { MERROR("save_to_disk: cannot open " << path); return false; }
            write_u32(f, static_cast<uint32_t>(chain_uptime_samples_.size()));
            for (const auto& [key, samples] : chain_uptime_samples_) {
                write_u8(f, static_cast<uint8_t>(key.size()));
                f.write(key.data(), key.size());
                write_u32(f, static_cast<uint32_t>(samples.size()));
                for (const auto& s : samples) {
                    write_u64(f, s.height);
                    write_u64(f, s.block_ts);
                }
            }
        }
        // Save chain_proof_samples_ (Anti-Sybil Fase 2)
        //
        // File separato per la stessa ragione di chain_uptime.dat: non tocca il
        // formato di un file che un binario precedente si aspetta di leggere.
        {
            std::string path = db_path_ + "/chain_proof.dat";
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) { MERROR("save_to_disk: cannot open " << path); return false; }
            write_u32(f, static_cast<uint32_t>(chain_proof_samples_.size()));
            for (const auto& [key, samples] : chain_proof_samples_) {
                write_u8(f, static_cast<uint8_t>(key.size()));
                f.write(key.data(), key.size());
                write_u32(f, static_cast<uint32_t>(samples.size()));
                for (const auto& s : samples) {
                    write_u64(f, s.height);
                    write_u64(f, s.target_height);
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
        return true;
    } catch (const std::exception& e) {
        MERROR("save_to_disk failed: " << e.what());
        return false;
    }
}

bool MevaTrustEngine::load_from_disk() {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    try {
        // Load uptime_history_
        {
            std::string path = db_path_ + "/uptime.dat";
            std::ifstream f(path, std::ios::binary);
            if (!f) { MDEBUG("load_from_disk: no uptime.dat, clean start"); }
            else {
                uptime_history_.clear();
                uint32_t count = read_u32(f);
                if (count > kMaxDiskEntryCount) { f.setstate(std::ios::failbit); return false; }
                for (uint32_t i = 0; i < count; ++i) {
                    uint8_t klen = read_u8(f);
                    std::string key(klen, '\0');
                    f.read(key.data(), klen);
                    uint32_t evcount = read_u32(f);
                    if (evcount > kMaxDiskEntryCount) { f.setstate(std::ios::failbit); return false; }
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
        // Load chain_uptime_samples_ (regola 3% v3)
        {
            std::string path = db_path_ + "/chain_uptime.dat";
            std::ifstream f(path, std::ios::binary);
            if (!f) { MDEBUG("load_from_disk: no chain_uptime.dat, clean start"); }
            else {
                chain_uptime_samples_.clear();
                uint32_t count = read_u32(f);
                if (count > kMaxDiskEntryCount) { f.setstate(std::ios::failbit); return false; }
                for (uint32_t i = 0; i < count; ++i) {
                    uint8_t klen = read_u8(f);
                    std::string key(klen, '\0');
                    f.read(key.data(), klen);
                    uint32_t n = read_u32(f);
                    if (n > kMaxDiskEntryCount) { f.setstate(std::ios::failbit); return false; }
                    std::vector<ChainUptimeSample> samples;
                    samples.reserve(n);
                    for (uint32_t j = 0; j < n; ++j) {
                        ChainUptimeSample s;
                        s.height   = read_u64(f);
                        s.block_ts = read_u64(f);
                        samples.push_back(s);
                    }
                    // L'ordinamento e' un invariante del calcolo a coppie, non un
                    // gradimento: se il file e' stato riordinato o corrotto, le quote
                    // che ne uscirebbero non coinciderebbero con quelle degli altri nodi.
                    if (!std::is_sorted(samples.begin(), samples.end(),
                                        [](const ChainUptimeSample& a, const ChainUptimeSample& b) {
                                            return a.height < b.height;
                                        })) {
                        MERROR("load_from_disk: chain_uptime.dat non ordinato per altezza, scarto");
                        chain_uptime_samples_.clear();
                        f.setstate(std::ios::failbit);
                        return false;
                    }
                    chain_uptime_samples_[key] = std::move(samples);
                }
            }
        }
        // Load chain_proof_samples_ (Anti-Sybil Fase 2)
        {
            std::string path = db_path_ + "/chain_proof.dat";
            std::ifstream f(path, std::ios::binary);
            if (!f) { MDEBUG("load_from_disk: no chain_proof.dat, clean start"); }
            else {
                chain_proof_samples_.clear();
                uint32_t count = read_u32(f);
                if (count > kMaxDiskEntryCount) { f.setstate(std::ios::failbit); return false; }
                for (uint32_t i = 0; i < count; ++i) {
                    uint8_t klen = read_u8(f);
                    std::string key(klen, '\0');
                    f.read(key.data(), klen);
                    uint32_t n = read_u32(f);
                    if (n > kMaxDiskEntryCount) { f.setstate(std::ios::failbit); return false; }
                    std::vector<ChainProofSample> samples;
                    samples.reserve(n);
                    for (uint32_t j = 0; j < n; ++j) {
                        ChainProofSample s;
                        s.height        = read_u64(f);
                        s.target_height = read_u64(f);
                        samples.push_back(s);
                    }
                    if (!std::is_sorted(samples.begin(), samples.end(),
                                        [](const ChainProofSample& a, const ChainProofSample& b) {
                                            return a.height < b.height;
                                        })) {
                        MERROR("load_from_disk: chain_proof.dat non ordinato per altezza, scarto");
                        chain_proof_samples_.clear();
                        f.setstate(std::ios::failbit);
                        return false;
                    }
                    chain_proof_samples_[key] = std::move(samples);
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
                if (count > kMaxDiskEntryCount) { f.setstate(std::ios::failbit); return false; }
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
        return true;
    } catch (const std::exception& e) {
        MERROR("load_from_disk failed: " << e.what());
        return false;
    }
}

bool MevaTrustEngine::sync_database()       { return save_to_disk(); }
bool MevaTrustEngine::open_database()       { return true; }
bool MevaTrustEngine::close_database()      { return save_to_disk(); }

bool MevaTrustEngine::prune_old_data(uint64_t keep_before_height) {
    std::lock_guard<std::recursive_mutex> lock(cache_lock_);
    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    const uint64_t max_age_secs = 90 * 86400; // 90 days
    for (auto& kv : uptime_history_) {
        auto& ev = kv.second;
        ev.erase(std::remove_if(ev.begin(), ev.end(),
            [keep_before_height, now, max_age_secs](const UptimeEvent& e) {
                if (e.block_height > 0)
                    return e.block_height < keep_before_height;
                return (now > e.timestamp) && (now - e.timestamp) > max_age_secs;
            }), ev.end());
    }
    return true;
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

float MevaTrustEngine::calculate_uptime_score(const crypto::hash& node_id, uint64_t) {
    auto it = uptime_history_.find(hk(node_id));
    if (it == uptime_history_.end()) return 0.0f;
    uint32_t on = 0;
    for (auto& ev : it->second) if (ev.online) on++;
    size_t tot = it->second.size();
    return tot > 0 ? std::min(1.0f, static_cast<float>(on) / tot) : 0.0f;
}

float MevaTrustEngine::calculate_sync_score(
    const crypto::hash& node_id, uint64_t, uint64_t) {
    if (!node_registry_) return 0.0f;
    NodeRegistryEntry entry;
    if (!node_registry_->get_node_by_id(node_id, entry)) return 0.0f;
    return entry.is_synchronized ? 1.0f : 0.5f;
}

float MevaTrustEngine::calculate_responsiveness_score(const crypto::hash& node_id) {
    float rate  = get_challenge_success_rate(node_id);
    float avg   = get_average_response_time(node_id);
    float rt_sc = avg > 0 ? std::max(0.0f, 1.0f - avg / 2000.0f) : 1.0f;
    return (rate + rt_sc) / 2.0f;
}

float MevaTrustEngine::calculate_activity_score(const crypto::hash& node_id, uint64_t) {
    auto it = uptime_history_.find(hk(node_id));
    if (it == uptime_history_.end()) return 0.0f;
    uint32_t act = 0;
    for (auto& ev : it->second) if (ev.peer_count > 0) act++;
    return std::min(1.0f, static_cast<float>(act) / 10.0f);
}

} // namespace cryptonote


