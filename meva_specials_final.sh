#!/bin/bash
# ============================================================
#  MEVACOIN — Sistema Completo Blocchi Speciali
#  Versione finale — un unico script, da zero
#
#  COSA FA:
#    1. Crea mevacoin_specials.h (configurazione facile)
#    2. Patcha blockchain.cpp (validazione)
#    3. Patcha cryptonote_tx_utils.cpp (creazione blocco)
#
#  BLOCCHI ATTIVI:
#    - 18 Dicembre ogni anno: reward +25% + messaggio Desy
#    - Blocco #20: reward x100 (Fondo Fondatore)
#
#  COMING SOON: 12 date + 6 blocchi pronti da attivare
# ============================================================

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
NC='\033[0m'

echo ""
echo -e "${CYAN}============================================================${NC}"
echo -e "${CYAN}  MEVACOIN — Sistema Completo Blocchi Speciali${NC}"
echo -e "${CYAN}============================================================${NC}"
echo ""

cd /root/mevacoin

# Verifica che siamo nel posto giusto
if [ ! -f "src/cryptonote_core/blockchain.cpp" ]; then
    echo -e "${RED}ERRORE: non trovo src/cryptonote_core/blockchain.cpp${NC}"
    echo -e "${RED}Sei nella directory giusta? Esegui: cd /root/mevacoin${NC}"
    exit 1
fi

# ============================================================
# STEP 1: Crea mevacoin_specials.h
# ============================================================
echo -e "${YELLOW}[1/3] Creo mevacoin_specials.h${NC}"

cat > src/cryptonote_core/mevacoin_specials.h << 'EOF'
// Copyright (c) 2026, The MevaCoin Project
//
// ╔══════════════════════════════════════════════════════════╗
// ║        MEVACOIN — BLOCCHI SPECIALI E COMMEMORATIVI      ║
// ║                                                          ║
// ║  Questo file contiene TUTTE le date e i blocchi speciali ║
// ║  della blockchain MevaCoin.                              ║
// ║                                                          ║
// ║  COME AGGIUNGERE UNA DATA COMMEMORATIVA:                 ║
// ║    1. Vai alla tabella DATE COMMEMORATIVE qui sotto       ║
// ║    2. Togli // dalla riga che vuoi attivare               ║
// ║    3. Cambia mese, giorno, bonus e messaggio              ║
// ║    4. Ricompila: make -j$(nproc)                          ║
// ║                                                          ║
// ║  COME AGGIUNGERE UN BLOCCO BONUS:                        ║
// ║    1. Vai alla tabella BLOCCHI BONUS qui sotto            ║
// ║    2. Togli // dalla riga che vuoi attivare               ║
// ║    3. Cambia altezza blocco, moltiplicatore e messaggio   ║
// ║    4. Ricompila: make -j$(nproc)                          ║
// ║                                                          ║
// ╚══════════════════════════════════════════════════════════╝

#pragma once
#include <cstdint>
#include <ctime>
#include <string>

namespace meva {

// ============================================================
//  STRUTTURE DATI
// ============================================================

struct SpecialDate {
    int month;        // Mese: 1=Gen 2=Feb ... 12=Dic
    int day;          // Giorno: 1-31
    int bonus_pct;    // Bonus reward: 25 = +25%. Metti 0 per solo messaggio
    const char* name; // Nome dell'evento (breve)
    const char* msg;  // Messaggio completo che appare nel log e nel blocco
};

struct SpecialBlock {
    uint64_t height;  // Numero esatto del blocco
    int multiplier;   // Moltiplicatore: 100 = x100, 2 = x2, 1 = solo messaggio
    const char* name; // Nome dell'evento (breve)
    const char* msg;  // Messaggio completo che appare nel log e nel blocco
};


// ╔══════════════════════════════════════════════════════════╗
// ║                  DATE COMMEMORATIVE                      ║
// ║                                                          ║
// ║  Formato: {mese, giorno, bonus%, "nome", "messaggio"}    ║
// ║                                                          ║
// ║  Queste date si ripetono OGNI ANNO, per sempre.          ║
// ║  Il bonus si applica a TUTTI i blocchi di quel giorno.   ║
// ╚══════════════════════════════════════════════════════════╝

static const SpecialDate SPECIAL_DATES[] = {

    // ====== ATTIVE ======

    {12, 18, 25,
        "Compleanno Desy",
        "Buon compleanno Desy! Con amore, dalla blockchain MevaCoin."},

    // ====== COMING SOON — togli // per attivare ======

    // {1,   1,  10, "Capodanno",
    //     "Buon anno nuovo dalla blockchain MevaCoin!"},

    // {2,  14,  15, "San Valentino",
    //     "San Valentino — MevaCoin e' amore digitale."},

    // {3,   8,  10, "Festa della Donna",
    //     "Festa della donna — MevaCoin celebra."},

    // {4,  22,   5, "Giornata della Terra",
    //     "Giornata della Terra — MevaCoin per il pianeta."},

    // {5,   1,  10, "Festa dei Lavoratori",
    //     "Festa dei lavoratori — ogni blocco e' lavoro."},

    // {6,  15,   0, "Evento Giugno",
    //     "Compleanno di [NOME] — dedicato a te."},

    // {7,  20,   0, "Evento Luglio",
    //     "[EVENTO] — messaggio personalizzato."},

    // {8,  15,   0, "Ferragosto",
    //     "Ferragosto — anche la blockchain festeggia."},

    // {9,   1,   0, "Evento Settembre",
    //     "[EVENTO] — messaggio personalizzato."},

    // {10, 31,   5, "Halloween",
    //     "Halloween MevaCoin — dolcetto o blocchetto?"},

    // {11, 11,   0, "Evento Novembre",
    //     "[EVENTO] — messaggio personalizzato."},

    // {12, 25,  15, "Natale",
    //     "Buon Natale dalla blockchain MevaCoin!"},
};

static const int NUM_SPECIAL_DATES = sizeof(SPECIAL_DATES) / sizeof(SPECIAL_DATES[0]);


// ╔══════════════════════════════════════════════════════════╗
// ║                    BLOCCHI BONUS                          ║
// ║                                                          ║
// ║  Formato: {altezza, moltiplicatore, "nome", "messaggio"} ║
// ║                                                          ║
// ║  Questi bonus si applicano UNA VOLTA SOLA,               ║
// ║  quando viene minato quel blocco specifico.              ║
// ╚══════════════════════════════════════════════════════════╝

static const SpecialBlock SPECIAL_BLOCKS[] = {

    // ====== ATTIVI ======

    {20, 100,
        "Reward del Fondatore",
        "Blocco #20 — Reward del Fondatore: fondo promozione MevaCoin (x100)"},

    // ====== COMING SOON — togli // per attivare ======

    // {100,      10, "Primo Traguardo",
    //     "Blocco #100 — Primo traguardo raggiunto! Bonus x10"},

    // {1000,      5, "Milestone Mille",
    //     "Blocco #1.000 — Mille blocchi nella storia! Bonus x5"},

    // {10000,     3, "Milestone Diecimila",
    //     "Blocco #10.000 — La community cresce! Bonus x3"},

    // {50000,     2, "Milestone Cinquantamila",
    //     "Blocco #50.000 — Mezzo viaggio! Bonus x2"},

    // {100000,    2, "Milestone Centomila",
    //     "Blocco #100.000 — Centomila blocchi nella storia! Bonus x2"},

    // {1000000,   2, "Il Milione",
    //     "Blocco #1.000.000 — Un milione! La storia e' scritta. Bonus x2"},
};

static const int NUM_SPECIAL_BLOCKS = sizeof(SPECIAL_BLOCKS) / sizeof(SPECIAL_BLOCKS[0]);


// ╔══════════════════════════════════════════════════════════╗
// ║                FUNZIONI (non modificare)                  ║
// ╚══════════════════════════════════════════════════════════╝

// Cerca se il timestamp cade in una data commemorativa
// Ritorna: true se trovata, con bonus_pct, name e msg compilati
inline bool find_special_date(time_t timestamp, int &bonus_pct, const char* &name, const char* &msg) {
    struct tm* t = gmtime(&timestamp);
    if (!t) return false;
    int m = t->tm_mon + 1;  // tm_mon: 0=Gen, convertiamo a 1=Gen
    int d = t->tm_mday;
    for (int i = 0; i < NUM_SPECIAL_DATES; i++) {
        if (SPECIAL_DATES[i].month == m && SPECIAL_DATES[i].day == d) {
            bonus_pct = SPECIAL_DATES[i].bonus_pct;
            name = SPECIAL_DATES[i].name;
            msg = SPECIAL_DATES[i].msg;
            return true;
        }
    }
    return false;
}

// Cerca se l'altezza corrisponde a un blocco bonus
// Ritorna: true se trovato, con multiplier, name e msg compilati
inline bool find_special_block(uint64_t height, int &multiplier, const char* &name, const char* &msg) {
    for (int i = 0; i < NUM_SPECIAL_BLOCKS; i++) {
        if (SPECIAL_BLOCKS[i].height == height) {
            multiplier = SPECIAL_BLOCKS[i].multiplier;
            name = SPECIAL_BLOCKS[i].name;
            msg = SPECIAL_BLOCKS[i].msg;
            return true;
        }
    }
    return false;
}

// Calcola la reward finale applicando tutti i bonus applicabili
// I bonus data e bonus blocco si SOMMANO se coincidono
inline uint64_t calculate_final_reward(uint64_t base_reward, uint64_t height, time_t timestamp) {
    uint64_t reward = base_reward;
    const char* n = nullptr;
    const char* m = nullptr;
    int bonus_pct = 0;
    int multiplier = 1;

    // 1. Controlla data commemorativa → bonus percentuale
    if (find_special_date(timestamp, bonus_pct, n, m) && bonus_pct > 0) {
        reward = reward + (reward * static_cast<uint64_t>(bonus_pct) / 100);
    }

    // 2. Controlla blocco bonus → moltiplicatore
    if (find_special_block(height, multiplier, n, m) && multiplier > 1) {
        reward = reward * static_cast<uint64_t>(multiplier);
    }

    return reward;
}

} // namespace meva
EOF

echo -e "  ${GREEN}✓ mevacoin_specials.h creato con successo${NC}"
echo -e "    Date commemorative attive: 1 (Desy 18/12 +25%)"
echo -e "    Date coming soon:          12"
echo -e "    Blocchi bonus attivi:       1 (Blocco #20 x100)"
echo -e "    Blocchi bonus coming soon:  6"
echo ""

# ============================================================
# STEP 2: Patch blockchain.cpp (VALIDAZIONE)
# ============================================================
echo -e "${YELLOW}[2/3] Patch blockchain.cpp — validazione blocchi speciali${NC}"

BLOCKCHAIN="src/cryptonote_core/blockchain.cpp"

# Backup
cp "$BLOCKCHAIN" "${BLOCKCHAIN}.bak.specials" 2>/dev/null || true

# Rimuovi vecchie patch se esistono
sed -i '/MEVA_DESY_REWARD/d' "$BLOCKCHAIN" 2>/dev/null || true
sed -i '/MEVA_DESY_BIRTHDAY/d' "$BLOCKCHAIN" 2>/dev/null || true

if grep -q "MEVA_SPECIALS_V2" "$BLOCKCHAIN" 2>/dev/null; then
    echo -e "  ${GREEN}✓ Già patchato (v2)${NC}"
else
    # 2a. Aggiungi include dopo gli altri include
    if ! grep -q "mevacoin_specials.h" "$BLOCKCHAIN"; then
        LAST_INCLUDE=$(grep -n "^#include" "$BLOCKCHAIN" | tail -1 | cut -d: -f1)
        sed -i "${LAST_INCLUDE}a\\
#include \"mevacoin_specials.h\"  // MEVA_SPECIALS_V2" "$BLOCKCHAIN"
        echo -e "  ${GREEN}✓ #include aggiunto${NC}"
    fi

    # 2b. Trova get_block_reward dentro validate_miner_transaction
    # e aggiungi il codice subito dopo
    REWARD_LINE=$(grep -n "get_block_reward(median_weight" "$BLOCKCHAIN" | head -1 | cut -d: -f1)

    if [ -z "$REWARD_LINE" ]; then
        # Prova pattern alternativo
        REWARD_LINE=$(grep -n "get_block_reward(" "$BLOCKCHAIN" | head -1 | cut -d: -f1)
    fi

    if [ -n "$REWARD_LINE" ]; then
        AFTER=$((REWARD_LINE + 2))
        sed -i "${AFTER}i\\
\\
  // MEVA_SPECIALS_V2 — Sistema blocchi speciali e commemorativi\\
  // Applica bonus reward per date speciali e blocchi milestone\\
  {\\
    time_t block_ts = static_cast<time_t>(b.timestamp);\\
    uint64_t block_h = boost::get<txin_gen>(b.miner_tx.vin[0]).height;\\
    uint64_t final_reward = meva::calculate_final_reward(base_reward, block_h, block_ts);\\
\\
    if (final_reward != base_reward) {\\
      const char* evt_name = nullptr;\\
      const char* evt_msg = nullptr;\\
      int bp = 0; int bm = 1;\\
\\
      // Log data commemorativa\\
      if (meva::find_special_date(block_ts, bp, evt_name, evt_msg)) {\\
        MGINFO(\"\");\\
        MGINFO(\"  ============================================\");\\
        MGINFO(\"  [MEVA] \" << evt_name);\\
        MGINFO(\"  \" << evt_msg);\\
        if (bp > 0) MGINFO(\"  Reward bonus: +\" << bp << \"%\");\\
        MGINFO(\"  ============================================\");\\
        MGINFO(\"\");\\
      }\\
\\
      // Log blocco bonus\\
      if (meva::find_special_block(block_h, bm, evt_name, evt_msg)) {\\
        MGINFO(\"\");\\
        MGINFO(\"  ********************************************\");\\
        MGINFO(\"  [MEVA] \" << evt_name);\\
        MGINFO(\"  \" << evt_msg);\\
        MGINFO(\"  Reward: x\" << bm << \" (\" << print_money(final_reward) << \" MEVA)\");\\
        MGINFO(\"  ********************************************\");\\
        MGINFO(\"\");\\
      }\\
\\
      base_reward = final_reward;\\
    }\\
  }" "$BLOCKCHAIN"
        echo -e "  ${GREEN}✓ Validazione blocchi speciali aggiunta${NC}"
    else
        echo -e "  ${RED}✗ ERRORE: non trovata funzione get_block_reward${NC}"
        echo -e "  ${RED}  Patch manuale necessaria in validate_miner_transaction${NC}"
    fi
fi

echo ""

# ============================================================
# STEP 3: Patch cryptonote_tx_utils.cpp (CREAZIONE BLOCCO)
# ============================================================
echo -e "${YELLOW}[3/3] Patch cryptonote_tx_utils.cpp — creazione blocco${NC}"

TX_UTILS="src/cryptonote_core/cryptonote_tx_utils.cpp"

# Backup
cp "$TX_UTILS" "${TX_UTILS}.bak.specials" 2>/dev/null || true

# Rimuovi vecchie patch se esistono
sed -i '/MEVA_DESY_REWARD/d' "$TX_UTILS" 2>/dev/null || true
sed -i '/MEVA_DESY_BIRTHDAY/d' "$TX_UTILS" 2>/dev/null || true

if grep -q "MEVA_SPECIALS_V2" "$TX_UTILS" 2>/dev/null; then
    echo -e "  ${GREEN}✓ Già patchato (v2)${NC}"
else
    # 3a. Aggiungi include
    if ! grep -q "mevacoin_specials.h" "$TX_UTILS"; then
        LAST_INCLUDE=$(grep -n "^#include" "$TX_UTILS" | tail -1 | cut -d: -f1)
        sed -i "${LAST_INCLUDE}a\\
#include \"mevacoin_specials.h\"  // MEVA_SPECIALS_V2" "$TX_UTILS"
        echo -e "  ${GREEN}✓ #include aggiunto${NC}"
    fi

    # 3b. Trova dove la reward viene calcolata
    FEE_LINE=$(grep -n "block_reward += fee" "$TX_UTILS" | head -1 | cut -d: -f1)

    if [ -z "$FEE_LINE" ]; then
        FEE_LINE=$(grep -n "+= fee" "$TX_UTILS" | head -1 | cut -d: -f1)
    fi

    if [ -z "$FEE_LINE" ]; then
        # Ultima risorsa: dopo get_block_reward
        FEE_LINE=$(grep -n "get_block_reward" "$TX_UTILS" | head -1 | cut -d: -f1)
        if [ -n "$FEE_LINE" ]; then
            FEE_LINE=$((FEE_LINE + 2))
        fi
    fi

    if [ -n "$FEE_LINE" ]; then
        sed -i "${FEE_LINE}i\\
\\
  // MEVA_SPECIALS_V2 — Applica bonus alla creazione del blocco\\
  {\\
    time_t now = time(nullptr);\\
    uint64_t adjusted = meva::calculate_final_reward(block_reward, height, now);\\
\\
    if (adjusted != block_reward) {\\
      const char* evt_name = nullptr;\\
      const char* evt_msg = nullptr;\\
      int bp = 0; int bm = 1;\\
\\
      if (meva::find_special_date(now, bp, evt_name, evt_msg)) {\\
        LOG_PRINT_L0(\"[MEVA] \" << evt_name << \" — \" << evt_msg);\\
        if (bp > 0) LOG_PRINT_L0(\"[MEVA] Reward bonus: +\" << bp << \"%\");\\
      }\\
\\
      if (meva::find_special_block(height, bm, evt_name, evt_msg)) {\\
        LOG_PRINT_L0(\"[MEVA] \" << evt_name << \" — \" << evt_msg);\\
        LOG_PRINT_L0(\"[MEVA] Reward: x\" << bm);\\
      }\\
\\
      block_reward = adjusted;\\
    }\\
  }" "$TX_UTILS"
        echo -e "  ${GREEN}✓ Creazione blocco con bonus aggiunta${NC}"
    else
        echo -e "  ${RED}✗ ERRORE: non trovato punto di inserimento${NC}"
        echo -e "  ${RED}  Patch manuale necessaria in construct_miner_tx${NC}"
    fi
fi

echo ""

# ============================================================
# VERIFICA
# ============================================================
echo -e "${YELLOW}Verifica file...${NC}"
echo ""

if [ -f "src/cryptonote_core/mevacoin_specials.h" ]; then
    echo -e "  ${GREEN}✓${NC} mevacoin_specials.h esiste"
else
    echo -e "  ${RED}✗${NC} mevacoin_specials.h NON trovato"
fi

if grep -q "MEVA_SPECIALS_V2" "$BLOCKCHAIN"; then
    echo -e "  ${GREEN}✓${NC} blockchain.cpp patchato"
else
    echo -e "  ${RED}✗${NC} blockchain.cpp NON patchato"
fi

if grep -q "MEVA_SPECIALS_V2" "$TX_UTILS"; then
    echo -e "  ${GREEN}✓${NC} cryptonote_tx_utils.cpp patchato"
else
    echo -e "  ${RED}✗${NC} cryptonote_tx_utils.cpp NON patchato"
fi

echo ""

# ============================================================
# RIEPILOGO
# ============================================================
echo -e "${CYAN}============================================================${NC}"
echo -e "${CYAN}  INSTALLAZIONE COMPLETATA${NC}"
echo -e "${CYAN}============================================================${NC}"
echo ""
echo -e "  ${GREEN}ATTIVI:${NC}"
echo -e "    📅 18 Dicembre (ogni anno): +25% reward"
echo -e "       ${CYAN}\"Buon compleanno Desy! Con amore, dalla blockchain MevaCoin.\"${NC}"
echo -e ""
echo -e "    🏆 Blocco #20 (una volta): reward x100"
echo -e "       ${CYAN}\"Reward del Fondatore: fondo promozione MevaCoin\"${NC}"
echo ""
echo -e "  ${YELLOW}COMING SOON (12 date + 6 blocchi pronti da attivare):${NC}"
echo -e "    Per attivare: ${CYAN}nano src/cryptonote_core/mevacoin_specials.h${NC}"
echo -e "    Togli // dalla riga desiderata e ricompila."
echo ""
echo -e "  ${YELLOW}Backup creati:${NC}"
echo -e "    blockchain.cpp.bak.specials"
echo -e "    cryptonote_tx_utils.cpp.bak.specials"
echo ""
echo -e "  ${YELLOW}PROSSIMO PASSO:${NC}"
echo -e "    ${CYAN}make -j\$(nproc)${NC}"
echo ""
