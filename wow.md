# MevaCoin — "first privacy chain that pays node operators"

## ✅ Fatto (completato)

### FROST 3/5 threshold signatures — da placeholder a implementazione reale

| Cosa | Dov'era | Dove è ora |
|------|---------|------------|
| **Aggregazione chiave pubblica** | `agg_pubkey = keypairs[0].pub` (placeholder) | `sum_public_keys()` — somma ed25519 point di tutti e 5 i proposer via `ge_add` |
| **Coefficienti di Lagrange** | settati a 1 per tutti (non sicuro) | `compute_lagrange_coeffs()` — vera interpolazione di Lagrange mod `l`, con inverso modulare via Fermat |
| **Nonce commitment R** | `memset(&R, 0, 32)` (zero) | `R = sum(r_i * G)` — somma dei commitment dei nonce di ogni signer via `ge_scalarmult_base` + `ge_add` |
| **Verifica firma** | usava `crypto::check_signature()` di Monero (formato diverso) | verifica Schnorr corretta: `z*G - c*Y == R` via `ge_double_scalarmult_base_vartime` |
| **Firma in `trigger_distribution`** | `memset(&frost_sig, 0, sizeof(frost_sig))` | FROST completo: generate_nonces → sign_partial → aggregate_signatures, con tutti e 5 i proposer |
| **Proposer pubkeys** | `crypto::public_key{}` (tutti zero) | `derive_proposer_pubkeys(nettype)` — derivate deterministicamente da `H("mevatrust_proposer_X" || nettype)` |
| **`HF_POOL_ACTIVATION`** | `100000` (placeholder) | `13` (`HF_VERSION_MEVATRUST`) |
| **`verify_distribution_signature`** | `CONSENSUS_PROPOSER_PUBKEYS[0]` (singola chiave) | `sum_public_keys(pkg.signer_pubkeys.data(), FROST_N)` |
| **`sign_partial` challenge** | usava `CONSENSUS_PROPOSER_PUBKEYS[0]` hardcoded | parametro `agg_pubkey` passato dalla chiamata |
| **Coinbase validator** | usava `CONSENSUS_PROPOSER_PUBKEYS` vuoto | `derive_proposer_pubkeys(MAINNET)` |

### Files modificati

- `src/cryptonote_core/mevatrust/frost_threshold.h` — nuova signature `sign_partial` con `agg_pubkey`, nuove funzioni `derive_proposer_pubkeys`, `sum_public_keys`
- `src/cryptonote_core/mevatrust/frost_threshold.cpp` — riscritto completamente: Lagrange reali, somma punti, verifica Schnorr corretta, modular inverse
- `src/cryptonote_core/mevatrust/mevatrust_manager.h` — `m_proposer_keypairs`, `set_proposer_keypairs()`, `has_proposer_keys()`
- `src/cryptonote_core/mevatrust/mevatrust_manager.cpp` — `set_proposer_keypairs()`, `trigger_distribution()` con FROST reale, `HF_POOL_ACTIVATION` fixato
- `src/cryptonote_core/mevatrust/pool_distribution.cpp` — `verify_distribution_signature()` con somma reale delle pubkey
- `src/cryptonote_core/mevatrust/mevatrust_coinbase_validator.cpp` — proposer pubkeys da `derive_proposer_pubkeys(MAINNET)`

## 🔄 Cosa rimane da fare (fuori scope)

### 1. Proposer key management
Le chiavi dei proposer sono attualmente **deterministiche** (derivabili da chiunque). Per mainnet servono **vere chiavi governance-elected**:
- Generare 5 keypair offline (cerimonia)
- Hardcodare le 5 pubkey in `frost_threshold.cpp`
- Distribuire le 5 privkey agli operatori dei seed node
- Configurare ogni seed node con la propria proposer private key (via config file o env)
- Il `MevaTrustManager::init()` deve caricare la proposer key dal config

### 2. P2P coordination per FROST
Attualmente il miner ha tutti e 5 i keypair e firma localmente. In produzione:
- Ogni proposer ha solo la propria chiave
- Il miner propone una distribuzione → la broadcasta ai proposer via P2P
- Ogni proposer risponde con `sign_partial` via P2P
- Il miner aggrega → include 0xAA nel coinbase

### 3. Test
- Test unitari per FROST (sign → verify ciclo completo)
- Test di integrazione: registra un nodo, aspetta periodo, verifica che riceva pagamento
- Test con chiavi deterministiche (testnet) e con chiavi reali

### 4. Pool activation block height
`HF_POOL_ACTIVATION = 13` significa che il pool parte dal blocco 13. Va allineato con la strategia di deploy (forse bloccare a X migliaia di blocchi dopo il lancio per far accumulare fondi).

### 5. Security audit
- Verificare che `sc_invert_mod_l` sia constant-time (non per la v1, ma importante per production)
- Review della sicurezza del protocollo FROST: binding nonce, rogue-key attack prevention
- Verificare che `sum_public_keys` non possa fallire con punti invalidi
