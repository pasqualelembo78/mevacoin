// Copyright (c) 2026, The MevaCoin Project
// SPDX-License-Identifier: BSD-3-Clause
//
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  MEVACOIN — PARTICIPATION SECURITY HARDENING                           ║
// ║  File: src/cryptonote_core/mevatrust/mevatrust_security.h              ║
// ║                                                                          ║
// ║  Protezioni implementate:                                                ║
// ║    1. Anti-Sybil: deposit minimo MVC per registrazione nodo             ║
// ║    2. Restart protection: downtime tracking obbligatorio                 ║
// ║    3. Anti-eclipse: challenge broadcast a nodi multipli                  ║
// ║    4. Rate limiting: max N challenge per minuto per peer                 ║
// ║    5. Challenge window: scadenza challenge e penalità                    ║
// ╚══════════════════════════════════════════════════════════════════════════╝

#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <vector>
#include "crypto/crypto.h"
#include "mevatrust_types.h"

namespace cryptonote {
namespace mevatrust {
namespace security {

// ── 1. ANTI-SYBIL: Deposit Configuration ────────────────────────────────────
// Un nodo deve dimostrare di possedere MVC per registrarsi.
// Questo non richiede un lock-up reale ora, ma richiede una firma
// su un output non-speso (UTXO) con saldo sufficiente.

struct SybilConfig {
  // Minimo MVC richiesto per registrarsi (in atomic units)
  // Default: 10 MVC = 10 * 1e12 atomic units
  static constexpr uint64_t MIN_REGISTRATION_BALANCE = 10ULL * 1'000'000'000'000ULL;

  // Numero massimo di nodi per wallet address
  // Previene un wallet con tanto MVC di registrare N nodi
  static constexpr uint32_t MAX_NODES_PER_WALLET = 3;

  // Periodo di cooldown tra registrazioni dello stesso wallet (secondi)
  // Previene spam registrations
  static constexpr uint64_t REGISTRATION_COOLDOWN_SECS = 3600; // 1 ora

  // Challenge frequency boost per nodi nuovi (prime 24h)
  // I nodi nuovi ricevono 3x più challenge per rilevare fakers velocemente
  static constexpr uint32_t NEW_NODE_CHALLENGE_MULTIPLIER = 3;
  static constexpr uint64_t NEW_NODE_PERIOD_SECS = 86400; // 24h

  // L'UTXO usato per firmare la registrazione deve essere ancora non-speso
  // al momento della validazione. Previene "firma-e-spendi" immediato.
  static constexpr bool REQUIRE_UNSPENT_UTXO = true;
};

// ── 2. RESTART PROTECTION ───────────────────────────────────────────────────
// Quando un nodo riavvia, il downtime deve essere loggato.
// Un nodo NON può "resettare" le sue failed challenges riavviando.

struct RestartProtection {
  // Massimo downtime ignorabile (daemon crash breve, aggiornamento)
  // Se il downtime è <= questo valore, non viene penalizzato
  static constexpr uint64_t GRACE_PERIOD_SECS = 300; // 5 minuti

  // Se il nodo era assente durante N challenge, riceve penalità uptime
  // Penalità = missed_challenges * MISSED_CHALLENGE_PENALTY_PCT %
  static constexpr double MISSED_CHALLENGE_PENALTY_PCT = 5.0; // -5% per challenge mancata

  // Massima penalità per periodo (cap)
  static constexpr double MAX_PERIOD_PENALTY_PCT = 50.0;

  // Stato persistente su disco: ogni nodo salva il suo last_seen_timestamp
  // Al riavvio, se (now - last_seen) > GRACE_PERIOD, il gap è loggato
  struct NodePersistentState {
    uint64_t node_id_hash;          // Hash del node_id
    uint64_t last_active_timestamp; // Ultimo timestamp di attività
    uint64_t total_downtime_secs;   // Downtime totale accumulato
    uint32_t session_count;         // Numero di sessioni (riavvii)
    uint64_t registration_height;   // Altezza blocco di registrazione
  };

  // Verifica se un downtime è entro il grace period
  static bool is_graceful_restart(uint64_t last_active, uint64_t now_ts) {
    if (now_ts <= last_active) return true;
    return (now_ts - last_active) <= GRACE_PERIOD_SECS;
  }

  // Calcola penalità per downtime prolungato
  static double calculate_penalty(uint64_t downtime_secs, uint32_t missed_challenges) {
    double penalty = missed_challenges * MISSED_CHALLENGE_PENALTY_PCT;
    return std::min(penalty, MAX_PERIOD_PENALTY_PCT);
  }
};

// ── 3. ANTI-ECLIPSE: Multi-peer Challenge Routing ──────────────────────────
// Invece di inviare la challenge solo al peer diretto del nodo target,
// inviala anche a 2-3 nodi vicini (witnesses).
// Previene che un nodo eclissato manipoli la risposta.

struct EclipseProtection {
  // Numero di witness nodes a cui inviare la challenge
  static constexpr uint32_t WITNESS_COUNT = 3;

  // Timeout per raccogliere risposte dai witness (ms)
  static constexpr uint32_t WITNESS_TIMEOUT_MS = 5000;

  // Quorum minimo di witness che devono concordare sulla risposta
  // (per validare che la risposta sia autentica)
  static constexpr uint32_t WITNESS_QUORUM = 3; // unanimità: 3/3

  // Se il nodo target risponde ma i witness non vedono la risposta,
  // la risposta è sospetta
  static constexpr bool REQUIRE_WITNESS_CONFIRMATION = true;
};

// ── 4. RATE LIMITING ────────────────────────────────────────────────────────
// Previene DOS via challenge flood: un peer non può inviare
// più di N challenge al secondo verso lo stesso target.

class ChallengeRateLimiter {
public:
  struct PeerState {
    uint64_t window_start_ms  = 0;
    uint32_t challenge_count  = 0;
    uint32_t response_count   = 0;
  };

  // Max challenge per peer per finestra
  static constexpr uint32_t MAX_CHALLENGES_PER_WINDOW = 1;
  // Finestra temporale (ms)
  static constexpr uint64_t WINDOW_MS = 60'000; // 1 minuto
  // Penalità per chi supera il limite: ban temporaneo
  static constexpr uint64_t RATE_LIMIT_BAN_MS = 300'000; // 5 minuti

  // Combina peer_id (crittografico) + IP per la chiave.
  static std::string make_key(const std::string& peer_ip, const crypto::hash& peer_id) {
    std::string k;
    k.reserve(64 + peer_ip.size());
    k.assign(reinterpret_cast<const char*>(peer_id.data), 32);
    k += '|';
    k += peer_ip;
    return k;
  }

  bool is_allowed(const std::string& peer_ip, const crypto::hash& peer_id, uint64_t now_ms) {
    std::lock_guard<std::mutex> lock(m_);
    auto& state = peers_[make_key(peer_ip, peer_id)];
    if (now_ms - state.window_start_ms > WINDOW_MS) {
      state.window_start_ms = now_ms;
      state.challenge_count = 0;
    }
    if (state.challenge_count >= MAX_CHALLENGES_PER_WINDOW) return false;
    ++state.challenge_count;
    return true;
  }

  bool is_global_allowed(uint64_t now_ms) {
    std::lock_guard<std::mutex> lock(m_);
    if (now_ms - global_window_start_ > WINDOW_MS) {
      global_window_start_ = now_ms;
      global_challenge_count_ = 0;
    }
    if (global_challenge_count_ >= GLOBAL_MAX_PER_WINDOW) return false;
    ++global_challenge_count_;
    return true;
  }

  bool is_challenge_id_seen(const crypto::hash& cid, uint64_t now_ms) {
    std::lock_guard<std::mutex> lock(m_);
    auto it = seen_challenges_.find(cid);
    if (it != seen_challenges_.end()) {
      if (now_ms - it->second < DEDUP_TTL_MS) return true;
      seen_challenges_.erase(it);
    }
    seen_challenges_[cid] = now_ms;
    if (seen_challenges_.size() > DEDUP_MAX_SIZE) {
      auto oldest = seen_challenges_.begin();
      for (auto i = seen_challenges_.begin(); i != seen_challenges_.end(); ++i)
        if (i->second < oldest->second) oldest = i;
      seen_challenges_.erase(oldest);
    }
    return false;
  }

  void record_response(const std::string& peer_ip, const crypto::hash& peer_id) {
    std::lock_guard<std::mutex> lock(m_);
    auto it = peers_.find(make_key(peer_ip, peer_id));
    if (it != peers_.end()) ++it->second.response_count;
  }

  void cleanup(uint64_t now_ms) {
    std::lock_guard<std::mutex> lock(m_);
    for (auto it = peers_.begin(); it != peers_.end(); ) {
      if (now_ms - it->second.window_start_ms > WINDOW_MS * 10)
        it = peers_.erase(it);
      else
        ++it;
    }
    for (auto it = seen_challenges_.begin(); it != seen_challenges_.end(); ) {
      if (now_ms - it->second > DEDUP_TTL_MS)
        it = seen_challenges_.erase(it);
      else
        ++it;
    }
  }

private:
  std::mutex m_;
  std::unordered_map<std::string, PeerState> peers_;

  uint64_t global_window_start_ = 0;
  uint32_t global_challenge_count_ = 0;
  static constexpr uint32_t GLOBAL_MAX_PER_WINDOW = 60;

  std::unordered_map<crypto::hash, uint64_t> seen_challenges_;
  static constexpr uint64_t DEDUP_TTL_MS = 30'000;
  static constexpr size_t DEDUP_MAX_SIZE = 5000;
};

// ── 5. CHALLENGE WINDOW: Scadenza e penalità ────────────────────────────────
// Una challenge deve ricevere risposta entro X ms.
// Risposte tardive vengono rifiutate (nodo offline = penalità).

struct ChallengeWindowConfig {
  // Timeout standard per risposta challenge
  static constexpr uint64_t RESPONSE_TIMEOUT_MS = 2'000; // 2 secondi

  // Timeout esteso per nodi su reti lente
  static constexpr uint64_t EXTENDED_TIMEOUT_MS = 5'000; // 5 secondi

  // Massimo numero di challenge in sospeso per nodo
  // Previene accumulo infinito di challenge non risposte
  static constexpr uint32_t MAX_PENDING_CHALLENGES = 5;

  // Dopo N challenge fallite consecutive, il nodo va in stato SUSPENDED
  static constexpr uint32_t CONSECUTIVE_FAIL_THRESHOLD = 5;

  // Periodo di sospensione (il nodo non riceve reward durante)
  static constexpr uint64_t SUSPENSION_PERIOD_SECS = 86400; // 24 ore
};

// ── FUNZIONE HELPER: valida una registrazione contro le regole anti-Sybil ───
inline bool validate_registration_security(
    const std::string& wallet_address,
    uint64_t wallet_balance_atomic,
    uint32_t existing_nodes_for_wallet,
    uint64_t last_registration_ts,
    uint64_t now_ts,
    bool utxo_already_spent,
    std::string& error_msg)
{
  // 1. Saldo minimo
  if (wallet_balance_atomic < SybilConfig::MIN_REGISTRATION_BALANCE) {
    error_msg = "Saldo insufficiente: richiesti 10 MVC per registrare un nodo";
    return false;
  }
  // 2. UTXO deve essere non-speso (anti "firma-e-spendi")
  if (SybilConfig::REQUIRE_UNSPENT_UTXO && utxo_already_spent) {
    error_msg = "L'UTXO usato per la firma e' gia' stato speso: "
                "la registrazione richiede un UTXO non-speso come proof-of-stake";
    return false;
  }
  // 3. Limite nodi per wallet
  if (existing_nodes_for_wallet >= SybilConfig::MAX_NODES_PER_WALLET) {
    error_msg = "Massimo " + std::to_string(SybilConfig::MAX_NODES_PER_WALLET)
                + " nodi per wallet";
    return false;
  }
  // 4. Cooldown registrazione
  if (last_registration_ts > 0 &&
      (now_ts - last_registration_ts) < SybilConfig::REGISTRATION_COOLDOWN_SECS) {
    error_msg = "Cooldown: attendi " +
                std::to_string(SybilConfig::REGISTRATION_COOLDOWN_SECS / 60)
                + " minuti tra registrazioni";
    return false;
  }
  return true;
}

} // namespace security
} // namespace mevatrust
} // namespace cryptonote

