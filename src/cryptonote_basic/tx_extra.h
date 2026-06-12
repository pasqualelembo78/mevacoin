// Copyright (c) 2014-2024, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#pragma once

#include <cstdint>
#include <cstddef>

#include <boost/variant/variant.hpp>

#include "serialization/serialization.h"
#include "serialization/binary_archive.h"
#include "serialization/variant.h"
#include "serialization/string.h"
#include "crypto/crypto.h"

#define TX_EXTRA_PADDING_MAX_COUNT          255
#define TX_EXTRA_NONCE_MAX_COUNT            255

#define TX_EXTRA_TAG_PADDING                0x00
#define TX_EXTRA_TAG_PUBKEY                 0x01
#define TX_EXTRA_NONCE                      0x02
#define TX_EXTRA_MERGE_MINING_TAG           0x03
#define TX_EXTRA_TAG_ADDITIONAL_PUBKEYS     0x04
#define TX_EXTRA_MYSTERIOUS_MINERGATE_TAG   0xDE

// ── Mevacoin Participation tags — Fase 2 On-Chain Registration ───────────────
#define TX_EXTRA_TAG_MEVATRUST_REGISTRATION  0xA0
#define TX_EXTRA_TAG_MEVATRUST_DEREGISTER    0xA1
#define TX_EXTRA_TAG_MEVATRUST_SNAPSHOT      0xA2
#define TX_EXTRA_TAG_MEVATRUST_CIRCLE        0xA3
#define TX_EXTRA_TAG_MEVATRUST_PENALTY       0xA4
#define TX_EXTRA_TAG_MEVATRUST_UPTIME        0xA5
#define TX_EXTRA_TAG_MEVATRUST_CHALLENGE     0xA6
#define TX_EXTRA_TAG_MEVATRUST_STATE_ROOT    0xA7
#define TX_EXTRA_TAG_MEVATRUST_STORE         0xA8
#define TX_EXTRA_TAG_MEVATRUST_CIRCLE_VOTE  0xA9
// ─────────────────────────────────────────────────────────────────────────────


#define TX_EXTRA_NONCE_PAYMENT_ID           0x00
#define TX_EXTRA_NONCE_ENCRYPTED_PAYMENT_ID 0x01

namespace cryptonote
{
  struct tx_extra_padding
  {
    size_t size;

    // load
    template <template <bool> class Archive>
    bool member_do_serialize(Archive<false>& ar)
    {
      // size - 1 - because of variant tag
      for (size = 1; size <= TX_EXTRA_PADDING_MAX_COUNT; ++size)
      {
        if (ar.eof())
          break;

        uint8_t zero;
        if (!::do_serialize(ar, zero))
          return false;

        if (0 != zero)
          return false;
      }

      return size <= TX_EXTRA_PADDING_MAX_COUNT;
    }

    // store
    template <template <bool> class Archive>
    bool member_do_serialize(Archive<true>& ar)
    {
      if(TX_EXTRA_PADDING_MAX_COUNT < size)
        return false;

      // i = 1 - because of variant tag
      for (size_t i = 1; i < size; ++i)
      {
        uint8_t zero = 0;
        if (!::do_serialize(ar, zero))
          return false;
      }
      return true;
    }
  };

  struct tx_extra_pub_key
  {
    crypto::public_key pub_key;

    BEGIN_SERIALIZE()
      FIELD(pub_key)
    END_SERIALIZE()
  };

  struct tx_extra_nonce
  {
    std::string nonce;

    BEGIN_SERIALIZE()
      FIELD(nonce)
      if(TX_EXTRA_NONCE_MAX_COUNT < nonce.size()) return false;
    END_SERIALIZE()
  };

  struct tx_extra_merge_mining_tag
  {
    struct serialize_helper
    {
      tx_extra_merge_mining_tag& mm_tag;

      serialize_helper(tx_extra_merge_mining_tag& mm_tag_) : mm_tag(mm_tag_)
      {
      }

      BEGIN_SERIALIZE()
        VARINT_FIELD_N("depth", mm_tag.depth)
        FIELD_N("merkle_root", mm_tag.merkle_root)
      END_SERIALIZE()
    };

    uint64_t depth;
    crypto::hash merkle_root;

    // load
    template <template <bool> class Archive>
    bool member_do_serialize(Archive<false>& ar)
    {
      std::string field;
      if(!::do_serialize(ar, field))
        return false;

      binary_archive<false> iar{epee::strspan<std::uint8_t>(field)};
      serialize_helper helper(*this);
      return ::serialization::serialize(iar, helper);
    }

    // store
    template <template <bool> class Archive>
    bool member_do_serialize(Archive<true>& ar)
    {
      std::ostringstream oss;
      binary_archive<true> oar(oss);
      serialize_helper helper(*this);
      if(!::do_serialize(oar, helper))
        return false;

      std::string field = oss.str();
      return ::serialization::serialize(ar, field);
    }
  };

  // per-output additional tx pubkey for multi-destination transfers involving at least one subaddress
  struct tx_extra_additional_pub_keys
  {
    std::vector<crypto::public_key> data;

    BEGIN_SERIALIZE()
      FIELD(data)
    END_SERIALIZE()
  };

  struct tx_extra_mysterious_minergate
  {
    std::string data;

    BEGIN_SERIALIZE()
      FIELD(data)
    END_SERIALIZE()
  };

  
// ── Participation Registration — tag 0xA0 ──────────────────────────────────
// Registra un nodo on-chain: sign(H(node_id||node_pubkey)) con wallet key.
struct tx_extra_mevatrust_registration
{
  crypto::hash       node_id;        // H(wallet_pubkey || node_pubkey || timestamp)
  crypto::public_key wallet_pubkey;  // Chiave pubblica wallet proprietario
  crypto::public_key node_pubkey;    // Chiave pubblica nodo per firmare challenge
  std::string        wallet_address; // Indirizzo wallet (stringa)
  uint32_t           port{0};        // Porta P2P del nodo
  crypto::signature  signature;      // sign(node_id||node_pubkey) con wallet key

  BEGIN_SERIALIZE()
    FIELD(node_id)
    FIELD(wallet_pubkey)
    FIELD(node_pubkey)
    FIELD(wallet_address)
    VARINT_FIELD(port)
    FIELD(signature)
  END_SERIALIZE()
};

// ── Participation Deregistration — tag 0xA1 ────────────────────────────────
// Deregistra un nodo on-chain: sign("deregister"||node_id) con wallet key.
struct tx_extra_mevatrust_deregister
{
  crypto::hash       node_id;
  crypto::public_key wallet_pubkey;
  crypto::signature  signature;      // sign("deregister"||node_id) con wallet key

  BEGIN_SERIALIZE()
    FIELD(node_id)
    FIELD(wallet_pubkey)
    FIELD(signature)
  END_SERIALIZE()
};

// ── Participation Snapshot — tag 0xA2 (Fase 3: Badge On-Chain) ────────────
// Snapshot periodico ogni 240 blocchi + badge awards firmati dal proposer.
struct tx_extra_mevatrust_snapshot
{
  // ── BadgeAward: un badge assegnato on-chain in questo periodo ──────────
  struct BadgeAward {
    crypto::hash       node_id;            // Nodo che riceve il badge
    uint8_t            badge_type{0};      // BadgeType (uint8_t)
    uint64_t           awarded_height{0};  // Altezza assegnazione
    crypto::public_key proposer_pubkey;    // Pubkey del nodo che propone
    crypto::signature  proposer_sig;       // sign(node_id||badge_type||height)

    BEGIN_SERIALIZE()
      FIELD(node_id)
      VARINT_FIELD(badge_type)
      VARINT_FIELD(awarded_height)
      FIELD(proposer_pubkey)
      FIELD(proposer_sig)
    END_SERIALIZE()
  };

  uint64_t                height{0};      // Altezza blocco snapshot
  uint32_t                period{0};      // Numero periodo (height / 240)
  uint32_t                node_count{0};  // Nodi attivi in questo periodo
  std::vector<BadgeAward> badge_awards;   // Badge assegnati in questo periodo

  BEGIN_SERIALIZE()
    VARINT_FIELD(height)
    VARINT_FIELD(period)
    VARINT_FIELD(node_count)
    FIELD(badge_awards)
  END_SERIALIZE()
};

// ── Circle Operations — tag 0xA3 (Fase 2) ───────────────────────────────────
// Micro-DAO operations: CREATE/JOIN/LEAVE/CHANGE_ADMIN/DISBAND.
struct tx_extra_mevatrust_circle
{
  enum OpType : uint8_t { CREATE=0, JOIN=1, LEAVE=2, CHANGE_ADMIN=3, DISBAND=4 };
  OpType             op_type;
  crypto::hash       circle_id;
  std::string        circle_name;
  crypto::public_key target_pubkey;
  crypto::public_key signer_pubkey;
  crypto::signature  signature;

  BEGIN_SERIALIZE()
    VARINT_FIELD(op_type)
    FIELD(circle_id)
    FIELD(circle_name)
    FIELD(target_pubkey)
    FIELD(signer_pubkey)
    FIELD(signature)
  END_SERIALIZE()
};

// ── Circle Vote Operations — tag 0xA9 (Voting all'italiana) ───────────────────
// Sistema di voto per cambio admin con prima/seconda convocazione.
struct tx_extra_mevatrust_circle_vote
{
  enum VoteOpType : uint8_t { PROPOSE_CHANGE_ADMIN=0, CAST_VOTE=1, FINALIZE_VOTE=2 };
  VoteOpType        op_type;
  crypto::hash      proposal_id;    // PROPOSE: 0; VOTE/FINALIZE: ID proposta
  crypto::hash      circle_id;      // Cerchia coinvolta
  crypto::public_key target_pubkey; // PROPOSE: nuovo admin; VOTE: voter pk
  crypto::public_key signer_pubkey; // Firma
  bool              vote_yes;       // VOTE: true=yes false=no
  std::string       reason;         // Motivazione opzionale
  crypto::signature signature;

  BEGIN_SERIALIZE()
    VARINT_FIELD(op_type)
    FIELD(proposal_id)
    FIELD(circle_id)
    FIELD(target_pubkey)
    FIELD(signer_pubkey)
    FIELD(vote_yes)
    FIELD(reason)
    FIELD(signature)
  END_SERIALIZE()
};

// ── Penalty Event — tag 0xA4 (On-Chain Penalty System) ──────────────────────
// Infligge una penalita' o ban/unban su un nodo. Firmato da admin/daemon.
// amount: penalita' in millipunti (es. -50 = -0.05 score, -500 = -0.50 score).
struct tx_extra_mevatrust_penalty
{
  enum PenOpType : uint8_t { PENALTY=0, BAN=1, UNBAN=2 };
  PenOpType         op_type;
  crypto::hash      node_id;          // Nodo bersaglio
  std::string       offense_type;     // Stringa tipo offesa (es. "MALICIOUS_ACTIVITY")
  int64_t           amount{0};        // Penalita' in millipunti (-50 = -0.05)
  std::string       reason;           // Motivazione
  crypto::public_key signer_pubkey;   // Chi ha emesso la penalita'
  crypto::signature signature;        // sign(op_type || node_id || offense || amount || reason)

  BEGIN_SERIALIZE()
    VARINT_FIELD(op_type)
    FIELD(node_id)
    FIELD(offense_type)
    VARINT_FIELD(amount)
    FIELD(reason)
    FIELD(signer_pubkey)
    FIELD(signature)
  END_SERIALIZE()
};

// ── Uptime Commitment — tag 0xA5 (On-Chain Uptime Proof) ────────────────────
// Firmato dal nodo ogni N blocchi per dimostrare uptime.
struct tx_extra_mevatrust_uptime
{
  crypto::hash       node_id;
  uint64_t           uptime_seconds{0};
  uint64_t           timestamp{0};
  uint32_t           peer_count{0};
  bool               is_synced{false};
  uint64_t           sync_height{0};
  crypto::public_key node_pubkey;
  crypto::signature  signature;        // sign(node_id || uptime || ts || peers || sync)

  BEGIN_SERIALIZE()
    FIELD(node_id)
    VARINT_FIELD(uptime_seconds)
    VARINT_FIELD(timestamp)
    VARINT_FIELD(peer_count)
    FIELD(is_synced)
    VARINT_FIELD(sync_height)
    FIELD(node_pubkey)
    FIELD(signature)
  END_SERIALIZE()
};

// ── Challenge Result — tag 0xA6 (On-Chain Challenge Proof) ──────────────────
// Risultato di una sfida P2P tra due nodi, firmato da entrambi.
struct tx_extra_mevatrust_challenge
{
  crypto::hash       challenger_node_id;
  crypto::hash       challenged_node_id;
  bool               success{false};
  uint64_t           response_time_ms{0};
  uint64_t           height{0};
  crypto::public_key challenger_pubkey;
  crypto::public_key challenged_pubkey;
  crypto::signature  challenger_sig;   // sign(challenger || challenged || success || time || height)
  crypto::signature  challenged_sig;

  BEGIN_SERIALIZE()
    FIELD(challenger_node_id)
    FIELD(challenged_node_id)
    FIELD(success)
    VARINT_FIELD(response_time_ms)
    VARINT_FIELD(height)
    FIELD(challenger_pubkey)
    FIELD(challenged_pubkey)
    FIELD(challenger_sig)
    FIELD(challenged_sig)
  END_SERIALIZE()
};

// ── MevaTrust State Root — tag 0xA7 (Fork Detection) ────────────────────────
// Commesso nel miner_tx di OGNI blocco. Contiene l'hash Merkle dello stato
// MevaTrust (nodi, cerchie, badge) a questa altezza. Ogni nodo deriva il
// proprio stato e verifica che il root matchi: se non matcha => FORK.
struct tx_extra_mevatrust_state_root
{
  crypto::hash state_root;      // Merkle root dello stato MevaTrust
  uint64_t     height{0};

  BEGIN_SERIALIZE()
    FIELD(state_root)
    VARINT_FIELD(height)
  END_SERIALIZE()
};

// ── MevaTrust Store — tag 0xA8 (On-Chain Store) ────────────────────────────
// Permette a chiunque di creare un negozio personalizzato on-chain,
// listare item e acquistarli. Tutto registrato in LMDB.
struct tx_extra_mevatrust_store
{
  enum Operation : uint8_t {
    STORE_CREATE   = 0,
    STORE_UPDATE   = 1,
    ITEM_LIST      = 2,
    ITEM_DELIST    = 3,
    ITEM_BUY       = 4,
  };

  Operation op{STORE_CREATE};
  crypto::hash store_id{};
  crypto::hash item_id{};
  std::string name;
  std::string description;
  std::string url;
  uint64_t price{0};
  std::string category;
  std::string metadata;
  crypto::public_key owner_pubkey{};
  crypto::signature owner_sig{};
  crypto::public_key buyer_pubkey{};

  BEGIN_SERIALIZE()
    VARINT_FIELD(op)
    FIELD(store_id)
    FIELD(item_id)
    FIELD(name)
    FIELD(description)
    FIELD(url)
    VARINT_FIELD(price)
    FIELD(category)
    FIELD(metadata)
    FIELD(owner_pubkey)
    FIELD(owner_sig)
    FIELD(buyer_pubkey)
  END_SERIALIZE()
};

// ─────────────────────────────────────────────────────────────────────────────
// tx_extra_field format, except tx_extra_padding and tx_extra_pub_key:
  //   varint tag;
  //   varint size;
  //   varint data[];
  typedef boost::variant<tx_extra_padding, tx_extra_pub_key, tx_extra_nonce, tx_extra_merge_mining_tag, tx_extra_additional_pub_keys, tx_extra_mysterious_minergate> tx_extra_field;
}

VARIANT_TAG(binary_archive, cryptonote::tx_extra_padding, TX_EXTRA_TAG_PADDING);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_pub_key, TX_EXTRA_TAG_PUBKEY);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_nonce, TX_EXTRA_NONCE);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_merge_mining_tag, TX_EXTRA_MERGE_MINING_TAG);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_additional_pub_keys, TX_EXTRA_TAG_ADDITIONAL_PUBKEYS);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_mysterious_minergate, TX_EXTRA_MYSTERIOUS_MINERGATE_TAG);

