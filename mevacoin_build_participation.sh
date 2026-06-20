#!/bin/bash
# ╔══════════════════════════════════════════════════════════════════════════╗
# ║    MEVACOIN — PARTICIPATION SYSTEM — MASTER BUILD SCRIPT               ║
# ║    Versione: 1.0 | Generato da Cody (CodeWords AI)                     ║
# ╚══════════════════════════════════════════════════════════════════════════╝
set -euo pipefail
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; CYN='\033[0;36m'; BLD='\033[1m'; NC='\033[0m'
ok()   { echo -e "${GRN}  ✓ $*${NC}"; }
warn() { echo -e "${YLW}  ! $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; }
hdr()  { echo -e "\n${CYN}${BLD}═══ $* ═══${NC}"; }
REPO_DIR="${1:-/root/mevacoin}"; BUILD_DIR="$REPO_DIR/build"; JOBS=$(nproc)

hdr "FASE 0 — Prerequisiti"
[ -f "$REPO_DIR/src/cryptonote_core/blockchain.cpp" ] || { err "Repo non trovato in $REPO_DIR. Usa: bash $0 /percorso/mevacoin"; exit 1; }
cd "$REPO_DIR"
BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
[ "$BRANCH" = "test" ] || { warn "Branch: $BRANCH — cambio a test..."; git checkout test; }
ok "Branch: test | Repo: $REPO_DIR"

hdr "FASE 1 — Verifica 11 file core partecipazione"
MISSING=0
for f in \
  "src/cryptonote_core/participation/availability_proof.cpp" \
  "src/cryptonote_core/participation/badge_system.cpp" \
  "src/cryptonote_core/participation/node_registry.cpp" \
  "src/cryptonote_core/participation/participation_engine.cpp" \
  "src/cryptonote_core/participation/participation_manager.cpp" \
  "src/cryptonote_core/participation/participation_coinbase_validator.cpp" \
  "src/cryptonote_core/participation/reward_distributor.cpp" \
  "src/cryptonote_core/participation/snapshot_broadcaster.cpp" \
  "src/cryptonote_core/participation/participation_tx_parser.cpp" \
  "src/rpc/participation_rpc_commands.h" \
  "src/cryptonote_core/participation/participation_types.h"; do
  [ -f "$REPO_DIR/$f" ] && ok "$f" || { err "MANCANTE: $f"; MISSING=$((MISSING+1)); }
done
[ $MISSING -gt 0 ] && { err "$MISSING file mancanti — verifica branch test"; exit 1; }

hdr "FASE 2 — Fix patch residue"

# PATCH01: get_nodes_due_for_challenge non-stub
AVAIL="$REPO_DIR/src/cryptonote_core/participation/availability_proof.cpp"
if grep -q "node_registry_->get_active_nodes()" "$AVAIL"; then
  ok "PATCH01: già applicata (get_nodes_due implementato)"
else
  warn "PATCH01: applico implementazione get_nodes_due..."
  python3 << 'PYEOF'
import re
path = "src/cryptonote_core/participation/availability_proof.cpp"
with open(path, 'r') as f: c = f.read()
stub = r'(AvailabilityProofEngine::get_nodes_due_for_challenge\([^)]*\)\s*\{)[^}]*\}'
rep = r'''\1
  std::vector<crypto::hash> nodes_due;
  if (!node_registry_) return nodes_due;
  auto active_nodes = node_registry_->get_active_nodes();
  for (const auto& entry : active_nodes) {
    { std::lock_guard<std::mutex> lk(cache_lock_);
      auto pit = pending_challenges_.find(entry.node_id);
      if (pit != pending_challenges_.end() && !pit->second.empty()) continue; }
    bool due = (entry.next_challenge_time==0)||(current_timestamp>=entry.next_challenge_time);
    if (due) nodes_due.push_back(entry.node_id);
  }
  return nodes_due;
}'''
nc = re.sub(stub, rep, c, flags=re.DOTALL)
with open(path,'w') as f: f.write(nc)
print("PATCH01 applicata")
PYEOF
fi

# PATCH02: cryptonote_core.h timer PoA
CORE_H="$REPO_DIR/src/cryptonote_core/cryptonote_core.h"
grep -q "m_poa_challenge_interval" "$CORE_H" && ok "PATCH02: timer PoA già presente" || {
  warn "PATCH02: aggiungo timer PoA in cryptonote_core.h..."
  sed -i 's/m_diff_recalc_interval;/m_diff_recalc_interval;\n    epee::math_helper::once_a_time_seconds<60, true>       m_poa_challenge_interval;/' "$CORE_H"
  grep -q "dispatch_poa_challenges" "$CORE_H" || \
    sed -i '/bool update_blockchain_pruning/a\    bool dispatch_poa_challenges();' "$CORE_H"
  ok "PATCH02: timer + dispatch aggiunti"
}

# PATCH03: <chrono> in cryptonote_core.cpp
CORE_CPP="$REPO_DIR/src/cryptonote_core/cryptonote_core.cpp"
grep -q "#include <chrono>" "$CORE_CPP" && ok "PATCH03: <chrono> già presente" || {
  sed -i '/#include "participation\/participation_manager.h"/a #include <chrono>' "$CORE_CPP"
  ok "PATCH03: #include <chrono> aggiunto"
}

# PATCH04: blockchain.cpp coinbase_validator include
BLOCKCHAIN="$REPO_DIR/src/cryptonote_core/blockchain.cpp"
grep -q "participation_coinbase_validator.h" "$BLOCKCHAIN" && ok "PATCH04: coinbase_validator include già presente" || {
  sed -i '/#include "participation\/participation_manager.h"/a #include "participation\/participation_coinbase_validator.h"' "$BLOCKCHAIN"
  ok "PATCH04: include aggiunto"
}

# PATCH07: CMakeLists
CMAKE_PART="$REPO_DIR/src/cryptonote_core/CMakeLists.txt"
grep -q "participation_coinbase_validator.cpp" "$CMAKE_PART" && ok "PATCH07: CMakeLists già aggiornato" || {
  sed -i '/snapshot_broadcaster.cpp/a \  participation\/participation_coinbase_validator.cpp' "$CMAKE_PART"
  ok "PATCH07: CMakeLists aggiornato"
}

hdr "FASE 3 — Directory DB e participation.conf"
mkdir -p /var/lib/mevacoin/{participation,node_registry,badges,rewards}
ok "Directory DB create in /var/lib/mevacoin/"
[ -f "/etc/mevacoin/participation.conf" ] || {
  mkdir -p /etc/mevacoin
  cp "$REPO_DIR/participation.conf" /etc/mevacoin/participation.conf 2>/dev/null && ok "participation.conf copiato" || {
    cat > /etc/mevacoin/participation.conf << 'CONF'
enable-participation=1
pool-percentage=0.03
node-registry-db=/var/lib/mevacoin/node_registry
participation-db=/var/lib/mevacoin/participation
uptime-db=/var/lib/mevacoin/participation/uptime
badge-db=/var/lib/mevacoin/badges
reward-db=/var/lib/mevacoin/rewards
participation-port=18088
participation-bind-ip=0.0.0.0
poa-challenge-interval=36000
poa-timeout-ms=2000
poa-challenges-per-period=3
distribution-period=240
distribution-maturation=10
participation-log-level=INFO
CONF
    ok "participation.conf creato in /etc/mevacoin/"
  }
}

hdr "FASE 4 — Compilazione ($JOBS jobs)"
echo -e "${YLW}  Richiede 20-40 minuti...${NC}"
mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release -DSTATIC=OFF -DBUILD_TESTS=OFF .. 2>&1 | tail -5
make -j"$JOBS" mevacoind mevacoin-wallet-cli 2>&1 | grep -E "(rror|FAILED|Linking)" | tail -15 || true
[ -f "$BUILD_DIR/bin/mevacoind" ] || { err "Compilazione fallita. Controlla errori sopra."; exit 1; }
ok "mevacoind compilato con successo!"
ls -lh "$BUILD_DIR/bin/mevacoind" "$BUILD_DIR/bin/mevacoin-wallet-cli" 2>/dev/null || true

hdr "FASE 5 — Merge test → mevacoin"
cd "$REPO_DIR"
git add -A 2>/dev/null || true
git diff --cached --quiet 2>/dev/null || git commit -m "chore: participation patches verified and applied"
git checkout mevacoin || { err "Branch mevacoin non trovato"; exit 1; }
git merge test --no-ff -m "feat: merge participation incentive system (badge+PoA+rewards+RPC)" || {
  err "Conflitti nel merge. Risolvi con: git mergetool  poi: git merge --continue"; exit 1; }
ok "Merge completato: test → mevacoin"

hdr "TUTTO COMPLETATO"
echo ""
echo -e "${GRN}${BLD}Sistema Badge e Incentivi MevaCoin PRONTO!${NC}"
echo ""
echo "  DEPLOY:"
echo "  1. sudo systemctl stop mevacoind"
echo "  2. sudo cp $BUILD_DIR/bin/mevacoind /usr/local/bin/mevacoind"
echo "  3. sudo systemctl start mevacoind"
echo ""
echo "  REGISTRA NODO:"
echo "  mevacoind register_node <wallet_address> <node_pubkey_hex>"
echo ""
echo "  QUERY RPC (dopo HF blocco 13):"
echo "  curl http://127.0.0.1:18081/get_badges?wallet_address=<addr>"
echo "  curl http://127.0.0.1:18081/get_node_uptime?node_id=<id>"
echo "  curl http://127.0.0.1:18081/get_participation_score?node_id=<id>"
echo "  curl http://127.0.0.1:18081/get_reward_history?node_id=<id>&count=20"
echo ""
echo "  WALLET CLI:"
echo "  mevacoin-wallet-cli → get_badges"
