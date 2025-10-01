// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#pragma once

#include <cstdint>
#include <vector>

#include "crypto/hash.h"

namespace mevacoin
{
    bool check_hash(const crypto::Hash &hash, uint64_t difficulty);
}
