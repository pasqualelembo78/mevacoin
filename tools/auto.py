#!/usr/bin/env python3
"""
governance_spend_auto.py — Fully automated MevaCoin premine spend tool.

Usage:
  ./governance_spend_auto.py

Connects to running mevacoind (http://127.0.0.1:18081) and
auto-starts mevacoin-wallet-rpc (http://127.0.0.1:18083) if needed.

Fund types:
  team-lock  — Spend from Team Lock (200k MVC, 24mo lock).
               Uses the founder's wallet loaded in wallet-rpc.
  network    — Spend from Network Fund (400k MVC, rate-limited 10k/30d).
               Automatically creates wallet for network fund.
  treasury   — Spend from Treasury (400k MVC, 2/3 governance signatures).
               Automates zero-sig → sign → inject flow.
"""

import sys
import os
import json
import subprocess
import signal
import atexit
import time

import requests

# ── Configuration ─────────────────────────────────────────────────────────
DAEMON_URL = os.environ.get("MEVACOIND_URL", "http://127.0.0.1:18081")
WALLET_URL = os.environ.get("WALLET_RPC_URL", "http://127.0.0.1:18083")
GOV_CRYPTO = os.environ.get("GOV_CRYPTO_BIN", os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "gov_crypto"
))
CN_HASH_CLI = os.environ.get("CN_HASH_CLI", os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "cn_fast_hash_cli"
))
GOV_SPEND = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gov_spend.py")
WALLET_RPC_BIN = os.environ.get("WALLET_RPC_BIN", os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "build", "Linux", "mevacoin", "release", "bin", "mevacoin-wallet-rpc"
))
WALLET_RPC_DIR = os.environ.get("WALLET_RPC_DIR",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "wallet_rpc_data"))

COIN = 10**12  # 1 MVC = 10^12 atomic units (matches C++ COIN)

# Minimum ring size — must match wallet2.cpp get_min_ring_size()
# Testnet (only 2 outputs per amount): 2
# Mainnet: 11 (standard Monero minimum)
MIN_RING_SIZE = 2  # TODO: change to 11 for mainnet

# Known deterministic address domains
DOMAIN_TREASURY = "mevacoin_governance"
DOMAIN_NETWORK = "mevacoin_network_fund"

# Founder wallet credentials (Team Lock)
FOUNDER_ADDRESS = "M5MxXAn9DPJfqU9DZBsuuV8f1kfnmnud6iGxtEKPeTFB9YC57RCXaFViMGf11joaAJ9yoXxF49b2C2mBoxdN8u6j1qxDcK2"
FOUNDER_SPENDKEY = "3b50617a4a09555056872d35dd0ce5ee800942eefb5f4987c62ea6d4c2661b01"
FOUNDER_VIEWKEY = "28c5124b5c316f986e4c6ed35d35059ceeddfa44fd14416367d60e30d5902407"
FOUNDER_PASSWORD = "pass_new_founder"

# ── RPC helpers ───────────────────────────────────────────────────────────

def daemon_rpc(method, params=None):
    """JSON-RPC calls: /json_rpc"""
    url = DAEMON_URL + "/json_rpc"
    payload = {"jsonrpc": "2.0", "id": "0", "method": method}
    if params is not None:
        payload["params"] = params
    resp = requests.post(url, json=payload, timeout=30)
    data = resp.json()
    if "error" in data:
        raise RuntimeError(f"Daemon RPC error: {data['error']}")
    return data.get("result")

def daemon_rest(path, payload=None):
    """REST-style calls: GET /path or POST /path with JSON body."""
    url = DAEMON_URL + path
    if payload is not None:
        resp = requests.post(url, json=payload, timeout=30)
    else:
        resp = requests.get(url, timeout=30)
    return resp.json()

def wallet_rpc(method, params=None):
    url = WALLET_URL + "/json_rpc"
    payload = {"jsonrpc": "2.0", "id": "0", "method": method}
    if params is not None:
        payload["params"] = params
    resp = requests.post(url, json=payload, timeout=60)
    data = resp.json()
    if "error" in data:
        raise RuntimeError(f"Wallet RPC error: {data['error']}")
    return data.get("result")

# ── Wallet RPC process management ────────────────────────────────────────

_wallet_rpc_proc = None

def start_wallet_rpc():
    """Launch mevacoin-wallet-rpc as a subprocess if not already running."""
    global _wallet_rpc_proc
    if _wallet_rpc_proc is not None:
        return

    # Parse port from WALLET_URL
    port = WALLET_URL.rsplit(":", 1)[-1]
    host = WALLET_URL.replace("http://", "").rsplit(":", 1)[0]

    # Parse daemon address from DAEMON_URL
    daemon_addr = DAEMON_URL.replace("http://", "")

    os.makedirs(WALLET_RPC_DIR, exist_ok=True)

    print(f"  Starting mevacoin-wallet-rpc on {host}:{port}...")
    _wallet_rpc_proc = subprocess.Popen(
        [
            WALLET_RPC_BIN,
            "--rpc-bind-ip", host,
            "--rpc-bind-port", port,
            "--wallet-dir", WALLET_RPC_DIR,
            "--daemon-address", daemon_addr,
            "--trusted-daemon",
            "--disable-rpc-login",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    # Wait for it to be ready
    for i in range(30):
        try:
            wallet_rpc("get_version")
            print(f"  ✓ Wallet RPC ready (pid={_wallet_rpc_proc.pid})")
            return
        except Exception:
            time.sleep(0.5)

    raise RuntimeError("Wallet RPC did not become ready within 15 seconds")

def stop_wallet_rpc():
    """Terminate the wallet RPC subprocess."""
    global _wallet_rpc_proc
    if _wallet_rpc_proc is None:
        return
    print("  Stopping wallet RPC...")
    _wallet_rpc_proc.terminate()
    try:
        _wallet_rpc_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        _wallet_rpc_proc.kill()
        _wallet_rpc_proc.wait()
    _wallet_rpc_proc = None

# ── Crypto helpers ────────────────────────────────────────────────────────

def gov_run(*args):
    result = subprocess.run(
        [GOV_CRYPTO] + list(args), capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"gov_crypto failed: {result.stderr}")
    return result.stdout

def derive_wallet(domain, nettype=0):
    out = gov_run("derive-wallet", domain, str(nettype))
    lines = out.strip().split("\n")
    info = {}
    for line in lines:
        k, v = line.split(": ", 1)
        info[k] = v.strip()
    return info

def cn_fast_hash(data_hex):
    result = subprocess.run(
        [CN_HASH_CLI, data_hex], capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"cn_fast_hash failed: {result.stderr}")
    return result.stdout.strip()

# ── Tx blob parsing ──────────────────────────────────────────────────────

def _read_varint(data, offset):
    val = 0
    shift = 0
    while offset < len(data):
        b = data[offset]
        offset += 1
        val |= (b & 0x7F) << shift
        shift += 7
        if not (b & 0x80):
            return val, offset
    raise ValueError("Truncated varint")

def find_extra_range(tx_blob_bytes):
    """Find the byte range [start, end) of the extra field within
    the serialized transaction_prefix, and return (start_of_extra_data, end_of_extra_data)
    where start_of_extra_data points to the first byte of the extra blob
    (the varint size), and end_of_extra_data points past the last byte.
    """
    offset = 0
    # version (varint)
    _, offset = _read_varint(tx_blob_bytes, offset)
    # unlock_time (varint)
    _, offset = _read_varint(tx_blob_bytes, offset)
    # vin (list of txin_to_key)
    vin_count, offset = _read_varint(tx_blob_bytes, offset)
    for _ in range(vin_count):
        # Each txin_to_key:
        amount, offset = _read_varint(tx_blob_bytes, offset)
        offset += 32  # key_image (32 bytes)
        key_offsets_count, offset = _read_varint(tx_blob_bytes, offset)
        for _ in range(key_offsets_count):
            _, offset = _read_varint(tx_blob_bytes, offset)
    # vout (list of tx_out)
    vout_count, offset = _read_varint(tx_blob_bytes, offset)
    for _ in range(vout_count):
        amount, offset = _read_varint(tx_blob_bytes, offset)
        # tx_out target type
        # type: txout_to_key = 2, txout_to_tagged_key = 3
        target_type = tx_blob_bytes[offset]
        offset += 1
        if target_type == 0x02:
            offset += 32  # key
        elif target_type == 0x03:
            offset += 32  # key
            offset += 2   # tag (uint16)
        else:
            raise ValueError(f"Unknown txout target type: 0x{target_type:02x}")
    # extra (blob: varint size + bytes)
    extra_start = offset
    extra_size, offset = _read_varint(tx_blob_bytes, offset)
    extra_data_end = offset + extra_size
    return (extra_start, extra_data_end)

def compute_tx_prefix_hash_with_zeroed_sigs(tx_blob_hex):
    """Parse the tx blob, zero governance sigs in extra, compute cn_fast_hash
    of the modified prefix, and return the hash hex."""
    blob = bytes.fromhex(tx_blob_hex)
    extra_start, extra_data_end = find_extra_range(blob)

    # Extract the extra blob
    extra_blob_raw = blob[extra_start:extra_data_end]

    # Parse the extra: varint(size) + data
    extra_size, extra_data_offset = _read_varint(extra_blob_raw, 0)
    extra_data = extra_blob_raw[extra_data_offset:]

    # Find governance tag (0xB0) within extra data
    # The extra may contain multiple sub-fields
    # We need to modify sigs within governance transfer (0xB0) tag
    # Governance transfer: tag(1) + amount(varint) + spend(32) + view(32)
    #   + sig_count(varint) + for each: signer_key(32) + sig(64)
    mod_extra = bytearray(extra_data)

    off = 0
    while off < len(mod_extra):
        tag = mod_extra[off]
        if tag == 0x01:  # TX_EXTRA_TAG_PUBKEY — tag(1) + key(32)
            off += 1 + 32
        elif tag == 0x02:  # TX_EXTRA_NONCE
            off += 1
            _, nonce_len = _read_varint(mod_extra, off)
            off += nonce_len
        elif tag == 0x03:  # TX_EXTRA_ADDITIONAL_PUBKEYS
            off += 1
            count, off = _read_varint(mod_extra, off)
            off += count * 32
        elif tag == 0x04:  # TX_EXTRA_PADDING
            off += 1
            pad_len, off = _read_varint(mod_extra, off)
            off += pad_len
        elif tag == 0xB0:  # governance_transfer — zero the sigs
            off += 1
            _, off = _read_varint(mod_extra, off)  # amount
            off += 64  # spend + view keys
            sig_count, off = _read_varint(mod_extra, off)
            for _ in range(sig_count):
                off += 32  # signer_key (keep intact)
                for j in range(64):       # zero out the 64-byte sig
                    mod_extra[off + j] = 0
                off += 64
        elif tag == 0xC0:  # network_fund_transfer — no sigs to zero
            off += 1
            _, off = _read_varint(mod_extra, off)  # amount
            off += 64  # spend + view keys
        else:
            off += 1  # unknown tag, skip 1 byte

    # Reconstruct full extra blob with modified data
    # The extra field is stored as varint(size) + data
    # Keep the original size prefix same (length unchanged)
    prefix_bytes = bytearray(blob[:extra_start])
    prefix_bytes.extend(extra_blob_raw[:extra_data_offset])  # size prefix
    prefix_bytes.extend(mod_extra)

    # Hash the prefix
    prefix_hex = prefix_bytes.hex()
    return cn_fast_hash(prefix_hex)

# ── GUI / Interaction ─────────────────────────────────────────────────────

def ask(prompt, default=None):
    if default:
        val = input(f"{prompt} [{default}]: ").strip()
        return val if val else default
    return input(f"{prompt}: ").strip()

def ask_int(prompt, default=None):
    while True:
        val = ask(prompt, default)
        try:
            return int(val)
        except ValueError:
            print("  Invalid number, try again.")

def ask_yesno(prompt, default="y"):
    val = ask(prompt + " (y/n)", default).lower()
    return val.startswith("y")

def select_option(prompt, options):
    print(prompt)
    for i, (key, desc) in enumerate(options):
        print(f"  {i+1}. {key} — {desc}")
    while True:
        choice = input(f"Choose (1-{len(options)}): ").strip()
        try:
            idx = int(choice) - 1
            if 0 <= idx < len(options):
                return options[idx][0]
        except ValueError:
            pass
        print("  Invalid choice.")

# ── Wallet management ────────────────────────────────────────────────────

def ensure_wallet(filename, address, spendkey, viewkey, password="govspend"):
    """Create or switch to a wallet with given keys via wallet RPC.
    Close any currently open wallet first."""
    try:
        wallet_rpc("close_wallet")
    except RuntimeError:
        pass

    try:
        result = wallet_rpc("generate_from_keys", {
            "filename": filename,
            "address": address,
            "spendkey": spendkey,
            "viewkey": viewkey,
            "password": password,
            "restore_height": 0,
            "autosave_current": False,
            "language": "English"
        })
        print(f"  Wallet '{filename}' created: {result['address']}")
        return result["address"]
    except RuntimeError as e:
        err = str(e).lower()
        if "already exists" in err:
            result = wallet_rpc("open_wallet", {
                "filename": filename,
                "password": password
            })
            print(f"  Wallet '{filename}' opened.")
            return address
        raise

def refresh_wallet():
    """Rescan blockchain from scratch to ensure correct balance and spent state."""
    result = wallet_rpc("rescan_blockchain")
    print("  Wallet rescanned from height 0.")

# ── Fund type operations ─────────────────────────────────────────────────

def do_team_lock(amount, dest_addr):
    """Spend from Team Lock using the founder's wallet (auto-opened)."""
    print(f"\n{'='*60}")
    print(f"TEAM LOCK SPEND")
    print(f"  Amount:     {amount / COIN:.4f} MVC ({amount} atomic)")
    print(f"  Recipient:  {dest_addr}")
    print(f"{'='*60}")

    # Auto-create/open the founder wallet
    ensure_wallet("founder", FOUNDER_ADDRESS, FOUNDER_SPENDKEY, FOUNDER_VIEWKEY, FOUNDER_PASSWORD)
    refresh_wallet()

    result = wallet_rpc("transfer", {
        "destinations": [{"amount": amount, "address": dest_addr}],
        "priority": 0,
        "ring_size": MIN_RING_SIZE,
        "get_tx_key": True,
        "get_tx_hex": True,
        "do_not_relay": False,
    })
    print(f"\n✓ Transaction broadcast!")
    print(f"  Tx hash: {result['tx_hash']}")
    return result

def do_network_fund(amount, dest_addr):
    """Spend from Network Fund. Creates/opens the network fund wallet and
    builds the transaction with 0xC0 tag extra.  Uses wallet-rpc `extra`
    parameter directly (sort_tx_extra fix in cryptonote_format_utils.cpp
    allows the 0xC0 tag)."""
    print(f"\n{'='*60}")
    print(f"NETWORK FUND SPEND")
    print(f"  Amount:     {amount / COIN:.4f} MVC ({amount} atomic)")
    print(f"  Recipient:  {dest_addr}")
    print(f"{'='*60}")

    # Derive network fund keys
    wallet_info = derive_wallet(DOMAIN_NETWORK, 0)
    print(f"  Network fund address: {wallet_info['address']}")

    # Ensure wallet exists in wallet RPC
    ensure_wallet(
        "network_fund",
        wallet_info["address"],
        wallet_info["spend_sec"],
        wallet_info["view_sec"]
    )

    # Refresh to scan for outputs
    refresh_wallet()

    # Build tx_extra via gov_spend.py
    result = subprocess.run(
        [GOV_SPEND, "network", str(amount), dest_addr],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"gov_spend.py failed: {result.stderr}")
    extra_hex = result.stdout.strip().split("\n")[0].strip()
    print(f"  tx_extra: {extra_hex[:40]}... ({len(bytes.fromhex(extra_hex))} bytes)")

    result = wallet_rpc("transfer", {
        "destinations": [{"amount": amount, "address": dest_addr}],
        "priority": 0,
        "ring_size": MIN_RING_SIZE,
        "extra": extra_hex,
        "get_tx_key": True,
        "get_tx_hex": True,
        "do_not_relay": False,
    })
    print(f"\n✓ Transaction broadcast!")
    print(f"  Tx hash: {result['tx_hash']}")
    return result

def do_treasury(amount, dest_addr):
    """Spend from Treasury. Multi-step governance flow:
    1. Create zero-sig tx_extra
    2. Create unsigned tx with wallet RPC
    3. Get tx_prefix_hash (zero sigs)
    4. Sign with governance keys
    5. Build real-sig tx_extra
    6. Replace extra in tx blob
    7. Broadcast
    """
    print(f"\n{'='*60}")
    print(f"TREASURY GOVERNANCE SPEND")
    print(f"  Amount:     {amount / COIN:.4f} MVC ({amount} atomic)")
    print(f"  Recipient:  {dest_addr}")
    print(f"{'='*60}")

    # Derive treasury keys
    wallet_info = derive_wallet(DOMAIN_TREASURY, 0)
    print(f"  Treasury address: {wallet_info['address']}")

    # Decode recipient address
    recv_result = subprocess.run(
        [GOV_CRYPTO, "decode", dest_addr],
        capture_output=True, text=True
    )
    if recv_result.returncode != 0:
        raise RuntimeError(f"Failed to decode recipient address: {recv_result.stderr}")
    recv_lines = recv_result.stdout.strip().split("\n")
    recv_spend = recv_lines[0].split(": ", 1)[1].strip()
    recv_view = recv_lines[1].split(": ", 1)[1].strip()

    # Get signer public keys (hardcoded in foundation_vesting.h — public info)
    signer_pubs = [
        "d12e990816a51475c1151ff04a4dc31b62693fbf2bf2d28076cda44e784699bb",
        "dfeb3f3ce8c3efe6c28b5670d365d1529ec6032dd2aa3cbd53a8595be9b7312a",
        "0e88576abcefeb9d095a9e4db59376f3e8eed036eae68949b2ce4253444493ae",
    ]
    signer_names = ["Signer 0 (MD5VJc...)", "Signer 1 (MDdsyR...)", "Signer 2 (M5hfHu...)"]

    # Ask which signers to use (need at least 2)
    print("\n  Available governance signers (public keys only):")
    selected = []
    while len(selected) < 2:
        print(f"\n  Need {2 - len(selected)} more signer(s).")
        sel_indices = {s["index"] for s in selected}
        for i, (name, pub) in enumerate(zip(signer_names, signer_pubs)):
            status = "✓" if i in sel_indices else " "
            print(f"    [{status}] {i+1}. {name}  pub={pub[:16]}...")
        print(f"    [ ] {len(signer_pubs)+1}. Custom signer (manually enter pubkey)")
        choice = input(f"  Select signer #{len(selected)+1}: ").strip()
        try:
            idx = int(choice) - 1
            if idx == len(signer_pubs):
                pub = input("  Enter signer public key hex: ").strip()
                selected.append({
                    "index": len(signer_pubs),
                    "pub": pub,
                    "name": f"Custom ({pub[:16]}...)"
                })
            elif 0 <= idx < len(signer_pubs):
                if idx not in sel_indices:
                    selected.append({
                        "index": idx,
                        "pub": signer_pubs[idx],
                        "name": signer_names[idx]
                    })
                else:
                    print("    Already selected.")
            else:
                print("    Invalid choice.")
        except ValueError:
            print("    Invalid choice.")

    actual_signer_pubs = [s["pub"] for s in selected]
    print(f"\n  Signers: {', '.join(s['name'] for s in selected)}")

    # ── Step 1: Create zero-sig tx_extra ─────────────────────────────
    print("\n  [1/7] Building zero-sig tx_extra...")
    result = subprocess.run(
        [GOV_SPEND, "treasury-zero", str(amount), dest_addr] + actual_signer_pubs,
        capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"gov_spend.py treasury-zero failed: {result.stderr}")
    zero_extra_hex = result.stdout.strip().split("\n")[0].strip()
    zero_extra_bytes = bytes.fromhex(zero_extra_hex)
    print(f"    Zero-sig extra: {len(zero_extra_bytes)} bytes")

    # ── Step 2: Create unsigned tx via wallet RPC ────────────────────
    print("  [2/7] Creating treasury wallet and transaction...")
    ensure_wallet(
        "treasury",
        wallet_info["address"],
        wallet_info["spend_sec"],
        wallet_info["view_sec"]
    )
    refresh_wallet()

    tx_result = wallet_rpc("transfer", {
        "destinations": [{"amount": amount, "address": dest_addr}],
        "priority": 0,
        "ring_size": MIN_RING_SIZE,
        "extra": zero_extra_hex,
        "get_tx_key": True,
        "get_tx_hex": True,
        "do_not_relay": True,
    })
    tx_blob_hex = tx_result["tx_blob"]
    print(f"    Unsigned tx created: {tx_result['tx_hash']}")
    print(f"    tx_blob: {len(tx_blob_hex)//2} bytes")

    # ── Step 3: Compute tx_prefix_hash with zeroed sigs ─────────────
    print("  [3/7] Computing tx_prefix_hash (governance zero-sig convention)...")
    tx_prefix_hash = compute_tx_prefix_hash_with_zeroed_sigs(tx_blob_hex)
    print(f"    tx_prefix_hash: {tx_prefix_hash}")

    # ── Step 4: Collect signatures from signers ─────────────────────
    print("  [4/7] Collecting signatures...")
    print(f"\n  ═══ GIVE THIS HASH TO EACH SIGNER ═══")
    print(f"  Hash: {tx_prefix_hash}")
    print(f"  Each signer runs:  gov_crypto sign <their_privkey> {tx_prefix_hash}")
    print(f"  ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══")
    sigs = []
    for s in selected:
        print(f"\n  Signer: {s['name']}  pub={s['pub']}")
        ans = input("  Paste signature hex (or press Enter to sign locally with privkey): ").strip()
        if ans:
            sig_hex = ans
        else:
            privkey = input("  Enter private key for local signing: ").strip()
            sig_out = subprocess.run(
                [GOV_CRYPTO, "sign", privkey, tx_prefix_hash],
                capture_output=True, text=True
            )
            if sig_out.returncode != 0:
                print(f"    Invalid private key, try pasting signature instead.")
                ans2 = input("  Paste signature hex: ").strip()
                if not ans2:
                    raise RuntimeError(f"Missing signature for {s['name']}")
                sig_hex = ans2
            else:
                sig_hex = sig_out.stdout.strip()
        sigs.append({"signer_key": s["pub"], "sig": sig_hex})
        print(f"    ✓ Signature collected ({len(bytes.fromhex(sig_hex))} bytes)")

    # ── Step 5: Build real-sig tx_extra ─────────────────────────────
    print("  [5/7] Building real-sig tx_extra...")
    sigs_json = json.dumps(sigs)

    # Use treasury-build command
    result = subprocess.run(
        [GOV_SPEND, "treasury-build", str(amount), dest_addr,
         actual_signer_pubs[0], sigs_json],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"gov_spend.py treasury-build failed: {result.stderr}")
    real_extra_hex = result.stdout.strip().split("\n")[0].strip()
    real_extra_bytes = bytes.fromhex(real_extra_hex)
    print(f"    Real-sig extra: {len(real_extra_bytes)} bytes")

    if len(real_extra_bytes) != len(zero_extra_bytes):
        print(f"    WARNING: Length mismatch! zero={len(zero_extra_bytes)} real={len(real_extra_bytes)}")

    # ── Step 6: Replace extra in tx blob ────────────────────────────
    print("  [6/7] Injecting real signatures into tx blob...")

    # Find and replace the zero-sig extra bytes within the hex-encoded blob
    # The extra in the blob starts with a varint-encoded length, followed by
    # the extra bytes. Our zero-sig extra has the same length as real-sig.
    blob_bytes = bytearray.fromhex(tx_blob_hex)
    extra_start, extra_data_end = find_extra_range(blob_bytes)

    # The extra data within the blob: varint(size) + data
    # We need to find where the governance tag starts within the extra data
    # and replace those bytes
    # Actually: the extra field is stored as varint(size) + data
    # The data part starts at extra_data_offset within the extra field
    # Let's parse it properly
    extra_raw = blob_bytes[extra_start:extra_data_end]
    _, first_extra_pos = _read_varint(extra_raw, 0)
    extra_data = extra_raw[first_extra_pos:]

    # Find 0xB0 tag in extra_data (governance transfer tag)
    tag_pos = extra_data.find(bytes([0xB0]))
    if tag_pos < 0:
        raise RuntimeError("Cannot find governance tag (0xB0) in tx extra")

    # The zero-sig extra starts with 0xB0 tag
    # Replace from tag_pos to end of extra_data with the real-sig data
    # But the real-sig extra also starts with 0xB0
    # The real extra data should have the same length
    real_extra_data = real_extra_bytes

    if len(real_extra_data) != len(extra_data):
        # Adjust length: the full extra includes auto-generated + governance
        # We need to replace only the governance part
        # The governance part starts where 0xB0 is and goes to the end
        gov_part = extra_data[tag_pos:]
        if len(gov_part) == len(real_extra_data):
            # Same length — direct replacement
            modified_extra_data = bytearray(extra_data)
            modified_extra_data[tag_pos:] = real_extra_data
        else:
            print(f"    WARNING: Governance part length mismatch. "
                  f"Expected {len(gov_part)}, got {len(real_extra_data)}")
            print(f"    Zero-sig extra hex: {zero_extra_hex}")
            print(f"    Real-sig extra hex: {real_extra_hex}")
            if ask_yesno("  Continue anyway?"):
                modified_extra_data = bytearray(extra_data)
                modified_extra_data[tag_pos:] = real_extra_data[:len(gov_part)]
            else:
                raise RuntimeError("Aborted")
    else:
        modified_extra_data = real_extra_data

    # Reconstruct the full extra field
    new_extra_field = bytearray(extra_raw[:first_extra_pos])  # size varint
    new_extra_field.extend(modified_extra_data)

    # Patch into blob
    modified_blob = bytearray(blob_bytes)
    modified_blob[extra_start:extra_data_end] = new_extra_field

    modified_blob_hex = modified_blob.hex()
    print(f"    Modified blob: {len(modified_blob)} bytes")

    # ── Step 7: Broadcast ───────────────────────────────────────────
    print("  [7/7] Broadcasting transaction...")
    send_result = daemon_rest("/send_raw_transaction", {
        "tx_as_hex": modified_blob_hex,
        "do_not_relay": False,
        "do_sanity_checks": True,
    })
    if send_result.get("status") == "OK":
        print(f"\n✓ Transaction broadcast successfully!")
        # Get tx hash from the wallet result
        print(f"  Tx hash (from wallet): {tx_result['tx_hash']}")
    else:
        reason = send_result.get("reason", "unknown")
        raise RuntimeError(f"Broadcast failed: {reason}")

    return tx_result

# ── Main ─────────────────────────────────────────────────────────────────

def main():
    print("=" * 60)
    print("  MevaCoin Premine Spend — Automated Tool")
    print("=" * 60)
    print()
    print(f"  Daemon RPC:  {DAEMON_URL}")
    print(f"  Wallet RPC:  {WALLET_URL}")
    print()

    # Check connectivity
    try:
        height_info = daemon_rest("/get_height")
        print(f"  ✓ Daemon connected (height: {height_info['height']})")
    except Exception as e:
        print(f"  ✗ Cannot connect to daemon at {DAEMON_URL}")
        print(f"    Error: {e}")
        print("    Start mevacoind with --rpc-bind-ip 127.0.0.1")
        sys.exit(1)

    try:
        wallet_info = wallet_rpc("get_address")
        print(f"  ✓ Wallet connected ({wallet_info['address'][:20]}...)")
    except Exception:
        print(f"  Wallet RPC not reachable at {WALLET_URL}, attempting to start it...")
        try:
            start_wallet_rpc()
            atexit.register(stop_wallet_rpc)
        except Exception as e2:
            print(f"  ✗ Failed to start wallet RPC: {e2}")
            print(f"    Start it manually: mevacoin-wallet-rpc --rpc-bind-port {WALLET_URL.rsplit(':', 1)[-1]}")
            sys.exit(1)

    print()

    # Select fund type
    fund_type = select_option("Select fund type:", [
        ("team-lock", "Simple transfer from founder's wallet (200k MVC, 24mo lock)"),
        ("network", "Network fund spend (400k MVC, rate-limited 10k/30d, no sigs)"),
        ("treasury", "Treasury governance spend (400k MVC, 2/3 signatures)"),
    ])

    # Ask for amount and recipient
    while True:
        amount_mvc_str = ask("Amount (MVC)")
        try:
            amount_mvc = float(amount_mvc_str)
            if amount_mvc <= 0:
                print("  Amount must be positive.")
                continue
            amount_atomic = int(amount_mvc * COIN)
            break
        except ValueError:
            print("  Invalid amount.")

    dest_addr = ask("Recipient MevaCoin address")
    if not dest_addr:
        print("  Address is required.")
        sys.exit(1)

    # Confirm
    print(f"\n  Summary:")
    print(f"    Fund type:   {fund_type}")
    print(f"    Amount:      {amount_mvc:.4f} MVC ({amount_atomic} atomic)")
    print(f"    Recipient:   {dest_addr}")

    if not ask_yesno("\n  Proceed?"):
        print("  Aborted.")
        sys.exit(0)

    # Execute
    try:
        if fund_type == "team-lock":
            do_team_lock(amount_atomic, dest_addr)
        elif fund_type == "network":
            do_network_fund(amount_atomic, dest_addr)
        elif fund_type == "treasury":
            do_treasury(amount_atomic, dest_addr)
        else:
            print(f"Unknown fund type: {fund_type}")
            sys.exit(1)
    except Exception as e:
        print(f"\n✗ ERROR: {e}")
        sys.exit(1)

    print(f"\n✓ Done! Funds will arrive at {dest_addr} after the tx is mined.")
    print(f"  Check with: mevacoin-wallet-rpc --wallet-file <wallet> --rpc-bind-port 12346")

if __name__ == "__main__":
    main()
