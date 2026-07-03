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
- Build and test network fund spend via wallet-rpc `transfer` (e.g., 10 MVC to `M8viv...`)

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
- **Wallet ring-size fix for unmixable outputs** (`src/wallet/wallet2.cpp`):
  - **`get_outs()` (line 9703–9708)**: When `outs.back().size() < fake_outputs_count+1` (not enough decoys on chain for minimum mixin), check if output is non‑RCT with `num_outs <= fake_outputs_count+1`. If so, mark as `unmixable` instead of `scanty` — suppresses the `not_enough_outs_to_mix` error (`WALLET_RPC_ERROR_CODE_NOT_ENOUGH_OUTS_TO_MIX`, code –19). Consensus (`blockchain.cpp:3517–3585`) already allows smaller rings for unmixable outputs; wallet just needed to not reject them.
  - **`transfer_selected()` (line 9813)**: Changed ring iteration from `fake_outputs_count + 1` to `outs[out_index].size()` so the non‑RCT tx builder uses whatever ring size was gathered.
  - **`transfer_selected_rct()` (lines 10034, 10037)**: Same ring‑size fix for the RCT tx builder. Error guard changed from `outs[out_index].size() < fake_outputs_count` to `outs[out_index].size() == 0` since smaller rings are now valid.
   - **Root cause**: HF version 13 enforces `min_mixin=10` (ring ≥ 11), but only 2 outputs of 400k MVC exist on chain (treasury + network fund at genesis height 0, unlocked at height 60). `get_min_ring_size()` returns 11 → `adjust_mixin()` overrides user‑requested `ring_size=2` → `get_outs()` tries to build ring of 11 but finds only 2 → throws `not_enough_outs_to_mix`. `create_unmixable_sweep_transactions()` (which uses `fake_outs_count=0`) already worked but sends to own address, not a custom destination.

### CLSAG ring-order fix (Jul 1 2026)
- **Root cause**: CLSAG `verRctCLSAGSimple` failed with `final_c != c1` (mu_P/mu_C mismatch) even with the `inSk.mask=identity()` fix. The mu_P/mu_C hash input had identical ring members but in **different order** between signer and verifier.
- **Why**: For unmixable outputs (ring smaller than requested), `get_outs() at `wallet2.cpp:9727` **skipped sorting** `outs[...]` by global index (the sort was inside `if (outs.back().size() >= fake_outputs_count+1)`). But `absolute_output_offsets_to_relative()` at `cryptonote_format_utils.cpp:1510` **always** sorts absolute indices ascending before computing relative offsets for the tx. The verifier rebuilt the ring in sorted order from the sorted key_offsets, but the signer used the unsorted mixRing order → mu_P/mu_C differed → CLSAG round hash chain diverged.
- **Fix** (`wallet2.cpp:9709-9728`): Moved `std::sort(...)` outside the ring-size guard so the ring is **always sorted by global index** regardless of ring size.
- **Verification**: Network fund transfer (non-RCT, 400k MVC, 0xC0 extra, ring_size=2) succeeds. Signer: `mu_P=eeee4a49...`, Verifier: `mu_P=eeee4a49...` (match). Daemon log: `final_c == c1, diff=0`. Tx `2a30a040b012d93895407cb12ecd45ef866a374a1e4bc63993b389a142f2b8f9` accepted to mempool.
