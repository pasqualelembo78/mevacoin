---
title: "Guida Completa a MevaCoin"
subtitle: "Manuale Operativo --- RPC, Wallet CLI, e Integrazioni MevaTrust"
author: "MevaCoin Team"
date: "Giugno 2026"
toc: true
toc-depth: 3
numbersections: true
geometry: margin=2.5cm
fontsize: 10pt
---

# Introduzione

**MevaCoin** è una fork di Monero con un sistema di partecipazione a prova di
Sybil, incentivi on-chain per operatori di nodi, reputation scoring, badge,
circoli sociali, e un marketplace decentralizzato (Store on-chain).

Tutte le operazioni MevaTrust sono:

- **On-chain**: ogni operazione è una transazione firmata, registrata
  permanentemente nella blockchain.
- **Immutabile**: una volta confermata, nessuno può modificarla.
- **Trasparente**: ogni nodo può verificare lo stato interrogando i propri
  RPC locali.

## Convenzioni usate in questo documento

| Simbolo | Significato |
|---------|-------------|
| `DAEMON_IP` | Indirizzo IP del daemon (es. `127.0.0.1` o `188.34.22.11`) |
| `DAEMON_PORT` | Porta RPC del daemon (default: `18081`) |
| `WALLET_IP` | Indirizzo IP del wallet RPC (default: `127.0.0.1`) |
| `WALLET_PORT` | Porta RPC del wallet (default: `18082`) |
| `<hex>` | Stringa esadecimale (64 caratteri per hash/pubkey) |
| `"..."` | Parametro stringa da passare con virgolette |

\newpage

# 1. Setup e Primi Passi

## 1.1 Avviare il Daemon

```bash
./mevacoind --data-dir /path/to/data --rpc-bind-ip 0.0.0.0 --rpc-bind-port 18081
```

## 1.2 Creare un Wallet

Tramite RPC:

```bash
curl -X POST http://127.0.0.1:18082/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"create_wallet",
       "params":{"filename":"mio_wallet","password":"supersicura","language":"Italian"}}'
```

Tramite CLI:

```bash
./wallet-cli --daemon-address 127.0.0.1:18081 --wallet-file mio_wallet
```

All'interno del wallet:
```
wallet-cli> create_wallet
```

## 1.3 Ottenere l'indirizzo del wallet

```bash
curl -X POST http://127.0.0.1:18082/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_address","params":{"account_index":0}}'
```

```
wallet-cli> address
```

## 1.4 Verificare il saldo

```bash
curl -X POST http://127.0.0.1:18082/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_balance","params":{"account_index":0}}'
```

```
wallet-cli> balance
```

## 1.5 Verificare lo stato del daemon

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_info","params":{}}'
```

```
wallet-cli> status
```

\newpage

# 2. Comandi RPC Standard Monero (via curl)

Tutti i comandi RPC standard sono accessibili via HTTP POST su
`http://DAEMON_IP:DAEMON_PORT/json_rpc`.

## 2.1 Blockchain

### Ottenere l'altezza della blockchain

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_block_count","params":{}}'
```

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_height","params":{}}'
```

### Ottenere un blocco per hash

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_block","params":{"hash":"<block_hash_hex>"}}'
```

Per altezza:

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_block_header_by_height","params":{"height":123456}}'
```

### Ottenere l'ultimo header

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_last_block_header","params":{}}'
```

## 2.2 Info Generali

### Info nodo (connessioni, sincronizzazione, versione)

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_info","params":{}}'
```

### Versione del software

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_version","params":{}}'
```

### Stima della fee

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_fee_estimate","params":{}}'
```

## 2.3 Transazioni

### Ottenere transazioni per hash

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_transactions",
       "params":{"txs_hashes":["<tx_hash_hex>"],"decode_as_json":true}}'
```

### Ottenere il pool di transazioni in attesa

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_transaction_pool","params":{}}'
```

### Inviare una transazione grezza

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"send_raw_transaction",
       "params":{"tx_as_hex":"<tx_hex_blob>"}}'
```

## 2.4 Connessioni

### Elenco connessioni peer

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_connections","params":{}}'
```

### Informazioni di sincronizzazione

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"sync_info","params":{}}'
```

## 2.5 Amministrazione (richiedono `--restricted-rpc`)

### Bannare un IP

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"set_bans",
       "params":{"bans":[{"ip":12345678,"ban":true,"seconds":3600}]}}'
```

### Svuotare il pool di transazioni

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"flush_txpool","params":{}}'
```

\newpage

# 3. Comandi RPC MevaTrust (via curl)

Endpoints aggiuntivi di MevaCoin. Tutti accessibili via
`http://DAEMON_IP:DAEMON_PORT/json_rpc`.

## 3.1 Nodi e Reputation

### Stato di un nodo

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_node_status",
       "params":{"node_id":"<node_id_hex>"}}'
```

**Risposta:**
```json
{
  "node_id": "<hex>",
  "is_active": true,
  "is_synced": true,
  "last_seen": 1718000000,
  "status": "OK"
}
```

### Reputation Score

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_mevatrust_score",
       "params":{"node_id":"<node_id_hex>"}}'
```

**Risposta:**
```json
{
  "node_id": "<hex>",
  "score": 0.95,
  "status": "OK"
}
```

> Lo score va da `0.0` (pessimo) a `1.0` (perfetto).
> Soglie: `>= 0.8` --> ALTA, `>= 0.5` --> MEDIA, `< 0.5` --> BASSA.

### Uptime del nodo

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_node_uptime",
       "params":{"node_id":"<node_id_hex>"}}'
```

**Risposta:**
```json
{
  "node_id": "<hex>",
  "uptime_seconds": 86400,
  "uptime_percentage": 99.7,
  "status": "OK"
}
```

### Badge del nodo

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_badges",
       "params":{"node_id":"<node_id_hex>"}}'
```

**Risposta:**
```json
{
  "node_id": "<hex>",
  "badges": ["EARLY_ADOPTER", "STABLE_30D", "LONG_UPTIME_90D"],
  "status": "OK"
}
```

### Storico incentivi (badge e rewards)

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_incentive_history",
       "params":{"node_id":"<node_id_hex>","limit":20,"offset":0}}'
```

### Penalità del nodo

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_penalty_history",
       "params":{"node_id":"<node_id_hex>"}}'
```

### Riepilogo di tutti i nodi

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_all_node_incentives",
       "params":{"limit":100}}'
```

## 3.2 Registrazione Nodo (on-chain)

### Registrare un nodo

La registrazione richiede una prova UTXO (output non speso \textgreater{}= 10 MVC).

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{
    "jsonrpc":"2.0",
    "id":"0",
    "method":"register_node",
    "params":{
      "public_key":"<wallet_spend_pubkey_hex>",
      "signature":"<firma_H(node_id || node_pubkey)>",
      "address":"<indirizzo_wallet>",
      "view_key_hex":"<chiave_privata_view_hex>",
      "proof_txid":"<txid_prova_possesso_10MVC>",
      "proof_output_index":0,
      "node_public_key":"<chiave_p2p_nodo_hex>"
    }
  }'
```

### De-registrare un nodo

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{
    "jsonrpc":"2.0",
    "id":"0",
    "method":"unregister_node",
    "params":{
      "node_id":"<node_id_hex>",
      "address":"<indirizzo_wallet>",
      "signature":"<firma_H(node_id || deregister)>"
    }
  }'
```

### Stato pool incentivi

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_incentive_pool_status","params":{}}'
```

## 3.3 Cerchie (Circle)

### Creare una cerchia

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_create",
       "params":{"name":"La Mia Cerchia","admin_pubkey":"<wallet_pubkey_hex>"}}'
```

### Info su una cerchia

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_info",
       "params":{"circle_id":"<circle_id_hex>"}}'
```

### Elenco cerchie

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_list",
       "params":{"wallet_pubkey":""}}'
```

> Se `wallet_pubkey` è vuoto, mostra tutte. Se specificato, filtra.

## 3.4 Store (Negozio On-Chain)

### Elenco negozi (top 20 per item_count)

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"store_list",
       "params":{"active_only":true,"limit":20,"top":true}}'
```

### Dettaglio negozio

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"store_show",
       "params":{"store_id":"<store_id_hex>"}}'
```

### Ricerca negozi

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"store_search",
       "params":{"keyword":"badge","search_items":true}}'
```

> Cerca per nome, descrizione, URL, proprietario, nome prodotto e categoria.

### Acquisti effettuati da un wallet

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"store_my_purchases",
       "params":{"buyer_pubkey":"<wallet_pubkey_hex>"}}'
```

\newpage

# 4. Comandi Wallet CLI Standard Monero

Tutti i comandi si eseguono all'interno del `wallet-cli` dopo aver aperto un wallet.

## 4.1 Gestione Wallet

| Comando | Descrizione | Uso |
|---------|-------------|-----|
| `wallet_info` | Mostra le informazioni del wallet | `wallet_info` |
| `status` | Stato del wallet (connessione, altezza) | `status` |
| `balance` | Saldo del wallet | `balance [detail]` |
| `address` | Mostra l'indirizzo principale | `address [index]` |
| `integrated_address` | Crea/decodifica indirizzo integrato | `integrated_address [payment_id]` |
| `viewkey` | Mostra la chiave privata view | `viewkey` |
| `spendkey` | Mostra la chiave privata spend | `spendkey` |
| `seed` | Mostra il seed mnemonico | `seed` |
| `password` | Cambia la password del wallet | `password` |
| `save` | Salva il wallet su disco | `save` |
| `refresh` | Sincronizza il wallet | `refresh` |

## 4.2 Transazioni

| Comando | Descrizione | Uso |
|---------|-------------|-----|
| `transfer` | Invia MVC | `transfer <address> <amount> [payment_id]` |
| `sweep_all` | Invia tutto il saldo | `sweep_all <address>` |
| `sweep_unmixable` | Invia output non mescolabili | `sweep_unmixable` |
| `donate` | Dona al team di sviluppo | `donate <amount>` |
| `show_transfers` | Mostra cronologia transazioni | `show_transfers [in/out/all] [min] [max]` |
| `payments` | Cerca pagamenti per payment ID | `payments <payment_id>` |
| `get_tx_key` | Ottiene la chiave di una TX | `get_tx_key <txid>` |
| `check_tx_key` | Verifica una TX | `check_tx_key <txid> <tx_key> <address>` |
| `sign_transfer` | Firma TX da file | `sign_transfer <file>` |
| `submit_transfer` | Invia TX firmata da file | `submit_transfer <file>` |

## 4.3 Mining

| Comando | Descrizione | Uso |
|---------|-------------|-----|
| `start_mining` | Avvia mining | `start_mining [threads]` |
| `stop_mining` | Ferma mining | `stop_mining` |

## 4.4 Multisig

| Comando | Descrizione | Uso |
|---------|-------------|-----|
| `prepare_multisig` | Prepara wallet multisig | `prepare_multisig` |
| `make_multisig` | Crea wallet multisig | `make_multisig <params>` |
| `export_multisig_info` | Esporta info multisig | `export_multisig_info <filename>` |
| `import_multisig_info` | Importa info multisig | `import_multisig_info <filename>` |
| `sign_multisig` | Firma TX multisig | `sign_multisig <file>` |
| `submit_multisig` | Invia TX multisig | `submit_multisig <file>` |

## 4.5 Altri comandi

| Comando | Descrizione |
|---------|-------------|
| `help` | Mostra aiuto completo |
| `apropos <keyword>` | Cerca comandi che contengono `keyword` |
| `set <option> <value>` | Imposta opzioni (es. `set priority 2`) |
| `rescan_bc` | Riscansiona la blockchain da capo |
| `rescan_spent` | Rileggi output spesi |
| `freeze <key_image>` | Congela un output |
| `thaw <key_image>` | Scongela un output |
| `lock` | Blocca la console (richiede password) |
| `version` | Versione del wallet |
| `net_stats` | Statistiche di rete |

\newpage

# 5. Comandi Wallet CLI MevaTrust

## 5.1 Nodi e Registrazione

### `register_node`

Registra il nodo nella rete MevaTrust. Richiede:
1. Il daemon avviato (genera la chiave in `~/.mevacoin/mevatrust/node_signing_key`)
2. Saldo \textgreater{}= 10 MVC per la prova Anti-Sybil

```
register_node
```

Non servono argomenti: il wallet legge la chiave del nodo e la propria chiave
spend, calcola il `node_id`, firma e invia la transazione 0xA0 on-chain.

### `unregister_node <node_id>`

Rimuove un nodo dalla rete. Richiede conferma interattiva ("si").

```
unregister_node a1b2c3d4...
```

### `list_nodes`

Mostra tutti i nodi registrati, badge, rewards, score.

```
list_nodes
```

### `node_status [node_id]`

Stato del nodo (attivo, sincronizzato, ultimo visto). Senza argomenti
usa il nodo locale.

```
node_status
node_status a1b2c3d4...
```

### `node_score [node_id]`

Reputation score (0.0--1.0). Colorato in verde/giallo/rosso.

```
node_score
```

### `node_uptime [node_id]`

Uptime in secondi, ore e percentuale.

```
node_uptime
```

### `node_badges [node_id]`

Badge on-chain assegnati al nodo. Ogni badge è colorato per tipo:
- Viola --> `LONG_UPTIME`
- Blu --> `STABLE`
- Giallo --> `CORE`
- Verde --> `ACTIVE`
- Rosso --> `EARLY`

```
node_badges
```

### `node_penalties [node_id]`

Storico penalità con tipo, importo e motivazione.

```
node_penalties
```

### `incentive_history [node_id] [count]`

Storico completo incentivi: badge ricevuti e reward in satoshi.

```
incentive_history
incentive_history a1b2c3d4... 50
```

## 5.2 Dashboard

### `my_status`

Dashboard riepilogativa che mostra in un unico pannello:
- Stato nodo
- Reputation score
- Penalità (totale e conteggio)
- Badge attivi
- Uptime
- Incentivi (riepilogo rewards)
- Negozi posseduti
- Acquisti effettuati

```
my_status
```

## 5.3 Cerchie (Circle)

### `circle_create "<nome>"`

Crea una nuova cerchia. Il wallet diventa admin.

```
circle_create "Validatori Italia"
```

### `circle_info <circle_id>`

Mostra i dettagli di una cerchia (nome, admin, membri).

```
circle_info a1b2c3d4...
```

### `circle_list [wallet_pubkey]`

Elenco cerchie. Se specificato un pubkey, filtra quelle di cui
il wallet è membro.

```
circle_list
circle_list a1b2c3d4...
```

### `circle_join <circle_id> <member_pubkey>`

Aggiunge un membro a una cerchia. Devi essere admin.

```
circle_join a1b2c3d4... ffeeddcc...
```

### `circle_leave <circle_id> [member_pubkey]`

Rimuove un membro. Senza `member_pubkey`, rimuove il wallet corrente.

```
circle_leave a1b2c3d4...
circle_leave a1b2c3d4... ffeeddcc...
```

### `circle_change_admin <circle_id> <new_admin_pubkey>`

Trasferisce l'admin a un altro wallet.

```
circle_change_admin a1b2c3d4... aabbccdd...
```

### `circle_disband <circle_id>`

Scioglie (cancella) una cerchia. Solo admin.

```
circle_disband a1b2c3d4...
```

## 5.4 Store (Negozio On-Chain)

### `store_list`

Mostra i top 20 negozi ordinati per numero di item. Include nome,
descrizione, URL, item count e proprietario.

```
store_list
```

### `store_search <keyword>`

Cerca negozi in tutti i campi: nome, descrizione, URL, proprietario,
nome/categoria prodotto. La ricerca è case-insensitive.

```
store_search badge
store_search mario
```

### `store_show <store_id>`

Dettaglio completo del negozio con tutti gli item disponibili.

```
store_show a1b2c3d4...
```

### `store_create "<nome>" "<descrizione>" [url]`

Crea un nuovo negozio on-chain. L'URL è opzionale.

```
store_create "Mario's Shop" "Badge e item premium" "https://miosito.it"
```

> Attenzione: la creazione richiede un deposito di **10 MVC** nella TX.

### `store_update <store_id> "<nome>" "<descrizione>" [url]`

Aggiorna nome, descrizione e URL del negozio. Solo il creatore può farlo.

```
store_update a1b2c3d4... "Nuovo Nome" "Nuova descrizione" "https://nuovourl.it"
```

### `store_add_item <store_id> "<nome>" <prezzo_mvc> [categoria]`

Aggiunge un item in vendita. Massimo 100 item per negozio. Deposito 1 MVC.

```
store_add_item a1b2c3d4... "Badge Oro" 50 "Badge"
```

### `store_delist <store_id> <item_id>`

Rimuove un item dalla vendita. Solo il proprietario del negozio.

```
store_delist a1b2c3d4... item1234...
```

### `store_buy <store_id> <item_id>`

Acquista un item. Il wallet invia una TX con pagamento \textgreater{}= prezzo item.
La transazione include un output per il venditore.

```
store_buy a1b2c3d4... item1234...
```

### `store_my_stores`

Mostra i negozi di cui sei proprietario.

```
store_my_stores
```

### `store_my_purchases`

Mostra la cronologia dei tuoi acquisti on-chain.

```
store_my_purchases
```

## 5.5 Admin / Penalità

### `ban_node <node_id> [reason]`

Banna un nodo dalla rete (azione admin). Impatto score: -1000.

```
ban_node a1b2c3d4... "Violazione termini"
```

### `unban_node <node_id>`

Rimuove il ban da un nodo.

```
unban_node a1b2c3d4...
```

\newpage

# 6. Come Creare e Gestire un Negozio --- Tutorial Passo-Passo

## 6.1 Creare un Negozio

```
wallet-cli> store_create "Mario's Gadget" "Badge e item per la community" "https://mario.it"
Negozio creato! TXID: abc123...
```

Prendi nota del `store_id` (lo vedrai con `store_my_stores`).

## 6.2 Aggiungere Item

```
wallet-cli> store_add_item <store_id> "Badge Platino" 100 "Badge"
Item listato! TXID: def456...

wallet-cli> store_add_item <store_id> "Badge Argento" 30 "Badge"
Item listato! TXID: ghi789...
```

## 6.3 Il Negozio è Visibile a Tutti

Qualsiasi nodo può vedere il tuo negozio:

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"store_list","params":{}}'
```

O cercarlo:

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"store_search","params":{"keyword":"Mario"}}'
```

## 6.4 Acquistare un Item

```
wallet-cli> store_buy <store_id> <item_id>
```

La TX deve contenere un output di valore \textgreater{}= prezzo item. Il pagamento va
direttamente al venditore. L'acquisto è registrato on-chain.

## 6.5 Aggiornare il Negozio

```
wallet-cli> store_update <store_id> "Mario's Super Shop" "Badge premium" "https://nuovosite.it"
```

## 6.6 Rimuovere un Item

```
wallet-cli> store_delist <store_id> <item_id>
```

\newpage

# 7. Come Registrare un Nodo --- Tutorial Passo-Passo

## 7.1 Prerequisiti

1. Daemon **avviato** (genera automaticamente la chiave del nodo)
2. Wallet con **saldo \textgreater{}= 10 MVC**
3. Una transazione confermata con output \textgreater{}= 10 MVC

## 7.2 Verificare la Chiave del Nodo

Il daemon genera la chiave in `~/.mevacoin/mevatrust/node_signing_key`.

```
wallet-cli> register_node
```

Nessun argomento: il wallet:
1. Legge la chiave del nodo
2. Calcola il node_id = H(wallet_pubkey || node_pubkey || timestamp)
3. Firma e invia la TX 0xA0 on-chain
4. Salva il node_id in `~/.mevacoin/mevatrust/node_id`

## 7.3 Verificare la Registrazione

```
wallet-cli> my_status
```

Oppure via RPC:

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_node_status",
       "params":{"node_id":"<node_id_dalla_registrazione>"}}'
```

## 7.4 Ottenere Badge e Incentivi

I badge vengono assegnati automaticamente dal sistema in base all'uptime e
all'affidabilità:

- **EARLY_ADOPTER**: primi nodi registrati
- **STABLE_30D**: 30 giorni consecutivi di uptime
- **LONG_UPTIME_90D**: 90 giorni di uptime
- **CORE_CONTRIBUTOR**: contributi speciali
- **ACTIVE_VALIDATOR**: validatore attivo

```
wallet-cli> my_status
```

\newpage

# 8. Appendice --- Riepilogo Completo

## 8.1 Tutti i Tag On-Chain MevaTrust

| Tag | Nome | Descrizione | Operazioni |
|-----|------|-------------|------------|
| `0xA0` | Registration | Registrazione nodo | `register` |
| `0xA1` | Deregistration | De-registrazione nodo | `deregister` |
| `0xA2` | Snapshot | Snapshot stato badge/rewards | `snapshot` |
| `0xA3` | Circle | Gestione cerchie | `create, join, leave, change_admin, disband` |
| `0xA4` | Penalty | Penalità e ban | `penalty, ban, unban` |
| `0xA5` | Uptime | Commitment uptime | `uptime_commit` |
| `0xA6` | Challenge | Sfida P2P | `challenge, response` |
| `0xA7` | State Root | Radice stato MevaTrust | `state_root` |
| `0xA8` | Store | Negozio on-chain | `store_create, store_update, item_list, item_delist, item_buy` |

## 8.2 Categorie di Item Negozio

La categoria è un campo libero (stringa). Esempi di categorie:

| Categoria | Descrizione |
|-----------|-------------|
| `Badge` | Badge premium/collezionabili |
| `NFT` | Token non fungibili |
| `Servizio` | Servizi (hosting, consulenza) |
| `Virtual Goods` | Beni virtuali |
| `Membership` | Abbonamenti |

## 8.3 Limiti di Sistema

| Parametro | Valore |
|-----------|--------|
| Deposito creazione negozio | 10 MVC |
| Deposito listino item | 1 MVC |
| Max item per negozio | 100 |
| Anti-Sybil minimo | 10 MVC |
| Max nodi per wallet | 3 |
| Cooldown registrazione | 1 ora |
| Sospensione dopo fail | 24 ore |
| Max fail consecutivi | 5 |
| Quorum witness | 3/3 (unanime) |

## 8.4 Conversione Unità

```
1 MVC           = 1.000.000.000.000 atomic units (10^12)
0.001 MVC (fee) =   1.000.000.000 atomic units
Prezzo item 50 MVC = 50.000.000.000.000 atomic units
```

\newpage

# 9. Appendice --- Riferimento Rapido Wallet

## 9.1 Comandi MevaTrust in ordine alfabetico

| Comando | Pagina | Categoria |
|---------|--------|-----------|
| `ban_node <node_id> [reason]` | §5.5 | Admin |
| `circle_change_admin <cid> <pk>` | §5.3 | Cerchie |
| `circle_create "<name>"` | §5.3 | Cerchie |
| `circle_disband <circle_id>` | §5.3 | Cerchie |
| `circle_info <circle_id>` | §5.3 | Cerchie |
| `circle_join <cid> <pk>` | §5.3 | Cerchie |
| `circle_leave <cid> [pk]` | §5.3 | Cerchie |
| `circle_list [pubkey]` | §5.3 | Cerchie |
| `incentive_history [node_id] [n]` | §5.1 | Nodi |
| `list_nodes` | §5.1 | Nodi |
| `my_status` | §5.2 | Dashboard |
| `node_badges [node_id]` | §5.1 | Nodi |
| `node_penalties [node_id]` | §5.1 | Nodi |
| `node_score [node_id]` | §5.1 | Nodi |
| `node_status [node_id]` | §5.1 | Nodi |
| `node_uptime [node_id]` | §5.1 | Nodi |
| `register_node` | §5.1 | Nodi |
| `store_add_item <sid> "<name>" <price> [cat]` | §5.4 | Store |
| `store_buy <sid> <item_id>` | §5.4 | Store |
| `store_create "<name>" "<desc>" [url]` | §5.4 | Store |
| `store_delist <sid> <item_id>` | §5.4 | Store |
| `store_list` | §5.4 | Store |
| `store_my_purchases` | §5.4 | Store |
| `store_my_stores` | §5.4 | Store |
| `store_search <keyword>` | §5.4 | Store |
| `store_show <store_id>` | §5.4 | Store |
| `store_update <sid> "<name>" "<desc>" [url]` | §5.4 | Store |
| `unban_node <node_id>` | §5.5 | Admin |
| `unregister_node <node_id>` | §5.1 | Nodi |

## 9.2 Comandi Standard Monero più usati

| Comando | Descrizione |
|---------|-------------|
| `balance` | Mostra saldo |
| `address` | Mostra indirizzo |
| `transfer <addr> <amount>` | Invia MVC |
| `status` | Stato connessione |
| `refresh` | Sincronizza |
| `show_transfers` | Cronologia |
| `save` | Salva wallet |
| `rescan_bc` | Riscansiona blockchain |
| `help [comando]` | Aiuto su un comando |

---

*Documento generato il 2026-06-09. MevaCoin v1.0.*
