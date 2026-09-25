#!/usr/bin/env bash
#
# ============================================================
#  MevaCoin — deploy-node.sh       (v1.0 — ZERO-DOWNLOAD)
# ============================================================
#  Deployer per nodi aggiuntivi (server piccoli, pochi GB disco).
#
#  NON fa NESSUNA installazione:
#    ❌ no git clone        ❌ no apt-get install
#    ❌ no cmake/make       ❌ no toolchain minima
#    ❌ no build directory  ❌ no download dipendenze
#
#  Fa SOLO (come richiesto):
#    ✅ PRENDE il binario gia' compilato (mevacoind, ~18MB full monolite)
#    ✅ lo AVVIA come daemon con --add-peer verso i seed
#    ✅ crea la medesima unit systemd del nodo principale
#
#  Uso:
#    ./deploy-node.sh [percorso-binario-mevacoind]
#
#  Esempio:
#    ./deploy-node.sh /root/mevacoin/build/Linux/mevacoin/release/bin/mevacoind
#
#  Se omesso, il binario viene cercato automaticamente nella build locale.
#  Il binario puo' essere trasferito via scp/rsync da un nodo gia' pronto
#  (copia del file SOLO, ~18MB) — nessun clone, nessun rebuild.
#
#  NOTA P2P (rete non isolata):
#    Basta che il nodo raggiunga ALMENO un altro nodo (grafo connesso):
#    l'handshake scambia la peerlist e il gossip propaga il resto.
#    Non serve una maglia completa — un singolo --add-peer è sufficiente.
# ============================================================

set -euo pipefail

# ============================================================
# COLORI + COSTANTI (allineate a starts.sh)
# ============================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SEED1_IP="82.165.218.56"
SEED2_IP="87.106.40.193"
SEED3_IP="87.106.233.72"
P2P_PORT="18080"
RPC_PORT="18081"

SERVICE_NAME="mevacoind"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
DATA_DIR="/root/.mevacoin"

# ============================================================
# 1. INDIVIDUAZIONE BINARIO (SOLO FILE LOCALE — nessun download)
# ============================================================
echo ""
echo -e "${CYAN}>>> [1/5] Individuazione binario mevacoind...${NC}"

BUILD_DAEMON="/root/mevacoin/build/Linux/mevacoin/release/bin/mevacoind"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_DAEMON="${PWD}/mevacoind"
SCRIPT_DIR_DAEMON="${SCRIPT_DIR}/mevacoind"

DAEMON=""
if [ $# -ge 1 ] && [ -n "$1" ] && [ -f "$1" ]; then
    DAEMON="$1"
    echo -e "    ${GREEN}✅ Binario fornito esplicitamente:${NC} ${DAEMON}"
elif [ -f "${BUILD_DAEMON}" ]; then
    DAEMON="${BUILD_DAEMON}"
    echo -e "    ${GREEN}✅ Binario trovato nella build originale:${NC} ${DAEMON}"
elif [ -f "${LOCAL_DAEMON}" ]; then
    DAEMON="${LOCAL_DAEMON}"
    echo -e "    ${GREEN}✅ Binario trovato nella directory di avvio:${NC} ${DAEMON}"
elif [ -f "${SCRIPT_DIR_DAEMON}" ]; then
    DAEMON="${SCRIPT_DIR_DAEMON}"
    echo -e "    ${GREEN}✅ Binario trovato nella directory dello script:${NC} ${DAEMON}"
else
    echo -e "    ${RED}❌ Nessun mevacoind trovato (né argomento né build né directory di avvio).${NC}"
    echo "       Copialo su questa macchina via scp da un nodo gia' compilato:"
    echo "         scp root@<nodo-origine>:/root/mevacoin/build/Linux/mevacoin/release/bin/mevacoind ."
    exit 1
fi

# Garantisce il bit di esecuzione (copie via scp/sftp spesso lo perdono)
if [ ! -x "${DAEMON}" ]; then
    chmod +x "${DAEMON}"
    echo -e "    ${YELLOW}⚠️  Bit di esecuzione assente, impostato chmod +x${NC}"
fi

bin_size=$(du -h "${DAEMON}" | cut -f1)
echo -e "    Peso binario: ${bin_size} — zero download, zero rebuild"

# ============================================================
# 2. RILEVAMENTO IP PUBBLICO + RUOLO + PEER BOOTSTRAP
# ============================================================
echo ""
echo -e "${CYAN}>>> [2/5] Rilevamento IP pubblico e bootstrap peer...${NC}"

PUBLIC_IP=$(
    curl -s --max-time 10 https://api.ipify.org || \
    curl -s --max-time 10 https://ifconfig.me || \
    curl -s --max-time 10 https://icanhazip.com || \
    echo "UNKNOWN"
)
PUBLIC_IP="$(echo "${PUBLIC_IP}" | tr -d ' \n\r\t')"

if [ "${PUBLIC_IP}" = "UNKNOWN" ] || [ -z "${PUBLIC_IP}" ]; then
    echo -e "    ${RED}❌ Impossibile rilevare l'IP pubblico.${NC}"
    exit 1
fi
echo -e "    IP rilevato: ${GREEN}${PUBLIC_IP}${NC}"

SERVER_NAME=""
PEER_OPTS=""
case "${PUBLIC_IP}" in
    "${SEED1_IP}")
        SERVER_NAME="seed1"
        PEER_OPTS="--add-peer ${SEED2_IP}:${P2P_PORT} --add-peer ${SEED3_IP}:${P2P_PORT}"
        ;;
    "${SEED2_IP}")
        SERVER_NAME="seed2"
        PEER_OPTS="--add-peer ${SEED1_IP}:${P2P_PORT} --add-peer ${SEED3_IP}:${P2P_PORT}"
        ;;
    "${SEED3_IP}")
        SERVER_NAME="seed3"
        PEER_OPTS="--add-peer ${SEED1_IP}:${P2P_PORT} --add-peer ${SEED2_IP}:${P2P_PORT}"
        ;;
    *)
        SERVER_NAME="node"
        PEER_OPTS="--seed-node ${SEED1_IP}:${P2P_PORT} --seed-node ${SEED2_IP}:${P2P_PORT} --seed-node ${SEED3_IP}:${P2P_PORT}"
        echo -e "    ${YELLOW}ℹ️  IP non compreso tra i seed: ruolo 'nodo di rete'.${NC}"
        echo "       Bootstrap: fa seed-node verso i 3 seed (scaricare la peerlist, poi si scollega)."
        echo "       La discovery (gossip) + peerlist persistita su disco troveranno e ricorderanno il resto."
        ;;
esac

echo -e "    Ruolo:      ${GREEN}${SERVER_NAME}${NC}"
echo -e "    Peer:       ${PEER_OPTS}"

# ============================================================
# 3. ARRESTO EVENTUALI DAEMON ESISTENTI
# ============================================================
echo ""
echo -e "${CYAN}>>> [3/5] Fermo eventuali istanze esistenti...${NC}"

pkill -f "mevacoind" 2>/dev/null || true
if systemctl list-unit-files --type=service 2>/dev/null | grep -q "^${SERVICE_NAME}\.service"; then
    systemctl stop "${SERVICE_NAME}" 2>/dev/null || true
    systemctl disable "${SERVICE_NAME}" 2>/dev/null || true
fi
if [ -f "${SERVICE_FILE}" ]; then
    rm -f "${SERVICE_FILE}"
    systemctl daemon-reload 2>/dev/null || true
fi
echo -e "    ${GREEN}✅ Eventuali istanze fermate.${NC}"

# ============================================================
# 4. CREAZIONE UNIT SYSTEMD (identica al nodo principale)
# ============================================================
echo ""
echo -e "${CYAN}>>> [4/5] Creazione unit systemd...${NC}"

cat > "${SERVICE_FILE}" <<EOF
[Unit]
Description=MevaCoin Daemon (${SERVER_NAME} - ${PUBLIC_IP} - full)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=root
WorkingDirectory=$(dirname "${DAEMON}")
ExecStart=${DAEMON} \
    --non-interactive \
    --data-dir ${DATA_DIR} \
    --p2p-bind-ip 0.0.0.0 \
    --p2p-bind-port ${P2P_PORT} \
    --rpc-bind-ip 0.0.0.0 \
    --rpc-bind-port ${RPC_PORT} \
    --confirm-external-bind \
    ${PEER_OPTS} \
    --log-level 2

Restart=always
RestartSec=10
TimeoutStopSec=60
LimitNOFILE=65535
StandardOutput=journal
StandardError=journal
KillMode=control-group

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable "${SERVICE_NAME}"
echo -e "    ${GREEN}✅ Unit creata in ${SERVICE_FILE}${NC}"

# ============================================================
# 5. AVVIO + VERIFICA
# ============================================================
echo ""
echo -e "${CYAN}>>> [5/5] Avvio del daemon...${NC}"

systemctl restart "${SERVICE_NAME}"
echo "    Attendo avvio (10s)..."
sleep 10

echo ""
echo -e "${CYAN}=============================================="
echo "  VERIFICA STATO DAEMON — ${SERVER_NAME}"
echo -e "==============================================${NC}"

if systemctl is-active --quiet "${SERVICE_NAME}"; then
    echo -e "    ${GREEN}✅ Daemon attivo${NC}"
    echo "    PID:   $(systemctl show -p MainPID --value ${SERVICE_NAME})"
    echo "    Stato: $(systemctl is-active ${SERVICE_NAME})"
    echo ""
    echo -e "${CYAN}>>> Verifica RPC...${NC}"

    if info=$(curl -s --max-time 6 "http://127.0.0.1:${RPC_PORT}/get_info"); then
        height=$(echo "${info}" | python3 -c 'import sys,json; print(json.load(sys.stdin)["height"])' 2>/dev/null || echo "N/A")
        echo -e "    ${GREEN}✅ RPC funzionante — altezza: ${height}${NC}"
        echo -e "    ${GREEN}✅ Stato: READY — P2P: ${PUBLIC_IP}:${P2P_PORT}${NC}"
    else
        echo -e "    ${YELLOW}⚠️ Daemon attivo ma RPC non ancora pronto (sync iniziale)...${NC}"
    fi
else
    echo -e "    ${RED}❌ Il servizio non è partito. Log:${NC}"
    journalctl -u "${SERVICE_NAME}" --no-pager -n 20 2>/dev/null | tail -20
    exit 1
fi
