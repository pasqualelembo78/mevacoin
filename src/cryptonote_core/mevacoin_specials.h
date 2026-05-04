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


// Controlla se mostrare il messaggio commemorativo
// - Primo blocco del giorno: SEMPRE
// - Poi: ogni 10 blocchi (non ad ogni singolo blocco)
// Il REWARD bonus viene sempre applicato, solo il MESSAGGIO e' limitato
inline bool should_show_commemorative_message(time_t timestamp) {
    static int last_shown_day = -1;
    static int last_shown_year = -1;
    static int blocks_since_msg = 0;

    struct tm* t = gmtime(&timestamp);
    if (!t) return true;

    int day = t->tm_yday;
    int year = t->tm_year;

    if (day != last_shown_day || year != last_shown_year) {
        // Primo blocco di un nuovo giorno commemorativo
        last_shown_day = day;
        last_shown_year = year;
        blocks_since_msg = 0;
        return true;  // SEMPRE al primo blocco
    }

    blocks_since_msg++;

    // Mostra ogni 10 blocchi
    if (blocks_since_msg >= 10) {
        blocks_since_msg = 0;
        return true;
    }

    return false;
}

} // namespace meva
