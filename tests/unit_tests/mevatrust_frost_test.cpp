#include <gtest/gtest.h>
#include <cstring>
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
int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool decode_hex(const std::string& hex, crypto::secret_key& out) {
    if (hex.size() != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
        int hi = hexval(hex[i*2]), lo = hexval(hex[i*2+1]);
        if (hi < 0 || lo < 0) return false;
        out.data[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

// Test certificate issuance under the exact manager signing path:
//   signer_indices {1,2,3} -> nonces -> commitments -> binding factors ->
//   aggregate R -> per-partial verify -> aggregate -> verify vs ceremony Y.
bool frost_sign_distribution(
    uint64_t height,
    uint32_t period,
    const std::vector<std::pair<crypto::public_key, uint64_t>>& outputs,
    frost::FrostSignature& sig_out)
{
    const std::vector<uint8_t> signer_indices = {1, 2, 3};

    // Ceremony private shares (test-only copy; fixed at the compile-time
    // ceremony, individually matching CONSENSUS_PROPOSER_PUBKEYS).
    // sec1..sec3 are the shares held by signers 1..3.
    const std::vector<std::string> share_hex = {
        "415ec82c42e2f28db494d4b44ff5c11de8b12bbec768c079e4e316cffa697d09",
        "1da4fd52f3f51d16f9577ea341a2506a1eb7eaa18e57c0945fd81e3b0fe73c05",
        "9ca761fa6cb68c6ba9c927de34881076e465b8261d7516c5907fccf11f5e1c04",
        "be68f422af233f8ec5e9d06429a701413abe944c73c1c20a78d91ff32ccf1b06",
        "83e7b5ccb93d357e4db879371fff23cb1fc07f13913cc56515e6183f363a3b0b",
    };

    std::vector<frost::SignerKeypair> kp(share_hex.size());
    for (size_t i = 0; i < share_hex.size(); ++i) {
        if (!decode_hex(share_hex[i], kp[i].sec)) return false;
        crypto::secret_key_to_public_key(kp[i].sec, kp[i].pub);
        kp[i].index = static_cast<uint8_t>(i + 1);
    }

    // Sanity: keypairs must be the ceremony set.
    for (size_t i = 0; i < frost::FROST_N; ++i) {
        if (memcmp(&kp[i].pub, &frost::CONSENSUS_PROPOSER_PUBKEYS[i], 32) != 0)
            return false;
    }

    crypto::hash msg_hash = frost::create_distribution_message_hash(height, period, outputs);

    std::vector<crypto::ec_scalar> lambdas;
    if (!frost::compute_lagrange_coeffs(signer_indices, lambdas)) return false;
    if (lambdas.size() != signer_indices.size()) return false;

    std::vector<frost::NoncePair> nonces(signer_indices.size());
    std::vector<std::pair<crypto::public_key, crypto::public_key>> comms(signer_indices.size());
    for (size_t i = 0; i < signer_indices.size(); ++i) {
        if (!frost::generate_nonces(nonces[i])) return false;
        frost::nonce_commitments(nonces[i], comms[i].first, comms[i].second);
    }

    std::vector<crypto::ec_scalar> rho;
    if (!frost::compute_binding_factors(signer_indices, msg_hash, comms, rho)) return false;

    crypto::public_key R;
    if (!frost::compute_aggregate_r(comms, rho, R)) return false;

    std::vector<frost::PartialSignature> partials;
    for (size_t i = 0; i < signer_indices.size(); ++i) {
        frost::PartialSignature ps;
        ps.signer_index = signer_indices[i];
        if (!frost::sign_partial(msg_hash, nonces[i], kp[signer_indices[i]-1].sec,
                                 lambdas[i], rho[i], R, frost::CONSENSUS_GROUP_PUBKEY, ps))
            return false;
        std::string perr;
        if (!frost::verify_partial(msg_hash, ps, lambdas[i], rho[i], R,
                                   frost::CONSENSUS_GROUP_PUBKEY,
                                   frost::CONSENSUS_PROPOSER_PUBKEYS[signer_indices[i]-1], perr))
            return false;
        partials.push_back(std::move(ps));
    }

    if (!frost::aggregate_signatures(msg_hash, signer_indices, partials, rho,
                                     frost::CONSENSUS_GROUP_PUBKEY, sig_out))
        return false;

    frost::PublicKeyPackage pkg;
    pkg.agg_pubkey = frost::CONSENSUS_GROUP_PUBKEY;
    pkg.signer_pubkeys = frost::CONSENSUS_PROPOSER_PUBKEYS;
    std::string verr;
    return frost::verify_signature(sig_out, pkg, verr);
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