#include <gtest/gtest.h>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "cryptonote_core/mevatrust/frost_threshold.h"
#include "cryptonote_core/mevatrust/pool_distribution.h"
#include "cryptonote_core/mevatrust/mevatrust_tx_parser.h"
#include "cryptonote_basic/tx_extra.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "string_tools.h"

using namespace cryptonote;
using namespace cryptonote::mevatrust;

namespace {

bool frost_sign_distribution(
    uint64_t height,
    uint32_t period,
    const std::vector<std::pair<crypto::public_key, uint64_t>>& outputs,
    frost::FrostSignature& sig_out)
{
    const std::vector<uint8_t> signer_indices = {1, 2, 3};

    // Chiavi di cerimonia generate qui, non copie di quelle deployate.
    //
    // Il test aveva le share private della cerimonia hardcoded, ma la cerimonia
    // e' stata ruotata (gov_crypto frost-keygen, 2026-09-23) e CONSENSUS_PROPOSER_PUBKEYS
    // contiene le chiavi nuove: le share nel test non derivavano piu' nessuna di
    // quelle, e il sanity check falliva. Le share nuove sono con i cinque
    // operatori, non nel repo, quindi il test non era recuperabile com'era.
    //
    // Qui si genera un pacchetto di cerimonia fresco con frost_keygen(): e' lo
    // stesso percorso della cerimonia reale, quindi il test verifica davvero la
    // matematica (Shamir, Lagrange, binding factors, aggregazione). Quello che
    // NON verifica, e che va detto: che le chiavi di cerimonia effettivamente
    // in uso sui nodi siano quelle giuste. Per quello servono le share, e quelle
    // non sono nel repo per scelta. Copre ceremony_keys_are_self_consistent()
    // solo la forma dell'array, non la sua verita'.
    frost::FrostKeyPackage keypkg{};
    if (!frost::frost_keygen(keypkg)) { std::cerr << "DIAG keygen FAIL\n"; return false; }

    std::vector<frost::SignerKeypair> kp(frost::FROST_N);
    for (size_t i = 0; i < frost::FROST_N; ++i)
        kp[i] = keypkg.signer_keypairs[i];
    const crypto::public_key& group_pk = keypkg.group_public_key;

    crypto::hash msg_hash = frost::create_distribution_message_hash(height, period, outputs);

    std::vector<crypto::ec_scalar> lambdas;
    if (!frost::compute_lagrange_coeffs(signer_indices, lambdas)) { std::cerr << "DIAG lagrange FAIL\n"; return false; }
    if (lambdas.size() != signer_indices.size()) return false;

    std::vector<frost::NoncePair> nonces(signer_indices.size());
    std::vector<std::pair<crypto::public_key, crypto::public_key>> comms(signer_indices.size());
    for (size_t i = 0; i < signer_indices.size(); ++i) {
        if (!frost::generate_nonces(nonces[i])) return false;
        frost::nonce_commitments(nonces[i], comms[i].first, comms[i].second);
    }

    std::vector<crypto::ec_scalar> rho;
    if (!frost::compute_binding_factors(signer_indices, msg_hash, comms, rho)) { std::cerr << "DIAG binding FAIL\n"; return false; }

    crypto::public_key R;
    if (!frost::compute_aggregate_r(comms, rho, R)) { std::cerr << "DIAG aggR FAIL\n"; return false; }

    std::vector<frost::PartialSignature> partials;
    for (size_t i = 0; i < signer_indices.size(); ++i) {
        frost::PartialSignature ps;
        ps.signer_index = signer_indices[i];
        if (!frost::sign_partial(msg_hash, nonces[i], kp[signer_indices[i]-1].sec,
                                 lambdas[i], rho[i], R, group_pk, ps))
            { std::cerr << "DIAG sign_partial FAIL i=" << i << "\n"; return false; }
        std::string perr;
        if (!frost::verify_partial(msg_hash, ps, lambdas[i], rho[i], R,
                                   group_pk,
                                   kp[signer_indices[i]-1].pub, perr))
            { std::cerr << "DIAG verify_partial FAIL i=" << i << " err=" << perr << "\n"; return false; }
        partials.push_back(std::move(ps));
    }

    if (!frost::aggregate_signatures(msg_hash, signer_indices, partials, rho,
                                     group_pk, keypkg.signer_public_keys, sig_out))
        return false;

    frost::PublicKeyPackage pkg;
    pkg.agg_pubkey = group_pk;
    for (size_t i = 0; i < frost::FROST_N; ++i) pkg.signer_pubkeys[i] = kp[i].pub;
    std::string verr;
    bool v = frost::verify_signature(sig_out, pkg, verr);
    if (!v) std::cerr << "DIAG verify_signature FAIL err=" << verr << "\n";
    return v;
}

// Cosa NON coprono i test FROST, detto nel codice.
//
// I test firmano con un pacchetto di cerimonia generato da frost_keygen(), non
// con le share della cerimonia deployata: quelle sono con i cinque operatori e
// non sono nel repo. Quindi verificano che il protocollo sia matematicamente
// giusto, non che le chiavi in uso sui nodi siano quelle giuste.
//
// Questo test mette nero su bianco il perimetro residuo: controlla solo la forma
// dei dati. Se qualcuno cambiasse una chiave di cerimonia con un valore arbitrario
// e nessuno avesse la share corrispondente, questi test continuerebbero a
// passare. Serve un controllo fuori banda, non un test.
TEST(mevatrust_frost, ceremony_keys_are_shaped_but_not_verified)
{
    // Esistono e sono valori distinti: il minimo che serve per non firmare e
    // aggregare tutto con la stessa chiave.
    std::set<std::string> seen;
    for (size_t i = 0; i < frost::FROST_N; ++i) {
        const std::string hex = epee::string_tools::pod_to_hex(frost::CONSENSUS_PROPOSER_PUBKEYS[i]);
        EXPECT_EQ(hex.size(), 64u) << "una chiave di cerimonia non e' un punto da 32 byte";
        EXPECT_FALSE(hex.empty());
        EXPECT_TRUE(seen.insert(hex).second)
            << "due firmatari hanno la stessa chiave pubblica";
    }
    EXPECT_EQ(frost::FROST_N, 5u);
    EXPECT_EQ(frost::FROST_T, 3u);
    EXPECT_EQ(sizeof(frost::CONSENSUS_GROUP_PUBKEY.data), 32u);

    // Quello che non si puo' fare qui: nessuna share privata disponibile nel
    // repo con cui derivare queste chiavi e provarle. Dichiarato perche' il
    // limite sia leggibile, non perche' il test lo verifichi.
}

} // anonymous namespace

// ── FROST Consensus Path Tests ──────────────────────────────────────────

// End-to-end over the exact code the coinbase validator will run when a real
// distribution fires on-chain:
//   1. sign a distribution message with real ceremony shares (3-of-5)
//   2. construct the 0xAA pool-distribution extra
//   3. parse it back
//   4. validate_pool_distribution -> verify_distribution_signature (consensus)
TEST(mevatrust_frost, consensus_full_round_and_validate)
{
    const uint64_t height = 720;
    const uint32_t period = 3;

    // Two reward recipients (spend keys only, what 0xAA stores)
    crypto::public_key pk_a{}, pk_b{};
    crypto::secret_key sk_a{}, sk_b{};
    crypto::generate_keys(pk_a, sk_a);
    crypto::generate_keys(pk_b, sk_b);
    std::vector<std::pair<crypto::public_key, uint64_t>> outputs = {
        {pk_a, 1000000000ULL}, {pk_b, 2500000000ULL},
    };

    frost::FrostSignature sig;
    ASSERT_TRUE(frost_sign_distribution(height, period, outputs, sig));

    // Build the 0xAA extra (manager path uses build_distribution_outputs with
    // address-pairs; here we drive construct_pool_distribution_extra which
    // takes rewards + balance and produces dist.outputs == spend keys).
    std::vector<NodeCoinbaseReward> rewards;
    NodeCoinbaseReward r1{}, r2{};
    r1.amount = 1000000000ULL; r1.address.m_spend_public_key = pk_a; r1.address.m_view_public_key = pk_a;
    r2.amount = 2500000000ULL; r2.address.m_spend_public_key = pk_b; r2.address.m_view_public_key = pk_b;
    rewards.push_back(r1); rewards.push_back(r2);

    const uint64_t pool_balance = 3500000000ULL;
    std::vector<uint8_t> extra;
    ASSERT_TRUE(mevatrust::construct_pool_distribution_extra(
        height, period, pool_balance, rewards, sig, extra));
    ASSERT_FALSE(extra.empty());

    // Parse back from a real transaction extra (same as coinbase validator)
    transaction tx;
    tx.extra.assign(extra.begin(), extra.end());
    tx_extra_mevatrust_pool_distribution dist;
    ASSERT_TRUE(mevatrust::parse_mevatrust_pool_distribution_from_tx(tx, dist));

    // Consensus validation
    mevatrust::ProposerState proposers;
    for (size_t i = 0; i < frost::FROST_N; ++i)
        proposers.pubkeys[i] = frost::CONSENSUS_PROPOSER_PUBKEYS[i];

    std::string err;
    ASSERT_TRUE(mevatrust::validate_pool_distribution(
        dist, height, period, pool_balance, proposers, err)) << err;
}

// Tampering with an output amount MUST invalidate the signature.
TEST(mevatrust_frost, tampered_output_rejected)
{
    const uint64_t height = 720;
    const uint32_t period = 3;
    crypto::public_key pk_a{}, pk_b{};
    crypto::secret_key sk_a{}, sk_b{};
    crypto::generate_keys(pk_a, sk_a);
    crypto::generate_keys(pk_b, sk_b);
    std::vector<std::pair<crypto::public_key, uint64_t>> outputs = {
        {pk_a, 1000000000ULL}, {pk_b, 2500000000ULL},
    };

    frost::FrostSignature sig;
    ASSERT_TRUE(frost_sign_distribution(height, period, outputs, sig));

    std::vector<NodeCoinbaseReward> rewards;
    NodeCoinbaseReward r1{}, r2{};
    r1.amount = 1000000000ULL; r1.address.m_spend_public_key = pk_a; r1.address.m_view_public_key = pk_a;
    r2.amount = 2500000000ULL; r2.address.m_spend_public_key = pk_b; r2.address.m_view_public_key = pk_b;
    rewards.push_back(r1); rewards.push_back(r2);
    const uint64_t pool_balance = 3500000000ULL;

    std::vector<uint8_t> extra;
    ASSERT_TRUE(mevatrust::construct_pool_distribution_extra(
        height, period, pool_balance, rewards, sig, extra));

    transaction tx;
    tx.extra.assign(extra.begin(), extra.end());
    tx_extra_mevatrust_pool_distribution dist;
    ASSERT_TRUE(mevatrust::parse_mevatrust_pool_distribution_from_tx(tx, dist));

    // Corrupt the second output amount
    ASSERT_EQ(dist.outputs.size(), 2);
    dist.outputs[1].second += 1;

    mevatrust::ProposerState proposers;
    for (size_t i = 0; i < frost::FROST_N; ++i)
        proposers.pubkeys[i] = frost::CONSENSUS_PROPOSER_PUBKEYS[i];

    std::string err;
    ASSERT_FALSE(mevatrust::validate_pool_distribution(
        dist, height, period, pool_balance, proposers, err));
}

// A wrong period must be rejected regardless of signature.
TEST(mevatrust_frost, wrong_period_rejected)
{
    const uint64_t height = 720;
    const uint32_t period = 3;
    crypto::public_key pk_a{};
    crypto::secret_key sk_a{};
    crypto::generate_keys(pk_a, sk_a);
    std::vector<std::pair<crypto::public_key, uint64_t>> outputs = {
        {pk_a, 500000000ULL},
    };

    frost::FrostSignature sig;
    ASSERT_TRUE(frost_sign_distribution(height, period, outputs, sig));

    std::vector<NodeCoinbaseReward> rewards;
    NodeCoinbaseReward r1{};
    r1.amount = 500000000ULL; r1.address.m_spend_public_key = pk_a; r1.address.m_view_public_key = pk_a;
    rewards.push_back(r1);
    const uint64_t pool_balance = 500000000ULL;

    std::vector<uint8_t> extra;
    ASSERT_TRUE(mevatrust::construct_pool_distribution_extra(
        height, period, pool_balance, rewards, sig, extra));

    transaction tx;
    tx.extra.assign(extra.begin(), extra.end());
    tx_extra_mevatrust_pool_distribution dist;
    ASSERT_TRUE(mevatrust::parse_mevatrust_pool_distribution_from_tx(tx, dist));

    mevatrust::ProposerState proposers;
    for (size_t i = 0; i < frost::FROST_N; ++i)
        proposers.pubkeys[i] = frost::CONSENSUS_PROPOSER_PUBKEYS[i];

    std::string err;
    ASSERT_FALSE(mevatrust::validate_pool_distribution(
        dist, height, period + 1, pool_balance, proposers, err));
}