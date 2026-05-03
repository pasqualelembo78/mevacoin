#!/bin/bash
# ============================================================================
# MEVACOIN AUDIT FIX SCRIPT
# ============================================================================
# Applica tutti i fix identificati nell'audit del repository MevaCoin.
# Eseguire dalla root del repository mevacoin.
#
# Uso:  chmod +x fix_mevacoin_audit.sh && ./fix_mevacoin_audit.sh
#
# NOTE:
#   - NON tocca genesis TX
#   - NON cambia porte P2P/RPC  
#   - Sicuro per chain non ancora lanciate
# ============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN}  MEVACOIN AUDIT FIX SCRIPT${NC}"
echo -e "${CYAN}============================================${NC}"
echo ""

# Verifica che siamo nella root del repo
if [ ! -f "src/cryptonote_config.h" ]; then
    echo -e "${RED}ERRORE: Eseguire dalla root del repository mevacoin!${NC}"
    echo "  Il file src/cryptonote_config.h non trovato."
    exit 1
fi

FIXES_APPLIED=0
FIXES_FAILED=0

apply_fix() {
    local num=$1
    local desc=$2
    echo -e "\n${YELLOW}━━━ FIX $num: $desc ━━━${NC}"
}

success() {
    echo -e "  ${GREEN}✓ Applicato${NC}"
    ((FIXES_APPLIED++))
}

fail() {
    echo -e "  ${RED}✗ FALLITO: $1${NC}"
    ((FIXES_FAILED++))
}

# ============================================================================
# FIX 1: OpenAlias "oa1:xmr" → "oa1:meva"
# ============================================================================
apply_fix 1 "OpenAlias: oa1:xmr → oa1:meva"

if grep -q '"oa1:xmr"' src/common/dns_utils.cpp 2>/dev/null; then
    sed -i \
        -e 's|"oa1:xmr"|"oa1:meva"|g' \
        -e 's|XMR address|MEVA address|g' \
        src/common/dns_utils.cpp
    success
else
    echo -e "  ${CYAN}→ Già applicato o file non trovato${NC}"
fi

# ============================================================================
# FIX 2: Prefisso indirizzo → "M..." (prefix 123/124/125)
# ============================================================================
apply_fix 2 "Prefisso indirizzo: 18 → 123 (indirizzi M...)"

CONFIG_FILE="src/cryptonote_config.h"

if grep -q 'CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX = 18;' "$CONFIG_FILE" 2>/dev/null; then
    # Mainnet
    sed -i \
        -e 's/CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX = 18;/CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX = 123;  \/\/ "M..." addresses/' \
        -e 's/CRYPTONOTE_PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX = 19;/CRYPTONOTE_PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX = 124;  \/\/ "M..." integrated/' \
        -e 's/CRYPTONOTE_PUBLIC_SUBADDRESS_BASE58_PREFIX = 42;/CRYPTONOTE_PUBLIC_SUBADDRESS_BASE58_PREFIX = 125;  \/\/ "M..." subaddress/' \
        "$CONFIG_FILE"
    
    # Testnet
    sed -i \
        -e '/namespace testnet/,/}/ s/CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX = 53;/CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX = 126;  \/\/ testnet "M..."/' \
        -e '/namespace testnet/,/}/ s/CRYPTONOTE_PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX = 54;/CRYPTONOTE_PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX = 127;/' \
        -e '/namespace testnet/,/}/ s/CRYPTONOTE_PUBLIC_SUBADDRESS_BASE58_PREFIX = 63;/CRYPTONOTE_PUBLIC_SUBADDRESS_BASE58_PREFIX = 128;/' \
        "$CONFIG_FILE"
    
    # Stagenet  
    sed -i \
        -e '/namespace stagenet/,/}/ s/CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX = 24;/CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX = 129;  \/\/ stagenet/' \
        -e '/namespace stagenet/,/}/ s/CRYPTONOTE_PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX = 25;/CRYPTONOTE_PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX = 130;/' \
        -e '/namespace stagenet/,/}/ s/CRYPTONOTE_PUBLIC_SUBADDRESS_BASE58_PREFIX = 36;/CRYPTONOTE_PUBLIC_SUBADDRESS_BASE58_PREFIX = 131;/' \
        "$CONFIG_FILE"
    
    success
else
    echo -e "  ${CYAN}→ Già applicato o valore diverso${NC}"
fi

# ============================================================================
# FIX 3: DEFAULT_FEE_ATOMIC_XMR_PER_KB → MEVA
# ============================================================================
apply_fix 3 "Rinomina DEFAULT_FEE_ATOMIC_XMR_PER_KB → MEVA"

if grep -rq 'DEFAULT_FEE_ATOMIC_XMR_PER_KB' src/ 2>/dev/null; then
    # Trova e sostituisci in tutti i file sorgente
    find src/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.c" -o -name "*.hpp" -o -name "*.inl" \) \
        -exec sed -i 's/DEFAULT_FEE_ATOMIC_XMR_PER_KB/DEFAULT_FEE_ATOMIC_MEVA_PER_KB/g' {} +
    
    COUNT=$(grep -rn 'DEFAULT_FEE_ATOMIC_MEVA_PER_KB' src/ 2>/dev/null | wc -l)
    echo -e "  ${GREEN}✓ Rinominato in $COUNT occorrenze${NC}"
    ((FIXES_APPLIED++))
else
    echo -e "  ${CYAN}→ Già rinominato${NC}"
fi

# ============================================================================
# FIX 4: HF_VERSION per BP+, View Tags, 2021 Scaling → 12
# ============================================================================
apply_fix 4 "Abbassa HF_VERSION_BULLETPROOF_PLUS/VIEW_TAGS/2021_SCALING a 12"

if grep -q 'HF_VERSION_BULLETPROOF_PLUS.*15' "$CONFIG_FILE" 2>/dev/null; then
    sed -i \
        -e 's/#define HF_VERSION_BULLETPROOF_PLUS             15/#define HF_VERSION_BULLETPROOF_PLUS             12  \/\/ MevaCoin: attivo da HF12/' \
        -e 's/#define HF_VERSION_VIEW_TAGS                    15/#define HF_VERSION_VIEW_TAGS                    12  \/\/ MevaCoin: attivo da HF12/' \
        -e 's/#define HF_VERSION_2021_SCALING                 15/#define HF_VERSION_2021_SCALING                 12  \/\/ MevaCoin: attivo da HF12/' \
        "$CONFIG_FILE"
    success
else
    echo -e "  ${CYAN}→ Già applicato${NC}"
fi

# ============================================================================
# FIX 5: .gitmodules — aggiornare URL supercop
# ============================================================================
apply_fix 5 ".gitmodules: monero-project/supercop → pasqualelembo78/supercop"

if grep -q 'monero-project/supercop' .gitmodules 2>/dev/null; then
    sed -i \
        -e 's|https://github.com/monero-project/supercop|https://github.com/pasqualelembo78/supercop|g' \
        -e 's|branch = monero|branch = mevacoin|g' \
        .gitmodules
    success
    echo -e "  ${YELLOW}⚠ RICORDA: forkare monero-project/supercop nel tuo GitHub e rinominare il branch a 'mevacoin'${NC}"
else
    echo -e "  ${CYAN}→ Già aggiornato${NC}"
fi

# ============================================================================
# FIX 6: xmr_amount → meva_amount (RingCT)
# ============================================================================
apply_fix 6 "xmr_amount → meva_amount (RingCT e tutto il codebase)"

if grep -rq 'xmr_amount' src/ 2>/dev/null; then
    # Sostituisci in tutti i file sorgente (escluso device_trezor che ha protobuf Monero)
    find src/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.c" -o -name "*.hpp" -o -name "*.inl" \) \
        ! -path "*/device_trezor/*" \
        -exec sed -i 's/xmr_amount/meva_amount/g' {} +
    
    # Anche le funzioni helper come randXmrAmount
    find src/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.c" -o -name "*.hpp" \) \
        ! -path "*/device_trezor/*" \
        -exec sed -i 's/randXmrAmount/randMevaAmount/g' {} +
    
    COUNT=$(grep -rn 'meva_amount' src/ 2>/dev/null | wc -l)
    echo -e "  ${GREEN}✓ Rinominato in $COUNT occorrenze${NC}"
    ((FIXES_APPLIED++))
    echo -e "  ${YELLOW}⚠ device_trezor/ escluso (protobuf Monero immutabili)${NC}"
else
    echo -e "  ${CYAN}→ Già rinominato${NC}"
fi

# ============================================================================
# FIX 7: Pulizia commenti Monero residui
# ============================================================================
apply_fix 7 "Pulizia commenti Monero residui"

# CMakeLists.txt
if grep -q 'before Monero implemented LMDB' CMakeLists.txt 2>/dev/null; then
    sed -i \
        -e 's/before Monero implemented LMDB/before LMDB was implemented/g' \
        -e 's/not enough for monero/not enough for mevacoin/g' \
        CMakeLists.txt
    echo -e "  ${GREEN}✓ CMakeLists.txt pulito${NC}"
else
    echo -e "  ${CYAN}→ CMakeLists.txt già pulito${NC}"
fi

# simplewallet help text - cerca e sostituisci "xmr" nei testi utente
if grep -q '"xmr"' src/simplewallet/simplewallet.cpp 2>/dev/null; then
    # Attenzione: solo nelle stringhe utente, non nei nomi di variabili
    echo -e "  ${YELLOW}⚠ simplewallet.cpp: verificare manualmente le occorrenze di 'xmr' nei testi d'aiuto${NC}"
fi

((FIXES_APPLIED++))

# ============================================================================
# VERIFICA FINALE
# ============================================================================
echo -e "\n${CYAN}============================================${NC}"
echo -e "${CYAN}  VERIFICA RIFERIMENTI MONERO RESIDUI${NC}"
echo -e "${CYAN}============================================${NC}"

echo -e "\nRicerca 'monero' nel codice sorgente (escl. copyright, device_trezor, rebrand scripts)..."
REMAINING=$(grep -rin 'monero' src/ tests/ contrib/ cmake/ \
    --include="*.cpp" --include="*.h" --include="*.c" --include="*.hpp" --include="*.inl" \
    2>/dev/null | \
    grep -vi 'Copyright' | \
    grep -vi 'device_trezor' | \
    grep -vi 'MoneroMessageSignature' | \
    grep -vi 'set_monero_version' | \
    wc -l)

echo -e "  Riferimenti 'monero' rimasti: ${YELLOW}$REMAINING${NC}"

echo -e "\nRicerca 'xmr' nel codice sorgente (escl. device_trezor)..."
XMR_REMAINING=$(grep -rin '\bxmr\b' src/ tests/ \
    --include="*.cpp" --include="*.h" --include="*.c" --include="*.hpp" --include="*.inl" \
    2>/dev/null | \
    grep -vi 'device_trezor' | \
    wc -l)

echo -e "  Riferimenti 'xmr' rimasti: ${YELLOW}$XMR_REMAINING${NC}"

if [ "$REMAINING" -gt 0 ] || [ "$XMR_REMAINING" -gt 0 ]; then
    echo -e "\n${YELLOW}Dettaglio riferimenti rimasti:${NC}"
    grep -rin 'monero' src/ tests/ contrib/ cmake/ \
        --include="*.cpp" --include="*.h" --include="*.c" --include="*.hpp" --include="*.inl" \
        2>/dev/null | \
        grep -vi 'Copyright' | \
        grep -vi 'device_trezor' | \
        grep -vi 'MoneroMessageSignature' | \
        grep -vi 'set_monero_version' | \
        head -20
    
    grep -rin '\bxmr\b' src/ tests/ \
        --include="*.cpp" --include="*.h" --include="*.c" --include="*.hpp" --include="*.inl" \
        2>/dev/null | \
        grep -vi 'device_trezor' | \
        head -20
fi

# ============================================================================
# RIEPILOGO
# ============================================================================
echo -e "\n${CYAN}============================================${NC}"
echo -e "${CYAN}  RIEPILOGO${NC}"
echo -e "${CYAN}============================================${NC}"
echo -e "  Fix applicati:  ${GREEN}$FIXES_APPLIED${NC}"
echo -e "  Fix falliti:    ${RED}$FIXES_FAILED${NC}"
echo -e ""
echo -e "  ${YELLOW}PROSSIMI PASSI:${NC}"
echo -e "  1. Forkare monero-project/supercop nel tuo GitHub"
echo -e "  2. Ricompilare: ${CYAN}make clean && make -j$(nproc)${NC}"
echo -e "  3. Testare generazione indirizzi (devono iniziare con 'M')"
echo -e "  4. Testare mining (RandomX deve attivarsi al blocco 12)"
echo -e "  5. Configurare DNS: seed1/2/3.mevacoin.com con DNSSEC"
echo -e ""
echo -e "${GREEN}Script completato!${NC}"
