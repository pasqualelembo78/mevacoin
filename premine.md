# Guida Spesa Premine — MevaCoin

## Cosa c'e' nel premine

Al genesis sono stati creati 3 output (1M MVC total):

| Fondo          | MVC    | Atomic units         | Indirizzo                                             | Come si spende                          |
|----------------|--------|----------------------|-------------------------------------------------------|-----------------------------------------|
| **Team Lock**  | 200.000 | 200000000000000      | `M5MxXAn9DPJfqU9DZBsuuV8f1kfnmnud6iGxtEKPeTFB9YC57RCXaFViMGf11joaAJ9yoXxF49b2C2mBoxdN8u6j1qxDcK2` (FOUNDATION_ADDRESS, wallet REALE) | Chiave privata del wallet fondatore (fuori repo). |
| **Treasury**   | 400.000 | 400000000000000      | Deterministico da `mevacoin_governance` (NESSUNO ha la chiave) | 2 firme su 3 signer governance in tx_extra. |
| **Network**    | 400.000 | 400000000000000      | Deterministico da `mevacoin_network_fund` (NESSUNO ha la chiave) | Nessuna firma. Rate-limited: 10.000 MVC / 30gg. |

---

## Wallet & Chiavi

### Wallet Signer Governance (3 firmatari)

Servono per firmare transazioni Treasury. Ci vogliono **2 firme su 3** per autorizzare.

|               | Signer 0                                      | Signer 1                                      | Signer 2                                      |
|---------------|-----------------------------------------------|-----------------------------------------------|-----------------------------------------------|
| **Password**  | `pass_signer_0`                               | `pass_signer_1`                               | `pass_signer_2`                               |
| **Indirizzo** | `M6nt4TRn2Rn28qqXQqk2zQ3cAQtd4Fv2ECebLbnKjr4jEUiUjBnTzr8ibVs45h5rtbYdAJqS695kUZTnDTqinbzARkvmbD7` | `M8nh1znTagMgB3X4ykxj9wRdPx4B2BhVtCkPCU1ZEBFG7ZhHvPzvtYuSyX98hbmuhzb4KYgWeUMMthsCYUKVhSus7jExWDc` | `M8XSNr9xaddAmzS3XzQSUUEhQHiNyyvQPeAL2VwnU89eFXtoD2R7VT3EKVppPenFwhUzM9YxaGC695JZi4DkMVKE5tmivR1` |
| **Pub spend**  | `2b4bc2ec2ba0c906c97630e6bc00bd0f94ec15f85287db45a3c169c3ccf38c50` | `5ffc6b4dd5960eea337717fe4524f6933cff642eae114b463c8fe7763898eb27` | `590bedada0f2683a71eed813b121a551e2f0f4226c5b60de2b8a0dd052463956` |
| **Pub view**   | `942b2b108f2145f8add1cb24ed2cf4bd107582678fd919c2136fb810dc9bd3db` | `3de711d40c673e9b4a8f6a69be0259cb9d6b4351708573f451a0f56f2a2afe3b` | `e28d65f10e86e64fa0ea9eb5cc33b4a755733b74f60e3e19b994391ffd1fa12b` |
| **File wallet** | `/root/mvc_mainnet_keys/signer_0.txt` (fuori repo) | `/root/mvc_mainnet_keys/signer_1.txt` (fuori repo) | `/root/mvc_mainnet_keys/signer_2.txt` (fuori repo) |

NOTA: le chiavi PRIVATE dei signer NON sono nel repository. Sono in `/root/mvc_mainnet_keys/signer_{0,1,2}.txt` (chmod 600). Le spend/view key mostrate in precedenti versioni di questo file erano la VECCHIA cerimonia e non sono piu' valide.

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
./gov_spend.py decode "M6nt4TRn2Rn28qqXQqk2zQ3cAQtd4Fv2ECebLbnKjr4jEUiUjBnTzr8ibVs45h5rtbYdAJqS695kUZTnDTqinbzARkvmbD7"
# Output:
#   Spend key: 2b4bc2ec2ba0c906c97630e6bc00bd0f94ec15f85287db45a3c169c3ccf38c50
#   View key:  942b2b108f2145f8add1cb24ed2cf4bd107582678fd919c2136fb810dc9bd3db
```

### Step 2: Creare il tx_extra con firme zero

```bash
./gov_spend.py treasury-zero \
  50000000000000 \
  "M6nt4TRn2Rn28qqXQqk2zQ3cAQtd4Fv2ECebLbnKjr4jEUiUjBnTzr8ibVs45h5rtbYdAJqS695kUZTnDTqinbzARkvmbD7" \
  "2b4bc2ec2ba0c906c97630e6bc00bd0f94ec15f85287db45a3c169c3ccf38c50" \
  "5ffc6b4dd5960eea337717fe4524f6933cff642eae114b463c8fe7763898eb27"
```

**Spiegazione parametri:**
- `50000000000000` = 50.000 MVC (moltiplicare MVC * 10^9 per atomic units)
- Indirizzo destinazione M6nt4T...
- `2b4bc2ec...` = chiave pubblica signer 0 (quella che firmera')
- `5ffc6b4d...` = chiave pubblica signer 1 (seconda firma)

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

# Le chiavi private attive NON sono nel repo; leggerle da /root/mvc_mainnet_keys
./gov_spend.py sign "$HASH" \
  "$(grep -oP 'spend_sec: \\K[0-9a-f]{64}' /root/mvc_mainnet_keys/signer_0.txt)" \
  "$(grep -oP 'spend_sec: \\K[0-9a-f]{64}' /root/mvc_mainnet_keys/signer_1.txt)"
```

**Spiegazione:**
- `$HASH` = il tx_prefix_hash della transazione
- Prima chiave privata = signer 0 (leggere da `/root/mvc_mainnet_keys/signer_0.txt`)
- Seconda chiave privata = signer 1 (leggere da `/root/mvc_mainnet_keys/signer_1.txt`)

**Output** (JSON con le firme):
```json
[
  {
    "signer_key": "2b4bc2ec2ba0c906c97630e6bc00bd0f94ec15f85287db45a3c169c3ccf38c50",
    "sig": "9fdc646238b5a42354e061bfa233f047ba0ab2346cc85626755a21975be05901c9d08a8d8cbd34569c46aa228442e3a29a36676120114a968fdade3b4155fc06"
  },
  {
    "signer_key": "5ffc6b4dd5960eea337717fe4524f6933cff642eae114b463c8fe7763898eb27",
    "sig": "bafac803de30c97b36a9b4f73ee7c3d80d6d8fa5aca3be4ff2bde7926105b20b8b5531cf6731b05d4befc67659d6a93b83140405007d6d56f58521327a53bb0e"
  }
]
```

### Step 6: Costruire il blob finale con firme reali

```bash
cd /root/mevacoin/tools/governance_spend

SIGS='[
  {
    "signer_key": "2b4bc2ec2ba0c906c97630e6bc00bd0f94ec15f85287db45a3c169c3ccf38c50",
    "sig": "9fdc646238b5a42354e061bfa233f047ba0ab2346cc85626755a21975be05901c9d08a8d8cbd34569c46aa228442e3a29a36676120114a968fdade3b4155fc06"
  },
  {
    "signer_key": "5ffc6b4dd5960eea337717fe4524f6933cff642eae114b463c8fe7763898eb27",
    "sig": "bafac803de30c97b36a9b4f73ee7c3d80d6d8fa5aca3be4ff2bde7926105b20b8b5531cf6731b05d4befc67659d6a93b83140405007d6d56f58521327a53bb0e"
  }
]'

./gov_spend.py treasury-build \
  50000000000000 \
  "M6nt4TRn2Rn28qqXQqk2zQ3cAQtd4Fv2ECebLbnKjr4jEUiUjBnTzr8ibVs45h5rtbYdAJqS695kUZTnDTqinbzARkvmbD7" \
  "2b4bc2ec2ba0c906c97630e6bc00bd0f94ec15f85287db45a3c169c3ccf38c50" \
  "$SIGS"
```

**Output:** hex blob ~200 byte con le firme REALI.

### Step 7: Verificare le firme

```bash
./gov_spend.py verify \
  "b080a0e5b9c291012b4bc2ec2ba0c906c97630e6bc00bd0f94ec15f85287db45a3c169c3ccf38c50942b2b108f2145f8add1cb24ed2cf4bd107582678fd919c2136fb810dc9bd3db022b4bc2ec2ba0c906c97630e6bc00bd0f94ec15f85287db45a3c169c3ccf38c509fdc646238b5a42354e061bfa233f047ba0ab2346cc85626755a21975be05901c9d08a8d8cbd34569c46aa228442e3a29a36676120114a968fdade3b4155fc065ffc6b4dd5960eea337717fe4524f6933cff642eae114b463c8fe7763898eb27bafac803de30c97b36a9b4f73ee7c3d80d6d8fa5aca3be4ff2bde7926105b20b8b5531cf6731b05d4befc67659d6a93b83140405007d6d56f58521327a53bb0e" \
  "$HASH"

# Output:
#   Tag:      0xB0
#   Amount:   50000000000000
#   To:       spend=2b4bc2ec2ba0c906...
#             view=942b2b108f2145f8...
#   Signers:  2
#     #0: key=2b4bc2ec2ba0c906... VALID
#     #1: key=5ffc6b4dd5960eea... VALID
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
  "M6nt4TRn2Rn28qqXQqk2zQ3cAQtd4Fv2ECebLbnKjr4jEUiUjBnTzr8ibVs45h5rtbYdAJqS695kUZTnDTqinbzARkvmbD7"
```

**Spiegazione:**
- `5000000000000` = 5.000 MVC (= 5.000 * 10^9 atomic units)
- Indirizzo di destinazione

**Output:**
```
c080a094a58d1d2b4bc2ec2ba0c906c97630e6bc00bd0f94ec15f85287db45a3c169c3ccf38c50942b2b108f2145f8add1cb24ed2cf4bd107582678fd919c2136fb810dc9bd3db
```

Questo hex va copiato nel campo `extra` della transazione.

Il blob si decompone cosi':
- `c0` = tag 0xC0 (Network Fund Transfer)
- `80a094a58d1d` = 5.000 MVC in varint
- `2b4bc2ec...` = recipient spend key (32 byte)
- `942b2b10...` = recipient view key (32 byte)

### Cosa controlla il nodo quando arriva la transazione

1. L'output speso DEVE essere l'indirizzo deterministico del Network Fund
2. Il tag 0xC0 DEVE essere presente in tx_extra
3. L'importo NON deve superare il limite mensile (10.000 MVC / 30gg)
4. Il saldo del Network Fund NON deve andare sotto zero

---

## ESEMPIO 3: Spesa Team Lock

Obiettivo: spostare il Team Lock (200.000 MVC) dopo 24 mesi.

Il Team Lock e' l'unico output del premine che HA UNA CHIAVE PRIVATA: va all'indirizzo `FOUNDATION_ADDRESS` (wallet REALE, non deterministico). La chiave privata del wallet fondatore e' tenuta fuori dal repository.

### Quando si puo' spendere

ATTENZIONE: il blocco "24 mesi" (TEAM_LOCK_BLOCKS = 518.400) NON e' applicato dal consenso. L'output team viene trattato come un normale output coinbase del genesis: e' spendibile dopo l'unlock window standard (CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW = 60 blocchi). Il verso vesting a 24 mesi e' una responsabilita' della fondazione, non un vincolo di rete.

### Come costruire la transazione

1. Aprire il wallet fondatore con `mevacoin-wallet-rpc`
2. Creare una transazione normale verso l'indirizzo desiderato
3. Il wallet selezionera' automaticamente l'output del team lock come input
4. Firmare la transazione con la chiave privata del wallet
5. Broadcast

Niente tx_extra speciale, niente governance. E' una transazione Monero standard. NOTA: `check_premine_spend()` NON blocca la spesa dell'output team (controlla solo treasury 0xB0/0xC0); la sicurezza dell'output team dipende esclusivamente dalla custodia della chiave privata del wallet fondatore.

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
| **Chiave privata** | Si (wallet fondatore, fuori repo) | NO (indirizzo deterministico) | NO (indirizzo deterministico) |
| **Firme governance** | No | Si, 2/3 signer | No |
| **tx_extra speciale** | No | Tag 0xB0 + amount + destinazione + firme | Tag 0xC0 + amount + destinazione |
| **Limite** | Nessun lock consensuale (vesting 24 mesi = responsabilita' fondazione) | Fino a 400.000 MVC totali | 10.000 MVC / 30gg |
| **Strumento** | mevacoin-wallet-rpc normale | `gov_spend.py` | `gov_spend.py` |

---

## Test Wallets (solo sviluppo!)

ATTENZIONE: le chiavi dei signer governance e del fondatore NON sono nel repository.
Le uniche copie valide (cerimonia attuale) stanno in `/root/mvc_mainnet_keys/`:

| File | Descrizione |
|------|-------------|
| `signer_0.txt` | Wallet signer 0 (fuori repo, chmod 600) |
| `signer_1.txt` | Wallet signer 1 (fuori repo, chmod 600) |
| `signer_2.txt` | Wallet signer 2 (fuori repo, chmod 600) |

I file `wallet/signer_*.keys` e `wallet/*.json` che un tempo esistevano nel repo sono stati
rimossi dallo storico git (contenevano chiavi della vecchia cerimonia, non piu' valide).

### Aprire un wallet signer con mevacoin-wallet-rpc

I wallet signer vanno creati dai valori in `/root/mvc_mainnet_keys/signer_{0,1,2}.txt`
tramite `--generate-from-json` o `--generate-from-view-key`, poi:

```bash
/root/mevacoin/build/Linux/mevacoin/release/bin/mevacoin-wallet-rpc \
  --wallet-file /root/mvc_mainnet_keys/signer_0 \
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
