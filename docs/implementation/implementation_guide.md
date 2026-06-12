# GUIDA IMPLEMENTAZIONE COMPLETA
# SISTEMA INCENTIVI FULL NODE MEVACOIN

## FASE 1: PREPARAZIONE INFRASTRUTTURA (1-2 settimane)

### 1.1 Struttura Directory

```bash
# Creare la directory participation nel src
mkdir -p src/participation

# File da aggiungere
src/participation/node_registry.h          ✓ (file creato)
src/participation/node_registry.cpp         (da implementare)
src/participation/participation_engine.h    ✓ (file creato)
src/participation/participation_engine.cpp  (da implementare)
src/participation/availability_proof.h      ✓ (file creato)
src/participation/availability_proof.cpp    (da implementare)
src/participation/badge_system.h            ✓ (file creato)
src/participation/badge_system.cpp          (da implementare)
src/participation/reward_distributor.h      ✓ (file creato)
src/participation/reward_distributor.cpp    (da implementare)
```

### 1.2 Aggiornamento CMakeLists.txt

```cmake
# In src/CMakeLists.txt, aggiungere:

set(participation_sources
  participation/node_registry.cpp
  participation/participation_engine.cpp
  participation/availability_proof.cpp
  participation/badge_system.cpp
  participation/reward_distributor.cpp
)

# Aggiungere ai target che linkano cryptonote_core:
target_sources(cryptonote_core PRIVATE ${participation_sources})

# Includere paths
target_include_directories(cryptonote_core 
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/participation
)
```

### 1.3 Database Backend

Scegliere uno tra:

**Opzione A: LMDB** (Consigliato - stesso di Mevacoin)
```cpp
#include "blockchain_db/blockchain_db.h"
// Estendere il database di blockchain per nuovo schema
```

**Opzione B: LevelDB**
```cpp
#include "blockchain_db/lmdb/db_lmdb.h"
// Creare database separato per participation
```

**Opzione C: SQLite** (Più semplice, meno performante)
```cpp
#include "sqlite3.h"
// Buono per test ma non production
```

**Raccomandazione**: Estendere LMDB esistente di Mevacoin

---

## FASE 2: IMPLEMENTAZIONE NODE REGISTRY (Week 1)

### 2.1 File: src/participation/node_registry.cpp

```cpp
#include "node_registry.h"
#include "crypto/hash.h"
#include "serialization/binary_io.h"
#include <cassert>
#include <mutex>

namespace cryptonote {

NodeRegistry::NodeRegistry(const std::string& db_path)
  : db_path_(db_path)
{
  open_database();
  load_from_disk();
}

NodeRegistry::~NodeRegistry() {
  save_to_disk();
  close_database();
}

crypto::hash NodeRegistry::compute_node_id(
  const crypto::public_key& wallet_pubkey,
  const crypto::public_key& node_pubkey,
  uint64_t timestamp)
{
  // Hash: H(wallet_pubkey || node_pubkey || timestamp)
  epee::serialization::portable_storage ps;
  ps.set_value("wallet_pubkey", epee::string_tools::pod_to_hex(wallet_pubkey));
  ps.set_value("node_pubkey", epee::string_tools::pod_to_hex(node_pubkey));
  ps.set_value("timestamp", timestamp);
  
  std::string data = ps.dump();
  crypto::hash h;
  crypto::cn_fast_hash(data.data(), data.size(), h);
  return h;
}

bool NodeRegistry::verify_registration_signature(
  const crypto::public_key& wallet_pubkey,
  const crypto::signature& signature,
  const std::string& wallet_address)
{
  // Verify: sign(wallet_address, wallet_privkey)
  // Public verification using wallet_pubkey
  
  crypto::hash msg_hash;
  crypto::cn_fast_hash(
    wallet_address.data(),
    wallet_address.size(),
    msg_hash
  );
  
  return crypto::check_signature(msg_hash, wallet_pubkey, signature);
}

bool NodeRegistry::register_node(
  const crypto::public_key& wallet_pubkey,
  const std::string& wallet_address,
  const crypto::public_key& node_pubkey,
  const crypto::signature& registration_signature,
  uint16_t port,
  const std::string& ip_address,
  uint64_t height)
{
  std::lock_guard<std::mutex> lock(nodes_lock_);
  
  // Verify signature
  if (!verify_registration_signature(wallet_pubkey, registration_signature, wallet_address))
  {
    LOG_ERROR("Invalid registration signature for wallet: " << wallet_address);
    return false;
  }
  
  // Compute node ID
  uint64_t timestamp = time(nullptr);
  crypto::hash node_id = compute_node_id(wallet_pubkey, node_pubkey, timestamp);
  
  // Check if already registered
  auto it = nodes_.find(node_id);
  if (it != nodes_.end())
  {
    LOG_WARNING("Node already registered: " << epee::string_tools::pod_to_hex(node_id));
    return false;
  }
  
  // Create entry
  NodeRegistryEntry entry;
  entry.node_id = node_id;
  entry.wallet_address = wallet_address;
  entry.wallet_pubkey = wallet_pubkey;
  entry.node_pubkey = node_pubkey;
  entry.registered_height = height;
  entry.registered_timestamp = timestamp;
  entry.registration_signature = registration_signature;
  entry.ip_address = ip_address;
  entry.port = port;
  entry.status = NodeStatus::ACTIVE;
  entry.is_synchronized = false;
  entry.reputation_score = 1.0f;  // Start with perfect reputation
  entry.created_at = timestamp;
  entry.updated_at = timestamp;
  entry.total_challenges = 0;
  entry.successful_challenges = 0;
  entry.total_uptime_seconds = 0;
  entry.disconnections = 0;
  entry.peer_count = 0;
  entry.last_sync_height = 0;
  entry.last_challenge_time = timestamp;
  entry.next_challenge_time = timestamp + 36000;  // ~10 hours
  
  nodes_[node_id] = entry;
  
  LOG_INFO("Node registered successfully: " << epee::string_tools::pod_to_hex(node_id)
    << " | Wallet: " << wallet_address << " | Port: " << port);
  
  return true;
}

bool NodeRegistry::get_node_by_id(
  const crypto::hash& node_id,
  NodeRegistryEntry& entry)
{
  std::lock_guard<std::mutex> lock(nodes_lock_);
  
  auto it = nodes_.find(node_id);
  if (it == nodes_.end())
    return false;
  
  entry = it->second;
  return true;
}

bool NodeRegistry::get_node_by_wallet(
  const std::string& wallet_address,
  NodeRegistryEntry& entry)
{
  std::lock_guard<std::mutex> lock(nodes_lock_);
  
  for (const auto& pair : nodes_)
  {
    if (pair.second.wallet_address == wallet_address)
    {
      entry = pair.second;
      return true;
    }
  }
  return false;
}

std::vector<NodeRegistryEntry> NodeRegistry::get_active_nodes(
  uint32_t limit,
  uint32_t offset)
{
  std::lock_guard<std::mutex> lock(nodes_lock_);
  
  std::vector<NodeRegistryEntry> result;
  uint32_t count = 0;
  
  for (const auto& pair : nodes_)
  {
    if (pair.second.status == NodeStatus::ACTIVE)
    {
      if (count >= offset && (limit == 0 || result.size() < limit))
      {
        result.push_back(pair.second);
      }
      count++;
    }
  }
  
  return result;
}

bool NodeRegistry::update_node_seen(
  const crypto::hash& node_id,
  const std::string& ip,
  uint32_t peer_count)
{
  std::lock_guard<std::mutex> lock(nodes_lock_);
  
  auto it = nodes_.find(node_id);
  if (it == nodes_.end())
    return false;
  
  it->second.last_seen_timestamp = time(nullptr);
  it->second.ip_address = ip;
  it->second.peer_count = peer_count;
  it->second.updated_at = time(nullptr);
  
  return true;
}

uint32_t NodeRegistry::count_active_nodes() const
{
  std::lock_guard<std::mutex> lock(nodes_lock_);
  
  uint32_t count = 0;
  for (const auto& pair : nodes_)
  {
    if (pair.second.status == NodeStatus::ACTIVE)
      count++;
  }
  return count;
}

bool NodeRegistry::save_to_disk() const
{
  std::lock_guard<std::mutex> lock(nodes_lock_);
  
  epee::serialization::portable_storage ps;
  
  epee::serialization::storage_entry nodes_array;
  for (const auto& pair : nodes_)
  {
    epee::serialization::storage_entry node_entry;
    pair.second.serialize(node_entry);
    nodes_array.m_array.push_back(node_entry);
  }
  
  ps.set_value("nodes", nodes_array);
  ps.set_value("saved_at", (uint64_t)time(nullptr));
  
  std::string data = ps.dump();
  
  std::ofstream file(db_path_ + "/registry.bin", std::ios::binary);
  if (!file.is_open())
    return false;
  
  file.write(data.data(), data.size());
  file.close();
  
  return true;
}

bool NodeRegistry::load_from_disk()
{
  std::lock_guard<std::mutex> lock(nodes_lock_);
  
  std::ifstream file(db_path_ + "/registry.bin", std::ios::binary);
  if (!file.is_open())
    return true;  // First run
  
  std::string data((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  file.close();
  
  // Parse and restore nodes
  // ... deserialize logic ...
  
  return true;
}

bool NodeRegistry::open_database()
{
  // Create directory if not exists
  // mkdir db_path_
  return true;
}

bool NodeRegistry::close_database()
{
  save_to_disk();
  return true;
}

// ... Implementare metodi rimanenti ...

} // namespace cryptonote
```

### 2.2 Integrazione nel daemon

```cpp
// In cryptonote_core/cryptonote_core.h, aggiungere:

#include "participation/node_registry.h"

class core {
private:
  std::shared_ptr<NodeRegistry> m_node_registry;
  
public:
  std::shared_ptr<NodeRegistry> get_node_registry() { return m_node_registry; }
};

// Nel constructor di core:
core::core(const boost::program_options::variables_map& vm, bool test_genesis)
{
  // ... existing init code ...
  
  m_node_registry = std::make_shared<NodeRegistry>(
    tools::get_default_data_dir() + "/node_registry"
  );
}
```

---

## FASE 3: PARTICIPATION ENGINE (Week 1-2)

### 3.1 Implementazione ParticipationEngine

```cpp
// File: src/participation/participation_engine.cpp

#include "participation_engine.h"
#include "common/util.h"

namespace cryptonote {

ParticipationEngine::ParticipationEngine(
  const std::string& db_path,
  std::shared_ptr<NodeRegistry> node_registry)
  : db_path_(db_path),
    node_registry_(node_registry),
    current_period_start_height_(0),
    last_period_processed_height_(0)
{
  // Default scoring weights
  scoring_weights_.uptime_weight = 0.40f;
  scoring_weights_.sync_weight = 0.30f;
  scoring_weights_.responsiveness_weight = 0.20f;
  scoring_weights_.activity_weight = 0.10f;
  
  // Default parameters
  parameters_.minimum_uptime_for_rewards = 360;  // 1 hour
  parameters_.period_length = 240;  // ~4 hours
  parameters_.challenges_per_period = 3;
  parameters_.challenge_timeout_ms = 2000;
  parameters_.minimum_score_for_rewards = 0.5f;
  parameters_.sync_threshold_percentage = 99;
  
  open_database();
  load_from_disk();
}

ParticipationScoreSnapshot ParticipationEngine::calculate_node_score(
  const crypto::hash& node_id,
  uint64_t current_height)
{
  std::lock_guard<std::mutex> lock(cache_lock_);
  
  ParticipationScoreSnapshot snapshot;
  snapshot.node_id = node_id;
  snapshot.period_height = current_height;
  snapshot.period_start_height = current_period_start_height_;
  snapshot.recorded_at = time(nullptr);
  
  // Calculate each component
  snapshot.uptime_score = calculate_uptime_score(node_id, current_height);
  snapshot.sync_score = calculate_sync_score(node_id, current_height, current_height);
  snapshot.responsiveness_score = calculate_responsiveness_score(node_id);
  snapshot.activity_score = calculate_activity_score(node_id, current_height);
  
  // Weighted total
  snapshot.total_score = 
    (snapshot.uptime_score * scoring_weights_.uptime_weight) +
    (snapshot.sync_score * scoring_weights_.sync_weight) +
    (snapshot.responsiveness_score * scoring_weights_.responsiveness_weight) +
    (snapshot.activity_score * scoring_weights_.activity_weight);
  
  // Clamp between 0 and 1
  snapshot.total_score = std::min(1.0f, std::max(0.0f, snapshot.total_score));
  
  return snapshot;
}

float ParticipationEngine::calculate_uptime_score(
  const crypto::hash& node_id,
  uint64_t period_end_height)
{
  auto& events = uptime_history_[node_id];
  if (events.empty())
    return 0.0f;
  
  uint64_t online_seconds = 0;
  uint64_t total_seconds = 0;
  
  // Calculate percentage of time online in period
  // from period_start to period_end
  
  // Simple: count seconds online vs offline
  // More sophisticated: weight consecutive uptime
  
  for (size_t i = 1; i < events.size(); i++)
  {
    uint64_t delta_seconds = events[i].timestamp - events[i-1].timestamp;
    total_seconds += delta_seconds;
    
    if (events[i-1].online)
      online_seconds += delta_seconds;
  }
  
  if (total_seconds == 0)
    return 0.0f;
  
  return static_cast<float>(online_seconds) / total_seconds;
}

float ParticipationEngine::calculate_sync_score(
  const crypto::hash& node_id,
  uint64_t period_end_height,
  uint64_t current_chain_height)
{
  NodeRegistryEntry entry;
  if (!node_registry_->get_node_by_id(node_id, entry))
    return 0.0f;
  
  if (!entry.is_synchronized)
    return 0.0f;
  
  // Sync percentage
  if (current_chain_height == 0)
    return 0.0f;
  
  float sync_pct = static_cast<float>(entry.last_sync_height) / current_chain_height;
  
  // Score: 1.0 at 100%, drops off below threshold
  if (sync_pct >= 0.99f)
    return 1.0f;
  
  return sync_pct;
}

float ParticipationEngine::calculate_responsiveness_score(
  const crypto::hash& node_id)
{
  // Based on average response time to PoA challenges
  // Perfect: < 500ms = 1.0
  // Good: 500-1000ms = 0.8
  // OK: 1000-1500ms = 0.6
  // Slow: 1500-2000ms = 0.4
  // Timeout: > 2000ms = 0.0
  
  uint32_t avg_time = 1000;  // Get from database
  
  if (avg_time <= 500)
    return 1.0f;
  else if (avg_time <= 1000)
    return 0.8f;
  else if (avg_time <= 1500)
    return 0.6f;
  else if (avg_time <= 2000)
    return 0.4f;
  else
    return 0.0f;
}

float ParticipationEngine::calculate_activity_score(
  const crypto::hash& node_id,
  uint64_t period_end_height)
{
  // Track: transaction relays, peer updates, etc
  // Get from activity counters stored in database
  
  // Simple: 0.5 if any activity, 0.0 if none
  // More sophisticated: weighted by number of activities
  
  return 0.5f;  // Placeholder
}

} // namespace cryptonote
```

---

## FASE 4: BLOCKCHAIN INTEGRATION (Week 2-3)

### 4.1 Modificare blockchain.cpp

Seguire i dettagli in `blockchain_modifications.cpp` fornito.

**Punti chiave**:
1. Aggiungere member variables per participation engine
2. Modificare `validate_miner_transaction()` per split reward
3. Aggiungere `process_participation_period()` function
4. Chiamare `process_participation_period()` in `add_new_block()`

### 4.2 Aggiungere parametri RPC

```cpp
// In src/daemon/daemon.cpp o dove si inizializzano le opzioni:

const command_line::arg_descriptor<bool> daemon_args::arg_enable_participation = {
  "enable-participation",
  "Enable full node participation incentive system",
  "",
  false
};

const command_line::arg_descriptor<float> daemon_args::arg_pool_percentage = {
  "pool-percentage",
  "Percentage of block reward for incentive pool (0.0-1.0)",
  "",
  0.03f
};

const command_line::arg_descriptor<std::string> daemon_args::arg_participation_db = {
  "participation-db",
  "Path to participation system database",
  "",
  tools::get_default_data_dir() + "/participation"
};
```

---

## FASE 5: RPC ENDPOINTS (Week 3)

### 5.1 Aggiungere commands in core_rpc_server.cpp

```cpp
// Add to core_rpc_server.cpp

bool core_rpc_server::on_get_participation_score(
  const COMMAND_RPC_GET_PARTICIPATION_SCORE::request& req,
  COMMAND_RPC_GET_PARTICIPATION_SCORE::response& res,
  const connection_context& cctx)
{
  if (m_core.get_blockchain_core().get_participation_engine() == nullptr)
  {
    res.status = "PARTICIPATION_DISABLED";
    return false;
  }
  
  auto engine = m_core.get_blockchain_core().get_participation_engine();
  auto registry = m_core.get_blockchain_core().get_node_registry();
  
  // Find node by ID or wallet address
  crypto::hash node_id;
  NodeRegistryEntry entry;
  
  if (!req.node_id.empty())
  {
    epee::string_tools::hex_to_pod(req.node_id, node_id);
    if (!registry->get_node_by_id(node_id, entry))
    {
      res.status = "NODE_NOT_FOUND";
      return false;
    }
  }
  else if (!req.wallet_address.empty())
  {
    if (!registry->get_node_by_wallet(req.wallet_address, entry))
    {
      res.status = "WALLET_NOT_FOUND";
      return false;
    }
    node_id = entry.node_id;
  }
  else
  {
    res.status = "MISSING_PARAMETERS";
    return false;
  }
  
  // Calculate score
  uint64_t current_height = m_core.get_current_blockchain_height();
  auto snapshot = engine->calculate_node_score(node_id, current_height);
  
  res.node_id = epee::string_tools::pod_to_hex(node_id);
  res.wallet_address = entry.wallet_address;
  res.current_score = snapshot.total_score;
  res.uptime_percentage = snapshot.uptime_score;
  res.sync_percentage = snapshot.sync_score;
  res.responsiveness = snapshot.responsiveness_score;
  res.activity_score = snapshot.activity_score;
  res.status = "OK";
  
  return true;
}

// Add to URI_MAP2 in core_rpc_server.h:
MAP_JON_RPC_WE("get_participation_score", on_get_participation_score, COMMAND_RPC_GET_PARTICIPATION_SCORE)
```

---

## FASE 6: P2P PROTOCOL (Week 4-5)

### 6.1 Aggiungere protocol messages

```cpp
// In p2p_protocol_defs.h, aggiungere:

struct node_announcement {
  static const int ID = P2P_COMMAND_NODE_ANNOUNCEMENT;
  
  DEFINE_LIMITS();
  
  crypto::hash node_id;
  crypto::public_key wallet_pubkey;
  crypto::public_key node_pubkey;
  uint16_t port;
  uint64_t timestamp;
  crypto::signature signature;
  
  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(node_id)
    KV_SERIALIZE(wallet_pubkey)
    KV_SERIALIZE(node_pubkey)
    KV_SERIALIZE(port)
    KV_SERIALIZE(timestamp)
    KV_SERIALIZE(signature)
  END_KV_SERIALIZE_MAP()
};

struct availability_challenge {
  static const int ID = P2P_COMMAND_AVAILABILITY_CHALLENGE;
  
  DEFINE_LIMITS();
  
  crypto::hash challenge_id;
  crypto::hash node_id;
  uint8_t challenge_type;
  uint64_t parameter;
  uint64_t timestamp;
  
  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(challenge_id)
    KV_SERIALIZE(node_id)
    KV_SERIALIZE(challenge_type)
    KV_SERIALIZE(parameter)
    KV_SERIALIZE(timestamp)
  END_KV_SERIALIZE_MAP()
};
```

### 6.2 Aggiungere handler in net_node.inl

```cpp
// In net_node.inl, aggiungere:

HANDLE_INVOKE_T2(node_announcement) {
  // Process node announcement
  // Add to registry
  
  auto registry = m_core->get_node_registry();
  // ... process ...
  
  return 1;
}

HANDLE_INVOKE_T2(availability_challenge) {
  // Respond to challenge
  
  auto proof = m_core->get_availability_proof();
  // ... generate response ...
  
  return 1;
}
```

---

## FASE 7: WALLET INTEGRATION (Week 5-6)

### 7.1 Estendere wallet RPC

```cpp
// In wallet_rpc_server_commands_defs.h:

struct COMMAND_RPC_GET_NODE_INFO {
  // Similar to participation RPC endpoints
};

// In wallet_rpc_server.cpp:
bool wallet_rpc_server::on_get_node_info(...)
{
  // Query participation data from daemon
  // Display to wallet user
}
```

---

## FASE 8: TESTING & HARDENING (Weeks 7-10)

### 8.1 Unit Tests

```cpp
// tests/unit_tests/participation/test_node_registry.cpp

#include <gtest/gtest.h>
#include "participation/node_registry.h"

class NodeRegistryTest : public ::testing::Test {
protected:
  NodeRegistry registry{"./test_db"};
};

TEST_F(NodeRegistryTest, RegisterNode) {
  // Test registration
  EXPECT_TRUE(registry.register_node(...));
}

TEST_F(NodeRegistryTest, GetNode) {
  // Test retrieval
}

// ... more tests ...
```

### 8.2 Integration Tests

```bash
# tests/integration/participation_test.sh

# Start test network with participation enabled
# Register nodes
# Trigger distributions
# Verify rewards were distributed correctly
# Check badge awards
```

### 8.3 Performance Testing

```cpp
// Benchmark participation operations
// Test with 1000+ nodes
// Measure CPU/memory impact
// Verify no blockchain slowdown
```

---

## FASE 9: DEPLOYMENT CHECKLIST

### Pre-Launch

- [ ] Code review complete
- [ ] Security audit passed
- [ ] All tests passing
- [ ] Documentation complete
- [ ] Systemd service tested
- [ ] Database migration script ready
- [ ] Rollback plan documented

### Launch

- [ ] Announce feature
- [ ] Testnet launch (1 week)
- [ ] Fix issues from testnet
- [ ] Mainnet soft-fork at block X
- [ ] Monitor closely first 72 hours
- [ ] Collect node feedback
- [ ] Optimize based on real data

### Post-Launch

- [ ] Monitor system stability
- [ ] Collect participation metrics
- [ ] Adjust parameters if needed
- [ ] Plan Phase 2 improvements

---

## FILE SUMMARY

**Created Files**:
1. `/tmp/mevacoin_incentives_analysis.md` - Complete analysis
2. `/tmp/node_registry.h` - Node registry header
3. `/tmp/participation_engine.h` - Participation engine header
4. `/tmp/availability_proof.h` - PoA system header
5. `/tmp/badge_system.h` - Badge system header
6. `/tmp/reward_distributor.h` - Reward distribution header
7. `/tmp/participation_rpc_commands.h` - RPC command definitions
8. `/tmp/mevacoind_participation.service` - Systemd service file
9. `/tmp/blockchain_modifications.cpp` - Blockchain integration guide
10. `/tmp/implementation_guide.md` - This guide

**Next Steps**:
1. Implement .cpp files for each header
2. Integrate with blockchain.cpp as described
3. Add RPC handlers
4. Add P2P protocol messages
5. Write tests
6. Security audit

