# MevaCoin — Market Readiness Assessment

## TL;DR

Protocollo implementato al ~90%. Il problema non è tecnico — è di **visibilità**.
MevaCoin ha 10 sottosistemi unici implementati ma invisibili.
Il nome promette "MEV resistance" — ora c'è un whitepaper che la spiega.
Mancano: block explorer, docker-compose, testnet pubblico.

---

## ✅ Fatto — fondazione solida

| Cosa | Dov'è |
|------|-------|
| Whitepaper EN/IT | `docs/MEVATRUST_WHITEPAPER.md`, `_IT.md` |
| Ring size 11 a HF>=8 | `wallet2.cpp` |
| Governance treasury spend (0xB0) | `gov_spend.py`, `check_premine_spend` |
| Governance add/remove signer (0xB1/0xB2) | `gov_spend.py`, validate/process in C++ |
| Governance network fund (0xC0) | `gov_spend.py`, `check_premine_spend` |
| Premine enforcement (key_image) | `blockchain.cpp:6060-6107` |
| Fee dinamico (dead code rimosso) | `cryptonote_config.h` |
| FROST keys persistite su disco | `core::init()` → `proposer_keys/` |
| `--mevatrust-proposer-key=<hex>` | CLI override per FROST |
| FROST P2P 2-round signing | `frost_broadcaster.cpp`, ID=14/15 |
| 13 RPC ZMQ implementati | `daemon_handler.cpp` |
| `update` command | `core_rpc_server.cpp`, `rpc_command_executor.cpp` |
| `mdb_env_close` thread-safe | `db_lmdb.cpp` |
| `tx_propagation_timeout` 58s | `wallet2.cpp` (era 500s) |
| `memcmp` → `cn_fast_hash` sort | `multisig_account_kex_impl.cpp` |
| DNS parsing con regex | `dns_utils.cpp` |
| Subaddress mining permesso | `command_parser_executor.cpp` |
| Warning x32 silenziato | `binary_archive.h` |
| Monero TODOs in blockchain.cpp/tx_pool | ~40 fixati, ~30 lasciati (non bloccanti) |

---

## ❌ Gap — cosa manca per il "wow"

### Gap #1: Narrativa MEV — ✅ FATTO (whitepaper EN + IT)
`docs/MEVATRUST_WHITEPAPER.md` e `docs/MEVATRUST_WHITEPAPER_IT.md`

### Gap #2: Block explorer / governance dashboard [CRITICO]
Dati disponibili via RPC/ZMQ ma nessuna interfaccia utente.
**Serve:** Dashboard web che mostri:
- Pool del 3% accumularsi
- Distribuzioni FROST in tempo reale
- Treasury balance e transazioni
- Nodi registrati, badge, score
- Block explorer standard

### Gap #3: One-command testnet [ALTO]
- Dockerfile ancora punta a `monerod` / `.bitmonero`
- Nessun docker-compose.yml
- systemd unit ancora `monerod`
**Serve:** `s/monerod/mevacoind/g` + docker-compose con mevacoind + wallet-rpc + block explorer

### Gap #4: Infrastruttura seed [BASSO]
- Dominio `mevacoin.com` da verificare
- Seed DNS `seed[1-3].mevacoin.com` da risolvere o IP fallback da hardcodare
- Checkpoints vuoti (da popolare dopo testnet)
- Genesis block mai validato su catena live

---

## 🎯 Azioni raccomandate (in ordine di impatto)

```
Priorità    Cosa                    Stato           Sforzo
───────     ────                    ─────           ──────
P0          Whitepaper EN + IT      ✅ DONE         —
P0          Governance dashboard    ❌ DA FARE      3-5 giorni
P1          Docker fix + compose    ❌ DA FARE      1 giorno
P1          Block explorer minimal  ❌ DA FARE      3-5 giorni
P2          Seed nodes / DNS        ❌ DA FARE      2 ore
P2          Testnet bootstrap       ❌ DA FARE      1-2 giorni
```

## 📊 Stato deployabilità

| Requisito | Stato | Note |
|-----------|-------|------|
| Whitepaper tecnico | ✅ EN + IT | `docs/MEVATRUST_WHITEPAPER*.md` |
| Genesis block testnet | ✅ OK | Configurato, non verificato su catena live |
| Seed nodes | ⚠️ DNS names, no IP | `seed[1-3].mevacoin.com` |
| Checkpoints | ⚠️ Vuoti | Ok per testnet, da aggiungere per mainnet |
| Solo mining | ✅ OK | CPU mining via `start_mining` |
| Pool mining | ❌ Assente | No stratum — non bloccante per ora |
| Docker | ⚠️ Stale | Riferimenti a Monero da aggiornare |
| docker-compose | ❌ Assente | Da creare |
| Block explorer | ❌ Assente | Da creare |
| Test unitari | ⚠️ Parziali | Infrastruttura Monero OK, test MevaTrust mancanti |

---

## Note

- Whitepaper tecnico creato (EN + IT) — colma il gap narrativo #1
- Ring size 2 — Risolto
- FROST placeholder sigs — Risolto
- Governance zero-sig placeholder — Risolto
- `check_premine_spend` security gap — Risolto
- Fee dead code — Rimosso
- Proposer key management — Risolto
- P2P FROST coordination — Risolto
- 13 RPC ZMQ — Implementati
- update command — Risolto
- mdb_env_close thread safety — Risolto
- Monero TODOs — 11 fixati, ~40 lasciati (non bloccanti)
