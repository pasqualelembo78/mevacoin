// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "serialization/iserializer.h"

namespace mevacoin
{

    struct CoreStatistics
    {
        uint64_t transactionPoolSize;
        uint64_t blockchainHeight;
        uint64_t miningSpeed;
        uint64_t alternativeBlockCount;
        std::string topBlockHashString;

        void serialize(ISerializer &s)
        {
            s(transactionPoolSize, "tx_pool_size");
            s(blockchainHeight, "blockchain_height");
            s(miningSpeed, "mining_speed");
            s(alternativeBlockCount, "alternative_blocks");
            s(topBlockHashString, "top_block_id_str");
        }
    };

}
