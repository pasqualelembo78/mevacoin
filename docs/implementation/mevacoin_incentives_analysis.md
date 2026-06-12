# ANALISI SISTEMA INCENTIVI FULL NODE + CONSENSO IBRIDO MEVACOIN

## EXECUTIVE SUMMARY

Mevacoin è derivato da Monero e utilizza Proof of Work (RandomX). L'obiettivo è implementare un **sistema parallelo di incentivi** che:
- Mantiene il consenso PoW invariato (mining rewards 100% agli attuali miner)
- Introduce un **Participation Incentive Layer** finanziato da una percentuale del block reward
- Premia i full node reali e attivi
- Previene Sybil attack e node farming
- Crea uno storico persistente di uptime e score

---

## 1. ARCHITETTURA GLOBALE

### 1.1 Fonte dei Rewards

```
Block Reward PoW (RandomX) = 100%
    ├── Mining Reward = 97% (GOES TO MINER)
    └── Network Incentive Fund = 3% (GOES TO PARTICIPATION ENGINE)

Participation Engine distributes 3% to:
    ├── Active Full Nodes (70%)
    ├── High Uptime Nodes (20%)
    └── Reserve Fund (10%)
```

**Rationale**: Il 3% è una cifra accettabile che non compromette i mining incentives ma fornisce finanziamento sufficiente per il network.

### 1.2 Componenti Principali

```
┌─────────────────────────────────────────────┐
│      MEVACOIND DAEMON                       │
├─────────────────────────────────────────────┤
│ Blockchain Engine (PoW maintained)          │
│  ├─ Block Validation                        │
│  ├─ Mining Reward Distribution              │
│  └─ Incentive Fund Accumulation             │
├─────────────────────────────────────────────┤
│ Participation Incentive Layer (NEW)         │
│  ├─ Node Registry                           │
│  ├─ Participation Score Engine              │
│  ├─ Availability Proof (PoA)                │
│  ├─ Reward Distribution Engine              │
│  └─ Badge System                            │
├─────────────────────────────────────────────┤
│ RPC API Extensions (NEW)                    │
│  ├─ get_participation_score                 │
│  ├─ get_node_uptime                         │
│  ├─ get_node_status                         │
│  ├─ register_node                           │
│  ├─ get_reward_history                      │
│  └─ get_badges                              │
├─────────────────────────────────────────────┤
│ P2P Extensions (MODIFIED)                   │
│  ├─ Node Challenge-Response (PoA)           │
│  ├─ Node Announcement                       │
│  └─ Peer Monitoring                         │
├─────────────────────────────────────────────┤
│ Database Layer (EXTENDED)                   │
│  ├─ Node Registry (LevelDB/LMDB)            │
│  ├─ Participation Scores                    │
│  ├─ Uptime Tracking                         │
│  └─ Reward History                          │
├─────────────────────────────────────────────┤
│ Wallet RPC Integration                      │
│  ├─ Wallet-Node Association                 │
│  ├─ Display Participation Data              │
│  └─ Manage Node Identity                    │
└─────────────────────────────────────────────┘
```

---

## 2. CURRENT STATE ANALYSIS

### 2.1 Code Structure

```
/tmp/mevacoin/
├── src/
│   ├── rpc/
│   │   ├── core_rpc_server.h/cpp (EXTEND)
│   │   ├── core_rpc_server_commands_defs.h (ADD NEW COMMANDS)
│   │   └── rpc_handler.h/cpp (EXTEND)
│   │
│   ├── p2p/
│   │   ├── net_node.h/cpp (MODIFY)
│   │   ├── net_node.inl (ADD CHALLENGE-RESPONSE)
│   │   ├── net_peerlist.h/cpp (EXTEND)
│   │   └── p2p_protocol_defs.h (ADD NEW MESSAGES)
│   │
│   ├── cryptonote_core/
│   │   ├── blockchain.h/cpp (MODIFY REWARD DISTRIBUTION)
│   │   ├── cryptonote_core.h/cpp (EXTEND)
│   │   └── tx_pool.h/cpp (NO CHANGE)
│   │
│   ├── cryptonote_basic/
│   │   ├── cryptonote_basic_impl.h/cpp (GET_BLOCK_REWARD - ANALYZE)
│   │   └── miner.h/cpp (NO CHANGE)
│   │
│   ├── wallet/
│   │   ├── wallet_rpc_server.h/cpp (EXTEND)
│   │   └── wallet2.h/cpp (EXTEND FOR NODE INFO)
│   │
│   └── participation/ (NEW DIRECTORY)
│       ├── node_registry.h/cpp
│       ├── participation_engine.h/cpp
│       ├── availability_proof.h/cpp
│       ├── badge_system.h/cpp
│       └── reward_distributor.h/cpp
│
├── utils/
│   └── systemd/ (MODIFY)
│       ├── mevacoind.service (ADD ENVIRONMENT VARS)
│       └── mevacoind-wallet.service (NEW)
│
└── docs/
    └── PARTICIPATION_SYSTEM.md (NEW)
```

### 2.2 Current RPC Endpoints (Sample)

```
GET_HEIGHT - Get blockchain height
GET_BLOCKS_FAST - Get blocks quickly
GET_TRANSACTIONS - Get transaction details
GET_MINING_STATUS - Get mining info
```

**Gap**: Nessun endpoint per tracking node participation.

### 2.3 Current P2P Protocol

**Monero LEVIN Protocol** used for node communication.

**Gap**: Nessun challenge-response per proof of availability.

### 2.4 Reward System (Current)

```cpp
// In cryptonote_basic_impl.cpp
bool get_block_reward(size_t median_weight, size_t current_block_weight, 
                      uint64_t already_generated_coins, uint64_t &reward, uint8_t version)
{
    uint64_t base_reward = (MONEY_SUPPLY - already_generated_coins) >> emission_speed_factor;
    // ... calculations ...
    reward = base_reward; // Goes 100% to miner
    return true;
}
```

**Current flow**:
- Block found by miner → Transaction with miner's address in vin_gen → Full reward to miner

**Required modification**:
- Block found by miner → Split reward into 97% miner + 3% incentive pool

---

## 3. DESIGN DECISIONS

### 3.1 Node Identification

**Problem**: Nessun meccanismo per identificare nodi unici e preveniare Sybil attacks.

**Solution**: Cryptographic Node Identity
```
NodeID = H(wallet_pubkey || node_pubkey || timestamp)
Sign(NodeID) = wallet_signature (proof of ownership)
```

**Benefits**:
- Non based on IP (unchanging)
- Non based on hostname (unchanging)
- Requires wallet private key to register node
- One wallet = one node registration per period

### 3.2 Participation Scoring

```
ParticipationScore = 
    (Uptime Score × 0.4) +
    (Sync Score × 0.3) +
    (Responsiveness Score × 0.2) +
    (Activity Score × 0.1)

Where:
- Uptime Score: Percentage of time node is online
- Sync Score: Blockchain synchronization percentage
- Responsiveness Score: Avg response time to PoA challenges
- Activity Score: Transaction relay, peer updates, etc.
```

### 3.3 Proof of Availability (PoA)

**Mechanism**: Random challenge-response protocol

```
1. Miner/Validator node sends random CHALLENGE:
   - Block height H (last 100 blocks)
   - Query: "What is block hash at height H?"
   
2. Target node must respond within 2 seconds:
   - RESPONSE: block hash H
   - Signature: sign(block_hash, node_private_key)
   
3. Validator checks:
   - Is response correct? (hash matches)
   - Is signature valid?
   - Is response time < 2s?
   - Is node synchronized?
```

**Frequency**: Every 10 hours, 3-5 random challenges per node

**Scoring**:
- Correct + fast response: +1.0 score
- Correct + slow (1-2s): +0.5 score
- Incorrect/No response: 0 score, uptime reset

### 3.4 Reward Distribution

```
Incentive Fund = Block Reward × 3%

Distribution happens EVERY 240 BLOCKS (~4 hours):
  1. Accumulate 3% rewards from 240 blocks
  2. Calculate each node's score for period
  3. Distribute pool proportionally:
     Reward_node = Pool × (NodeScore / TotalScores)
  4. Create individual transactions to node wallets
  5. Send with 10-block maturation delay
```

### 3.5 Badge System

**Persistent badges awarded automatically**:

```
Badge: ACTIVE_MINER
  - Earned: Mine 10 blocks total
  - Permanent (with caveats)

Badge: FULL_NODE_OPERATOR
  - Earned: 100h+ continuous uptime
  - Requirement: 99%+ sync

Badge: STABLE_NODE
  - Earned: 90 days + 95%+ uptime
  - Revoked if: Falls below 90% in 30-day window

Badge: CORE_NETWORK_NODE
  - Earned: 180 days + 99% uptime + 1000+ peer connections
  - Elite status

Badge: LONG_UPTIME_NODE
  - Earned: 365 days + 98%+ uptime
  - Annual renewal
```

### 3.6 Sybil Attack Prevention

```
Protection Layers:

1. Identity Verification
   - Signature required from wallet holding X MVC (configurable)
   - Cooldown: 1 hour between identity changes

2. PoA (Proof of Availability)
   - Node must respond correctly to 3/3 challenges in period
   - If fails 2/3: marked as suspicious, rewards frozen

3. Proof of Real Blockchain
   - Query: "What is tx hash for tx_index N?"
   - Node must have full UTXO set and transaction history

4. Reputation Scoring
   - Tracks node behavior over time
   - Multiple failures → reputation penalty
   - Low reputation → rewards halved

5. VPS Detection (Heuristics)
   - Monitor: same peer connections across "different" nodes
   - Monitor: identical bandwidth patterns
   - Monitor: clock skew patterns
   - Suspicious patterns → investigation + potential reward penalty
```

---

## 4. DATABASE SCHEMA

### 4.1 Node Registry Table

```
Table: node_registry
┌─────────────────────────────────────────────┐
│ Column              │ Type      │ Purpose   │
├─────────────────────────────────────────────┤
│ node_id (PRIMARY)   │ Hash256   │ Unique ID │
│ wallet_address      │ String    │ Owner     │
│ wallet_pubkey       │ Key       │ Identity  │
│ registered_height   │ uint64    │ When      │
│ registered_timestamp│ uint64    │ When      │
│ last_sync_height    │ uint64    │ Status    │
│ is_synchronized     │ bool      │ Status    │
│ ip_address          │ String    │ Network   │
│ port                │ uint16    │ Network   │
│ status              │ enum      │ Active/   │
│                     │           │ Suspended│
│ signature           │ Signature │ Proof     │
└─────────────────────────────────────────────┘
```

### 4.2 Participation Score Table

```
Table: participation_scores
┌──────────────────────────────────────────────┐
│ Column              │ Type     │ Purpose    │
├──────────────────────────────────────────────┤
│ node_id (PK)        │ Hash256  │ Reference  │
│ period_height (PK)  │ uint64   │ Time bucket│
│ uptime_score        │ float    │ 0.0-1.0    │
│ sync_score          │ float    │ 0.0-1.0    │
│ responsiveness      │ float    │ 0.0-1.0    │
│ activity_score      │ float    │ 0.0-1.0    │
│ total_score         │ float    │ Weighted   │
│ challenges_passed   │ uint32   │ Count      │
│ challenges_total    │ uint32   │ Count      │
└──────────────────────────────────────────────┘
```

### 4.3 Uptime History Table

```
Table: uptime_history
┌───────────────────────────────────────────┐
│ Column              │ Type    │ Purpose   │
├───────────────────────────────────────────┤
│ node_id (PK)        │ Hash256 │ Reference │
│ timestamp (PK)      │ uint64  │ Time      │
│ online              │ bool    │ Status    │
│ block_height        │ uint64  │ Sync      │
│ peer_count          │ uint32  │ Network   │
│ response_time_ms    │ uint32  │ Perf      │
└───────────────────────────────────────────┘

Retention: Keep last 90 days (pruned nightly)
```

### 4.4 Reward History Table

```
Table: reward_history
┌──────────────────────────────────────────────┐
│ Column              │ Type      │ Purpose   │
├──────────────────────────────────────────────┤
│ node_id (PK)        │ Hash256   │ Recipient │
│ reward_height (PK)  │ uint64    │ Time      │
│ reward_amount       │ uint64    │ Atomics   │
│ period_start        │ uint64    │ Context   │
│ period_end          │ uint64    │ Context   │
│ period_score        │ float     │ Context   │
│ tx_hash             │ Hash256   │ Block ref │
│ status              │ enum      │ Pending/  │
│                     │           │ Confirmed │
└──────────────────────────────────────────────┘
```

### 4.5 Badge Table

```
Table: node_badges
┌──────────────────────────────────────────┐
│ Column              │ Type     │ Purpose  │
├──────────────────────────────────────────┤
│ node_id (PK)        │ Hash256  │ Owner    │
│ badge_type (PK)     │ enum     │ Which    │
│ awarded_height      │ uint64   │ When     │
│ awarded_timestamp   │ uint64   │ When     │
│ is_active           │ bool     │ Status   │
│ revocation_reason   │ String   │ Why      │
│ metadata            │ JSON     │ Extra    │
└──────────────────────────────────────────┘
```

---

## 5. RPC API DESIGN

### 5.1 New Endpoints

```
// Register/Manage Node Identity
POST /register_node
  Request: {
    wallet_address: "Mx...",
    wallet_pubkey: "...",
    node_signature: "...",  // sign(wallet_address, wallet_privkey)
    node_pubkey: "...",
    port: 18080
  }
  Response: {
    status: "success",
    node_id: "hash256...",
    message: "Node registered successfully"
  }

// Get Node Participation Score
GET /get_participation_score
  Params: { node_id or wallet_address }
  Response: {
    node_id: "hash256...",
    current_score: 0.87,
    uptime_pct: 0.95,
    sync_pct: 0.99,
    responsiveness: 0.90,
    activity: 0.75,
    period: "current|recent",
    next_distribution: 2024
  }

// Get Node Uptime
GET /get_node_uptime
  Params: { node_id }
  Response: {
    node_id: "hash256...",
    total_uptime_days: 45.5,
    uptime_percentage: 0.923,
    consecutive_uptime_hours: 120,
    last_online: timestamp,
    next_check: timestamp,
    status: "online|offline|suspended"
  }

// Get Node Status
GET /get_node_status
  Params: { node_id or wallet_address }
  Response: {
    node_id: "hash256...",
    registered: true,
    is_online: true,
    sync_height: 2400000,
    chain_height: 2400050,
    sync_percentage: 0.999,
    peers_connected: 42,
    last_challenge: timestamp,
    challenges_passed: 12,
    challenges_total: 12,
    reputation_score: 0.95
  }

// Get Rewards History
GET /get_reward_history
  Params: { node_id or wallet_address, limit: 100 }
  Response: {
    node_id: "hash256...",
    total_rewards: "50.234",
    rewards: [
      {
        height: 2400000,
        amount: "0.234",
        timestamp: 1234567890,
        status: "confirmed",
        tx_hash: "..."
      }
    ]
  }

// Get Node Badges
GET /get_badges
  Params: { node_id or wallet_address }
  Response: {
    node_id: "hash256...",
    wallet_address: "Mx...",
    badges: [
      {
        type: "FULL_NODE_OPERATOR",
        awarded_at: timestamp,
        is_active: true
      },
      {
        type: "STABLE_NODE",
        awarded_at: timestamp,
        is_active: true
      }
    ]
  }

// Get Incentive Pool Status
GET /get_incentive_pool_status
  Response: {
    current_pool_balance: "123.456",
    pool_percentage: 0.03,
    blocks_accumulated: 12,
    blocks_until_distribution: 228,
    total_eligible_nodes: 342,
    estimated_next_distribution: timestamp
  }

// List All Eligible Nodes
GET /get_eligible_nodes
  Params: { limit: 100, offset: 0 }
  Response: {
    total_nodes: 542,
    nodes: [
      {
        node_id: "...",
        wallet_address: "Mx...",
        score: 0.87,
        uptime: 0.95,
        status: "online"
      }
    ]
  }
```

---

## 6. P2P PROTOCOL EXTENSIONS

### 6.1 New Protocol Messages

```cpp
// Message: NODE_ANNOUNCEMENT
struct node_announcement {
    node_id: Hash256
    wallet_pubkey: PublicKey
    port: uint16
    timestamp: uint64
    signature: Signature  // sign(node_id, wallet_privkey)
}

// Message: AVAILABILITY_CHALLENGE
struct availability_challenge {
    challenge_id: Hash256
    node_id: Hash256
    challenge_type: uint8  // 0=block_hash, 1=tx_hash, etc
    parameter: uint64     // block height, tx index, etc
    timestamp: uint64
}

// Message: AVAILABILITY_RESPONSE
struct availability_response {
    challenge_id: Hash256
    node_id: Hash256
    response_data: Hash256
    node_signature: Signature
    timestamp: uint64
}

// Message: PEER_STATUS_UPDATE
struct peer_status_update {
    node_id: Hash256
    sync_height: uint64
    peer_count: uint32
    timestamp: uint64
    signature: Signature
}
```

### 6.2 Challenge-Response Flow

```
Peer A (Validator) ──→ Peer B (Target)
        │
        └─→ CHALLENGE: "What is block hash at height 2400000?"
                       (+ random challenge_id)

Peer B (Target)
        │
        └─→ RESPONSE: "hash is 0xABCD1234..."
                      (signed by target node's key)

Peer A (Validator)
        │
        ├─→ Verify signature
        ├─→ Verify hash is correct
        ├─→ Record response time
        └─→ Update Peer B's score
```

---

## 7. SYSTEMD INTEGRATION

### 7.1 Current Service File

```ini
[Unit]
Description=Mevacoin Daemon
After=network.target

[Service]
ExecStart=/root/mevacoin/build/Linux/master/release/bin/mevacoind \
    --non-interactive \
    --rpc-bind-ip=0.0.0.0 \
    --rpc-bind-port=18081 \
    --confirm-external-bind
WorkingDirectory=/root/mevacoin/build/Linux/master/release/bin/
Restart=always
RestartSec=5
User=root
LimitNOFILE=4096

[Install]
WantedBy=multi-user.target
```

### 7.2 Required Modifications

```ini
[Unit]
Description=Mevacoin Daemon with Participation System
After=network.target

[Service]
# Enable Participation System
Environment="MEVACOIN_ENABLE_PARTICIPATION=1"
Environment="MEVACOIN_NODE_WALLET_ADDRESS=Mx..."
Environment="MEVACOIN_PARTICIPATION_BIND_PORT=18088"

# Database paths
Environment="MEVACOIN_NODE_REGISTRY_DB=/var/lib/mevacoin/node_registry"
Environment="MEVACOIN_PARTICIPATION_DB=/var/lib/mevacoin/participation"

ExecStart=/root/mevacoin/build/Linux/master/release/bin/mevacoind \
    --non-interactive \
    --rpc-bind-ip=0.0.0.0 \
    --rpc-bind-port=18081 \
    --participation-port=18088 \
    --enable-participation \
    --node-db-path=/var/lib/mevacoin/node_registry \
    --participation-db-path=/var/lib/mevacoin/participation \
    --confirm-external-bind

WorkingDirectory=/root/mevacoin/build/Linux/master/release/bin/
Restart=always
RestartSec=10
User=mevacoin
Group=mevacoin

# Resource limits
LimitNOFILE=8192
MemoryMax=4G
CPUQuota=80%

# Security
ProtectSystem=strict
ProtectHome=yes
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

### 7.3 New Service File: Wallet RPC

```ini
[Unit]
Description=Mevacoin Wallet RPC with Participation
After=mevacoind.service
Wants=mevacoind.service

[Service]
Environment="MEVACOIN_WALLET_RPC_BIND_IP=127.0.0.1"
Environment="MEVACOIN_WALLET_RPC_PORT=18082"
Environment="MEVACOIN_DAEMON_HOST=127.0.0.1:18081"
Environment="MEVACOIN_DAEMON_PARTICIPATION_PORT=18088"

ExecStart=/root/mevacoin/build/Linux/master/release/bin/mevacoin-wallet-rpc \
    --wallet-file=/home/mevacoin/.mevacoin/wallet \
    --password= \
    --rpc-bind-ip=127.0.0.1 \
    --rpc-bind-port=18082 \
    --daemon-host=127.0.0.1 \
    --daemon-port=18081 \
    --enable-participation-display

User=mevacoin
Group=mevacoin
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

---

## 8. IMPLEMENTATION PHASES

### Phase 1: Core Infrastructure (Weeks 1-2)
- Node registry database setup
- Participation score engine
- Basic RPC endpoints
- Badge system database

### Phase 2: Blockchain Integration (Weeks 3-4)
- Reward splitting in block validation
- Incentive pool accumulation
- Reward distribution transactions
- Chainstate persistence

### Phase 3: P2P & Monitoring (Weeks 5-6)
- Availability proof protocol
- Challenge-response mechanism
- Peer monitoring and scoring
- Sybil attack prevention

### Phase 4: Wallet Integration (Week 7)
- Wallet RPC extensions
- Badge display
- Participation data display
- Node management endpoints

### Phase 5: Testing & Hardening (Weeks 8-10)
- Unit tests
- Integration tests
- Security audit
- Performance optimization
- Documentation

---

## 9. SECURITY CONSIDERATIONS

### 9.1 Attack Vectors & Mitigations

| Attack | Vector | Mitigation |
|--------|--------|-----------|
| Sybil | Create 1000 nodes | Requires wallet sig + PoA verification |
| Eclipse | Isolate node | Peer connection monitoring |
| VPS Farming | Run 100s on VPS | Clock skew + peer pattern detection |
| Reward Theft | Steal node ID | Signature verification on all ops |
| Double Spend | Claim same reward | DB transaction atomicity |
| 51% Attack | Control chain | No changes to PoW consensus |

### 9.2 Cryptographic Requirements

```
1. Node Registration Signature
   Format: sign(node_id, wallet_private_key)
   Algo: Ed25519 (same as Mevacoin)
   
2. Availability Response Signature
   Format: sign(block_hash, node_private_key)
   Algo: Ed25519
   
3. Reward Authorization
   Signed by daemon, verified before distribution
```

### 9.3 Rate Limiting

```
- Register node: 1 per wallet per 1 hour
- Submit challenge response: 1 per peer per 5 seconds
- Query RPC endpoints: Standard DDoS limits apply
```

---

## 10. MONITORING & METRICS

### 10.1 Metrics to Track

```
Global Metrics:
- Active eligible nodes count
- Average network uptime
- Total incentive pool distributed
- Reward distribution frequency
- Average node response time

Per-Node Metrics:
- Uptime percentage (historical)
- Challenge success rate
- Network connectivity quality
- Sync status accuracy
- Reward accumulation
```

### 10.2 Alerting

```
- Node offline > 6 hours: Log warning
- Node loses sync for > 1 hour: Suspend rewards
- Challenge failure 2/3: Mark suspicious
- Reputation score < 0.5: Freeze rewards
```

---

## CONCLUSION

Questo sistema crea un'economia decentralizzata intorno ai full node di Mevacoin, incentivando la partecipazione alla rete senza compromettere la sicurezza PoW sottostante.

Il design è resistente agli attack comuni ed implementabile in fasi progressive.

