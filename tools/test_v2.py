#!/usr/bin/env python3
"""Test v2 network fund spend with proper refresh from height 0."""
import sys, os, json, subprocess, time, signal, atexit
import requests

sys.setrecursionlimit(10000)

DAEMON_URL = "http://82.165.218.56:18081"
WALLET_URL = "http://127.0.0.1:18087"
COIN = 10**12
DOMAIN_NETWORK = "mevacoin_network_fund"

BIN = "/root/mevacoin/build/Linux/mevacoin/release/bin/mevacoin-wallet-rpc"
GOV = "/root/mevacoin/tools/gov_crypto"
GOV_SPEND = "/root/mevacoin/tools/gov_spend.py"
DATADIR = "/root/mevacoin/tools/wallet_rpc_data"

os.makedirs(DATADIR, exist_ok=True)

def wallet_rpc(method, params=None):
    url = WALLET_URL + "/json_rpc"
    payload = {"jsonrpc":"2.0","id":"0","method":method}
    if params: payload["params"] = params
    r = requests.post(url, json=payload, timeout=120)
    data = r.json()
    if "error" in data:
        raise RuntimeError(f"Wallet RPC error ({method}): {data['error']}")
    return data.get("result")

def main():
    # Start wallet-rpc on port 18087
    proc = subprocess.Popen([
        BIN, "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", "18087",
        "--wallet-dir", DATADIR, "--daemon-address", DAEMON_URL.replace("http://",""),
        "--trusted-daemon", "--disable-rpc-login",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    atexit.register(lambda: proc.terminate())

    for i in range(30):
        try:
            wallet_rpc("get_version")
            break
        except: time.sleep(0.5)
    else:
        sys.exit("Wallet RPC did not start")

    print("Wallet RPC ready")

    # Derive network fund keys
    info = subprocess.run([GOV, "derive-wallet", DOMAIN_NETWORK, "0"], capture_output=True, text=True)
    if info.returncode != 0:
        sys.exit(f"gov_crypto failed: {info.stderr}")
    lines = info.stdout.strip().split("\n")
    keys = {}
    for line in lines:
        k, v = line.split(": ", 1)
        keys[k] = v.strip()
    spend_sec = keys["spend_sec"]
    view_sec = keys["view_sec"]
    address = keys["address"]
    print(f"Network fund address: {address}")

    # Create/open wallet
    try:
        w = wallet_rpc("create_wallet", {"filename":"test_netfund","password":"","language":"English","spendkey":spend_sec,"viewkey":view_sec})
        print(f"Wallet created")
    except:
        w = wallet_rpc("open_wallet", {"filename":"test_netfund","password":""})
        print(f"Wallet opened")

    # refresh from height 0
    r = wallet_rpc("refresh", {"start_height": 0})
    print(f"Refresh result: {json.dumps(r, indent=2)}")

    # Get balance
    bal = wallet_rpc("get_balance")
    print(f"Balance: {bal}")

    dest_addr = "M5MxXAn9DPJfqU9DZBsuuV8f1kfnmnud6iGxtEKPeTFB9YC57RCXaFViMGf11joaAJ9yoXxF49b2C2mBoxdN8u6j1qxDcK2"
    amount = 10 * COIN
    result = subprocess.run([GOV_SPEND, "network", str(amount), dest_addr], capture_output=True, text=True)
    extra_hex = result.stdout.strip().split("\n")[0].strip()
    print(f"Extra hex: {extra_hex[:60]}...")

    try:
        t = wallet_rpc("transfer", {
            "destinations": [{"amount": amount, "address": dest_addr}],
            "priority": 0,
            "ring_size": 2,
            "extra": extra_hex,
        })
        print(f"Transfer result: {json.dumps(t, indent=2)}")
    except RuntimeError as e:
        print(f"RPC error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
