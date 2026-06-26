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

## sort_tx_extra Fix (New)
- **`src/cryptonote_basic/cryptonote_format_utils.cpp:619-624`**: Added `pick<>` calls for `tx_extra_governance_transfer` (0xB0), `tx_extra_governance_add_signer` (0xB1), `tx_extra_governance_remove_signer` (0xB2), and `tx_extra_network_fund_transfer` (0xC0) in `sort_tx_extra()`. The function rejects any type from the `tx_extra_field` variant that it doesn't have a `pick` handler for — causing `"transaction was not constructed"` when the wallet-rpc `transfer` call includes custom extra. **Root cause of wallet-rpc rejecting custom extra.**

## Verified Network Fund Spend
- **Network fund transfer with 0xC0 extra succeeded** after sort_tx_extra fix. Tx `d61e13d2eb62af2b123a24442976d4ad4631eb9023ed3af5d6fb7acc4527d517` was created and broadcast to the tx pool. The `inject_extra.py` blob-injection approach is no longer needed — the wallet-rpc `extra` parameter works directly now.

## Build Notes
- `gov_crypto` needs recompile after full project rebuild (links to build artifacts)
- `check_premine_spend` fix needs full `make` (it's in blockchain.cpp)
- `cn_fast_hash_cli` needs `make -C tools/governance_spend` if `hash.c` or `keccak.c` changes
- `sort_tx_extra` fix is in `cryptonote_format_utils.cpp` — part of `libcryptonote_basic`, rebuilds with `make`

## Known Security Gap
- `check_premine_spend()` enforces governance rules ONLY if governance/network-fund `tx_extra` is present; it does NOT reject transactions that spend treasury/network outputs WITHOUT the extra. Anyone who computes the deterministic private key (derivable from the public domain string) can drain the treasury/network fund without governance approval. Fix should reject premine output spends lacking the expected extra field.

## Next Steps
- Test treasury governance spend flow (zero-sig → sign → inject)
- Fix the security gap: reject premine output spends that lack the required extra tag
- Add governance add/remove signer support to `gov_spend.py`
- Write functional/integration tests
