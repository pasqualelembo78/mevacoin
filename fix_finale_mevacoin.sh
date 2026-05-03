#!/bin/bash
# ============================================================
#  FIX FINALE MEVACOIN — Ultimi riferimenti Monero nel daemon
#  Uso:  bash fix_finale_mevacoin.sh
#  Eseguire dalla root del repo: cd /root/mevacoin
# ============================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo ""
echo -e "${GREEN}============================================================${NC}"
echo -e "${GREEN}  FIX FINALE MEVACOIN — Ultimi riferimenti Monero${NC}"
echo -e "${GREEN}============================================================${NC}"
echo ""

FIXES=0
ERRORS=0

apply_fix() {
    local desc="$1"
    shift
    echo -e "  ${YELLOW}→${NC} $desc"
    if "$@" 2>/dev/null; then
        echo -e "    ${GREEN}✓ OK${NC}"
        FIXES=$((FIXES + 1))
    else
        echo -e "    ${RED}✗ ERRORE o già applicato${NC}"
        ERRORS=$((ERRORS + 1))
    fi
}

# ---- 1. RINOMINA FILE ----
echo -e "${YELLOW}[1/5] Rinomina file con 'monero' nel nome${NC}"
echo ""

if [ -f "contrib/rlwrap/monerocommands_bitmonerod.txt" ]; then
    apply_fix "monerocommands_bitmonerod.txt → mevacoincommands_mevacoind.txt" \
        mv contrib/rlwrap/monerocommands_bitmonerod.txt contrib/rlwrap/mevacoincommands_mevacoind.txt
else
    echo -e "    ${GREEN}✓ Già rinominato o non esiste${NC}"
fi

if [ -f "contrib/rlwrap/monerocommands_monero-wallet-cli.txt" ]; then
    apply_fix "monerocommands_monero-wallet-cli.txt → mevacoincommands_mevacoin-wallet-cli.txt" \
        mv contrib/rlwrap/monerocommands_monero-wallet-cli.txt contrib/rlwrap/mevacoincommands_mevacoin-wallet-cli.txt
else
    echo -e "    ${GREEN}✓ Già rinominato o non esiste${NC}"
fi

if [ -f "contrib/valgrind/mevacoin.supp" ]; then
    apply_fix "mevacoin.supp → mevacoin.supp" \
        mv contrib/valgrind/mevacoin.supp contrib/valgrind/mevacoin.supp
    # Aggiorna riferimenti a questo file nel progetto
    grep -rl "monero\.supp" --include="*.cmake" --include="CMakeLists.txt" \
        --include="*.sh" --include="*.md" --exclude-dir=".git" . 2>/dev/null | while read f; do
        sed -i 's/monero\.supp/mevacoin.supp/g' "$f" 2>/dev/null
        echo -e "    ${GREEN}✓ Aggiornato riferimento in $f${NC}"
    done
else
    echo -e "    ${GREEN}✓ Già rinominato o non esiste${NC}"
fi

if [ -f "utils/gpg_keys/moneromooo.asc" ]; then
    apply_fix "Rimuovi chiave GPG moneromooo.asc (sviluppatore Monero)" \
        rm utils/gpg_keys/moneromooo.asc
    # Rimuovi riferimenti nel codice
    grep -rl "moneromooo" --include="*.cpp" --include="*.h" \
        --exclude-dir=".git" . 2>/dev/null | while read f; do
        sed -i '/moneromooo/d' "$f" 2>/dev/null
        echo -e "    ${GREEN}✓ Rimosso riferimento in $f${NC}"
    done
else
    echo -e "    ${GREEN}✓ Già rimosso o non esiste${NC}"
fi

echo ""

# ---- 2. GITMODULES: supercop ----
echo -e "${YELLOW}[2/5] Fix .gitmodules — supercop${NC}"
echo ""

if grep -q "monero-project/supercop" .gitmodules 2>/dev/null; then
    apply_fix ".gitmodules: monero-project/supercop → pasqualelembo78/supercop" \
        sed -i 's|https://github.com/monero-project/supercop|https://github.com/pasqualelembo78/supercop|g' .gitmodules
    echo ""
    echo -e "  ${YELLOW}⚠ RICORDA: devi forkare monero-project/supercop nel tuo GitHub${NC}"
    echo -e "  ${YELLOW}  se non l'hai già fatto: https://github.com/monero-project/supercop${NC}"
else
    echo -e "    ${GREEN}✓ .gitmodules già corretto${NC}"
fi

echo ""

# ---- 3. wallet2.cpp — testo help ----
echo -e "${YELLOW}[3/5] Fix testo help in wallet2.cpp${NC}"
echo ""

WALLET2="src/wallet/wallet2.cpp"
if [ -f "$WALLET2" ] && grep -q "instead of 18081" "$WALLET2" 2>/dev/null; then
    apply_fix "wallet2.cpp: 'instead of 18081' → 'instead of default'" \
        sed -i 's/instead of 18081/instead of default/g' "$WALLET2"
else
    echo -e "    ${GREEN}✓ Già corretto o file non trovato${NC}"
fi

echo ""

# ---- 4. CMakeLists.txt — commento ----
echo -e "${YELLOW}[4/5] Fix commento in CMakeLists.txt${NC}"
echo ""

if grep -q "not enough for monero" CMakeLists.txt 2>/dev/null; then
    apply_fix "CMakeLists.txt: commento 'for monero' → 'for mevacoin'" \
        sed -i 's/not enough for monero/not enough for mevacoin/g' CMakeLists.txt
else
    echo -e "    ${GREEN}✓ Già corretto${NC}"
fi

echo ""

# ---- 5. Contenuto dei file rinominati (rlwrap) ----
echo -e "${YELLOW}[5/5] Fix contenuto file rlwrap${NC}"
echo ""

for f in contrib/rlwrap/mevacoincommands_mevacoind.txt contrib/rlwrap/mevacoincommands_mevacoin-wallet-cli.txt; do
    if [ -f "$f" ]; then
        if grep -qi "monero" "$f" 2>/dev/null; then
            apply_fix "Contenuto di $(basename $f): monero → mevacoin" \
                sed -i -e 's/monero/mevacoin/gI' -e 's/Monero/MevaCoin/g' -e 's/MONERO/MEVACOIN/g' "$f"
        else
            echo -e "    ${GREEN}✓ $(basename $f) già pulito${NC}"
        fi
    fi
done

if [ -f "contrib/valgrind/mevacoin.supp" ]; then
    if grep -qi "monero" contrib/valgrind/mevacoin.supp 2>/dev/null; then
        apply_fix "Contenuto di mevacoin.supp: monero → mevacoin" \
            sed -i -e 's/monero/mevacoin/gI' -e 's/Monero/MevaCoin/g' -e 's/MONERO/MEVACOIN/g' contrib/valgrind/mevacoin.supp
    else
        echo -e "    ${GREEN}✓ mevacoin.supp già pulito${NC}"
    fi
fi

echo ""

# ---- RIEPILOGO ----
echo -e "${GREEN}============================================================${NC}"
echo -e "${GREEN}  COMPLETATO${NC}"
echo -e "${GREEN}============================================================${NC}"
echo ""
echo -e "  Fix applicati:  ${GREEN}$FIXES${NC}"
echo -e "  Errori/skip:    ${YELLOW}$ERRORS${NC}"
echo ""

# Verifica finale
REMAINING=$(grep -rciI "monero" \
    --exclude-dir=".git" --exclude-dir="build" \
    --exclude="rebrand_*" --exclude="fix*" --exclude="audit_*" \
    --exclude="*.proto" --exclude="*.pb.h" --exclude="*.pb.cc" \
    --include="*.cpp" --include="*.h" --include="*.cmake" \
    --include="*.txt" --include="*.supp" \
    . 2>/dev/null | awk -F: '{s+=$2}END{print s+0}')

echo -e "  Riferimenti Monero rimasti nel codice C++ (esclusi Trezor proto): ${YELLOW}$REMAINING${NC}"
echo ""

if [ "$REMAINING" -gt 0 ]; then
    echo -e "  ${YELLOW}Dettaglio riferimenti rimasti:${NC}"
    grep -rniI "monero" \
        --exclude-dir=".git" --exclude-dir="build" \
        --exclude="rebrand_*" --exclude="fix*" --exclude="audit_*" \
        --exclude="*.proto" --exclude="*.pb.h" --exclude="*.pb.cc" \
        --include="*.cpp" --include="*.h" --include="*.cmake" \
        --include="*.txt" --include="*.supp" \
        . 2>/dev/null | grep -iv "Copyright" | head -20
fi

echo ""
echo -e "  ${GREEN}Nota: i riferimenti nel protobuf Trezor (messages-monero.proto)${NC}"
echo -e "  ${GREEN}sono intenzionalmente NON modificati per compatibilità firmware.${NC}"
echo ""
