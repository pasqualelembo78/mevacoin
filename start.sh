#!/bin/bash
set -e
KNOWN_IPS=("82.165.218.56" "87.106.40.193" "87.106.233.72")
DAEMON="/root/mevacoin/build/Linux/master/release/bin/mevacoind"
DATA_DIR="/root/.mevacoin"
BUILD_DIR="/root/mevacoin"
REPO_URL="https://github.com/pasqualelembo78/mevacoin.git"

echo ""
echo "=========================================="
echo "  RILEVAMENTO IP SERVER"
echo "=========================================="
MY_IP=""
SERVER_IPS=$(hostname -I 2>/dev/null || ip -4 addr show | grep -oP '(?<=inet\s)\d+(\.\d+){3}')
for ip in ${SERVER_IPS}; do
    for known in "${KNOWN_IPS[@]}"; do
        if [ "$ip" == "$known" ]; then MY_IP="$known"; break 2; fi
    done
done
if [ -z "$MY_IP" ]; then
    echo "ERRORE: Nessun IP conosciuto trovato!"
    echo "   IP rilevati: ${SERVER_IPS}"
    echo "   IP attesi: ${KNOWN_IPS[*]}"
    exit 1
fi
echo "IP rilevato: ${MY_IP}"
case "$MY_IP" in
    "82.165.218.56") SEED_NAME="seed1" ;;
    "87.106.40.193") SEED_NAME="seed2" ;;
    "87.106.233.72") SEED_NAME="seed3" ;;
esac
echo "   Server: ${SEED_NAME}"
EXCLUSIVE_NODES=()
for known in "${KNOWN_IPS[@]}"; do
    if [ "$known" != "$MY_IP" ]; then EXCLUSIVE_NODES+=("$known"); fi
done
echo "   Nodi esclusivi: ${EXCLUSIVE_NODES[0]}:18080, ${EXCLUSIVE_NODES[1]}:18080"

# ============================================
# FERMA DAEMON ESISTENTE
# ============================================
echo ""
echo "=========================================="
echo "  ${SEED_NAME} (${MY_IP}) - Preparazione"
echo "=========================================="
if pgrep -x "mevacoind" > /dev/null; then
    echo ">>> Fermata daemon esistente..."
    pkill mevacoind || true
    sleep 3
    pgrep -x "mevacoind" > /dev/null && pkill -9 mevacoind && sleep 2
    echo "Daemon fermato."
else
    echo "   Nessun daemon attivo."
fi

# ============================================
# REPOSITORY
# ============================================
echo ""
echo "=========================================="
echo "  REPOSITORY"
echo "=========================================="

if [ ! -d "${BUILD_DIR}" ]; then
    # =======================================================
    # CARTELLA NON ESISTE -> PULIZIA TOTALE + CLONE + BUILD
    # Tutto automatico, nessuna domanda
    # =======================================================
    echo ">>> ${BUILD_DIR} non trovata."
    echo ">>> Prima installazione: pulizia completa automatica."
    echo ""

    echo ">>> ccache -C (pulizia cache compilazione)..."
    ccache -C 2>/dev/null || echo "   (ccache non installato, salto)"

    if [ -d "${DATA_DIR}" ]; then
        echo ">>> Rimozione dati blockchain residui ${DATA_DIR}..."
        rm -rf "${DATA_DIR}"
        echo "   ${DATA_DIR} rimossa."
    fi

    echo ""
    echo ">>> Clonazione da ${REPO_URL}..."
    git clone --recursive "${REPO_URL}" "${BUILD_DIR}"
    echo "   Repository clonato."

    echo ""
    echo "=========================================="
    echo "  COMPILAZIONE (prima installazione)"
    echo "=========================================="
    cd "${BUILD_DIR}"
    echo ">>> make release... (potrebbe richiedere tempo)"
    make release
    echo "   make release completato."

else
    # =======================================================
    # CARTELLA ESISTE -> FLUSSO INTERATTIVO
    # =======================================================
    echo ">>> ${BUILD_DIR} trovata."
    echo ""
    read -p ">>> Installazione pulita? (rimuove tutto e riscarica da GitHub) (s/n, default: n): " R1
    R1=${R1:-n}

    if [[ "$R1" =~ ^[sSyY] ]]; then
        echo ""
        echo ">>> Installazione pulita richiesta."
        echo ">>> ccache -C..."
        ccache -C 2>/dev/null || true
        echo ">>> make clean..."
        cd "${BUILD_DIR}" && make clean 2>/dev/null || true
        cd /root
        if [ -d "${DATA_DIR}" ]; then
            echo ">>> Rimozione ${DATA_DIR}..."
            rm -rf "${DATA_DIR}"
            echo "   ${DATA_DIR} rimossa."
        fi
        echo ">>> Rimozione ${BUILD_DIR}..."
        rm -rf "${BUILD_DIR}"
        echo "   ${BUILD_DIR} rimossa."
        echo ""
        echo ">>> Clonazione da ${REPO_URL}..."
        git clone --recursive "${REPO_URL}" "${BUILD_DIR}"
        echo "   Repository clonato."
    else
        echo ">>> OK, uso la cartella esistente."
    fi

    # --- Pulizia dati blockchain ---
    echo ""
    echo "=========================================="
    echo "  PULIZIA DATI BLOCKCHAIN"
    echo "=========================================="
    if [ -d "${DATA_DIR}" ]; then
        read -p ">>> Rimuovere dati blockchain? (s/n, default: s): " R2
        R2=${R2:-s}
        if [[ "$R2" =~ ^[sSyY] ]]; then
            rm -rf "${DATA_DIR}"
            echo "   ${DATA_DIR} rimossa."
        else
            echo ">>> Mantengo ${DATA_DIR}."
        fi
    else
        echo "   ${DATA_DIR} non presente."
    fi

    # --- Compilazione ---
    echo ""
    echo "=========================================="
    echo "  COMPILAZIONE"
    echo "=========================================="
    cd "${BUILD_DIR}"
    read -p ">>> make clean + ccache -C prima di compilare? (s/n, default: s): " R3
    R3=${R3:-s}
    if [[ "$R3" =~ ^[sSyY] ]]; then
        echo ">>> ccache -C..."
        ccache -C 2>/dev/null || true
        echo ">>> make clean..."
        make clean 2>/dev/null || true
        echo "   Pulizia completata."
    else
        echo ">>> Salto pulizia..."
    fi
    echo ""
    echo ">>> make release... (potrebbe richiedere tempo)"
    make release
    echo "   make release completato."
fi

# ============================================
# AVVIO DAEMON
# ============================================
mkdir -p "${DATA_DIR}"

echo ""
echo "=========================================="
echo "  AVVIO DAEMON - ${SEED_NAME}"
echo "=========================================="
echo ">>> Avvio mevacoind su ${MY_IP}..."
echo "    Nodo 1: ${EXCLUSIVE_NODES[0]}:18080"
echo "    Nodo 2: ${EXCLUSIVE_NODES[1]}:18080"
echo ""

${DAEMON} \
    --detach --non-interactive \
    --data-dir ${DATA_DIR} \
    --p2p-bind-ip 0.0.0.0 --p2p-bind-port 18080 \
    --rpc-bind-ip 0.0.0.0 --rpc-bind-port 18081 \
    --confirm-external-bind \
    --add-exclusive-node ${EXCLUSIVE_NODES[0]}:18080 \
    --add-exclusive-node ${EXCLUSIVE_NODES[1]}:18080 \
    --log-level 2 \
    --log-file ${DATA_DIR}/mevacoind.log

echo ">>> Attesa avvio (10s)..."
sleep 10

# ============================================
# VERIFICA STATO
# ============================================
echo ""
echo "=========================================="
echo "  VERIFICA STATO - ${SEED_NAME}"
echo "=========================================="
if pgrep -x "mevacoind" > /dev/null; then
    echo "Daemon attivo (PID: $(pgrep -x mevacoind))"
    echo ""
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
    echo "--- Verifica Hard Fork (RandomX = versione >= 12) ---"
    curl -s http://127.0.0.1:18081/json_rpc \
        -d '{"jsonrpc":"2.0","id":"0","method":"hard_fork_info"}' \
        -H "Content-Type: application/json" | \
        python3 -c '
import sys, json
try:
    d = json.load(sys.stdin)["result"]
    v = d.get("version", 0)
    print("  Versione HF attiva: %s" % v)
    print("  Enabled:            %s" % d.get("enabled", "N/A"))
    print("  Stato:              %s" % d.get("state", "N/A"))
    if v >= 12:
        print("  RANDOMX ATTIVO! (versione %s >= 12)" % v)
    else:
        print("  RandomX NON attivo (versione %s < 12)" % v)
except Exception as e:
    print("  Errore: %s" % e)
' 2>/dev/null || echo "  (RPC non ancora pronto)"

    echo ""
    echo "--- Log RandomX ---"
    grep -i "randomx\|rx_slow\|hard.fork\|version.*12\|version.*16" ${DATA_DIR}/mevacoind.log 2>/dev/null | tail -10 || echo "  (nessun log RandomX trovato ancora)"
else
    echo "Daemon non avviato!"
    echo "   Controlla: tail -50 ${DATA_DIR}/mevacoind.log"
fi

echo ""
echo "=========================================="
echo "  ${SEED_NAME} (${MY_IP}) - Riepilogo"
echo "=========================================="
echo "  Nodi collegati:"
echo "    - ${EXCLUSIVE_NODES[0]}:18080"
echo "    - ${EXCLUSIVE_NODES[1]}:18080"
echo ""
echo "  Comandi utili:"
echo "  tail -f ${DATA_DIR}/mevacoind.log"
echo "  ${DAEMON} status"
echo "=========================================="
