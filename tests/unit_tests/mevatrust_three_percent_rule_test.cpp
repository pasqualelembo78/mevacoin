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
#include "cryptonote_core/mevatrust/pool_address.h"
#include "cryptonote_config.h"
#include "cryptonote_core/mevatrust/mevatrust_manager.h"
#include "cryptonote_core/mevatrust/reward_distributor.h"
#include "cryptonote_core/mevatrust/mevatrust_tx_parser.h"
#include "cryptonote_core/mevatrust/node_registry.h"
#include "cryptonote_core/mevatrust/pool_distribution.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/tx_extra.h"
#include "cryptonote_core/cryptonote_tx_utils.h"
#include "cryptonote_basic/cryptonote_basic_impl.h"
#include <sys/stat.h>

#include "crypto/crypto.h"
#include "crypto/hash.h"

#include "cryptonote_core/foundation_vesting.h"
#include "ringct/rctOps.h"
#include "ringct/rctSigs.h"
#include "ringct/rctTypes.h"
#include "device/device.hpp"

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

    /// Registra un nodo con una node_pubkey precisa, per i test del producer
    /// 0xA5. Serve perche' il commitment viene costruito sul node_id che il
    /// manager trova cercando *la propria* chiave nel registro: se il test
    /// registrasse un nodo con una chiave generata a caso e poi firmasse con
    /// un'altra, non avrebbe un commitment da firmare, e il test fallirebbe per
    /// il motivo sbagliato sembrando un bug del producer.
    void add_node_as(const std::string& label, const crypto::public_key& node_pk) {
        Seed s{};
        s.node_pubkey = node_pk;
        crypto::secret_key wsk{};
        crypto::generate_keys(s.wallet_pubkey, wsk);
        // node_id stabile e arbitrario: qui non si verifica la formula del
        // produttore della registrazione, ma che il commitment venga costruito
        // sul nodo giusto.
        memcpy(&s.node_id, &node_pk, sizeof(crypto::hash));
        m_wallet[label] = addr_of(fresh_key());
        apply_registration(*m_manager, m_wallet[label], s);
        m_node_id[label] = s.node_id;
    }

    /// La lista che il consenso si aspetta a questa altezza. Se il manager
    /// produce qualcosa di diverso dal previsto, il test che lo usa fallisce: la
    /// regola e' quella che dice lui, non quella che il test immagina.
    std::vector<MevaTrustManager::expected_payout> expected_at(uint64_t height) const {
        std::vector<MevaTrustManager::expected_payout> out;
        std::string err;
        EXPECT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, HF_VERSION_MEVATRUST_RULE_V2, out, err))
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
                                    const std::vector<crypto::public_key>& /*out_keys*/ = {}) {

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
/// Costruisce la coinbase come fa davvero construct_miner_tx_with_mevatrust.
///
/// out_keys sovrascrive le chiavi-output reali delle vout dei nodi. Va lasciato
/// vuoto per un blocco onesto, dove la chiave e' la spend_pub del nodo; va
/// riempito per un attacco, ed e' esattamente il punto in cui il miner sceglie a
/// chi mandare i soldi. Prima che gli output dei nodi diventassero piatti questo
/// parametro non aveva importanza — la chiave era una derivazione e nessuno la
/// guardava, quindi ogni valore era indistinguibile dagli altri. Ora e' l'unico
/// posto in cui si puo' deviare, ed e' esattamente quello che il consenso blocca.
transaction build_coinbase(uint64_t miner_amount,
                           const std::vector<MevaTrustManager::expected_payout>& payouts,
                           const std::vector<crypto::public_key>& out_keys = {}) {
    transaction tx;
    tx.version = 2;
    tx.unlock_time = 0;

    if (miner_amount > 0) {
        tx_out o;
        set_tx_out(miner_amount, fresh_key(), false, crypto::view_tag{}, o);
        tx.vout.push_back(o);
    }
    for (size_t i = 0; i < payouts.size(); ++i) {
        // Onesto: la chiave e' quella del nodo. Attacco: quella fornita.
        const crypto::public_key& key =
            (i < out_keys.size()) ? out_keys[i] : payouts[i].address.m_spend_public_key;
        tx_out o;
        set_tx_out(payouts[i].amount, key, false, crypto::view_tag{}, o);
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
        tx, height, kBlockReward, HF_VERSION_MEVATRUST_RULE_V2, MAINNET, err);
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

    // out_keys vuoto: la coinbase usa la spend_pub dei nodi, come fa il miner vero.
    transaction tx = build_coinbase(kBlockReward - budget, expected);
    tx.extra = build_manifest(expected, height, {});
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

    // E anche i soldi vanno al ladro: questo test non si limita a mentire sul
    // manifesto, devia davvero la coinbase.
    std::vector<crypto::public_key> out_keys;
    out_keys.reserve(stolen.size());
    for (size_t i = 0; i < stolen.size(); ++i)
        out_keys.push_back(stolen[i].address.m_spend_public_key);

    transaction tx = build_coinbase(kBlockReward - budget, stolen, out_keys);
    tx.extra = build_manifest(stolen, height, {});
    ASSERT_FALSE(tx.extra.empty());

    const Verdict v = judge(tx, height);
    EXPECT_FALSE(v.accepted)
        << "il 3% e' finito all'indirizzo arbitrario del miner e il blocco e' "
           "stato accettato: la regola non e' una regola";
    // La diagnosi che arriva e' "non c'e' un'uscita spendibile verso il nodo
    // dovuto", non "il destinatario e' sbagliato". Sono due fatti distinti e il
    // secondo e' piu' grave: il manifesto puo' essere perfettamente conforme
    // mentre i soldi sono andati altrove. Un operatore che legge "destinatario
    // sbagliato" va a guardare il manifesto, che invece e' corretto, e non
    // trova mai il bug.
    EXPECT_NE(v.error.find("no spendable output"), std::string::npos)
        << "il rifiuto deve dire che i soldi non sono dove il protocollo vuole, "
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

    uint64_t stolen = 0;
    for (const auto& p : expected) stolen += p.amount;
    for (const auto& p : partial) stolen -= p.amount;

    // out_keys vuoto: le vout sono oneste, quelle del nodo "a". Il furto qui e'
    // solo la voce mancante dal manifesto, non i soldi spostati: e i due casi
    // hanno bisogni di controlli diversi, quindi i test li tengono separati.
    transaction tx = build_coinbase(kBlockReward - budget, partial);
    tx.extra = build_manifest(partial, height);
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
        tx, height, kBlockReward, HF_VERSION_MEVATRUST_VALIDATION, MAINNET, err);
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
    ASSERT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, HF_VERSION_MEVATRUST_RULE_V2, a, err_a))
        << "il nodo A non e' riuscito a calcolare: " << err_a;
    ASSERT_TRUE(manager_b.compute_expected_payouts(height, kBlockReward, HF_VERSION_MEVATRUST_RULE_V2, b, err_b))
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

    ASSERT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, HF_VERSION_MEVATRUST_RULE_V2, first, err))
        << "primo ricalcolo fallito: " << err;
    ASSERT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, HF_VERSION_MEVATRUST_RULE_V2, second, err))
        << "secondo ricalcolo fallito: " << err;
    // Terza chiamata a un'altra altezza e poi di nuovo a questa: se il ricalcolo
    // dipendesse dall'ordine delle chiamate, il rientro mostrerebbe la crepa.
    std::vector<MevaTrustManager::expected_payout> elsewhere;
    ASSERT_TRUE(m_manager->compute_expected_payouts(480, kBlockReward, HF_VERSION_MEVATRUST_RULE_V2, elsewhere, err));
    ASSERT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, HF_VERSION_MEVATRUST_RULE_V2, third, err))
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


// ─────────────────────────────────────────────────────────────────────────────
// Test 10 - L'ATTACCO FORTE: il punto 6 della richiesta, quello vero.
//
// Il test 2 dice che il manifesto non puo' mentire sul destinatario. Ma il
// manifesto e' un documento, e un documento puo' dire la verita' mentre i soldi
// vanno altrove: il miner sceglie anche le chiavi-output delle vout, e fino a
// poco fa non c'era nessun legame fra quelle scelte e i destinatari dichiarati.
//
// Questo e' il test che mancava, ed e' quello che chiude il buco.
//
// Com'e' andata quando l'ho scritto: il blocco e' stato ACCETTATO, con un errore
// vuoto. Il manifesto era corretto, le somme erano corrette, e i soldi erano
// del miner. Tutte le verifiche esistenti erano soddisfatte, perche' guardavano
// il manifesto e non guardavano la coinbase.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, honest_manifest_but_the_money_goes_to_the_miner_is_rejected)
{
    const uint64_t height = 240;
    const uint64_t budget = node_budget_of(kBlockReward);
    ASSERT_GT(budget, 0);

    m_wallet["a"] = addr_of(fresh_key());
    add_node("a");

    const auto expected = expected_at(height);
    ASSERT_FALSE(expected.empty());

    // Il manifesto e' ONESTO: e' la lista del consenso, parola per parola.
    // Il colpevole e' uno solo, ed e' la chiave-output della vout.
    const account_public_address thief = addr_of(fresh_key());
    ASSERT_TRUE(memcmp(&thief.m_spend_public_key,
                       &expected[0].address.m_spend_public_key,
                       sizeof(crypto::public_key)) != 0)
        << "il test non avrebbe niente da dimostrare";

    std::vector<crypto::public_key> thief_keys;
    thief_keys.reserve(expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        thief_keys.push_back(thief.m_spend_public_key);

    // Solo la chiave cambia. Gli importi, gli indirizzi dichiarati e tutte le
    // somme restano quelli giusti: e' un attacco che non si vede leggendo il
    // manifesto, ed e' per questo che il manifesto non basta.
    transaction tx = build_coinbase(kBlockReward - budget, expected, thief_keys);
    tx.extra = build_manifest(expected, height);
    ASSERT_FALSE(tx.extra.empty());

    const Verdict v = judge(tx, height);
    EXPECT_FALSE(v.accepted)
        << "manifesto perfettamente conforme, somme giuste, e i soldi del 3% "
           "sono finiti all'indirizzo del miner: il blocco e' stato accettato. "
           "Il 3% non e' una regola di consenso";
    EXPECT_NE(v.error.find("no spendable output"), std::string::npos)
        << "il rifiuto deve nominare il problema vero (i soldi non sono dove il "
           "protocollo prescrive), non il manifesto, che e' corretto"
        << " (messaggio ricevuto: " << v.error << ")";
}


// ─────────────────────────────────────────────────────────────────────────────
// Test 11 - IL CONFINE DEL FORK.
//
// Un test che asserisce che un attacco riesce sembra il test sbagliato da scrivere.
// Qui e' il test giusto, e vale la pena capire perche'.
//
// La regola v2 cambia il formato degli output ai nodi: da stealth a piatto sulla
// chiave pubblica registrata. Le due regole sono incompatibili, quindi stanno in
// due epoche diverse (HF_VERSION_MEVATRUST_RULE_V2). I blocchi prodotti prima del
// fork sono costruiti con il metodo vecchio e non devono essere giudicati con la
// regola nuova: se lo fossero, ogni blocco gia' in catena diventerebbe invalido e
// un nodo che riavvia si troverebbe a rifiutare la propria storia.
//
// Quindi questo test fissa una cosa che sembra brutta e che e' giusta: il 3% dei
// blocchi gia' prodotti non aveva il vincolo sugli output. Non e' una concessione,
// e' il motivo per cui la catena esistente resta valida. La protezione vale da qui
// in avanti.
//
// Se questo test comincia a fallire, il fork e' stato spostato o tolto senza volerlo,
// e i blocchi vecchi vengono giudicati con regole che non gli appartengono.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RuleFixture, the_output_rule_starts_at_the_fork_and_not_before)
{
    const uint64_t height = 240;
    const uint64_t budget = node_budget_of(kBlockReward);

    m_wallet["a"] = addr_of(fresh_key());
    add_node("a");
    const auto expected = expected_at(height);
    ASSERT_FALSE(expected.empty());

    const account_public_address thief = addr_of(fresh_key());
    ASSERT_TRUE(memcmp(&thief.m_spend_public_key,
                       &expected[0].address.m_spend_public_key,
                       sizeof(crypto::public_key)) != 0);
    std::vector<crypto::public_key> thief_keys;
    thief_keys.reserve(expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        thief_keys.push_back(thief.m_spend_public_key);

    transaction tx = build_coinbase(kBlockReward - budget, expected, thief_keys);
    tx.extra = build_manifest(expected, height);

    // PRIMA del fork: il vincolo sugli output non esisteva e non viene inventato
    // adesso. Il manifesto e' comunque verificato, quindi non e' che il 3% fosse
    // libero: era vincolato alla destinazione dichiarata e non a quella effettiva.
    std::string err;
    const bool pre_fork = check_mevatrust_coinbase_pure_consensus(
        tx, height, kBlockReward, HF_VERSION_MEVATRUST_VALIDATION, MAINNET, err);
    EXPECT_TRUE(pre_fork)
        << "la regola e' stata applicata retroattivamente ai blocchi pre-fork: "
           "un nodo che riavvia su una catena lunga rifiuterebbe la propria storia";

    // DAAL fork: la stessa identica forma di blocco viene respinta.
    std::string err2;
    const bool post_fork = check_mevatrust_coinbase_pure_consensus(
        tx, height, kBlockReward, HF_VERSION_MEVATRUST_RULE_V2, MAINNET, err2);
    EXPECT_FALSE(post_fork)
        << "la regola non morde dopo il fork: il buco del punto 6 e' di nuovo aperto";

    EXPECT_LT(HF_VERSION_MEVATRUST_VALIDATION, HF_VERSION_MEVATRUST_RULE_V2)
        << "l'ordine dei due HF e' invertito: la regola vecchia non puo' venire "
           "dopo quella nuova";
}


// ─────────────────────────────────────────────────────────────────────────────
// Test 12 - LA COINBASE DEL VERO MINER, CONTROLLATA DAL VERO VALIDATORE.
//
// Fin qui i test costruivano la coinbase con un helper scritto a mano, e il
// validatore era quello vero. Funzionavano, ma non rispondevano alla domanda che
// conta di piu': se il miner produce e il validatore accetta.
//
// Sono due funzioni diverse in due file diversi, e nessun test le metteva insieme.
// Diventano cosi' facili che il costruttore emetta un output nella forma A e il
// validatore si aspetti la forma B: i test del validatore passano (perche'
// costruiscono la forma B a mano), i test del costruttore passano (perche'
// controllano solo le somme), e la catena si ferma al primo blocco vero.
//
// Qui il blocco lo costruisce construct_miner_tx_with_mevatrust() — quella vera,
// con la sua chiave tx, i suoi output derivati, il suo ordine — e lo giudica
// check_mevatrust_coinbase_pure_consensus(). Se i due non concordano, il miner
// onesto produce blocchi che i validatori rifiutano, e la rete si ferma al primo
// confine di periodo.
// ─────────────────────────────────────────────────────────────────────────────
// ── La registrazione deve stare sulla catena, non sul disco ──────────────────
//
// Un nodo puo' registrarsi con la RPC register_node(), che scrive in questo
// stesso registro e lo lascia ACTIVE. ACTIVE, da solo, e' tutto quello che il
// calcolo delle quote guardava: quindi quel nodo finiva nel blocco di questo
// daemon, con un importo che nessun altro nodo ricostruisce, e il blocco veniva
// respinto dalla rete.
//
// Il caso e' diverso da una semplice disattenzione. register_node() ricalcola
// anche il node_id da time(nullptr): due nodi che registrano lo stesso fatto
// ottengono due identificatori diversi. Il registro locale non racconta la stessa
// storia del registro degli altri, quindi la lista delle quote non e' confrontabile
// voce per voce — e quando non e' confrontabile non c'e' piu' consenso, c'e' solo
// il miner che si crede d'accordo con se stesso.
//
// ACTIVE resta il filtro per l'amministrazione (RPC, statistiche, ban). Per i
// soldi vale registered_onchain, e il posto che lo chiede e' uno solo.

TEST_F(RuleFixture, a_node_registered_only_locally_earns_nothing) {
    const uint64_t height = 240;
    Seed s = make_seed();
    ASSERT_TRUE(m_manager->node_registry()->register_node(
            s.wallet_pubkey, addr_str(m_wallet["local"]), s.node_pubkey,
            crypto::signature{}, 5555, "10.0.0.9", 1))
        << "registrazione locale respinta: il test non sta provando niente";

    // Il nodo e' nel registro, e questo e' il punto: non e' invisibile.
    NodeRegistryEntry e{};
    // get_node_by_pubkey confronta il NODE_pubkey, non il wallet.
    ASSERT_TRUE(m_manager->node_registry()->get_node_by_pubkey(s.node_pubkey, e))
        << "il nodo non e' nel registro: il test sta provando il caso sbagliato";
    EXPECT_EQ(e.status, NodeStatus::ACTIVE)
        << "il caso d'interesse e' un nodo ACTIVE, non uno scartato a monte";

    // Eppure non viene pagato, perche' nessun blocco lo ha mai registrato.
    EXPECT_TRUE(m_manager->node_registry()->get_eligible_nodes().empty())
        << "un nodo registrato solo in locale e' stato pagato";

    auto payouts = expected_at(height);
    EXPECT_TRUE(payouts.empty())
        << "le quote contengono un nodo che non esiste sulla catena: "
        << payouts.size() << " payout";
}

TEST_F(RuleFixture, the_same_node_on_chain_is_paid_and_locally_it_is_not) {
    const uint64_t height = 240;
    Seed s = make_seed();

    // Stesso wallet, stesso indirizzo, stesso passato di uptime. Cambia solo da
    // dove arriva la registrazione: un blocco, o una RPC.
    ASSERT_TRUE(m_manager->node_registry()->register_node(
            s.wallet_pubkey, addr_str(m_wallet["x"]), s.node_pubkey,
            crypto::signature{}, 5555, "10.0.0.9", 1));
    EXPECT_TRUE(expected_at(height).empty())
        << "prima della registrazione on-chain nessuno deve essere pagato";

    tx_extra_mevatrust_registration reg{};
    reg.wallet_pubkey  = s.wallet_pubkey;
    reg.node_pubkey    = s.node_pubkey;
    reg.wallet_address = addr_str(m_wallet["x"]);
    reg.node_id        = s.node_id;
    ASSERT_TRUE(m_manager->node_registry()->register_node_onchain(reg, 1));

    // Registrato non basta: la soglia guarda il punteggio, e il punteggio si
    // costruisce da uptime. Dieci eventi online con peer: il massimo possibile.
    for (uint32_t i = 0; i < 10; ++i) {
        UptimeEvent ev{};
        ev.timestamp        = 1000 + i;
        ev.online           = true;
        ev.block_height     = 240;
        ev.peer_count       = 4;
        ev.response_time_ms = 50;
        ev.ip_address       = "127.0.0.1";
        ASSERT_TRUE(m_manager->mevatrust_engine()->record_uptime_event(s.node_id, ev));
    }

    auto payouts = expected_at(height);
    EXPECT_EQ(payouts.size(), 1u)
        << "la registrazione on-chain non rende il nodo eleggibile";
    // Si confronta con l'indirizzo dichiarato nella registrazione, non con
    // wallet_pubkey: quello e' solo la chiave che il nodo prova di possedere,
    // l'indirizzo di pagamento e' un altro campo dello stesso messaggio.
    if (!payouts.empty())
        EXPECT_EQ(memcmp(&payouts[0].address.m_spend_public_key,
                         &m_wallet["x"].m_spend_public_key,
                         sizeof(crypto::public_key)), 0)
            << "i soldi sono andati a un indirizzo diverso da quello dichiarato";
}

// ── Il conto: 97 al miner, 3 di budget, niente moneta coniata ───────────────
//
// Il caso che nessun test copriva e che la catena reale non poteva mostrare:
// sulla regtest non c'e' mai stato un nodo eleggibile, quindi node_total era
// sempre zero e i due difetti sotto restavano addormentati.
//
// Il primo: create_block_template() passava total_block_reward = base+fee+quote,
// e il costruttore aggiungeva le quote una seconda volta al check sulla somma.
// La coinbase coniava node_total a ogni blocco di distribuzione, e il check finale
// la rifiutava — quindi catena ferma al primo periodo con un nodo reale.
//
// Il secondo: 3% al pool PIÙ 3% ai nodi faceva 6%, e il miner incassava il 94%.
// Il pool non ha una chiave privata, quindi il suo 3% non e' una riserva per
// i nodi: e' moneta bloccata. I nodi venivano pagati con soldi inesistenti.

TEST_F(RuleFixture, the_budget_is_three_percent_and_nothing_is_minted) {
    const uint64_t height = 240;
    const uint64_t reward = kBlockReward;

    // Cinque nodi con lo stesso punteggio. Il numero conta: la quota di ciascun
    // nodo e' floor(budget / n), quindi il pool incassa solo il resto budget % n,
    // e il resto esiste solo se n non divide il budget. Con questo reward budget =
    // 998299224, che e' divisibile per 2, 3, 4, 6, 8 e 9: con tre nodi la divisione
    // e' esatta e al pool non resta niente — il caso da provare ("il 3% smette di
    // essere contato due volte") resterebbe addormentato. Cinque non lo divide
    // (998299224 % 5 == 4), ed e' quel resto che il pool deve incassare.
    m_wallet["a"] = addr_of(fresh_key());
    m_wallet["b"] = addr_of(fresh_key());
    m_wallet["c"] = addr_of(fresh_key());
    m_wallet["d"] = addr_of(fresh_key());
    m_wallet["e"] = addr_of(fresh_key());
    add_node("a");
    add_node("b");
    add_node("c");
    add_node("d");
    add_node("e");

    const auto expected = expected_at(height);
    ASSERT_EQ(expected.size(), 5u) << "servono cinque nodi perche' la divisione lasci un resto";

    // Le quote che il miner riceve dal consenso, e il manifesto insieme. Si passa
    // da get_coinbase_rewards() perche' e' la stessa porta che apre il miner: il
    // manifesto 0xAA nasce li'. expected_at() serve solo come riferimento.
    std::vector<NodeCoinbaseReward> claimed;
    uint64_t node_total = 0;
    {
        uint64_t miner_out = 0;
        claimed = m_manager->get_coinbase_rewards(height, reward, HF_VERSION_MEVATRUST_RULE_V2, miner_out);
        ASSERT_EQ(claimed.size(), expected.size());
        for (size_t i = 0; i < claimed.size(); ++i)
            ASSERT_EQ(claimed[i].amount, expected[i].amount);
        for (const auto& c : claimed) node_total += c.amount;
    }
    ASSERT_GT(node_total, 0u);
    ASSERT_LT(node_total, reward);

    // Il budget e' il 3% del blocco. Le quote ci stanno dentro: e' il punto.
    // Con 5 nodi a peso uguale, budget non e' divisibile per 5 (998299224 % 5 = 4)
    // quindi la divisione per troncamento lascia un resto al pool.
    const uint64_t budget = node_budget_of(reward);
    ASSERT_LE(node_total, budget)
        << "le quote sono piu' del budget: il 3% non basta a coprirle";
    ASSERT_LT(node_total, budget)
        << "le quote mangiano tutto il budget: al pool non resta niente da provare";

    m_wallet["miner"] = addr_of(fresh_key());
    const std::vector<uint8_t> manifest = m_manager->consume_pending_pool_distribution_extra();
    ASSERT_FALSE(manifest.empty());

    // total_block_reward = base+fee, ESATTAMENTE come fa create_block_template().
    // Passare base+fee+quote e' il difetto: e' quello che si corregge qui.
    transaction coinbase;
    ASSERT_TRUE(construct_miner_tx_with_mevatrust(
        height, 500, 1, 0, reward, m_wallet["miner"], claimed, coinbase,
        blobdata(), 999, HF_VERSION_MEVATRUST_RULE_V2, manifest, 0, MAINNET, 0))
        << "il costruttore ha rifiutato un blocco onesto";

    uint64_t sum = 0;
    for (const auto& o : coinbase.vout) sum += o.amount;
    EXPECT_EQ(sum, reward)
        << "la coinbase vale " << sum << " invece di " << reward
        << ": " << (sum > reward ? "ha coniato " : "ha perso ")
        << (sum > reward ? sum - reward : reward - sum) << " atomic";

    // Il miner prende il 97%, non il 94%.
    const account_public_address pool_addr =
        mevatrust::get_pool_address(MAINNET);
    // Per posizione, non per chiave. Il costruttore emette nell'ordine
    // [output del miner, splitti in cifre][un output per nodo][pool], quindi
    // l'ultimo e' il pool, i claimed.size() precedenti sono i nodi e il resto e'
    // del miner. Confrontare le chiavi non funzionerebbe: quelle del miner sono
    // derivate e non eguagliano mai la sua spend_pub.
    // L'output del pool c'e' solo se il budget non e' stato tutto distribuito: e'
    // quello che distingue "i nodi sono stati pagati dentro il 3%" da "il 3% e'
    // sparito".
    const bool     has_pool  = (budget > node_total);
    const size_t   pool_pos  = coinbase.vout.size() - (has_pool ? 1 : 0);
    const size_t   nodes_from = pool_pos - claimed.size();
    ASSERT_GE(pool_pos, claimed.size()) << "coinbase troppo corta: mancano gli output";

    uint64_t to_miner = 0, to_pool = 0, to_nodes = 0;
    for (size_t i = 0; i < coinbase.vout.size(); ++i) {
        const auto& o = coinbase.vout[i];
        const auto* k  = boost::get<txout_to_key>(&o.target);
        const auto* tk = k ? nullptr : boost::get<txout_to_tagged_key>(&o.target);

        if (has_pool && i == pool_pos) {
            // L'output del pool e' piatto per costruzione: e' la proprieta' che
            // permette al consenso di confrontarne la chiave.
            ASSERT_TRUE(k) << "l'output del pool non e' piatto";
            EXPECT_EQ(memcmp(&k->key, &pool_addr.m_spend_public_key,
                             sizeof(k->key)), 0)
                << "l'output in posizione di pool non e' all'indirizzo del pool";
            to_pool += o.amount;
        } else if (i >= nodes_from) {
            // Idem per i nodi: se non sono piatti, il consenso puo' contare i
            // soldi ma non accertarsi che siano andati al nodo giusto.
            ASSERT_TRUE(k) << "l'output di un nodo non e' piatto";
            EXPECT_EQ(memcmp(&k->key, &expected[i - nodes_from].address.m_spend_public_key,
                             sizeof(k->key)), 0)
                << "il nodo " << (i - nodes_from) << " e' stato pagato verso un'altra chiave";
            to_nodes += o.amount;
        } else {
            // Il formato degli output del miner non e' il soggetto di questo
            // test: cambia con HF_VERSION_VIEW_TAGS e non c'entra col conto del
            // 3%. Qui basta che sia un output a chiave.
            EXPECT_TRUE(k || tk) << "output del miner non a chiave";
            to_miner += o.amount;
        }
    }

    EXPECT_EQ(to_nodes, node_total) << "ai nodi non e' arrivata la quota calcolata";
    EXPECT_EQ(to_pool, budget - node_total)
        << "il pool ha preso il 3% pieno invece del resto: i nodi verrebbero "
           "pagati due volte, dal pool e dal miner";
    EXPECT_EQ(to_miner, reward - budget)
        << "il miner non ha incassato il 97%";

    std::string err;
    EXPECT_TRUE(check_mevatrust_coinbase(coinbase, height, reward,
                                         HF_VERSION_MEVATRUST_RULE_V2, MAINNET, err))
        << "il check del pool non riconosce il conto giusto: " << err;
    EXPECT_TRUE(check_mevatrust_coinbase_pure_consensus(
        coinbase, height, reward, HF_VERSION_MEVATRUST_RULE_V2, MAINNET, err))
        << "validatore rifiuta un blocco onesto: " << err;
}

TEST_F(RuleFixture, the_real_miner_output_passes_the_real_validator)
{
    const uint64_t height = 240;
    const uint64_t reward = kBlockReward;
    const uint64_t budget = node_budget_of(reward);

    m_wallet["a"] = addr_of(fresh_key());
    m_wallet["b"] = addr_of(fresh_key());
    add_node("a");
    add_node("b");

    const auto expected = expected_at(height);
    ASSERT_FALSE(expected.empty());

    // Le quote che il miner riceve dal consenso: la stessa lista che il validatore
    // ricalcolera'. Nessuna scorciatoia: e' l'interfaccia reale.
    std::vector<NodeCoinbaseReward> node_rewards;
    node_rewards.reserve(expected.size());
    uint64_t node_total = 0;
    for (const auto& e : expected) {
        NodeCoinbaseReward ncr{};
        ncr.address = e.address;
        ncr.amount  = e.amount;
        node_rewards.push_back(ncr);
        node_total += e.amount;
    }
    ASSERT_LT(node_total, reward) << "le quote non ci stanno nel blocco";

    m_wallet["miner"] = addr_of(fresh_key());

    // ── Il miner chiede le quote al consenso ────────────────────────────────
    //
    // L'ordine non e' decorativo e replica esattamente create_block_template():
    // prima le quote e il manifesto, poi la coinbase che li contiene. E' il punto
    // del primo refactor — il miner non calcola nulla, chiede.
    uint64_t miner_out = 0;
    const auto claimed = m_manager->get_coinbase_rewards(height, reward, HF_VERSION_MEVATRUST_RULE_V2, miner_out);
    ASSERT_EQ(claimed.size(), node_rewards.size());
    for (size_t i = 0; i < claimed.size(); ++i)
        ASSERT_EQ(claimed[i].amount, node_rewards[i].amount);

    const std::vector<uint8_t> manifest = m_manager->consume_pending_pool_distribution_extra();
    ASSERT_FALSE(manifest.empty())
        << "nessun manifesto 0xAA: il blocco non dichiarerebbe il 3% e il validatore "
           "lo respingerebbe per motivi che sembrerebbero casuali";

    // ── Il miner vero costruisce ────────────────────────────────────────────
    transaction coinbase;
    const bool built = construct_miner_tx_with_mevatrust(
        height, 500, /*already_generated_coins=*/1, /*current_block_weight=*/0,
        reward, m_wallet["miner"], claimed, coinbase,
        /*extra_nonce=*/blobdata(), /*max_outs=*/999,
        HF_VERSION_MEVATRUST_RULE_V2, manifest,
        /*custom_unlock_window=*/0, MAINNET, /*desy_additional_reward=*/0);
    ASSERT_TRUE(built) << "construct_miner_tx_with_mevatrust ha fallito";
    for (size_t i = 0; i < claimed.size(); ++i)
        ASSERT_EQ(claimed[i].amount, node_rewards[i].amount);

    // ── Il validatore vero giudica ──────────────────────────────────────────
    std::string err;
    const bool ok = check_mevatrust_coinbase_pure_consensus(
        coinbase, height, reward, HF_VERSION_MEVATRUST_RULE_V2, MAINNET, err);
    EXPECT_TRUE(ok)
        << "il miner ha costruito un blocco che il validatore rifiuta: la catena "
           "si fermerebbe al primo confine di periodo. Motivo: " << err;

    // E il caso furto, sullo stesso blocco: si cambia solo la chiave-output del
    // nodo, lasciando il manifesto intatto. Il miner non puo' produrre questo
    // blocco perche' costruisce gli output dai suoi stessi indirizzi, ma un
    // blocco cosi' puo' arrivare dalla rete, ed e' quello che il consenso deve
    // respingere.
    transaction stolen = coinbase;
    bool diverted = false;
    //
    // txout_to_key::key e' const: non si puo' scriverci dentro. Si ricostruisce
    // l'output da zero con la chiave del miner, che e' quello che farebbe un
    // attaccante vero: non modifica un output, ne costruisce uno diverso.
    for (auto& vout : stolen.vout) {
        const auto* tk = boost::get<txout_to_key>(&vout.target);
        if (!tk) continue;
        if (vout.amount == claimed[0].amount &&
            memcmp(&tk->key, &claimed[0].address.m_spend_public_key,
                   sizeof(crypto::public_key)) == 0) {
            tx_out replacement;
            set_tx_out(vout.amount, m_wallet["miner"].m_spend_public_key,
                       false, crypto::view_tag{}, replacement);
            vout = replacement;
            diverted = true;
            break;
        }
    }
    ASSERT_TRUE(diverted)
        << "non ho trovato l'output del nodo da deviare: il test non proverebbe niente";

    std::string err2;
    EXPECT_FALSE(check_mevatrust_coinbase_pure_consensus(
        stolen, height, reward, HF_VERSION_MEVATRUST_RULE_V2, MAINNET, err2))
        << "il 3% e' stato deviato al miner e il blocco e' passato";
}



// -- Il producer di uptime 0xA5 ----------------------------------------------

// Il consumatore 0xA5 non legge un blob: legge una transazione. Il test mette
// quindi il blob dove andrebbe messo e passa per la stessa funzione che il
// manager usa su ogni blocco, cosi' copre anche la serializzazione in tx_extra
// e non solo la firma.
tx_extra_mevatrust_uptime roundtrip(const std::vector<uint8_t>& extra) {
    transaction tx{};
    tx.version     = 1;
    tx.unlock_time = 60;
    tx.extra       = extra;
    tx_extra_mevatrust_uptime out{};
    bool ok = mevatrust::parse_mevatrust_uptime_from_tx(tx, out);
    EXPECT_TRUE(ok) << "il consumatore 0xA5 non ha riconosciuto il blob prodotto";
    return out;
}

TEST_F(RuleFixture, uptime_commitment_is_signed_by_the_node_and_parses_back) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    m_manager->set_node_key(sk, pk);

    add_node_as("alpha", pk);

    // Il commitment e' costruito sul node_id che questo nodo ha registrato,
    // cercato per la sua chiave: non su uno dichiarato da fuori.
    NodeRegistryEntry me{};
    ASSERT_TRUE(m_manager->node_registry()->get_node_by_pubkey(pk, me))
        << "il nodo non e' in registro per la propria chiave";

    std::vector<uint8_t> extra;
    std::string err, node_id_hex;
    uint64_t uptime = 0;
    ASSERT_TRUE(m_manager->build_signed_uptime_commitment(4, 240, true, extra, err,
                                                          node_id_hex, uptime))
        << "commitment non costruito: " << err;
    ASSERT_FALSE(extra.empty());
    EXPECT_TRUE(err.empty()) << "su un percorso riuscito non deve esserci un errore";

    const tx_extra_mevatrust_uptime u = roundtrip(extra);

    EXPECT_EQ(u.node_id, me.node_id);
    EXPECT_EQ(u.node_pubkey, pk);
    EXPECT_TRUE(mevatrust::verify_uptime_signature(u))
        << "il blob firmato dal nodo non verifica con la chiave del nodo";
    EXPECT_EQ(u.peer_count, 4u);
    EXPECT_TRUE(u.is_synced);
    EXPECT_EQ(u.sync_height, 240u);
    EXPECT_EQ(uptime, u.uptime_seconds)
        << "la cifra risposta all'operatore e' quella firmata nel blob";
}

// Il caso che la separazione delle chiavi deve chiudere: se il richiedente potesse
// scegliere il node_id, potrebbe far firmare a questo nodo l'uptime di un altro e
// prendersi i suoi punti. Il commitment deve essere sempre su questo nodo.
TEST_F(RuleFixture, uptime_commitment_cannot_be_built_for_someone_elses_node) {
    add_node("alpha");
    add_node("beta");

    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    m_manager->set_node_key(sk, pk);

    // Questo nodo non e' mai registrato: non puo' firmare nulla, ne' per se' ne'
    // per gli altri. Il messaggio deve dirlo, non restituire un blob vuoto che
    // finirebbe in una transazione che sembra andare a buon fine.
    std::vector<uint8_t> extra;
    std::string err, node_id_hex;
    uint64_t uptime = 0;
    EXPECT_FALSE(m_manager->build_signed_uptime_commitment(4, 240, true, extra, err,
                                                           node_id_hex, uptime));
    EXPECT_TRUE(extra.empty());
    EXPECT_NE(err.find("not registered"), std::string::npos)
        << "l'errore dovrebbe dire che il nodo non e' registrato, non altro";
}

TEST_F(RuleFixture, uptime_commitment_from_a_tampered_blob_does_not_verify) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    m_manager->set_node_key(sk, pk);
    add_node_as("alpha", pk);

    std::vector<uint8_t> extra;
    std::string err, node_id_hex;
    uint64_t uptime = 0;
    ASSERT_TRUE(m_manager->build_signed_uptime_commitment(4, 240, true, extra, err,
                                                          node_id_hex, uptime));

    // Inflare l'uptime: un secondo dichiarato come un milione. E' la
    // falsificazione piu' semplice, e anche la prima che un nodo disonesto
    // proverebbe.
    const tx_extra_mevatrust_uptime honest = roundtrip(extra);
    tx_extra_mevatrust_uptime u = honest;
    u.uptime_seconds = 1000000;

    EXPECT_FALSE(mevatrust::verify_uptime_signature(u))
        << "un uptime gonfiato e' stato accettato come firmato dal nodo";

    // E il blob col dato alterato non deve passare la rilettura del consumatore
    // come se fosse un commitment valido: e' il percorso che il manager esegue.
    std::vector<uint8_t> tampered;
    if (mevatrust::build_mevatrust_uptime_extra(u, tampered))
        EXPECT_FALSE(mevatrust::verify_uptime_signature(roundtrip(tampered)));
}

// Un commitment firmato da una chiave diversa da quella dichiarata: se il
// consumatore accettasse questo, un nodo con una sola chiave potrebbe
// attribuirsi l'uptime di un altro.
TEST_F(RuleFixture, uptime_commitment_signed_by_a_foreign_key_does_not_verify) {
    crypto::secret_key mine_sk{};
    crypto::public_key mine_pk{};
    crypto::generate_keys(mine_pk, mine_sk);

    crypto::secret_key other_sk{};
    crypto::public_key other_pk{};
    crypto::generate_keys(other_pk, other_sk);

    tx_extra_mevatrust_uptime u{};
    u.node_id        = crypto::hash{};
    u.uptime_seconds = 999;
    u.timestamp      = 1700000000;
    u.peer_count     = 3;
    u.is_synced      = true;
    u.sync_height    = 240;
    u.node_pubkey    = mine_pk;
    crypto::generate_signature(mevatrust::uptime_message_hash(u), other_pk, other_sk,
                               u.signature);

    EXPECT_FALSE(mevatrust::verify_uptime_signature(u))
        << "una firma fatta con una chiave diversa da quella dichiarata e' passata";
}


// -- Il commitment deve sopravvivere alla costruzione della transazione -------

// Questo e' il test che mancava, ed e' quello che conta.
//
// I test precedenti passano l'extra direttamente al consumatore, quindi
// saltano il passaggio in cui la transazione viene costruita. Ma sort_tx_extra
// conosce solo i tag che ha un pick<>: i tag MevaTrust non lo hanno, e senza un
// pick un campo non riconosciuto fa fallire la costruzione con "transaction was
// not constructed". Lo stesso errore ha gia' fatto perdere una transazione di
// governance.
//
// Per ora i tag MevaTrust non hanno VARIANT_TAG, quindi sort_tx_extra si
// ferma sul tag sconosciuto, lascia i campi non raccolti e con allow_partial
// riappende i byte grezzi in coda: la transazione si costruisce e il blob
// sopravvive. Questo test blocca quel comportamento. Se qualcuno aggiunge un
// VARIANT_TAG a 0xA5 senza aggiungere il pick<>, questo test fallisce - che e'
// esattamente quando serve saperlo.
TEST_F(RuleFixture, uptime_blob_survives_tx_construction_and_stays_findable) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    m_manager->set_node_key(sk, pk);
    add_node_as("alpha", pk);

    std::vector<uint8_t> extra;
    std::string err, node_id_hex;
    uint64_t uptime = 0;
    ASSERT_TRUE(m_manager->build_signed_uptime_commitment(4, 240, true, extra, err,
                                                          node_id_hex, uptime));

    // allow_partial=true: e' il valore che usa construct_tx_with_tx_key(), cioe'
    // il percorso di un invio dal wallet.
    std::vector<uint8_t> sorted;
    ASSERT_TRUE(sort_tx_extra(extra, sorted, true))
        << "la transazione con 0xA5 non si costruirebbe: il commitment non arriverebbe mai";

    // Con un solo campo non riconosciuto l'output puo' tornare identico
    // all'input: sort_tx_extra non raccoglie nulla e riappende da offset 0. Non
    // e' un difetto, e non e' quello che questo test deve controllare. Quello che
    // conta e' che il blob resti intatto e ritrovabile.
    EXPECT_EQ(sorted.size(), extra.size())
        << "il commitment ha perso byte passando dalla costruzione della transazione";

    // Il consumatore deve ritrovarlo dopo il riordino, altrimenti la firma
    // verificherebbe ma l'evento non verrebbe mai registrato.
    transaction tx{};
    tx.version     = 1;
    tx.unlock_time = 60;
    tx.extra       = sorted;

    tx_extra_mevatrust_uptime u{};
    ASSERT_TRUE(mevatrust::parse_mevatrust_uptime_from_tx(tx, u))
        << "il commitment e' sopravvissuto alla costruzione ma il consumatore non lo trova";
    EXPECT_TRUE(mevatrust::verify_uptime_signature(u));
    EXPECT_EQ(u.node_pubkey, pk);
}

// ─────────────────────────────────────────────────────────────────────────────
// Anti-Sybil Fase 1 (HF18): binding
//
// Fino a HF17 una sola node_pubkey poteva firmare l'uptime (0xA5) di decine di
// node_id diversi, e la firma era verificata contro la chiave trasportata dentro
// la 0xA5 stessa: bastava inventarsi un node_id e una chiave. Questi test
// bloccano entrambe le scorciatoie e, altrettanto importante, verificano che
// sotto la soglia del fork il comportamento vecchio resti intatto: la nuova
// regola non e' retroattiva.
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(RuleFixture, antisypil_same_node_pubkey_rejected_under_hf18) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);

    tx_extra_mevatrust_registration a{};
    a.wallet_pubkey   = pk;
    a.node_pubkey     = pk;
    a.wallet_address  = "addrA";
    a.node_id         = crypto::cn_fast_hash("a", 1);
    ASSERT_TRUE(m_manager->node_registry()->register_node_onchain(a, 1, 18));

    // Wallet diverso (quindi il cap "3 per wallet" non c'entra): l'unica cosa
    // che respinge e' la node_pubkey gia' in uso.
    tx_extra_mevatrust_registration b{};
    b.wallet_pubkey   = fresh_key();
    b.node_pubkey     = pk;
    b.wallet_address  = "addrB";
    b.node_id         = crypto::cn_fast_hash("b", 1);
    EXPECT_FALSE(m_manager->node_registry()->register_node_onchain(b, 1, 18))
        << "una node_pubkey gia' attiva e' stata accettata una seconda volta";
}

TEST_F(RuleFixture, antisypil_same_node_pubkey_allowed_before_hf18) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);

    tx_extra_mevatrust_registration a{};
    a.wallet_pubkey   = pk;
    a.node_pubkey     = pk;
    a.wallet_address  = "addrA";
    a.node_id         = crypto::cn_fast_hash("a", 1);
    ASSERT_TRUE(m_manager->node_registry()->register_node_onchain(a, 1, 0));

    tx_extra_mevatrust_registration b{};
    b.wallet_pubkey   = fresh_key();
    b.node_pubkey     = pk;
    b.wallet_address  = "addrB";
    b.node_id         = crypto::cn_fast_hash("b", 1);
    EXPECT_TRUE(m_manager->node_registry()->register_node_onchain(b, 1, 0))
        << "sotto la soglia del fork la regola vecchia deve restare intatta";
}

/// Mette una singola tx con extra 0xA5 in un blocco e lo consegna al processing
/// reale del manager: stesso percorso che gira su ogni blocco della catena.
static uint64_t feed_uptime_block(MevaTrustManager& mgr, const tx_extra_mevatrust_uptime& u,
                                  uint64_t height, uint8_t hf_version) {
    std::vector<uint8_t> extra;
    if (!mevatrust::build_mevatrust_uptime_extra(u, extra)) return UINT64_MAX;
    transaction tx{};
    tx.version     = 1;
    tx.unlock_time = 60;
    tx.extra       = extra;
    const crypto::hash txid = get_transaction_hash(tx);

    block bl{};
    bl.major_version = hf_version;
    bl.miner_tx.version = 1;
    bl.tx_hashes.push_back(txid);
    mgr.set_get_tx_func([&tx, &txid](const crypto::hash& id, transaction& out) -> bool {
        if (id == txid) { out = tx; return true; }
        return false;
    });
    mgr.process_mevatrust_txs(bl, height, hf_version);
    return 0;
}

TEST_F(RuleFixture, antisypil_uptime_bound_to_registered_key_under_hf18) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    add_node_as("alpha", pk);

    NodeRegistryEntry alpha{};
    ASSERT_TRUE(m_manager->node_registry()->get_node_by_id(m_node_id["alpha"], alpha));
    ASSERT_TRUE(alpha.registered_onchain);

    tx_extra_mevatrust_uptime u{};
    u.node_id        = alpha.node_id;
    u.uptime_seconds = 100;
    u.timestamp      = 1700000000;
    u.peer_count     = 3;
    u.is_synced      = true;
    u.sync_height    = 240;
    u.node_pubkey    = pk;
    crypto::generate_signature(mevatrust::uptime_message_hash(u), pk, sk, u.signature);
    ASSERT_TRUE(mevatrust::verify_uptime_signature(u));

    ASSERT_EQ(feed_uptime_block(*m_manager, u, 100, 18), 0u);

    NodeRegistryEntry after{};
    ASSERT_TRUE(m_manager->node_registry()->get_node_by_id(alpha.node_id, after));
    EXPECT_EQ(after.last_sync_height, 240u)
        << "un uptime legittimo, firmato con la chiave registrata, e' stato scartato";
}

TEST_F(RuleFixture, antisypil_uptime_with_foreign_key_rejected_under_hf18) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    add_node_as("alpha", pk);

    NodeRegistryEntry alpha{};
    ASSERT_TRUE(m_manager->node_registry()->get_node_by_id(m_node_id["alpha"], alpha));
    ASSERT_EQ(alpha.last_sync_height, 1u);

    // Il node_id e' quello vero di alpha, ma la chiave che firma e' un'altra:
    // la firma e' valida rispetto a se stessa, non rispetto al registro. E' la
    // scorciatoia che HF18 chiude.
    crypto::secret_key other_sk{};
    crypto::public_key other_pk{};
    crypto::generate_keys(other_pk, other_sk);

    tx_extra_mevatrust_uptime u{};
    u.node_id        = alpha.node_id;
    u.uptime_seconds = 100;
    u.timestamp      = 1700000000;
    u.peer_count     = 3;
    u.is_synced      = true;
    u.sync_height    = 999;
    u.node_pubkey    = other_pk;
    crypto::generate_signature(mevatrust::uptime_message_hash(u), other_pk, other_sk, u.signature);
    ASSERT_TRUE(mevatrust::verify_uptime_signature(u));

    ASSERT_EQ(feed_uptime_block(*m_manager, u, 100, 18), 0u);

    NodeRegistryEntry after{};
    ASSERT_TRUE(m_manager->node_registry()->get_node_by_id(alpha.node_id, after));
    EXPECT_EQ(after.last_sync_height, 1u)
        << "un uptime firmato con una chiave non registrata e' stato accettato";
}

TEST_F(RuleFixture, antisypil_uptime_foreign_key_allowed_before_hf18) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    add_node_as("alpha", pk);

    NodeRegistryEntry alpha{};
    ASSERT_TRUE(m_manager->node_registry()->get_node_by_id(m_node_id["alpha"], alpha));

    crypto::secret_key other_sk{};
    crypto::public_key other_pk{};
    crypto::generate_keys(other_pk, other_sk);

    tx_extra_mevatrust_uptime u{};
    u.node_id        = alpha.node_id;
    u.uptime_seconds = 100;
    u.timestamp      = 1700000000;
    u.peer_count     = 3;
    u.is_synced      = true;
    u.sync_height    = 999;
    u.node_pubkey    = other_pk;
    crypto::generate_signature(mevatrust::uptime_message_hash(u), other_pk, other_sk, u.signature);

    ASSERT_EQ(feed_uptime_block(*m_manager, u, 100, 0), 0u);

    NodeRegistryEntry after{};
    ASSERT_TRUE(m_manager->node_registry()->get_node_by_id(alpha.node_id, after));
    EXPECT_EQ(after.last_sync_height, 999u)
        << "sotto la soglia del fork la regola vecchia deve restare intatta";
}

// ─────────────────────────────────────────────────────────────────────────────
// Anti-Sybil Fase 2 (HF18) — la prova di possesso dati (tag 0xAC)
//
// La Fase 1 lega fino e uptime a una chiave registrata. Resta aperto il caso piu'
// economico: chi non possiede la catena puo' comunque firmare. La Fase 2 chiede
// una risposta su un blocco passato scelto dal protocollo (l'altezza bersaglio e'
// derivata da node_id e anchor, non scelta dal nodo), e la verifica contro la
// catena. Questi test bloccano la scorciatoia e verificano che sotto la HF il
// comportamento vecchio resti intatto.
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(RuleFixture, chainproof_target_is_deterministic_and_deep_enough) {
    const crypto::hash id_a = crypto::cn_fast_hash("node-a", 6);
    const crypto::hash id_b = crypto::cn_fast_hash("node-b", 6);
    const uint64_t anchor = 50000;

    uint64_t t_a1 = 0, t_a2 = 0, t_b = 0;
    ASSERT_TRUE(mevatrust::chain_proof_target(id_a, anchor, t_a1));
    ASSERT_TRUE(mevatrust::chain_proof_target(id_a, anchor, t_a2));
    ASSERT_TRUE(mevatrust::chain_proof_target(id_b, anchor, t_b));

    // Deterministico: due chiamate danno la stessa altezza.
    EXPECT_EQ(t_a1, t_a2);
    // Il nodo non sceglie la domanda: node_id diversi danno bersagli (quasi sempre)
    // diversi; qui si verifica che l'input conta, cioe' che non sia una costante.
    EXPECT_NE(t_a1, t_b);
    // Sempre a profondita' minima: il bersaglio sta fra MIN_DEPTH e MIN_DEPTH+SPAN
    // blocchi indietro. E' questo che impedisce di rispondere senza avere il blocco.
    EXPECT_LE(t_a1, anchor - mevatrust::consensus::CHAIN_PROOF_MIN_DEPTH);
    EXPECT_GE(t_a1, anchor - mevatrust::consensus::CHAIN_PROOF_MIN_DEPTH - (mevatrust::consensus::CHAIN_PROOF_SPAN - 1));
}

TEST_F(RuleFixture, chainproof_target_rejects_chain_too_short) {
    const crypto::hash id = crypto::cn_fast_hash("node", 4);
    uint64_t t = 0;
    // Sotto MIN_DEPTH+SPAN non esiste un bersaglio a profondita' sufficiente: la
    // funzione deve rifiutare, non produrre un bersaglio "vicino".
    EXPECT_FALSE(mevatrust::chain_proof_target(id, mevatrust::consensus::CHAIN_PROOF_MIN_DEPTH, t));
    EXPECT_FALSE(mevatrust::chain_proof_target(id, 0, t));
}

TEST_F(RuleFixture, chainproof_sample_window_and_idempotency) {
    auto eng = m_manager->mevatrust_engine();
    ASSERT_TRUE(eng);

    const crypto::hash id = crypto::cn_fast_hash("n", 1);
    EXPECT_FALSE(eng->has_recent_chain_proof(id, mevatrust::consensus::CHAIN_PROOF_VALIDITY_BLOCKS));

    // Primo campione accettato, secondo alla stessa altezza scartato (idempotenza).
    EXPECT_TRUE(eng->record_chain_proof_sample(id, 43100, 42500));
    EXPECT_FALSE(eng->record_chain_proof_sample(id, 43100, 42500));
    EXPECT_TRUE(eng->has_recent_chain_proof(id, mevatrust::consensus::CHAIN_PROOF_VALIDITY_BLOCKS));

    // Fuori dalla finestra: il campione piu' vecchio non tiene piu' il nodo eleggibile.
    const uint64_t far = 43100 + mevatrust::consensus::CHAIN_PROOF_VALIDITY_BLOCKS + 1;
    EXPECT_FALSE(eng->has_recent_chain_proof(id, far));

    // Altezza sotto la finestra: nessuna eleggibilita' possibile.
    EXPECT_FALSE(eng->has_recent_chain_proof(id, 100));

    eng->clear_chain_proof_samples();
    EXPECT_FALSE(eng->has_chain_proof_samples());
}

TEST_F(RuleFixture, chainproof_counts_below_window_no_dead_zone) {
    // Regressione: la HF anti-sybil entra a h2100 ma la finestra vale 43200 blocchi.
    // Con il vecchio ritorno anticipato su `height < CHAIN_PROOF_VALIDITY_BLOCKS`
    // nessun nodo era eleggibile per ~114 giorni dopo l'attivazione. Una prova
    // registrata dentro la finestra (qui l'intera catena, perche' height < finestra)
    // deve contare da subito.
    auto eng = m_manager->mevatrust_engine();
    ASSERT_TRUE(eng);
    const crypto::hash id = crypto::cn_fast_hash("early", 9);
    ASSERT_TRUE(eng->record_chain_proof_sample(id, 2100, 1500));
    EXPECT_TRUE(eng->has_recent_chain_proof(id, 2100));
    EXPECT_TRUE(eng->has_recent_chain_proof(id, 2101));
    // Fuori finestra decade come qualunque campione.
    EXPECT_FALSE(eng->has_recent_chain_proof(
        id, 2100 + mevatrust::consensus::CHAIN_PROOF_VALIDITY_BLOCKS + 1));
}

TEST_F(RuleFixture, chainproof_sample_survives_reload) {
    auto eng = m_manager->mevatrust_engine();
    ASSERT_TRUE(eng);
    const crypto::hash id = crypto::cn_fast_hash("persist", 7);
    ASSERT_TRUE(eng->record_chain_proof_sample(id, 50000, 49500));
    ASSERT_TRUE(eng->save_to_disk());

    eng->clear_chain_proof_samples();
    ASSERT_FALSE(eng->has_chain_proof_samples());

    ASSERT_TRUE(eng->load_from_disk());
    EXPECT_TRUE(eng->has_chain_proof_samples());
    EXPECT_TRUE(eng->has_recent_chain_proof(id, 51000))
        << "la prova non e' sopravvissuta al giro su disco";
}

/// Costruisce un blocco che contiene una singola tx 0xAC e lo consegna al
/// processing reale: stesso percorso che gira su ogni blocco della catena.
static bool feed_chain_proof_block(MevaTrustManager& mgr,
                                   const tx_extra_mevatrust_chain_proof& cp,
                                   uint64_t height, uint8_t hf_version) {
    std::vector<uint8_t> extra;
    if (!mevatrust::build_mevatrust_chain_proof_extra(cp, extra)) return false;
    transaction tx{};
    tx.version     = 1;
    tx.unlock_time = 60;
    tx.extra       = extra;
    const crypto::hash txid = get_transaction_hash(tx);

    block bl{};
    bl.major_version    = hf_version;
    bl.miner_tx.version = 1;
    bl.tx_hashes.push_back(txid);
    mgr.set_get_tx_func([&tx, &txid](const crypto::hash& id, transaction& out) -> bool {
        if (id == txid) { out = tx; return true; }
        return false;
    });
    mgr.process_mevatrust_txs(bl, height, hf_version);
    return true;
}

/// Prepara i pezzi comuni: nodo registrato con una chiave, un blocco "di catena"
/// all'altezza bersaglio e la prova firmata che lo attesta.
struct ChainProofScene {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    uint64_t anchor{0};
    uint64_t target{0};
    crypto::hash block_hash{};
    tx_extra_mevatrust_chain_proof cp{};

    bool build(MevaTrustManager& mgr, const crypto::hash& node_id) {
        anchor = 50000;
        if (!mevatrust::chain_proof_target(node_id, anchor, target)) return false;
        block target_blk{};
        target_blk.miner_tx.version  = 1;
        target_blk.miner_tx.unlock_time = 424242;   // contenuto deterministico e non vuoto
        block_hash = cryptonote::get_block_hash(target_blk);
        mgr.set_get_block_func([target = target, target_blk](uint64_t h, block& out) -> bool {
            if (h == target) { out = target_blk; return true; }
            return false;
        });
        return true;
    }
};

TEST_F(RuleFixture, chainproof_valid_proof_is_accepted_under_hf18) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    add_node_as("alpha", pk);

    ChainProofScene scene;
    scene.sk = sk;
    scene.pk = pk;
    ASSERT_TRUE(scene.build(*m_manager, m_node_id["alpha"]));

    tx_extra_mevatrust_chain_proof cp{};
    cp.node_id       = m_node_id["alpha"];
    cp.node_pubkey   = pk;
    cp.anchor_height = scene.anchor;
    cp.proof_height  = scene.target;
    cp.block_hash    = scene.block_hash;
    crypto::generate_signature(mevatrust::chain_proof_message_hash(cp), pk, sk, cp.signature);
    ASSERT_TRUE(mevatrust::verify_chain_proof_signature(cp));

    ASSERT_TRUE(feed_chain_proof_block(*m_manager, cp, scene.anchor, 18));
    EXPECT_TRUE(m_manager->mevatrust_engine()->has_recent_chain_proof(cp.node_id, scene.anchor))
        << "una prova valida, firmata con la chiave registrata e con l'hash giusto, e' stata scartata";
}

TEST_F(RuleFixture, chainproof_wrong_hash_rejected_under_hf18) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    add_node_as("alpha", pk);

    ChainProofScene scene;
    scene.sk = sk;
    scene.pk = pk;
    ASSERT_TRUE(scene.build(*m_manager, m_node_id["alpha"]));

    tx_extra_mevatrust_chain_proof cp{};
    cp.node_id       = m_node_id["alpha"];
    cp.node_pubkey   = pk;
    cp.anchor_height = scene.anchor;
    cp.proof_height  = scene.target;
    cp.block_hash    = crypto::cn_fast_hash("not-the-block", 13);  // hash inventato
    crypto::generate_signature(mevatrust::chain_proof_message_hash(cp), pk, sk, cp.signature);

    ASSERT_TRUE(feed_chain_proof_block(*m_manager, cp, scene.anchor, 18));
    EXPECT_FALSE(m_manager->mevatrust_engine()->has_chain_proof_samples())
        << "una prova con l'hash sbagliato e' stata accettata: la verifica contro la catena non morde";
}

TEST_F(RuleFixture, chainproof_stale_anchor_rejected_under_hf18) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    add_node_as("alpha", pk);

    ChainProofScene scene;
    scene.sk = sk;
    scene.pk = pk;
    ASSERT_TRUE(scene.build(*m_manager, m_node_id["alpha"]));

    tx_extra_mevatrust_chain_proof cp{};
    cp.node_id       = m_node_id["alpha"];
    cp.node_pubkey   = pk;
    cp.anchor_height = scene.anchor;
    cp.proof_height  = scene.target;
    cp.block_hash    = scene.block_hash;
    crypto::generate_signature(mevatrust::chain_proof_message_hash(cp), pk, sk, cp.signature);

    // Il blocco che contiene la prova e' troppo avanti rispetto all'anchor: la
    // prova e' stantia e non va rigiocata.
    const uint64_t too_late = scene.anchor + mevatrust::consensus::CHAIN_PROOF_MAX_ANCHOR_LAG + 1;
    ASSERT_TRUE(feed_chain_proof_block(*m_manager, cp, too_late, 18));
    EXPECT_FALSE(m_manager->mevatrust_engine()->has_chain_proof_samples())
        << "una prova con anchor stantio e' stata accettata";
}

TEST_F(RuleFixture, chainproof_foreign_key_rejected_under_hf18) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    add_node_as("alpha", pk);

    ChainProofScene scene;
    scene.sk = sk;
    scene.pk = pk;
    ASSERT_TRUE(scene.build(*m_manager, m_node_id["alpha"]));

    crypto::secret_key other_sk{};
    crypto::public_key other_pk{};
    crypto::generate_keys(other_pk, other_sk);

    // node_id vero, ma firma e chiave di un altro: valida rispetto a se stessa,
    // non rispetto al registro. E' la scorciatoia che la Fase 2 deve chiudere.
    tx_extra_mevatrust_chain_proof cp{};
    cp.node_id       = m_node_id["alpha"];
    cp.node_pubkey   = other_pk;
    cp.anchor_height = scene.anchor;
    cp.proof_height  = scene.target;
    cp.block_hash    = scene.block_hash;
    crypto::generate_signature(mevatrust::chain_proof_message_hash(cp), other_pk, other_sk, cp.signature);
    ASSERT_TRUE(mevatrust::verify_chain_proof_signature(cp));

    ASSERT_TRUE(feed_chain_proof_block(*m_manager, cp, scene.anchor, 18));
    EXPECT_FALSE(m_manager->mevatrust_engine()->has_chain_proof_samples())
        << "una prova firmata con una chiave non registrata e' stata accettata";
}

TEST_F(RuleFixture, chainproof_ignored_before_hf18) {
    crypto::secret_key sk{};
    crypto::public_key pk{};
    crypto::generate_keys(pk, sk);
    add_node_as("alpha", pk);

    ChainProofScene scene;
    scene.sk = sk;
    scene.pk = pk;
    ASSERT_TRUE(scene.build(*m_manager, m_node_id["alpha"]));

    tx_extra_mevatrust_chain_proof cp{};
    cp.node_id       = m_node_id["alpha"];
    cp.node_pubkey   = pk;
    cp.anchor_height = scene.anchor;
    cp.proof_height  = scene.target;
    cp.block_hash    = scene.block_hash;
    crypto::generate_signature(mevatrust::chain_proof_message_hash(cp), pk, sk, cp.signature);

    // Sotto la HF anti-sybil il tag 0xAC non esiste come concetto di consenso: non
    // deve registrare niente. E' cio' che tiene i blocchi pre-fork identici.
    ASSERT_TRUE(feed_chain_proof_block(*m_manager, cp, scene.anchor, 0));
    EXPECT_FALSE(m_manager->mevatrust_engine()->has_chain_proof_samples())
        << "sotto la soglia del fork la Fase 2 non deve avere effetto";
}

TEST_F(RuleFixture, chainproof_gates_payout_eligibility_under_hf18) {
    auto eng = m_manager->mevatrust_engine();
    ASSERT_TRUE(eng);
    add_node_as("alpha", fresh_key());

    const crypto::hash id = m_node_id["alpha"];
    const uint64_t height = mevatrust::consensus::UPTIME_WINDOW_BLOCKS;   // 43200, multiplo del periodo

    // Uptime sufficiente: cinque campioni a 12h l'uno dall'altro = due giorni
    // accreditati, cioe' il minimo per essere eleggibile. Senza la Fase 2 questo
    // nodo verrebbe pagato.
    uint64_t ts = 100000;
    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(eng->record_chain_uptime_sample(id, 1000 + i * 10, ts + i * 43200));

    auto standing = eng->compute_uptime_standing(id, height);
    ASSERT_TRUE(standing.eligible) << "scenario mal costruito: il nodo non ha uptime a sufficienza";

    // Con HF18 e nessuna prova di possesso dati: non eleggibile, nessun pagamento.
    std::vector<MevaTrustManager::expected_payout> out;
    std::string err;
    ASSERT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, 18, out, err))
        << "ricalcolo fallito: " << err;
    EXPECT_TRUE(out.empty())
        << "un nodo senza prova di possesso dati e' stato pagato sotto HF18";

    // Con la prova recente: torna eleggibile e viene pagato.
    ASSERT_TRUE(eng->record_chain_proof_sample(id, height - 100, height - 600));
    ASSERT_TRUE(m_manager->compute_expected_payouts(height, kBlockReward, 18, out, err))
        << "ricalcolo fallito: " << err;
    EXPECT_FALSE(out.empty())
        << "un nodo con prova di possesso dati valida non e' stato pagato sotto HF18";
}

// ─────────────────────────────────────────────────────────────────────────────
// Anti-Sybil Fase 3 (HF19): bond per la registrazione
//
// La registrazione e' una tx RCT: l'output verso il network fund ha amount==0 e
// la chiave e' quella one-time, non la spend pubkey del fondo. Il vecchio
// controllo (amount>=bond && key==spend_pub) non trovava mai il bond. Questi test
// bloccano l'importo decodificandolo con la view key del fondo, esattamente come
// fara' il manager.
// ─────────────────────────────────────────────────────────────────────────────

namespace {
// Chiave one-time dell'output per `addr` come la deriva il mittente.
crypto::public_key out_key_for(const crypto::secret_key& tx_sec,
                               const account_public_address& addr, size_t index) {
    crypto::key_derivation d;
    crypto::generate_key_derivation(addr.m_view_public_key, tx_sec, d);
    crypto::public_key out;
    crypto::derive_public_key(d, index, addr.m_spend_public_key, out);
    return out;
}
} // namespace

TEST_F(RuleFixture, governance_bond_transparent_output_is_counted) {
    const account_public_address gov = get_governance_address(MAINNET);
    cryptonote::keypair txkey = cryptonote::keypair::generate(hw::get_device("default"));

    transaction tx{};
    tx.version = 1;
    add_tx_pub_key_to_extra(tx, txkey.pub);

    const uint64_t bond = mevatrust::consensus::NODE_REGISTRATION_BOND_ATOMIC;
    crypto::view_tag vt{};
    tx_out o;
    set_tx_out(bond, out_key_for(txkey.sec, gov, 0), false, vt, o);
    tx.vout.push_back(o);

    EXPECT_EQ(mevatrust::governance_deposit_amount(tx, MAINNET), bond);
}

TEST_F(RuleFixture, governance_bond_transparent_below_minimum_is_not_enough) {
    const account_public_address gov = get_governance_address(MAINNET);
    cryptonote::keypair txkey = cryptonote::keypair::generate(hw::get_device("default"));

    transaction tx{};
    tx.version = 1;
    add_tx_pub_key_to_extra(tx, txkey.pub);

    const uint64_t too_little = mevatrust::consensus::NODE_REGISTRATION_BOND_ATOMIC - 1;
    crypto::view_tag vt{};
    tx_out o;
    set_tx_out(too_little, out_key_for(txkey.sec, gov, 0), false, vt, o);
    tx.vout.push_back(o);

    EXPECT_EQ(mevatrust::governance_deposit_amount(tx, MAINNET), too_little);
    EXPECT_LT(mevatrust::governance_deposit_amount(tx, MAINNET),
              mevatrust::consensus::NODE_REGISTRATION_BOND_ATOMIC);
}

TEST_F(RuleFixture, governance_bond_to_other_recipient_is_not_counted) {
    cryptonote::keypair txkey = cryptonote::keypair::generate(hw::get_device("default"));
    cryptonote::keypair other = cryptonote::keypair::generate(hw::get_device("default"));
    account_public_address other_addr{other.pub, other.pub};

    transaction tx{};
    tx.version = 1;
    add_tx_pub_key_to_extra(tx, txkey.pub);

    crypto::view_tag vt{};
    tx_out o;
    set_tx_out(mevatrust::consensus::NODE_REGISTRATION_BOND_ATOMIC,
               out_key_for(txkey.sec, other_addr, 0), false, vt, o);
    tx.vout.push_back(o);

    EXPECT_EQ(mevatrust::governance_deposit_amount(tx, MAINNET), 0u)
        << "un output verso un altro destinatario e' stato contato come bond";
}

TEST_F(RuleFixture, governance_bond_hidden_in_rct_output_is_decoded) {
    const account_public_address gov = get_governance_address(MAINNET);
    cryptonote::keypair txkey = cryptonote::keypair::generate(hw::get_device("default"));
    const uint64_t bond = mevatrust::consensus::NODE_REGISTRATION_BOND_ATOMIC;

    transaction tx{};
    tx.version = 2;
    add_tx_pub_key_to_extra(tx, txkey.pub);

    crypto::view_tag vt{};
    tx_out o0;
    set_tx_out(0, out_key_for(txkey.sec, gov, 0), false, vt, o0);  // importo nascosto
    tx.vout.push_back(o0);
    cryptonote::keypair other = cryptonote::keypair::generate(hw::get_device("default"));
    tx_out o1;
    set_tx_out(0, other.pub, false, vt, o1);                      // estraneo
    tx.vout.push_back(o1);

    hw::device& hwdev = hw::get_device("default");
    tx.rct_signatures.type = rct::RCTTypeCLSAG;
    tx.rct_signatures.outPk.resize(2);
    tx.rct_signatures.ecdhInfo.resize(2);

    crypto::key_derivation d0;
    ASSERT_TRUE(crypto::generate_key_derivation(gov.m_view_public_key, txkey.sec, d0));
    crypto::secret_key s0;
    hwdev.derivation_to_scalar(d0, 0, s0);
    const rct::key shared0 = rct::sk2rct(s0);
    const rct::key mask0 = rct::genCommitmentMask(shared0);
    rct::ecdhTuple et0;
    et0.amount = rct::d2h(bond);
    hwdev.ecdhEncode(et0, shared0, true);
    tx.rct_signatures.ecdhInfo[0] = et0;
    rct::addKeys2(tx.rct_signatures.outPk[0].mask, mask0, rct::d2h(bond), rct::H);

    crypto::key_derivation d1;
    ASSERT_TRUE(crypto::generate_key_derivation(other.pub, txkey.sec, d1));
    crypto::secret_key s1;
    hwdev.derivation_to_scalar(d1, 1, s1);
    const rct::key shared1 = rct::sk2rct(s1);
    const rct::key mask1 = rct::genCommitmentMask(shared1);
    rct::ecdhTuple et1;
    et1.amount = rct::zero();
    hwdev.ecdhEncode(et1, shared1, true);
    tx.rct_signatures.ecdhInfo[1] = et1;
    rct::addKeys2(tx.rct_signatures.outPk[1].mask, mask1, rct::zero(), rct::H);

    EXPECT_EQ(mevatrust::governance_deposit_amount(tx, MAINNET), bond)
        << "l'importo RCT verso la tesoreria non e' stato decodificato";
}

// Il consenso protegge gli output verso la tesoreria tramite le loro key image.
// Verifica che governance_owned_outputs() produca esattamente la key image che
// il detentore della chiave (derivabile) produrrebbe, cosi' check_premine_spend
// puo' pretendere il tag 0xB0 quando quell'output viene speso.
TEST_F(RuleFixture, governance_owned_outputs_key_image_matches) {
    const account_public_address gov = get_governance_address(MAINNET);
    const crypto::secret_key gov_sk = derive_premine_secret_key("mevacoin_governance", MAINNET);
    cryptonote::keypair txkey = cryptonote::keypair::generate(hw::get_device("default"));
    const uint64_t bond = mevatrust::consensus::NODE_REGISTRATION_BOND_ATOMIC;

    transaction tx{};
    tx.version = 1;
    add_tx_pub_key_to_extra(tx, txkey.pub);
    crypto::view_tag vt{};
    tx_out o;
    set_tx_out(bond, out_key_for(txkey.sec, gov, 0), false, vt, o);
    tx.vout.push_back(o);

    std::vector<crypto::key_image> kis;
    uint64_t total = 0;
    mevatrust::governance_owned_outputs(tx, MAINNET, kis, total);

    ASSERT_EQ(kis.size(), 1u);
    EXPECT_EQ(total, bond);

    crypto::key_derivation d;
    ASSERT_TRUE(crypto::generate_key_derivation(txkey.pub, gov_sk, d));
    crypto::secret_key out_sec;
    crypto::derive_secret_key(d, 0, gov_sk, out_sec);
    crypto::key_image expected;
    crypto::generate_key_image(out_key_for(txkey.sec, gov, 0), out_sec, expected);

    EXPECT_EQ(kis[0], expected)
        << "la key image tracciata non corrisponde a quella dell'output di bond";
}

} // namespace
