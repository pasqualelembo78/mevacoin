#!/bin/bash
# ============================================
# MevaCoin Daemon — Installazione Pulita
# Auto-rileva il server tramite IP pubblico
# Installa dipendenze e compila da sorgente
# Termina TUTTE le istanze esistenti
# Chiede conferma prima di eliminare la chain
# ============================================
set -euo pipefail

# ============================================
# CONFIGURAZIONE SEED NODES
# ============================================
SEED1_IP="82.165.218.56"
SEED2_IP="87.106.40.193"
SEED3_IP="87.106.233.72"
P2P_PORT="18080"
RPC_PORT="18081"

BUILD_VARIANT="full"
BUILD_DIR="Linux/mevacoin"
DAEMON="/root/mevacoin/build/${BUILD_DIR}/release/bin/mevacoind"
SERVICE_NAME="mevacoind"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
MEVA_SRC_DIR="/root/mevacoin"

DO_BUILD=false
CLEAN_CHAIN=false
SERVER_NAME=""
PEER_OPTS=""

# ============================================
# COLORI
# ============================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo ""
echo -e "${CYAN}=============================================="
echo "  MevaCoin — Installazione Pulita"
echo -e "==============================================${NC}"
echo ""

# ============================================
# STEP 1 — RILEVAMENTO IP PUBBLICO
# ============================================
echo -e "${CYAN}>>> [1/8] Rilevamento IP pubblico...${NC}"
PUBLIC_IP=$(
    curl -s --max-time 10 https://api.ipify.org || \
    curl -s --max-time 10 https://ifconfig.me || \
    curl -s --max-time 10 https://icanhazip.com || \
    echo "UNKNOWN"
)
PUBLIC_IP="$(echo "$PUBLIC_IP" | tr -d ' \n\r\t')"

if [ "$PUBLIC_IP" = "UNKNOWN" ] || [ -z "$PUBLIC_IP" ]; then
    echo -e "${RED}❌ Impossibile rilevare l'IP pubblico. Controlla la connessione.${NC}"
    exit 1
fi

echo -e "    IP rilevato: ${GREEN}${PUBLIC_IP}${NC}"

# ============================================
# STEP 2 — SELEZIONE NODO IN BASE ALL'IP
# ============================================
echo ""
echo -e "${CYAN}>>> [2/8] Identificazione server...${NC}"

case "$PUBLIC_IP" in
    "$SEED1_IP")
        SERVER_NAME="seed1"
        PEER_OPTS="--add-peer ${SEED2_IP}:${P2P_PORT} --add-peer ${SEED3_IP}:${P2P_PORT}"
        ;;
    "$SEED2_IP")
        SERVER_NAME="seed2"
        PEER_OPTS="--add-peer ${SEED1_IP}:${P2P_PORT} --add-peer ${SEED3_IP}:${P2P_PORT}"
        ;;
    "$SEED3_IP")
        SERVER_NAME="seed3"
        PEER_OPTS="--add-peer ${SEED1_IP}:${P2P_PORT} --add-peer ${SEED2_IP}:${P2P_PORT}"
        ;;
    *)
        echo -e "${RED}❌ IP ${PUBLIC_IP} non riconosciuto come seed node.${NC}"
        echo "   Seed noti: ${SEED1_IP} | ${SEED2_IP} | ${SEED3_IP}"
        exit 1
        ;;
esac

echo -e "    Server: ${GREEN}${SERVER_NAME}${NC} (${PUBLIC_IP})"
echo "    Peer bootstrap: ${PEER_OPTS}"

# ============================================
# STEP 2.5 — SELEZIONE VARIANTE BUILD
# ============================================
echo ""
echo -e "${CYAN}>>> [2.5/8] Selezione variante di compilazione...${NC}"
echo ""
echo -e "    Scegli la versione di MevaCoin da installare:"
echo -e "    ${YELLOW}1) Ultra-Lite${NC}  — 5-10GB disco, 512MB RAM, 1 core CPU"
echo -e "       Sincronizzazione: 24-48 ore, 2 connessioni P2P"
echo -e "    ${YELLOW}2) Lite${NC}        — 15-30GB disco, 1GB RAM, 1-2 core CPU"
echo -e "       Sincronizzazione: 10-20 ore, 4 connessioni P2P"
echo -e "    ${GREEN}3) Completa${NC}    — 50-100GB disco, 2GB+ RAM, 2+ core CPU"
echo -e "       Sincronizzazione: 2-4 ore, 12+ connessioni P2P"
echo ""
echo -n "    Scelta [1-3] (default: 3): "
read -r VARIANT_CHOICE

case "$VARIANT_CHOICE" in
    1)
        BUILD_VARIANT="ultra-lite"
        echo -e "    ${YELLOW}✅ Variante Ultra-Lite selezionata${NC}"
        ;;
    2)
        BUILD_VARIANT="lite"
        echo -e "    ${YELLOW}✅ Variante Lite selezionata${NC}"
        ;;
    3|"")
        BUILD_VARIANT="full"
        echo -e "    ${GREEN}✅ Variante Completa selezionata${NC}"
        ;;
    *)
        echo -e "    ${RED}❌ Scelta non valida. Uso variante Completa.${NC}"
        BUILD_VARIANT="full"
        ;;
esac

case "${BUILD_VARIANT}" in
    ultra-lite)
        DAEMON="/root/mevacoin/build/${BUILD_DIR}/ultra-lite/release/bin/mevacoind"
        ;;
    lite)
        DAEMON="/root/mevacoin/build/${BUILD_DIR}/lite/release/bin/mevacoind"
        ;;
    *)
        DAEMON="/root/mevacoin/build/${BUILD_DIR}/release/bin/mevacoind"
        ;;
esac

# ============================================
# STEP 3 — DIPENDENZE
# ============================================
echo ""
echo -e "${CYAN}>>> [3/8] Installazione dipendenze di compilazione...${NC}"

apt-get update -qq
apt-get install -y \
    build-essential cmake pkg-config libssl-dev libzmq3-dev libsodium-dev \
    libunwind8-dev liblzma-dev libreadline-dev libexpat1-dev \
    qttools5-dev-tools libhidapi-dev libusb-1.0-0-dev \
    libprotobuf-dev protobuf-compiler libudev-dev \
    libunbound-dev libboost-all-dev ccache curl

echo -e "    ${GREEN}✅ Tutte le dipendenze installate.${NC}"

# ============================================
# STEP 4 — GESTIONE SORGENTI
# ============================================
echo ""
echo -e "${CYAN}>>> [4/8] Verifica cartella sorgente mevacoin/...${NC}"

if [ -d "${MEVA_SRC_DIR}" ]; then
    src_size=$(du -sh "${MEVA_SRC_DIR}" 2>/dev/null | cut -f1 || echo "N/A")
    echo ""
    echo -e "${YELLOW}    ⚠️  La cartella ${MEVA_SRC_DIR} esiste già (${src_size}).${NC}"
    echo -e "${YELLOW}       Eliminandola verrà eseguito un clone e una ricompilazione completi.${NC}"
    echo ""
    echo -n "    Vuoi ELIMINARE ${MEVA_SRC_DIR} e ricompilare da zero? [s/N]: "
    read -r SRC_CHOICE

    case "$SRC_CHOICE" in
        [sS]|[sS][iI])
            echo "    Rimuovo ${MEVA_SRC_DIR}..."
            rm -rf "${MEVA_SRC_DIR}"
            echo -e "    ${GREEN}✅ Cartella rimossa.${NC}"
            DO_BUILD=true
            ;;
        *)
            echo ""
            echo "    La cartella viene mantenuta. Procedo con la compilazione nella cartella esistente."
            DO_BUILD=true
            ;;
    esac
else
    echo "    Cartella ${MEVA_SRC_DIR} non trovata. Procedo con clone e compilazione."
    DO_BUILD=true
fi

# ============================================
# STEP 5 — CLONE E COMPILAZIONE
# ============================================
echo ""
echo -e "${CYAN}>>> [5/8] Clone e compilazione sorgenti...${NC}"

if [ "$DO_BUILD" = true ]; then
    cd /root

    if [ ! -d "${MEVA_SRC_DIR}" ]; then
        echo "    git clone --recursive https://github.com/pasqualelembo78/mevacoin"
        git clone --recursive https://github.com/pasqualelembo78/mevacoin
    fi

    echo "    Entro in ${MEVA_SRC_DIR}..."
    cd "${MEVA_SRC_DIR}"

    echo ""
    echo -e "    ${YELLOW}Avvio compilazione variante '${BUILD_VARIANT}'...${NC}"
    echo -e "    ${YELLOW}(Questo passaggio può richiedere diversi minuti)${NC}"
    echo ""

    if ! make release-${BUILD_VARIANT} 2>&1 | tee /tmp/mevacoin_build.log; then
        echo ""
        echo -e "${RED}❌ Compilazione fallita.${NC}"
        echo "   Controlla il log: /tmp/mevacoin_build.log"
        tail -20 /tmp/mevacoin_build.log || true
        exit 1
    fi

    if [ ! -f "${DAEMON}" ]; then
        echo ""
        echo -e "${RED}❌ Compilazione fallita: binario non trovato in ${DAEMON}${NC}"
        echo "   Controlla il log: /tmp/mevacoin_build.log"
        tail -20 /tmp/mevacoin_build.log || true
        exit 1
    fi

    echo ""
    echo -e "    ${GREEN}✅ Compilazione completata. Binario: ${DAEMON}${NC}"
    DAEMON_VERSION=$("${DAEMON}" --version 2>/dev/null | head -1 || echo "N/A")
    echo "    Versione: ${DAEMON_VERSION}"
else
    echo "    Compilazione saltata (binario già presente)."
fi

# ============================================
# STEP 6 — TERMINAZIONE COMPLETA DI TUTTE LE ISTANZE
# ============================================
echo ""
echo -e "${CYAN}>>> [6/8] Terminazione di tutte le istanze mevacoind...${NC}"

if systemctl list-unit-files --type=service 2>/dev/null | grep -q "^${SERVICE_NAME}\.service"; then
    echo "    Trovato servizio systemd: ${SERVICE_NAME}"
    if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
        echo "    Fermo il servizio systemd..."
        systemctl stop "${SERVICE_NAME}" 2>/dev/null || true
        sleep 3
    fi
    echo "    Disabilito il servizio systemd..."
    systemctl disable "${SERVICE_NAME}" 2>/dev/null || true
else
    echo "    Nessun servizio systemd '${SERVICE_NAME}' trovato."
fi

if [ -f "${SERVICE_FILE}" ]; then
    echo "    Rimuovo ${SERVICE_FILE}..."
    rm -f "${SERVICE_FILE}"
    systemctl daemon-reload 2>/dev/null || true
fi

FOUND_PIDS=$(pgrep -f "mevacoind" 2>/dev/null || true)
if [ -n "$FOUND_PIDS" ]; then
    echo "    Trovati processi mevacoind attivi (PID: ${FOUND_PIDS}) — terminazione..."
    echo "$FOUND_PIDS" | xargs kill -SIGTERM 2>/dev/null || true
    sleep 5
    STILL_ALIVE=$(pgrep -f "mevacoind" 2>/dev/null || true)
    if [ -n "$STILL_ALIVE" ]; then
        echo "    Processo ancora attivo — forzo la terminazione (SIGKILL)..."
        echo "$STILL_ALIVE" | xargs kill -SIGKILL 2>/dev/null || true
        sleep 2
    fi
else
    echo "    Nessun processo mevacoind attivo trovato."
fi

FINAL_CHECK=$(pgrep -f "mevacoind" 2>/dev/null || true)
if [ -n "$FINAL_CHECK" ]; then
    echo -e "${RED}    ❌ Impossibile terminare tutti i processi. PID rimasti: ${FINAL_CHECK}${NC}"
    echo "       Prova manualmente: kill -9 ${FINAL_CHECK}"
    exit 1
fi
echo -e "    ${GREEN}✅ Nessun processo mevacoind in esecuzione.${NC}"

# ============================================
# STEP 7 — RILEVAMENTO DIRECTORY DATI CHAIN
# ============================================
echo ""
echo -e "${CYAN}>>> [7/8] Ricerca dati blockchain esistenti...${NC}"

KNOWN_PATHS=(
    "/root/.mevacoin"
    "/root/.Mevacoin"
)

CANDIDATE_DIRS=()
for dir in "${KNOWN_PATHS[@]}"; do
    if [ -d "$dir" ]; then
        CANDIDATE_DIRS+=("$dir")
    fi
done

if [ ${#CANDIDATE_DIRS[@]} -eq 0 ]; then
    echo "    Nessuna directory dati esistente trovata. Procedo con installazione pulita."
    DATA_DIR="/root/.mevacoin"
    CLEAN_CHAIN=false
else
    echo ""
    echo -e "${YELLOW}    ⚠️  Trovate le seguenti directory con dati blockchain:${NC}"
    echo ""
    for i in "${!CANDIDATE_DIRS[@]}"; do
        dir="${CANDIDATE_DIRS[$i]}"
        dir_size=$(du -sh "$dir" 2>/dev/null | cut -f1 || echo "N/A")
        echo "      [$((i+1))] ${dir}  (${dir_size})"
    done

    echo ""
    echo -e "${YELLOW}    ⚠️  ATTENZIONE: eliminare questi dati significa che la chain${NC}"
    echo -e "${YELLOW}       ripartirà da ZERO (riscaricamento completo della blockchain).${NC}"
    echo ""
    echo -n "    Vuoi ELIMINARE tutte le directory elencate? [s/N]: "
    read -r USER_CHOICE

    case "$USER_CHOICE" in
        [sS]|[sS][iI])
            echo ""
            echo "    Eliminazione in corso..."
            for dir in "${CANDIDATE_DIRS[@]}"; do
                echo "    Rimuovo: ${dir}"
                rm -rf "$dir"
            done
            echo -e "    ${GREEN}✅ Directory dati eliminate.${NC}"
            CLEAN_CHAIN=true
            ;;
        *)
            echo ""
            echo "    Mantengo i dati esistenti. Il daemon riprenderà dalla chain attuale."
            CLEAN_CHAIN=false
            ;;
    esac

    if [ "$CLEAN_CHAIN" = true ]; then
        DATA_DIR="/root/.mevacoin"
    else
        DATA_DIR="${CANDIDATE_DIRS[0]}"
        echo "    Directory dati attiva: ${DATA_DIR}"
    fi
fi

# ============================================
# STEP 8 — SYSTEMD E AVVIO
# ============================================
echo ""
echo -e "${CYAN}>>> [8/8] Configurazione servizio systemd e avvio...${NC}"

mkdir -p "${DATA_DIR}"
echo "    Directory dati: ${DATA_DIR}"

# ============================================
# PARAMETRI SPECIFICI PER VARIANTE
# ============================================
VARIANT_OPTS=""
case "${BUILD_VARIANT}" in
    ultra-lite)
        VARIANT_OPTS="--prune-blockchain --max-txpool-weight 50000000 --block-sync-size 5 --batch-max-weight 5 --mining-threads 1 --rpc-max-connections 10"
        echo -e "    ${YELLOW}Configurazione Ultra-Lite: pruning attivo, risorse minimizzate${NC}"
        ;;
    lite)
        VARIANT_OPTS="--prune-blockchain --max-txpool-weight 100000000 --block-sync-size 10 --batch-max-weight 8 --mining-threads 2 --rpc-max-connections 20"
        echo -e "    ${YELLOW}Configurazione Lite: pruning attivo, risorse ridotte${NC}"
        ;;
    *)
        VARIANT_OPTS="--max-txpool-weight 648000000 --rpc-max-connections 100"
        echo -e "    ${GREEN}Configurazione Completa: tutte le funzionalità${NC}"
        ;;
esac

cat > "${SERVICE_FILE}" <<EOF
[Unit]
Description=MevaCoin Daemon (${SERVER_NAME} - ${PUBLIC_IP} - ${BUILD_VARIANT})
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
    ${VARIANT_OPTS} \
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

echo "    Ricarico configurazione systemd..."
systemctl daemon-reload

echo "    Abilito il servizio all'avvio..."
systemctl enable "${SERVICE_NAME}"

echo ""
echo "    Avvio il servizio..."
systemctl restart "${SERVICE_NAME}"

echo "    Attendo avvio (10s)..."
sleep 10

echo ""
echo -e "${CYAN}=========================================="
echo "  VERIFICA STATO DAEMON — ${SERVER_NAME}"
echo -e "==========================================${NC}"

if systemctl is-active --quiet "${SERVICE_NAME}"; then
    echo -e "  ${GREEN}✅ Servizio systemd attivo${NC}"
    echo "     PID:     $(systemctl show -p MainPID --value ${SERVICE_NAME})"
    echo "     Stato:   $(systemctl is-active ${SERVICE_NAME})"
    echo "     Variante: ${BUILD_VARIANT}"
    if [ "$CLEAN_CHAIN" = true ]; then
        echo -e "     ${YELLOW}Chain partita da zero (dati precedenti eliminati).${NC}"
    else
        echo "     Chain: dati esistenti mantenuti."
    fi
    echo ""

    echo "--- Info Generale ---"
    curl -s http://127.0.0.1:${RPC_PORT}/get_info | \
        python3 -c '
import sys, json
try:
    d = json.load(sys.stdin)
    print("  Altezza:  %s" % d.get("height", "N/A"))
    print("  TopHash:  %s..." % str(d.get("top_block_hash", "N/A"))[:16])
    print("  Peers:    %s" % (d.get("incoming_connections_count", 0) + d.get("outgoing_connections_count", 0)))
    print("  Synced:   %s" % d.get("synchronized", "N/A"))
except Exception as e:
    print("  Errore lettura RPC: %s" % e)
' || echo "  (RPC non ancora pronto — attendi qualche secondo)"

    echo ""

    echo "--- Verifica Hard Fork ---"
    curl -s http://127.0.0.1:${RPC_PORT}/hard_fork_info | \
        python3 -c '
import sys, json
try:
    d = json.load(sys.stdin)
    print("  Versione HF attiva: %s" % d.get("version", "N/A"))
    print("  Enabled:            %s" % d.get("enabled", "N/A"))
    print("  State:              %s" % d.get("state", "N/A"))
except Exception as e:
    print("  Errore: %s" % e)
' || echo "  (RPC non ancora pronto)"

    echo ""

    echo "--- Log RandomX ---"
    grep -i "randomx\|rx_slow\|hard.fork\|version.*12\|version.*16" \
        "${DATA_DIR}/mevacoind.log" 2>/dev/null | tail -10 \
        || echo "  (nessun log RandomX trovato ancora — normale in fase di avvio)"
else
    echo -e "  ${RED}❌ Servizio non avviato!${NC}"
    echo ""
    echo "  Diagnostica systemd:"
    systemctl status "${SERVICE_NAME}" --no-pager -l 2>/dev/null | tail -20
    echo ""
    echo "  Log daemon:"
    tail -30 "${DATA_DIR}/mevacoind.log" 2>/dev/null || echo "  (log non trovato)"
fi

echo ""
echo -e "${CYAN}=========================================="
echo "  Comandi utili:"
echo "  systemctl status ${SERVICE_NAME}"
echo "  systemctl restart ${SERVICE_NAME}"
echo "  systemctl stop ${SERVICE_NAME}"
echo "  journalctl -u ${SERVICE_NAME} -f"
echo "  curl -s http://127.0.0.1:${RPC_PORT}/get_info | python3 -m json.tool"
echo -e "==========================================${NC}"
echo ""