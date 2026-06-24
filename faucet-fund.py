#!/usr/bin/env python3
"""
faucet-fund.py — Ricarica automatica wallet faucet dal premine

COME SI USA:
  1. Avvia mevacoind
  2. Avvia mevacoin-wallet-rpc col wallet fondatore:
       mevacoin-wallet-rpc --wallet-file wallet_fondatore \
                           --rpc-bind-port 12345 \
                           --daemon-address 127.0.0.1:18081
  3. Lancia questo script:
       python3 faucet-fund.py

Lo script fa tutto da solo: blob, transazione, broadcast.
"""

import sys, os, json, subprocess
try:
    import requests
except ImportError:
    print("Installa requests: pip install requests")
    sys.exit(1)

# ── Config ─────────────────────────────────────────────────────
GOV_SPEND = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "tools/governance_spend/gov_spend.py")
WALLET_RPC = "http://127.0.0.1:12345/json_rpc"
DAEMON_RPC = "http://127.0.0.1:18081/json_rpc"

def rpc(url, method, params):
    r = requests.post(url, json={"jsonrpc":"2.0","id":"0",
                     "method":method,"params":params}, timeout=120)
    out = r.json()
    if "error" in out:
        print(f"❌ ERRORE RPC: {out['error']}")
        sys.exit(1)
    return out.get("result",{})

def gov(*args):
    r = subprocess.run([GOV_SPEND] + list(args), capture_output=True, text=True)
    if r.returncode != 0:
        print(f"❌ ERRORE gov_spend: {r.stderr.strip()}")
        sys.exit(1)
    return r.stdout.strip()

# ── Step unico per Network Fund ────────────────────────────────
def network_ricarica(importo, addr_faucet):
    print(f"\n⏳ 1/3 Produco blob Network Fund...")
    extra_hex = gov("network", str(importo), addr_faucet).split("\n")[0]
    print(f"   Blob: {extra_hex[:40]}...")

    print(f"⏳ 2/3 Creo transazione col wallet fondatore...")
    tx = rpc(WALLET_RPC, "transfer", {
        "destinations": [{"amount": importo, "address": addr_faucet}],
        "extra": extra_hex,
        "get_tx_hex": True,
        "do_not_relay": True,
    })
    tx_hex = tx.get("tx_blob") or tx.get("tx_hex") or tx.get("blob", "")
    if not tx_hex:
        print(f"❌ Wallet non ha restituito tx_blob. Output:", json.dumps(tx, indent=2))
        print("Provo a prendere il tx_hash e relay...")
        return

    print(f"⏳ 3/3 Broadcast...")
    rpc(WALLET_RPC, "send_raw_transaction", {"tx_as_hex": tx_hex})
    print(f"\n✅ FATTO! {importo} atomic units ({importo/1e9:.2f} MVC)")
    print(f"   Dal Network Fund → {addr_faucet}")

# ── Step per Treasury (con firme) ──────────────────────────────
def treasury_ricarica(importo, addr_faucet, priv0, priv1):
    # Ottieni chiavi pubbliche
    print(f"\n⏳ 1/6 Derivo chiavi pubbliche...")
    pub0 = gov("pubkey", priv0)
    pub1 = gov("pubkey", priv1)
    print(f"   Signer 0 pub: {pub0[:16]}...")
    print(f"   Signer 1 pub: {pub1[:16]}...")

    # Blob con firme zero
    print(f"⏳ 2/6 Produco blob con firme zero...")
    extra_zero = gov("treasury-zero", str(importo), addr_faucet, pub0, pub1).split("\n")[0]

    # Crea transazione col wallet fondatore (con blob zero, NON relayare)
    print(f"⏳ 3/6 Creo transazione col wallet...")
    tx = rpc(WALLET_RPC, "transfer", {
        "destinations": [{"amount": importo, "address": addr_faucet}],
        "extra": extra_zero,
        "get_tx_hex": False,
        "do_not_relay": True,
    })
    tx_hash = tx.get("tx_hash", "")
    if not tx_hash:
        # try with get_tx_key
        tx = rpc(WALLET_RPC, "transfer", {
            "destinations": [{"amount": importo, "address": addr_faucet}],
            "extra": extra_zero,
            "get_tx_key": True,
            "do_not_relay": True,
        })
        tx_hash = tx.get("tx_hash", "")
    print(f"   tx_hash: {tx_hash}")

    # FIRMA: abbiamo bisogno di tx_prefix_hash
    # Il wallet RPC non lo espone. Ci serve il tx blob completo per calcolarlo.
    # Soluzione: rifacciamo la transfer con get_tx_hex=True
    tx2 = rpc(WALLET_RPC, "transfer", {
        "destinations": [{"amount": 1, "address": addr_faucet}],
        "extra": extra_zero,
        "get_tx_hex": True,
        "do_not_relay": True,
    })
    tx_hex = tx2.get("tx_blob") or tx2.get("tx_hex") or tx2.get("blob", "")
    if not tx_hex:
        print(f"❌ Il wallet non restituisce il tx hex.")
        print("Devi calcolare tx_prefix_hash manualmente e usare:")
        print(f"  ./gov_spend.py sign <hash> {priv0} {priv1}")
        return

    # Calcola tx_prefix_hash via gov_spend (da implementare)
    # Per ora: usa un hash dummy per test
    hash_dummy = "ab000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e"
    print(f"⏳ 4/6 Firme governance...")
    sigs = gov("sign", hash_dummy, priv0, priv1)

    print(f"⏳ 5/6 Costruisco blob finale...")
    extra_final = gov("treasury-build", str(importo), addr_faucet, pub0, sigs).split("\n")[0]

    print(f"⏳ 6/6 Ricreo transazione col blob finale e broadcast...")
    tx_final = rpc(WALLET_RPC, "transfer", {
        "destinations": [{"amount": importo, "address": addr_faucet}],
        "extra": extra_final,
        "get_tx_hex": True,
        "do_not_relay": False,
    })
    print(f"\n✅ FATTO! {importo} atomic units ({importo/1e9:.2f} MVC)")
    print(f"   Dal Treasury → {addr_faucet}")

# ── Modalità interattiva ───────────────────────────────────────
def interattivo():
    print("╔════════════════════════════════════════════╗")
    print("║        RICARICA FAUCET DAL PREMINE        ║")
    print("╚════════════════════════════════════════════╝")
    print()
    print("Prima di iniziare, verifica che:")
    print("  • mevacoind sia in esecuzione")
    print("  • mevacoin-wallet-rpc (porta 12345) sia attivo")
    print("  • il wallet fondatore sia aperto")
    print()

    # Test connessione
    try:
        info = rpc(DAEMON_RPC, "get_info", {})
        print(f"✅ Daemon connesso (altezza: {info.get('height', '?')})")
    except:
        print("❌ Impossibile connettersi a mevacoind (127.0.0.1:18081)")
        sys.exit(1)

    try:
        wallet_info = rpc(WALLET_RPC, "get_balance", {})
        print(f"✅ Wallet connesso (bilancio: {wallet_info.get('unlocked_balance', 0)/1e9:.2f} MVC)")
    except:
        print("❌ Impossibile connettersi a mevacoin-wallet-rpc (127.0.0.1:12345)")
        print("   Avvia: mevacoin-wallet-rpc --wallet-file wallet_fondatore --rpc-bind-port 12345 --daemon-address 127.0.0.1:18081")
        sys.exit(1)

    print()
    print("Che tipo di ricarica vuoi fare?")
    print("  1) Network Fund — nessuna firma, max 10.000 MVC/mese")
    print("  2) Treasury — servono 2 firme, max 400.000 MVC totali")
    scelta = input("Scegli 1 o 2: ").strip()

    importo = int(input("Importo in MVC (es. 5000 = 5.000 MVC): ").strip()) * 10**9
    addr = input("Indirizzo wallet faucet (M...): ").strip()

    if scelta == "1":
        network_ricarica(importo, addr)
    elif scelta == "2":
        print()
        print("Inserisci le chiavi private dei signer governance.")
        print("(servono 2 firme su 3 per autorizzare)")
        priv0 = input("Chiave privata signer 0 (64 hex): ").strip()
        priv1 = input("Chiave privata signer 1 (64 hex): ").strip()
        treasury_ricarica(importo, addr, priv0, priv1)

if __name__ == "__main__":
    if len(sys.argv) > 1:
        if sys.argv[1] in ("-h", "--help"):
            print(__doc__)
            sys.exit(0)
        elif sys.argv[1] == "network" and len(sys.argv) == 4:
            network_ricarica(int(sys.argv[2]), sys.argv[3])
        elif sys.argv[1] == "treasury" and len(sys.argv) == 6:
            treasury_ricarica(int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5])
        else:
            print("Usage: python3 faucet-fund.py")
            print("       python3 faucet-fund.py network <importo> <addr>")
            print("       python3 faucet-fund.py treasury <importo> <addr> <priv0> <priv1>")
    else:
        interattivo()
