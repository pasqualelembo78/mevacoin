#!/bin/bash
while true; do
  clear
  DATA=$(curl -s --max-time 3 http://127.0.0.1:18081/get_info 2>/dev/null)
  if [ -z "$DATA" ]; then echo "Daemon non risponde"; sleep 5; continue; fi
  
  H=$(echo "$DATA" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('height','?'))")
  D=$(echo "$DATA" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('difficulty','?'))")
  TX=$(echo "$DATA" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('tx_pool_size','?'))")
  CD=$(echo "$DATA" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('cumulative_difficulty','?'))")
  NS=$(echo "$DATA" | python3 -c "import sys,json; d=json.load(sys.stdin); print('SI' if d.get('synchronized','') else 'NO')")
  
  echo "╔══════════════════════════════╗"
  echo "║     MevaCoin Mining Monitor  ║"
  echo "╠══════════════════════════════╣"
  echo "║ Altezza      : $H"
  echo "║ Difficoltà   : $D"
  echo "║ Cumulativa   : $CD"
  echo "║ Mempool TX   : $TX"
  echo "║ Synced       : $NS"
  echo "║ Ora          : $(date +%H:%M:%S)"
  echo "╚══════════════════════════════╝"
  
  sleep 5
done