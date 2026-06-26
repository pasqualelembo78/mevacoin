# Agent Summary

## Completed

### Circular Dependency Fix
- **`src/cryptonote_core/blockchain.cpp:5983-6036`**: `check_premine_spend()` now zeroes governance sigs in a parsed copy of `tx_extra` before computing `tx_prefix_hash` for verification. Breaks the circular dependency (governance sigs in tx_extra → tx_prefix_hash → need sigs to verify).

### C++ Crypto Tool
- **`tools/governance_spend/gov_crypto.cpp`**: Standalone binary linked against `libcncrypto.a` + `libcommon.a`. Commands: `sign`, `genkey`, `verify`, `pubkey`, `decode` (address → spend+view keys).
- Build command in `tools/governance_spend/README.md`.

### Python Orchestration
- **`tools/governance_spend/gov_spend.py`**: `genkey`, `pubkey`, `decode`, `network`, `treasury-zero`, `treasury-build`, `sign`, `verify`.

## Governance Spend Workflow
1. `treasury-zero` → blob with zero sigs (for tx construction)
2. Embed blob in tx, compute `tx_prefix_hash` (using blockchain's `get_transaction_prefix_hash`)
3. `sign` with signer private keys → JSON sigs
4. `treasury-build` → blob with real sigs
5. Replace in tx, broadcast

### Network Fund Spend
No sigs needed. `gov_spend.py network <amount> <addr>` → tx_extra blob.

## sort_tx_extra Fix (New)
- **`src/cryptonote_basic/cryptonote_format_utils.cpp:619-624`**: Added `pick<>` calls for `tx_extra_governance_transfer` (0xB0), `tx_extra_governance_add_signer` (0xB1), `tx_extra_governance_remove_signer` (0xB2), and `tx_extra_network_fund_transfer` (0xC0) in `sort_tx_extra()`. **This was the root cause of `"transaction was not constructed"` when passing custom extra to wallet-rpc `transfer`.**

## Verified Network Fund Spend
- **Network fund transfer with 0xC0 extra succeeded** after sort_tx_extra fix. Tx `d61e13d2eb62af2b123a24442976d4ad4631eb9023ed3af5d6fb7acc4527d517`.

## Build Notes
- `gov_crypto` needs recompile after full project rebuild (links to build artifacts)
- `check_premine_spend` fix needs full `make` (it's in blockchain.cpp)
- `sort_tx_extra` fix is in `cryptonote_format_utils.cpp` — part of `libcryptonote_basic`, rebuilds with `make`

## Next Steps
- Test treasury governance spend flow (zero-sig → sign → inject) — extra injection approach no longer needed, wallet-rpc `extra` param works
- Fix security gap: reject premine output spends that lack the required extra tag
- Add governance add/remove signer support to gov_spend.py
- Write functional/integration tests
