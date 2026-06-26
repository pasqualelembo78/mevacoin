# Agent Summary

## Completed

### Circular Dependency Fix
- **`src/cryptonote_core/blockchain.cpp:5983-6036`**: `check_premine_spend()` now zeroes governance sigs in a parsed copy of `tx_extra` before computing `tx_prefix_hash` for verification. Breaks the circular dependency (governance sigs in tx_extra → tx_prefix_hash → need sigs to verify).

### C++ Crypto Tool
- **`tools/governance_spend/gov_crypto.cpp`**: Standalone binary linked against `libcncrypto.a` + `libcommon.a`. Commands: `sign`, `genkey`, `verify`, `pubkey`, `decode` (address → spend+view keys).
- Build command in `tools/governance_spend/README.md`.

### Python Orchestration
- **`tools/governance_spend/gov_spend.py`**: `genkey`, `pubkey`, `decode`, `network`, `treasury-zero`, `treasury-build`, `sign`, `verify`.
- `cmd_treasury_build` no longer takes `<pub0>` arg — now just `<amount> <address> <sigs.json>`. Removed dead zero-sig placeholder code.

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
- Test all premine spend flows (treasury, add-signer, remove-signer)
- Fix `gov_crypto` to use shared derivation from `foundation_vesting.h` (stale `derive-wallet`)
- Write functional/integration tests

## Completed in this session
- `DEFAULT_FEE_ATOMIC_MVC_PER_KB` removed (dead code, dynamic fee system active)
- `gov_spend.py`: `add-signer-zero/build`, `remove-signer-zero/build` for 0xB1/0xB2
- `parse_governance_extra()` handles all 3 governance tags
- `cmd_verify()` shows type-specific fields for all tags
- Proposer key persistence: FROST keypairs saved to `<data_dir>/mevatrust/proposer_keys`, loaded on restart
- `--mevatrust-proposer-key=<hex>` CLI arg for key override
- `set_proposer_keypairs()` now actually called — FROST signing no longer dead code
- `mevatrust_coinbase_validator.cpp` uses `CONSENSUS_PROPOSER_PUBKEYS`
- **FROST P2P coordination (item 7 from mevacoin.md)**: Full 2-round protocol implemented
  - `FrostBroadcaster` class with ballot-box state machine in `src/cryptonote_core/mevatrust/frost_broadcaster.{h,cpp}`
  - P2P message types `NOTIFY_MEVATRUST_FROST_NONCE` (ID=14) and `NOTIFY_MEVATRUST_FROST_SIGN` (ID=15) in `cryptonote_protocol_defs.h`
  - Handlers registered in `cryptonote_protocol_handler` invoke map with `handle_NOTIFY_MEVATRUST_FROST_NONCE/SIGN` implementations in `.inl`
  - Virtual broadcast methods `broadcast_frost_nonce` / `broadcast_frost_sign_request` in `i_cryptonote_protocol`
  - Broadcast function wiring in `core.cpp` Fase 7 (blob decode → handler dispatch)
  - `MevaTrustManager` integration: `m_frost_broadcaster`, `set_frost_broadcast_func()`, apply callback, P2P round start in `trigger_distribution()`
  - `CMakeLists.txt` updated with `frost_broadcaster.cpp`
  - `m_resolved_pool_balance` added for async apply callback
