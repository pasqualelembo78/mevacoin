#!/usr/bin/env bash
# Deploy mevacoind ai seed remote (seed2/seed3).
# Uso:  tools/deploy.sh [host ...]        (default: SEED2_HOST SEED3_HOST)
#        tools/deploy.sh --local          (solo verifica binario locale, niente remote)
# Credenziali: /root/mevacoin/deploy.env (chmod 600, gitignored) — mai commitare.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
ENV_FILE="$REPO_DIR/deploy.env"
[[ -f "$ENV_FILE" ]] || { echo "FATAL: manca $ENV_FILE" >&2; exit 1; }
# shellcheck disable=SC1090
set -a; source "$ENV_FILE"; set +a

SSH_USER="${SSH_USER:-root}"
SSH_PORT="${SSH_PORT:-22}"
SERVICE="${SERVICE:-mevacoind}"
BIN_PATH="${BIN_PATH:-/root/mevacoin/mevacoind}"
RPC_PORT="${RPC_PORT:-18081}"
LOCAL_BIN="$REPO_DIR/build/Linux/mevacoin/release/bin/mevacoind"

if [[ "${1:-}" == "--local" ]]; then
  echo "== Verifica binario locale"
  md5sum "$LOCAL_BIN" "$REPO_DIR/mevacoind"
  exit 0
fi

HOSTS=("$@")
if [[ ${#HOSTS[@]} -eq 0 ]]; then
  HOSTS=("$SEED2_HOST" "$SEED3_HOST")
fi

[[ -x "$LOCAL_BIN" ]] || { echo "FATAL: manca binario $LOCAL_BIN" >&2; exit 1; }
if [[ ! -f "$REPO_DIR/mevacoind" ]] || ! cmp -s "$LOCAL_BIN" "$REPO_DIR/mevacoind"; then
  echo "== Copia binario build -> $REPO_DIR/mevacoind"
  cp -f "$LOCAL_BIN" "$REPO_DIR/mevacoind"
fi
SRC_BIN="$REPO_DIR/mevacoind"
LOCAL_MD5="$(md5sum "$SRC_BIN" | awk '{print $1}')"
echo "== md5 locale: $LOCAL_MD5"

SSH_OPTS=(-o StrictHostKeyChecking=no -o ConnectTimeout=10 -p "$SSH_PORT")
command -v sshpass >/dev/null || { echo "FATAL: manca sshpass" >&2; exit 1; }

rssh() {
  local host="$1"; shift
  sshpass -p "$SSH_PASS" ssh "${SSH_OPTS[@]}" "$SSH_USER@$host" "$@"
}

remote_height() {
  rssh "$1" "curl -s --max-time 5 http://127.0.0.1:$RPC_PORT/get_info \
    | python3 -c 'import json,sys;d=json.load(sys.stdin);print(d[\"height\"])'" 2>/dev/null || echo ""
}

deploy_one() {
  local host="$1"
  echo "================ $host ================"
  echo "== Pre-upload height: $(remote_height "$host")"

  echo "== Upload binario (tmp)"
  sshpass -p "$SSH_PASS" ssh "${SSH_OPTS[@]}" "$SSH_USER@$host" "cat > $BIN_PATH.new" < "$SRC_BIN" \
    || { echo "FAIL[$host]: upload"; return 1; }

  echo "== md5 remoto (tmp): $(rssh "$host" "md5sum $BIN_PATH.new | cut -d' ' -f1")"
  local rmd5
  rmd5="$(rssh "$host" "md5sum $BIN_PATH.new | cut -d' ' -f1")"
  if [[ "$rmd5" != "$LOCAL_MD5" ]]; then
    echo "FAIL[$host]: md5 mismatch ($rmd5 != $LOCAL_MD5)"; rssh "$host" "rm -f $BIN_PATH.new"; return 1
  fi

  echo "== Stop $SERVICE (TimeoutStopSec 60)"
  rssh "$host" "systemctl stop $SERVICE && sleep 2"

  echo "== Backup $BIN_PATH -> $BIN_PATH.prev"
  rssh "$host" "cp -f $BIN_PATH $BIN_PATH.prev 2>/dev/null || true; mv -f $BIN_PATH.new $BIN_PATH && chmod +x $BIN_PATH"

  echo "== Start $SERVICE"
  rssh "$host" "systemctl start $SERVICE"

  echo "== Verifica (boot + height + stabilita)"
  local ok=0
  for i in $(seq 1 30); do
    sleep 2
    local h state rests
    h="$(remote_height "$host")"
    state="$(rssh "$host" "systemctl is-active $SERVICE" 2>/dev/null || echo dead)"
    rests="$(rssh "$host" "systemctl show $SERVICE -p NRestarts --value" 2>/dev/null || echo '?')"
    echo "   t=$((i*2))s state=$state NRestarts=$rests height=${h:-?}"
    if [[ "$state" == "active" && -n "$h" && "$h" =~ ^[0-9]+$ && "$rests" == "0" ]]; then
      ok=1; break
    fi
    if [[ "$state" != "active" ]]; then break; fi
  done

  if [[ "$ok" == "1" ]]; then
    echo "OK[$host]: deployed md5=$LOCAL_MD5 height=$(remote_height "$host")"
    return 0
  fi

  echo "FAIL[$host]: rollback a $BIN_PATH.prev"
  rssh "$host" "systemctl stop $SERVICE || true
    if [ -f $BIN_PATH.prev ]; then cp -f $BIN_PATH.prev $BIN_PATH && chmod +x $BIN_PATH; fi
    rm -f $BIN_PATH.new
    systemctl start $SERVICE || true"
  echo "ROLLBACK[$host]: eseguito"
  return 1
}

rc=0
for h in "${HOSTS[@]}"; do
  deploy_one "$h" || rc=1
done
exit $rc
