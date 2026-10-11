// Copyright (c) 2024, The Mevacoin Project
// mevatrust_consensus.h
//
// I numeri del 3% in un posto solo.
//
// Prima questi valori vivevano in tre posti diversi, ognuno con la sua copia: il
// 3% scritto a mano in compute_pool_balance_from_chain(), il default 240 nel
// RewardDistributor e un altro 240 nel MevaTrustManager. Nessuno li confrontava.
//
// E' una bad idea che si paga in silenzio. Se il test asserisce sul 3% e la
// produzione usa un altro numero, il test è verde e il consenso è sbagliato: non
// è che il test non copra la regola, è che copre una regola diversa da quella che
// gira. Diventa una divergenza che si vede solo quando un miner onesto produce un
// blocco che i validatori rifiutano, cioe' a una catena ferma.
//
// Quindi: un file, due costanti, e la produzione le usa come fonte. Se un giorno il
// 3% cambiasse, si cambia qui e i test che lo asseriscono smettono di essere
// veri nello stesso momento in cui smettono di essere giusti.

#pragma once

#include <cstdint>

// Lo spazio di nomi e' cryptonote::mevatrust, non mevatrust: quest'ultimo esiste
// gia' come sotto-spazio di cryptonote e li contiene dentro. Dichiarando qui un
// namespace mevatrust globale, dai sorgenti che sono gia' dentro cryptonote il
// nome mevatrust::consensuswould risolvere a cryptonote::mevatrust::consensus,
// che non e' questo, e l'errore che si legge e' fuorviante.
namespace cryptonote {
namespace mevatrust {
namespace consensus {

/// Quota del reward di blocco destinata ai nodi, in percentuale.
/// 3 = i nodi ricevono il 3%, il miner il 97% meno cio' che va ai nodi.
constexpr uint32_t NODE_REWARD_PERCENT = 3;

/// Distanza in blocchi fra due distribuzioni. 240 = circa un'ora a 4 minuti/blocco.
///
/// Non e' arbitrario: e' la granularita' con cui il protocollo paga. Ridurlo
/// rende i pagamenti piu' frequenti e le quote piu' piccole, e con piu' occasioni
/// di arrotondare a zero un nodo che aveva diritto a qualcosa.
constexpr uint32_t DISTRIBUTION_PERIOD_BLOCKS = 240;

/// Soglia di punteggio sotto la quale un nodo registrato non riceve nulla.
/// Sotto questa soglia il nodo non e' "in attesa": e' considerato non utile, e la
/// differenza e' deliberata perche' un nodo che risponde a caso e' rumore.
constexpr float MIN_SCORE_FOR_REWARD = 0.5f;

// ════════════════════════════════════════════════════════════════════════════
// Regola 3% v3 — l'uptime come gate, non come voce di uno score
// ════════════════════════════════════════════════════════════════════════════
//
// Fin qui la ripartizione era proporzionale a uno score composto da quattro voci
// (uptime 0.40, sync 0.30, responsiveness 0.20, activity 0.10) con soglia 0.5. La
// forma e' elegante. Il problema e' che lo score non era alimentato: in produzione
// nessuna delle quattro strutture che lo compongono veniva scritta, tranne il sync.
// Il tetto raggiungibile era 0.30 (0.40 con il bonus validator), quindi nessun nodo
// superava la soglia e la regola non ha mai pagato nessuno.
//
// La v3 sostituisce lo score con una sola grandezza misurata sulla catena: quanti
// secondi di uptime sono ACCREDITATI a un nodo dentro una finestra scorrevole. Il
// nodo e' eleggibile sopra i due giorni, e la sua quota cresce con i giorni fino a
// un tetto. Nessun altro canale decide chi e' pagato.
//
// ── Perche' i secondi si contano sui timestamp dei blocchi ───────────────────
//
// L'uptime dichiarato dal nodo (campo `timestamp` del 0xA5) e' una firma, non una
// prova: un nodo puo' firmare timestamp = adesso + 10 anni e accreditarsi dieci anni
// di uptime con un solo messaggio. Il credito si conta quindi sui timestamp dei
// BLOCCHI che contengono i 0xA5 accettati: sono dati che il consenso gia' valida,
// e che due nodi onesti ricostruiscono identici. Il timestamp dichiarato viene
// ignorato per il calcolo della quota e resta solo come informazione per l'explorer.

/// Secondi di uptime accreditati necessari per essere eleggibili: due giorni.
///
/// Non e' un numero di blocchi perche' la richiesta e' in giorni, e blocchi e
/// giorni divergono appena il tempo di blocco si muove. La conversione resta
/// comunque deterministica perche' i secondi vengono dai timestamp dei blocchi.
constexpr uint64_t MIN_ELIGIBLE_UPTIME_SECONDS = 2 * 24 * 60 * 60;

/// Tetto dei giorni che contano ai fini della quota: novanta.
///
/// Serve perche' senza tetto il nodo piu' longevo prende sempre piu' e il 3% si
/// concentra su una sola macchina, il peggiore esito possibile per una regola
/// pensata per incentivare la rete piu' che per premiare un singolo nodo. Il
/// merito dell'anzianita' non si perde: resta nei badge, che sono simbolici e non
/// incidono sul denaro.
constexpr uint64_t UPTIME_WEIGHT_CAP_DAYS = 90;

/// Ampiezza della finestra scorrevole, in blocchi.
///
/// 43200 = 120 giorni a 4 minuti/blocco. Deve essere piu' larga del tetto dei
/// novanta giorni, altrimenti il tetto non lega mai e la finestra diventerebbe essa
/// stessa il limite. E' la finestra a produrre il decadimento: un nodo che si
/// ferma vede il suo credito uscire dalla finestra un giorno alla volta, invece di
/// ripartire da zero.
///
/// Si misura in blocchi e non in secondi perche' l'altezza e' l'unica cosa che il
/// miner e il validatore conoscono con sicurezza insieme: il tempo del blocco che
/// sta per essere costruito non e' ancora deciso quando le quote si calcolano.
constexpr uint64_t UPTIME_WINDOW_BLOCKS = 43200;

/// Secondi accreditabili da una singola coppia di campioni consecutivi: dodici ore.
///
/// Il credito e' il delta fra due timestamp di blocco consecutivi del nodo, ma
/// limitato: se un nodo spariste per un mese e torna, il salto e' enorme e
/// accreditarglielo intero equivarrebbe a premiarlo anche per il mese in cui non
/// c'era. Il limite trasforma "assente" in "non accreditato" senzache serva sapere
/// quando e' tornato.
///
/// Dodici ore e' anche il ritmo minimo di emissione dei 0xA5 che un nodo deve
/// tenere per non perdere quote: due al giorno sono abbastanza per dimostrare
/// presenza e abbastanza rare da non affollare la mempool.
constexpr uint64_t MAX_UPTIME_CREDIT_GAP_SECONDS = 12 * 60 * 60;

// ════════════════════════════════════════════════════════════════════════════
// Anti-Sybil Fase 2 — la prova di possesso dei dati della catena
// ════════════════════════════════════════════════════════════════════════════
//
// La regola 3% paga i nodi eleggibili. Essere eleggibili finora richiedeva solo
// di registrarsi on-chain (0xA0) e di firmare uptime (0xA5). Niente in quella
// catena di eventi obbliga chi firma a POSSIEDERE la catena: uno script senza
// dati puo' firmare 0xA5 per mille node_id diversi e incassare mille quote,
// perche' una firma non costa nulla.
//
// La Fase 2 aggiunge una condizione: per essere eleggibile, un nodo deve aver
// dimostrato di saper rispondere, nel recente passato, a una domanda su un
// blocco passato. La domanda non e' scelta dal nodo: l'altezza bersaglio e'
// derivata da (node_id, anchor_height) con chain_proof_target(), con una
// profondita' minima che impedisce di rispondere senza avere il blocco. Un
// nodo vero risponde in un accesso; un firmatario senza catena deve procurarsi
// il blocco, cioe' diventare (o agganciarsi a) un nodo che ce l'ha.
//
// Limite dichiarato: questo NON ferma un singolo nodo vero che apre molte
// identita' — quel nodo la catena ce l'ha e risponde per tutte. Quello lo
// affronta la Fase 3 (bond economico). La Fase 2 alza il costo di chi non ha
// alcun nodo, che e' il caso piu' economico da attaccare.

/// Profondita' minima della domanda, in blocchi. Il bersaglio sta sempre almeno
/// 100 blocchi indietro rispetto all'anchor: abbastanza perche' un nodo appena
/// partito non possa rispondere con l'ultimo blocco, ma dentro la storia che
/// ogni nodo sincronizzato conserva.
constexpr uint64_t CHAIN_PROOF_MIN_DEPTH = 100;

/// Ampiezza dell'offset casuale-deterministico, in blocchi. Il bersaglio e' a
/// MIN_DEPTH + (hash % SPAN) blocchi indietro: entro ~1000 blocchi di profondita'
/// (~2.7 giorni a 4 minuti/blocco) la risposta non e' precomputabile e resta
/// nella parte di catena che nessun nodo pota.
constexpr uint64_t CHAIN_PROOF_SPAN = 1000;

/// Ritardo massimo fra l'anchor dichiarato e il blocco che contiene la prova.
/// Oltre, la prova e' stantia: impedisce di rigiocare all'infinito una risposta
/// calcolata una volta sola. Un'ora di blocchi (60 a 4 minuti) da' margine per
/// la mempool senza lasciare finestre larghe per il replay.
constexpr uint64_t CHAIN_PROOF_MAX_ANCHOR_LAG = 60;

/// Finestra entro cui una prova deve essere stata registrata perche' il nodo
/// resti eleggibile. Riusa UPTIME_WINDOW_BLOCKS: se un nodo smette di produrre
/// prove, perde l'eleggibilita' con lo stesso orizzonte con cui perde l'uptime,
/// cosi' non esistono due orologi di decadimento diversi da riconciliare.
constexpr uint64_t CHAIN_PROOF_VALIDITY_BLOCKS = UPTIME_WINDOW_BLOCKS;

// ════════════════════════════════════════════════════════════════════════════
// Anti-Sybil Fase 3 — Bond economico per registrazione nodo
// ════════════════════════════════════════════════════════════════════════════
//
// Non-refundable deposit per registrazione (sunk cost). Il bond viene inviato
// al network fund (0xC0) e non viene mai restituito. Ciò rende costoso
// creare molte identità da un singolo nodo reale.
constexpr uint64_t NODE_REGISTRATION_BOND_ATOMIC = 100ULL * 1'000'000'000'000ULL; // 100 MVC

} // namespace consensus
} // namespace mevatrust
} // namespace cryptonote
