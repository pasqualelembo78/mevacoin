// Copyright (c) 2024, The Mevacoin Project
// Participation System - AvailabilityProof Implementation

#include "availability_proof.h"
#include <ctime>
#include <cstring>
#include <algorithm>
#include <random>
#include <cstdio>

namespace cryptonote {

AvailabilityProofEngine::AvailabilityProofEngine(
    const std::string& db_path,
    std::shared_ptr<NodeRegistry> node_registry)
    : db_path_(db_path),
      node_registry_(node_registry) {
    parameters_.challenge_interval_hours = 10;
    parameters_.challenges_per_period = 3;
    parameters_.response_timeout_ms = 2000;
    parameters_.success_rate_threshold = 0.66f;
    parameters_.max_consecutive_failures = 3;
    parameters_.reputation_penalty_per_failure = 0.05f;
    parameters_.reputation_reward_per_success = 0.02f;
    parameters_.verify_block_hash = true;
}

AvailabilityProofEngine::~AvailabilityProofEngine() {
}

AvailabilityChallenge AvailabilityProofEngine::generate_challenge(
    const crypto::hash& node_id,
    uint64_t current_height,
    uint64_t current_timestamp) {
    
    AvailabilityChallenge challenge;
    challenge.challenge_id = compute_challenge_id(node_id, current_timestamp, ChallengeType::BLOCK_HASH);
    challenge.node_id = node_id;
    challenge.challenge_type = ChallengeType::BLOCK_HASH;
    challenge.parameter.block_height = current_height;
    challenge.sent_timestamp = current_timestamp;
    challenge.timeout_ms = parameters_.response_timeout_ms;
    challenge.sender_id = 0;
    challenge.responded = false;
    challenge.response_timestamp = 0;
    challenge.response_time_ms = 0;
    challenge.response_correct = false;
    
    return challenge;
}

std::vector<AvailabilityChallenge> AvailabilityProofEngine::get_pending_challenges(const crypto::hash& node_id) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    auto it = pending_challenges_.find(node_id);
    if (it != pending_challenges_.end()) {
        return it->second;
    }
    return std::vector<AvailabilityChallenge>();
}

bool AvailabilityProofEngine::has_expired_challenges(const crypto::hash& node_id, uint64_t current_timestamp) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    auto it = pending_challenges_.find(node_id);
    if (it == pending_challenges_.end()) {
        return false;
    }
    
    for (const auto& challenge : it->second) {
        uint64_t elapsed_ms = (current_timestamp - challenge.sent_timestamp) * 1000;
        if (elapsed_ms > challenge.timeout_ms) {
            return true;
        }
    }
    return false;
}

bool AvailabilityProofEngine::get_challenge(const crypto::hash& challenge_id, AvailabilityChallenge& challenge) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    for (const auto& node_challenges : pending_challenges_) {
        for (const auto& ch : node_challenges.second) {
            if (ch.challenge_id == challenge_id) {
                challenge = ch;
                return true;
            }
        }
    }
    return false;
}

bool AvailabilityProofEngine::record_response(
    const AvailabilityChallenge& challenge,
    const AvailabilityResponse& response,
    uint64_t current_timestamp) {
    
    std::lock_guard<std::mutex> lock(cache_lock_);
    
    auto& pending = pending_challenges_[challenge.node_id];
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        if (it->challenge_id == challenge.challenge_id) {
            it->responded = true;
            it->response_timestamp = current_timestamp;
            it->response_time_ms = current_timestamp - it->sent_timestamp;
            it->response_data = response.response_data;
            challenge_history_[challenge.node_id].push_back(*it);
            pending.erase(it);
            return true;
        }
    }
    return false;
}

AvailabilityProofEngine::ResponseValidation AvailabilityProofEngine::validate_response(
    const AvailabilityChallenge& challenge,
    const AvailabilityResponse& response,
    uint64_t current_height) {
    
    if (response.response_timestamp > challenge.sent_timestamp + challenge.timeout_ms) {
        return ResponseValidation::TIMEOUT;
    }
    
    if (response.response_data.empty()) {
        return ResponseValidation::INVALID_DATA;
    }
    
    if (parameters_.verify_block_hash && challenge.challenge_type == ChallengeType::BLOCK_HASH) {
        if (!verify_block_response(challenge.parameter.block_height, response.response_data, challenge.node_id)) {
            return ResponseValidation::INVALID_DATA;
        }
    }
    
    return ResponseValidation::VALID;
}

std::vector<crypto::hash> AvailabilityProofEngine::get_nodes_due_for_challenge(uint64_t current_timestamp)
{
    std::vector<crypto::hash> nodes_due;
    if (!node_registry_) return nodes_due;
    auto active_nodes = node_registry_->get_active_nodes();
    for (const auto& entry : active_nodes) {
        {
            std::lock_guard<std::mutex> lock(cache_lock_);
            auto pit = pending_challenges_.find(entry.node_id);
            if (pit != pending_challenges_.end() && !pit->second.empty()) continue;
        }
        bool due = (entry.next_challenge_time == 0) || (current_timestamp >= entry.next_challenge_time);
        if (due) nodes_due.push_back(entry.node_id);
    }
    return nodes_due;
}

uint32_t AvailabilityProofEngine::schedule_period_challenges(
    uint64_t current_height,
    uint64_t current_timestamp,
    uint32_t challenges_per_node)
{
    if (!node_registry_) return 0;
    uint32_t total_scheduled = 0;
    auto active_nodes = node_registry_->get_active_nodes();
    uint32_t n=(uint32_t)active_nodes.size();
    if (!n) return 0;
    uint64_t interval_ms=parameters_.challenge_interval_hours*3600ULL*1000ULL;
    for (const auto& entry:active_nodes) {
        if (entry.next_challenge_time==0) {
            uint64_t stagger=(total_scheduled*interval_ms)/n;
            node_registry_->update_node_challenge_time(entry.node_id,current_timestamp+stagger);
            total_scheduled++;
        }
    }
    return total_scheduled;
}

uint64_t AvailabilityProofEngine::get_next_challenge_time(const crypto::hash& node_id)
{
    if (!node_registry_) return 0;
    NodeRegistryEntry entry;
    if (!node_registry_->get_node_by_id(node_id,entry)) return 0;
    return entry.next_challenge_time;
}

AvailabilityProofEngine::ChallengeStatistics AvailabilityProofEngine::get_node_challenge_statistics(const crypto::hash& node_id) {
    ChallengeStatistics stats;
    stats.node_id = node_id;
    stats.total_challenges = 0;
    stats.successful_responses = 0;
    stats.failed_responses = 0;
    stats.expired_challenges = 0;
    stats.invalid_responses = 0;
    stats.success_rate = 0.0f;
    stats.average_response_time_ms = 0.0f;
    stats.last_challenge_time = 0;
    stats.next_challenge_time = 0;
    stats.consecutive_failures = 0;
    stats.consecutive_successes = 0;
    
    std::lock_guard<std::mutex> lock(cache_lock_);
    
    auto it = challenge_history_.find(node_id);
    if (it != challenge_history_.end()) {
        const auto& history = it->second;
        stats.total_challenges = history.size();
        
        uint64_t total_response_time = 0;
        for (const auto& challenge : history) {
            if (challenge.responded) {
                if (challenge.response_correct) {
                    stats.successful_responses++;
                    stats.consecutive_successes++;
                    stats.consecutive_failures = 0;
                } else {
                    stats.failed_responses++;
                    stats.consecutive_failures++;
                    stats.consecutive_successes = 0;
                }
                total_response_time += challenge.response_time_ms;
            } else {
                stats.expired_challenges++;
            }
            stats.last_challenge_time = std::max(stats.last_challenge_time, challenge.sent_timestamp);
        }
        
        if (stats.total_challenges > 0) {
            stats.success_rate = static_cast<float>(stats.successful_responses) / stats.total_challenges;
            if (stats.successful_responses + stats.failed_responses > 0) {
                stats.average_response_time_ms = 
                    static_cast<float>(total_response_time) / (stats.successful_responses + stats.failed_responses);
            }
        }
    }
    
    return stats;
}

AvailabilityProofEngine::NetworkChallengeStats AvailabilityProofEngine::get_network_challenge_statistics() {
    NetworkChallengeStats stats;
    stats.total_challenges_sent = 0;
    stats.total_responses_received = 0;
    stats.total_expired = 0;
    stats.average_success_rate = 0.0f;
    stats.average_response_time_ms = 0.0f;
    stats.nodes_with_perfect_score = 0;
    stats.nodes_with_failing_score = 0;
    
    std::lock_guard<std::mutex> lock(cache_lock_);
    
    if (challenge_history_.empty()) {
        return stats;
    }
    
    for (const auto& node_history : challenge_history_) {
        const auto& challenges = node_history.second;
        stats.total_challenges_sent += challenges.size();
        
        for (const auto& challenge : challenges) {
            if (challenge.responded) {
                stats.total_responses_received++;
            } else {
                stats.total_expired++;
            }
        }
    }
    
    return stats;
}

bool AvailabilityProofEngine::record_response_time(const crypto::hash& node_id, uint32_t response_time_ms) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    response_times_[node_id].push_back(response_time_ms);
    return true;
}

float AvailabilityProofEngine::get_average_response_time(const crypto::hash& node_id, uint32_t lookback_count) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    
    auto it = response_times_.find(node_id);
    if (it == response_times_.end() || it->second.empty()) {
        return 0.0f;
    }
    
    const auto& times = it->second;
    uint32_t count = std::min(lookback_count, static_cast<uint32_t>(times.size()));
    uint64_t sum = 0;
    
    for (uint32_t i = 0; i < count; ++i) {
        sum += times[times.size() - 1 - i];
    }
    
    return static_cast<float>(sum) / count;
}

float AvailabilityProofEngine::calculate_reputation_impact(
    const ChallengeStatistics& stats,
    float current_reputation) {
    
    float impact = 0.0f;
    
    if (stats.success_rate >= parameters_.success_rate_threshold) {
        impact = parameters_.reputation_reward_per_success * stats.successful_responses;
    } else {
        impact = -parameters_.reputation_penalty_per_failure * stats.failed_responses;
    }
    
    return current_reputation + impact;
}

void AvailabilityProofEngine::set_parameters(const AvailabilityParameters& params) {
    parameters_ = params;
}

AvailabilityProofEngine::AvailabilityParameters AvailabilityProofEngine::get_parameters() const {
    return parameters_;
}

void AvailabilityProofEngine::set_block_hash_func(GetBlockHashFunc func) {
    get_block_hash_ = func;
}

void AvailabilityProofEngine::set_block_height_func(GetBlockHeightFunc func) {
    get_block_height_ = func;
}

void AvailabilityProofEngine::set_tx_hash_func(GetTxHashFunc func) {
    get_tx_hash_ = func;
}

bool AvailabilityProofEngine::save_to_disk() const {
    static constexpr uint64_t POA_MAGIC=0x504F4148495354FFULL;
    static constexpr uint32_t POA_VERSION=1;
    const std::string tmp=db_path_+"/poa_history.dat.tmp", fin=db_path_+"/poa_history.dat";
    std::ofstream f(tmp,std::ios::binary|std::ios::trunc);
    if(!f.is_open()) return false;
    auto ws=[&](const std::string& s){uint16_t l=(uint16_t)s.size();f.write((const char*)&l,sizeof(l));if(l)f.write(s.data(),l);};
    std::lock_guard<std::mutex> lock(cache_lock_);
    uint32_t nc=(uint32_t)challenge_history_.size();
    f.write((const char*)&POA_MAGIC,sizeof(POA_MAGIC));
    f.write((const char*)&POA_VERSION,sizeof(POA_VERSION));
    f.write((const char*)&nc,sizeof(nc));
    for(const auto& kv:challenge_history_){
        f.write((const char*)kv.first.data,32);
        uint32_t cc=(uint32_t)kv.second.size();f.write((const char*)&cc,sizeof(cc));
        for(const auto& ch:kv.second){
            uint8_t ct=(uint8_t)ch.challenge_type,rsp=ch.responded?1u:0u,rco=ch.response_correct?1u:0u;
            f.write((const char*)ch.challenge_id.data,32);
            f.write((const char*)&ct,sizeof(ct));
            f.write((const char*)&ch.parameter.block_height,sizeof(ch.parameter.block_height));
            f.write((const char*)&ch.sent_timestamp,sizeof(ch.sent_timestamp));
            f.write((const char*)&ch.timeout_ms,sizeof(ch.timeout_ms));
            f.write((const char*)&rsp,sizeof(rsp));
            f.write((const char*)&ch.response_time_ms,sizeof(ch.response_time_ms));
            f.write((const char*)&rco,sizeof(rco));
            ws(ch.response_data);
        }
    }
    uint32_t rtc=(uint32_t)response_times_.size();f.write((const char*)&rtc,sizeof(rtc));
    for(const auto& kv:response_times_){
        f.write((const char*)kv.first.data,32);
        uint32_t tc=(uint32_t)kv.second.size();f.write((const char*)&tc,sizeof(tc));
        for(uint32_t t:kv.second) f.write((const char*)&t,sizeof(t));
    }
    if(!f.good()){f.close();std::remove(tmp.c_str());return false;}
    f.close();
    if(std::rename(tmp.c_str(),fin.c_str())!=0){std::remove(tmp.c_str());return false;}
    return true;
}

bool AvailabilityProofEngine::load_from_disk() {
    static constexpr uint64_t POA_MAGIC=0x504F4148495354FFULL;
    static constexpr uint32_t POA_VERSION=1;
    const std::string path=db_path_+"/poa_history.dat";
    std::ifstream f(path,std::ios::binary);
    if(!f.is_open()) return true;
    auto rs=[&](std::string& s){uint16_t l=0;f.read((char*)&l,sizeof(l));if(l){s.resize(l);f.read(&s[0],l);}else s.clear();};
    uint64_t magic=0;uint32_t ver=0,nc=0;
    f.read((char*)&magic,sizeof(magic));f.read((char*)&ver,sizeof(ver));f.read((char*)&nc,sizeof(nc));
    if(!f.good()||magic!=POA_MAGIC||ver!=POA_VERSION) return false;
    std::lock_guard<std::mutex> lock(cache_lock_);
    challenge_history_.clear();response_times_.clear();
    for(uint32_t i=0;i<nc;i++){
        crypto::hash nid{};f.read((char*)nid.data,32);
        uint32_t ch_count=0;f.read((char*)&ch_count,sizeof(ch_count));
        auto& hist=challenge_history_[nid];
        for(uint32_t j=0;j<ch_count;j++){
            AvailabilityChallenge ch{};uint8_t ct=0,rsp=0,rco=0;
            f.read((char*)ch.challenge_id.data,32);
            f.read((char*)&ct,sizeof(ct));ch.challenge_type=static_cast<ChallengeType>(ct);
            f.read((char*)&ch.parameter.block_height,sizeof(ch.parameter.block_height));
            f.read((char*)&ch.sent_timestamp,sizeof(ch.sent_timestamp));
            f.read((char*)&ch.timeout_ms,sizeof(ch.timeout_ms));
            f.read((char*)&rsp,sizeof(rsp));ch.responded=(rsp==1);
            f.read((char*)&ch.response_time_ms,sizeof(ch.response_time_ms));
            f.read((char*)&rco,sizeof(rco));ch.response_correct=(rco==1);
            rs(ch.response_data);ch.node_id=nid;
            if(f.good()) hist.push_back(ch);
        }
        if(!f.good()) break;
    }
    uint32_t rtc=0;
    if(f.read((char*)&rtc,sizeof(rtc))&&f.good()){
        for(uint32_t i=0;i<rtc;i++){
            crypto::hash nid{};f.read((char*)nid.data,32);
            uint32_t tc=0;f.read((char*)&tc,sizeof(tc));
            auto& times=response_times_[nid];
            for(uint32_t j=0;j<tc;j++){uint32_t t=0;f.read((char*)&t,sizeof(t));if(f.good())times.push_back(t);}
            if(!f.good()) break;
        }
    }
    return true;
}

bool AvailabilityProofEngine::sync_database() {
    return true;
}

bool AvailabilityProofEngine::prune_old_challenges(uint64_t keep_before_timestamp) {
    std::lock_guard<std::mutex> lock(cache_lock_);
    
    for (auto& node_history : challenge_history_) {
        auto& challenges = node_history.second;
        challenges.erase(
            std::remove_if(challenges.begin(), challenges.end(),
                [keep_before_timestamp](const AvailabilityChallenge& c) {
                    return c.sent_timestamp < keep_before_timestamp;
                }),
            challenges.end()
        );
    }
    
    return true;
}

std::string AvailabilityProofEngine::generate_random_data(size_t bytes) {
    std::string result(bytes, '\0');
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (size_t i = 0; i < bytes; ++i) {
        result[i] = static_cast<char>(dis(gen));
    }
    
    return result;
}

crypto::hash AvailabilityProofEngine::compute_challenge_id(
    const crypto::hash& node_id,
    uint64_t timestamp,
    ChallengeType type) {
    
    std::string m;
    m.append(reinterpret_cast<const char*>(node_id.data), sizeof(node_id.data));
    m.append(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
    uint8_t t = static_cast<uint8_t>(type);
    m.push_back(t);
    return crypto::cn_fast_hash(m.data(), m.size());
}

bool AvailabilityProofEngine::verify_block_response(
    uint64_t block_height,
    const std::string& response_data,
    const crypto::hash& node_id) {
    
    if (get_block_hash_) {
        crypto::hash expected_hash;
        if (get_block_hash_(block_height, expected_hash)) {
            crypto::hash response_hash;
            if (response_data.size() >= sizeof(response_hash.data)) {
                memcpy(response_hash.data, response_data.data(), sizeof(response_hash.data));
            } else {
                return false;
            }
            return response_hash == expected_hash;
        }
    }
    
    return false;
}

bool AvailabilityProofEngine::open_database() {
    return true;
}

bool AvailabilityProofEngine::close_database() {
    return true;
}

std::string AvailabilityProofEngine::challenge_type_to_string(ChallengeType type) const {
    switch (type) {
        case ChallengeType::BLOCK_HASH:
            return "BLOCK_HASH";
        case ChallengeType::BLOCK_HEIGHT:
            return "BLOCK_HEIGHT";
        case ChallengeType::TX_HASH:
            return "TX_HASH";
        case ChallengeType::TX_EXISTS:
            return "TX_EXISTS";
        case ChallengeType::UTXO_EXISTS:
            return "UTXO_EXISTS";
        case ChallengeType::LAST_BLOCK_HASH:
            return "LAST_BLOCK_HASH";
        default:
            return "UNKNOWN";
    }
}

void AvailabilityProofEngine::set_send_challenge_func(SendChallengeFunc func) { send_challenge_func_=std::move(func); }
bool AvailabilityProofEngine::send_challenge(const crypto::hash& target_node_id, uint64_t current_height, uint64_t current_timestamp_ms) {
    AvailabilityChallenge challenge=generate_challenge(target_node_id,current_height,current_timestamp_ms);
    { std::lock_guard<std::mutex> lock(cache_lock_); pending_challenges_[target_node_id].push_back(challenge); }
    if (!send_challenge_func_) return false;
    bool sent=send_challenge_func_(challenge);
    if (!sent) { std::lock_guard<std::mutex> lock(cache_lock_); auto& p=pending_challenges_[target_node_id];
        p.erase(std::remove_if(p.begin(),p.end(),[&challenge](const AvailabilityChallenge& c){ return c.challenge_id==challenge.challenge_id; }),p.end()); }
    return sent;
}
bool AvailabilityProofEngine::process_challenge_response(const AvailabilityResponse& response, uint64_t current_height) {
    AvailabilityChallenge challenge;
    if (!get_challenge(response.challenge_id,challenge)) return false;
    auto validation=validate_response(challenge,response,current_height);
    bool is_correct=(validation==ResponseValidation::VALID);
    AvailabilityChallenge updated=challenge;
    updated.responded=true; updated.response_correct=is_correct;
    updated.response_data=response.response_data; updated.response_time_ms=response.processing_time_ms;
    updated.response_timestamp=response.response_timestamp;
    { std::lock_guard<std::mutex> lock(cache_lock_);
      challenge_history_[challenge.node_id].push_back(updated);
      auto& p=pending_challenges_[challenge.node_id];
      p.erase(std::remove_if(p.begin(),p.end(),[&](const AvailabilityChallenge& c){ return c.challenge_id==challenge.challenge_id; }),p.end()); }
    record_response_time(challenge.node_id,response.processing_time_ms);
    return is_correct;
}

} // namespace cryptonote
