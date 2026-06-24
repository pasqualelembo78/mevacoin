#!/usr/bin/env python3
"""
gov_spend.py — MevaCoin Governance / Network Fund Spend Tool

Builds serialised tx_extra blobs for governance treasury and network fund
transactions.  Delegates crypto to companion gov_crypto binary.

Usage:
  # Generate a new key pair
  ./gov_spend.py genkey

  # Get public key from private key
  ./gov_spend.py pubkey <privkey_hex>

  # Decode MevaCoin address to public keys
  ./gov_spend.py decode <address>

  # Build network fund tx_extra (no signatures needed)
  ./gov_spend.py network <amount> <address>

  # Build governance treasury tx_extra with zero sigs (embed in tx, get hash)
  ./gov_spend.py treasury-zero <amount> <address> <signer0_pub> [signer1_pub ...]

  # Sign a tx_prefix_hash with governance keys, output sigs JSON
  ./gov_spend.py sign <tx_prefix_hash_hex> <signer0_priv> [signer1_priv ...]

  # Build final governance treasury tx_extra from sigs JSON
  ./gov_spend.py treasury-build <amount> <address> <signer0_pub> <sigs_json>

  # Verify governance signatures in a tx_extra blob
  ./gov_spend.py verify <tx_extra_hex> <tx_prefix_hash_hex>

Environment:
  GOV_CRYPTO_BIN  — path to gov_crypto binary (default: ./gov_crypto)
"""

import sys
import os
import subprocess
import json

GOV_CRYPTO = os.environ.get("GOV_CRYPTO_BIN", os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "gov_crypto"
))

# ── Tag constants ─────────────────────────────────────────────────────
TX_EXTRA_TAG_GOVERNANCE_TRANSFER     = 0xB0
TX_EXTRA_TAG_GOVERNANCE_ADD_SIGNER   = 0xB1
TX_EXTRA_TAG_GOVERNANCE_REMOVE_SIGNER = 0xB2
TX_EXTRA_TAG_NETWORK_FUND_TRANSFER   = 0xC0


# ── Varint helpers (Monero binary archive) ────────────────────────────

def _write_varint(val):
    buf = bytearray()
    while val >= 0x80:
        buf.append((val & 0x7F) | 0x80)
        val >>= 7
    buf.append(val & 0x7F)
    return bytes(buf)


def _read_varint(buf, offset):
    val = 0
    shift = 0
    while offset < len(buf):
        b = buf[offset]
        offset += 1
        val |= (b & 0x7F) << shift
        shift += 7
        if not (b & 0x80):
            return val, offset
    raise ValueError("Truncated varint")


# ── Binary serialization (matching Monero binary_archive) ────────────

def _ser_governance_transfer(amount, recipient_spend, recipient_view, signatures):
    """
    tx_extra_governance_transfer (tag 0xB0):
      amount(varint) + spend(32) + view(32) + sig_count(varint) + sigs[]
    Each sig: signer_key(32) + sig_c(32) + sig_r(32)
    """
    data = _write_varint(amount)
    data += bytes.fromhex(recipient_spend)
    data += bytes.fromhex(recipient_view)
    data += _write_varint(len(signatures))
    for sk_hex, sig_hex in signatures:
        data += bytes.fromhex(sk_hex)
        data += bytes.fromhex(sig_hex)
    return data


def _ser_network_fund_transfer(amount, recipient_spend, recipient_view):
    data = _write_varint(amount)
    data += bytes.fromhex(recipient_spend)
    data += bytes.fromhex(recipient_view)
    return data


def build_extra_blob(tag, data):
    return bytes([tag]) + data


def parse_governance_extra(blob):
    """Parse tx_extra blob, extract governance fields. Returns dict."""
    tag = blob[0]
    if tag != TX_EXTRA_TAG_GOVERNANCE_TRANSFER:
        raise ValueError(f"Unexpected tag: 0x{tag:02X}")
    offset = 1
    amount, offset = _read_varint(blob, offset)
    recv_spend = blob[offset:offset+32].hex()
    offset += 32
    recv_view = blob[offset:offset+32].hex()
    offset += 32
    sig_count, offset = _read_varint(blob, offset)
    sigs = []
    for _ in range(sig_count):
        sk = blob[offset:offset+32].hex()
        offset += 32
        sg = blob[offset:offset+64].hex()
        offset += 64
        sigs.append({"signer_key": sk, "sig": sg})
    return {
        "tag": tag,
        "amount": amount,
        "recipient_spend": recv_spend,
        "recipient_view": recv_view,
        "signatures": sigs,
    }


# ── Gov crypto binary wrapper ─────────────────────────────────────────

def _gov(*args):
    result = subprocess.run(
        [GOV_CRYPTO] + list(args), capture_output=True, text=True
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"gov_crypto failed: {result.stderr.strip()}")
    return result.stdout.strip()


def gov_sign(privkey_hex, hash_hex):
    return _gov("sign", privkey_hex, hash_hex)


def gov_pubkey(privkey_hex):
    return _gov("pubkey", privkey_hex)


def gov_genkey():
    out = _gov("genkey")
    lines = out.split("\n")
    privkey = lines[0].split(": ")[1]
    pubkey = lines[1].split(":  ")[1]
    return privkey, pubkey


def gov_decode(address):
    out = _gov("decode", address)
    lines = out.split("\n")
    spend = lines[0].split(": ")[1]
    view = lines[1].split(":  ")[1]
    return spend, view


def gov_verify(pubkey_hex, sig_hex, hash_hex):
    result = subprocess.run(
        [GOV_CRYPTO, "verify", pubkey_hex, sig_hex, hash_hex],
        capture_output=True, text=True
    )
    return result.stdout.strip()


# ── Commands ──────────────────────────────────────────────────────────

def cmd_genkey():
    priv, pub = gov_genkey()
    print(f"Private key: {priv}")
    print(f"Public key:  {pub}")


def cmd_pubkey(privkey_hex):
    pub = gov_pubkey(privkey_hex)
    print(pub)


def cmd_decode(address):
    spend, view = gov_decode(address)
    print(f"Spend key: {spend}")
    print(f"View key:  {view}")


def cmd_network(amount_str, address):
    amount = int(amount_str)
    spend, view = gov_decode(address)
    data = _ser_network_fund_transfer(amount, spend, view)
    blob = build_extra_blob(TX_EXTRA_TAG_NETWORK_FUND_TRANSFER, data)
    print(blob.hex())
    print(f"\nNetwork fund transfer tx_extra hex (above), {len(blob)} bytes",
          file=sys.stderr)


def cmd_treasury_zero(amount_str, address, *signer_pubs):
    """Build governance tx_extra with ZERO signatures for embedding in tx."""
    amount = int(amount_str)
    spend, view = gov_decode(address)
    zero_sigs = [(pk, "00" * 64) for pk in signer_pubs]
    data = _ser_governance_transfer(amount, spend, view, zero_sigs)
    blob = build_extra_blob(TX_EXTRA_TAG_GOVERNANCE_TRANSFER, data)
    print(blob.hex())
    print(f"\nGovernance treasury tx_extra (zero sigs), {len(blob)} bytes",
          file=sys.stderr)
    print(f"Destination: {address}", file=sys.stderr)
    print(f"Amount:      {amount}", file=sys.stderr)
    print(f"Signers:     {len(signer_pubs)}", file=sys.stderr)
    for pk in signer_pubs:
        print(f"  {pk}", file=sys.stderr)
    print(file=sys.stderr)
    print("Instructions:", file=sys.stderr)
    print("1. Embed this tx_extra hex in your transaction", file=sys.stderr)
    print("2. Compute tx_prefix_hash from the unsigned tx", file=sys.stderr)
    print("3. Sign with: gov_spend.py sign <hash> <priv0> [priv1 ...]",
          file=sys.stderr)
    print("4. Build final: gov_spend.py treasury-build <amount> <addr> "
          "<pub0> <sigs.json>", file=sys.stderr)


def cmd_sign(hash_hex, *privkeys):
    """Sign a tx_prefix_hash with governance keys. Outputs JSON for treasury-build."""
    sigs = []
    for pk in privkeys:
        pub = gov_pubkey(pk)
        sig_hex = gov_sign(pk, hash_hex)
        sigs.append({"signer_key": pub, "sig": sig_hex})
    print(json.dumps(sigs, indent=2))


def cmd_treasury_build(amount_str, address, signer_pub, sigs_json_str):
    """Build final governance tx_extra with real signatures from sign output."""
    amount = int(amount_str)
    spend, view = gov_decode(address)
    sigs = json.loads(sigs_json_str)
    sig_tuples = [(s["signer_key"], s["sig"]) for s in sigs]
    # Prepend the primary signer's pubkey to the list if it's not already there
    all_sigs = [(signer_pub, "00" * 64)]  # zero placeholder for primary
    for st in sig_tuples:
        if st[0] != signer_pub:
            all_sigs.append(st)
    # Actually, just use all sigs from JSON
    data = _ser_governance_transfer(amount, spend, view, sig_tuples)
    blob = build_extra_blob(TX_EXTRA_TAG_GOVERNANCE_TRANSFER, data)
    print(blob.hex())
    print(f"\nFinal governance tx_extra hex (above), {len(blob)} bytes",
          file=sys.stderr)


def cmd_verify(tx_extra_hex, hash_hex):
    """Verify governance signatures in a tx_extra blob."""
    blob = bytes.fromhex(tx_extra_hex)
    info = parse_governance_extra(blob)
    print(f"Tag:      0x{info['tag']:02X}")
    print(f"Amount:   {info['amount']}")
    print(f"To:       spend={info['recipient_spend'][:16]}...")
    print(f"          view={info['recipient_view'][:16]}...")
    print(f"Signers:  {len(info['signatures'])}")
    for i, sig in enumerate(info['signatures']):
        result = gov_verify(sig['signer_key'], sig['sig'], hash_hex)
        print(f"  #{i}: key={sig['signer_key'][:16]}... {result}")


# ── Main ──────────────────────────────────────────────────────────────

COMMANDS = {
    "genkey":        lambda args: cmd_genkey(),
    "pubkey":        lambda args: cmd_pubkey(*args),
    "decode":        lambda args: cmd_decode(args[0]),
    "network":       lambda args: cmd_network(*args),
    "treasury-zero": lambda args: cmd_treasury_zero(*args),
    "treasury-build": lambda args: cmd_treasury_build(*args),
    "sign":          lambda args: cmd_sign(*args),
    "verify":        lambda args: cmd_verify(*args),
}


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__, file=sys.stderr)
        sys.exit(0 if sys.argv[1] in ("-h", "--help") else 1)

    cmd = sys.argv[1]
    args = sys.argv[2:]

    if cmd not in COMMANDS:
        print(f"Unknown command: {cmd}", file=sys.stderr)
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    COMMANDS[cmd](args)


if __name__ == "__main__":
    main()
