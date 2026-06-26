// Copyright (c) 2024, The Mevacoin Project
// mevatrust_manager.cpp

#include "mevatrust_manager.h"
#include "cryptonote_basic/cryptonote_basic_impl.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "misc_log_ex.h"
#include "string_tools.h"
#include <sys/stat.h>
#include <stdexcept>
#include <algorithm>
#include <ctime>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.manager"

#include "mevatrust_tx_parser.h"
#include "penalty.h"
#include "pool_distribution.h"
#include "pool_address.h"
#include "frost_threshold.h"

extern "C" {
#include "crypto/crypto-ops.h"
}

namespace cryptonote {

namespace mevatrust {
  static std::shared_ptr<MevaTrustManager> g_manager;
  static std::mutex g_mgr_lock;
  void set_manager(std::shared_ptr<MevaTrustManager> m) {
    std::lock_guard<std::mutex> lk(g_mgr_lock); g_manager=std::move(m);
  }
  MevaTrustManager* get_manager() {
    std::lock_guard<std::mutex> lk(g_mgr_lock); return g_manager.get();
  }
  static crypto::hash g_my_node_id{};
  static bool g_my_node_id_set=false;
  static std::mutex g_my_node_id_lock;
  void set_my_node_id(const crypto::hash& id) {
    std::lock_guard<std::mutex> lk(g_my_node_id_lock);
    g_my_node_id=id; g_my_node_id_set=true;
    MINFO("PoA: my_node_id=" << epee::string_tools::pod_to_hex(id));
  }
  bool get_my_node_id(crypto::hash& out) {
    std::lock_guard<std::mutex> lk(g_my_node_id_lock);
    if (!g_my_node_id_set) return false;
    out=g_my_node_id; return true;
  }
}

MevaTrustManager::~MevaTrustManager() { shutdown(); }

bool MevaTrustManager::init(const std::string& data_dir, network_type nettype, uint8_t hf)
{
  if (m_initialized.load()) return true;
  m_data_dir=data_dir; m_nettype=nettype; m_hf_version=hf;
  const std::string p = data_dir+"/mevatrust";
#ifdef _WIN32
  ::CreateDirectoryA(p.c_str(),nullptr); ::CreateDirectoryA((p+"/db").c_str(),nullptr);
#else
  ::mkdir(p.c_str(),0755); ::mkdir((p+"/db").c_str(),0755);
#endif
  MINFO("Initializing MevaTrust in "<<p);
  try {
    m_node_registry = std::make_shared<NodeRegistry>(p+"/db");
    m_node_registry->load_from_disk();
    m_circle_registry = std::make_shared<CircleRegistry>(p+"/db");
    m_circle_registry->load_from_disk();
    MINFO("[MevaTrustManager] CircleRegistry inizializzato");
    m_mevatrust_engine = std::make_shared<MevaTrustEngine>(p+"/db",m_node_registry);
    m_mevatrust_engine->load_from_disk();
    m_availability_proof = std::make_shared<AvailabilityProofEngine>(p+"/db",m_node_registry);
    m_badge_system = std::make_shared<BadgeSystem>(p+"/db",m_node_registry,m_mevatrust_engine);
    m_badge_system->load_from_disk();
    m_reward_distributor = std::make_shared<RewardDistributor>(p);
    m_store_registry = std::make_shared<StoreRegistry>(p+"/db");
    m_store_registry->load_from_disk();
    m_snapshot_broadcaster = std::make_unique<SnapshotBroadcaster>();
    MINFO("SnapshotBroadcaster creato [Fase 3]");
    m_frost_broadcaster = std::make_unique<FrostBroadcaster>();
    MINFO("FrostBroadcaster creato [Fase 7]");

    // ── C2-WIRING: badge_system -> on_chain_cb_ ──────────────────────────────
    // Quando award_badge() assegna un badge, questo callback viene chiamato
    // immediatamente. Qui registriamo il fatto (log) — il blob 0xA2 vero e'
    // costruito da build_pending_snapshot() al prossimo period boundary, che
    // legge tutti i badge tramite get_recently_awarded() da LMDB.
    // Se vuoi publishing immediato out-of-period, costruisci il blob qui.
    if (m_badge_system) {
      m_badge_system->set_on_chain_callback(
        [this](const crypto::hash& node_id,
               uint8_t             badge_type,
               uint64_t            height,
               const std::string&  reason) -> bool {
          MINFO("[MevaTrustManager] C2: badge notificato on-chain"
                " nid=" << epee::string_tools::pod_to_hex(node_id)
                << " badge_type=" << (int)badge_type
                << " h=" << height
                << " reason=" << (reason.empty() ? "(auto)" : reason)
                << " -- sara' incluso nello snapshot 0xA2 al prossimo periodo.");
          // Il blob vero e' costruito da build_pending_snapshot(height) che viene
          // chiamata da on_new_block() al boundary del periodo.
          // L'award e' gia' persistito in LMDB da badge_system.cpp -- sara'
          // letto da get_recently_awarded() e incluso nel prossimo miner_tx.extra.
          return true;
        }
      );
      MINFO("[MevaTrustManager] C2: on_chain_callback configurato per BadgeSystem");
    }

    // ── C4-WIRING: snapshot_broadcaster -> apply_fn ──────────────────────────
    // Quando il quorum di proposer (>= QUORUM_THRESHOLD = 3) e' raggiunto,
    // SnapshotBroadcaster::try_finalize() chiama questa funzione con la
    // snapshot aggregata. Qui la serializziamo in m_pending_snapshot_extra
    // cosi' il prossimo miner_tx la inclu dera' nel proprio extra (tag 0xA2).
    if (m_snapshot_broadcaster) {
      m_snapshot_broadcaster->set_apply_func(
        [this](const tx_extra_mevatrust_snapshot& snap) -> bool {
          std::lock_guard<std::mutex> lk(m_lock);
          // Serializza il blob 0xA2 della snapshot quorum
          m_pending_snapshot_extra.clear();
          if (!mevatrust::build_mevatrust_snapshot_extra(
                  snap, m_pending_snapshot_extra)) {
            MERROR("[MevaTrustManager] C4: build_mevatrust_snapshot_extra"
                   " fallita per snapshot quorum h=" << snap.height);
            return false;
          }
          m_has_pending_snapshot = true;
          MINFO("[MevaTrustManager] C4: snapshot quorum accettata"
                " h=" << snap.height
                << " badges=" << snap.badge_awards.size()
                << " -- " << m_pending_snapshot_extra.size()
                << " bytes accodati per miner_tx");
          return true;
        }
      );
      MINFO("[MevaTrustManager] C4: apply_func configurato per SnapshotBroadcaster (quorum 3/5)");
    }

    // ── Fase 7: FrostBroadcaster apply callback ─────────────────────────────
    // When P2P FROST round completes, store the aggregate sig for miner_tx.
    if (m_frost_broadcaster) {
      m_frost_broadcaster->set_apply_func(
        [this](const mevatrust::frost::FrostSignature& sig) -> bool {
          std::lock_guard<std::mutex> lk(m_lock);
          std::vector<uint8_t> dist_extra;
          // Reuse cached resolved rewards
          if (!mevatrust::construct_pool_distribution_extra(
              m_resolved_at_height,
              static_cast<uint32_t>(m_resolved_at_height / m_period_length),
              m_resolved_pool_balance, m_resolved_rewards, sig, dist_extra)) {
            MERROR("[FROST:P2P] construct_pool_distribution_extra failed");
            return false;
          }
          m_pending_pool_distribution_extra = std::move(dist_extra);
          m_has_pending_pool_distribution = true;
          MINFO("[FROST:P2P] apply_func: distribution stored h="
                << m_resolved_at_height);
          return true;
        }
      );
      MINFO("[FROST:P2P] apply_func configurato per FrostBroadcaster");
    }
    // ────────────────────────────────────────────────────────────────────────

  } catch (const std::exception& e) {
    MERROR("MevaTrustManager::init failed: "<<e.what()); return false;
  }
  m_initialized.store(true);
  MINFO("MevaTrustManager initialized");
  return true;
}

void MevaTrustManager::shutdown() {
  if (!m_initialized.exchange(false)) return;
  MINFO("MevaTrustManager shutdown");
  if (m_node_registry)        m_node_registry->save_to_disk();
  if (m_circle_registry)      m_circle_registry->save_to_disk();
  if (m_mevatrust_engine) m_mevatrust_engine->save_to_disk();
  if (m_badge_system)         m_badge_system->save_to_disk();
}

void MevaTrustManager::set_proposer_keypairs(
    const std::array<frost::SignerKeypair, frost::FROST_N>& kp)
{
    std::lock_guard<std::mutex> lk(m_lock);
    m_proposer_keypairs = kp;
    m_has_proposer_keys = true;
    // Wire FrostBroadcaster with the first proposer key (index 0 as default coordinator)
    if (m_frost_broadcaster) {
      m_frost_broadcaster->set_node_key(kp[0].sec, kp[0].pub, 0);
      MINFO("[FROST:P2P] FrostBroadcaster key set: index=0 pub="
            << epee::string_tools::pod_to_hex(kp[0].pub));
    }
    MINFO("[FROST] Proposer keypairs set (" << frost::FROST_N << " keys)");
}

  // ── Validator system ──────────────────────────────────────────────────────
static constexpr uint64_t VALIDATOR_MIN_STAKE = 1'000'000'000'000'000ULL; // 1000 MVC
static constexpr uint64_t VALIDATOR_AUTO_UPTIME_SECS = 30 * 86400ULL;     // 30 giorni
static constexpr float    VALIDATOR_AUTO_UPTIME_PCT = 0.95f;              // 95%

void MevaTrustManager::promote_to_validator(const crypto::hash& node_id, uint64_t height, uint64_t stake) {
    if (!m_node_registry) return;
    NodeRegistryEntry entry;
    if (!m_node_registry->get_node_by_id(node_id, entry)) return;
    if (entry.is_validator) return;
    // Persiste in NodeRegistry + LMDB
    if (!m_node_registry->update_node_validator(node_id, true, height, stake)) {
        MWARNING("[Validator] Fallito aggiornamento LMDB per nodo "
                 << epee::string_tools::pod_to_hex(node_id).substr(0,16));
        return;
    }
    MINFO("[Validator] Promosso nodo " << epee::string_tools::pod_to_hex(node_id).substr(0,16)
          << " a h=" << height << " stake=" << stake);
    if (m_badge_system)
        m_badge_system->award_badge(node_id, BadgeType::NETWORK_VALIDATOR, height, "validator-promotion");
    PoppedOp po; po.op = PoppedOp::VALIDATOR_PROMOTION; po.node_id = node_id;
    po.badge_type = static_cast<uint8_t>(BadgeType::NETWORK_VALIDATOR);
    m_reorg_log[height].push_back(po);
}

bool MevaTrustManager::check_auto_validator_promotion(uint64_t height) {
    if (!m_node_registry || !m_mevatrust_engine) return false;
    uint32_t promoted = 0;
    auto nodes = m_node_registry->get_active_nodes();
    for (const auto& n : nodes) {
        if (n.is_validator) continue;
        if (n.status != NodeStatus::ACTIVE) continue;
        uint64_t uptime_secs = m_mevatrust_engine->get_total_uptime_seconds(n.node_id);
        float uptime_pct = m_mevatrust_engine->get_uptime_percentage(n.node_id);
        if (uptime_secs >= VALIDATOR_AUTO_UPTIME_SECS && uptime_pct >= VALIDATOR_AUTO_UPTIME_PCT) {
            promote_to_validator(n.node_id, height, 0);
            ++promoted;
        }
    }
    if (promoted) MINFO("[Validator] Auto-promossi " << promoted << " nodi a h=" << height);
    return promoted > 0;
}

bool MevaTrustManager::is_validator(const crypto::hash& node_id) const {
    if (!m_node_registry) return false;
    NodeRegistryEntry entry;
    return m_node_registry->get_node_by_id(node_id, entry) && entry.is_validator;
}

uint32_t MevaTrustManager::get_validator_count() const {
    if (!m_node_registry) return 0;
    uint32_t count = 0;
    auto nodes = m_node_registry->get_active_nodes();
    for (const auto& n : nodes)
        if (n.is_validator) ++count;
    return count;
}

void MevaTrustManager::on_new_block(uint64_t height, uint64_t block_reward, uint8_t hf) {
  if (!m_initialized.load()) return;
  std::lock_guard<std::mutex> lk(m_lock);
  m_hf_version = hf;

  // Pool è on-chain: 3% nel coinbase -> pool_address deterministica.
  // Non accumuliamo più in LMDB. pool_balance = sum(3% coinbase) - distribuzioni.
    if (height>0 && height%m_period_length==0) {
    MINFO("Period boundary h="<<height);
    if (m_mevatrust_engine) m_mevatrust_engine->process_reward_period(height);
    if (m_badge_system) {
      uint32_t ch = m_badge_system->evaluate_all_badges(height);
      if (ch) MINFO("Badge changes: "<<ch<<" at h="<<height);
    }
    check_auto_validator_promotion(height);
    trigger_distribution(height);

    // Fase 3: broadcast snapshot 0xA2 P2P con badge assegnati nel periodo
    if (m_snapshot_broadcaster) {
      const uint32_t active = m_node_registry
          ? m_node_registry->get_active_node_count() : 0u;
      std::vector<std::pair<crypto::hash, uint8_t>> awards;
      if (m_badge_system) {
        auto raw = m_badge_system->get_recently_awarded(m_last_period_height, height);
        awards.reserve(raw.size());
        for (auto& [nid, bt] : raw)
          awards.emplace_back(nid, static_cast<uint8_t>(bt));
      }
      const uint32_t period = static_cast<uint32_t>(height / m_period_length);
      m_snapshot_broadcaster->broadcast_snapshot(height, period, active, awards);
    }


        // [C2] Badge On-Chain: prepara snapshot 0xA2 da embeddare nel prossimo miner_tx
        build_pending_snapshot(height);

        // Escalate SUSPENDED da >2 periodi a BANNED
        if (m_node_registry) {
            auto suspended = m_node_registry->get_nodes_by_status(NodeStatus::SUSPENDED);
            const uint64_t suspend_window = m_period_length * 2u;
            for (const auto& entry : suspended) {
                uint64_t blocks_since_update = height - entry.updated_at;
                if (blocks_since_update >= suspend_window) {
                    mevatrust::apply_penalty(entry.node_id,
                        mevatrust::OffenseType::MALICIOUS_ACTIVITY, height);
                }
            }
        }

        m_last_period_height = height;
    }

    // Scadenza acquisti pending: esegue auto-refund se expiry superato
    if (m_store_registry && height > 0 && (height % 240) == 0) {
        // Nota: in produzione serve una scansione efficiente con indice.
        // Per ora la scansione integrale avviene ogni 240 blocchi.
        MINFO("[MevaTrustManager] Scansione acquisti scaduti h=" << height);
    }
}

void MevaTrustManager::trigger_distribution(uint64_t height) {
  if (!m_reward_distributor||!m_mevatrust_engine) return;

  // Pool balance from on-chain state root (not LMDB)
  uint64_t pool_balance = compute_pool_balance_from_chain();
  m_resolved_pool_balance = pool_balance;
  if (pool_balance == 0) return;

  // Process welcome bonuses  
  auto welcome_outputs = m_reward_distributor->process_welcome_bonuses(height);
  if (!m_reward_distributor->is_distribution_due(height) && welcome_outputs.empty()) return;
  MINFO("Triggering distribution h=" << height << " pool_balance=" << pool_balance);

  // Regular distribution (uses MevaTrustEngine scores)
  if (!m_reward_distributor->distribute_rewards(m_mevatrust_engine, height)) {
    MERROR("distribute_rewards failed h="<<height); return;
  }
  auto pending = m_reward_distributor->get_pending_coinbase_outputs();

  // Merge welcome + regular outputs
  for (auto& pco : welcome_outputs) pending.push_back(std::move(pco));
  if (pending.empty()) return;

  std::vector<NodeCoinbaseReward> resolved; resolved.reserve(pending.size());
  for (auto& pco : pending) {
    if (pco.wallet_address.empty() && m_node_registry) {
      crypto::hash nid{};
      if (epee::string_tools::hex_to_pod(pco.node_id_str,nid)) {
        NodeRegistryEntry e{};
        if (m_node_registry->get_node_by_id(nid,e)) pco.wallet_address=e.wallet_address;
      }
    }
    if (pco.wallet_address.empty()) continue;
    NodeCoinbaseReward ncr{};
    if (!parse_wallet_address(pco.wallet_address,ncr.address)) continue;
    ncr.amount=pco.amount; ncr.node_id=pco.node_id_str;
    resolved.push_back(std::move(ncr));
  }
  if (resolved.empty()) return;
  
  // Node rewards go into MINER_COINBASE as vouts (spendable by nodes)
  // The 0xAA FROST blob proves authorization
  m_resolved_rewards=std::move(resolved); m_resolved_at_height=height;
  
  // Build FROST-authorized pool distribution blob (0xAA) for consensus validation
  cryptonote::mevatrust::frost::FrostSignature frost_sig;
  if (m_has_proposer_keys) {
    // Real FROST signing using proposer keypairs
    // We need at least FROST_T (3) signers. Use all available proposers.
    std::vector<uint8_t> signer_indices;
    for (uint8_t i = 0; i < frost::FROST_N; ++i) signer_indices.push_back(i);

    // Compute Lagrange coefficients for the signer set
    cryptonote::mevatrust::frost::PublicKeyPackage pkg;
    {
        crypto::public_key pk_array[frost::FROST_N];
        for (size_t i = 0; i < frost::FROST_N; ++i) {
            pk_array[i] = m_proposer_keypairs[i].pub;
            pkg.signer_pubkeys[i] = m_proposer_keypairs[i].pub;
        }
        pkg.agg_pubkey = cryptonote::mevatrust::frost::sum_public_keys(
            pk_array, frost::FROST_N);
    }
    cryptonote::mevatrust::frost::compute_lagrange_coeffs(signer_indices, pkg.lagrange_coeffs);

    // Build distribution outputs to get the message hash
    auto outputs = cryptonote::mevatrust::build_distribution_outputs(
        m_resolved_rewards, pool_balance);
    crypto::hash msg_hash = cryptonote::mevatrust::frost::create_distribution_message_hash(
        height, static_cast<uint32_t>(height / m_period_length), outputs);

    // Round 1: each signer generates nonces
    std::array<cryptonote::mevatrust::frost::NoncePair, frost::FROST_N> nonces;
    for (size_t i = 0; i < frost::FROST_N; ++i)
        cryptonote::mevatrust::frost::generate_nonces(nonces[i]);

    // Compute aggregate R = sum(R_i)
    crypto::ec_scalar R;
    {
        bool first = true;
        ge_p3 R_sum;
        for (size_t i = 0; i < frost::FROST_N; ++i) {
            ge_p3 R_i;
            unsigned char r_bytes[32];
            memcpy(r_bytes, &nonces[i].hiding, 32);
            ge_scalarmult_base(&R_i, r_bytes);
            if (first) {
                R_sum = R_i;
                first = false;
            } else {
                ge_cached cached;
                ge_p3_to_cached(&cached, &R_i);
                ge_p1p1 p1;
                ge_add(&p1, &R_sum, &cached);
                ge_p1p1_to_p3(&R_sum, &p1);
            }
        }
        crypto::public_key R_pk;
        ge_p3_tobytes(reinterpret_cast<unsigned char*>(&R_pk), &R_sum);
        memcpy(&R, &R_pk, 32);
    }

    // Round 2: each signer creates partial signature
    std::vector<cryptonote::mevatrust::frost::PartialSignature> partials;
    for (size_t i = 0; i < frost::FROST_N; ++i) {
        cryptonote::mevatrust::frost::PartialSignature ps;
        ps.signer_index = i;
        ps.hiding_nonce = nonces[i].hiding;
        ps.binding_nonce = nonces[i].binding;
        cryptonote::mevatrust::frost::sign_partial(
            msg_hash, nonces[i], m_proposer_keypairs[i].sec,
            pkg.lagrange_coeffs[i], R, pkg.agg_pubkey, ps);
        partials.push_back(std::move(ps));
    }

    // Aggregate into final FROST signature
    cryptonote::mevatrust::frost::aggregate_signatures(
        msg_hash, partials, pkg, frost_sig);
    MINFO("[FROST] Real FROST signature produced for h=" << height
          << " signers=" << partials.size());
  } else {
    // No proposer keys available — produce a placeholder (will fail validation)
    memset(&frost_sig, 0, sizeof(frost_sig));
    MWARNING("[FROST] No proposer keys set — distribution will NOT validate");
  }
  
  std::vector<uint8_t> dist_extra;
  if (mevatrust::construct_pool_distribution_extra(
      height,
      static_cast<uint32_t>(height / m_period_length),
      pool_balance,
      m_resolved_rewards,
      frost_sig,
      dist_extra)) {
    m_pending_pool_distribution_extra = std::move(dist_extra);
    m_has_pending_pool_distribution = true;
    MINFO("Pool distribution 0xAA ready: " << m_resolved_rewards.size()
          << " outputs, period=" << (height / m_period_length));
  }

  // Start P2P FROST round in parallel (if broadcaster is wired).
  // This allows a real multi-proposer signature to replace the local one
  // for subsequent blocks in this period.
  if (m_frost_broadcaster && m_has_proposer_keys) {
    auto outputs = cryptonote::mevatrust::build_distribution_outputs(
        m_resolved_rewards, pool_balance);
    (void)m_frost_broadcaster->propose_distribution(
        height,
        static_cast<uint32_t>(height / m_period_length),
        outputs);
    MINFO("[FROST:P2P] Asynchronous round started h=" << height);
  }

  MINFO("Distribution ready: "<<m_resolved_rewards.size()<<" outputs h="<<height);
}

std::vector<NodeCoinbaseReward> MevaTrustManager::get_coinbase_rewards(
  uint64_t height, uint64_t total_reward, uint64_t& miner_out)
{
  miner_out=total_reward;
  if (!m_initialized.load()) return {};
  std::lock_guard<std::mutex> lk(m_lock);
  if (m_resolved_rewards.empty()) return {};
  if (height<m_resolved_at_height||height>=m_resolved_at_height+m_period_length) return {};
  uint64_t node_total=0; for (const auto& r:m_resolved_rewards) node_total+=r.amount;
  if (node_total>=total_reward) { MERROR("BUG: node_total>=block_reward"); return {}; }
  miner_out=total_reward-node_total;
  MINFO("Injecting "<<m_resolved_rewards.size()<<" outputs node_total="<<node_total<<" miner="<<miner_out);
  return m_resolved_rewards;
}

bool MevaTrustManager::parse_wallet_address(
  const std::string& addr, account_public_address& out) const
{
  // get_account_address_from_str* takes address_parse_info&, not account_public_address&
  cryptonote::address_parse_info info{};
  bool ok = cryptonote::get_account_address_from_str_or_url(info, m_nettype, addr,
    [](const std::string&, const std::vector<std::string>&, bool) -> std::string { return {}; });
  if (!ok) ok = cryptonote::get_account_address_from_str(info, m_nettype, addr);
  if (ok) out = info.address;
  return ok;
}

void MevaTrustManager::set_pool_fraction_percent(uint32_t p) {
  std::lock_guard<std::mutex> lk(m_lock); if (m_reward_distributor) m_reward_distributor->set_pool_fraction(p);
}
void MevaTrustManager::set_min_score_threshold(float s) {
  std::lock_guard<std::mutex> lk(m_lock); if (m_reward_distributor) m_reward_distributor->set_min_score(s);
}
void MevaTrustManager::set_period_length(uint32_t b) {
  std::lock_guard<std::mutex> lk(m_lock); m_period_length=b;
  if (m_reward_distributor) m_reward_distributor->set_distribution_period(b);
}


void MevaTrustManager::set_broadcast_tx_func(BroadcastTxFunc fn) {
  std::lock_guard<std::mutex> lk(m_lock);
  if (m_snapshot_broadcaster)
    m_snapshot_broadcaster->set_broadcast_func(std::move(fn));
}

void MevaTrustManager::set_frost_broadcast_func(FrostBroadcaster::BroadcastFunc fn) {
  std::lock_guard<std::mutex> lk(m_lock);
  if (m_frost_broadcaster)
    m_frost_broadcaster->set_broadcast_func(std::move(fn));
}

  void MevaTrustManager::set_node_key(const crypto::secret_key& sk,
                                           const crypto::public_key& pk) {
    std::lock_guard<std::mutex> lk(m_lock);
    // [C2] Store node key for signing badge awards embedded in miner_tx
    m_node_sk = sk; m_node_pk = pk; m_node_key_set = true;
    if (m_snapshot_broadcaster)
      m_snapshot_broadcaster->set_node_key(sk, pk);
  }



// ????????????????????????????????????????????????????????????????????????????
// Fase 2: On-Chain TX processing + registry rebuild
// ????????????????????????????????????????????????????????????????????????????


void MevaTrustManager::set_get_block_func(GetBlockFunc fn) {
    std::lock_guard<std::mutex> lk(m_lock);
    m_get_block_func = std::move(fn);
}

void MevaTrustManager::set_get_tx_func(GetTxFunc fn) {
    std::lock_guard<std::mutex> lk(m_lock);
    m_get_tx_func = std::move(fn);
}

void MevaTrustManager::process_mevatrust_txs(
    const block& bl, uint64_t height)
{
    if (!m_initialized.load()) return;

    // Scansiona ogni TX nel blocco (inclusa coinbase)
    std::vector<transaction> txs;
    txs.push_back(bl.miner_tx);
    for (const auto& tx : bl.tx_hashes) {
        if (m_get_tx_func) {
            transaction t;
            if (m_get_tx_func(tx, t)) txs.push_back(t);
        }
    }

    for (const auto& tx : txs) {
        // Tag 0xA0 ? Registration
        {
            tx_extra_mevatrust_registration reg;
            if (mevatrust::parse_mevatrust_registration_from_tx(tx, reg)) {
                if (!mevatrust::verify_registration_signature(reg)) {
                    MWARNING("[MevaTrustManager] Registration sig INVALIDA, skip");
                    continue;
                }
                if (m_node_registry->register_node_onchain(reg, height)) {
                    MINFO("[MevaTrustManager] Nodo registrato on-chain h="
                          << height << " node_id="
                          << epee::string_tools::pod_to_hex(reg.node_id));
                    // Welcome badge immediato
                    if (m_badge_system) {
                        m_badge_system->award_badge(reg.node_id, BadgeType::WELCOME,
                            height, "new-registration");
                    }
                    PoppedOp po; po.op = PoppedOp::REGISTER; po.node_id = reg.node_id;
                    m_reorg_log[height].push_back(po);
                } else if (m_node_registry->is_wallet_pubkey_registered(reg.wallet_pubkey)) {
                    MWARNING("[MevaTrustManager] Doppia registrazione rilevata h="
                             << height << " wallet="
                             << epee::string_tools::pod_to_hex(reg.wallet_pubkey));
                    NodeRegistryEntry existing;
                    if (m_node_registry->get_node_by_pubkey(reg.wallet_pubkey, existing)) {
                        mevatrust::apply_penalty(existing.node_id,
                            mevatrust::OffenseType::DOUBLE_REGISTRATION, height);
                        PoppedOp po; po.op = PoppedOp::PENALTY; po.node_id = existing.node_id;
                        m_reorg_log[height].push_back(po);
                    }
                }
            }
        }
        // Tag 0xA1 ? Deregistration
        {
            tx_extra_mevatrust_deregister dereg;
            if (mevatrust::parse_mevatrust_deregister_from_tx(tx, dereg)) {
                if (!mevatrust::verify_deregister_signature(dereg)) {
                    MWARNING("[MevaTrustManager] Deregister sig INVALIDA, skip");
                    continue;
                }
                if (m_node_registry->deregister_node_onchain(dereg)) {
                    MINFO("[MevaTrustManager] Nodo deregistrato on-chain h="
                          << height << " node_id="
                          << epee::string_tools::pod_to_hex(dereg.node_id));
                    PoppedOp po; po.op = PoppedOp::DEREGISTER; po.node_id = dereg.node_id;
                    m_reorg_log[height].push_back(po);
                }
            }
        }
        // Tag 0xAB ? Validator promotion
        {
            tx_extra_mevatrust_validator val;
            if (mevatrust::parse_mevatrust_validator_from_tx(tx, val)) {
                if (!mevatrust::verify_validator_signature(val)) {
                    MWARNING("[MevaTrustManager] Validator sig INVALIDA, skip");
                    continue;
                }
                // Verifica che il tx contenga stake sufficiente verso pool address
                uint64_t stake = 0;
                account_public_address pool_addr = mevatrust::get_pool_address(m_nettype);
                for (const auto& vout : tx.vout) {
                    if (vout.amount < stake) continue; // cerca l'output piu' grande verso pool
                    txout_to_key tk;
                    if (vout.target.type() == typeid(txout_to_key)) {
                        tk = boost::get<txout_to_key>(vout.target);
                        if (tk.key == pool_addr.m_spend_public_key)
                            stake = vout.amount;
                    }
                }
                if (stake < VALIDATOR_MIN_STAKE) {
                    MWARNING("[MevaTrustManager] Validator stake insufficiente: " << stake);
                    continue;
                }
                promote_to_validator(val.node_id, height, stake);
                PoppedOp po; po.op = PoppedOp::BADGE_AWARD; po.node_id = val.node_id;
                po.badge_type = static_cast<uint8_t>(BadgeType::NETWORK_VALIDATOR);
                m_reorg_log[height].push_back(po);
            }
        }

        // Tag 0xA2 ? Snapshot on-chain: applica badge awards (Fase 3)
        {
            tx_extra_mevatrust_snapshot snap;
            if (mevatrust::parse_mevatrust_snapshot_from_tx(tx, snap)) {
                MINFO("[MevaTrustManager] Snapshot on-chain h=" << height
                      << " period=" << snap.period
                      << " nodes=" << snap.node_count
                      << " badge_awards=" << snap.badge_awards.size());
                if (m_badge_system) {
                    for (const auto& ba : snap.badge_awards) {
                        std::string msg;
                        msg.append(reinterpret_cast<const char*>(ba.node_id.data), 32);
                        msg.push_back(static_cast<char>(ba.badge_type));
                        const uint64_t h_le = ba.awarded_height;
                        msg.append(reinterpret_cast<const char*>(&h_le), 8);
                        const crypto::hash mh = crypto::cn_fast_hash(msg.data(), msg.size());
                        if (!crypto::check_signature(mh, ba.proposer_pubkey, ba.proposer_sig)) {
                            MWARNING("[MevaTrustManager] Badge sig INVALIDA skip node="
                                     << epee::string_tools::pod_to_hex(ba.node_id));
                            continue;
                        }
                        const BadgeType bt = static_cast<BadgeType>(ba.badge_type);
                        if (!m_badge_system->has_badge(ba.node_id, bt)) {
                            m_badge_system->award_badge(ba.node_id, bt,
                                ba.awarded_height, "on-chain-replay");
                            PoppedOp po; po.op = PoppedOp::BADGE_AWARD;
                            po.node_id = ba.node_id; po.badge_type = static_cast<uint8_t>(bt);
                            m_reorg_log[height].push_back(po);
                        }
                    }
                }
            }
        }
        // Tag 0xA3 ? Circle operations (Fase 2)
        {
            tx_extra_mevatrust_circle op;
            if (mevatrust::parse_mevatrust_circle_from_tx(tx, op)) {
                if (!mevatrust::verify_circle_signature(op)) {
                    MWARNING("[MevaTrustManager] Circle op signature INVALIDA");
                    continue;
                }
                if (!m_circle_registry) {
                    MWARNING("[MevaTrustManager] CircleRegistry non inizializzato");
                    continue;
                }
                const crypto::hash tx_hash = cryptonote::get_transaction_hash(tx);
                // Anti-replay: usa tx_hash come nonce implicito (globalmente unico)
                // CREATE e' protetto da name uniqueness
                if (op.op_type != tx_extra_mevatrust_circle::CREATE &&
                    m_circle_registry->is_tx_processed(op.circle_id, tx_hash)) {
                    MWARNING("[MevaTrustManager] Circle op replay rilevato h=" << height
                             << " cid=" << epee::string_tools::pod_to_hex(op.circle_id)
                             << " tx=" << epee::string_tools::pod_to_hex(tx_hash));
                    continue;
                }
                PoppedOp po; po.circle_id = op.circle_id; po.pubkey = op.target_pubkey;
                switch (op.op_type) {
                    case tx_extra_mevatrust_circle::CREATE: {
                        crypto::hash new_id = m_circle_registry->create_circle(op.circle_name, op.signer_pubkey, height);
                        po.op = PoppedOp::CIRCLE_CREATE;
                        if (new_id != crypto::hash{})
                            po.circle_id = new_id;
                        break;
                    }
                    case tx_extra_mevatrust_circle::JOIN: {
                        bool ok = m_circle_registry->add_member(op.circle_id, op.target_pubkey, op.signer_pubkey);
                        if (!ok) { MWARNING("[MevaTrustManager] Circle JOIN fallito"); continue; }
                        m_circle_registry->mark_tx_processed(op.circle_id, tx_hash);
                        po.op = PoppedOp::CIRCLE_JOIN;
                        break;
                    }
                    case tx_extra_mevatrust_circle::LEAVE: {
                        // Capture admin pubkey before removal for reorg rollback
                        CircleEntry ce;
                        if (m_circle_registry->get_circle(op.circle_id, ce))
                            po.reorg_data = std::string(reinterpret_cast<const char*>(ce.admin_pubkey.data), 32);
                        bool ok = m_circle_registry->remove_member(op.circle_id, op.target_pubkey, op.signer_pubkey);
                        if (!ok) { MWARNING("[MevaTrustManager] Circle LEAVE fallito"); continue; }
                        m_circle_registry->mark_tx_processed(op.circle_id, tx_hash);
                        po.op = PoppedOp::CIRCLE_LEAVE;
                        break;
                    }
                    case tx_extra_mevatrust_circle::CHANGE_ADMIN: {
                        // Capture old admin pubkey before change for reorg rollback
                        CircleEntry ce;
                        if (m_circle_registry->get_circle(op.circle_id, ce))
                            po.reorg_data = std::string(reinterpret_cast<const char*>(ce.admin_pubkey.data), 32);
                        bool ok = m_circle_registry->change_admin(op.circle_id, op.target_pubkey, op.signer_pubkey);
                        if (!ok) { MWARNING("[MevaTrustManager] Circle CHANGE_ADMIN fallito"); continue; }
                        m_circle_registry->mark_tx_processed(op.circle_id, tx_hash);
                        po.op = PoppedOp::CIRCLE_CHANGE_ADMIN;
                        break;
                    }
                    case tx_extra_mevatrust_circle::DISBAND: {
                        // Capture full circle entry before disband for reorg rollback
                        CircleEntry ce;
                        if (m_circle_registry->get_circle(op.circle_id, ce))
                            po.reorg_data = CircleRegistry::pack_circle_entry(ce);
                        bool ok = m_circle_registry->disband_circle(op.circle_id, op.signer_pubkey);
                        if (!ok) { MWARNING("[MevaTrustManager] Circle DISBAND fallito"); continue; }
                        po.op = PoppedOp::CIRCLE_DISBAND;
                        break;
                    }
                }
                m_reorg_log[height].push_back(po);
                MINFO("[MevaTrustManager] Circle op elaborata h=" << height
                      << " type=" << (int)op.op_type
                      << " cid=" << epee::string_tools::pod_to_hex(op.circle_id)
                      << " tx=" << epee::string_tools::pod_to_hex(tx_hash));
            }
        }
        // Tag 0xA9 ? Circle Vote operations (prima/seconda convocazione)
        {
            tx_extra_mevatrust_circle_vote v;
            if (mevatrust::parse_mevatrust_circle_vote_from_tx(tx, v)) {
                if (!mevatrust::verify_circle_vote_signature(v)) {
                    MWARNING("[MevaTrustManager] Circle Vote sig INVALIDA");
                    continue;
                }
                if (!m_circle_registry) {
                    MWARNING("[MevaTrustManager] CircleRegistry non inizializzato");
                    continue;
                }
                const crypto::hash tx_hash = cryptonote::get_transaction_hash(tx);
                PoppedOp po; po.circle_id = v.circle_id; po.pubkey = v.target_pubkey;
                switch (v.op_type) {
                    case tx_extra_mevatrust_circle_vote::PROPOSE_CHANGE_ADMIN: {
                        crypto::hash pid = m_circle_registry->create_proposal(
                            v.circle_id, v.signer_pubkey, v.target_pubkey, height);
                        po.op = PoppedOp::CIRCLE_PROPOSE;
                        if (pid != crypto::hash{})
                            po.node_id = pid; // riusa node_id per proposal_id
                        MINFO("[MevaTrustManager] Proposta creata pid="
                              << epee::string_tools::pod_to_hex(pid));
                        break;
                    }
                    case tx_extra_mevatrust_circle_vote::CAST_VOTE: {
                        bool ok = m_circle_registry->cast_vote(
                            v.proposal_id, v.signer_pubkey, v.vote_yes, height);
                        if (!ok) { MWARNING("[MevaTrustManager] CAST_VOTE fallito"); continue; }
                        po.op = PoppedOp::CIRCLE_VOTE;
                        MINFO("[MevaTrustManager] Voto registrato prop="
                              << epee::string_tools::pod_to_hex(v.proposal_id)
                              << " vote=" << (v.vote_yes ? "SI" : "NO"));
                        break;
                    }
                    case tx_extra_mevatrust_circle_vote::FINALIZE_VOTE: {
                        uint8_t result = m_circle_registry->finalize_vote(
                            v.proposal_id, height);
                        po.op = PoppedOp::CIRCLE_FINALIZE;
                        std::string result_str = (result == 2) ? "APPROVATA" :
                                                 (result == 3) ? "BOCCIATA" :
                                                 (result == 0) ? "SECONDA_CONVOCAZIONE" : "ERRORE";
                        MINFO("[MevaTrustManager] Voto finalizzato prop="
                              << epee::string_tools::pod_to_hex(v.proposal_id)
                              << " risultato=" << (int)result << " " << result_str);
                        break;
                    }
                }
                m_reorg_log[height].push_back(po);
            }
        }
        // Tag 0xA4 ? Penalty event (on-chain penalty/ban/unban)
        {
            tx_extra_mevatrust_penalty pen;
            if (mevatrust::parse_mevatrust_penalty_from_tx(tx, pen)) {
                if (!mevatrust::verify_penalty_signature(pen)) {
                    MWARNING("[MevaTrustManager] Penalty sig INVALIDA");
                    continue;
                }
                if (!m_node_registry) { MWARNING("[MevaTrustManager] NodeRegistry non inizializzato"); continue; }
                crypto::hash nid = pen.node_id;
                if (pen.op_type == tx_extra_mevatrust_penalty::BAN) {
                    m_node_registry->ban_node(nid, pen.reason);
                    MINFO("[MevaTrustManager] Nodo bannato on-chain h=" << height
                          << " node=" << epee::string_tools::pod_to_hex(nid));
                } else if (pen.op_type == tx_extra_mevatrust_penalty::UNBAN) {
                    m_node_registry->unban_node(nid);
                    MINFO("[MevaTrustManager] Nodo sbannato on-chain h=" << height
                          << " node=" << epee::string_tools::pod_to_hex(nid));
                } else {
                    mevatrust::OffenseType ot;
                    if (pen.offense_type == "UPTIME_VIOLATION") ot = mevatrust::OffenseType::UPTIME_VIOLATION;
                    else if (pen.offense_type == "CHALLENGE_FAILURE") ot = mevatrust::OffenseType::CHALLENGE_FAILURE;
                    else if (pen.offense_type == "SYNC_FAILURE") ot = mevatrust::OffenseType::SYNC_FAILURE;
                    else if (pen.offense_type == "DOUBLE_REGISTRATION") ot = mevatrust::OffenseType::DOUBLE_REGISTRATION;
                    else if (pen.offense_type == "BYZANTINE_BEHAVIOR") ot = mevatrust::OffenseType::BYZANTINE_BEHAVIOR;
                    else ot = mevatrust::OffenseType::MALICIOUS_ACTIVITY;
                    mevatrust::apply_penalty(nid, ot, height);
                    MINFO("[MevaTrustManager] Penalita applicata on-chain h=" << height
                          << " node=" << epee::string_tools::pod_to_hex(nid)
                          << " type=" << pen.offense_type);
                }
                PoppedOp po; po.op = PoppedOp::PENALTY; po.node_id = nid;
                m_reorg_log[height].push_back(po);
            }
        }
        // Tag 0xA5 ? Uptime commitment
        {
            tx_extra_mevatrust_uptime upt;
            if (mevatrust::parse_mevatrust_uptime_from_tx(tx, upt)) {
                if (!mevatrust::verify_uptime_signature(upt)) {
                    MWARNING("[MevaTrustManager] Uptime sig INVALIDA");
                    continue;
                }
                if (!m_node_registry) { MWARNING("[MevaTrustManager] NodeRegistry non inizializzato"); continue; }
                m_node_registry->record_uptime_event(upt.node_id, true, upt.timestamp, upt.peer_count, 0);
                m_node_registry->update_node_sync(upt.node_id, upt.sync_height, upt.is_synced);
                MINFO("[MevaTrustManager] Uptime commitment h=" << height
                      << " node=" << epee::string_tools::pod_to_hex(upt.node_id)
                      << " uptime=" << upt.uptime_seconds);
                PoppedOp po; po.op = PoppedOp::UPTIME; po.node_id = upt.node_id;
                m_reorg_log[height].push_back(po);
            }
        }
        // Tag 0xA6 ? Challenge result
        {
            tx_extra_mevatrust_challenge ch;
            if (mevatrust::parse_mevatrust_challenge_from_tx(tx, ch)) {
                if (!mevatrust::verify_challenge_signatures(ch)) {
                    MWARNING("[MevaTrustManager] Challenge sig INVALIDA");
                    continue;
                }
                if (!m_node_registry) { MWARNING("[MevaTrustManager] NodeRegistry non inizializzato"); continue; }
                m_node_registry->update_node_challenge_result(ch.challenger_node_id, ch.success);
                m_node_registry->update_node_challenge_result(ch.challenged_node_id, ch.success);
                if (ch.success) {
                    m_node_registry->update_reputation(ch.challenger_node_id, 0.02f);
                    m_node_registry->update_reputation(ch.challenged_node_id, 0.02f);
                }
                MINFO("[MevaTrustManager] Challenge result h=" << height
                      << " success=" << ch.success
                      << " challenger=" << epee::string_tools::pod_to_hex(ch.challenger_node_id));
                PoppedOp po; po.op = PoppedOp::CHALLENGE; po.node_id = ch.challenger_node_id;
                m_reorg_log[height].push_back(po);
            }
        }
        // Tag 0xA7 ? State root verification (fork detection)
        {
            tx_extra_mevatrust_state_root sr;
            if (m_state_root_verification_enabled &&
                mevatrust::parse_mevatrust_state_root_from_tx(tx, sr)) {
                crypto::hash local_root = compute_mevatrust_state_root();
                if (local_root != sr.state_root) {
                    MERROR("[MevaTrustManager] MEVATRUST STATE ROOT MISMATCH h=" << height
                           << " blocco=" << epee::string_tools::pod_to_hex(sr.state_root)
                           << " locale=" << epee::string_tools::pod_to_hex(local_root)
                           << " => FORK DETECTED! RIFIUTO BLOCCO.");
                    // Il blocco viene rifiutato piu' avanti in handle_block_to_main_chain()
                }
            }
        }
        // Tag 0xA8 ? Store operations (on-chain store)
        {
            tx_extra_mevatrust_store st;
            if (mevatrust::parse_mevatrust_store_from_tx(tx, st)) {
                const crypto::hash tx_hash = cryptonote::get_transaction_hash(tx);
                bool sig_valid = false;
                if (st.op == tx_extra_mevatrust_store::STORE_CONFIRM)
                    sig_valid = mevatrust::verify_store_confirm_signature(st);
                else if (st.op == tx_extra_mevatrust_store::STORE_CANCEL)
                    sig_valid = mevatrust::verify_store_cancel_signature(st);
                else if (st.op == tx_extra_mevatrust_store::BUYER_CANCEL)
                    sig_valid = mevatrust::verify_store_buyer_cancel_signature(st);
                else if (st.op == tx_extra_mevatrust_store::BUYER_CONFIRM_RECEIPT)
                    sig_valid = mevatrust::verify_store_buyer_confirm_receipt_signature(st);
                else
                    sig_valid = mevatrust::verify_store_signature(st);
                if (!sig_valid) {
                    MWARNING("[MevaTrustManager] Store op sig INVALIDA, skip");
                    continue;
                }
                if (!m_store_registry) {
                    MWARNING("[MevaTrustManager] StoreRegistry non inizializzato");
                    continue;
                }
                switch (st.op) {
                case tx_extra_mevatrust_store::STORE_CREATE: {
    // Verifica deposito: la tx deve avere un output verso il pool address
    account_public_address pool_addr = mevatrust::get_pool_address(m_nettype);
    bool deposit_found = false;
    for (const auto& vout : tx.vout) {
        txout_to_key tk;
        if (vout.target.type() == typeid(txout_to_key)) {
            tk = boost::get<txout_to_key>(vout.target);
            if (tk.key == pool_addr.m_spend_public_key) {
                deposit_found = true;
                break;
            }
        }
    }
    if (!deposit_found) {
        MWARNING("[MevaTrustManager] STORE_CREATE rifiutato: nessun output verso pool address (deposito mancante)");
        break;
    }
    // RingCT nasconde l'importo esatto; il nodo verifica solo la presenza dell'output.
    // La verifica dell'ammontare minimo (STORE_DEPOSIT) e' demandata al venditore off-chain.
    crypto::hash sid = m_store_registry->create_store(
        st.name, st.description, st.url, st.payment_address,
        st.euro_enabled, st.euro_details,
        st.mvc_percent, st.euro_percent,
        st.owner_pubkey, height);
    if (sid != crypto::hash{}) {
        MINFO("[MevaTrustManager] Store creato h=" << height
              << " sid=" << epee::string_tools::pod_to_hex(sid)
              << " euro=" << (st.euro_enabled ? "SI" : "NO"));
    }
    break;
}
                case tx_extra_mevatrust_store::STORE_UPDATE: {
                    if (m_store_registry->update_store(st.store_id, st.name,
                        st.description, st.url, st.payment_address,
                        st.euro_enabled, st.euro_details,
                        st.mvc_percent, st.euro_percent,
                        st.owner_pubkey)) {
                        MINFO("[MevaTrustManager] Store aggiornato h=" << height);
                    }
                    break;
                }
case tx_extra_mevatrust_store::ITEM_LIST: {
    // Verifica deposito item: output verso pool address
    account_public_address pool_addr = mevatrust::get_pool_address(m_nettype);
    bool deposit_found = false;
    for (const auto& vout : tx.vout) {
        txout_to_key tk;
        if (vout.target.type() == typeid(txout_to_key)) {
            tk = boost::get<txout_to_key>(vout.target);
            if (tk.key == pool_addr.m_spend_public_key) {
                deposit_found = true;
                break;
            }
        }
    }
    if (!deposit_found) {
        MWARNING("[MevaTrustManager] ITEM_LIST rifiutato: nessun output verso pool address (deposito mancante)");
        break;
    }
    crypto::hash iid = m_store_registry->list_item(
        st.store_id, st.name, st.description, st.price,
        st.quantity, st.category, st.metadata,
        st.payment_mode, height);
    if (iid != crypto::hash{}) {
        MINFO("[MevaTrustManager] Item listato h=" << height
              << " iid=" << epee::string_tools::pod_to_hex(iid));
    }
    break;
}
                case tx_extra_mevatrust_store::ITEM_DELIST: {
                    m_store_registry->delist_item(st.store_id, st.item_id, st.owner_pubkey);
                    break;
                }
                case tx_extra_mevatrust_store::STORE_DEACTIVATE: {
                    if (m_store_registry->deactivate_store(st.store_id, st.owner_pubkey)) {
                        MINFO("[MevaTrustManager] Store disattivato h=" << height
                              << " sid=" << epee::string_tools::pod_to_hex(st.store_id));
                    }
                    break;
                }
                case tx_extra_mevatrust_store::ITEM_BUY: {
    StoreItemEntry item;
    uint64_t price = 0;
    bool item_ok = false;
    if (m_store_registry->get_item(st.item_id, item) && item.active && item.quantity > 0) {
        price = item.price;
        item_ok = true;
    }
    if (!item_ok) {
        MWARNING("[MevaTrustManager] ITEM_BUY rifiutato: item inesistente/esaurito");
        break;
    }
    // RingCT nasconde gli importi: la validazione on-chain non puo' verificare l'ammontare
    // del pagamento. La verifica avviene off-chain: il venditore controlla il proprio wallet.
    if (m_store_registry->buy_item(st.store_id, st.item_id,
        st.buyer_pubkey, height, static_cast<uint64_t>(time(nullptr)),
        price, st.euro_ref, st.euro_amount)) {
        MINFO("[MevaTrustManager] Acquisto registrato h=" << height
              << " buyer=" << epee::string_tools::pod_to_hex(st.buyer_pubkey)
              << " prezzo=" << price
              << " expiry=" << (height + StoreRegistry::CONFIRM_WINDOW_BLOCKS)
              << " metodo=" << st.buyer_payment_method);
    }
    break;
}
                case tx_extra_mevatrust_store::STORE_CONFIRM: {
    if (!mevatrust::verify_store_confirm_signature(st)) {
        MWARNING("[MevaTrustManager] STORE_CONFIRM sig INVALIDA");
        break;
    }
    StoreEntry store;
    if (!m_store_registry->get_store(st.store_id, store) || !store.active) {
        MWARNING("[MevaTrustManager] STORE_CONFIRM: store inesistente/disattivato");
        break;
    }
    if (memcmp(store.owner_pubkey.data, st.seller_pubkey.data, 32) != 0) {
        MWARNING("[MevaTrustManager] STORE_CONFIRM: seller non e' il proprietario");
        break;
    }
    if (m_store_registry->confirm_purchase(st.store_id, st.item_id,
        st.buyer_pubkey, st.seller_pubkey,
        tx_hash, height)) {
        MINFO("[MevaTrustManager] STORE_CONFIRM ok h=" << height
              << " item=" << epee::string_tools::pod_to_hex(st.item_id));
    }
    break;
}
                case tx_extra_mevatrust_store::STORE_CANCEL: {
    if (!mevatrust::verify_store_cancel_signature(st)) {
        MWARNING("[MevaTrustManager] STORE_CANCEL sig INVALIDA");
        break;
    }
    StoreEntry store;
    if (!m_store_registry->get_store(st.store_id, store) || !store.active) {
        MWARNING("[MevaTrustManager] STORE_CANCEL: store inesistente/disattivato");
        break;
    }
    if (memcmp(store.owner_pubkey.data, st.seller_pubkey.data, 32) != 0) {
        MWARNING("[MevaTrustManager] STORE_CANCEL: seller non e' il proprietario");
        break;
    }
    if (m_store_registry->cancel_purchase(st.store_id, st.item_id,
        st.buyer_pubkey, st.seller_pubkey,
        st.cancel_reason, tx_hash, height)) {
        MINFO("[MevaTrustManager] STORE_CANCEL ok h=" << height
              << " item=" << epee::string_tools::pod_to_hex(st.item_id)
              << " reason=" << st.cancel_reason);
    }
    break;
}
                case tx_extra_mevatrust_store::BUYER_CANCEL: {
    if (!mevatrust::verify_store_buyer_cancel_signature(st)) {
        MWARNING("[MevaTrustManager] BUYER_CANCEL sig INVALIDA");
        break;
    }
    if (m_store_registry->buyer_cancel_purchase(st.store_id, st.item_id,
        st.buyer_pubkey, tx_hash, height)) {
        MINFO("[MevaTrustManager] BUYER_CANCEL ok h=" << height
              << " item=" << epee::string_tools::pod_to_hex(st.item_id));
    }
    break;
}
                case tx_extra_mevatrust_store::BUYER_CONFIRM_RECEIPT: {
    if (!mevatrust::verify_store_buyer_confirm_receipt_signature(st)) {
        MWARNING("[MevaTrustManager] BUYER_CONFIRM_RECEIPT sig INVALIDA");
        break;
    }
    if (m_store_registry->buyer_confirm_receipt(st.store_id, st.item_id,
        st.buyer_pubkey, height)) {
        MINFO("[MevaTrustManager] BUYER_CONFIRM_RECEIPT ok h=" << height
              << " item=" << epee::string_tools::pod_to_hex(st.item_id));
    }
    break;
}
                }
                PoppedOp po; po.op = PoppedOp::STORE;
                po.node_id = st.store_id; po.reorg_data = st.name;
                m_reorg_log[height].push_back(po);
            }
        }
        // Tag 0xAA ? Pool Distribution
        {
            tx_extra_mevatrust_pool_distribution pd;
            if (mevatrust::parse_mevatrust_pool_distribution_from_tx(tx, pd)) {
                MINFO("[MevaTrustManager] Pool distribution TX found h=" << height
                      << " period=" << pd.period
                      << " amount=" << pd.total_distributed);
                // Validazione consensus fatta in check_distribution_tx
                PoppedOp po; po.op = PoppedOp::POOL_DISTRIBUTION;
                po.reorg_data = std::to_string(pd.total_distributed);
                m_reorg_log[height].push_back(po);
            }
        }
    }

    // Expire nodi offline ogni 240 blocchi
    if (height > 0 && (height % 240) == 0) {
        m_node_registry->expire_inactive_nodes(height);
    }
}

void MevaTrustManager::rebuild_registry_from_chain(
    uint64_t start_height, uint64_t end_height)
{
    if (!m_initialized.load()) {
        MERROR("[MevaTrustManager] rebuild_registry_from_chain: non inizializzato");
        return;
    }
    if (!m_get_block_func) {
        MERROR("[MevaTrustManager] rebuild_registry_from_chain: get_block_func non wired");
        return;
    }

    MINFO("[MevaTrustManager] rebuild_registry_from_chain da h="
          << start_height << " a h=" << end_height);

    // Azzera il registry prima di ricostruire
    m_node_registry->clear_all();

    // Itera ogni blocco e processa le TX di registrazione/deregistrazione
    for (uint64_t h = start_height; h <= end_height; ++h) {
        block bl;
        if (!m_get_block_func(h, bl)) {
            MWARNING("[MevaTrustManager] rebuild: blocco non trovato h=" << h);
            continue;
        }
        process_mevatrust_txs(bl, h);
    }

    MINFO("[MevaTrustManager] rebuild_registry_from_chain completato: "
          << m_node_registry->get_active_node_count() << " nodi attivi");
}


// ============================================================================
// [C2] Badge On-Chain: build_pending_snapshot + consume_pending_snapshot_extra
// ============================================================================

// Costruisce la struttura tx_extra_mevatrust_snapshot con tutti i badge
// assegnati dall'ultimo periodo ad oggi, firma ogni award con m_node_sk,
// serializza in m_pending_snapshot_extra (tag 0xA2 ready to append to tx.extra).
// Chiamato internamente da on_new_block() al period boundary.
void MevaTrustManager::build_pending_snapshot(uint64_t height)
{
  // Richiede la chiave nodo per firmare -- salta se non configurata
  if (!m_node_key_set) {
    MDEBUG("[C2] build_pending_snapshot: node key non configurata, skip h=" << height);
    return;
  }
  if (!m_badge_system) return;

  // Raccoglie i badge assegnati nell'ultimo periodo
  auto raw = m_badge_system->get_recently_awarded(m_last_period_height, height);
  if (raw.empty()) {
    MDEBUG("[C2] build_pending_snapshot: nessun badge nel periodo, skip h=" << height);
    return;
  }

  // Costruisce lo snapshot
  tx_extra_mevatrust_snapshot snap;
  snap.height     = height;
  snap.period     = static_cast<uint32_t>(height / m_period_length);
  snap.node_count = m_node_registry ? m_node_registry->get_active_node_count() : 0u;

  for (const auto& kv : raw) {
    const crypto::hash& nid = kv.first;
    const BadgeType     bt  = kv.second;

    tx_extra_mevatrust_snapshot::BadgeAward ba;
    ba.node_id        = nid;
    ba.badge_type     = static_cast<uint8_t>(bt);
    ba.awarded_height = height;
    ba.proposer_pubkey = m_node_pk;

    // Firma: H(node_id[32] || badge_type[1] || height[8])
    std::string msg;
    msg.append(reinterpret_cast<const char*>(nid.data), 32);
    msg.push_back(static_cast<char>(ba.badge_type));
    const uint64_t h_le = height;
    msg.append(reinterpret_cast<const char*>(&h_le), 8);
    const crypto::hash mh = crypto::cn_fast_hash(msg.data(), msg.size());
    crypto::generate_signature(mh, m_node_pk, m_node_sk, ba.proposer_sig);

    snap.badge_awards.push_back(std::move(ba));
  }

  // Serializza in 0xA2 blob
  m_pending_snapshot_extra.clear();
  if (!mevatrust::build_mevatrust_snapshot_extra(snap, m_pending_snapshot_extra)) {
    MERROR("[C2] build_pending_snapshot: serializzazione fallita h=" << height);
    return;
  }
  m_has_pending_snapshot = true;
  MINFO("[C2] Snapshot on-chain pronto: " << snap.badge_awards.size()
        << " badge awards da embeddare nel miner_tx, h=" << height);
}

// Restituisce e svuota il buffer 0xA2 (consume semantics).
// Chiamare PRIMA di construct_miner_tx_with_mevatrust().
std::vector<uint8_t> MevaTrustManager::consume_pending_snapshot_extra()
{
  std::lock_guard<std::mutex> lk(m_lock);
  if (!m_has_pending_snapshot) return {};
  m_has_pending_snapshot = false;
  MINFO("[C2] consume_pending_snapshot_extra: "
        << m_pending_snapshot_extra.size() << " bytes consegnati al miner");
  auto result = std::move(m_pending_snapshot_extra);
  m_pending_snapshot_extra.clear();
  return result;
}

// ?? Pool Distribution: restituisce e svuota il buffer 0xAA (consume semantics).
std::vector<uint8_t> MevaTrustManager::consume_pending_pool_distribution_extra()
{
  std::lock_guard<std::mutex> lk(m_lock);
  if (!m_has_pending_pool_distribution) return {};
  m_has_pending_pool_distribution = false;
  MINFO("[PoolDist] consume_pending_pool_distribution_extra: "
        << m_pending_pool_distribution_extra.size() << " bytes consegnati al miner");
  auto result = std::move(m_pending_pool_distribution_extra);
  m_pending_pool_distribution_extra.clear();
  return result;
}



// ── [C4] Accumula voto da proposer P2P — chiama SnapshotBroadcaster::on_receive_vote().
// Restituisce QuorumVoteResult per ogni badge award nel voto accettato.
// Se il quorum (>= 3 proposer distinti) e' raggiunto, l'apply_func
// impostata in init() serializza il blob 0xA2 in m_pending_snapshot_extra.
std::vector<MevaTrustManager::QuorumVoteResult>
MevaTrustManager::submit_snapshot_for_quorum(
  const tx_extra_mevatrust_snapshot& snap)
{
  std::vector<QuorumVoteResult> results;
  if (!m_snapshot_broadcaster) {
    MWARNING("[MevaTrustManager] submit_snapshot_for_quorum: broadcaster non inizializzato");
    return results;
  }
  // Delega al ballot-box del SnapshotBroadcaster.
  // on_receive_vote() verifica la firma del proposer, accumula nel ballot_box_,
  // e chiama try_finalize() -> apply_func quando raggiunge QUORUM_THRESHOLD.
  if (!m_snapshot_broadcaster->on_receive_vote(snap)) {
    MINFO("[MevaTrustManager] C4: voto snapshot rifiutato (firma invalida o duplicato) h="
          << snap.height);
    return results;
  }
  MINFO("[MevaTrustManager] C4: voto snapshot accettato h=" << snap.height
        << " badges=" << snap.badge_awards.size());
  // Restituisce QuorumVoteResult per ogni award nel voto
  results.reserve(snap.badge_awards.size());
  for (const auto& award : snap.badge_awards) {
    QuorumVoteResult r;
    r.node_id        = award.node_id;
    r.badge_type     = award.badge_type;
    r.awarded_height = award.awarded_height;
    results.push_back(r);
  }
  return results;
}

// ============================================================================
// Reorg Rollback — inverte lo stato MevaTrust per un blocco rimosso
// ============================================================================
void MevaTrustManager::on_mevatrust_block_popped(const block& bl, uint64_t height)
{
    if (!m_initialized.load()) return;
    auto it = m_reorg_log.find(height);
    if (it == m_reorg_log.end()) return;

    auto& ops = it->second;
    // Inverti in ordine inverso
    for (auto ri = ops.rbegin(); ri != ops.rend(); ++ri) {
        const auto& po = *ri;
        switch (po.op) {
            case PoppedOp::REGISTER:
                // Re-register + deregister: rimuovi nodo
                if (m_node_registry)
                    m_node_registry->unregister_node(po.node_id);
                break;
            case PoppedOp::DEREGISTER:
                // Non possiamo re-registrare automaticamente (mancano i dati originali)
                // La prossima sync dalla chain ripristinera' lo stato corretto
                MWARNING("[MevaTrustManager] Reorg deregister h=" << height
                         << ": nodo " << epee::string_tools::pod_to_hex(po.node_id)
                         << " sara' ripristinato al prossimo re-scan");
                break;
            case PoppedOp::BADGE_AWARD:
                if (m_badge_system)
                    m_badge_system->revoke_badge(po.node_id, static_cast<BadgeType>(po.badge_type), "reorg-rollback");
                break;
            case PoppedOp::BADGE_REVOKE:
                // Non possiamo ri-assegnare automaticamente
                break;
            case PoppedOp::VALIDATOR_PROMOTION:
                if (m_node_registry)
                    m_node_registry->update_node_validator(po.node_id, false, 0, 0);
                if (m_badge_system)
                    m_badge_system->revoke_badge(po.node_id, static_cast<BadgeType>(po.badge_type), "reorg-rollback");
                break;
            case PoppedOp::CIRCLE_CREATE:
                if (m_circle_registry)
                    m_circle_registry->disband_circle(po.circle_id, po.pubkey);
                break;
            case PoppedOp::CIRCLE_DISBAND:
                // Ripristina la cerchia dai dati serializzati pre-disband
                if (m_circle_registry && !po.reorg_data.empty()) {
                    CircleEntry ce;
                    if (CircleRegistry::unpack_circle_entry(po.reorg_data, ce))
                        m_circle_registry->restore_circle(ce);
                }
                break;
            case PoppedOp::CIRCLE_JOIN:
                if (m_circle_registry)
                    m_circle_registry->remove_member(po.circle_id, po.pubkey, po.pubkey);
                break;
            case PoppedOp::CIRCLE_LEAVE:
                // Ri-aggiunge il membro usando l'admin pubkey salvato in reorg_data
                if (m_circle_registry && !po.reorg_data.empty()) {
                    crypto::public_key admin_pk;
                    if (po.reorg_data.size() >= sizeof(admin_pk)) {
                        memcpy(&admin_pk, po.reorg_data.data(), sizeof(admin_pk));
                        m_circle_registry->add_member(po.circle_id, po.pubkey, admin_pk);
                    }
                }
                break;
            case PoppedOp::CIRCLE_CHANGE_ADMIN:
                // Ripristina l'admin precedente salvato in reorg_data
                if (m_circle_registry && !po.reorg_data.empty()) {
                    crypto::public_key old_admin;
                    if (po.reorg_data.size() >= sizeof(old_admin)) {
                        memcpy(&old_admin, po.reorg_data.data(), sizeof(old_admin));
                        m_circle_registry->set_admin(po.circle_id, old_admin);
                    }
                }
                break;
            case PoppedOp::PENALTY:
                if (m_node_registry)
                    m_node_registry->update_node_status(po.node_id, NodeStatus::ACTIVE);
                break;
            case PoppedOp::UPTIME:
            case PoppedOp::CHALLENGE:
            case PoppedOp::STORE:
            case PoppedOp::CIRCLE_PROPOSE:
            case PoppedOp::CIRCLE_VOTE:
            case PoppedOp::CIRCLE_FINALIZE:
            case PoppedOp::POOL_DISTRIBUTION:
                // Dati non reversibili, verranno ricalcolati al prossimo re-scan
                break;
        }
    }
    m_reorg_log.erase(it);
    MINFO("[MevaTrustManager] Reorg rollback completato h=" << height
          << " (" << ops.size() << " operazioni invertite)");
}

// ============================================================================
// State Commitment
// ============================================================================
crypto::hash MevaTrustManager::compute_mevatrust_state_root() const
{
    // Costruisce un hash ordinato di tutto lo stato MevaTrust:
    // 1. Hash di tutti i nodi registrati
    // 2. Hash di tutte le cerchie
    // 3. Hash di tutti i badge attivi
    // 4. Pool balance (on-chain, deterministico da chain)
    std::string state_data;

    // Nodi registrati
    if (m_node_registry) {
        auto nodes = m_node_registry->get_all_nodes();
        std::sort(nodes.begin(), nodes.end(),
            [](const NodeRegistryEntry& a, const NodeRegistryEntry& b) {
                return memcmp(a.node_id.data, b.node_id.data, 32) < 0;
            });
        for (const auto& n : nodes) {
            state_data.append(reinterpret_cast<const char*>(n.node_id.data), 32);
            state_data.push_back(static_cast<uint8_t>(n.status));
            state_data.append(reinterpret_cast<const char*>(&n.reputation_score), sizeof(n.reputation_score));
        }
    }

    // Cerchie — include TUTTI i membri per fork detection robusta
    if (m_circle_registry) {
        auto circles = m_circle_registry->list_circles();
        std::sort(circles.begin(), circles.end(),
            [](const CircleEntry& a, const CircleEntry& b) {
                return memcmp(a.circle_id.data, b.circle_id.data, 32) < 0;
            });
        for (const auto& c : circles) {
            state_data.append(reinterpret_cast<const char*>(c.circle_id.data), 32);
            state_data.append(reinterpret_cast<const char*>(c.admin_pubkey.data), 32);
            auto members = c.members;
            std::sort(members.begin(), members.end(),
                [](const crypto::public_key& a, const crypto::public_key& b) {
                    return memcmp(a.data, b.data, 32) < 0;
                });
            for (const auto& m : members)
                state_data.append(reinterpret_cast<const char*>(m.data), 32);
        }
    }

    // Pool balance — deterministico da chain (non da LMDB locale)
    uint64_t pool_balance = compute_pool_balance_from_chain();
    state_data.append(reinterpret_cast<const char*>(&pool_balance), sizeof(pool_balance));

    return crypto::cn_fast_hash(state_data.data(), state_data.size());
}

// Calcola pool_balance on-chain: somma 3% contributi - distribuzioni eseguite
// Itera blocchi da HF_MEVATRUST_POOL activation height
uint64_t MevaTrustManager::compute_pool_balance_from_chain() const
{
    if (!m_get_block_func) return 0;
    
    const uint64_t HF_POOL_ACTIVATION = 13; // Activation at HF_VERSION_MEVATRUST (v13)
    uint64_t current_height = 0;
    if (m_get_block_func) {
        block dummy;
        if (m_get_block_func(0, dummy)) {
            // Find current tip height
            for (uint64_t h = 1; ; ++h) {
                if (!m_get_block_func(h, dummy)) { current_height = h - 1; break; }
            }
        }
    }
    if (current_height <= HF_POOL_ACTIVATION) return 0;

    uint64_t total_contributions = 0;
    uint64_t total_distributions = 0;
    
    block bl;
    for (uint64_t h = HF_POOL_ACTIVATION + 1; h <= current_height; ++h) {
        if (!m_get_block_func(h, bl)) continue;
        
        // 1. Contribution: 3% of block reward (base_reward + fees)
        uint64_t block_reward = bl.miner_tx.vout.empty() ? 0 : bl.miner_tx.vout[0].amount;
        for (size_t i = 1; i < bl.miner_tx.vout.size(); ++i) {
            block_reward += bl.miner_tx.vout[i].amount;
        }
        if (block_reward > 0) {
            total_contributions += block_reward * 3 / 100;
        }
        
        // 2. Check for distribution transactions in this block (identifiable by tag 0xAA)
        for (const auto& tx_hash : bl.tx_hashes) {
            transaction tx;
            if (!m_get_tx_func(tx_hash, tx)) continue;

            // Check if this TX has a pool distribution extra tag (0xAA)
            tx_extra_mevatrust_pool_distribution dist;
            if (mevatrust::parse_mevatrust_pool_distribution_from_tx(tx, dist)) {
                // Sum all outputs — these are rewards distributed to nodes
                uint64_t tx_total = 0;
                for (const auto& out : tx.vout) {
                    tx_total += out.amount;
                }
                total_distributions += tx_total;
            }
        }
    }
    
    return total_contributions >= total_distributions ? total_contributions - total_distributions : 0;
}

bool MevaTrustManager::verify_mevatrust_state_root(const block& bl, uint64_t height) const
{
    if (!m_state_root_verification_enabled) return true;
    tx_extra_mevatrust_state_root sr;
    if (!mevatrust::parse_mevatrust_state_root_from_tx(bl.miner_tx, sr)) {
        MWARNING("[MevaTrustManager] State root non trovato nel miner_tx h=" << height);
        return true; // blocco pre-attivazione
    }
    if (sr.height != height) {
        MWARNING("[MevaTrustManager] State root height mismatch h=" << height);
        return false;
    }
    crypto::hash local = compute_mevatrust_state_root();
    if (local != sr.state_root) {
        MERROR("[MevaTrustManager] FORK DETECTED! State root mismatch h=" << height
               << " chain=" << epee::string_tools::pod_to_hex(sr.state_root)
               << " local=" << epee::string_tools::pod_to_hex(local));
        return false;
    }
    return true;
}

} // namespace cryptonote




