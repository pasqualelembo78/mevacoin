# Guida Spesa Premine — MevaCoin

## Cosa c'e' nel premine

Al genesis sono stati creati 3 output (1M MVC total):

| Fondo          | MVC    | Atomic units         | Indirizzo (deterministico, nessuno ha la chiave) | Come si spende                          |
|----------------|--------|----------------------|--------------------------------------------------|-----------------------------------------|
| **Team Lock**  | 200.000 | 200000000000000      | Derivato da `mevacoin_team_lock`                 | Chiave privata del wallet fondatore. Bloccato 24 mesi (518.400 blocchi). |
| **Treasury**   | 400.000 | 400000000000000      | Derivato da `mevacoin_governance`                | 2 firme su 3 signer governance in tx_extra. |
| **Network**    | 400.000 | 400000000000000      | Derivato da `mevacoin_network_fund`              | Nessuna firma. Rate-limited: 10.000 MVC / 30gg. |

---

## Wallet & Chiavi

### Wallet Signer Governance (3 firmatari)

Servono per firmare transazioni Treasury. Ci vogliono **2 firme su 3** per autorizzare.

|               | Signer 0                                      | Signer 1                                      | Signer 2                                      |
|---------------|-----------------------------------------------|-----------------------------------------------|-----------------------------------------------|
| **Password**  | `pass_signer_0`                               | `pass_signer_1`                               | `pass_signer_2`                               |
| **Indirizzo** | `MD5VJcujdh5LhN5tZ3W4c25afTsvWKZh3NVGDtSD1N7iYHYW96nHbFiAjCPmK3KcRVENRFA6NXbhdXXTCyBWBXuSJMUjfLc` | `MDdsyRtQeukfbfJouCU1sNEpX8fm9qjuDYfi1M7WNsUt8ASsWwzibAef9RVoCwuG4McMaChrjbdthLJN3zeSV8snPjXWHaE` | `M5hfHudn48aTGTAdqQ3AXfhoERnZ7wKNpDKy7G5yYXN2W8vyEqqEooAEy93mMLunAAjmmKpqgHVgEJCXjr5wRZP3SiciU2c` |
| **Priv spend** | `6c90f4fc20bde8dbe0eb2a4d8c9948174d5f0cdb85cccaf536752de2285c4402` | `a6c9c2103d56ff0f4971f2c0705310cfc051d7fbd765d640c7ff618b1244d60c` | `90137ff7a5ea576d2fc39332e792d292823fc0c1f93f18bc549c4ced11be6c03` |
| **Priv view**  | `7d33dacb480e6ccbbcb7e36a8aad170c71563af24335ff7b5b4bc914ed291601` | `018d92d7442dfc1058829c2d7948dc6ad9210368e22ebb848b20180dc567720d` | `1b9d06e6bf9bf51f748976c5035d698c6d90d75b85784791e5cb763c0e8cd00f` |
| **Pub spend**  | `d12e990816a51475c1151ff04a4dc31b62693fbf2bf2d28076cda44e784699bb` | `dfeb3f3ce8c3efe6c28b5670d365d1529ec6032dd2aa3cbd53a8595be9b7312a` | `0e88576abcefeb9d095a9e4db59376f3e8eed036eae68949b2ce4253444493ae` |
| **Pub view**   | `0aca48dbce5a0d3a2835b0879574a84fedff620f76705cb67f4b1f8f229e3999` | `d2ef03909ecaefe40e29f668822abed35f36750f49c8d6736222bec5bfbecdc9` | `327811a721183553823883298d1e37ffb742f3b014158366d32a3e7c81c05ee3` |
| **File wallet** | `test_wallets/signer_0`                      | `test_wallets/signer_1`                      | `test_wallets/signer_2`                      |
| **File .keys**  | `test_wallets/signer_0.keys`                  | `test_wallets/signer_1.keys`                  | `test_wallets/signer_2.keys`                  |

### Wallet Fondatore (Team Lock)

Per spendere il Team Lock servono la chiave privata del wallet che ha creato la genesis transaction e il wallet deve essere aperto con `mevacoin-wallet-rpc`.

### Indirizzi Treasury / Network Fund (NESSUNO ha la chiave privata)

Questi indirizzi sono calcolati deterministicamente. I fondi si muovono SOLO attraverso il meccanismo di governance (tx_extra con tag 0xB0 / 0xC0).

```
Treasury address (mainnet):  (spend = view = hash("mevacoin_governance" + nettype))
Network fund address (mainnet): (spend = view = hash("mevacoin_network_fund" + nettype))
```

---

## Tool creati

| Tool | Path | Descrizione |
|------|------|-------------|
| `gov_crypto` | `tools/governance_spend/gov_crypto` | C++ binario per crittografia Ed25519 (firme, chiavi, decode indirizzi) |
| `gov_spend.py` | `tools/governance_spend/gov_spend.py` | Script Python che orchestra gov_crypto |

### Compilazione gov_crypto

```bash
# Dopo aver fatto make del progetto principale:
cd /root/mevacoin
g++ -std=c++17 -o tools/governance_spend/gov_crypto \
  tools/governance_spend/gov_crypto.cpp \
  -I src -I build/Linux/mevacoin/release/generated \
  -I contrib/epee/include -I external/easylogging++ \
  -I build/Linux/mevacoin/release/translations \
  -I build/Linux/mevacoin/release/external/easylogging++ \
  build/Linux/mevacoin/release/src/crypto/libcncrypto.a \
  build/Linux/mevacoin/release/src/common/libcommon.a \
  build/Linux/mevacoin/release/contrib/epee/src/libepee.a \
  build/Linux/mevacoin/release/external/easylogging++/libeasylogging.a \
  -lssl -lcrypto -lpthread -lboost_system -ldl \
  -lboost_filesystem -lboost_thread -lboost_regex \
  -lboost_chrono -lboost_date_time -lunbound
```

### Comandi gov_spend.py

```bash
cd /root/mevacoin/tools/governance_spend

# Generare una nuova coppia di chiavi
./gov_spend.py genkey

# Ottenere la chiave pubblica da una privata
./gov_spend.py pubkey <privkey_hex>

# Decodificare un indirizzo MevaCoin in chiavi pubbliche
./gov_spend.py decode <indirizzo>

# Transazione Network Fund (SENZA firme)
./gov_spend.py network <amount_atomic> <indirizzo_destinazione>

# Transazione Treasury - FASE 1: blob con firme zero
./gov_spend.py treasury-zero <amount> <indirizzo> <pub_signer0> [pub_signer1 ...]

# Transazione Treasury - FASE 2: firmare l'hash
./gov_spend.py sign <tx_prefix_hash_hex> <priv_signer0> [priv_signer1 ...]

# Transazione Treasury - FASE 3: costruire blob finale con firme reali
./gov_spend.py treasury-build <amount> <indirizzo> <pub_signer0> '<sigs_json>'

# Verificare le firme in un blob governance
./gov_spend.py verify <tx_extra_hex> <tx_prefix_hash_hex>
```

---

## ESEMPIO 1: Transazione Treasury Governance

Obiettivo: inviare 50.000 MVC dal Treasury all'indirizzo del Signer 0.

Il treasury ha 400.000 MVC. Ci vogliono 2 firme su 3 signer.

### Step 1: Decodificare l'indirizzo di destinazione

```bash
cd /root/mevacoin/tools/governance_spend
./gov_spend.py decode "MD5VJcujdh5LhN5tZ3W4c25afTsvWKZh3NVGDtSD1N7iYHYW96nHbFiAjCPmK3KcRVENRFA6NXbhdXXTCyBWBXuSJMUjfLc"
# Output:
#   Spend key: d12e990816a51475c1151ff04a4dc31b62693fbf2bf2d28076cda44e784699bb
#   View key:  0aca48dbce5a0d3a2835b0879574a84fedff620f76705cb67f4b1f8f229e3999
```

### Step 2: Creare il tx_extra con firme zero

```bash
./gov_spend.py treasury-zero \
  50000000000000 \
  "MD5VJcujdh5LhN5tZ3W4c25afTsvWKZh3NVGDtSD1N7iYHYW96nHbFiAjCPmK3KcRVENRFA6NXbhdXXTCyBWBXuSJMUjfLc" \
  "d12e990816a51475c1151ff04a4dc31b62693fbf2bf2d28076cda44e784699bb" \
  "dfeb3f3ce8c3efe6c28b5670d365d1529ec6032dd2aa3cbd53a8595be9b7312a"
```

**Spiegazione parametri:**
- `50000000000000` = 50.000 MVC (moltiplicare MVC * 10^9 per atomic units)
- Indirizzo destinazione MD5VJc...
- `d12e99...` = chiave pubblica signer 0 (quella che firmera')
- `dfeb3f...` = chiave pubblica signer 1 (seconda firma)

**Output:**
```
b0... (hex blob lungo ~200 byte)
```

Questo blob ha le firme impostate a ZERO (64 byte di zeri per ogni signer).

### Step 3: Costruire la transazione

Il blob hex va copiato nel campo `extra` della transazione. Ci sono 2 modi:

**Opzione A: con mevacoin-wallet-rpc**
```bash
# Creare una transazione normale verso l'indirizzo treasury primo
# Poi modificare manualmente il campo extra della transazione firmata
# Rimpiazzando il tx_extra con il blob governance
# (richiede accesso raw al transaction blob)

# Al momento non c'e' supporto diretto nel wallet RPC per governance.
```

**Opzione B: costruire la raw transaction manualmente**
Usando i tool RPC `create_transaction` o costruendo il blob binario direttamente. Il campo `extra` della transaction_prefix deve contenere il blob hex prodotto sopra.

### Step 4: Calcolare tx_prefix_hash

Una volta costruita la transazione (non firmata), calcolare l'hash del prefix:

```cpp
crypto::hash h = get_transaction_prefix_hash(tx);
```

Oppure, via RPC, il wallet puo' esportare la transazione non firmata e calcolare l'hash con un piccolo tool.

NOTA: Questo hash SARA' DIVERSO dal nostro test perche' la transazione reale ha input, output, ecc.

**Per il test**, usiamo un hash fittizio:
```
HASH=ab000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e
```

### Step 5: Firmare l'hash con 2 signer

```bash
cd /root/mevacoin/tools/governance_spend

HASH="ab000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e"

./gov_spend.py sign "$HASH" \
  "6c90f4fc20bde8dbe0eb2a4d8c9948174d5f0cdb85cccaf536752de2285c4402" \
  "a6c9c2103d56ff0f4971f2c0705310cfc051d7fbd765d640c7ff618b1244d60c"
```

**Spiegazione:**
- `$HASH` = il tx_prefix_hash della transazione
- Prima chiave privata = signer 0
- Seconda chiave privata = signer 1

**Output** (JSON con le firme):
```json
[
  {
    "signer_key": "d12e990816a51475c1151ff04a4dc31b62693fbf2bf2d28076cda44e784699bb",
    "sig": "9fdc646238b5a42354e061bfa233f047ba0ab2346cc85626755a21975be05901c9d08a8d8cbd34569c46aa228442e3a29a36676120114a968fdade3b4155fc06"
  },
  {
    "signer_key": "dfeb3f3ce8c3efe6c28b5670d365d1529ec6032dd2aa3cbd53a8595be9b7312a",
    "sig": "bafac803de30c97b36a9b4f73ee7c3d80d6d8fa5aca3be4ff2bde7926105b20b8b5531cf6731b05d4befc67659d6a93b83140405007d6d56f58521327a53bb0e"
  }
]
```

### Step 6: Costruire il blob finale con firme reali

```bash
cd /root/mevacoin/tools/governance_spend

SIGS='[
  {
    "signer_key": "d12e990816a51475c1151ff04a4dc31b62693fbf2bf2d28076cda44e784699bb",
    "sig": "9fdc646238b5a42354e061bfa233f047ba0ab2346cc85626755a21975be05901c9d08a8d8cbd34569c46aa228442e3a29a36676120114a968fdade3b4155fc06"
  },
  {
    "signer_key": "dfeb3f3ce8c3efe6c28b5670d365d1529ec6032dd2aa3cbd53a8595be9b7312a",
    "sig": "bafac803de30c97b36a9b4f73ee7c3d80d6d8fa5aca3be4ff2bde7926105b20b8b5531cf6731b05d4befc67659d6a93b83140405007d6d56f58521327a53bb0e"
  }
]'

./gov_spend.py treasury-build \
  50000000000000 \
  "MD5VJcujdh5LhN5tZ3W4c25afTsvWKZh3NVGDtSD1N7iYHYW96nHbFiAjCPmK3KcRVENRFA6NXbhdXXTCyBWBXuSJMUjfLc" \
  "d12e990816a51475c1151ff04a4dc31b62693fbf2bf2d28076cda44e784699bb" \
  "$SIGS"
```

**Output:** hex blob ~200 byte con le firme REALI.

### Step 7: Verificare le firme

```bash
./gov_spend.py verify \
  "b080a0e5b9c29101d12e990816a51475c1151ff04a4dc31b62693fbf2bf2d28076cda44e784699bb0aca48dbce5a0d3a2835b0879574a84fedff620f76705cb67f4b1f8f229e399902d12e990816a51475c1151ff04a4dc31b62693fbf2bf2d28076cda44e784699bb9fdc646238b5a42354e061bfa233f047ba0ab2346cc85626755a21975be05901c9d08a8d8cbd34569c46aa228442e3a29a36676120114a968fdade3b4155fc06dfeb3f3ce8c3efe6c28b5670d365d1529ec6032dd2aa3cbd53a8595be9b7312abafac803de30c97b36a9b4f73ee7c3d80d6d8fa5aca3be4ff2bde7926105b20b8b5531cf6731b05d4befc67659d6a93b83140405007d6d56f58521327a53bb0e" \
  "$HASH"

# Output:
#   Tag:      0xB0
#   Amount:   50000000000000
#   To:       spend=d12e990816a51475...
#             view=0aca48dbce5a0d3a...
#   Signers:  2
#     #0: key=d12e990816a51475... VALID
#     #1: key=dfeb3f3ce8c3efe6... VALID
```

Entrambe le firme sono VALIDE.

### Step 8: Inserire il blob nella transazione

Rimpiazzare il campo `extra` della transazione con il blob finale (firme reali). Poi firmare la transazione con la chiave del wallet mittente e trasmettere.

---

## ESEMPIO 2: Transazione Network Fund

Obiettivo: inviare 5.000 MVC dal Network Fund al Signer 0.

Il Network Fund ha 400.000 MVC. NON servono firme. Rate limit: max 10.000 MVC in 30 giorni.

### Comando singolo

```bash
cd /root/mevacoin/tools/governance_spend

./gov_spend.py network \
  5000000000000 \
  "MD5VJcujdh5LhN5tZ3W4c25afTsvWKZh3NVGDtSD1N7iYHYW96nHbFiAjCPmK3KcRVENRFA6NXbhdXXTCyBWBXuSJMUjfLc"
```

**Spiegazione:**
- `5000000000000` = 5.000 MVC (= 5.000 * 10^9 atomic units)
- Indirizzo di destinazione

**Output:**
```
c080a094a58d1dd12e990816a51475c1151ff04a4dc31b62693fbf2bf2d28076cda44e784699bb0aca48dbce5a0d3a2835b0879574a84fedff620f76705cb67f4b1f8f229e3999
```

Questo hex va copiato nel campo `extra` della transazione.

Il blob si decompone cosi':
- `c0` = tag 0xC0 (Network Fund Transfer)
- `80a094a58d1d` = 5.000 MVC in varint
- `d12e99...` = recipient spend key (32 byte)
- `0aca48...` = recipient view key (32 byte)

### Cosa controlla il nodo quando arriva la transazione

1. L'output speso DEVE essere l'indirizzo deterministico del Network Fund
2. Il tag 0xC0 DEVE essere presente in tx_extra
3. L'importo NON deve superare il limite mensile (10.000 MVC / 30gg)
4. Il saldo del Network Fund NON deve andare sotto zero

---

## ESEMPIO 3: Spesa Team Lock

Obiettivo: spostare il Team Lock (200.000 MVC) dopo 24 mesi.

Il Team Lock e' l'unico output del premine che HA UNA CHIAVE PRIVATA. Chi ha generato la genesis transaction ha la chiave privata che controlla l'output.

### Quando si puo' spendere

```
TEAM_LOCK_BLOCKS = 518.400 blocchi ≈ 24 mesi (a 120 sec/blocco)
```

Prima di questo altezza, qualsiasi tentativo di spendere viene rifiutato da `check_premine_spend()`.

### Come costruire la transazione

1. Aprire il wallet fondatore con `mevacoin-wallet-rpc`
2. Creare una transazione normale verso l'indirizzo desiderato
3. Il wallet selezionera' automaticamente l'output del team lock come input
4. Firmare la transazione con la chiave privata del wallet
5. Broadcast

Niente tx_extra speciale, niente governance. E' una transazione Monero standard.

### Se si vuole forzare manualmente

```bash
# 1. Trovare l'output del team lock
#    (il nodo lo calcola come compute_premine_output_key(...) usando
#     la tx_secret_key della genesis transaction)

# 2. Costruire una transazione che spende QUEL preciso output
# 3. Firmare con la chiave privata che controlla l'output
#    (la stessa usata per generare la genesis tx)
# 4. Broadcast
```

---

## Riepilogo: differenze tra i 3 metodi

| Aspetto | Team Lock | Treasury | Network Fund |
|---------|-----------|----------|--------------|
| **Chiave privata** | Si (wallet fondatore) | NO (indirizzo deterministico) | NO (indirizzo deterministico) |
| **Firme governance** | No | Si, 2/3 signer | No |
| **tx_extra speciale** | No | Tag 0xB0 + amount + destinazione + firme | Tag 0xC0 + amount + destinazione |
| **Limite** | Bloccato 24 mesi | Fino a 400.000 MVC totali | 10.000 MVC / 30gg |
| **Strumento** | mevacoin-wallet-rpc normale | `gov_spend.py` | `gov_spend.py` |

---

## Test Wallets (solo sviluppo!)

Tutti i file wallet sono in `/root/mevacoin/test_wallets/`:

| File | Descrizione |
|------|-------------|
| `signer_0` / `signer_0.keys` | Wallet signer 0 |
| `signer_1` / `signer_1.keys` | Wallet signer 1 |
| `signer_2` / `signer_2.keys` | Wallet signer 2 |
| `signer_0.json` | JSON per generare il wallet (mevacoin-wallet-rpc --generate-from-json) |
| `signer_1.json` | ... |
| `signer_2.json` | ... |
| `wallet-keys.txt` | Tabella completa con tutte le chiavi |
| `wallet.txt` | Copia di wallet-keys.txt |

### Aprire un wallet con mevacoin-wallet-rpc

```bash
/root/mevacoin/build/Linux/mevacoin/release/bin/mevacoin-wallet-rpc \
  --wallet-file /root/mevacoin/test_wallets/signer_0 \
  --password pass_signer_0 \
  --rpc-bind-port 12345 \
  --daemon-address 127.0.0.1:18081
```

---

## Codice rilevante

| File | Cosa contiene |
|------|---------------|
| `src/cryptonote_core/foundation_vesting.h` | Allocazioni, indirizzi deterministici, chiavi pubbliche signer |
| `src/cryptonote_core/blockchain.cpp:5978` | `check_premine_spend()` - validazione premine |
| `src/cryptonote_core/blockchain.cpp:6087` | `process_premine_actions()` - aggiornamento stato |
| `src/cryptonote_core/governance.cpp` | `verify_governance_signatures()`, `validate_governance_transfer()` |
| `src/cryptonote_core/governance.h` | Struct `governance_state` |
| `src/cryptonote_core/network_fund.h` | Struct `network_fund_state` |
| `src/cryptonote_basic/tx_extra.h` | Tag 0xB0, 0xB1, 0xB2, 0xC0 |
| `tools/governance_spend/gov_crypto.cpp` | Tool crittografico C++ |
| `tools/governance_spend/gov_spend.py` | Script Python orchestrazione |
