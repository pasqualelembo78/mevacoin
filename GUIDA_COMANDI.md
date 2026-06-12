---
title: "MevaCoin — Riferimento Rapido"
subtitle: "Solo comandi curl e wallet CLI"
author: "MevaCoin Team"
date: "Giugno 2026"
toc: true
toc-depth: 2
geometry: margin=2cm
fontsize: 9pt
---

# RPC via curl

Tutti su `http://DAEMON_IP:18081/json_rpc` tranne `create_wallet`,
`get_address`, `get_balance` su `http://WALLET_IP:18082/json_rpc`.

## Nodi e Reputation

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_mevatrust_score",
       "params":{"node_id":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_node_uptime",
       "params":{"node_id":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_node_status",
       "params":{"node_id":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_badges",
       "params":{"node_id":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_penalty_history",
       "params":{"node_id":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_incentive_history",
       "params":{"node_id":"<hex>","limit":20,"offset":0}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_all_node_incentives",
       "params":{"limit":100}}'
```

## Registrazione Nodo

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"register_node",
       "params":{"public_key":"<hex>","signature":"<hex>","address":"<addr>",
                 "view_key_hex":"<hex>","proof_txid":"<hex>",
                 "proof_output_index":0,"node_public_key":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"unregister_node",
       "params":{"node_id":"<hex>","address":"<addr>","signature":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_incentive_pool_status",
       "params":{}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_eligible_nodes","params":{}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_reward_history",
       "params":{"node_id":"<hex>","count":20}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_reward_history_by_node",
       "params":{"node_id":"<hex>","limit":50}}'
```

## Cerchie

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_create",
       "params":{"name":"Nome","admin_pubkey":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_info",
       "params":{"circle_id":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_list",
       "params":{"wallet_pubkey":""}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_join",
       "params":{"circle_id":"<hex>","member_pubkey":"<hex>",
                 "caller_pubkey":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_leave",
       "params":{"circle_id":"<hex>","member_pubkey":"<hex>",
                 "caller_pubkey":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_change_admin",
       "params":{"circle_id":"<hex>","new_admin_pubkey":"<hex>",
                 "caller_pubkey":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_disband",
       "params":{"circle_id":"<hex>","caller_pubkey":"<hex>"}}'
```

## Votazioni Cerchie

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_proposal_list",
       "params":{"circle_id":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"circle_proposal_votes",
       "params":{"proposal_id":"<hex>"}}'
```

## Store

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"store_list",
       "params":{"active_only":true,"limit":20,"top":true}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"store_show",
       "params":{"store_id":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"store_search",
       "params":{"keyword":"testo","search_items":true}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"store_my_purchases",
       "params":{"buyer_pubkey":"<hex>"}}'
```

## Penalità

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"ban_node",
       "params":{"node_id":"<hex>","reason":"...","caller_pubkey":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"unban_node",
       "params":{"node_id":"<hex>","caller_pubkey":"<hex>"}}'
```

## Wallet di Base (su porta 18082)

```bash
curl -X POST http://127.0.0.1:18082/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"create_wallet",
       "params":{"filename":"wallet","password":"...","language":"Italian"}}'

curl -X POST http://127.0.0.1:18082/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_address",
       "params":{"account_index":0}}'

curl -X POST http://127.0.0.1:18082/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_balance",
       "params":{"account_index":0}}'
```

## Daemon (info generiche, porta 18081)

```bash
curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_info","params":{}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_block_count","params":{}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_height","params":{}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_block",
       "params":{"hash":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_transactions",
       "params":{"txs_hashes":["<hex>"],"decode_as_json":true}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"send_raw_transaction",
       "params":{"tx_as_hex":"<hex>"}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_connections","params":{}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_version","params":{}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"get_fee_estimate","params":{}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"sync_info","params":{}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"set_bans",
       "params":{"bans":[{"ip":12345678,"ban":true,"seconds":3600}]}}'

curl -X POST http://127.0.0.1:18081/json_rpc \
  -d '{"jsonrpc":"2.0","id":"0","method":"flush_txpool","params":{}}'
```

\newpage

# Wallet CLI

Tutti i comandi si eseguono dentro `wallet-cli` dopo aver aperto un wallet.

## Nodi

```
register_node
unregister_node <node_id_hex>
list_nodes
my_status

node_status [node_id_hex]
node_score [node_id_hex]
node_uptime [node_id_hex]
node_badges [node_id_hex]
node_penalties [node_id_hex]
incentive_history [node_id_hex] [count]
```

## Cerchie

```
circle_create "<nome>"
circle_info <circle_id_hex>
circle_list [wallet_pubkey_hex]
circle_join <circle_id_hex> <member_pubkey_hex>
circle_leave <circle_id_hex> [member_pubkey_hex]
circle_change_admin <circle_id_hex> <new_admin_pubkey_hex>
circle_disband <circle_id_hex>
```

## Votazioni Cerchie

```
circle_propose_change_admin <circle_id_hex> <new_admin_pk_hex> [reason]
circle_vote <proposal_id_hex> yes|no
circle_finalize_vote <proposal_id_hex>
circle_proposal_list <circle_id_hex>
```

## Store

```
store_list
store_show <store_id_hex>
store_search <keyword>
store_create "<nome>" "<descrizione>" [url]
store_update <store_id_hex> "<nome>" "<descrizione>" [url]
store_add_item <store_id_hex> "<nome>" <prezzo_mvc> [categoria]
store_delist <store_id_hex> <item_id_hex>
store_buy <store_id_hex> <item_id_hex>
store_my_stores
store_my_purchases
```

## Admin

```
ban_node <node_id_hex> [reason]
unban_node <node_id_hex>
```

## Wallet Standard (selezione)

```
wallet_info
status
balance [detail]
address [index]
transfer <address> <amount> [payment_id]
sweep_all <address>
show_transfers [in/out/all] [min] [max]
payments <payment_id>
save
refresh
rescan_bc
help [comando]
```

---

*MevaCoin v1.0 - 2026-06-09*
