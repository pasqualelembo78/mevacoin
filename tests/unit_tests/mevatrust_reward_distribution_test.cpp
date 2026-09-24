#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>

#include "cryptonote_core/mevatrust/node_registry.h"
#include "cryptonote_core/mevatrust/mevatrust_engine.h"
#include "cryptonote_core/mevatrust/reward_distributor.h"
#include "cryptonote_basic/tx_extra.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "string_tools.h"
#include "string_tools.h"


using namespace cryptonote;

namespace {

std::string create_temp_dir() {
    char tmpl[] = "/tmp/mevatrust_reward_XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) throw std::runtime_error("mkdtemp failed");
    return std::string(dir);
}

void cleanup_temp_dir(const std::string& dir) {
    std::string cmd = "rm -rf " + dir;
    int rc = system(cmd.c_str());
    (void)rc;
}

crypto::hash make_node_id(const crypto::public_key& wpk,
                          const crypto::public_key& npk) {
    std::string m;
    m.reserve(64);
    m.append((const char*)&wpk, sizeof(wpk));
    m.append((const char*)&npk, sizeof(npk));
    return crypto::cn_fast_hash(m.data(), m.size());
}

} // anonymous namespace

// ============================================================================
// REGRESSION: FROST signing never fired on-chain because the RewardDistributor
// rejected every period boundary.
//
// Root cause (reward_distributor.cpp:263):
//   if (m_pool_balance == 0) return true;   // ← the gate
//
// m_pool_balance (LMDB-cached, rewarded-distributor state) was ALWAYS 0:
// the MevaTrustManager computed the chain pool balance (calculate_mevatrust_
// pool_contribution) but NEVER passed it to the distributor. The distributor
// short-circuited distribution on EVERY period boundary → no pending coinbase
// output → the 0xAA FROST coinbase tag was never attached → the FROST FROST
// consensus path (frost_threshold.cpp) was dead on-chain.
//
// FIX (this repo): added RewardDistributor::set_pool_balance() and wired it
// into the manager's trigger_distribution path — the consensus gate now sees
// the REAL chain pool balance.
//
// This test drives the REAL LMDB consensus stack (NodeRegistry + MevaTrustEngine
// + RewardDistributor) and proves: with a registered node scoring above the
// threshold AND a set pool balance, distribute_rewards() produces pending
// coinbase outputs (the exact condition that gate lines 260-263 used to kill).
// ============================================================================
TEST(mevatrust_reward, distribution_produces_pending_with_pool_balance)
{
    const std::string tmpdir = create_temp_dir();
    try {
        const std::string reg_db  = tmpdir + "/registry";
        const std::string eng_db  = tmpdir + "/engine";
        const std::string dist_db = tmpdir + "/distributor";

        // ── 1. Build the real LMDB-backed consensus stack ────────────────────
        auto registry    = std::make_shared<NodeRegistry>(reg_db);
        auto engine      = std::make_shared<MevaTrustEngine>(eng_db, registry);
        auto distributor = std::make_shared<RewardDistributor>(dist_db);

        // ── 2. Deterministic keys → register node on-chain (consensus) ───────
        crypto::secret_key w_sk, n_sk;
        crypto::public_key w_pk, n_pk;
        crypto::generate_keys(w_pk, w_sk);
        crypto::generate_keys(n_pk, n_sk);

        tx_extra_mevatrust_registration reg;
        reg.node_id        = make_node_id(w_pk, n_pk);
        reg.wallet_pubkey  = w_pk;
        reg.node_pubkey    = n_pk;
        reg.wallet_address = "test_wallet_" +
                             epee::string_tools::pod_to_hex(w_pk);
        reg.port = 18080;

        const uint64_t reg_height = 10;
        ASSERT_TRUE(registry->register_node_onchain(reg, reg_height));

        // ── 3. Feed uptime so the node has non-zero score ────────────────────
        uint64_t now = (uint64_t)std::time(nullptr);
        for (uint64_t h = reg_height; h < reg_height + 20; ++h) {
            UptimeEvent ev;
            ev.timestamp    = now + (h - reg_height) * 30;
            ev.online       = true;
            ev.block_height = h;
            ev.peer_count   = 4;
            ev.response_time_ms = 90;
            ev.ip_address   = "10.0.0.1";
            ASSERT_TRUE(engine->record_uptime_event(reg.node_id, ev));
        }

        // ── 4. THE FIX: non-zero pool balance (pre-fix this was always 0) ────
        //        → the early-return gate at reward_distributor.cpp:263 no
        //          longer blocks distribution.
        const uint64_t pool_amount = 5'000'000'000'000ull;
        distributor->set_pool_balance(pool_amount);   // ignores if chain pool
        distributor->set_min_score(0.05f);            // threshold: node (0.4)
                                                      // comfortably above

        // ── 5. Distribute at a period boundary (period = 240) ────────────────
        ASSERT_TRUE(distributor->distribute_rewards(engine, 1440));

        // ── 6. PENDING COINBASE OUTPUTS MUST EXIST ───────────────────────────
        ASSERT_TRUE(distributor->has_pending_outputs());
        auto pending = distributor->get_pending_coinbase_outputs();
        ASSERT_FALSE(pending.empty());
        uint64_t total = 0;
        for (const auto& o : pending) total += o.amount;
        ASSERT_GT(total, (uint64_t)0);
        // pool drained
        ASSERT_LT(distributor->get_pool_balance(), pool_amount);
        ASSERT_GT(distributor->get_total_distributed(), (uint64_t)0);
    } catch (...) {
        cleanup_temp_dir(tmpdir);
        throw;
    }
    cleanup_temp_dir(tmpdir);
}

// ============================================================================
// Sanity: with pool_balance == 0 (pre-fix default) the distributor returns
// early and produces NO pending outputs — this is exactly the deadlock that
// the set_pool_balance fix removes.
// ============================================================================
TEST(mevatrust_reward, no_pending_without_pool_balance)
{
    const std::string tmpdir = create_temp_dir();
    try {
        const std::string reg_db  = tmpdir + "/registry2";
        const std::string eng_db  = tmpdir + "/engine2";
        const std::string dist_db = tmpdir + "/distributor2";

        auto registry    = std::make_shared<NodeRegistry>(reg_db);
        auto engine      = std::make_shared<MevaTrustEngine>(eng_db, registry);
        auto distributor = std::make_shared<RewardDistributor>(dist_db);

        crypto::secret_key w_sk, n_sk;
        crypto::public_key w_pk, n_pk;
        crypto::generate_keys(w_pk, w_sk);
        crypto::generate_keys(n_pk, n_sk);

        tx_extra_mevatrust_registration reg;
        reg.node_id        = make_node_id(w_pk, n_pk);
        reg.wallet_pubkey  = w_pk;
        reg.node_pubkey    = n_pk;
        reg.wallet_address = "test_wallet2_" +
                             epee::string_tools::pod_to_hex(w_pk);
        reg.port = 18081;
        const uint64_t reg_height = 10;
        ASSERT_TRUE(registry->register_node_onchain(reg, reg_height));

        uint64_t now = (uint64_t)std::time(nullptr);
        for (uint64_t h = reg_height; h < reg_height + 60; ++h) {
            UptimeEvent ev;
            ev.timestamp    = now + (h - reg_height) * 30;
            ev.online       = true;
            ev.block_height = h;
            ev.peer_count   = 4;
            ev.response_time_ms = 90;
            ev.ip_address   = "10.0.0.1";
            ASSERT_TRUE(engine->record_uptime_event(reg.node_id, ev));
        }

        // NO set_pool_balance → pool stays 0 → early-return at the 0xAA gate.
        distributor->set_min_score(0.05f);
        ASSERT_TRUE(distributor->distribute_rewards(engine, 1440));

        ASSERT_FALSE(distributor->has_pending_outputs());
        ASSERT_TRUE(distributor->get_pending_coinbase_outputs().empty());
        ASSERT_EQ(distributor->get_total_distributed(), (uint64_t)0);
    } catch (...) {
        cleanup_temp_dir(tmpdir);
        throw;
    }
    cleanup_temp_dir(tmpdir);
}
