# MevaCoin — Moneta Digitale Privata Resistente al MEV

**Sommario.** MevaCoin è un fork di Monero che introduce la prima difesa pratica contro il Maximal Extractable Value (MEV) in una cryptocurrency privacy-preserving. Combinando le garanzie di privacy di Monero (ring signatures, stealth addresses, RingCT, Dandelion++) con un protocollo innovativo di threshold-signature chiamato **MevaTrust**, MevaCoin elimina la principale superficie d'attacco del MEV: la possibilità di riordinare, censurare o fare front-running delle transazioni a livello di produzione del blocco.

---

## 1. Il Problema: MEV nelle Privacy Coin

Il Maximal Extractable Value (MEV) è il valore che può essere estratto riordinando, includendo o escludendo transazioni all'interno di un blocco. Nelle blockchain pubbliche come Ethereum, il MEV si manifesta come bot di front-running, sandwich attack e liquidazioni. La comunità privacy ha in gran parte ignorato il MEV, assumendo che le transazioni private siano intrinsecamente MEV-resistant.

**Questa assunzione è sbagliata.** Anche nelle privacy coin:

- **I produttori di blocchi controllano l'ordine delle transazioni.** Un miner malizioso può ritardare, riordinare o censurare transazioni.
- **Dandelion++ protegge solo l'origine.** Previene la de-anonimizzazione a livello di rete, ma non impedisce a un miner di vedere il contenuto delle transazioni durante la costruzione del block template.
- **Le distribuzioni del pool sono front-runnabili.** Se una transazione di distribuzione è redditizia da front-runnare, un miner può estrarre quel valore.

MevaCoin affronta questo problema a livello di protocollo, rendendo il processo di produzione del blocco stesso MEV-resistant.

---

## 2. Architettura MevaTrust

MevaTrust è un insieme di regole a livello di protocollo enforceate al livello di consenso. Consiste in cinque meccanismi fondamentali:

### 2.1 Produzione di Blocchi con Firma Soglia

Cinque **nodi proposer** sono eletti per produrre blocchi in uno schema cooperativo di firma soglia usando **FROST** (Flexible Round-Optimized Schnorr Threshold Signatures). Almeno 3 dei 5 proposer devono partecipare alla firma di ogni distribuzione. Questo impedisce a qualsiasi singolo attore di controllare il contenuto dei blocchi.

Ogni proposer possiede una chiave FROST. Le chiavi sono generate deterministicamente al genesis della rete e possono essere sovrascritte all'avvio tramite `--mevatrust-proposer-key=<hex>` per chiavi generate da cerimonia.

### 2.2 Pool di Ricompensa Deterministico

Il 3% di ogni block reward è diretto a un indirizzo di pool controllato dal protocollo:

```
P_pool = H(mevatrust_pool || network_type)
```

Questo indirizzo **non ha chiave privata nota**. I fondi non possono essere spostati se non attraverso il meccanismo di distribuzione MevaTrust. Questo elimina la superficie d'attacco MEV più comune: il controllo di un grande fondo di ricompensa.

### 2.3 Distribuzione Automatica del Pool

Ogni 240 blocchi (~8 ore con block time di 120s), il pool accumulato viene distribuito ai nodi registrati:

1. Il **coordinator proposer** attiva la distribuzione.
2. Tutti i 5 proposer scambiano nonce (Round 1 di FROST).
3. Il coordinator aggrega i nonce e trasmette la sfida.
4. Ogni proposer risponde con una firma parziale.
5. Il coordinator aggrega ≥3 firme parziali in una firma FROST finale.
6. La transazione di distribuzione è incorporata nella coinbase come `tx_extra` tag `0xAA`.

Questa distribuzione è **MEV-resistant** perché:
- Nessun singolo attore può distribuire fondi unilateralmente.
- La distribuzione è deterministica e automatica — nessuna opportunità di front-running.
- La firma FROST prova il consenso tra i proposer.

### 2.4 Validazione della Coinbase

La coinbase transaction di ogni blocco è validata a livello di consenso (`mevatrust_coinbase_validator.cpp`):

- Il contributo del 3% al pool deve esistere.
- Se una distribuzione è prevista, il tag `0xAA` deve essere presente con una firma FROST valida.
- La ricompensa totale deve essere uguale a: sussidio del blocco + fee — contributo pool + distribuzioni.
- Lo state root commitment deve corrispondere allo stato del MevaTrust engine.

Le coinbase non valide sono rifiutate da tutti i nodi. Questo rende l'estrazione di MEV tramite manipolazione del blocco economicamente impossibile.

### 2.5 Tesoreria di Governance

1.000.000 MVC sono stati pre-mined al genesis:

| Allocazione | Ammontare | Indirizzo | Controllo |
|-----------|----------|-----------|-----------|
| Team vesting | 200.000 MVC | Foundation address | Time-locked 24 mesi (518.400 blocchi) |
| Tesoreria governance | 400.000 MVC | `mevacoin_governance` (deterministico) | 2-of-3 multi-signature via `0xB0` tx_extra |
| Network fund | 400.000 MVC | `mevacoin_network_fund` (deterministico) | Rate-limited (10k MVC / 30 giorni) via `0xC0` tx_extra |

L'indirizzo della tesoreria di governance è deterministico — non ha chiave privata nota. La spesa richiede che 2 dei 3 signer di governance producano un blob `tx_extra_governance_transfer` (`0xB0`) valido con firme Ed25519. Questo design garantisce che nessuna singola parte possa appropriarsi indebitamente dei fondi della tesoreria.

---

## 3. Tokenomics

### 3.1 Emissione

MevaCoin eredita la tail emission di Monero: dopo il premine iniziale, vengono emessi 0,6 MVC per blocco a tempo indeterminato, più le fee delle transazioni. Questo garantisce la redditività del mining a lungo termine e la sicurezza della rete.

### 3.2 Distribuzione del Block Reward

```
Block reward (0,6 MVC + fee):
  ├── 97% → Miner (proposer)
  ├──  3% → Pool MevaTrust (distribuito ai nodi registrati)
  └──  Distribuzione ogni 240 blocchi
```

### 3.3 Schedule del Premine

| Anno | Team | Tesoreria | Network Fund |
|------|------|-----------|--------------|
| 0-1 | ~100k sbloccati (lineare) | Disponibile | 10k/30d rate limit |
| 1-2 | Restanti sbloccati | Disponibile | 10k/30d rate limit |
| 2+ | Completamente vestito | Disponibile | 10k/30d rate limit |

---

## 4. Partecipazione alla Rete

MevaCoin introduce un sistema di partecipazione strutturato per incentivare il funzionamento sano della rete.

### 4.1 Registrazione dei Nodi

Qualsiasi operatore può registrare un nodo on-chain. Ogni nodo riceve un'identità unica con attributi:
- **Stato:** Attivo, inattivo, sospeso o bannato
- **Punteggio di uptime:** Misurato tramite prove di disponibilità challenge-response
- **MevaTrust score:** Punteggio composito basato su uptime, sincronizzazione, reattività e attività
- **Badge:** Fino a 11 tipi di badge (es. ACTIVE_MINER, FULL_NODE_OPERATOR, STABLE_NODE)

### 4.2 Distribuzione delle Ricompense

Le ricompense del pool sono distribuite proporzionalmente al MevaTrust score di ogni nodo. Questo crea un mercato competitivo per la qualità dei nodi: infrastruttura migliore = punteggio più alto = ricompense maggiori.

### 4.3 Circles (Micro-DAO)

I nodi possono formare **circles** — micro-DAO on-chain con un modello di voto italiano a due convocazioni:
- **Prima convocazione:** Richiede 2/3 di quorum
- **Seconda convocazione:** Richiede 1/3 di quorum (se la prima fallisce)
- I circles possono gestire risorse condivise, coordinare attività o votare proposte.

### 4.4 Store On-Chain

Gli operatori di nodi possono creare store con annunci in un sistema a doppia valuta (MVC + Euro split). Gli acquisti seguono un flusso in due fasi (buy → confirm/cancel/refund) familiare dal design dei marketplace decentralizzati.

---

## 5. Modello di Sicurezza

### 5.1 Garanzie di MEV Resistance

| Attacco | Monero | MevaCoin |
|-------|--------|----------|
| Riordinamento transazioni | Possibile | Prevenuto (soglia FROST) |
| Front-running distribuzioni pool | Possibile | Impossibile (programma deterministico) |
| Censura | Controllato dal miner | Qualsiasi proposer può includere |
| Furto del pool | Possibile (leak chiave privata) | Impossibile (nessuna chiave privata) |
| Withholding del blocco | Singolo attore | Richiede collusione 3/5 |

### 5.2 Sicurezza della Soglia

FROST 3-of-5 significa:
- **Qualsiasi 3 proposer** possono produrre una distribuzione valida.
- **Fino a 2 proposer maliziosi** non possono rubare fondi o censurare.
- **La rotazione del set di proposer** può essere attuata tramite governance (aggiornamento futuro).

### 5.3 Sicurezza dei Signer di Governance

La spesa della tesoreria richiede 2-of-3 signer di governance. I signer possono essere aggiunti o rimossi tramite cascate di firme `(2-of-3) → (nuovo 2-of-3)`, garantendo la continuità della governance.

---

## 6. Stato dell'Implementazione

Tutti i componenti fondamentali sono implementati e integrati nel demone `mevacoind`:

| Componente | Stato | File |
|-----------|-------|------|
| FROST threshold signing | ✅ Completato | `frost_threshold.cpp`, `frost_broadcaster.cpp` |
| Distribuzione pool | ✅ Completato | `pool_distribution.cpp`, `reward_distributor.cpp` |
| Validazione coinbase | ✅ Completato | `mevatrust_coinbase_validator.cpp` |
| Registrazione nodi | ✅ Completato | `node_registry.cpp` |
| Sistema badge | ✅ Completato | `badge_system.cpp` |
| Circles (DAO) | ✅ Completato | `circle_registry.cpp` |
| Marketplace store | ✅ Completato | `mevatrust_store_registry.cpp` |
| Tesoreria governance | ✅ Completato | `foundation_vesting.h`, `blockchain.cpp` |
| Premine vesting | ✅ Completato | `foundation_vesting.h` |
| MevaTrust engine | ✅ Completato | `mevatrust_engine.cpp`, `mevatrust_manager.cpp` |
| Comandi wallet CLI | ✅ Completato | `simplewallet_mevatrust.cpp` |
| Endpoint RPC | ✅ Completato | `core_rpc_server.cpp` |
| Coordinazione P2P FROST | ✅ Completato | `frost_broadcaster.cpp`, protocol handlers |

**Cosa rimane:**
- Block explorer / dashboard di governance (layer di visualizzazione)
- Testnet pubblico con distribuzioni automatiche
- Deploy Docker per setup node one-command
- Test suite e benchmark di MEV resistance

---

## 7. Comparazione

| Funzionalità | MevaCoin | Monero | Zcash | Bitcoin |
|-------------|---------|-------|-------|---------|
| Transazioni private | ✓ RingCT + rings | ✓ | ✓ (shielded) | ✗ |
| MEV resistance | ✓ MevaTrust | ✗ | ✗ | ✗ |
| Ricompense in pool | ✓ 3% a nodi | ✗ | ✗ | ✗ |
| Governance DAO | ✓ Circles | ✗ | ✗ | ✗ |
| Marketplace on-chain | ✓ Store system | ✗ | ✗ | ✗ |
| Tail emission | ✓ | ✓ | ✗ | ✗ |
| ASIC resistance | ✓ RandomX | ✓ | ✗ | ✗ |

---

## 8. Roadmap di Lancio

1. **Fase 1 — Demo (corrente):** Demone e wallet funzionanti, tutti i sottosistemi implementati, governance integrata nella CLI.
2. **Fase 2 — Testnet:** Testnet pubblico con distribuzioni automatiche del pool, block explorer, deploy Docker.
3. **Fase 3 — Audit:** Audit di sicurezza di terze parti sull'implementazione FROST, sistema di governance e regole di consenso.
4. **Fase 4 — Mainnet:** Chiavi FROST generate da cerimonia, checkpoint hardcodati, seed nodes, integrazione exchange.
5. **Fase 5 — Ecosystem:** Wallet GUI, wallet mobile, strumenti per merchant, infrastruttura bridge.

---

## Riferimenti

- Monero Research Lab. "MRL-0001: A Note on Chain Reactions in Traceability."
- Komlo, C., Goldberg, I. "FROST: Flexible Round-Optimized Schnorr Threshold Signatures." (2020)
- van Saberhagen, N. "CryptoNote v 2.0." (2013)
- Noether, S. "Ring Signature Confidential Transactions." (2015)
- Dandelion++: Boosting Anonymity in Cryptocurrencies. (2020)
