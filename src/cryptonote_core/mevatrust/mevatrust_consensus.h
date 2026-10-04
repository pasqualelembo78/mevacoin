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

} // namespace consensus
} // namespace mevatrust
} // namespace cryptonote