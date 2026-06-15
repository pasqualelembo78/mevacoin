# Pool Distribution Validation (tag 0xAA) in check_mevatrust_coinbase

## Goal
Validare il blob 0xAA (pool distribution FROST) in `check_mevatrust_coinbase()` per il consensus.

## Files da modificare

### 1. `src/cryptonote_core/mevatrust/mevatrust_manager.h`
Aggiungere getter pubblico per `m_period_length`:
```cpp
uint32_t period_length() const { return m_period_length; }
```
Dopo `void set_period_length(uint32_t blocks);` (linea ~127)

### 2. `src/cryptonote_core/mevatrust/mevatrust_coinbase_validator.cpp`
Aggiungere includes all'inizio:
```cpp
#include "mevatrust/mevatrust_tx_parser.h"
#include "mevatrust/pool_distribution.h"
```
Dopo l'inclusione di `mevatrust_manager.h` (linea ~12)

Modificare `check_mevatrust_coinbase()`: dopo il blocco `// 6. If no node rewards expected...` (linea ~95), sostituire con:

```cpp
  // 6. Parse pool distribution tag 0xAA from tx_extra
  tx_extra_mevatrust_pool_distribution dist;
  bool has_dist = mevatrust::parse_mevatrust_pool_distribution_from_tx(miner_tx, dist);

  // If no node rewards expected (distribution period not active)
  if (expected.empty()) {
    // Tag 0xAA must NOT be present when no distribution is expected
    if (has_dist) {
      error_msg = "Unexpected pool distribution tag 0xAA at height " + std::to_string(height);
      MERROR(error_msg);
      return false;
    }
    // Miner gets 97% - pool gets 3%
    if (expected_miner_reward + expected_pool_amount != total_reward) {
      error_msg = "Miner + pool reward mismatch total_reward";
      MERROR(error_msg);
      return false;
    }
    MDEBUG("Coinbase OK: pool=" << expected_pool_amount << " miner=" << expected_miner_reward << " h=" << height);
    return true;
  }

  // Node rewards expected => tag 0xAA MUST be present
  if (!has_dist) {
    error_msg = "Missing pool distribution tag 0xAA at height " + std::to_string(height);
    MERROR(error_msg);
    return false;
  }

  // Validate pool distribution via existing validator
  auto* pm = mevatrust::get_manager();
  if (!pm) {
    error_msg = "MevaTrust manager not initialized";
    MERROR(error_msg);
    return false;
  }

  uint64_t pool_balance = pm->compute_pool_balance_from_chain();
  uint32_t period = static_cast<uint32_t>(height / pm->period_length());

  mevatrust::ProposerState proposers;
  for (size_t i = 0; i < frost::FROST_N; ++i) {
    proposers.pubkeys[i] = frost::CONSENSUS_PROPOSER_PUBKEYS[i];
  }

  if (!mevatrust::validate_pool_distribution(dist, height, period, pool_balance, proposers, error_msg)) {
    MERROR("Pool distribution validation failed: " << error_msg);
    return false;
  }
```

### 3. `src/cryptonote_core/mevatrust/mevatrust_coinbase_validator.h`
Nessuna modifica necessaria.

## Verifica
- Build: `make -j$(nproc)`
- Test: `./build/Linux/mevacoin/release/tests/unit_tests/unit_tests --gtest_filter="*MevaTrust*"`
```

