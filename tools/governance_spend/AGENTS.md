# Agent Summary

## Completed

### Circular Dependency Fix
- **`src/cryptonote_core/blockchain.cpp:5983-6036`**: `check_premine_spend()` now zeroes governance sigs in a parsed copy of `tx_extra` before computing `tx_prefix_hash` for verification. Breaks the circular dependency (governance sigs in tx_extra → tx_prefix_hash → need sigs to verify).

### C++ Crypto Tool
- **`tools/governance_spend/gov_crypto.cpp`**: Standalone binary linked against `libcncrypto.a` + `libcommon.a`. Commands: `sign`, `genkey`, `verify`, `pubkey`, `decode` (address → spend+view keys), `derive-wallet` (deterministic wallet derivation from domain string).
- Build command in `tools/governance_spend/README.md`.

### Standalone Keccak-256 Hash CLI
- **`tools/governance_spend/cn_fast_hash_cli`** compiled from `src/crypto/hash.c` + `src/crypto/keccak.c`. Used by `governance_spend_auto.py` to compute `tx_prefix_hash` with zeroed governance sigs (avoids linking `libcryptonote_basic` which pulls in `libdevice` with HID dependency).

### Python Orchestration
- **`tools/governance_spend/gov_spend.py`**: `genkey`, `pubkey`, `decode`, `network`, `treasury-zero`, `treasury-build`, `sign`, `verify`.

### Fully Automated Premine Spend Script
- **`tools/governance_spend/governance_spend_auto.py`**: Single interactive script that connects to `mevacoind` + `mevacoin-wallet-rpc` and handles all three fund types:
  - `team-lock` — simple wallet RPC transfer from founder's wallet
  - `network` — opens network fund wallet, builds 0xC0 extra, broadcasts
  - `treasury` — full multi-step flow: zero-sig extra → create tx with `do_not_relay=True` → compute `tx_prefix_hash` (with zeroed sigs, via binary parsing + `cn_fast_hash_cli`) → sign with selected governance keys → build real-sig extra → inject into blob → `send_raw_transaction`
- Uses binary blob parsing (`find_extra_range` + varint walk) to locate the extra field in the serialized tx prefix and replace governance sigs in-place (same byte length preserves `rv.message` for CLSAG verification).

## Governance Spend Workflow (Automated)
1. User runs `./governance_spend_auto.py`
2. Selects fund type (team-lock / network / treasury)
3. Enters amount + recipient address
4. Script handles everything: wallet creation, extra construction, signing, blob patching, broadcast

## Build Notes
- `gov_crypto` needs recompile after full project rebuild (links to build artifacts)
- `check_premine_spend` fix needs full `make` (it's in blockchain.cpp)
- `cn_fast_hash_cli` needs `make -C tools/governance_spend` if `hash.c` or `keccak.c` changes

## Known Security Gap
- `check_premine_spend()` enforces governance rules ONLY if governance/network-fund `tx_extra` is present; it does NOT reject transactions that spend treasury/network outputs WITHOUT the extra. Anyone who computes the deterministic private key (derivable from the public domain string) can drain the treasury/network fund without governance approval. Fix should reject premine output spends lacking the expected extra field.

## Next Steps
- Test each mode against a running `mevacoind` + `mevacoin-wallet-rpc` network
- Fix the security gap: reject premine output spends that lack the required extra tag
- Add governance add/remove signer support to `gov_spend.py`
- Write functional/integration tests
