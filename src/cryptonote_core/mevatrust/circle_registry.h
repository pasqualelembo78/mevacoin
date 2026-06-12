// Copyright (c) 2024, The Mevacoin Project
// circle_registry.h — CircleRegistry: registro LMDB per cerchie (micro-DAO).
#pragma once

struct MDB_env;
typedef unsigned int MDB_dbi;

#include <string>
#include <map>
#include <set>
#include <vector>
#include <cstdint>
#include <mutex>
#include "crypto/crypto.h"
#include "cryptonote_basic/tx_extra.h"

namespace cryptonote {

// ── Voting constants (modello italiano) ──────────────────────────────────────
// Prima convocazione:  quorum 2/3 dei membri, maggioranza dei votanti
// Seconda convocazione: quorum 1/3 dei membri, maggioranza dei votanti
static constexpr uint64_t VOTE_WINDOW_BLOCKS = 10080; // ~1 settimana (1 blocco/min)
static constexpr double   FIRST_CONVOCATION_QUORUM = 2.0/3.0;
static constexpr double   SECOND_CONVOCATION_QUORUM = 1.0/3.0;

struct ProposalEntry {
  crypto::hash proposal_id;
  crypto::hash circle_id;
  crypto::public_key proposer_pk;
  crypto::public_key target_pk;       // Nuovo admin proposto
  uint64_t created_height;
  uint8_t  convocation;              // 0=prima, 1=seconda (attuale)
  uint8_t  status;                   // 0=open_first, 1=open_second, 2=passed, 3=failed
  uint64_t finalize_height;          // 0 = non ancora finalizzata
  bool passed_first_convocation() const { return status == 2; }
  bool is_open() const { return status == 0 || status == 1; }
};

struct VoteEntry {
  crypto::hash proposal_id;
  crypto::public_key voter_pk;
  bool vote_yes;
  uint64_t height;
};

struct CircleEntry {
  crypto::hash circle_id;
  std::string name;
  crypto::public_key admin_pubkey;
  std::vector<crypto::public_key> members;
  uint64_t created_height;
  uint64_t created_timestamp;
  uint64_t updated_at;
  std::string metadata;

  bool is_admin(const crypto::public_key& pk) const;
  bool has_member(const crypto::public_key& pk) const;
};

class CircleRegistry {
public:
  CircleRegistry(const std::string& db_path);
  ~CircleRegistry();

  // CRUD
  crypto::hash create_circle(const std::string& name,
                             const crypto::public_key& admin_pubkey,
                             uint64_t height);
  bool add_member(const crypto::hash& circle_id, const crypto::public_key& member_pubkey,
                  const crypto::public_key& caller_pubkey);
  bool remove_member(const crypto::hash& circle_id, const crypto::public_key& member_pubkey,
                     const crypto::public_key& caller_pubkey);
  bool change_admin(const crypto::hash& circle_id, const crypto::public_key& new_admin,
                    const crypto::public_key& caller_pubkey);
  bool disband_circle(const crypto::hash& circle_id,
                      const crypto::public_key& caller_pubkey);

  // Query
  bool get_circle(const crypto::hash& circle_id, CircleEntry& out) const;
  bool get_circle_by_name(const std::string& name, CircleEntry& out) const;
  std::vector<CircleEntry> list_circles() const;
  std::vector<CircleEntry> get_circles_for_member(const crypto::public_key& pubkey) const;
  bool is_member(const crypto::hash& circle_id, const crypto::public_key& pubkey) const;
  bool name_exists(const std::string& name) const;

  // ── Voting (prima/seconda convocazione) ──────────────────────────────────
  crypto::hash create_proposal(const crypto::hash& circle_id,
                                const crypto::public_key& proposer_pk,
                                const crypto::public_key& target_pk,
                                uint64_t height);
  bool cast_vote(const crypto::hash& proposal_id,
                  const crypto::public_key& voter_pk,
                  bool vote_yes, uint64_t height);
  uint8_t finalize_vote(const crypto::hash& proposal_id, uint64_t current_height);
  bool get_proposal(const crypto::hash& proposal_id, ProposalEntry& out) const;
  std::vector<ProposalEntry> list_proposals(const crypto::hash& circle_id) const;
  std::vector<VoteEntry> list_votes(const crypto::hash& proposal_id) const;

  // ── Vote persistence ─────────────────────────────────────────────────────
  bool db_put_proposal(const ProposalEntry& e);
  bool db_put_vote(const VoteEntry& e);
  bool db_get_proposal(const crypto::hash& proposal_id, ProposalEntry& out) const;
  bool db_del_proposal(const crypto::hash& proposal_id);

  // Anti-replay: verifica e marca un tx_hash usato per una cerchia
  // Usa il tx_hash come nonce implicito (globalmente unico, senza serializzazione extra)
  bool is_tx_processed(const crypto::hash& circle_id, const crypto::hash& tx_hash) const;
  void mark_tx_processed(const crypto::hash& circle_id, const crypto::hash& tx_hash);

  // Limiti
  static constexpr size_t MAX_MEMBERS = 1000;

  // Reorg helpers: bypassano i controlli di autorizzazione
  bool restore_circle(const CircleEntry& e);      // ripristina cerchia (per reorg DISBAND)
  bool set_admin(const crypto::hash& circle_id,   // imposta admin senza controlli (per reorg CHANGE_ADMIN)
                 const crypto::public_key& new_admin);
  // Serializza/deserializza CircleEntry per reorg
  static std::string pack_circle_entry(const CircleEntry& e);
  static bool unpack_circle_entry(const std::string& data, CircleEntry& e);

  // Persistence
  bool load_from_disk();
  bool save_to_disk() const;
  bool clear_all();

private:
  std::string db_path_;
  MDB_env* m_env_{nullptr};
  MDB_dbi  m_dbi_{0};
  MDB_dbi  m_dbi_proposals_{0};
  MDB_dbi  m_dbi_votes_{0};

  std::map<std::string, CircleEntry> circles_;
  std::map<std::string, std::string> name_to_id_;
  mutable std::mutex lock_;

  // In-memory proposals/votes (popolato da LMDB in load_from_disk)
  std::map<std::string, ProposalEntry> proposals_;
  // votes_ mappa: key = hk(proposal_id) + hk(voter_pk), value = VoteEntry
  std::map<std::string, VoteEntry> votes_;
  // Index: circle_id -> set di proposal_id
  std::map<std::string, std::set<std::string>> circle_proposals_;

  // Anti-replay: per ogni cerchia, set di tx_hash elaborati (chiave = hk(circle_id))
  // Non persistito su LMDB — i tx_hash sono impliciti nella cronologia on-chain.
  // Al re-scan dalla chain, la mappa si ripopola da zero.
  mutable std::map<std::string, std::set<std::string>> m_processed_txs;

  bool db_put(const CircleEntry& e);
  bool db_del(const crypto::hash& circle_id);
};

} // namespace cryptonote
