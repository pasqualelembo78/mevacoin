# MevaCoin Codebase Analysis — Comprehensive Reference

## Current Objective
Fully analyze and document the MevaCoin cryptocurrency codebase, with emphasis on the MevaTrust marketplace/escrow subsystem, blockchain core, P2P network, transaction validation, and all data serialization formats.

---

## Architecture Overview

MevaCoin is a Monero (Monero) hard fork with an integrated on-chain marketplace (MevaTrust), a node reputation/staking system, a circle/micro-DAO governance system, and a FROST-threshold-signed pool distribution mechanism. All subsystems are embedded in the standard Monero transaction structure via `tx_extra` fields and validated at the consensus level from HF_VERSION_MEVATRUST_VALIDATION = 13.

### Directory Layout (src/)
```
src/
├── cryptonote_basic/        # Core tx types, tx_extra definitions
├── cryptonote_core/         # Blockchain, tx validation, mevatrust/ subsystem
│   ├── mevatrust/           # All MevaTrust subsystems (33 files)
│   ├── cryptonote_tx_utils.cpp   # Miner tx construction with pool split
│   └── tx_verification_utils.cpp # Tx validation
├── rpc/                     # JSON-RPC handlers + command definitions
├── simplewallet/            # CLI wallet + MevaTrust commands
├── p2p/                     # P2P network (node discovery, sync)
├── blockchain_db/           # LMDB blockchain storage
├── crypto/                  # Crypto primitives (ed25519, FROST helpers)
├── serialization/           # Binary/KV serialization framework
├── ringct/                  # RingCT (Confidential Transactions)
├── device/                  # Hardware wallet abstraction
└── tests/                   # Test suite (no dedicated MevaTrust tests found)
```

### Data Storage
All MevaTrust state is stored in LMDB at `{data_dir}/mevatrust/lmdb/` via `MevaTrustLMDB` — a reference-counted singleton environment wrapper (`mevatrust_lmdb.h`). Named databases:

| DBI name | Used by | Key | Value |
|---|---|---|---|
| `MT_NODES` | NodeRegistry | node_id[32] | packed NodeRegistryEntry |
| `MT_POOL` | RewardDistributor | "pool"[4] | PoolState |
| `MT_REWARDS` | RewardDistributor | node_id[32] | RewardRecord (DUPSORT) |
| `MT_BADGES` | BadgeSystem | node_id[32] | packed badges |
| `MT_ANTIREPLAY` | SnapshotAntireplay | period[4]\|\|height[8] | height[8] |
| `MT_WELCOME` | RewardDistributor | node_id[32] | WelcomeBonus |
| `MT_CIRCLES` | CircleRegistry | circle_id[32] | CircleEntry |
| `MT_PENALTIES` | Penalty | node_id[32] | PenaltyEntry |
| `MT_STORES` | StoreRegistry | store_id[32] | StoreEntry |
| `MT_ITEMS` | StoreRegistry | item_id[32] | StoreItemEntry |
| `MT_PURCHASES` | StoreRegistry | buyer_pubkey[32] | StorePurchaseEntry (DUPSORT) |
| `MT_CIRCLE_PROPOSALS` | CircleRegistry | proposal_id[32] | ProposalEntry |
| `MT_CIRCLE_VOTES` | CircleRegistry | proposal_id[32] | VoteEntry |
| `MT_CHALLENGES` | AvailabilityProof | challenge_id[32] | AvailabilityChallenge |
| `MT_CHALLENGE_HISTORY` | AvailabilityProof | node_id[32] | ChallengeRecord |

---

## MevaTrust tx_extra Tags (0xA0–0xAD)

All tags are embedded in the `tx_extra` field of regular transactions (typically self-sends or payments). Defined in `tx_extra.h:55-68` and `mevatrust_tx_parser.h`.

| Tag | Name | Struct | Purpose |
|---|---|---|---|
| 0xA0 | REGISTRATION | `tx_extra_mevatrust_registration` | Register a node (node_id, wallet pubkey, node_pubkey, PoA key, C3 anti-Sybil proof) |
| 0xA1 | DEREGISTER | `tx_extra_mevatrust_deregister` | Unregister a node (node_id, signature) |
| 0xA2 | SNAPSHOT | `tx_extra_mevatrust_snapshot` | Period snapshot (badge awards, proposer consensus) |
| 0xA3 | CIRCLE | `tx_extra_mevatrust_circle` | Circle ops (create/disband/join/leave/change_admin/propose_member) |
| 0xA4 | PENALTY | `tx_extra_mevatrust_penalty` | Node penalty (offense type, amount, reason) |
| 0xA5 | UPTIME | `tx_extra_mevatrust_uptime` | Node uptime commitment (seconds, peer count, sync status) |
| 0xA6 | CHALLENGE | `tx_extra_mevatrust_challenge` | PoA challenge result (challenger, challenged, success, response_time_ms) |
| 0xA7 | STATE_ROOT | `tx_extra_mevatrust_state_root` | MevaTrust state Merkle root (fork resistance: included in every coinbase at HF13+) |
| 0xA8 | STORE | `tx_extra_mevatrust_store` | Store marketplace operations (see below) |
| 0xA9 | CIRCLE_VOTE | `tx_extra_mevatrust_circle_vote` | Circle proposal vote (yes/no, Italian-model quorum) |
| 0xAA | POOL_DISTRIBUTION | `tx_extra_mevatrust_pool_distribution` | Pool reward distribution (FROST 3/5 threshold-signed) |
| 0xAB | VALIDATOR | `tx_extra_mevatrust_validator` | Validator promotion (node_id, stake optional) |

### Store Operations (tag 0xA8) — `tx_extra_mevatrust_store`
```
enum Operation : uint8_t {
    STORE_CREATE   = 0,  // Create new store
    STORE_UPDATE   = 1,  // Update store details
    ITEM_LIST      = 2,  // List new item for sale
    ITEM_DELIST    = 3,  // Remove item from sale
    ITEM_BUY       = 4,  // Purchase item (funds sent to seller with protocol info)
    STORE_DEACTIVATE = 5 // Deactivate store
};
```

Fields: op, store_id, item_id, name, description, url, price, quantity, category, metadata, payment_address, owner_pubkey, owner_sig, buyer_pubkey, euro_enabled, euro_details, mvc_percent, euro_percent, payment_mode, buyer_payment_method, euro_ref, euro_amount.

Euro split mode: when `euro_enabled` is true, `mvc_percent` + `euro_percent` must equal 100 and `mvc_percent` must be 1-100.

Purchase flow:
1. **Buyer** sends ITEM_BUY tx — `buyer_pubkey` set, `buyer_payment_method` chosen, `euro_ref`/`euro_amount` for off-chain Euro portion
2. **Protocol**: funds escrowed in the transaction (sent to seller's address with the tx_extra carrying protocol metadata); seller sees `buyer_pubkey` in tx_extra
3. **Seller** confirms (tag 0xA9 CONFIRM — via `tx_extra_mevatrust_store_confirm`) or cancels (tag 0xAA CANCEL — via `tx_extra_mevatrust_store_cancel`) the purchase
4. **Timeout**: if seller does not respond within expiry window, buyer may claim refund

---

## Subsystem Details

### 1. MevaTrustManager (`mevatrust_manager.h/.cpp`)
Central orchestrator that owns all subsystem shared_ptrs:
- `NodeRegistry`, `CircleRegistry`, `MevaTrustEngine`, `AvailabilityProofEngine`, `BadgeSystem`, `RewardDistributor`, `StoreRegistry`
- `SnapshotBroadcaster` (unique_ptr) for P2P snapshot quorum
- `on_new_block()` — triggers period processing every 240 blocks (m_period_length), snapshot building, pending extra management
- `on_mevatrust_block_popped()` — reorg rollback via `PoppedOp` queue (tracks 18 operation types)
- `get_coinbase_rewards()` — resolves node rewards for current height
- `compute_mevatrust_state_root()` — Merkle commitment to MevaTrust state (included as 0xA7 in coinbase)
- `submit_snapshot_for_quorum()` — multi-proposer ballot accumulator (≥3/5 distinct proposers required for on-chain badge award)
- `consume_pending_snapshot_extra()` / `consume_pending_pool_distribution_extra()` — feeds 0xA2/0xAA blobs into miner_tx

### 2. NodeRegistry (`node_registry.h/.cpp`)
Persistent node registry (LMDB-backed). Each `NodeRegistryEntry`:
- `node_id` (hash), `wallet_pubkey`, `node_pubkey` (separate Ed25519 P2P signing key)
- `registration_signature` (proves wallet ownership)
- `status`: ACTIVE, OFFLINE, SUSPENDED, BANNED, UNREGISTERED
- `heartbeat_tracking`: last_seen, last_uptime_commitment, last_challenge_result
- `poa_key`: Proof-of-Authority Ed25519 keypair for P2P challenges
- PoA mode: when `use_poa` is true, challenges are signed by the node's Ed25519 key (not spend key)
- `peer_address` / `peer_port` for P2P routing
- Registration requires C3 anti-Sybil: ≥10 MVC balance demonstrated via UTXO signature proof

### 3. CircleRegistry (`circle_registry.h/.cpp`)
Micro-DAO governance system. `CircleEntry`:
- `name`, `admin_pubkey`, `member_pubkeys`, `metadata`
- `CircleProposal` for member expulsion with voting
- Italian-model voting: 2/3 quorum in first convocation, 1/3 in second convocation
- Vote window: 10080 blocks (≈7 days at 1 block/min)
- Seven operations encoded in CircleOp::Operation: CREATE, DISBAND, JOIN, LEAVE, CHANGE_ADMIN, PROPOSE_MEMBER, VOTE_MEMBER

### 4. MevaTrustEngine (`mevatrust_engine.h/.cpp`)
Node scoring engine with four weighted components:
- **Uptime** (weight 0.40): percentage of time node is reachable and responsive
- **Sync** (weight 0.30): how well the node stays synced to chain tip
- **Responsiveness** (weight 0.20): response time to challenges (faster = better)
- **Activity** (weight 0.10): participation in network activities (badges, challenges, etc.)
- Period length: 240 blocks
- Minimum score for rewards: 0.5
- Updates triggered by `process_uptime()`, `process_challenge()`, `process_badge()` calls

### 5. BadgeSystem (`badge_system.h/.cpp`)
11 badge types with progressive difficulty:

| Badge | Threshold Score | Min Uptime % | Min Challenges | Other |
|---|---|---|---|---|
| WELCOME | — | — | — | Auto-granted on registration |
| EARLY_ADOPTER | — | — | — | Granted by snapshot |
| ACTIVE_MINER | 0.75 | 85 | 10 | — |
| STABLE_NODE | 0.85 | 92 | 20 | — |
| RELIABLE_NODE | 0.90 | 95 | 30 | — |
| TRUSTED_NODE | 0.95 | 98 | 50 | 0 penalties |
| VETERAN_NODE | 0.97 | 99 | 100 | No penalties in 30d |
| CORE_NODE | 0.98 | 99 | 200 | 100% wins in challenges |
| ELITE_VALIDATOR | 0.99 | 99.5 | 500 | Validator status |
| LEGENDARY_NODE | 0.999 | 99.9 | 1000 | Top 1% score |
| PERFECT_NODE | 1.0 | 100 | 2000 | Zero penalties ever |

Each `BadgeRequirements` defines: `required_score`, `required_uptime_pct`, `required_challenges`, `required_consecutive_challenge_wins`, `max_penalties`, `penalty_freshness_blocks`.

### 6. RewardDistributor (`reward_distributor.h/.cpp`)
Pool reward tracking and distribution logic.
- `calculate_pool_contribution()`: returns `block_reward * m_pool_fraction / 100` (hardcoded 3%)
- `distribute_rewards()`: distributes accumulated pool to nodes based on MevaTrustEngine scores
- `schedule_welcome_bonus()` / `process_welcome_bonuses()`: new node incentive
- `get_pending_coinbase_outputs()`: returns `PendingCoinbaseOutput` vector for coinbase construction
- `is_distribution_due()`: checks if `current_height - m_last_distribution_height >= m_distribution_period`
- LMDB DBs: `MT_POOL` (pool state), `MT_REWARDS` (reward records), `MT_WELCOME` (welcome bonuses)

### 7. StoreRegistry (`mevatrust_store_registry.h/.cpp`)
Complete on-chain store marketplace with 863 lines of implementation.

**Data Structures:**
- `StoreEntry`: store_id, name, description, url, owner_pubkey, payment_address, euro_enabled/mvc_percent/euro_percent, created_height, active, item_count
- `StoreItemEntry`: item_id, store_id, name, description, price, quantity, category, metadata, payment_mode ("mvc_only" or "mvc_euro"), active, listed_height
- `StorePurchaseEntry`: store_id, item_id, buyer_pubkey, purchase_height, purchase_timestamp

**Operations:**
- `create_store()`: validates euro percentages (sum=100, mvc>0), generates store_id via XOR of `cn_fast_hash(owner_pubkey)` and `cn_fast_hash(name||height)`
- `update_store()`: owner-only, validates percentages
- `deactivate_store()`: owner-only soft-delete
- `list_item()`: validates store active, MAX_ITEMS_PER_STORE limit, price>0, auto-sets payment_mode=mvc_only if euro disabled, generates item_id hash
- `delist_item()`: owner-only, decrements item_count
- `buy_item()`: decrements quantity, auto-delists at 0, records purchase, prevents duplicate purchase
- `list_stores()`: paginated with category/price-range filters, 5 sort modes (price_asc/desc, newest, oldest, name, relevance)
- `search_stores()`: keyword search across store name/desc/url + item name/desc/category, same filter/sort/paginate
- `list_categories()`: deduplicated from active items
- Reorg helpers: `restore_item()`, `reverse_purchase()`

**Serialization format (manual binary packing):**
- Entries have version byte for forward compatibility
- Strings: 2-byte length prefix + UTF-8 data
- Pods: raw binary
- LMDB keys: 32-byte hash IDs

### 8. AvailabilityProofEngine (`availability_proof.h/.cpp`)
Proof-of-Availability challenge system for verifying node liveness.

**Challenge Types:**
- BLOCK_HASH (0): "What is block hash at height X?" — verified against chain via `get_block_hash_`
- BLOCK_HEIGHT (1): "What height is block with hash Y?"
- TX_HASH (2): "What tx hash at index X in block Y?"
- TX_EXISTS (3): "Does tx Y exist in chain?"
- UTXO_EXISTS (4): "Does UTXO Y exist?" — Merkle proof verification
- LAST_BLOCK_HASH (5): "What is current tip?"

**Parameters:**
- Default interval: 10 hours
- Challenges per period: 3–5
- Response timeout: 2000ms
- Success rate threshold: 0.66 (2/3)
- Max consecutive failures: 3
- Reputation penalty per failure: 0.05, reward per success: 0.02

**Callbacks for chain data access:**
- `GetBlockHashFunc`, `GetBlockHeightFunc`, `GetTxHashFunc` — set by blockchain layer

### 9. SnapshotBroadcaster (`snapshot_broadcaster.h`)
Multi-proposer quorum ballot system for badge awards.
- 5 proposers, threshold of 3
- BALLOT_TTL_SECONDS = 300 (5 minutes)
- Each proposer submits snapshot; broadcaster accumulates votes
- When ≥3 distinct proposers agree, badge award is finalized

### 10. Penalty System (`penalty.h/.cpp`)
Six offense types with `apply_penalty()`:
- UPTIME_VIOLATION, CHALLENGE_FAILURE, SYNC_FAILURE
- DOUBLE_REGISTRATION, BYZANTINE_BEHAVIOR, MALICIOUS_ACTIVITY

### 11. FROST Threshold Signatures (`frost_threshold.h`)
3-of-5 threshold Schnorr signatures for pool distribution (based on CFRG FROST draft).
- `FROST_N` = 5 proposers, `FROST_T` = 3 threshold
- Round 1: nonce generation (hiding + binding)
- Round 2: partial signature signing with Lagrange coefficients
- Coordinator: aggregate partials into `FrostSignature` (R, z, msg_hash)
- Verifiable as standard Schnorr with aggregated pubkey
- `CONSENSUS_PROPOSER_PUBKEYS` — hardcoded governance-defined proposer keys

### 12. Pool Distribution (`pool_distribution.h`)
Validated via `validate_pool_distribution()`:
1. Sum(outputs) <= pool_balance
2. FROST signature verified against proposer set
3. Period correct (distribution only at period boundary)
4. No duplicate distribution in same period

### 13. Pool Address (`pool_address.h`)
Deterministic pool address: `spend_key = H("mevatrust_pool" || nettype_as_byte)` reduced to scalar → pubkey. View key = spend key (anyone can view). NO private key exists — funds spent only via FROST threshold-signed distribution transactions.

### 14. Coinbase Validation (`mevatrust_coinbase_validator.h/.cpp`)
- Enforced at HF_VERSION_MEVATRUST_VALIDATION = 13
- `check_mevatrust_coinbase()`: verifies pool output exists (3% of total reward), validates 0xAA pool distribution tag (presence/absence vs expected), validates pool distribution data
- `construct_miner_tx_with_mevatrust()`: builds coinbase with 3% pool output + miner output + node reward outputs

### 15. Security System (`mevatrust_security.h`)
- C3 anti-Sybil: 10 MVC minimum balance via UTXO signature proof
- `MIN_REGISTRATION_BALANCE = 10 * COIN` (10 full MVC)
- Restart protection, anti-eclipse, rate limiting
- Challenge window configuration

---

## RPC Layer

### JSON-RPC Commands (`mevatrust_rpc_commands.h`)

**Node Management:**
- `register_node` — C3 anti-Sybil with view_key_hex, proof_txid, proof_output_index, node_public_key (separate Ed25519 P2P key)
- `unregister_node` — requires node_id + address + signature
- `get_node_pubkey` — returns daemon's node public key
- `get_node_status` — is_active, is_synced, last_seen

**Scores & Rewards:**
- `get_mevatrust_score` — node_id → double score + status
- `get_node_uptime` — uptime_seconds, uptime_percentage
- `get_reward_history` — height, amount, timestamp entries
- `get_incentive_pool_status` — pool_balance, total_distributed, active_nodes
- `get_eligible_nodes` — list of nodes with score + uptime
- `get_incentive_history` — per-node with badge_type + reason
- `get_all_node_incentives` — aggregate with badge counts

**Badges:**
- `get_badges` — list of badge strings for node
- `get_badge_requirements` — detailed per-badge with metric/current/required/met

**Circles:**
- `circle_create`, `circle_info`, `circle_list`, `circle_join`, `circle_leave`, `circle_change_admin`
- `circle_proposal_list`, `circle_proposal_votes`, `circle_disband`

**Penalties:**
- `get_penalty_history` — offense_type, amount, height, reason
- `ban_node`, `unban_node`

**Store:**
- `store_list` — paginated with category/price/sort (5 modes), returns StoreInfo[] + categories
- `store_show` — single store with full item list
- `store_search` — keyword search across stores+items, paginated
- `store_my_purchases` — by buyer_pubkey

### RPC Server Handlers (`core_rpc_server.cpp`)
- `on_store_list` (L5008): delegates to `sr->list_stores()`, maps to RPC response
- `on_store_show` (L5051): hex-to-pod store_id, gets store + items
- `on_store_search` (L5123): keyword+category+price filters, delegates to `sr->search_stores()`
- `on_store_my_purchases` (L5097): by buyer_pubkey hex

All handlers follow same pattern: get MevaTrustManager, get StoreRegistry, validate, call registry method, map to RPC structs.

---

## Simplewallet Integration (`simplewallet_mevatrust.cpp`)

1636 lines of wallet-side MevaTrust commands.

### Transaction Submission Helpers:
- `submit_mevatrust_tx()` — self-send with custom tx_extra (0.001 MVC dust)
- `submit_mevatrust_tx_to()` — send to specific address with custom tx_extra (for store purchases)
- Both use `wallet2::create_transactions_2()` with RingCT, fake_outs from min ring size

### Node Key Management:
- `read_node_pubkey()` — reads` ~/.mevacoin/mevatrust/node_signing_key` (32-byte seed file), derives pubkey via `secret_key_to_public_key()`
- `node_id_path()` — stores node_id at `~/.mevacoin/mevatrust/node_id`

### Commands:
- `register_node`: wallet keys + node key from file → build registration tx_extra (0xA0) → submit_mevatrust_tx
- `unregister_node`: build deregister tx_extra (0xA1) → submit
- `get_node_status`, `get_uptime`, `get_score`, `get_reward_history` — RPC queries
- `get_badges`, `get_badge_requirements` — RPC queries with color-coded output
- `get_incentive_history`, `get_all_incentives` — reward history display
- `store_create`: validates args → builds ITEM_BUY tx → self-send with 0xA8 extra
- `store_buy`: validates store/item exists → sends payment to store owner address with ITEM_BUY tx_extra
- `store_show`, `store_list`, `store_search` — RPC queries with formatted output
- `store_confirm`, `store_cancel` — ITEM_BUY → CONFIRM/CANCEL tag via tx_extra

---

## Consensus Rules (HF_VERSION_MEVATRUST_VALIDATION = 13)

1. Every coinbase (miner_tx) for HF13+ must pass `check_mevatrust_coinbase()`
2. 3% of total_block_reward goes to deterministic pool address (no private key)
3. Remaining 97% is split between miner and node reward outputs
4. Tag 0xAA pool distribution must be present when node rewards are due; must be absent when none are due
5. Pool distribution validated via FROST 3/5 threshold signature against `CONSENSUS_PROPOSER_PUBKEYS`
6. State root (tag 0xA7) is included in every coinbase tx_extra for fork detection
7. All MevaTrust operations (0xA0–0xAD) embedded in regular tx_extra — no special transaction type

---

## Key Design Decisions

1. **Not a smart contract system**: MevaTrust is an on-chain metadata protocol. All operations are embedded in standard Monero transaction `tx_extra` fields, using self-sends or normal payments as carriers.

2. **Deterministic pool address**: `spend_key = H("mevatrust_pool" || nettype) * G`, view_key = spend_key. No private key exists — funds are "burn-like" and can only be spent via consensus-validated FROST-signed distribution.

3. **Pool rewards**: Period-based (240 blocks ≈ 4 hours at 1 block/min). Node rewards proportional to `score * pool_share`. Welcome bonuses for new nodes.

4. **Two-phase purchase protocol**: ITEM_BUY followed by CONFIRM (0xA9) or CANCEL (0xAA) within expiry window. Funds flow to seller on BUY (not escrowed in protocol-burn). Buyer relies on seller reputation and off-chain dispute resolution.

5. **Euro split payments**: Optional hybrid payment where MVC + Euro (off-chain) comprise 100%. MVC portion must be ≥1%. Euro portion is a protocol metadata convention, not enforced on-chain.

6. **Anti-Sybil (C3)**: Registration requires ≥10 MVC UTXO demonstrated via `view_key_hex`, `proof_txid`, `proof_output_index`. View key is ephemeral — daemon verifies then discards.

7. **Separate P2P signing key**: Nodes have a distinct Ed25519 keypair (`node_public_key`) for P2P communication and PoA challenges, separate from the wallet spend key.

8. **Snapshot quorum**: Badge awards require ≥3 of 5 designated proposers to agree on-chain. Either via multi-proposer P2P ballot (SnapshotBroadcaster) or implicit blockchain consensus.

---

## LMDB Serialization Format

All custom binary serialization uses a common pattern:
1. Version byte (uint8) at position 0
2. Raw 32-byte binary keys for hash-based IDs
3. Length-prefixed strings (uint16_le length + UTF-8 data)
4. Raw pod data for fixed-size fields (uint64, bool, etc.)
5. Forward compatibility via version field (`unpack_*` checks version >= N for newer fields)

---

## Test Coverage
No dedicated MevaTrust/marketplace tests found anywhere in the test suite (`tests/` directory contains `core_tests/` and `unit_tests/` directories with standard Monero tests only). The marketplace system has no automated test coverage.
