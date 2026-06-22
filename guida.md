# MevaCoin — Piano Halving + Staking

## 1. Perché questo piano

MevaCoin è un fork di Monero con:
- **RandomX** come PoW (CPU-friendly, ASIC-resistant)
- **Participation System** (MevaTrust) già attivo (v13)

Obiettivo: aggiungere **halving** (supply limitata) e **staking** (ricompensa per chi blocca monete)
per distinguersi da Monero e creare incentivi economici chiari.

---

## 2. Halving

### Come funziona ora (Monero-style)

File: `src/cryptonote_basic/cryptonote_basic_impl.cpp:83-93`

```cpp
bool get_block_reward(size_t median_weight, size_t current_block_weight,
    uint64_t already_generated_coins, uint64_t &reward, uint8_t version)
{
    const int target_minutes = target / 60;
    const int emission_speed_factor = EMISSION_SPEED_FACTOR_PER_MINUTE - (target_minutes-1);
    uint64_t base_reward = (MONEY_SUPPLY - already_generated_coins) >> emission_speed_factor;
    if (base_reward < FINAL_SUBSIDY_PER_MINUTE*target_minutes)
        base_reward = FINAL_SUBSIDY_PER_MINUTE*target_minutes;
```

- `MONEY_SUPPLY` = 2^64-1 (supply infinita teorica)
- `EMISSION_SPEED_FACTOR_PER_MINUTE` = 20
- `FINAL_SUBSIDY_PER_MINUTE` = 3 MVC/minuto (tail emission, non finisce mai)

### Cosa cambiamo

Passiamo a **halving* ogni N blocchi, stile Bitcoin.

#### Parametri scelti

| Parametro | Valore | Dove si definisce |
|---|---|---|
| Ricompensa iniziale | **50 MVC/blocco** | `INITIAL_BLOCK_REWARD` |
| Halving ogni | **1,051,920 blocchi** (~4 anni con target 120s) | `HALVING_INTERVAL` |
| Supply totale | **~105 milioni MVC** (arrotondabile a 100M) | Calcolata: 50 * 1,051,920 * 2 |
| Blocco attivazione | **v14 (HF_MYALGO)** | `hardforks.cpp` |
| Tempo blocco | **120 secondi** (invariato) | `DIFFICULTY_TARGET_V2` |

#### Calcolo supply

```
blocchi/anno = 365.25 * 24 * 60 * 60 / 120 = 262,980
halving_interval = 262,980 * 4 = 1,051,920
supply = 50 * 1,051,920 * (1 + 1/2 + 1/4 + ...) = 50 * 1,051,920 * 2 = ~105,192,000 MVC
```

Dopo ~32 halving (128 anni), la ricompensa arriva a < 0.000001 MVC/blocco (zero pratico).

### Cosa modificare

#### 2a. `src/cryptonote_config.h` — nuove costanti

Aggiungere dopo `HF_VERSION_EARLY_COINBASE_UNLOCK` (riga 220):

```cpp
#define HF_VERSION_HALVING                14  // attivazione halving

// Halving parameters
#define INITIAL_BLOCK_REWARD             ((uint64_t)50000000000000)  // 50 MVC = 50 * 10^12
#define HALVING_INTERVAL                 1051920                   // ~4 anni
#define HALVING_VERSION                  HF_VERSION_HALVING
```

#### 2b. `src/cryptonote_basic/cryptonote_basic_impl.cpp` — nuova funzione reward

Sostituire `get_block_reward` con logica condizionale:

```cpp
bool get_block_reward(size_t median_weight, size_t current_block_weight,
    uint64_t already_generated_coins, uint64_t &reward, uint8_t version)
{
    // --- HALVING (v14+) ---
    if (version >= HF_VERSION_HALVING)
    {
        // Determina l'epoch corrente
        uint64_t halving_epoch = already_generated_coins / (INITIAL_BLOCK_REWARD * HALVING_INTERVAL);
        uint64_t reward_per_block = INITIAL_BLOCK_REWARD >> halving_epoch;
        if (reward_per_block == 0)
            reward_per_block = 1;  // minima unita' (1 atomic = 0.000000000001 MVC)

        uint64_t full_reward_zone = get_min_block_weight(version);
        if (median_weight < full_reward_zone) median_weight = full_reward_zone;

        if (current_block_weight <= median_weight) {
            reward = reward_per_block;
            return true;
        }
        // Penalità per blocchi oversized (stessa logica di prima)
        // ... (codice invariato per penalty)
        return true;
    }

    // --- VECCHIA EMISSIONE (v1-v13, Monero-style) ---
    const int target = version < 2 ? DIFFICULTY_TARGET_V1 : DIFFICULTY_TARGET_V2;
    const int target_minutes = target / 60;
    const int emission_speed_factor = EMISSION_SPEED_FACTOR_PER_MINUTE - (target_minutes-1);
    uint64_t base_reward = (MONEY_SUPPLY - already_generated_coins) >> emission_speed_factor;
    if (base_reward < FINAL_SUBSIDY_PER_MINUTE*target_minutes)
        base_reward = FINAL_SUBSIDY_PER_MINUTE*target_minutes;
    // ... resto invariato ...
}
```

**NOTA**: l'epoch si calcola sulle `already_generated_coins` (monete già emesse) diviso per `INITIAL_BLOCK_REWARD * HALVING_INTERVAL`, non sull'altezza.

#### 2c. `src/hardforks/hardforks.cpp` — attivazione

Aggiungere:

```cpp
{ 14, 14, 0, 1780000000 },  // v14 - Halving + Staking (2026-06-01 UTC circa)
```

#### 2d. `src/cryptonote_basic/cryptonote_format_utils.cpp` — nessuna modifica

Il PoW (RandomX) rimane invariato. Halving riguarda solo la ricompensa, non l'hashing.

---

## 3. Staking

### Cos'è

Gli utenti bloccano MVC in un **indirizzo speciale** (staking contract) e ricevono
una ricompensa proporzionale ogni blocco. I fondi rimangono bloccati per un
periodo minimo (es. 30 giorni).

### Come si integra con MevaTrust esistente

MevaTrust già prende **3% dei block reward** (`MEVATRUST_POOL_FRACTION_PERCENT`)
e lo distribuisce ai nodi che partecipano (uptime). Il nostro staking si appoggia
allo stesso meccanismo ma con destinatari diversi.

### Parametri scelti

| Parametro | Valore |
|---|---|
| % blocco allo staking pool | **2%** (indipendente dal 3% MevaTrust) |
| Periodo di lock minimo | **30 giorni** (43,200 blocchi) |
| Ricompensa annuale stimata | **~5-8%** (variabile in base al totale stakato) |
| Attivazione | **v14** (stessa dell'halving) |

### Architettura

```
Block Reward (es. 50 MVC)
    ├── 95% → Miner (es. 47.5 MVC)
    ├── 3%  → MevaTrust Pool (participation/nodi)
    └── 2%  → Staking Pool (distribuito agli staker)
```

### Componenti nuovi

| File | Contiene |
|---|---|
| `src/staking/staking.h` | Dichiarazioni classe StakingManager |
| `src/staking/staking.cpp` | Logica staking (deposita, unlock, rewards) |
| `src/staking/CMakeLists.txt` | Compilazione |
| `src/cryptonote_core/cryptonote_tx_utils.cpp` | Modifica `construct_miner_tx_with_mevatrust` per includere staking pool |
| `src/cryptonote_core/blockchain.cpp` | Chiamate a StakingManager durante creazione/validazione blocchi |
| RPC / simplewallet | Comandi `stake`, `unstake`, `staking_rewards` |

### Flusso staking

#### Deposit (stake)
1. Utente invia MVC a uno **staking address speciale** (es. indirizzo derivato dal genesis)
2. La transazione include un `tx_extra` con tag `0xCC` + durata lock
3. `StakingManager` registra: `{indirizzo, importo, altezza_inizio, altezza_fine}`
4. I fondi sono bloccati fino a `altezza_fine`

#### Ricompense
1. Ogni blocco, `2%` della block reward va allo **staking pool**
2. `StakingManager.divide_rewards()` distribuisce proporzionalmente a tutti gli staker attivi
3. Le ricompense si accumulano in un UTXO speciale
4. Quando l'utente fa `unstake`, riceve indietro capitale + ricompense

#### Unlock (unstake)
1. Dopo `altezza >= altezza_fine`, l'utente può richiedere il ritiro
2. `StakingManager` crea una transazione che restituisce i fondi + rewards
3. Se richiede prima della scadenza: 0 rewards (o penalty del 10%)

### Modifiche al consenso

#### 3a. `src/cryptonote_config.h` — costanti staking

```cpp
#define HF_VERSION_STAKING               14

#define STAKING_POOL_PERCENT             2   // % block reward allo staking pool
#define STAKING_MIN_LOCK_BLOCKS          43200  // ~30 giorni a 120s/blocco
#define STAKING_TX_EXTRA_TAG             0xCC
#define STAKING_LOCK_MIN_AMOUNT          ((uint64_t)500000000000000)  // 500 MVC minimo
```

#### 3b. `src/staking/staking.h`

```cpp
#pragma once
#include <map>
#include <vector>
#include <cstdint>
#include "crypto/hash.h"
#include "cryptonote_basic/cryptonote_basic.h"

namespace staking
{
    struct StakeEntry {
        crypto::hash tx_hash;
        cryptonote::account_public_address addr;
        uint64_t amount;           // in atomic units
        uint64_t start_height;
        uint64_t end_height;
        uint64_t accumulated_reward;
    };

    struct StakeReward {
        cryptonote::account_public_address addr;
        uint64_t amount;
    };

    class StakingManager {
    public:
        StakingManager();

        // Chiamato ad ogni nuovo blocco
        bool on_new_block(uint64_t height, uint64_t staking_pool_amount);

        // Registra un nuovo stake
        bool add_stake(const StakeEntry& entry);

        // Rilascia stake scaduto
        bool release_stake(const crypto::hash& tx_hash);

        // Distribuisce rewards agli staker attivi
        std::vector<StakeReward> distribute_rewards(uint64_t height);

        // Verifica che un address abbia stake attivo
        bool is_staker(const cryptonote::account_public_address& addr) const;

    private:
        std::vector<StakeEntry> m_stakes;
        uint64_t m_total_staked;     // totale attualmente bloccato
    };
}
```

#### 3c. `src/staking/staking.cpp`

Logica principale:

```cpp
bool StakingManager::on_new_block(uint64_t height, uint64_t staking_pool_amount)
{
    if (m_total_staked == 0)
        return true;  // nessuno in staking, il pool si accumula

    // Distribuisci proporzionalmente
    // reward_per_unit = staking_pool_amount / m_total_staked
    for (auto& entry : m_stakes) {
        if (height >= entry.start_height && height < entry.end_height) {
            entry.accumulated_reward += staking_pool_amount * entry.amount / m_total_staked;
        }
    }
    return true;
}
```

#### 3d. `src/cryptonote_core/cryptonote_tx_utils.cpp` — modifica miner tx

In `construct_miner_tx_with_mevatrust`, dopo la riga 731:

```cpp
// Pool gets 3% (MevaTrust)
uint64_t pool_amount = total_block_reward * MEVATRUST_POOL_FRACTION_PERCENT / 100;
uint64_t miner_reward = total_block_reward - pool_amount;

// --- STAKING (v14+) ---
uint64_t staking_pool_amount = 0;
if (hard_fork_version >= HF_VERSION_STAKING) {
    staking_pool_amount = total_block_reward * STAKING_POOL_PERCENT / 100;
    miner_reward -= staking_pool_amount;
}
// --- FINE STAKING ---
```

Lo staking pool va in un indirizzo speciale (es. burn address o multisig del team)
oppure viene distribuito direttamente via `StakingManager.distribute_rewards()`
e aggiunto come output aggiuntivo nella coinbase.

#### 3e. `src/cryptonote_core/blockchain.cpp` — creazione blocco

In `create_block_template`, dopo aver calcolato `total_block_reward` (riga 1851):

```cpp
if (hf_version >= HF_VERSION_STAKING) {
    uint64_t staking_pool = total_block_reward * STAKING_POOL_PERCENT / 100;
    // Ottieni rewards per gli staker attuali
    auto staker_rewards = m_staking_manager->distribute_rewards(height);
    // Aggiungi come node_rewards (il sistema MevaTrust già supporta
    // destinatari multipli nella coinbase)
    for (const auto& sr : staker_rewards) {
        node_rewards.push_back({sr.addr, sr.amount, 0, 0});
    }
    // Riduci la ricompensa del miner
    effective_miner_reward -= staking_pool;
}
```

#### 3f. RPC e simplewallet — comandi utente

`simplewallet` nuovi comandi:

```
stake <indirizzo> <importo> [durata_giorni=30]
unstake <tx_hash>
staking_info
```

`daemon` nuovi RPC:

```
stake_deposit    -> registra stake
stake_release    -> sblocca stake
staking_pool_info -> mostra totale stakato, APY stimato
```

### Transazione di staking

Per bloccare i fondi senza smart contract (Monero non ha EVM),
usiamo un **transaction extra tag** `0xCC`:

```
[0xCC] [varint: durata_blocchi] [32 byte: indirizzo_pubblico]
```

Il nodo, vedendo un output con `tx_extra[0] == 0xCC`, lo tratta come stake:
- Non lo rende spendibile fino a `altezza_deposito + durata_blocchi`
- Lo aggiunge al `StakingManager`

---

## 4. Piano di implementazione ordine

### Fase 1 — Halving (solo, ~1 giorno)

1. `cryptonote_config.h` — costanti halving
2. `cryptonote_basic_impl.cpp` — modifica `get_block_reward`
3. `hardforks.cpp` — v14
4. Test: verificare che a blocco 1 dia 50 MVC, a 1.051.921 dia 25, ecc.

### Fase 2 — Staking base (~3 giorni)

1. `src/staking/` — StakingManager (deposito, rewards, rilascio)
2. `cryptonote_tx_utils.cpp` — pool 2% nella coinbase
3. `blockchain.cpp` — integrazione StakingManager nella creazione blocchi
4. `tx_extra` parsing per tag `0xCC`
5. Test: stake, rewards, unstake

### Fase 3 — RPC e wallet (~2 giorni)

1. Comandi RPC: `stake_deposit`, `stake_release`, `staking_info`
2. Comandi simplewallet: `stake`, `unstake`, `staking_info`
3. Test end-to-end

---

## 5. Riepilogo parametri finali

| Feature | Dettaglio |
|---|---|
| Algoritmo PoW | RandomX (invariato) |
| Ricompensa iniziale | 50 MVC/blocco |
| Halving ogni | 1,051,920 blocchi (~4 anni) |
| Supply totale | ~105M MVC |
| Staking APY | ~5-8% annuo |
| Lock minimo staking | 30 giorni (43,200 blocchi) |
| Blocco staking minimo | 500 MVC |
| Pool staking | 2% block reward |
| Pool participation (MevaTrust) | 3% block reward (invariato) |
| Attivazione | v14 (hard fork) |
