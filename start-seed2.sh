#!/bin/bash
# ============================================
# MevaCoin Daemon — Avvio Manuale
# Server: seed2 (87.106.40.193)
# ============================================
set -e

DAEMON="/root/mevacoin/build/Linux/master/release/bin/mevacoind"
DATA_DIR="/root/.mevacoin"

echo "MevaCoin Daemon — seed2 (87.106.40.193)"

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
    --add-exclusive-node 82.165.218.56:18080 \\
    --add-exclusive-node 87.106.233.72:18080 \\
    --log-level 2 \
    --log-file ${DATA_DIR}/mevacoind.log

echo ">>> Attesa avvio (10s)..."
sleep 10

# Verifica
if pgrep -x "mevacoind" > /dev/null; then
    echo "✅ Daemon attivo (PID: $(pgrep -x mevacoind))"
    curl -s http://127.0.0.1:18081/json_rpc \
        -d '{"jsonrpc":"2.0","id":"0","method":"get_info"}' \
        -H "Content-Type: application/json" | \
        python3 -c "import sys,json; d=json.load(sys.stdin)['result']; print(f\"  Altezza: {d['height']} | Peers: {d['incoming_connections_count']+d['outgoing_connections_count']} | Synced: {d['synchronized']}\")" 2>/dev/null || echo "  (RPC non ancora pronto)"
else
    echo "❌ Daemon non avviato! Controlla: tail -50 ${DATA_DIR}/mevacoind.log"
fi
