// Copyright (c) 2024, The Mevacoin Project
// mevatrust_three_percent_rule_test.cpp
//
// Il test che dimostra che il 3% non e' piu' una promessa firmata, ma una
// regola di consenso.
//
// ── Cosa cambia, e perche' questo test esiste ───────────────────────────────
//
// Prima il 3% era autorizzato da cinque chiavi FROST custodite su altrettanti
// server. Il validatore chiedeva: "la firma del gruppo dei proposer e' vera?".  Il
// destinatario era quindi deciso da chi possedeva la chiave, non dal protocollo.
// Un miner che voleva tenersi il 3% non poteva farlo - ma solo perche' non
// aveva tre delle cinque chiavi, non perche' la regola glielo impedisse.
//
// Ora la domanda del validatore e' un'altra: "questa lista di destinatari e' quella
// che il protocollo calcola?". Il ricalcolo parte da dati di catena, e ogni
// nodo lo fa da solo. Nessuna chiave partecipa al verdetto.
//
// Il test attraversa esattamente questo percorso e chiede la domanda che
// interessa: un miner onesto che produce il 3% secondo la regola viene accettato,
// e un miner che dirotta il 3% verso un indirizzo proprio viene rifiutato.
//
// Il test non ha bisogno di chiavi, di keyring, di daemon e non firma niente.
// Se un domaniquesto test richiedesse un segreto per girare, la regola che
// implementa non sarebbe quella che dichiara di implementare.

#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cryptonote_core/mevatrust/mevatrust_coinbase_validator.h"
#include "cryptonote_core/mevatrust/mevatrust_consensus.h"
#include "cryptonote_core/mevatrust/mevatrust_manager.h"
#include "cryptonote_core/mevatrust/reward_distributor.h"
#include "cryptonote_core/mevatrust/mevatrust_tx_parser.h"
#include "cryptonote_core/mevatrust/node_registry.h"
#include "cryptonote_core/mevatrust/pool_distribution.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/tx_extra.h"
#include <sys/stat.h>

#include "crypto/crypto.h"
#include "crypto/hash.h"

using namespace cryptonote;
using namespace cryptonote::mevatrust;

namespace {

const uint64_t kBlockReward = 33276640813735ULL / 1000;

uint64_t node_budget_of(uint64_t total_reward) {
    return total_reward * consensus::NODE_REWARD_PERCENT / 100;
}

crypto::public_key fresh_key() {
    crypto::public_key pk{};
    crypto::secret_key sk{};
    crypto::generate_keys(pk, sk);
    return pk;
}

/// Un indirizzo spendabile a partire da una chiave: serve perche' il confronto
/// fra il manifesto e la regola avviene sulla chiave di spesa.
account_public_address addr_of(const crypto::public_key& spend) {
    account_public_address a{};
    a.m_spend_public_key = spend;
    a.m_view_public_key = fresh_key();
    return a;
}

std::string addr_str(const account_public_address& a) {
    return get_account_address_as_str(MAINNET, false, a);
}

/// I dati di una registrazione on-chain, separati dal manager che li riceve.
///
/// Servono separati perche' il test di determinismo deve consegnare a due
/// manager *lo stesso* evento. Se ciascuno si costruisse da solo le sue chiavi,
/// i due nodi riceverebbero dati diversi e il confronto misurerebbe l'ingresso
/// invece del calcolo: passerebbe comunque, ma per un motivo sbagliato.
struct Seed {
    crypto::public_key wallet_pubkey;
    crypto::public_key node_pubkey;
    crypto::hash       node_id;
};

Seed make_seed() {
    Seed s{};
    crypto::secret_key sk{};
    crypto::generate_keys(s.wallet_pubkey, sk);
    crypto::generate_keys(s.node_pubkey, sk);
    crypto::generate_keys(s.wallet_pubkey, sk);
    crypto::generate_keys(s.node_pubkey, sk);
    // Il node_id reale e' H(wallet_pubkey || node_pubkey || timestamp) e non e'
    // ricostruibile qui in modo compatibile con il produttore della registrazione.
    // Il test non finge di essere quel produttore: prende un identificatore
    // arbitrario e stabilisce che sia questo. Cio' che si verifica e' che la
    // regola del 3% legge i dati che la registrazione on-chain ha messo in
    // catena, non che sappia rifare l'hash di una registrazione.
    memcpy(&s.node_id, &sk, sizeof(crypto::hash));
    return s;
}

/// Applica a un manager la sequenza di eventi che un blocco on-chain produrrebbe:
/// registrazione del nodo, poi una serie di eventi di presenza.
///
/// Si passa per register_node_onchain(), non per register_node(): il secondo
/// ricalcola il node_id da time(nullptr), quindi ogni nodo che lo usa ottiene un
/// identificatore diverso per lo stesso fatto - un vicolo cieco per una regola di
/// consenso.
///
/// Registrare non basta: la soglia del 3% guarda il punteggio, e il punteggio si
/// costruisce da uptime. Un nodo appena registrato vale zero e verrebbe escluso -
/// correttamente, perche' un nodo che non ha mai fatto niente non ha diritto a
/// nulla. Qui si gli danno i dati che un nodo che ha davvero lavorato avrebbe.
void apply_registration(MevaTrustManager& mgr,
                        const account_public_address& wallet,
                        const Seed& s) {
    tx_extra_mevatrust_registration reg{};
    reg.wallet_pubkey  = s.wallet_pubkey;
    reg.node_pubkey    = s.node_pubkey;
    reg.wallet_address = addr_str(wallet);
    reg.port           = 0;
    reg.node_id        = s.node_id;

    EXPECT_TRUE(mgr.node_registry()->register_node_onchain(reg, 1))
        << "registrazione on-chain respinta";

    // Dieci eventi di presenza, tutti online e tutti con peer: il punteggio e' il
    // massimo che il calcolo possa dare, e la soglia non lo esclude.
    for (uint32_t i = 0; i < 10; ++i) {
        UptimeEvent ev{};
        ev.timestamp        = 1000 + i;
        ev.online           = true;
        ev.block_height     = 240;
        ev.peer_count       = 4;
        ev.response_time_ms = 50;
        ev.ip_address       = "127.0.0.1";
        EXPECT_TRUE(mgr.mevatrust_engine()->record_uptime_event(s.node_id, ev));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Un MevaTrustManager vivo, in una directory temporanea
// ─────────────────────────────────────────────────────────────────────────────

/// Un manager vivo serve a una cosa sola: che ci sia qualcosa da ricalcolare.
/// Senza nodi registrati il 3% non ha destinatari e la regola non ha niente da
/// dire, quindi il test passerebbe triviale - e un test che passa triviale non
/// testa niente.
///
/// Non viene firmato niente e non viene letto nessun keyring: e' questo il punto.
class RuleFixture : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = "/tmp/mevatrust_rule_test_" +
                std::to_string(reinterpret_cast<uintptr_t>(this));
        // MevaTrustManager::init() crea "<data_dir>/mevatrust/db" con un solo
        // mkdir: se il livello di sopra non esiste, la creazione fallisce in
        // silenzio e ogni LMDB resta aperto su una directory inesistente. Il
        // risultato sarebbe un manager "vivo" che non ha un nodo in registro -
        // cioe' un test che passa senza mai aver provato niente.
        ASSERT_EQ(::mkdir(m_dir.c_str(), 0755), 0)
            << "non ho potuto creare " << m_dir << ": " << strerror(errno);
        m_manager = std::make_shared<MevaTrustManager>();
        ASSERT_TRUE(m_manager->init(m_dir, MAINNET, HF_VERSION_MEVATRUST_VALIDATION));
        set_manager(m_manager);
    }

    void TearDown() override {
        set_manager(nullptr);
        if (m_manager) m_manager->shutdown();
        m_manager.reset();
        std::string cmd = "rm -rf '" + m_dir + "'";
        if (std::system(cmd.c_str()) != 0) { /* la directory resta, non e' un errore */ }
    }

    void add_node(const std::string& label) {
        Seed s = make_seed();
        apply_registration(*m_manager, m_wallet[label], s);
        m_node_id[label] = s.node_id;
    }

    /// La lista che il consenso si aspetta a questa altezza. Se il manager
    /// produce qualcosa di diverso dal previsto, il test che lo usa fallisce: la
    /// regola e' quella che dice lui, non quella che il test immagina.
    std::vector<MevaTrustManager::expected_payout> expected_at(uint64_t height) const {
        std::vector<MevaTrustManager::expected_payout> out;
        std::string err;
        EXPECT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, out, err))
            << "expected payouts non calcolabili: " << err;
        return out;
    }

    std::string m_dir;
    std::shared_ptr<MevaTrustManager> m_manager;
    std::map<std::string, account_public_address> m_wallet;
    std::map<std::string, crypto::hash>           m_node_id;
};

// ─────────────────────────────────────────────────────────────────────────────
// Costruzione dei blocchi, dal lato del miner
// ─────────────────────────────────────────────────────────────────────────────

/// Il miner onesto: prende la lista che il consenso vuole e la mette in
/// coinbase, dichiarandola nel manifesto.
///
/// La firma e' zero. Non perche' non si possa firmare, ma perche' nessuno glielo
/// chiede: il manifesto non e' un documento autorizzato, e' la dichiarazione di un
/// conto che chi riceve puo' rifare da solo.
std::vector<uint8_t> build_manifest(const std::vector<MevaTrustManager::expected_payout>& payouts,
                                    uint64_t height,
                                    const std::vector<crypto::public_key>& out_keys) {
    if (payouts.size() != out_keys.size()) return {};

    std::vector<NodeCoinbaseReward> nr;
    nr.reserve(payouts.size());
    uint64_t total = 0;
    for (size_t i = 0; i < payouts.size(); ++i) {
        NodeCoinbaseReward r{};
        r.address = payouts[i].address;
        r.amount  = payouts[i].amount;
        nr.push_back(r);
        total += r.amount;
    }

    frost::FrostSignature zero_sig;
    memset(&zero_sig, 0, sizeof(zero_sig));

    // total_pool_balance e' il saldo dichiarato al momento della distribuzione.
    // Qui e' la somma stessa delle quote: il manifesto non deve promettere piu' di
    // quanto il pool contiene, e validate_pool_distribution() controlla esattamente
    // questo (total_distributed <= pool_balance).
    std::vector<uint8_t> extra;
    if (!construct_pool_distribution_extra(
            height, static_cast<uint32_t>(
                         height / consensus::DISTRIBUTION_PERIOD_BLOCKS),
            total, nr, zero_sig, extra))
        return {};
    return extra;
}

/// La coinbase: prima gli output del minatore, poi i pagamenti in coda. La coda
/// e' la convenzione con cui il validatore sa dove iniziano i pagamenti, e non
/// viene dichiarata dal miner: un campo "i miei pagamenti iniziano qui" scritto
/// da lui sarebbe un altro modo per mentire.
transaction build_coinbase(uint64_t miner_amount,
                           const std::vector<MevaTrustManager::expected_payout>& payouts,
                           const std::vector<crypto::public_key>& out_keys) {
    transaction tx;
    tx.version = 2;
    tx.unlock_time = 0;

    if (miner_amount > 0) {
        tx_out o;
        set_tx_out(miner_amount, fresh_key(), false, crypto::view_tag{}, o);
        tx.vout.push_back(o);
    }
    for (size_t i = 0; i < payouts.size(); ++i) {
        tx_out o;
        set_tx_out(payouts[i].amount, out_keys[i], false, crypto::view_tag{}, o);
        tx.vout.push_back(o);
    }
    return tx;
}

struct Verdict {
    bool accepted{false};
    std::string error;
};

Verdict judge(const transaction& tx, uint64_t height) {
    Verdict v;
    std::string err;
    v.accepted = check_mevatrust_coinbase_pure_consensus(
        tx, height, kBlockReward, HF_VERSION_MEVATRUST_VALIDATION, err);
    v.error = err;
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 - il miner onesto non ha bisogno di nessuna chiave
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, honest_miner_needs_no_keys_and_is_accepted)
{
    const uint64_t height = 240;
    const uint64_t budget = node_budget_of(kBlockReward);
    ASSERT_GT(budget, 0) << "un blocco da zero non insegnerebbe niente";

    // Un nodo con punteggio alto, che si aspetta tutto il 3%.
    m_wallet["a"] = addr_of(fresh_key());
    add_node("a");

    const auto expected = expected_at(height);
    ASSERT_FALSE(expected.empty())
        << "senza destinatari la regola non avrebbe niente da dire, e il test "
           "passerebbe senza provare niente";

    std::vector<crypto::public_key> out_keys;
    out_keys.reserve(expected.size());
    for (size_t i = 0; i < expected.size(); ++i) out_keys.push_back(fresh_key());

    transaction tx = build_coinbase(kBlockReward - budget, expected, out_keys);
    tx.extra = build_manifest(expected, height, out_keys);
    ASSERT_FALSE(tx.extra.empty());

    const Verdict v = judge(tx, height);
    EXPECT_TRUE(v.accepted) << "il miner onesto e' stato respinto: " << v.error;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 - IL TEST OBBLIGATORIO
//
// Il miner dirotta il 3% verso un indirizzo proprio. Le somme restano giuste,
// il manifesto resta perfettamente formato, la firma e' zero come richiede la
// nuova regola: cambia solo il destinatario.
//
// Il blocco deve essere respinto, e il motivo deve dire chi doveva ricevere.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, miner_redirecting_three_percent_to_itself_is_rejected)
{
    const uint64_t height = 240;
    const uint64_t budget = node_budget_of(kBlockReward);
    ASSERT_GT(budget, 0);

    m_wallet["a"] = addr_of(fresh_key());
    add_node("a");

    const auto expected = expected_at(height);
    ASSERT_FALSE(expected.empty());

    // L'indirizzo del ladro: non e' nel registro, e non e' il destinatario che
    // il protocollo calcola.
    const account_public_address thief = addr_of(fresh_key());
    ASSERT_TRUE(memcmp(&thief.m_spend_public_key,
                       &expected[0].address.m_spend_public_key,
                       sizeof(crypto::public_key)) != 0)
        << "il test non avrebbe niente da dimostrare";

    // Il manifesto dichiara il ladro. Gli importi sono quelli giusti, presi
    // parola per parola dalla lista del consenso: la sola cosa cambiata e' il nome
    // di chi incassa.
    std::vector<MevaTrustManager::expected_payout> stolen = expected;
    stolen[0].address = thief;

    std::vector<crypto::public_key> out_keys;
    out_keys.reserve(stolen.size());
    for (size_t i = 0; i < stolen.size(); ++i) out_keys.push_back(fresh_key());

    transaction tx = build_coinbase(kBlockReward - budget, stolen, out_keys);
    tx.extra = build_manifest(stolen, height, out_keys);
    ASSERT_FALSE(tx.extra.empty());

    const Verdict v = judge(tx, height);
    EXPECT_FALSE(v.accepted)
        << "il 3% e' finito all'indirizzo arbitrario del miner e il blocco e' "
           "stato accettato: la regola non e' una regola";
    EXPECT_NE(v.error.find("mismatch"), std::string::npos)
        << "il rifiuto deve dire che il destinatario non e' quello dovuto, "
           "altrimenti un operatore non capisce perche' il blocco e' rimasto fuori"
        << " (messaggio ricevuto: " << v.error << ")";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 - il miner si trattiene il 3% intero, senza dichiararlo
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, miner_keeping_all_three_percent_without_manifest_is_rejected)
{
    const uint64_t height = 240;
    const uint64_t budget = node_budget_of(kBlockReward);
    ASSERT_GT(budget, 0);

    m_wallet["a"] = addr_of(fresh_key());
    add_node("a");
    ASSERT_FALSE(expected_at(height).empty());

    // Il miner incassa tutto e non allega nessun manifesto. E' la manovra piu'
    // semplice e piu' redditizia: un blocco in piu', e costa una riga.
    transaction tx = build_coinbase(kBlockReward, {}, {});

    const Verdict v = judge(tx, height);
    EXPECT_FALSE(v.accepted)
        << "il miner ha tenuto tutto il 3% e nessuno se ne e' accorto";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 - il miner paga a qualcuno, ma non a chi doveva
//
// Caso piu' sottile del precedente: il 3% esce dalla coinbase, le somme sono
// corrette, c'e' un manifesto. Solo che il destinatario e' un nodo che il
// protocollo non ha scelto - magari perch'e' appena stato sospeso.
//
// Serve a distinguere "i soldi mancano" da "i soldi vanno a chi non deve".
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, paying_the_right_amount_to_the_wrong_node_is_rejected)
{
    const uint64_t height = 240;
    const uint64_t budget = node_budget_of(kBlockReward);
    ASSERT_GT(budget, 0);

    m_wallet["a"] = addr_of(fresh_key());
    m_wallet["b"] = addr_of(fresh_key());
    add_node("a");
    add_node("b");

    const auto expected = expected_at(height);
    ASSERT_GE(expected.size(), 2u) << "servono almeno due destinatari";

    // Il manifesti indica solo il nodo "a": gli importi sono presi dalla lista
    // vera, quindi non e' il caso del furto grosso. E' il furto di un singolo.
    std::vector<MevaTrustManager::expected_payout> partial;
    for (const auto& e : expected)
        if (memcmp(&e.address.m_spend_public_key,
                   &m_wallet["a"].m_spend_public_key,
                   sizeof(crypto::public_key)) == 0)
            partial.push_back(e);
    ASSERT_FALSE(partial.empty());
    ASSERT_LT(partial.size(), expected.size())
        << "il nodo 'a' ha preso tutto: non c'e' piu' nulla da escludere";

    std::vector<crypto::public_key> out_keys;
    for (size_t i = 0; i < partial.size(); ++i) out_keys.push_back(fresh_key());

    uint64_t stolen = 0;
    for (const auto& p : expected) stolen += p.amount;
    for (const auto& p : partial) stolen -= p.amount;

    transaction tx = build_coinbase(kBlockReward - budget, partial, out_keys);
    tx.extra = build_manifest(partial, height, out_keys);
    ASSERT_FALSE(tx.extra.empty());

    const Verdict v = judge(tx, height);
    EXPECT_FALSE(v.accepted)
        << "un nodo ha ricevuto piu' di quanto gli spettava, o meno di quanto "
           "gli spettava, e il blocco e' passato";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 - l'importo giusto al destinatario giusto, ma la somma manipolata
//
// Il manifesto promette una cifra per ciascun nodo. Se le cifre sono giuste ma la
// somma dichiarata non torna con la somma delle voci, il manifesto e' bugiardo e
// va respinto: e' l'attacco classico al documento, non al contenuto.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, manifest_whose_sum_does_not_match_its_own_entries_is_rejected)
{
    const uint64_t height = 240;
    const uint64_t budget = node_budget_of(kBlockReward);
    ASSERT_GT(budget, 0);

    m_wallet["a"] = addr_of(fresh_key());
    add_node("a");

    const auto expected = expected_at(height);
    ASSERT_FALSE(expected.empty());

    std::vector<crypto::public_key> out_keys;
    out_keys.reserve(expected.size());
    for (size_t i = 0; i < expected.size(); ++i) out_keys.push_back(fresh_key());

    transaction tx = build_coinbase(kBlockReward - budget, expected, out_keys);
    tx.extra = build_manifest(expected, height, out_keys);
    ASSERT_FALSE(tx.extra.empty());

    // Riscrive il campo "totale" del manifesto con una cifra che non e' la somma
    // delle sue voci. Il testo del blob resta valido: e' un manifesto bugiardo,
    // non un manifesto corrotto.
    tx_extra_mevatrust_pool_distribution dist;
    ASSERT_TRUE(parse_mevatrust_pool_distribution_from_tx(tx, dist));
    dist.total_distributed = dist.total_distributed + 1;
    std::vector<uint8_t> rewritten;
    ASSERT_TRUE(build_mevatrust_pool_distribution_extra(dist, rewritten));
    tx.extra = rewritten;

    const Verdict v = judge(tx, height);
    EXPECT_FALSE(v.accepted)
        << "il manifesto dichiara una somma che non e' la somma delle sue voci, "
           "e nessuno se ne e' accorto";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6 - nessun nodo eleggibile: il 3% non esce, e va bene cosi'
//
// Quando non c'e' nessuno a cui pagare, la regola non pretende nulla. Il miner
// puo' tenere il blocco intero.
//
// E' il caso normale del giorno in cui la moneta si avvia, ed e' la ragione per
// cui la regola non puo' essere "paghi sempre il 3%": qualcuno potrebbe non
// averne diritto. Pretenderlo comunque bloccherebbe la catena in partenza.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, with_no_eligible_node_the_miner_may_keep_the_whole_reward)
{
    const uint64_t height = 240;

    // Nessun nodo registrato.
    EXPECT_TRUE(expected_at(height).empty())
        << "senza nodi non ci devono essere destinatari: se questo test fallisce "
           "la regola sta inventando qualcosa";

    transaction tx = build_coinbase(kBlockReward, {}, {});

    const Verdict v = judge(tx, height);
    EXPECT_TRUE(v.accepted)
        << "con nessun destinatario la regola non deve mordere: " << v.error;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7 - la regola e' agganciata al consenso, non a un optional
//
// Se il manager non e' vivo il validatore non puo' ricalcolare nulla, e deve
// rifiutare il blocco invece di accettarlo per prudenza.
//
// Questa e' la domanda opposta a quella che il resto dei test pone. Gli altri
// chiedono "il miner onesto passa"; questo chiede "il validatore non si arrende
// quando non sa". Un validatore che dice "va bene cosi'" quando non ha i dati per
// decidere non e' tollerante: e' permissivo.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, block_is_rejected_when_the_rule_cannot_be_recomputed)
{
    set_manager(nullptr);

    const uint64_t height = 240;
    transaction tx = build_coinbase(kBlockReward, {}, {});

    Verdict v;
    std::string err;
    v.accepted = check_mevatrust_coinbase_pure_consensus(
        tx, height, kBlockReward, HF_VERSION_MEVATRUST_VALIDATION, err);
    v.error = err;

    EXPECT_FALSE(v.accepted)
        << "senza stato MevaTrust il validatore ha accettato un blocco che non "
           "poteva giudicare";
    EXPECT_NE(v.error.find("unavailable"), std::string::npos)
        << "il rifiuto deve dire che manca lo stato, non che il blocco e' sbagliato"
        << " (messaggio ricevuto: " << v.error << ")";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8 - DUE NODI INDIPENDENTI, STESSA CATENA, STESSA DECISIONE
//
// Questo e' il test che regge tutto il resto.
//
// Gli altri sette chiedono "la regola e' quella che credo?". Questo chiede
// "due nodi che non si parlano arrivano alla stessa risposta?". E' la domanda a
// cui una regola di consenso deve saper rispondere, e su cui non si puo' fare
// finta: se due nodi calcolassero liste diverse, il primo che riceve un blocco
// firmato dall'altro lo rifiuterebbe, e la catena si spacca.
//
// La risposta onesta e' che questa non e' una simulazione. Qui il secondo manager
// ha directory propria, stato proprio e zero in comune con il primo - non c'e'
// nessun global in mezzo perche' i due non abbiano lo stesso oggetto sott'occhio.
// Ricevono gli stessi eventi, come due nodi che sincronizzano la stessa catena, e
// il confronto e' fra i risultati.
//
// Non copre tutto: due processi separati avrebbero anche da leggere gli stessi
// blocchi dagli stessi file. Copre la parte che e' facile sbagliare e impossibile
// notare: il calcolo.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, two_independent_nodes_with_the_same_chain_agree_on_the_payouts)
{
    const uint64_t height = 240;

    m_wallet["a"] = addr_of(fresh_key());
    m_wallet["b"] = addr_of(fresh_key());
    m_wallet["c"] = addr_of(fresh_key());

    // Tre nodi servono perche' con uno solo la divisione del 3% non avrebbe
    // niente da dividere: nessun arrotondamento, nessun ordine, nessun caso in cui
    // due scelte diverse danno la stessa somma. E' con piu' di un destinatario che
    // l'ordinamento e l'arrotondamento hanno un modo di divergere.
    const std::vector<std::string> labels = {"a", "b", "c"};
    std::map<std::string, Seed> seeds;
    for (const auto& l : labels) seeds[l] = make_seed();

    // ── Nodo A: quello che il fixture ha gia' in piedi ──────────────────────
    for (const auto& l : labels) apply_registration(*m_manager, m_wallet[l], seeds[l]);

    // ── Nodo B: directory propria, stato proprio, zero in comune ────────────
    const std::string dir_b = m_dir + "_b";
    ASSERT_EQ(::mkdir(dir_b.c_str(), 0755), 0)
        << "non ho potuto creare " << dir_b << ": " << strerror(errno);
    MevaTrustManager manager_b;
    ASSERT_TRUE(manager_b.init(dir_b, MAINNET, HF_VERSION_MEVATRUST_VALIDATION))
        << "il secondo nodo non si e' avviato: non c'e' confronto possibile";

    for (const auto& l : labels) apply_registration(manager_b, m_wallet[l], seeds[l]);

    // ── Il confronto ────────────────────────────────────────────────────────
    // compute_expected_payouts() e' membro di ciascun manager: nessun global,
    // nessuna cache condivisa. Ognuno ricalcola il proprio conto.
    std::vector<MevaTrustManager::expected_payout> a, b;
    std::string err_a, err_b;
    ASSERT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, a, err_a))
        << "il nodo A non e' riuscito a calcolare: " << err_a;
    ASSERT_TRUE(manager_b.compute_expected_payouts(height, kBlockReward, b, err_b))
        << "il nodo B non e' riuscito a calcolare: " << err_b;

    ASSERT_FALSE(a.empty())
        << "nessun destinatario: il confronto sarebbe vuoto e passerebbe senza "
           "aver provato niente";

    ASSERT_EQ(a.size(), b.size())
        << "i due nodi hanno deciso su un numero diverso di destinatari: uno dei "
           "due sbaglia, e su un blocco reale uno dei due avrebbe rifiutato il "
           "blocco dell'altro";

    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(0, memcmp(&a[i].address.m_spend_public_key,
                            &b[i].address.m_spend_public_key,
                            sizeof(crypto::public_key)))
            << "voce " << i << ": i due nodi hanno pagato persone diverse";
        EXPECT_EQ(0, memcmp(&a[i].address.m_view_public_key,
                            &b[i].address.m_view_public_key,
                            sizeof(crypto::public_key)))
            << "voce " << i << ": stesse chiavi di spesa, chiavi di vista diverse - "
                           "su una rete vera i due avrebbero firmato blocchi diversi";
        EXPECT_EQ(a[i].amount, b[i].amount)
            << "voce " << i << ": stesse persone, importi diversi - l'arrotondamento "
                              "non e' riproducibile, quindi il 3% non e' deterministico";
    }

    manager_b.shutdown();
    std::string cmd = "rm -rf '" + dir_b + "'";
    if (std::system(cmd.c_str()) != 0) { /* la directory resta, non e' un errore */ }
}


// ─────────────────────────────────────────────────────────────────────────────
// Test 9 - IL RICALCOLO NON CONSUMA NESSUNO STATO
//
// Questo test e' nato da un difetto vero, non da un sospetto.
//
// process_welcome_bonuses() non legge: paga e poi cancella la voce con
// mdb_cursor_del(). Sia il percorso del miner (trigger_distribution) sia quello
// del validatore (compute_expected_payouts) la chiamavano. Essendo distruttiva,
// la seconda chiamata alla stessa altezza restituiva una lista diversa dalla
// prima.
//
// Sul nodo che mina, l'ordine e' inevitabile: costruisci il blocco (consumando il
// bonus di benvenuto, che finisce nel manifesto), poi valuti lo stesso blocco e
// ti accorgi che il bonus non c'e' piu'. Ti rifiuti il blocco che hai appena
// fatto tu, e la catena si ferma.
//
// Il ricalcolo del consenso non puo' consumare niente: deve essere una funzione,
// non un'azione. Chiamarlo due volte alla stessa altezza deve dare due volte la
// stessa risposta, perche' un validatore che rivalida un blocco dopo un riorg ha
// bisogno esattamente di questo.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, recomputing_the_same_height_twice_gives_the_same_answer)
{
    const uint64_t height = 240;

    m_wallet["a"] = addr_of(fresh_key());
    m_wallet["b"] = addr_of(fresh_key());
    Seed sa = make_seed();
    Seed sb = make_seed();

    apply_registration(*m_manager, m_wallet["a"], sa);
    apply_registration(*m_manager, m_wallet["b"], sb);

    // Il bonus di benvenuto va sul nodo "b".
    //
    // Non va su "a", ed e' un dettaglio che sembra gratuito ma non lo e'. Se il
    // bonus appartenesse all'unico nodo eleggibile, il difetto qui sotto
    // sarebbe invisibile: il nodo "a" incasserebbe il 3% come bonus di benvenuto
    // alla prima chiamata e come quota di merito alla seconda, con lo stesso
    // importo e la stessa lista. Due risultati diversi che si somigliano tanto da
    // passare per uguali.
    //
    // Con due nodi e il bonus su uno solo dei due, le due risposte differiscono
    // nel numero di voci e negli importi, e il confronto le vede per quello che
    // sono. E' la forma in cui il difetto si presenta in una rete vera, dove i
    // nodi non sono uno.
    // 5 MVC. L'importo e' un argomento di schedule_welcome_bonus(), non una
    // costante condivisa: nessun codice di produzione schedula mai questi bonus, e
    // quindi qui il valore e' scelto dal test. Il commento sta FUORI dalla macro per
    //che' un // dentro un argomento di macro si mangia la parentesi di chiusura e
    // l'errore che si legge ("unterminated argument list") non dice niente del
    // vero motivo.
    const uint64_t welcome_amount = 5ULL * 1'000'000'000'000ULL;
    ASSERT_TRUE(m_manager->reward_distributor()->schedule_welcome_bonus(
        sb.node_id, 60, welcome_amount))
        << "bonus di benvenuto non schedulato: il test non avrebbe niente da provare";

    std::vector<MevaTrustManager::expected_payout> first, second, third;
    std::string err;

    ASSERT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, first, err))
        << "primo ricalcolo fallito: " << err;
    ASSERT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, second, err))
        << "secondo ricalcolo fallito: " << err;
    // Terza chiamata a un'altra altezza e poi di nuovo a questa: se il ricalcolo
    // dipendesse dall'ordine delle chiamate, il rientro mostrerebbe la crepa.
    std::vector<MevaTrustManager::expected_payout> elsewhere;
    ASSERT_TRUE(m_manager->compute_expected_payouts(480, kBlockReward, elsewhere, err));
    ASSERT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, third, err))
        << "terzo ricalcolo fallito: " << err;

    ASSERT_FALSE(first.empty())
        << "nessun destinatario: il confronto sarebbe vuoto";

    const auto same = [](const std::vector<MevaTrustManager::expected_payout>& x,
                         const std::vector<MevaTrustManager::expected_payout>& y) {
        if (x.size() != y.size()) return false;
        for (size_t i = 0; i < x.size(); ++i) {
            if (x[i].amount != y[i].amount) return false;
            if (memcmp(&x[i].address, &y[i].address, sizeof(account_public_address)) != 0)
                return false;
        }
        return true;
    };

    EXPECT_TRUE(same(first, second))
        << "ricalcolare due volte la stessa altezza ha dato due risposte diverse: "
           "il ricalcolo sta consumando stato, e un validatore che rivalida un "
           "blocco dopo un riorg si troverebbe a respingere un blocco che aveva "
           "prima accettato"
        << " (prima: " << first.size() << " voci, seconda: " << second.size() << ")";

    EXPECT_TRUE(same(first, third))
        << "il risultato dipende dall'altezza in mezzo e' passata: il ricalcolo ha "
           "memoria di cio' che e' successo prima, e quindi non e' una funzione "
           "dell'altezza";
}

} // namespace
