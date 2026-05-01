#!/bin/bash
# ============================================
# MevaCoin Daemon — Avvio Manuale
# Server: seed1 (82.165.218.56)
# ============================================
set -e

DAEMON="/root/mevacoin/build/Linux/master/release/bin/mevacoind"
DATA_DIR="/root/.mevacoin"

echo "MevaCoin Daemon — seed1 (82.165.218.56)"

# Ferma daemon esistente
if pgrep -x "mevacoind" > /dev/null; then
    echo ">>> Fermata daemon esistente..."
    pkill mevacoind || true
    sleep 3
    pgrep -x "mevacoind" > /dev/null && pkill -9 mevacoind && sleep 2
fi

# Crea directory dati
mkdir -p ${DATA_DIR}

# Avvia daemon
echo ">>> Avvio daemon..."
${DAEMON} \
    --detach --non-interactive \
    --data-dir ${DATA_DIR} \
    --p2p-bind-ip 0.0.0.0 --p2p-bind-port 18080 \
    --rpc-bind-ip 0.0.0.0 --rpc-bind-port 18081 \
    --confirm-external-bind \
    --add-exclusive-node 87.106.40.193:18080 \
    --add-exclusive-node 87.106.233.72:18080 \
    --log-level 2 \
    --log-file ${DATA_DIR}/mevacoind.log

echo ">>> Attesa avvio (10s)..."
sleep 10

# ============================================
# VERIFICA STATO + RANDOMX
# ============================================
echo ""
echo "=========================================="
echo "  VERIFICA STATO DAEMON"
echo "=========================================="

if pgrep -x "mevacoind" > /dev/null; then
    echo "✅ Daemon attivo (PID: $(pgrep -x mevacoind))"
    echo ""
    
    # 1) Info generale
    echo "--- Info Generale ---"
    curl -s http://127.0.0.1:18081/json_rpc \
        -d '{"jsonrpc":"2.0","id":"0","method":"get_info"}' \
        -H "Content-Type: application/json" | \
        python3 -c '
import sys, json
try:
    d = json.load(sys.stdin)["result"]
    print("  Altezza:  %s" % d.get("height", "N/A"))
    print("  TopHash:  %s..." % str(d.get("top_block_hash", "N/A"))[:16])
    print("  Peers:    %s" % (d.get("incoming_connections_count", 0) + d.get("outgoing_connections_count", 0)))
    print("  Synced:   %s" % d.get("synchronized", "N/A"))
    print("  Mainnet:  %s" % d.get("mainnet", "N/A"))
except Exception as e:
    print("  Errore: %s" % e)
' 2>/dev/null || echo "  (RPC non ancora pronto)"
    
    echo ""
    
    # 2) Hard Fork Info — VERIFICA RANDOMX
    echo "--- Verifica Hard Fork (RandomX = versione >= 12) ---"
    curl -s http://127.0.0.1:18081/json_rpc \
        -d '{"jsonrpc":"2.0","id":"0","method":"hard_fork_info"}' \
        -H "Content-Type: application/json" | \
        python3 -c '
import sys, json
try:
    d = json.load(sys.stdin)["result"]
    version = d.get("version", 0)
    print("  Versione HF attiva: %s" % version)
    print("  Enabled:            %s" % d.get("enabled", "N/A"))
    print("  Stato:              %s" % d.get("state", "N/A"))
    if version >= 12:
        print("  ✅ RANDOMX ATTIVO! (versione %s >= 12)" % version)
    else:
        print("  ❌ RandomX NON attivo (versione %s < 12)" % version)
except Exception as e:
    print("  Errore: %s" % e)
' 2>/dev/null || echo "  (RPC non ancora pronto)"

    echo ""
    
    # 3) Cerca nei log conferma RandomX
    echo "--- Log RandomX ---"
    grep -i "randomx\|rx_slow\|hard.fork\|version.*12\|version.*16" ${DATA_DIR}/mevacoind.log 2>/dev/null | tail -10 || echo "  (nessun log RandomX trovato ancora)"
    
else
    echo "❌ Daemon non avviato!"
    echo "   Controlla: tail -50 ${DATA_DIR}/mevacoind.log"
fi

echo ""
echo "=========================================="
echo "  Comandi utili:"
echo "  tail -f ${DATA_DIR}/mevacoind.log"
echo "  ${DAEMON} status"
echo "=========================================="
