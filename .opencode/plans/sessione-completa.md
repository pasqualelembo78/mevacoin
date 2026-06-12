# Sessione MevaTrust Circles + Penalty — Backup Completo

## Come usare questo file

Apri opencode in `/root/mevacoin/` (senza `--plan`) e dì:

> "Leggi il file `.opencode/plans/sessione-completa.md` e implementa la Fase 1. Poi fammi riepilogo e chiedimi se proseguire."

Per fasi successive:

> "Fase 2: implementa tag 0xA3. Leggi il piano dal file `.opencode/plans/sessione-completa.md`."

---

## Cos'è MevaTrust

Sistema di badge/incentivi/partecipazione per nodi MevaCoin.

**Flusso generale:**
1. Nodo si registra on-chain (tag 0xA0 in tx_extra) → entra in `NodeRegistry`
2. Viene monitorato per uptime (`record_uptime_event`), challenge (`AvailabilityProofEngine`)
3. Guadagna reputation score (`node_registry.update_reputation`)
4. Riceve badge (`BadgeSystem`) quando merita
5. Ogni 240 blocchi: pool distribution → ricompense in coinbase TX (`RewardDistributor`)
6. Periodicamente: snapshot 0xA2 embeddato nel miner_tx con badge awards firmati
7. Ora stiamo aggiungendo: **Circle Registry** (micro-DAO) e attivando **penalty system**

---

## Storico Sessioni Complete

### Sessione 0 (esplorazione iniziale)
- Letto mevatrust nel core: 12 endpoints RPC, strutture request/response
- Esplorato tutta l'architettura mevatrust

### Sessione 1 (wallet GUI — network.cpp + jsonRpc)
- Creato `jsonRpc()` in `src/qt/network.h/.cpp` — POST JSON-RPC 2.0 via epee HTTP client
- `network.cpp` compila senza errori (GCC 11, C++17). Fix: body passato come `std::string` non `boost::asio::buffer`
- Contenuto di `jsonRpc()`:
  ```cpp
  bool Network::jsonRpc(const std::string& method, const std::string& params,
                        std::string& response, const std::string& daemon_addr) {
      // Costruisce JSON-RPC request: {"jsonrpc":"2.0","id":"0","method":method,"params":params}
      // epee::net_utils::http::http_simple_client
      // POST con content-type application/json
      // Parsa response body
  }
  ```

### Sessione 2 (wallet GUI — Node.qml)
- Creata `pages/Node.qml` con:
  - Pool status (pool MVC, blocchi rimanenti)
  - Stato nodo (registration, sync height, peer count)
  - Badge colorati (verde=good, giallo=silver, rosso=gold, badge_type→colore)
  - Storico incentivi (tabella: height, amount MVC)
  - Classifica nodi (per reputation score)
  - **Auto-discovery node_id**: `discoverMyNodes()` filtra `get_all_node_incentives` per `wallet_address`
- Navigazione: bottone "Node" in `LeftPanel.qml`, Ctrl+N, `MiddlePanel` state, `main.qml` handler, `qml.qrc`
- Full build fallisce solo su `openpgp.cpp` pre-esistente (C++17 feature in C++14) — non correlato

### Sessione 3 (esplorazione penalty + architettura cerchie)
- Scoperto codice morto penalty:
  - `calculate_reputation_impact()` mai chiamato
  - `ban_node()` dichiarato in `node_registry.h` ma non implementato in `.cpp`
  - `NodeStatus::SUSPENDED` mai assegnato
  - Badge revocation mai attivata
- Deciso: nessun admin globale unilaterale, governance decentrata via cerchie
- Discusso Circle Registry (LMDB) come primo passo
- Creato questo piano completo

---

## Architettura Repository

```
/root/mevacoin/                              ← core mevacoin
  src/
    cryptonote_core/mevatrust/               ← tutto mevatrust
      mevatrust_lmdb.h                       ← shared LMDB env
      node_registry.h/.cpp                   ← NodeRegistry
      mevatrust_engine.h/.cpp                ← MevaTrustEngine (challenge/reward)
      availability_proof.h/.cpp              ← AvailabilityProofEngine
      badge_system.h/.cpp                    ← BadgeSystem
      badge_challenge_window.h               ← costanti finestra challenge
      reward_distributor.h/.cpp              ← RewardDistributor
      mevatrust_manager.h/.cpp               ← orchestrator
      mevatrust_tx_parser.h/.cpp             ← parsing tx_extra
      mevatrust_coinbase_validator.h/.cpp    ← validazione coinbase
      snapshot_broadcaster.h/.cpp            ← P2P snapshot
      mevatrust_security.h                   ← sicurezza
      mevatrust_snapshot_antireplay.h        ← anti-replay
      mevatrust_types.h                      ← tipi condivisi
    cryptonote_basic/
      tx_extra.h                             ← tag 0xA0 0xA1 0xA2
    rpc/
      mevatrust_rpc_commands.h               ← strutture request/response
      core_rpc_server.h/.cpp                 ← handler RPC
    CMakeLists.txt                           ← sources
/tmp/wallet-repo/                            ← wallet GUI
  pages/Node.qml                             ← già implementata
  src/qt/network.h/.cpp                      ← jsonRpc() già funzionante
  qml.qrc                                    ← già registrato Node.qml
```

---

## Sottosistemi Esistenti (dettaglio)

### 1. NodeRegistry (`node_registry.h/.cpp`)

Registro nodi con LMDB. Database `DB_MT_NODES`, key=node_id[32], value=packed entry.

**Struct `NodeRegistryEntry`:**
```
crypto::hash node_id
string wallet_address, ip_address
crypto::public_key wallet_pubkey, node_pubkey
crypto::signature registration_signature
uint64_t registered_height, registered_timestamp, last_seen_timestamp, last_sync_height
uint16_t port
uint32_t peer_count, total_challenges, successful_challenges, disconnections
NodeStatus status     // ACTIVE=0, OFFLINE=1, SUSPENDED=2, BANNED=3
float reputation_score
uint64_t created_at, updated_at, last_challenge_time, next_challenge_time
uint64_t total_uptime_seconds
string metadata
```

**Metodi:**
- `register_node()` — anti-Sybil: max 3 per wallet, 1 per IP
- `unregister_node()`, `update_node_status()`, `update_node_heartbeat()`
- `update_node_sync()`, `update_node_challenge_result()`, `update_node_challenge_time()`
- `update_reputation()`, `reset_reputation_suspect()`
- `record_uptime_event()`, `expire_inactive_nodes()`
- `register_node_onchain()`, `deregister_node_onchain()`
- Query: `get_node_by_id()`, `get_node_by_wallet()`, `get_node_by_pubkey()`
- `get_active_nodes()`, `get_all_nodes()`, `get_synchronized_nodes()`, `get_nodes_by_status()`
- `get_active_node_count()`, `get_total_registered_nodes()`, `get_nodes_registered_this_period()`
- `get_network_average_uptime()`
- Persistenza: `load_from_disk()`, `save_to_disk()`, `clear_all()`
- Legacy: `migrate_legacy_file()` da `node_registry.dat`

**Codice morto in .h ma non implementato (da attivare in Fase 5):**
```cpp
bool ban_node(const crypto::hash&, const std::string& reason);   // dichiarato, non in .cpp
bool unban_node(const crypto::hash&);                             // dichiarato, non in .cpp
bool verify_node_signature(...);                                  // dichiarato, non in .cpp
bool is_wallet_pubkey_registered(const crypto::public_key&) const; // dichiarato, non in .cpp
bool prune_old_data(uint64_t before_height);                      // dichiarato, non in .cpp
```

### 2. MevaTrustEngine (`mevatrust_engine.h/.cpp`)

**LMDB databases:** `DB_MT_POOL` (PoolState), `DB_MT_REWARDS` (RewardRecord con DUPSORT)

**Responsabilità:**
- Gestione pool di ricompense
- Calcolo punteggi nodi per distribuzione
- `process_reward_period(height)` — esegue a ogni period boundary
- Caricamento/salvataggio stato pool da LMDB

### 3. AvailabilityProofEngine (`availability_proof.h/.cpp`)

**Responsabilità:**
- Genera e verifica proof di disponibilità per i nodi
- `generate_challenge(node_id)` — crea una sfida crittografica
- `verify_response(node_id, response)` — verifica la risposta

### 4. BadgeSystem (`badge_system.h/.cpp`)

**LMDB database:** `DB_MT_BADGES`

**Tipi badge (BadgeType):**
```
NONE=0, PARTICIPANT=1, CONTRIBUTOR=2, AMBASSADOR=3,
GUARDIAN=4, PIONEER=5, VETERAN=6, ELITE=7, LEGENDARY=8
```

**Metodi:**
- `award_badge(node_id, badge_type, height, reason)` — assegna badge
- `has_badge(node_id, badge_type)` — verifica
- `get_recently_awarded(from_height, to_height)` — badge nel periodo
- `evaluate_all_badges(height)` — valuta automaticamente a ogni period boundary
- `set_on_chain_callback(callback)` — notifica quando badge assegnato
- `load_from_disk()`, `save_to_disk()`

**Da implementare in Fase 5:**
```cpp
void revoke_badge(crypto::hash node_id, BadgeType bt, std::string reason);
```

### 5. RewardDistributor (`reward_distributor.h/.cpp`)

**Responsabilità:**
- Accumula reward pool da ogni blocco (pool_fraction% della block_reward)
- `calculate_pool_contribution(block_reward)` — quanta MVC va al pool
- `accumulate_reward(amount, height)` — accumula
- `is_distribution_due(height)` — ogni 240 blocchi
- `distribute_rewards(engine)` — distribuisce ai nodi
- `process_welcome_bonuses(height)` — bonus benvenuto per nuovi nodi
- `get_pending_coinbase_outputs()` — output da iniettare nella coinbase TX
- `set_pool_fraction(pct)`, `set_min_score(threshold)`, `set_distribution_period(blocks)`

### 6. SnapshotBroadcaster (`snapshot_broadcaster.h/.cpp`)

**Responsabilità (Fase 3/C4):**
- Broadcast P2P di snapshot 0xA2 con badge awards
- Ballot box: accumula voti da proposer distinti
- Quorum: ≥3 proposer distinti per finalizzare
- `broadcast_snapshot(height, period, node_count, badge_awards)` — invia P2P
- `on_receive_vote(snapshot)` — riceve voto, accumula, verifica firma
- `try_finalize()` — verifica quorum, chiama apply_func
- `set_apply_func(callback)` — quando quorum raggiunto, serializza blob 0xA2
- `set_broadcast_func(callback)` — per invio P2P
- Ballot box con anti-replay (max 4 periodi, max 64 proposer per badge)

### 7. MevaTrustTxParser (`mevatrust_tx_parser.h/.cpp`)

**Funzioni:**
```cpp
bool parse_mevatrust_registration_from_tx(const transaction& tx, tx_extra_mevatrust_registration& out);
bool parse_mevatrust_deregister_from_tx(const transaction& tx, tx_extra_mevatrust_deregister& out);
bool parse_mevatrust_snapshot_from_tx(const transaction& tx, tx_extra_mevatrust_snapshot& out);

bool build_mevatrust_registration_extra(const tx_extra_mevatrust_registration& reg, vector<uint8_t>& extra);
bool build_mevatrust_deregister_extra(const tx_extra_mevatrust_deregister& dereg, vector<uint8_t>& extra);
bool build_mevatrust_snapshot_extra(const tx_extra_mevatrust_snapshot& snap, vector<uint8_t>& extra);

bool verify_registration_signature(const tx_extra_mevatrust_registration& reg);
bool verify_deregister_signature(const tx_extra_mevatrust_deregister& dereg);

// Helper per message_hash
crypto::hash registration_message_hash(const crypto::hash& node_id, const crypto::public_key& node_pubkey);
crypto::hash deregister_message_hash(const crypto::hash& node_id);
```

**Da aggiungere in Fase 2:**
```cpp
bool parse_mevatrust_circle_from_tx(const transaction& tx, tx_extra_mevatrust_circle& out);
bool build_mevatrust_circle_extra(const tx_extra_mevatrust_circle& op, vector<uint8_t>& extra);
bool verify_circle_signature(const tx_extra_mevatrust_circle& op);
crypto::hash circle_message_hash(const crypto::hash& circle_id, uint8_t op_type,
                                 const crypto::public_key& target, const std::string& name);
```

### 8. MevaTrustCoinbaseValidator (`mevatrust_coinbase_validator.h/.cpp`)

Valida gli output coinbase prodotti dal RewardDistributor. Chiamato durante la verifica dei blocchi.

### 9. MevaTrustManager (`mevatrust_manager.h/.cpp`)

**Orchestrator — il punto di ingresso per tutto mevatrust.**

`init(data_dir, nettype, hf_version)`:
```
m_node_registry = make_shared<NodeRegistry>(p+"/db")
m_node_registry->load_from_disk()
m_mevatrust_engine = make_shared<MevaTrustEngine>(p+"/db", m_node_registry)
m_mevatrust_engine->load_from_disk()
m_availability_proof = make_shared<AvailabilityProofEngine>(p+"/db", m_node_registry)
m_badge_system = make_shared<BadgeSystem>(p+"/db", m_node_registry, m_mevatrust_engine)
m_badge_system->load_from_disk()
m_reward_distributor = make_shared<RewardDistributor>(p)
m_snapshot_broadcaster = make_unique<SnapshotBroadcaster>()
// + wiring callback badge_system→on_chain_cb
// + wiring snapshot_broadcaster→apply_func
```

**Metodi principali:**
- `on_new_block(height, block_reward, hf)`:
  - Accumula reward pool
  - A ogni period boundary (height%240==0):
    - `process_reward_period()`, `evaluate_all_badges()`, `trigger_distribution()`
    - Broadcast snapshot P2P
    - `build_pending_snapshot()` → blob 0xA2 per miner_tx
- `get_coinbase_rewards(height, total_reward, miner_out)`:
  - Restituisce NodeCoinbaseReward[] da iniettare nella coinbase TX
  - Appiattisce su 240 blocchi (per-block share)
- `process_mevatrust_txs(block, height)`:
  - Per ogni TX nel blocco:
    - Tag 0xA0: `m_node_registry->register_node_onchain()`
    - Tag 0xA1: `m_node_registry->deregister_node_onchain()`
    - Tag 0xA2: parsare snapshot, verificare firme badge, `m_badge_system->award_badge()`
  - Ogni 240 blocchi: `expire_inactive_nodes()`
- `rebuild_registry_from_chain(start, end)`: clear_all + re-process tutti i blocchi
- `consume_pending_snapshot_extra()`: restituisce blob 0xA2 per miner_tx (consume semantics)

---

## Tag tx_extra Esistenti

### 0xA0 — Registrazione nodo
```cpp
struct tx_extra_mevatrust_registration {
  crypto::hash node_id;          // H(wallet_pubkey || node_pubkey || timestamp)
  crypto::public_key wallet_pubkey;
  crypto::public_key node_pubkey;
  std::string wallet_address;
  uint32_t port;
  crypto::signature signature;   // sign(node_id||node_pubkey) con wallet key
};
```

### 0xA1 — Deregistrazione nodo
```cpp
struct tx_extra_mevatrust_deregister {
  crypto::hash node_id;
  crypto::public_key wallet_pubkey;
  crypto::signature signature;   // sign("deregister"||node_id) con wallet key
};
```

### 0xA2 — Snapshot periodico (badge on-chain)
```cpp
struct tx_extra_mevatrust_snapshot {
  struct BadgeAward {
    crypto::hash node_id;
    uint8_t badge_type;
    uint64_t awarded_height;
    crypto::public_key proposer_pubkey;
    crypto::signature proposer_sig;  // sign(node_id||badge_type||height)
  };
  uint64_t height;
  uint32_t period;
  uint32_t node_count;
  vector<BadgeAward> badge_awards;
};
```

### 0xA3 — Circle operations (DA IMPLEMENTARE in Fase 2)
```cpp
#define TX_EXTRA_TAG_MEVATRUST_CIRCLE 0xA3
struct tx_extra_mevatrust_circle {
  enum OpType : uint8_t { CREATE=0, JOIN=1, LEAVE=2, CHANGE_ADMIN=3, DISBAND=4 };
  OpType op_type;
  crypto::hash circle_id;         // per JOIN/LEAVE/CHANGE_ADMIN/DISBAND
  std::string circle_name;        // solo per CREATE
  crypto::public_key target_pubkey;
  crypto::public_key signer_pubkey;
  crypto::signature signature;
  // sign: H(op_type || circle_id || circle_name || target_pubkey) con signer_pubkey
};
```

---

## RPC Endpoints Esistenti (12)

**File:** `src/rpc/mevatrust_rpc_commands.h` + `core_rpc_server.h/.cpp`

Endpoints JSON-RPC già presenti:
1. `get_node_registration` — stato registrazione nodo
2. `register_node` — registra nodo
3. `deregister_node` — deregistra nodo
4. `update_node_heartbeat` — heartbeat
5. `update_node_sync_status` — sync status
6. `get_active_nodes` — lista nodi attivi
7. `get_node_by_id` — dettaglio nodo
8. `get_all_node_incentives` — tutti gli incentivi (include wallet_address in node_summary_t!)
9. `get_incentive_pool_status` — stato pool (NON include wallet_address)
10. `get_badge_status` — badge del nodo
11. `calculate_reputation_impact` — calcola impatto reputation
12. `get_network_stats` — statistiche rete

**Pattern per aggiungere nuovi comandi RPC:**
1. `mevatrust_rpc_commands.h` — struct request/response con BEGIN_KV_SERIALIZE_MAP
2. `core_rpc_server.h` — dichiarazione handler: `bool on_rpc_XXX(req, res, error, ctx)`
3. `core_rpc_server.cpp` — implementazione handler + registrazione nel JSON-RPC map

Template handler:
```cpp
bool core_rpc_server::on_rpc_XXX(const rpc_XXX::request& req, rpc_XXX::response& res,
                                  epee::json_rpc::error& er, const connection_context* ctx) {
    PERF_TIMER(on_rpc_XXX);
    auto mgr = mevatrust::get_manager();
    if (!mgr) { res.status = "ERROR MevaTrust not initialized"; return true; }
    // ... operazioni ...
    res.status = "OK";
    return true;
}
```

---

## Wallet GUI Stato Attuale

### network.h/.cpp (in `/tmp/wallet-repo/src/qt/`)

```cpp
class Network : public QObject {
    Q_OBJECT
public:
    // ...
    bool jsonRpc(const std::string& method, const std::string& params,
                 std::string& response, const std::string& daemon_addr = "http://127.0.0.1:18081");
};
```

Implementazione: POST HTTP con `epee::net_utils::http::http_simple_client`.
Body: `{"jsonrpc":"2.0","id":"0","method":"<method>","params":<params>}`.
Parsa response JSON, estrae `result` o `error`.

### Node.qml (in `/tmp/wallet-repo/pages/`)

Già implementata con:
- **Pool status**: pending pool MVC, blocks remaining, period progress bar
- **Stato nodo**: sync height, peer count, registration status
- **Badge colorati**: loading da `get_badge_status`, colore in base a badge_type
- **Storico incentivi**: tabella da `get_all_node_incentives`
- **Classifica**: ordinata per reputation score
- **Auto-discovery node_id**: `discoverMyNodes()` filtra `get_all_node_incentives` per `wallet_address`
- Navigazione: LeftPanel "Node" button, Ctrl+N shortcut

**QML registrato in qml.qrc**, MainPanel/MiddlePanel state handler in main.qml.

---

## Pattern LMDB (usare per CircleRegistry)

Da `node_registry.cpp` — usare ESATTAMENTE questo pattern.

### Helpers (in mevatrust_lmdb.h)
```cpp
lmdb_write_str(buf, s)     // uint16_t len + data
lmdb_read_str(p, end, s)   // inverso
lmdb_write_pod(buf, v)     // sizeof(T) bytes
lmdb_read_pod(p, end, v)   // inverso
```

### pack_entry
```cpp
static std::string pack_entry(const CircleEntry& e) {
    std::string b; b.reserve(256);
    b.append(reinterpret_cast<const char*>(e.circle_id.data), 32);
    lmdb_write_str(b, e.name);
    b.append(reinterpret_cast<const char*>(&e.admin_pubkey), sizeof(e.admin_pubkey));
    uint32_t mc = (uint32_t)e.members.size(); lmdb_write_pod(b, mc);
    for (auto& m : e.members) b.append(reinterpret_cast<const char*>(&m), sizeof(m));
    lmdb_write_pod(b, e.created_height); lmdb_write_pod(b, e.created_timestamp);
    lmdb_write_pod(b, e.updated_at);
    lmdb_write_str(b, e.metadata);
    return b;
}
```

### unpack_entry
```cpp
static bool unpack_entry(const void* data, size_t sz, CircleEntry& e) {
    const char* p = (const char*)data, *end = p + sz;
    if (p + 32 > end) return false;
    memcpy(e.circle_id.data, p, 32); p += 32;
    if (!lmdb_read_str(p, end, e.name)) return false;
    if (p + (int)sizeof(e.admin_pubkey) > end) return false;
    memcpy(&e.admin_pubkey, p, sizeof(e.admin_pubkey)); p += sizeof(e.admin_pubkey);
    uint32_t mc=0; if (!lmdb_read_pod(p, end, mc)) return false;
    e.members.resize(mc);
    for (uint32_t i=0; i<mc; ++i) {
        if (p + 32 > end) return false;
        memcpy(&e.members[i], p, 32); p += 32;
    }
    if (!lmdb_read_pod(p, end, e.created_height)) return false;
    if (!lmdb_read_pod(p, end, e.created_timestamp)) return false;
    if (!lmdb_read_pod(p, end, e.updated_at)) return false;
    if (!lmdb_read_str(p, end, e.metadata)) return false;
    return true;
}
```

### db_put
```cpp
static bool db_put(MDB_env* env, MDB_dbi dbi, const CircleEntry& e) {
    if (!env) return true;
    std::string val = pack_entry(e);
    MDB_val k{32, const_cast<void*>((const void*)e.circle_id.data)};
    MDB_val v{val.size(), const_cast<char*>(val.data())};
    MDB_txn* txn = MevaTrustLMDB::begin_write(env);
    int rc = mdb_put(txn, dbi, &k, &v, 0);
    if (rc == 0) { mdb_txn_commit(txn); return true; }
    mdb_txn_abort(txn);
    MERROR("mdb_put: " << mdb_strerror(rc)); return false;
}
```

### db_del
```cpp
bool CircleRegistry::db_del(const crypto::hash& circle_id) {
    if (!m_env_) return true;
    MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
    MDB_val k{32, const_cast<void*>((const void*)circle_id.data)};
    int rc = mdb_del(txn, m_dbi_, &k, nullptr);
    if (rc == 0 || rc == MDB_NOTFOUND) { mdb_txn_commit(txn); return true; }
    mdb_txn_abort(txn);
    MERROR("mdb_del: " << mdb_strerror(rc)); return false;
}
```

### load_from_disk (itera LMDB con cursor)
```cpp
bool CircleRegistry::load_from_disk() {
    if (!m_env_) return false;
    MDB_txn* txn = MevaTrustLMDB::begin_read(m_env_);
    MDB_cursor* cur = nullptr;
    if (mdb_cursor_open(txn, m_dbi_, &cur) != 0) { mdb_txn_abort(txn); return false; }
    MDB_val k, v; uint32_t loaded = 0;
    while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
        CircleEntry e{};
        if (!unpack_entry(v.mv_data, v.mv_size, e)) { MWARNING("Corrupt circle entry skipped"); continue; }
        std::string key = hk(e.circle_id);
        circles_[key] = e;
        name_to_id_[e.name] = key;
        ++loaded;
    }
    mdb_cursor_close(cur); mdb_txn_abort(txn);
    MINFO("Loaded " << loaded << " circles from LMDB");
    return true;
}
```

### save_to_disk (drop + re-put)
```cpp
bool CircleRegistry::save_to_disk() const {
    if (!m_env_) return false;
    std::lock_guard<std::mutex> lk(lock_);
    MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
    mdb_drop(txn, m_dbi_, 0);
    for (const auto& kv : circles_) {
        std::string val = pack_entry(kv.second);
        MDB_val mk{32, const_cast<void*>((const void*)kv.second.circle_id.data)};
        MDB_val mv{val.size(), const_cast<char*>(val.data())};
        if (mdb_put(txn, m_dbi_, &mk, &mv, 0) != 0) { mdb_txn_abort(txn); return false; }
    }
    mdb_txn_commit(txn);
    return true;
}
```

### compute_circle_id
```cpp
crypto::hash CircleRegistry::compute_circle_id(const std::string& name,
                                                const crypto::public_key& admin,
                                                uint64_t nonce) {
    std::string m;
    m.append(name);
    m.append(reinterpret_cast<const char*>(&admin), sizeof(admin));
    m.append(reinterpret_cast<const char*>(&nonce), sizeof(nonce));
    return crypto::cn_fast_hash(m.data(), m.size());
}
```

### hk
```cpp
static std::string hk(const crypto::hash& h) {
    return epee::string_tools::pod_to_hex(h);
}
```

---

## Decisioni di Architettura

1. **Wallet ↔ Core**: comunicazione via JSON-RPC HTTP (no dipendenze header). Il wallet chiama `http://<daemon>:18081/json_rpc`
2. **Nessun admin globale unilaterale**: ogni cerchia ha il suo admin eletto
3. **Penalità solo automatiche**: basate su regole oggettive, nessun admin globale che banna
4. **Circle Registry su LMDB**: stessa shared env di NodeRegistry, DBI separato `MT_CIRCLES`
5. **Atomic unit MVC**: dividere per 1e12 per display leggibile
6. **Coinbase output injection**: `get_coinbase_rewards()` → iniettato nella miner_tx da blockchain.cpp
7. **Snapshot 0xA2 embeddato**: `build_pending_snapshot()` al period boundary → `consume_pending_snapshot_extra()` chiamato prima di `construct_miner_tx()`
8. **Anti-replay snapshot**: period\|node_id\|badge_type come chiave univoca, ballot box con max 4 periodi
9. **C++17 standard**: il core compila con GCC e C++17 (attenzione: wallet ha target C++14)

---

## Codice Morto da Attivare (Fase 5)

Tutta roba già dichiarata ma mai chiamata/implementata:

### In node_registry.h (dichiarato) ma non in node_registry.cpp:
- `ban_node(hash, reason)` — setta BANNED + reason in metadata
- `unban_node(hash)` — ripristina ACTIVE
- `verify_node_signature(hash, sig, msg)` — verifica firma nodo
- `is_wallet_pubkey_registered(pubkey)` — check se wallet già registrato
- `prune_old_data(height)` — rimuove nodi vecchi

### Mai chiamati in nessun punto del codice:
- `calculate_reputation_impact()` — esiste in RPC ma mai chiamato dal manager
- `NodeStatus::SUSPENDED` — mai assegnato (definito ma inutilizzato)
- Badge revocation — BadgeSystem non ha `revoke_badge()`
- `MevaTrustEngine::process_reward_period()` — chiamato ma verifica se fa davvero tutto

### Da implementare in Fase 5:

```cpp
// Offense types
enum OffenseType {
    UPTIME_VIOLATION,        // -0.05
    CHALLENGE_FAILURE,       // -0.02
    SYNC_FAILURE,            // -0.03
    DOUBLE_REGISTRATION,     // -0.10
    BYZANTINE_BEHAVIOR,      // -0.25 → SUSPENDED
    MALICIOUS_ACTIVITY       // -0.50 → BANNED
};

float calculate_reputation_impact(OffenseType t);
// Applica delta a reputation_score via update_reputation()
// Se reputation < 0.2 → SUSPENDED
// Se SUSPENDED da >N blocchi → BANNED
// Se BANNED → revoca tutti i badge
```

---

## PIANO COMPLETO FASE 1 → FASE 7

---

### FASE 1: Circle Registry (LMDB)

**Cosa fare:** Creare LMDB-backed registry per cerchie, stile NodeRegistry.

**File da CREARE:**

**1.1** `src/cryptonote_core/mevatrust/circle_registry.h`

```cpp
#pragma once
struct MDB_env;
typedef unsigned int MDB_dbi;
#include <string> <map> <vector> <cstdint> <mutex>
#include "crypto/crypto.h"
#include "cryptonote_basic/tx_extra.h"

namespace cryptonote {

struct CircleEntry {
  crypto::hash circle_id;              // H(name || admin || nonce)
  std::string name;                    // max 32 char, univoco
  crypto::public_key admin_pubkey;
  std::vector<crypto::public_key> members;
  uint64_t created_height;
  uint64_t created_timestamp;
  uint64_t updated_at;
  std::string metadata;                // descrizione opzionale
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
  bool add_member(const crypto::hash& circle_id, const crypto::public_key& member_pubkey);
  bool remove_member(const crypto::hash& circle_id, const crypto::public_key& member_pubkey);
  bool change_admin(const crypto::hash& circle_id, const crypto::public_key& new_admin);
  bool disband_circle(const crypto::hash& circle_id);

  // Query
  bool get_circle(const crypto::hash& circle_id, CircleEntry& out) const;
  bool get_circle_by_name(const std::string& name, CircleEntry& out) const;
  std::vector<CircleEntry> list_circles() const;
  std::vector<CircleEntry> get_circles_for_member(const crypto::public_key& pubkey) const;
  bool is_member(const crypto::hash& circle_id, const crypto::public_key& pubkey) const;
  bool name_exists(const std::string& name) const;

  // Persistence
  bool load_from_disk();
  bool save_to_disk() const;
  bool clear_all();

private:
  std::string db_path_;
  MDB_env* m_env_{nullptr};
  MDB_dbi  m_dbi_{0};

  std::map<std::string, CircleEntry> circles_;
  std::map<std::string, std::string> name_to_id_;
  mutable std::mutex lock_;

  static crypto::hash compute_circle_id(const std::string&, const crypto::public_key&, uint64_t);
  static std::string hk(const crypto::hash&);
  static std::string pack_entry(const CircleEntry& e);
  static bool unpack_entry(const void* data, size_t sz, CircleEntry& e);
  bool db_put(const CircleEntry& e);
  bool db_del(const crypto::hash& circle_id);
};

} // namespace cryptonote
```

**1.2** `src/cryptonote_core/mevatrust/circle_registry.cpp`

Implementare seguendo ESATTAMENTE i pattern sopra (pack_entry, unpack_entry, db_put, db_del, load_from_disk, save_to_disk, compute_circle_id, hk).

`create_circle()`:
1. lock
2. name vuoto o >32 char → return zero hash
3. name_exists() → return zero hash (gia esiste)
4. compute_circle_id(name, admin_pubkey, time(nullptr))
5. CircleEntry: circle_id, name, admin=admin_pubkey, members=[admin_pubkey], created_height=height, created_timestamp=time(nullptr), updated_at=time(nullptr)
6. db_put() → fallisce → return zero hash
7. circles_[hk(circle_id)] = e; name_to_id_[name] = hk(circle_id)
8. MINFO("Circle created: " << name << " id=" << pod_to_hex(circle_id))
9. return circle_id

`add_member()`: lock → lookup → se già has_member → false → push → db_put → update_at → true
`remove_member()`: lock → lookup → se is_admin → false → remove from vector → db_put → true
`change_admin()`: lock → lookup → nuovo admin deve essere in members → set admin_pubkey → db_put → true
`disband_circle()`: lock → lookup → db_del → erase da circles_ e name_to_id_ → true

Include: `circle_registry.h`, `mevatrust_lmdb.h`, `misc_log_ex.h`, `string_tools.h`, `<ctime>`, `<algorithm>`, `<cstring>`, `<sys/stat.h>`

**File da MODIFICARE:**

**1.3** `mevatrust_lmdb.h` — dopo `DB_MT_WELCOME` aggiungere:
```cpp
static constexpr const char* DB_MT_CIRCLES = "MT_CIRCLES";
```

**1.4** `mevatrust_manager.h`:
- `#include "circle_registry.h"`
- Membro dopo `m_node_registry`: `std::shared_ptr<CircleRegistry> m_circle_registry;`
- Accessor dopo `node_registry()`: `circle_registry() const { return m_circle_registry; }`

**1.5** `mevatrust_manager.cpp`:
- In `init()` dopo `m_node_registry->load_from_disk()`:
  ```cpp
  m_circle_registry = std::make_shared<CircleRegistry>(p+"/db");
  m_circle_registry->load_from_disk();
  MINFO("CircleRegistry initialized");
  ```
- In `shutdown()`: `if (m_circle_registry) m_circle_registry->save_to_disk();`

**1.6** `CMakeLists.txt` — aggiungere `mevatrust/circle_registry.cpp`

**Verifica:** `make -j$(nproc)` — compila senza errori.

---

### FASE 2: Circle Tx Extra + On-Chain (tag 0xA3)

**2.1** `tx_extra.h`:
```cpp
#define TX_EXTRA_TAG_MEVATRUST_CIRCLE 0xA3

struct tx_extra_mevatrust_circle {
  enum OpType : uint8_t { CREATE=0, JOIN=1, LEAVE=2, CHANGE_ADMIN=3, DISBAND=4 };
  OpType op_type;
  crypto::hash circle_id;
  std::string circle_name;        // solo per CREATE
  crypto::public_key target_pubkey;
  crypto::public_key signer_pubkey;
  crypto::signature signature;

  BEGIN_SERIALIZE()
    VARINT_FIELD(op_type)
    FIELD(circle_id)
    FIELD(circle_name)
    FIELD(target_pubkey)
    FIELD(signer_pubkey)
    FIELD(signature)
  END_SERIALIZE()
};
```

**2.2** `mevatrust_tx_parser.h`:
```cpp
bool parse_mevatrust_circle_from_tx(const transaction& tx, tx_extra_mevatrust_circle& out);
bool build_mevatrust_circle_extra(const tx_extra_mevatrust_circle& op, std::vector<uint8_t>& extra);
bool verify_circle_signature(const tx_extra_mevatrust_circle& op);

inline crypto::hash circle_message_hash(const crypto::hash& cid, uint8_t op_type,
                                         const crypto::public_key& target,
                                         const std::string& name) {
    std::string m;
    m.push_back(op_type);
    m.append(reinterpret_cast<const char*>(cid.data), sizeof(cid.data));
    m.append(reinterpret_cast<const char*>(&target), sizeof(target));
    m.append(name);
    return crypto::cn_fast_hash(m.data(), m.size());
}
```

**2.3** `mevatrust_tx_parser.cpp`: implementare parsing/serializzazione tag 0xA3

**2.4** `mevatrust_manager.cpp` in `process_mevatrust_txs()`: dopo blocco 0xA2, aggiungere:
```cpp
{
    tx_extra_mevatrust_circle op;
    if (mevatrust::parse_mevatrust_circle_from_tx(tx, op)) {
        if (!mevatrust::verify_circle_signature(op)) {
            MWARNING("Circle op signature INVALIDA"); continue;
        }
        switch (op.op_type) {
            case tx_extra_mevatrust_circle::CREATE:
                m_circle_registry->create_circle(op.circle_name, op.signer_pubkey, height);
                break;
            case tx_extra_mevatrust_circle::JOIN:
                m_circle_registry->add_member(op.circle_id, op.target_pubkey);
                break;
            case tx_extra_mevatrust_circle::LEAVE:
                m_circle_registry->remove_member(op.circle_id, op.target_pubkey);
                break;
            case tx_extra_mevatrust_circle::CHANGE_ADMIN:
                m_circle_registry->change_admin(op.circle_id, op.target_pubkey);
                break;
            case tx_extra_mevatrust_circle::DISBAND:
                m_circle_registry->disband_circle(op.circle_id);
                break;
        }
    }
}
```

---

### FASE 3: Circle RPC Endpoints

**File:** `mevatrust_rpc_commands.h`, `core_rpc_server.h/.cpp`

Endpoints JSON-RPC da aggiungere:

| Comando | Descrizione |
|---------|------------|
| `circle_create` | Crea cerchia (invia tx 0xA3) |
| `circle_join` | Admin aggiunge membro |
| `circle_leave` | Membro esce |
| `circle_change_admin` | Cambia admin |
| `circle_disband` | Scioglie cerchia |
| `circle_info` | Dettagli cerchia |
| `circle_list` | Lista cerchie (filtro per wallet_address) |

Ogni response ha formato: `{status: "OK"}` o `{status: "ERROR", message: "..."}`

Per `circle_create`/`circle_info`/`circle_list` è lettura (lookup su registry).
Per `circle_join`/`circle_leave`/`change_admin`/`disband` richiede costruzione tx 0xA3 e invio via relay? O solo modifica locale? Da decidere.

---

### FASE 4: Admin Actions (controlli firma)

Rafforzare `circle_registry.cpp`:
- Solo admin può: `add_member`, `remove_member`, `change_admin`, `disband`
- Verificare firma in `process_mevatrust_txs()` prima di applicare
- `LEAVE`: può farlo il membro stesso o l'admin

---

### FASE 5: Global Penalty System

**5.1** Implementare in `node_registry.cpp`:
```cpp
bool NodeRegistry::ban_node(const crypto::hash& nid, const std::string& reason) {
    return update_node_status(nid, NodeStatus::BANNED, reason);
}
bool NodeRegistry::unban_node(const crypto::hash& nid) {
    return update_node_status(nid, NodeStatus::ACTIVE, "unbanned");
}
bool NodeRegistry::is_wallet_pubkey_registered(const crypto::public_key& pk) const {
    for (auto& kv : nodes_)
        if (memcmp(&kv.second.wallet_pubkey, &pk, sizeof(pk)) == 0) return true;
    return false;
}
```

**5.2** Creare `calculate_reputation_impact()` in `mevatrust_engine.cpp` (o nuovo `penalty.cpp`):
```cpp
enum OffenseType {
    UPTIME_VIOLATION, CHALLENGE_FAILURE, SYNC_FAILURE,
    DOUBLE_REGISTRATION, BYZANTINE_BEHAVIOR, MALICIOUS_ACTIVITY
};

bool apply_penalty(const crypto::hash& node_id, OffenseType offense, uint64_t height) {
    float delta;
    NodeStatus new_status = NodeStatus::ACTIVE;
    switch (offense) {
        case UPTIME_VIOLATION:    delta = -0.05f; break;
        case CHALLENGE_FAILURE:   delta = -0.02f; break;
        case SYNC_FAILURE:        delta = -0.03f; break;
        case DOUBLE_REGISTRATION: delta = -0.10f; break;
        case BYZANTINE_BEHAVIOR:  delta = -0.25f; new_status = NodeStatus::SUSPENDED; break;
        case MALICIOUS_ACTIVITY:  delta = -0.50f; new_status = NodeStatus::BANNED; break;
    }
    auto mgr = mevatrust::get_manager();
    if (!mgr || !mgr->node_registry()) return false;
    // Update reputation
    mgr->node_registry()->update_reputation(node_id, delta);
    // Check threshold
    NodeRegistryEntry entry;
    if (mgr->node_registry()->get_node_by_id(node_id, entry)) {
        if (entry.reputation_score < 0.2f && new_status == NodeStatus::ACTIVE)
            new_status = NodeStatus::SUSPENDED;
        if (new_status != NodeStatus::ACTIVE)
            mgr->node_registry()->update_node_status(node_id, new_status, "auto-penalty");
        // Revoca badge se BANNED
        if (new_status == NodeStatus::BANNED && mgr->badge_system()) {
            // revoca tutti i badge del nodo
        }
    }
    return true;
}
```

**5.3** Agganciare in `mevatrust_manager.cpp`:
- `process_mevatrust_txs()`: dopo registrazione doppia → DOUBLE_REGISTRATION; dopo snapshot con firma invalida → BYZANTINE_BEHAVIOR
- `on_new_block()`: ogni 240 blocchi, nodi SUSPENDED da >X blocchi → BANNED

**5.4** `badge_system.cpp`: implementare `revoke_badge(node_id, badge_type, reason)`

**5.5** Storico penalità: nuovo LMDB DBI `MT_PENALTIES`

---

### FASE 6: Wallet GUI — Circle.qml

Nel repo `/tmp/wallet-repo/`:
- Creare `pages/Circle.qml`
- Lista cerchie dell'utente (via `circle_list` RPC filtrato per wallet_address)
- Bottone "Crea Cerchia" → dialog con nome
- Dettaglio cerchia: nome, admin (con badge), lista membri
- Azioni admin: aggiungi/rimuovi membro, cambia admin, sciogli
- Navigazione: LeftPanel, Ctrl+Shift+C, main.qml, qml.qrc

---

### FASE 7: Wallet GUI — Node.qml miglioramenti

- Mostrare a quali cerchie appartiene ogni nodo
- Filtro classifica per cerchia
- Reputation score con colore: verde>0.7, giallo>0.4, rosso<0.4
- NodeStatus con icona (ACTIVE=verde, OFFLINE=grigio, SUSPENDED=arancione, BANNED=rosso)
- Storico penalità (tabella cronologica)
- Dashboard cerchia nel profilo nodo

---

## Pattern Aggiunta Comando RPC (core)

File da toccare per ogni nuovo comando:

1. **`mevatrust_rpc_commands.h`** — struct request/response
```cpp
struct rpc_comando {
  struct request {
    std::string campo;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(campo) END_KV_SERIALIZE_MAP()
  };
  struct response {
    std::string status;
    BEGIN_KV_SERIALIZE_MAP() KV_SERIALIZE(status) END_KV_SERIALIZE_MAP()
  };
};
```

2. **`core_rpc_server.h`**:
```cpp
bool on_rpc_comando(const rpc_comando::request&, rpc_comando::response&,
                    epee::json_rpc::error&, const connection_context*);
```

3. **`core_rpc_server.cpp`**:
```cpp
bool core_rpc_server::on_rpc_comando(...) {
    PERF_TIMER(on_rpc_comando);
    auto mgr = mevatrust::get_manager();
    if (!mgr) { res.status = "ERROR MevaTrust not initialized"; return true; }
    // implementazione
    res.status = "OK";
    return true;
}
```
+ registrazione nel map `m_command_map` in `init()`.

---

## Note Finali

- **Compilazione:** Usare `make -j$(nproc)` nella root del repo.
- **C++ standard:** Core compila con C++17. Wallet ha target C++14 (openpgp.cpp fallisce).
- **LMDB:** shared env reference-counted in `MevaTrustLMDB::open()` / `release()`.
- **`maxdbs`:** attualmente 8 in `mevatrust_lmdb.h` — se servono più DBI, aumentare a 12.

### Database LMDB attuali (7)
```
MT_NODES       — NodeRegistry
MT_POOL        — MevaTrustEngine (PoolState)
MT_REWARDS     — MevaTrustEngine (RewardRecord, DUPSORT)
MT_BADGES      — BadgeSystem
MT_ANTIREPLAY  — SnapshotAntireplay
MT_WELCOME     — (welcome bonus tracking)
MT_CIRCLES     — CircleRegistry (DA AGGIUNGERE in Fase 1)
```
Massimo corrente: 8 (`maxdbs=8`) — sufficiente. Se in Fase 5 aggiungiamo `MT_PENALTIES`, va bene ancora.

---

## FINE PIANO
