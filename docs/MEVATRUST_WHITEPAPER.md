# MevaCoin — MEV-Resistant Private Digital Cash

**Abstract.** MevaCoin is a fork of Monero that introduces the first practical on-chain defense against Maximal Extractable Value (MEV) in a privacy-preserving cryptocurrency. By combining Monero's battle-tested privacy guarantees (ring signatures, stealth addresses, RingCT, Dandelion++) with a novel threshold-signature-based distribution protocol called **MevaTrust**, MevaCoin eliminates the primary attack surface for MEV: the ability to reorder, censor, or front-run transactions at the block production layer.

---

## 1. The Problem: MEV in Privacy Coins

Maximal Extractable Value (MEV) is the value that can be captured by reordering, including, or excluding transactions within a block. In public blockchains like Ethereum, MEV manifests as front-running bots, sandwich attacks, and liquidations. The privacy community has largely ignored MEV, operating under the assumption that private transactions are inherently MEV-resistant.

**This assumption is wrong.** Even in privacy coins:

- **Block producers control transaction ordering.** A malicious miner can delay, reorder, or censor transactions.
- **Dandelion++ only protects the origin.** It prevents network-level deanonymization but does not prevent a miner from seeing transaction contents during block template construction.
- **Pool distributions are front-runnable.** If a reward distribution transaction is profitable to front-run (e.g., by inserting a competing transaction), a miner can extract that value.

MevaCoin addresses this at the protocol level by making the block production process itself MEV-resistant.

---

## 2. MevaTrust Architecture

MevaTrust is a set of protocol-level rules enforced at the consensus layer. It consists of five core mechanisms:

### 2.1 Threshold-Signed Block Production

Five **proposer nodes** are elected to produce blocks in a cooperative threshold signing scheme using **FROST** (Flexible Round-Optimized Schnorr Threshold Signatures). At least 3 of 5 proposers must participate in signing each distribution. This prevents any single actor from controlling block contents.

Each proposer holds one FROST key share. Keys are deterministically generated at network genesis and can be overridden at startup via `--mevatrust-proposer-key=<hex>` for ceremony-generated keys.

### 2.2 Deterministic Reward Pool

3% of every block reward is directed to a protocol-controlled pool address:

```
P_pool = H(mevatrust_pool || network_type)
```

This address has **no known private key**. Funds cannot be moved except through the MevaTrust distribution mechanism. This eliminates the single most common MEV attack surface: control over a large fund pool.

### 2.3 Automated Pool Distribution

Every 240 blocks (~8 hours at 120s block time), the accumulated pool is distributed to registered nodes:

1. The **coordinator proposer** triggers distribution.
2. All 5 proposers exchange nonces (Round 1 of FROST).
3. The coordinator aggregates nonces and broadcasts the challenge.
4. Each proposer responds with a partial signature.
5. The coordinator aggregates ≥3 partials into a final FROST signature.
6. The distribution transaction is embedded in the coinbase as `tx_extra` tag `0xAA`.

This distribution is **MEV-resistant** because:
- No single actor can unilaterally distribute funds.
- The distribution is deterministic and automatic — no front-running opportunity.
- The FROST signature proves consensus among proposers.

### 2.4 Coinbase Validation

Every block's coinbase transaction is validated at the consensus layer (`mevatrust_coinbase_validator.cpp`):

- The 3% pool contribution must exist.
- If a distribution is due, the `0xAA` tag must be present with a valid FROST signature.
- The total reward must equal: block subsidy + tx fees — pool contribution + distributions.
- The state root commitment must match the MevaTrust engine state.

Invalid coinbase transactions are rejected by all nodes. This makes MEV extraction via block manipulation economically impossible.

### 2.5 Governance Treasury

1,000,000 MVC were pre-mined at genesis:

| Allocation | Amount | Address | Control |
|-----------|--------|---------|---------|
| Team vesting | 200,000 MVC | Foundation address | Time-locked 24 months (518,400 blocks) |
| Governance treasury | 400,000 MVC | `mevacoin_governance` (deterministic) | 2-of-3 multi-signature via `0xB0` tx_extra |
| Network fund | 400,000 MVC | `mevacoin_network_fund` (deterministic) | Rate-limited (10k MVC / 30 days) via `0xC0` tx_extra |

The governance treasury address is deterministic — it has no known private key. Spending requires 2-of-3 governance signers to produce a valid `tx_extra_governance_transfer` (`0xB0`) blob with Ed25519 signatures. This design ensures no single party can misappropriate treasury funds.

---

## 3. Tokenomics

### 3.1 Emission

MevaCoin inherits Monero's tail emission: after the initial premine, 0.6 MVC per block is emitted indefinitely, plus transaction fees. This ensures long-term mining viability and network security.

### 3.2 Block Reward Distribution

```
Block reward (0.6 MVC + fees):
  ├── 97% → Miner (proposer)
  ├──  3% → MevaTrust pool (distributed to registered nodes)
  └──  Distributions occur every 240 blocks
```

### 3.3 Premine Schedule

| Year | Team | Treasury | Network Fund |
|------|------|----------|--------------|
| 0-1 | ~100k unlocked (linear) | Available | 10k/30d rate limit |
| 1-2 | Remaining unlocked | Available | 10k/30d rate limit |
| 2+ | Fully vested | Available | 10k/30d rate limit |

---

## 4. Network Participation

MevaCoin introduces a structured participation system to incentivize healthy network operation.

### 4.1 Node Registration

Any operator can register a node on-chain. Each node receives a unique identity with attributes:
- **Status:** Active, inactive, suspended, or banned
- **Uptime score:** Measured via challenge-response availability proofs
- **MevaTrust score:** Composite score based on uptime, sync status, responsiveness, and activity
- **Badges:** Up to 11 badge types (e.g., ACTIVE_MINER, FULL_NODE_OPERATOR, STABLE_NODE)

### 4.2 Reward Distribution

Pool rewards are distributed proportionally to each node's MevaTrust score. This creates a competitive market for node quality: better infrastructure = higher score = larger rewards.

### 4.3 Circles (Micro-DAOs)

Nodes can form **circles** — on-chain micro-DAOs with an Italian two-convocation voting model:
- **First call:** Requires 2/3 quorum
- **Second call:** Requires 1/3 quorum (if first call fails)
- Circles can manage shared resources, coordinate activities, or vote on proposals.

### 4.4 On-Chain Store

Node operators can create stores with listings in a dual-currency system (MVC + Euro split). Purchases follow a two-phase flow (buy → confirm/cancel/refund) familiar from decentralized marketplace design.

---

## 5. Security Model

### 5.1 MEV Resistance Guarantees

| Attack | Monero | MevaCoin |
|--------|--------|----------|
| Transaction reordering | Possible | Prevented (FROST threshold) |
| Front-running pool dist. | Possible | Impossible (deterministic schedule) |
| Censorship | Miner-controlled | Any proposer can include |
| Pool theft | Possible (private key leak) | Impossible (no private key) |
| Block withholding | Single actor | Requires 3/5 collusion |

### 5.2 Threshold Security

FROST 3-of-5 threshold means:
- **Any 3 proposers** can produce a valid distribution.
- **Up to 2 malicious proposers** cannot steal funds or censor.
- **Proposer set rotation** can be enacted via governance (future upgrade).

### 5.3 Governance Signer Security

Treasury spending requires 2-of-3 governance signers. Signers can be added or removed via `(2-of-3) → (new 2-of-3)` signature cascades, ensuring governance continuity.

---

## 6. Implementation Status

All core components are implemented and integrated into the `mevacoind` daemon:

| Component | Status | Files |
|-----------|--------|-------|
| FROST threshold signing | ✅ Complete | `frost_threshold.cpp`, `frost_broadcaster.cpp` |
| Pool distribution | ✅ Complete | `pool_distribution.cpp`, `reward_distributor.cpp` |
| Coinbase validation | ✅ Complete | `mevatrust_coinbase_validator.cpp` |
| Node registry | ✅ Complete | `node_registry.cpp` |
| Badge system | ✅ Complete | `badge_system.cpp` |
| Circles (DAOs) | ✅ Complete | `circle_registry.cpp` |
| Store marketplace | ✅ Complete | `mevatrust_store_registry.cpp` |
| Governance treasury | ✅ Complete | `foundation_vesting.h`, `blockchain.cpp` |
| Premine vesting | ✅ Complete | `foundation_vesting.h` |
| MevaTrust engine | ✅ Complete | `mevatrust_engine.cpp`, `mevatrust_manager.cpp` |
| CLI wallet commands | ✅ Complete | `simplewallet_mevatrust.cpp` |
| RPC endpoints | ✅ Complete | `core_rpc_server.cpp` |
| P2P FROST coordination | ✅ Complete | `frost_broadcaster.cpp`, protocol handlers |

**What remains:**
- Block explorer / governance dashboard (visualization layer)
- Public testnet with automated distributions
- Docker deployment for one-command node setup
- MEV resistance test suite and benchmarks

---

## 7. Comparison

| Feature | MevaCoin | Monero | Zcash | Bitcoin |
|---------|----------|-------|-------|---------|
| Private transactions | ✓ RingCT + rings | ✓ | ✓ (shielded) | ✗ |
| MEV resistance | ✓ MevaTrust | ✗ | ✗ | ✗ |
| Pooled rewards | ✓ 3% to node pool | ✗ | ✗ | ✗ |
| DAO governance | ✓ Circles | ✗ | ✗ | ✗ |
| On-chain marketplace | ✓ Store system | ✗ | ✗ | ✗ |
| Tail emission | ✓ | ✓ | ✗ | ✗ |
| ASIC resistance | ✓ RandomX | ✓ | ✗ | ✗ |

---

## 8. Launch Roadmap

1. **Phase 1 — Demo (current):** Working daemon and wallet, all subsystems implemented, CLI-governance integrated.
2. **Phase 2 — Testnet:** Public testnet with automated pool distributions, block explorer, Docker deployment.
3. **Phase 3 — Audit:** Third-party security audit of FROST implementation, governance system, and consensus rules.
4. **Phase 4 — Mainnet:** Ceremony-generated FROST keys, hardcoded checkpoints, seed nodes, exchange integration.
5. **Phase 5 — Ecosystem:** Wallet GUI, mobile wallet, merchant tools, bridge infrastructure.

---

## References

- Monero Research Lab. "MRL-0001: A Note on Chain Reactions in Traceability."
- Komlo, C., Goldberg, I. "FROST: Flexible Round-Optimized Schnorr Threshold Signatures." (2020)
- van Saberhagen, N. "CryptoNote v 2.0." (2013)
- Noether, S. "Ring Signature Confidential Transactions." (2015)
- Dandelion++: Boosting Anonymity in Cryptocurrencies. (2020)
