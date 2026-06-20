// Copyright (c) 2024, The Mevacoin Project
// mevatrust_types.h — Shared types for the MevaTrust system
#pragma once
#include <string>
#include <cstdint>
#include "cryptonote_basic/cryptonote_basic.h"

namespace cryptonote {

struct NodeCoinbaseReward {
  account_public_address address;
  uint64_t               amount;
  std::string            node_id;
};

} // namespace cryptonote
