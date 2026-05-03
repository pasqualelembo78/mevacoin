#!/bin/bash
set -e

echo "🔧 Mevacoin Build Cleanup - Inizio..."
echo ""

# === STEP 1: Rimuovi gen_multisig e gen_ssl_cert ===
echo "[1/5] Rimuovo gen_multisig e gen_ssl_cert da src/CMakeLists.txt..."
sed -i '/add_subdirectory(gen_multisig)/d' src/CMakeLists.txt
sed -i '/add_subdirectory(gen_ssl_cert)/d' src/CMakeLists.txt

# === STEP 2: Rinomina mevacoin-wallet-rpc → mevacoin-wallet-rpc ===
echo "[2/5] Rinomino mevacoin-wallet-rpc → mevacoin-wallet-rpc..."
sed -i 's/OUTPUT_NAME "mevacoin-wallet-rpc"/OUTPUT_NAME "mevacoin-wallet-rpc"/' src/wallet/CMakeLists.txt

# === STEP 3: Rinomina mevacoin-blockchain-prune → mevacoin-blockchain-prune ===
echo "[3/5] Rinomino mevacoin-blockchain-prune → mevacoin-blockchain-prune..."
sed -i 's/OUTPUT_NAME "mevacoin-blockchain-prune"/OUTPUT_NAME "mevacoin-blockchain-prune"/' src/blockchain_utilities/CMakeLists.txt

# === STEP 4: Rimuovi 6 target inutili (definizioni + compilazione) ===
echo "[4/5] Rimuovo 6 target inutili da blockchain_utilities..."

# Rimuovi definizioni variabili
for TARGET in blockchain_blackball blockchain_usage blockchain_prune_known_spent_data blockchain_ancestry blockchain_depth blockchain_stats; do
  sed -i "/set(${TARGET}_sources/,/mevacoin_private_headers(${TARGET}/{/mevacoin_private_headers(${TARGET}/!d}" src/blockchain_utilities/CMakeLists.txt
  sed -i "/mevacoin_private_headers(${TARGET}/,+1d" src/blockchain_utilities/CMakeLists.txt
done

# Rimuovi blocchi di compilazione (add_executable + link + install)
for TARGET in blockchain_blackball blockchain_usage blockchain_ancestry blockchain_depth blockchain_stats blockchain_prune_known_spent_data; do
  sed -i "/mevacoin_add_executable(${TARGET}/,/install(TARGETS ${TARGET}/d" src/blockchain_utilities/CMakeLists.txt
done

# === STEP 5: Verifica ===
echo "[5/5] Verifica..."
echo ""
echo "=== OUTPUT_NAME trovati (devono essere 6) ==="
grep -n 'OUTPUT_NAME' src/blockchain_utilities/CMakeLists.txt src/wallet/CMakeLists.txt src/daemon/CMakeLists.txt src/simplewallet/CMakeLists.txt
echo ""
echo "=== gen_multisig/gen_ssl_cert (deve essere VUOTO) ==="
grep 'gen_multisig\|gen_ssl_cert' src/CMakeLists.txt || echo "(OK - nessun risultato)"
echo ""
echo "✅ Fatto! Binari finali:"
echo "   mevacoind"
echo "   wallet-cli"
echo "   mevacoin-wallet-rpc"
echo "   import"
echo "   export"
echo "   mevacoin-blockchain-prune"
echo ""
echo "Per committare esegui:"
echo "  git add -A && git commit -m 'Cleanup: keep only user-facing binaries, rename mevacoin to mevacoin' && git push origin master"
