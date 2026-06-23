# MevaCoin Premine — Sistema di Vesting, Governance e Network Fund

## Panoramica

Premine totale: **1.000.000 MVC**

Suddivisione:

| Categoria | Quantità | Meccanismo |
|-----------|----------|------------|
| Team Lock | 200.000 MVC | Bloccato 24 mesi on‑chain, poi spendibile liberamente |
| Treasury Governance | 400.000 MVC | Multisig 2‑di‑X con evoluzione firmatari |
| Network Fund | 400.000 MVC | Rate‑limit 10.000 MVC / 30 giorni |

---

## 1. Team Lock (200.000 MVC)

### Meccanismo
- UTXO creato nel genesis block verso `FOUNDATION_ADDRESS`
- Il codice tiene traccia della **chiave pubblica one‑time** di questo output
- In fase di validazione (`check_premine_spend` in `blockchain.cpp`):
  - se un input spende l'UTXO team → verificare `height >= TEAM_LOCK_BLOCKS` (518.400)
- `TEAM_LOCK_BLOCKS` = 518.400 (24 mesi × 30 gg × 720 blocchi/giorno)

### Dopo lo sblocco
- UTXO utilizzabile normalmente (ring signature, RingCT, qualsiasi destinazione)

---

## 2. Treasury Governance (400.000 MVC)

### Architettura
- Tre firmatari originali decisi nel genesis (hardcodati in `foundation_vesting.h`)
- Indirizzo deterministico del treasury = `H("mevacoin_governance" || nettype)` (nessuna private key)
- Il saldo è un UTXO bloccato con chiave pubblica nota, MA:
  - non c'è una private key → non spendibile via wallet normale
  - si spende solo tramite **transazione di governance** validata dal protocollo

### Regole validazione (`check_premine_spend`)
Quando un UTXO del treasury viene speso (rilevato dalla chiave pubblica one‑time):
1. Deve contenere un tag `0xB0` (GOVERNANCE_TRANSFER) in `tx_extra`
2. Il tag contiene la lista firme dei signatories
3. Servono almeno **2 firme valide** su X signatories attuali
4. L'ammontare non può superare il saldo residuo del treasury

### Evoluzione firmatari
- **Aggiunta** (tag `0xB1`): 2‑di‑X approvano l'aggiunta → `X++`, threshold resta 2
- **Rimozione** (tag `0xB2`): 2‑di‑X rimuovono un signer, **mai** i 3 genesis (indici 0‑2) → `X--`

### Stato governance (in memoria con `m_properties` callback)
- `signers` — `vector<crypto::public_key>` (firmatari attuali)
- `balance` — `uint64_t` (saldo residuo)
- I genesis signers (indici 0..GOVERNANCE_ORIGINAL_SIGNERS-1) non sono rimovibili

### Formato tx_extra

Tag `0xB0` — Governance Transfer:
```
struct tx_extra_governance_transfer {
    uint64_t              amount;
    crypto::public_key    recipient_spend;
    crypto::public_key    recipient_view;
    std::vector<governance_signature> signatures;  // ≥2
};
```

Tag `0xB1` — Add Signer:
```
struct tx_extra_governance_add_signer {
    crypto::public_key new_signer_key;
    std::vector<governance_signature> signatures;  // ≥2
};
```

Tag `0xB2` — Remove Signer:
```
struct tx_extra_governance_remove_signer {
    uint64_t signer_index;  // index nel vettore signers
    std::vector<governance_signature> signatures;  // ≥2
};
```

Ogni `governance_signature`:
```
struct governance_signature {
    crypto::public_key signer_key;
    crypto::signature  sig;  // sign(tx_prefix_hash) con private key del signer
};
```

---

## 3. Network Fund (400.000 MVC)

### Meccanismo
- UTXO creato nel genesis verso indirizzo deterministico `H("mevacoin_network_fund" || nettype)`
- Nessuna private key → spendibile solo via protocollo
- Rate limit: **max 10.000 MVC spesi in qualsiasi finestra rolling di 30 giorni**

### Validazione (`check_premine_spend`)
Quando un UTXO del network fund viene speso:
1. Deve contenere tag `0xC0` (NETWORK_FUND_TRANSFER) in `tx_extra`
2. Il tag contiene destinazione (chiavi pubblica spend/view) e amount
3. Validazione verifica il rolling window:
   - Somma amount di tutti gli spend events negli ultimi `NETWORK_FUND_WINDOW_BLOCKS` (21.600)
   - Se + nuovo amount ≤ 10.000 MVC → ok, registra nuovo spend event
   - Altrimenti → rifiutato

### Stato network fund (in memoria)
- `balance` — `uint64_t`
- `recent_spends` — `vector<network_spend_event>` dove ogni evento è `{height, amount}`
- All'aggiunta di un nuovo blocco, gli eventi precedenti alla finestra vengono potati

---

## 4. Genesis Block — Output Schema

Il genesis block (`generate_genesis_block()`) produce 3 output aggiuntivi dopo quelli standard:

```
Output n-3: Team Lock          200.000 MVC → FOUNDATION_ADDRESS
Output n-2: Treasury           400.000 MVC → governance_address (deterministico)
Output n-1: Network Fund       400.000 MVC → network_address (deterministico)
```

Tutti e tre usano `tx_secret_key = H(FOUNDATION_ADDRESS)`, con indici di derivazione diversi.
Le chiavi pubbliche one‑time sono pre‑calcolate dal codice e registrate in `premine_output_keys` per le validazioni successive.

---

## 5. Supply Accounting

- `already_generated_coins` parte da 1.000.000 × 10¹² (dopo genesis)
- I 3 output sono UTXO regolari → contabilizzati nell'emissione normalmente
- `get_block_reward()` calcola la ricompensa in base alla supply totale

---

## 6. File Implementati

### Nuovi file
| File | Descrizione |
|------|-------------|
| `src/cryptonote_core/foundation_vesting.h` | Costanti vesting / address derivation / premine_output_keys / compute_premine_output_key |
| `src/cryptonote_core/governance.h` | governance_state struct / verify_governance_signatures / validate_governance_transfer dichiarazioni |
| `src/cryptonote_core/governance.cpp` | Implementazione verify_governance_signatures e validate_governance_transfer |
| `src/cryptonote_core/network_fund.h` | network_fund_state / network_spend_event / validate_network_fund_spend dichiarazione |
| `src/cryptonote_core/network_fund.cpp` | Implementazione validate_network_fund_spend e potatura rolling window |

### File modificati
| File | Cosa |
|------|------|
| `src/cryptonote_basic/tx_extra.h` | Aggiunti tag 0xB0‑0xB2, 0xC0 + struct governance e network fund |
| `src/cryptonote_core/desy.h` | FOUNDATION_ALLOCATION ora = PREMINE_TOTAL; inclusione foundation_vesting.h |
| `src/cryptonote_core/cryptonote_tx_utils.cpp` | Genesis: 3 output (team/treasury/network) invece di foundation singolo |
| `src/cryptonote_core/blockchain.h` | Aggiunti m_premine_keys, m_governance, m_network_fund, m_premine_initialized + metodi init/check/process/rebuild |
| `src/cryptonote_core/blockchain.cpp` | init_premine_state() in init(); check_premine_spend() in check_tx_inputs(); process_premine_actions() in handle_block_to_main_chain(); rebuild_premine_state() per startup |
| `src/cryptonote_core/CMakeLists.txt` | Aggiunti governance.cpp e network_fund.cpp |

### Compilazione
Tutti i file compilano correttamente (verificato con g++).

---

## 7. Cosa Resta da Fare

### Bloccanti
1. **Generare 3 wallet reali** e sostituire le placeholder keys in `foundation_vesting.h::get_genesis_governance_signers()`
2. **Full build e test** — compilare `mevacoind` completamente e testare su testnet privata
3. **Test genesis** — verificare che il genesis block produca 3 output con le chiavi attese
4. **Test team lock** — spendere UTXO team prima/dopo block 518.400
5. **Test governance** — 2-of-3 signature verification, transfer, add/remove signer
6. **Test network fund** — rate limit con rolling window

### Miglioramenti futuri
- **Persistenza LMDB** — salvare governance state e network fund events in LMDB anziché solo in memoria (attualmente usa `m_properties` callback, ma non ancora implementato)
- **Ricostruzione robusta** — `rebuild_premine_state()` attualmente riproduce tutti i blocchi da genesis; potrebbe essere ottimizzata con checkpoint periodici
- **RPC** — endpoint per leggere governance state e network fund balance
- **Wallet integration** — supporto per creare governance transactions dal wallet CLI

---

## 8. Note Implementative

- Governance signatures usano `crypto::check_signature()` (Ed25519 standard)
- I wallet dei 3 signatories sono generati dall'utente prima del genesis
- Deterministic addresses: derivati come `H(domain || nettype)` → `hash_to_scalar()` → `secret_key_to_public_key()`
- `FOUNDATION_ADDRESS` è l'indirizzo monero esistente; `tx_secret_key = hash_to_scalar(FOUNDATION_ADDRESS)`
- `COIN = 1000000000000` (10¹² unità atomiche per MVC)
- `NETWORK_FUND_WINDOW_BLOCKS = 21600` (~30gg a 720 blocchi/giorno)
- Genesis block ha 3 output in più alla fine del vout, dopo gli output standard coinbase
