# MevaCoin Governance Spend Tools

Tools for constructing governance treasury and network fund spend transactions.

## Components

### `gov_crypto` — C++ crypto helper
Compiled binary with libcncrypto + libcommon for Ed25519 operations.

Commands: `sign`, `genkey`, `verify`, `pubkey`, `decode`

Build (from repo root):
```
g++ -std=c++17 -o tools/governance_spend/gov_crypto \
  tools/governance_spend/gov_crypto.cpp \
  -I src \
  -I build/Linux/mevacoin/release/generated \
  -I contrib/epee/include \
  -I external/easylogging++ \
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

### `gov_spend.py` — Python orchestration script
Wraps `gov_crypto` for higher-level operations. See `--help`.

## Governance Treasury Spend Flow

### Step 1: Build placeholder tx_extra with zero signatures
```
./gov_spend.py treasury-zero <amount> <dest_addr> <signer0_pub> [signer1_pub ...]
```
Outputs hex blob with `TX_EXTRA_TAG_GOVERNANCE_TRANSFER` (0xB0) with zeroed sigs.

### Step 2: Embed in transaction, get tx_prefix_hash
- Inject the hex blob into the transaction's `extra` field
- Construct the full unsigned transaction
- Compute `tx_prefix_hash` (keccak-256 of serialized `transaction_prefix`)

### Step 3: Sign with governance keys
```
./gov_spend.py sign <tx_prefix_hash_hex> <signer0_priv> [signer1_priv ...]
```
Outputs JSON array of `{signer_key, sig}`.

### Step 4: Build final tx_extra
```
./gov_spend.py treasury-build <amount> <dest_addr> <signer_pub> '<sigs_json>'
```
Outputs hex blob with real signatures.

### Step 5: Replace, broadcast
Replace the zero-sig blob in the transaction with the final blob, sign the tx with the sender's key, and broadcast via `mevacoind`.

## Network Fund Spend Flow

No governance signatures required (rate-limited to 10k MVC/30d).

```
./gov_spend.py network <amount> <dest_addr>
```

## Circular Dependency

Governance signatures are stored in `tx_extra`, which is part of `transaction_prefix`. The `tx_prefix_hash` (over the prefix) would normally include the signatures, making it impossible to sign.

**Fix**: `check_premine_spend()` in `blockchain.cpp` re-serializes `tx_extra` with zeroed governance sigs before computing `tx_prefix_hash` for verification. The signer does the same: builds the tx with zero sigs, hashes, signs, then replaces sigs.

## Test Wallets

Located in `test_wallets/`:
- `signer_0`: `pass_signer_0`
- `signer_1`: `pass_signer_1`
- `signer_2`: `pass_signer_2`

See `test_wallets/wallet-keys.txt` for all keys.
