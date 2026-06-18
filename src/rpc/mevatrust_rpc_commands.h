// Copyright (c) 2024, The Mevacoin Project
// Distributed under the MIT/X11 software license
//
// [C3 FIX] Anti-Sybil UTXO -- view_key_hex + proof_txid + proof_output_index
// aggiunti a COMMAND_RPC_REGISTER_NODE::request_t
//
// Sostituisce: src/rpc/participation_rpc_commands.h

#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "serialization/keyvalue_serialization.h"
#include "cryptonote_basic/cryptonote_basic.h"

namespace cryptonote { namespace rpc {

struct COMMAND_RPC_GET_MEVATRUST_SCORE {
  struct request_t { std::string node_id;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string node_id; double score; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(score) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_GET_NODE_UPTIME {
  struct request_t { std::string node_id;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string node_id; uint64_t uptime_seconds; double uptime_percentage; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(uptime_seconds) KV_SERIALIZE(uptime_percentage) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_GET_NODE_STATUS {
  struct request_t { std::string node_id;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string node_id; bool is_active; bool is_synced; uint64_t last_seen; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(is_active) KV_SERIALIZE(is_synced) KV_SERIALIZE(last_seen) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_GET_REWARD_HISTORY {
  struct request_t { std::string node_id; uint32_t count;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(count) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct reward_entry_t { uint64_t height; uint64_t amount; uint64_t timestamp;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(height) KV_SERIALIZE(amount) KV_SERIALIZE(timestamp) END_KV_SERIALIZE_MAP() };
  struct response_t { std::string node_id; std::vector<reward_entry_t> history; uint64_t total_rewards; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(history) KV_SERIALIZE(total_rewards) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_GET_BADGES {
  struct request_t { std::string node_id;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string node_id; std::vector<std::string> badges; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(badges) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_GET_BADGE_REQUIREMENTS {
  struct request_t { std::string node_id;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct requirement_detail_t {
    std::string metric;
    double current{0};
    double required{0};
    bool met{false};
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(metric) KV_SERIALIZE(current) KV_SERIALIZE(required) KV_SERIALIZE(met) END_KV_SERIALIZE_MAP() };
  struct badge_info_t {
    std::string name;
    std::string description;
    bool earned{false};
    std::string reason;
    std::vector<requirement_detail_t> details;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(name) KV_SERIALIZE(description) KV_SERIALIZE(earned) KV_SERIALIZE(reason) KV_SERIALIZE(details) END_KV_SERIALIZE_MAP() };
  struct response_t { std::string node_id; std::vector<badge_info_t> badges; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(badges) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

// =============================================================================
// [C3 FIX] COMMAND_RPC_REGISTER_NODE
//
// NUOVI CAMPI nella request per prova crittografica di possesso UTXO:
//
//   view_key_hex        -- private view key (hex 64 chars).
//                          Usata per derivare la chiave effimera e verificare
//                          l'ownership dell'output. Il daemon la scarta dopo la
//                          verifica: NON viene salvata in nessun log/DB.
//
//   proof_txid          -- TXID (hex 64 chars) di una TX confermata che contiene
//                          un output appartenente a questo wallet con valore
//                          >= MIN_REGISTRATION_BALANCE (10 MVC).
//
//   proof_output_index  -- Indice 0-based dell'output nella TX indicata.
//
// BYPASS TESTNET: se view_key_hex/proof_txid sono vuoti, il daemon
// accetta in bypass (MWARNING) -- usare SOLO in fase bootstrap.
// Rimuovere il bypass prima del lancio mainnet con supply sufficiente.
// =============================================================================
struct COMMAND_RPC_REGISTER_NODE {
  struct request_t {
    std::string public_key;            // spend public key (hex)
    std::string signature;             // firma su (node_id || pubkey)
    std::string address;               // wallet address
    // Anti-Sybil C3 -------------------------------------------------------
    std::string view_key_hex;          // private view key -- solo verifica, non salvata
    std::string proof_txid;            // TXID della TX con output >= 10 MVC
    uint32_t    proof_output_index{0}; // indice output nella TX
    // Chiave P2P separata (sicurezza: wallet key != node key) --------------
    std::string node_public_key;       // Ed25519 hex 64 char (chiave firma P2P del nodo)
    // ---------------------------------------------------------------------
    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(public_key)
      KV_SERIALIZE(signature)
      KV_SERIALIZE(address)
      KV_SERIALIZE_OPT(view_key_hex,       std::string(""))
      KV_SERIALIZE_OPT(proof_txid,         std::string(""))
      KV_SERIALIZE_OPT(proof_output_index, (uint32_t)0)
      KV_SERIALIZE_OPT(node_public_key,    std::string(""))
    END_KV_SERIALIZE_MAP()
  };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string node_id; bool success; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(success) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_GET_INCENTIVE_POOL_STATUS {
  struct request_t { BEGIN_KV_SERIALIZE_MAP() END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { uint64_t pool_balance; uint64_t total_distributed; uint32_t active_nodes; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(pool_balance) KV_SERIALIZE(total_distributed) KV_SERIALIZE(active_nodes) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_GET_ELIGIBLE_NODES {
  struct request_t { BEGIN_KV_SERIALIZE_MAP() END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct node_info_t { std::string node_id; double score; uint64_t uptime_seconds;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(score) KV_SERIALIZE(uptime_seconds) END_KV_SERIALIZE_MAP() };
  struct response_t { std::vector<node_info_t> nodes; uint32_t total_count; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(nodes) KV_SERIALIZE(total_count) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_GET_INCENTIVE_HISTORY {
  struct request_t { std::string node_id; uint32_t limit{50}; uint32_t offset{0};
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE_OPT(limit,(uint32_t)50) KV_SERIALIZE_OPT(offset,(uint32_t)0) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct incentive_entry_t { uint64_t height{0}; uint64_t amount{0}; uint64_t timestamp{0}; std::string badge_type; std::string reason;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(height) KV_SERIALIZE(amount) KV_SERIALIZE(timestamp) KV_SERIALIZE(badge_type) KV_SERIALIZE(reason) END_KV_SERIALIZE_MAP() };
  struct response_t { std::string node_id; std::vector<incentive_entry_t> entries; uint32_t total{0}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(entries) KV_SERIALIZE(total) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_GET_ALL_NODE_INCENTIVES {
  struct request_t { uint32_t limit{100};
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE_OPT(limit,(uint32_t)100) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct node_summary_t { std::string node_id; std::string wallet_address; uint32_t badge_count{0}; std::vector<std::string> badge_types; uint64_t total_rewards{0}; double score{0.0};
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(wallet_address) KV_SERIALIZE(badge_count) KV_SERIALIZE(badge_types) KV_SERIALIZE(total_rewards) KV_SERIALIZE(score) END_KV_SERIALIZE_MAP() };
  struct response_t { std::vector<node_summary_t> nodes; uint32_t total_nodes{0}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(nodes) KV_SERIALIZE(total_nodes) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

// =============================================================================
// COMMAND_RPC_UNREGISTER_NODE
//
// Permette al proprietario del wallet di rimuovere il proprio nodo dalla rete
// di partecipazione, cessando di ricevere incentivi.
// La firma crittografica garantisce che solo il proprietario possa farlo.
// =============================================================================
struct COMMAND_RPC_UNREGISTER_NODE {
  struct request_t {
    std::string node_id;       // ID del nodo da de-registrare (hex 64 char)
    std::string address;       // indirizzo wallet del proprietario
    std::string signature;     // firma Ed25519 su (node_id || address)
    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(node_id)
      KV_SERIALIZE(address)
      KV_SERIALIZE(signature)
    END_KV_SERIALIZE_MAP()
  };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t {
    bool        success{false};
    std::string status;
    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(success)
      KV_SERIALIZE(status)
    END_KV_SERIALIZE_MAP()
  };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_GET_REWARD_HISTORY_BY_NODE {
  struct request_t { std::string node_id; uint32_t limit{50};
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE_OPT(limit,(uint32_t)50) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct reward_record_t { uint64_t height{0}; uint64_t amount{0}; uint64_t timestamp{0}; double node_score{0.0};
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(height) KV_SERIALIZE(amount) KV_SERIALIZE(timestamp) KV_SERIALIZE(node_score) END_KV_SERIALIZE_MAP() };
  struct response_t { std::string node_id; std::vector<reward_record_t> records; uint64_t total_earned{0}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(records) KV_SERIALIZE(total_earned) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

// ── Circle Registry RPC (Fase 3) ──────────────────────────────────────────────

struct COMMAND_RPC_CIRCLE_CREATE {
  struct request_t { std::string name; std::string admin_pubkey;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(name) KV_SERIALIZE(admin_pubkey) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string circle_id; bool success{false}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(circle_id) KV_SERIALIZE(success) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_CIRCLE_INFO {
  struct request_t { std::string circle_id;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(circle_id) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string circle_id; std::string name; std::string admin_pubkey;
    std::vector<std::string> members; uint64_t created_height; uint64_t created_timestamp;
    uint64_t updated_at; std::string metadata; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(circle_id) KV_SERIALIZE(name) KV_SERIALIZE(admin_pubkey)
    KV_SERIALIZE(members) KV_SERIALIZE(created_height) KV_SERIALIZE(created_timestamp)
    KV_SERIALIZE(updated_at) KV_SERIALIZE(metadata) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_CIRCLE_LIST {
  struct request_t { std::string wallet_pubkey;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(wallet_pubkey) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct circle_summary_t {
    std::string circle_id; std::string name; std::string admin_pubkey;
    uint32_t member_count{0}; uint64_t created_height;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(circle_id) KV_SERIALIZE(name)
    KV_SERIALIZE(admin_pubkey) KV_SERIALIZE(member_count) KV_SERIALIZE(created_height) END_KV_SERIALIZE_MAP() };
  struct response_t { std::vector<circle_summary_t> circles; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(circles) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_CIRCLE_JOIN {
  struct request_t { std::string circle_id; std::string member_pubkey; std::string caller_pubkey;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(circle_id) KV_SERIALIZE(member_pubkey) KV_SERIALIZE(caller_pubkey) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { bool success{false}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(success) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_CIRCLE_LEAVE {
  struct request_t { std::string circle_id; std::string member_pubkey; std::string caller_pubkey;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(circle_id) KV_SERIALIZE(member_pubkey) KV_SERIALIZE(caller_pubkey) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { bool success{false}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(success) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_CIRCLE_CHANGE_ADMIN {
  struct request_t { std::string circle_id; std::string new_admin_pubkey; std::string caller_pubkey;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(circle_id) KV_SERIALIZE(new_admin_pubkey) KV_SERIALIZE(caller_pubkey) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { bool success{false}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(success) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_CIRCLE_PROPOSAL_LIST {
  struct request_t { std::string circle_id;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(circle_id) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct proposal_summary_t {
    std::string proposal_id; std::string circle_id; std::string proposer_pk;
    std::string target_pk; uint64_t created_height; uint8_t convocation;
    uint8_t status; uint64_t finalize_height; uint32_t yes_count{0};
    uint32_t no_count{0};
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(proposal_id) KV_SERIALIZE(circle_id)
    KV_SERIALIZE(proposer_pk) KV_SERIALIZE(target_pk) KV_SERIALIZE(created_height)
    KV_SERIALIZE(convocation) KV_SERIALIZE(status) KV_SERIALIZE(finalize_height)
    KV_SERIALIZE(yes_count) KV_SERIALIZE(no_count) END_KV_SERIALIZE_MAP() };
  struct response_t { std::vector<proposal_summary_t> proposals; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(proposals) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_CIRCLE_PROPOSAL_VOTES {
  struct request_t { std::string proposal_id;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(proposal_id) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct vote_entry_t { std::string voter_pk; bool vote_yes; uint64_t height;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(voter_pk) KV_SERIALIZE(vote_yes) KV_SERIALIZE(height) END_KV_SERIALIZE_MAP() };
  struct response_t { std::string proposal_id; std::string circle_id; std::string target_pk; uint8_t proposal_status{0}; uint8_t convocation{0};
    std::vector<vote_entry_t> votes; uint32_t total{0}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(proposal_id) KV_SERIALIZE(circle_id) KV_SERIALIZE(target_pk)
    KV_SERIALIZE(proposal_status) KV_SERIALIZE(convocation) KV_SERIALIZE(votes) KV_SERIALIZE(total) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_CIRCLE_DISBAND {
  struct request_t { std::string circle_id; std::string caller_pubkey;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(circle_id) KV_SERIALIZE(caller_pubkey) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { bool success{false}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(success) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

// ── Penalty System RPC (Fase 5) ───────────────────────────────────────────────

struct COMMAND_RPC_GET_PENALTY_HISTORY {
  struct request_t { std::string node_id;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct penalty_entry_t { std::string offense_type; double amount; uint64_t height; uint64_t timestamp; std::string reason;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(offense_type) KV_SERIALIZE(amount) KV_SERIALIZE(height) KV_SERIALIZE(timestamp) KV_SERIALIZE(reason) END_KV_SERIALIZE_MAP() };
  struct response_t { std::string node_id; std::vector<penalty_entry_t> entries; uint32_t total{0}; double total_penalty{0.0}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(entries) KV_SERIALIZE(total) KV_SERIALIZE(total_penalty) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_BAN_NODE {
  struct request_t { std::string node_id; std::string reason; std::string caller_pubkey;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(reason) KV_SERIALIZE(caller_pubkey) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { bool success{false}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(success) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_UNBAN_NODE {
  struct request_t { std::string node_id; std::string caller_pubkey;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(node_id) KV_SERIALIZE(caller_pubkey) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { bool success{false}; std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(success) KV_SERIALIZE(status) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

// ── Store commands ──────────────────────────────────────────────────────────
struct COMMAND_RPC_STORE_LIST {
  struct request_t { bool active_only{true}; uint32_t limit{0}; bool top{true};
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(active_only) KV_SERIALIZE(limit) KV_SERIALIZE(top) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string status;
    struct StoreInfo { std::string store_id; std::string name; std::string description;
      std::string url; std::string owner_pubkey; uint64_t created_height; uint32_t item_count;
      BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(store_id) KV_SERIALIZE(name)
        KV_SERIALIZE(description) KV_SERIALIZE(url) KV_SERIALIZE(owner_pubkey)
        KV_SERIALIZE(created_height) KV_SERIALIZE(item_count) END_KV_SERIALIZE_MAP() };
    std::vector<StoreInfo> stores;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(status) KV_SERIALIZE(stores) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_STORE_SHOW {
  struct request_t { std::string store_id;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(store_id) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string status; std::string name; std::string description;
    std::string url; std::string owner_pubkey; uint64_t created_height; uint32_t item_count;
    struct ItemInfo { std::string item_id; std::string name; std::string description;
      uint64_t price; std::string category; bool active;
      BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(item_id) KV_SERIALIZE(name)
        KV_SERIALIZE(description) KV_SERIALIZE(price) KV_SERIALIZE(category)
        KV_SERIALIZE(active) END_KV_SERIALIZE_MAP() };
    std::vector<ItemInfo> items;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(status) KV_SERIALIZE(name)
      KV_SERIALIZE(description) KV_SERIALIZE(url) KV_SERIALIZE(owner_pubkey)
      KV_SERIALIZE(created_height) KV_SERIALIZE(item_count)
      KV_SERIALIZE(items) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_STORE_SEARCH {
  struct request_t { std::string keyword; bool search_items{true};
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(keyword) KV_SERIALIZE(search_items) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string status;
    struct StoreInfo { std::string store_id; std::string name; std::string description;
      std::string url; std::string owner_pubkey; uint64_t created_height; uint32_t item_count;
      BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(store_id) KV_SERIALIZE(name)
        KV_SERIALIZE(description) KV_SERIALIZE(url) KV_SERIALIZE(owner_pubkey)
        KV_SERIALIZE(created_height) KV_SERIALIZE(item_count) END_KV_SERIALIZE_MAP() };
    std::vector<StoreInfo> stores;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(status) KV_SERIALIZE(stores) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

struct COMMAND_RPC_STORE_MY_PURCHASES {
  struct request_t { std::string buyer_pubkey;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(buyer_pubkey) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t { std::string status;
    struct PurchaseInfo { std::string store_id; std::string item_id;
      std::string buyer_pubkey; uint64_t purchase_height;
      BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(store_id) KV_SERIALIZE(item_id)
        KV_SERIALIZE(buyer_pubkey) KV_SERIALIZE(purchase_height) END_KV_SERIALIZE_MAP() };
    std::vector<PurchaseInfo> purchases;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(status) KV_SERIALIZE(purchases) END_KV_SERIALIZE_MAP() };
  typedef epee::misc_utils::struct_init<response_t> response;
};

// ── Get Node Public Key ──────────────────────────────────────────────────────
// Restituisce la chiave pubblica del nodo (node_pk) caricata nel daemon.
// Utile per registrare il nodo da remoto senza SSH.
struct COMMAND_RPC_GET_NODE_PUBKEY {
  struct request_t {
    BEGIN_KV_SERIALIZE_MAP() END_KV_SERIALIZE_MAP()
  };
  typedef epee::misc_utils::struct_init<request_t> request;
  struct response_t {
    std::string node_pubkey;   // 64 hex chars
    std::string status;
    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(node_pubkey)
      KV_SERIALIZE(status)
    END_KV_SERIALIZE_MAP()
  };
  typedef epee::misc_utils::struct_init<response_t> response;
};

} // namespace rpc
} // namespace cryptonote

