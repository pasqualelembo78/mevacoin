#!/bin/bash
# =============================================================================
# MevaCoin — Fix: construct_miner_tx_with_participation
# Eseguire dalla ROOT del repo: bash apply_miner_tx_fix.sh
# =============================================================================
set -e

DIR="src/cryptonote_core/participation"
H="${DIR}/participation_coinbase_validator.h"
CPP="${DIR}/participation_coinbase_validator.cpp"

echo ""
echo "======================================================"
echo "  MevaCoin — construct_miner_tx_with_participation fix"
echo "======================================================"
echo ""

if [ ! -f "CMakeLists.txt" ] || [ ! -d "$DIR" ]; then
  echo "ERRORE: Eseguire dalla root del repository mevacoin"; exit 1
fi

echo "[1/4] Backup file originali..."
cp "$H"   "${H}.bak.$(date +%Y%m%d_%H%M%S)"
cp "$CPP" "${CPP}.bak.$(date +%Y%m%d_%H%M%S)"
echo "      OK"

echo "[2/4] Copia file aggiornati..."
cp participation_coinbase_validator.h   "$H"
cp participation_coinbase_validator.cpp "$CPP"
echo "      OK"

echo "[3/4] Verifica dichiarazione..."
if grep -q "construct_miner_tx_with_participation" "$H"; then
  echo "      OK — dichiarazione trovata in .h"
else
  echo "ERRORE: dichiarazione NON trovata in .h"; exit 1
fi
if grep -q "construct_miner_tx_with_participation" "$CPP"; then
  echo "      OK — implementazione trovata in .cpp"
else
  echo "ERRORE: implementazione NON trovata in .cpp"; exit 1
fi

echo "[4/4] Build mevacoind..."
if [ ! -d "build" ]; then
  mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cd ..
fi
cd build && make -j$(nproc) daemon
echo ""
echo "======================================================"
echo "  Build OK!"
echo "  Binario: build/bin/mevacoind"
echo "======================================================"
