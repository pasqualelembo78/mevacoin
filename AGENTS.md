# Agent Summary

## Completed

### Nettype-Correct Genesis Build (Sep 25 2026) — commit 9a1583383
- **Root cause**: `add_genesis_output` in `generate_genesis_block` decoded addresses as `MAINNET` while treasury/network were round-tripped through a **nettype-prefixed address string** → on testnet/stagenet the decode failed, genesis creation aborted. Verified end-to-end that stagenet mainnet→ founder sees 200k team, unlock at height 60, on-chain transfer to a second wallet succeeds.
- **Changes**:
  - `cryptonote_tx_utils.cpp`: lambda now takes `const account_public_address&` directly; `FOUNDATION_ADDRESS` parsed once as `MAINNET` (nettype-independent); treasury/network passed from `get_governance_address(nettype)` / `get_network_fund_address(nettype)` (no string round-trip).
  - `wallet2.cpp:15373` (`wallet2::generate_genesis`): now passes `m_nettype` (was defaulting to `MAINNET`, causing genesis block mismatch on non-mainnet nets: wallet computed a different genesis than the daemon).
  - `blockchain.cpp:5877` (`addr_info_from_str`): parse `FOUNDATION_ADDRESS` as `MAINNET`, not `m_nettype` (previously spurious "Premine output keys mismatch" MERROR on non-mainnet).
- **Mainnet safety verified**: genesis hash unchanged = `33772d0925cf4853e0a421b376ad34608ea040fb68a24682bca75ceaad6807b7` (matches checkpoint).
- **Stagenet test caveats**: start daemon with `--fixed-difficulty 1` for fast mining; `wallet2 load_json` requires JSON field `"version":1`; unused `mevacoin-wallet-rpc` must be built from `tools/governance_spend` keys (spend 3b50617a... / view 28c5124b...).

### premine.md Updated to Active Signer Keys (commit 30efa14e8)
- Replaced STALE ceremony keys (priv 6c90f4fc/a6c9c210/90137ff7, pub d12e9908/dfeb3f3c/0e88576a, addrs MD5VJc/MDdsyR/M5hfHu) with ACTIVE (foundation_vesting.h): spend pubs 2b4bc2ec/5ffc6b4d/590bedad, view pubs 942b2b10/3de711d4/e28d65f1, addrs M6nt4T/M8nh1z/M8XSNr. Private keys NOT in the file; they live in `/root/mvc_mainnet_keys/signer_{0,1,2}.txt`.
- Corrected false claims: team is NOT derived from `mevacoin_team_lock` (it goes to real wallet `FOUNDATION_ADDRESS` M5MxXAn9...); team NOT locked 24mo by consensus (`TEAM_LOCK_BLOCKS` unused; real unlock = height 60); network fund requires NO signatures; wallet paths point to `/root/mvc_mainnet_keys`.

## Legacy (below) remains historical record.

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

## Verification bugs fixed (Sep 23 2026)
- **BUG 1 — restore bug in `tx_verification_utils.cpp`**: `exp (line 103) saved_extra = tx.extra;` happened AFTER `zero_governance_sigs_in_extra(tx.extra)` mutated it, so the "restore original extra" restored the ZEROED extra → `check_premine_spend` (blockchain.cpp:3720) always saw zeroed sigs → 0xB0/0xB1/0xB2 txs always rejected via the RCT path (only passed on verID-cache hit at blockchain.cpp:3684 — nondeterministic). Fixed in both `expand_tx_and_ver_rct_non_sem` and `expand_tx_and_ver_full_rct_non_sem`: save original extra BEFORE zeroing, clear saved copy if no governance fields found.
- **BUG 2 — v1/legacy path never zeroed governance sigs**: `tx_ver_legacy_ring_sigs` (tx_verification_utils.cpp:241) hashed the REAL extra while the non-RCT wallet signs over the zero-sig prefix hash → premine spends (v1 txs, unmixable outputs) always failed Schnorr ring check. Fixed: zero governance sigs for hash computation and restore the original extra, mirroring the RCT paths.
- **BUG 3 — `tools/auto.py` 4-arg `treasury-build`**: call passed `actual_signer_pubs[0]` as a 4th arg but `cmd_treasury_build` (gov_spend.py:314) takes only `amount address sigs.json` → TypeError at step 5/7. Removed the bogus arg.
- Verified: `ver_input_proofs_rings` takes `transaction&` (non-const) → the restore propagates to `check_premine_spend`, and verID at blockchain.cpp:3711 stays stable over the real extra.
- **Correction**: `gov_crypto.cpp derive-wallet` is NOT stale — its formula matches `foundation_vesting.h` (cn_fast_hash(domain+nettype) → hash_to_scalar → secret_key_to_public_key, spend==view). The old AGENTS note "fix stale derive-wallet" was wrong; removed from Next Steps.
- **Hazards resolved Sep 25 2026**:
  - `premine-output mismatch` was only MERROR-logged (`blockchain.cpp:5892-5897`) — now **FATAL** (init_premine_state returns false; node refuses to start) so a node with a forged/corrupted genesis (divergent team/treasury/network keys) cannot silently run. Fix in commit `a59f1900d` base.
  - nettype-correct genesis build fixed for testnet/stagenet (commit `9a1583383`) — no more MAINNET hardcode in genesis creation.
  - Remaining hazards: `tools/auto.py` hardcodes nettype=0 in `derive_wallet` (broken for --testnet — a workflow-tool issue only), and the founder spend key remains committed in `tools/auto.py` (see premine.md; user decision to defer rotation).
- **NOT a hazard**: `foundation_vesting.h:67-69` signers are the **public** active keys (`2b4bc2ec.../5ffc6b4d.../590bedad...`), verified against `/root/mvc_mainnet_keys/signer_{0,1,2}.txt`. The corresponding private keys are NOT in the repo.
