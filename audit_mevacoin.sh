#!/bin/bash
# ============================================================
#  AUDIT MEVACOIN — Cerca riferimenti nascosti a Monero
#  Uso:  bash audit_mevacoin.sh [percorso_repo]
#  Output: audit_risultati.txt nella directory corrente
# ============================================================

REPO="${1:-.}"
OUT="audit_risultati.txt"

cd "$REPO" || { echo "Directory non trovata: $REPO"; exit 1; }

exec > "$OUT" 2>&1

echo "============================================================"
echo "   AUDIT MEVACOIN — Riferimenti Monero"
echo "   Repository: $(pwd)"
echo "   Data: $(date)"
echo "============================================================"
echo ""

# ---- 1. TOTALE OCCORRENZE ----
echo "============================================================"
echo "  1. CONTEGGIO TOTALE"
echo "============================================================"
TOTAL=$(grep -rciI "monero\|xmr\|monerod\|bitmonero\|getmonero" \
  --exclude-dir=".git" --exclude-dir="build" . 2>/dev/null \
  | awk -F: '{s+=$2}END{print s}')
FILES=$(grep -rciI "monero\|xmr\|monerod\|bitmonero\|getmonero" \
  --exclude-dir=".git" --exclude-dir="build" . 2>/dev/null \
  | grep -v ":0$" | wc -l)
echo "  Riferimenti totali: $TOTAL"
echo "  File coinvolti:     $FILES"
echo ""

# ---- 2. URL MONERO (I PIU' PERICOLOSI) ----
echo "============================================================"
echo "  2. URL MONERO (PERICOLOSI — puntano a siti Monero)"
echo "============================================================"
grep -rnI "getmonero\|monero\.org\|monero\.com\|moneroworld\|moneropulse\|monero-project" \
  --exclude-dir=".git" --exclude-dir="build" --exclude-dir="translations" .
echo ""

# ---- 3. BINARIO monerod ----
echo "============================================================"
echo "  3. RIFERIMENTI A 'monerod' (nome binario daemon)"
echo "============================================================"
grep -rnI "monerod" \
  --include="*.cpp" --include="*.h" --include="*.qml" \
  --include="*.bat" --include="*.yml" --include="*.yaml" \
  --include="*.cmake" --include="*.iss" \
  --exclude-dir=".git" --exclude-dir="build" .
echo ""

# ---- 4. monero-wallet-gui (nome binario GUI) ----
echo "============================================================"
echo "  4. RIFERIMENTI A 'monero-wallet-gui' (nome binario GUI)"
echo "============================================================"
grep -rnI "monero-wallet-gui\|monero_wallet_gui" \
  --exclude-dir=".git" --exclude-dir="build" --exclude-dir="translations" .
echo ""

# ---- 5. .bitmonero (directory dati) ----
echo "============================================================"
echo "  5. RIFERIMENTI A '.bitmonero' (directory dati)"
echo "============================================================"
grep -rnI "bitmonero" --exclude-dir=".git" --exclude-dir="build" .
echo ""

# ---- 6. CODICE SORGENTE C++ (esclusi copyright e namespace lib) ----
echo "============================================================"
echo "  6. RIFERIMENTI NEL CODICE C++/H (esclusi copyright/namespace)"
echo "============================================================"
grep -rnI "monero\|monerod\|bitmonero\|getmonero" \
  --include="*.cpp" --include="*.h" \
  --exclude-dir=".git" --exclude-dir="build" . \
  | grep -iv "Copyright\|namespace Monero\|Monero::"
echo ""

# ---- 7. FILE QML ----
echo "============================================================"
echo "  7. RIFERIMENTI NEI FILE QML (interfaccia utente)"
echo "============================================================"
grep -rnI "monero\|monerod\|getmonero" \
  --include="*.qml" \
  --exclude-dir=".git" --exclude-dir="build" . \
  | grep -iv "Copyright"
echo ""

# ---- 8. FILE CON MONERO NEL NOME ----
echo "============================================================"
echo "  8. FILE/DIRECTORY CON 'monero' NEL NOME"
echo "============================================================"
find . -not -path "./.git/*" -not -path "./build/*" -iname "*monero*" | sort
echo ""

# ---- 9. PORTE MONERO HARDCODED ----
echo "============================================================"
echo "  9. PORTE MONERO HARDCODED (18080/18081/18082/18083)"
echo "============================================================"
grep -rnI "18080\|18081\|18082\|18083" \
  --include="*.cpp" --include="*.h" --include="*.qml" \
  --include="*.yml" --include="*.yaml" --include="*.iss" \
  --exclude-dir=".git" --exclude-dir="build" .
echo ""

# ---- 10. CONFIG/BUILD FILES ----
echo "============================================================"
echo "  10. RIFERIMENTI NEI FILE DI BUILD (CMake, Docker, CI/CD)"
echo "============================================================"
grep -rnI "monero\|monerod" \
  --include="CMakeLists.txt" --include="*.cmake" \
  --include="Dockerfile*" --include="Makefile" \
  --include="*.yml" --include="*.yaml" \
  --include="*.bat" --include="*.iss" \
  --exclude-dir=".git" --exclude-dir="build" .
echo ""

# ---- 11. TOP 30 FILE PER NUMERO OCCORRENZE ----
echo "============================================================"
echo "  11. TOP 30 FILE CON PIU' OCCORRENZE"
echo "============================================================"
printf "  %-60s %s\n" "FILE" "OCCORRENZE"
printf "  %-60s %s\n" "----" "----------"
grep -rciI "monero" --exclude-dir=".git" --exclude-dir="build" . \
  | grep -v ":0$" \
  | sort -t: -k2 -nr \
  | head -30 \
  | while IFS=: read -r file count; do
      printf "  %-60s %s\n" "$file" "$count"
    done
echo ""

# ---- 12. TRADUZIONI (conteggio per file) ----
echo "============================================================"
echo "  12. FILE DI TRADUZIONE (.ts) — conteggio per file"
echo "============================================================"
grep -ciI "monero\|xmr\|getmonero" translations/*.ts 2>/dev/null \
  | grep -v ":0$" \
  | sort -t: -k2 -nr
echo ""

# ---- RIEPILOGO FINALE ----
echo "============================================================"
echo "  RIEPILOGO FINALE"
echo "============================================================"
echo ""
echo "  Riferimenti totali:     $TOTAL"
echo "  File coinvolti:         $FILES"
echo ""
URL_COUNT=$(grep -rciI "getmonero\|monero\.org\|moneroworld\|moneropulse" \
  --exclude-dir=".git" --exclude-dir="build" . 2>/dev/null \
  | awk -F: '{s+=$2}END{print s}')
DAEMON_COUNT=$(grep -rciI "monerod" \
  --include="*.cpp" --include="*.h" --include="*.qml" \
  --exclude-dir=".git" --exclude-dir="build" . 2>/dev/null \
  | awk -F: '{s+=$2}END{print s}')
BITM_COUNT=$(grep -rciI "bitmonero" \
  --exclude-dir=".git" --exclude-dir="build" . 2>/dev/null \
  | awk -F: '{s+=$2}END{print s}')
FILE_NAMES=$(find . -not -path "./.git/*" -not -path "./build/*" \
  -iname "*monero*" 2>/dev/null | wc -l)
echo "  URL Monero (pericolosi):      $URL_COUNT"
echo "  Riferimenti a 'monerod':      $DAEMON_COUNT"
echo "  Riferimenti a '.bitmonero':   $BITM_COUNT"
echo "  File con 'monero' nel nome:   $FILE_NAMES"
echo ""
echo "  Output salvato in: $(pwd)/$OUT"
echo "============================================================"

# Messaggio a schermo (non nel file)
exec > /dev/tty 2>&1
echo ""
echo "Audit completato! Risultati salvati in: $(pwd)/$OUT"
echo ""
