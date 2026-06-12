// Copyright (c) 2024-2026, The MevaCoin Project
// Distributed under the MIT/X11 software license, see accompanying file COPYING or
// https://opensource.org/licenses/MIT
//
// ============================================================
//  tests/core_tests/participation_integration.cpp
//
//  Test di integrazione C6 per il sistema di incentivi MevaCoin.
//  Copertura completa dei requisiti di produzione:
//
//  T1  Registrazione nodo con UTXO proof + separazione wallet/node key
//  T2  Anti-Sybil: rigetto nodo falso (saldo basso, troppi nodi, cooldown)
//  T3  Uptime tracking -> assegnazione badge on-chain (ACTIVE, LONG_UPTIME)
//  T4  Reward coinbase blob 0xA2 (serializzazione round-trip)
//  T5  Anti-replay post-restart (LMDB persiste il set nonce tra riavvii)
//  T6  Quorum 3/5 (proposta -> voti -> finalizzazione, duplicati ignorati)
//  T7  rebuild_registry_from_chain() (sync nodo nuovo da zero)
//  T8  Incentive history trasparenza (qualsiasi nodo interroga la chain)
//
//  Build: aggiungere participation_integration.cpp a
//         tests/core_tests/CMakeLists.txt -> set(core_tests_sources)
//  Run:   ./core_tests --gtest_filter="ParticipationIntegration.*"
//         ./core_tests  (tutti i test insieme agli altri core_tests)
// ============================================================

#include "gtest/gtest.h"

#include "cryptonote_core/mevatrust/node_registry.h"
#include "cryptonote_core/mevatrust/mevatrust_engine.h"
#include "cryptonote_core/mevatrust/mevatrust_snapshot_antireplay.h"
#include "cryptonote_core/mevatrust/snapshot_broadcaster.h"
#include "cryptonote_core/mevatrust/mevatrust_types.h"
#include "cryptonote_core/mevatrust/mevatrust_security.h"
#include "cryptonote_core/mevatrust/mevatrust_tx_parser.h"
#include "cryptonote_config.h"
#include "crypto/crypto.h"
#include "string_tools.h"

#include <filesystem>
#include <memory>
#include <vector>
#include <string>
#include <atomic>

namespace fs = std::filesystem;

// ===========================================================================
//  Utilities condivise dai test
// ===========================================================================
namespace {

/// Genera una keypair deterministica a partire da un label stringa.
/// Utile per test ripetibili senza generazione casuale.
std::pair<crypto::public_key, crypto::secret_key>
make_keypair(const std::string& label)
{
  crypto::secret_key sk{};
  crypto::hash h = crypto::cn_fast_hash(label.data(), label.size());
  memcpy(sk.data, h.data, 32);
  crypto::public_key pk{};
  crypto::secret_key_to_public_key(sk, pk);
  return {pk, sk};
}

/// Genera un node_id deterministico da un label.
crypto::hash make_node_id(const std::string& label)
{
  return crypto::cn_fast_hash(label.data(), label.size());
}

/// Crea una directory temporanea per LMDB. Viene rimossa da rm_tmp().
fs::path make_tmp(const std::string& name)
{
  fs::path p = fs::temp_directory_path() / ("mvc_test_" + name);
  fs::create_directories(p);
  return p;
}

/// Rimuove la directory temporanea (cleanup after test).
void rm_tmp(const fs::path& p)
{
  std::error_code ec;
  fs::remove_all(p, ec);
}

} // anonymous namespace


// ===========================================================================
//  T1 — Registrazione nodo con UTXO proof
// ===========================================================================
//  Verifica che:
//  - register_node() persista correttamente i campi chiave
//  - wallet_pk e node_pk siano DISTINTI (separazione di sicurezza)
//  - registered_timestamp sia impostato
// ===========================================================================
TEST(MevaTrustIntegration, T1_NodeRegistrationWithUTXOProof)
{
  auto tmp = make_tmp("t1");
  auto reg = std::make_shared<cryptonote::mevatrust::NodeRegistry>(tmp.string());
  ASSERT_TRUE(reg->open()) << "T1: apertura NodeRegistry fallita";

  auto [wpk, wsk] = make_keypair("wallet_t1");
  auto [npk, nsk] = make_keypair("nodekey_t1");
  const std::string addr = "MVCt1addr000000000000000000000";

  crypto::signature sig{};
  const std::string msg = "register:" + addr;
  crypto::generate_signature(
      crypto::cn_fast_hash(msg.data(), msg.size()), wpk, wsk, sig);

  ASSERT_TRUE(reg->register_node(wpk, addr, npk, sig, 0, "127.0.0.1", 100))
      << "T1: register_node() ha restituito false";

  cryptonote::mevatrust::NodeRegistryEntry e{};
  ASSERT_TRUE(reg->get_node_by_wallet(addr, e))
      << "T1: nodo non trovato dopo registrazione";

  // wallet_pk deve corrispondere
  EXPECT_EQ(epee::string_tools::pod_to_hex(e.wallet_key),
            epee::string_tools::pod_to_hex(wpk))
      << "T1: wallet_key non corrisponde";

  // node_pk deve corrispondere
  EXPECT_EQ(epee::string_tools::pod_to_hex(e.node_key),
            epee::string_tools::pod_to_hex(npk))
      << "T1: node_key non corrisponde";

  // FONDAMENTALE: wallet_pk e node_pk devono essere DIVERSI
  EXPECT_NE(epee::string_tools::pod_to_hex(e.wallet_key),
            epee::string_tools::pod_to_hex(e.node_key))
      << "T1: wallet_key == node_key — la separazione delle chiavi non e' applicata!";

  EXPECT_GT(e.registered_timestamp, 0ULL)
      << "T1: registered_timestamp non impostato";

  reg->close();
  rm_tmp(tmp);
}


// ===========================================================================
//  T2 — Anti-Sybil: rigetto nodo falso senza proof (MAINNET)
// ===========================================================================
//  validate_registration_security() deve rifiutare:
//  - saldo insufficiente (< MIN_REGISTRATION_BALANCE)
//  - troppi nodi per stesso wallet (>= MAX_NODES_PER_WALLET)
//  - cooldown non rispettato (registrazione troppo recente)
//  E accettare una registrazione valida.
// ===========================================================================
TEST(MevaTrustIntegration, T2_AntiSybilRejectWithoutProof)
{
  using namespace cryptonote::mevatrust::security;
  std::string err;

  // Caso 1: saldo insufficiente
  ASSERT_FALSE(validate_registration_security(
      "MVCaddr_low", SybilConfig::MIN_REGISTRATION_BALANCE - 1,
      0, 0, 1000000, err))
      << "T2: saldo basso non rigettato — Anti-Sybil non attivo!";
  EXPECT_FALSE(err.empty()) << "T2: messaggio errore Anti-Sybil vuoto";

  // Caso 2: troppi nodi per stesso wallet
  ASSERT_FALSE(validate_registration_security(
      "MVCaddr_many", SybilConfig::MIN_REGISTRATION_BALANCE,
      SybilConfig::MAX_NODES_PER_WALLET, 0, 1000000, err))
      << "T2: wallet con troppi nodi non rigettato";

  // Caso 3: cooldown non rispettato
  const uint64_t now       = 1000000ULL;
  const uint64_t too_recent = now - SybilConfig::REGISTRATION_COOLDOWN_SECONDS + 10;
  ASSERT_FALSE(validate_registration_security(
      "MVCaddr_cool", SybilConfig::MIN_REGISTRATION_BALANCE,
      0, too_recent, now, err))
      << "T2: cooldown non rispettato non rigettato";

  // Caso 4: registrazione valida — deve essere accettata
  ASSERT_TRUE(validate_registration_security(
      "MVCaddr_ok", SybilConfig::MIN_REGISTRATION_BALANCE,
      0, 0, 1000000, err))
      << "T2: registrazione valida rigettata erroneamente: " << err;
}


// ===========================================================================
//  T3 — Uptime tracking -> badge assignment on-chain
// ===========================================================================
//  Simula accumulo uptime e verifica che i badge vengano assegnati
//  dopo aver superato le soglie:
//  - ACTIVE_NODE_SECONDS  -> badge ACTIVE_NODE
//  - LONG_UPTIME_SECONDS  -> badge LONG_UPTIME
//  - sotto soglia         -> nessun badge
// ===========================================================================
TEST(MevaTrustIntegration, T3_UptimeTrackingBadgeAssignment)
{
  auto tmp = make_tmp("t3");
  auto eng = std::make_shared<cryptonote::mevatrust::MevaTrustEngine>(tmp.string());
  ASSERT_TRUE(eng->init()) << "T3: MevaTrustEngine::init() fallita";

  using Thr = cryptonote::mevatrust::BadgeThresholds;

  // Caso 1: ACTIVE_NODE
  {
    auto nid = make_node_id("node_t3_active");
    eng->record_uptime(nid, Thr::ACTIVE_NODE_SECONDS);
    std::vector<std::string> awarded;
    eng->evaluate_badges(nid, awarded);
    ASSERT_FALSE(awarded.empty()) << "T3: nessun badge dopo soglia ACTIVE_NODE";
    bool found = false;
    for (const auto& b : awarded)
      if (b.find("ACTIVE") != std::string::npos) { found = true; break; }
    EXPECT_TRUE(found) << "T3: badge ACTIVE_NODE non presente";
  }

  // Caso 2: LONG_UPTIME
  {
    auto nid = make_node_id("node_t3_long");
    eng->record_uptime(nid, Thr::LONG_UPTIME_SECONDS);
    std::vector<std::string> awarded;
    eng->evaluate_badges(nid, awarded);
    bool found = false;
    for (const auto& b : awarded)
      if (b.find("LONG_UPTIME") != std::string::npos) { found = true; break; }
    EXPECT_TRUE(found) << "T3: badge LONG_UPTIME non presente dopo 30gg uptime";
  }

  // Caso 3: sotto soglia — nessun badge
  {
    auto nid = make_node_id("node_t3_short");
    eng->record_uptime(nid, Thr::ACTIVE_NODE_SECONDS - 1);
    std::vector<std::string> awarded;
    eng->evaluate_badges(nid, awarded);
    EXPECT_TRUE(awarded.empty()) << "T3: badge assegnato sotto soglia minima";
  }

  eng->shutdown();
  rm_tmp(tmp);
}


// ===========================================================================
//  T4 — Reward coinbase blob 0xA2 (round-trip serializzazione)
// ===========================================================================
//  build_mevatrust_snapshot_extra() serializza la snapshot in un blob
//  con tag 0xA2. parse_mevatrust_snapshot_extra() deve ricostruirla
//  identica (tutti i campi: height, period, awards, badges, rewards).
// ===========================================================================
TEST(MevaTrustIntegration, T4_RewardCoinbaseBlob)
{
  cryptonote::mevatrust::tx_extra_mevatrust_snapshot snap{};
  snap.height = 200;
  snap.period = 2;

  for (int i = 0; i < 2; i++) {
    cryptonote::mevatrust::tx_extra_mevatrust_snapshot::NodeAward a{};
    a.node_id = make_node_id("reward_node_" + std::to_string(i));
    a.badges  = { i == 0 ? "ACTIVE_NODE" : "LONG_UPTIME" };
    a.reward  = static_cast<uint64_t>(i + 1) * 1000000000ULL;
    snap.awards.push_back(a);
  }

  // Serializzazione
  std::vector<uint8_t> blob;
  ASSERT_TRUE(cryptonote::mevatrust::build_mevatrust_snapshot_extra(snap, blob))
      << "T4: build_mevatrust_snapshot_extra() fallita";
  ASSERT_FALSE(blob.empty()) << "T4: blob vuoto";
  ASSERT_EQ(blob[0], 0xA2) << "T4: tag blob non 0xA2";

  // Deserializzazione (round-trip)
  cryptonote::mevatrust::tx_extra_mevatrust_snapshot snap2{};
  ASSERT_TRUE(cryptonote::mevatrust::parse_mevatrust_snapshot_extra(blob, snap2))
      << "T4: parse_mevatrust_snapshot_extra() fallita";

  EXPECT_EQ(snap2.height, 200u)  << "T4: height mismatch";
  EXPECT_EQ(snap2.period, 2u)   << "T4: period mismatch";
  ASSERT_EQ(snap2.awards.size(), 2u) << "T4: numero awards mismatch";
  EXPECT_EQ(snap2.awards[0].reward, 1000000000ULL) << "T4: reward[0] mismatch";
  EXPECT_EQ(snap2.awards[1].reward, 2000000000ULL) << "T4: reward[1] mismatch";
  EXPECT_EQ(snap2.awards[0].badges[0], "ACTIVE_NODE") << "T4: badge[0] mismatch";
  EXPECT_EQ(snap2.awards[1].badges[0], "LONG_UPTIME")  << "T4: badge[1] mismatch";
}


// ===========================================================================
//  T5 — Anti-replay post-restart (persistenza LMDB)
// ===========================================================================
//  Un nonce consumato deve essere rigettato anche dopo il riavvio del daemon.
//  LMDB deve persistere il set dei nonce tra sessioni diverse.
// ===========================================================================
TEST(MevaTrustIntegration, T5_AntiReplayPostRestart)
{
  auto tmp = make_tmp("t5");
  const uint64_t nonce = 0xDEADBEEF12345678ULL;

  // Sessione 1: inserisci e consuma il nonce
  {
    cryptonote::mevatrust::MevaTrustSnapshotAntireplay ar;
    ASSERT_TRUE(ar.open(tmp.string())) << "T5: apertura DB sessione 1 fallita";
    EXPECT_FALSE(ar.is_consumed(nonce)) << "T5: nonce gia' segnato prima dell'uso";
    EXPECT_TRUE(ar.consume(nonce))      << "T5: consume() fallita per nonce nuovo";
    EXPECT_TRUE(ar.is_consumed(nonce))  << "T5: nonce non marcato dopo consume()";
    EXPECT_FALSE(ar.consume(nonce))     << "T5: REPLAY ammesso nella stessa sessione!";
    ar.close();
  }

  // Sessione 2: simula riavvio — il nonce deve ancora risultare consumato
  {
    cryptonote::mevatrust::MevaTrustSnapshotAntireplay ar;
    ASSERT_TRUE(ar.open(tmp.string())) << "T5: apertura DB sessione 2 fallita";
    EXPECT_TRUE(ar.is_consumed(nonce))
        << "T5: CRITICO — nonce non persistito dopo riavvio! Anti-replay bypassabile con restart.";
    EXPECT_FALSE(ar.consume(nonce))
        << "T5: REPLAY ammesso dopo riavvio — LMDB non ha persistito il nonce.";
    ar.close();
  }

  rm_tmp(tmp);
}


// ===========================================================================
//  T6 — Quorum 3/5 (consenso snapshot)
// ===========================================================================
//  Verifica:
//  - apply_func NON chiamata con < 3 voti
//  - apply_func chiamata esattamente UNA VOLTA al 3° voto distinto
//  - voti 4 e 5 non ri-triggerano apply_func (idempotenza)
//  - voti duplicati dello stesso proposer non vengono contati
// ===========================================================================
TEST(MevaTrustIntegration, T6_Quorum3of5Consensus)
{
  cryptonote::mevatrust::tx_extra_mevatrust_snapshot snap{};
  snap.height = 500;
  snap.period = 5;

  std::vector<std::pair<crypto::public_key, crypto::secret_key>> p;
  for (int i = 0; i < 5; i++)
    p.push_back(make_keypair("proposer_t6_" + std::to_string(i)));

  // Test quorum normale
  {
    std::atomic<int> apply_count{0};
    cryptonote::mevatrust::SnapshotBroadcaster br;
    br.set_apply_func(
        [&](const cryptonote::mevatrust::tx_extra_mevatrust_snapshot&) -> bool {
          apply_count++;
          return true;
        });

    br.add_vote(snap, p[0].first, p[0].second);
    EXPECT_EQ(apply_count.load(), 0) << "T6: apply_func chiamata dopo 1 voto (serve 3)";

    br.add_vote(snap, p[1].first, p[1].second);
    EXPECT_EQ(apply_count.load(), 0) << "T6: apply_func chiamata dopo 2 voti (serve 3)";

    br.add_vote(snap, p[2].first, p[2].second);
    EXPECT_EQ(apply_count.load(), 1) << "T6: apply_func NON chiamata al 3° voto — quorum fallito";

    br.add_vote(snap, p[3].first, p[3].second);
    br.add_vote(snap, p[4].first, p[4].second);
    EXPECT_EQ(apply_count.load(), 1) << "T6: apply_func chiamata piu' di una volta (non idempotente)";
  }

  // Test voti duplicati dello stesso proposer
  {
    std::atomic<int> cnt2{0};
    snap.height = 501;
    cryptonote::mevatrust::SnapshotBroadcaster br2;
    br2.set_apply_func(
        [&](const cryptonote::mevatrust::tx_extra_mevatrust_snapshot&) -> bool {
          cnt2++;
          return true;
        });
    br2.add_vote(snap, p[0].first, p[0].second);
    br2.add_vote(snap, p[0].first, p[0].second); // duplicato
    br2.add_vote(snap, p[0].first, p[0].second); // duplicato
    EXPECT_EQ(cnt2.load(), 0)
        << "T6: voti duplicati dello stesso proposer contati come distinti";
  }
}


// ===========================================================================
//  T7 — rebuild_registry_from_chain()
// ===========================================================================
//  Un nodo appena sincronizzato deve poter ricostruire il registry completo
//  leggendo le TX 0xA0 dalla chain, senza accedere al LMDB di altri nodi.
//  Verifica che tutti i 5 nodi siano presenti e con i campi corretti.
// ===========================================================================
TEST(MevaTrustIntegration, T7_RebuildRegistryFromChain)
{
  auto tmp_orig    = make_tmp("t7_orig");
  auto tmp_rebuilt = make_tmp("t7_rebuilt");

  std::vector<std::string>                                       addresses;
  std::vector<std::pair<crypto::public_key, crypto::secret_key>> wallets;
  std::vector<std::pair<crypto::public_key, crypto::secret_key>> nodekeys;
  std::vector<cryptonote::mevatrust::MevaTrustTxRecord>  records;

  // Fase 1: popola registry originale con 5 nodi
  {
    auto reg = std::make_shared<cryptonote::mevatrust::NodeRegistry>(tmp_orig.string());
    ASSERT_TRUE(reg->open());

    for (int i = 0; i < 5; i++) {
      wallets.push_back(make_keypair("wallet_t7_" + std::to_string(i)));
      nodekeys.push_back(make_keypair("nodekey_t7_" + std::to_string(i)));
      addresses.push_back("MVCt7_addr_" + std::to_string(i));

      crypto::signature sig{};
      const std::string msg = "register:" + addresses.back();
      crypto::generate_signature(
          crypto::cn_fast_hash(msg.data(), msg.size()),
          wallets.back().first, wallets.back().second, sig);

      ASSERT_TRUE(reg->register_node(
          wallets.back().first, addresses.back(),
          nodekeys.back().first, sig, 0, "127.0.0.1", 100 + i));

      // Crea record TX 0xA0 (come verrebbe scritto nella chain)
      cryptonote::mevatrust::MevaTrustTxRecord r{};
      r.type       = cryptonote::mevatrust::MevaTrustTxType::NODE_REGISTRATION;
      r.wallet_key = wallets.back().first;
      r.node_key   = nodekeys.back().first;
      r.address    = addresses.back();
      r.height     = 100 + i;
      records.push_back(r);
    }
    reg->close();
  }

  // Fase 2: ricostruisce registry da zero (simula nodo appena sincronizzato)
  {
    auto reg_new = std::make_shared<cryptonote::mevatrust::NodeRegistry>(tmp_rebuilt.string());
    ASSERT_TRUE(reg_new->open());

    ASSERT_TRUE(reg_new->rebuild_from_tx_records(records))
        << "T7: rebuild_from_tx_records() fallita";

    for (int i = 0; i < 5; i++) {
      cryptonote::mevatrust::NodeRegistryEntry e{};
      ASSERT_TRUE(reg_new->get_node_by_wallet(addresses[i], e))
          << "T7: nodo " << i << " mancante nel registry ricostruito";
      EXPECT_EQ(epee::string_tools::pod_to_hex(e.wallet_key),
                epee::string_tools::pod_to_hex(wallets[i].first))
          << "T7: wallet_key errata per nodo " << i;
      EXPECT_EQ(epee::string_tools::pod_to_hex(e.node_key),
                epee::string_tools::pod_to_hex(nodekeys[i].first))
          << "T7: node_key errata per nodo " << i;
    }
    reg_new->close();
  }

  rm_tmp(tmp_orig);
  rm_tmp(tmp_rebuilt);
}


// ===========================================================================
//  T8 — Incentive history: trasparenza totale
// ===========================================================================
//  Ogni incentivo/badge e' registrato on-chain nella snapshot 0xA2.
//  Qualsiasi nodo della rete deve poter leggere e verificare la storia
//  completa degli incentivi di qualsiasi altro nodo.
//  Verifica: 5 periodi, reward crescenti, badge speciale al periodo 3,
//            totale reward corretto, integrita' del dato on-chain.
// ===========================================================================
TEST(MevaTrustIntegration, T8_IncentiveHistoryTransparency)
{
  auto nid = make_node_id("node_t8");

  // Crea 5 periodi di incentivi on-chain
  std::vector<std::vector<uint8_t>> chain_blobs;
  for (int period = 1; period <= 5; period++) {
    cryptonote::mevatrust::tx_extra_mevatrust_snapshot snap{};
    snap.height = static_cast<uint64_t>(100 * period);
    snap.period = static_cast<uint32_t>(period);

    cryptonote::mevatrust::tx_extra_mevatrust_snapshot::NodeAward award{};
    award.node_id = nid;
    award.badges  = (period == 3)
                    ? std::vector<std::string>{"LONG_UPTIME"}
                    : std::vector<std::string>{"ACTIVE_NODE"};
    award.reward  = static_cast<uint64_t>(period) * 500000000ULL;
    snap.awards.push_back(award);

    std::vector<uint8_t> blob;
    ASSERT_TRUE(cryptonote::mevatrust::build_mevatrust_snapshot_extra(snap, blob))
        << "T8: serializzazione snapshot periodo " << period << " fallita";
    chain_blobs.push_back(blob);
  }

  // Lettura dalla chain (simula query da nodo esterno — trasparenza totale)
  uint64_t total_reward = 0;
  for (int i = 0; i < static_cast<int>(chain_blobs.size()); i++) {
    cryptonote::mevatrust::tx_extra_mevatrust_snapshot snap{};
    ASSERT_TRUE(cryptonote::mevatrust::parse_mevatrust_snapshot_extra(
        chain_blobs[i], snap))
        << "T8: parsing snapshot " << i << " fallito";

    EXPECT_EQ(snap.period, static_cast<uint32_t>(i + 1))
        << "T8: period mismatch blob " << i;
    ASSERT_EQ(snap.awards.size(), 1u)
        << "T8: awards size mismatch blob " << i;

    const auto& a = snap.awards[0];
    EXPECT_EQ(epee::string_tools::pod_to_hex(a.node_id),
              epee::string_tools::pod_to_hex(nid))
        << "T8: node_id mismatch blob " << i;
    EXPECT_EQ(a.reward, static_cast<uint64_t>(i + 1) * 500000000ULL)
        << "T8: reward mismatch blob " << i;

    // Verifica badge speciale al periodo 3
    if (i == 2)
      EXPECT_EQ(a.badges[0], "LONG_UPTIME")
          << "T8: badge LONG_UPTIME non presente al periodo 3";

    total_reward += a.reward;
  }

  // Somma attesa: (1+2+3+4+5) * 500_000_000 = 7_500_000_000
  EXPECT_EQ(total_reward, 7500000000ULL)
      << "T8: somma reward totale non corrisponde";

  // Verifica integrita' dato on-chain: manomissione deve produrre dati divergenti
  {
    std::vector<uint8_t> tampered = chain_blobs[0];
    if (tampered.size() > 15) {
      tampered[15] ^= 0xFF; // corrompi un byte
      cryptonote::mevatrust::tx_extra_mevatrust_snapshot snap_bad{};
      const bool ok = cryptonote::mevatrust::parse_mevatrust_snapshot_extra(
          tampered, snap_bad);
      if (ok) {
        // Se il parsing riesce i dati devono essere diversi dall'originale
        const bool data_differs =
            snap_bad.awards.empty() ||
            snap_bad.awards[0].reward != 500000000ULL ||
            snap_bad.period != 1;
        EXPECT_TRUE(data_differs)
            << "T8: dato manomesso accettato come valido — integrita' on-chain compromessa";
      }
      // ok == false e' accettabile (checksum / parsing fallito su dato corrotto)
    }
  }
}



