// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <memory>

namespace mevacoin
{

    class IBlockchainCache;
    class Currency;

    class IBlockchainCacheFactory
    {
    public:
        virtual ~IBlockchainCacheFactory() {}

        virtual std::unique_ptr<IBlockchainCache> createRootBlockchainCache(const Currency &currency) = 0;
        virtual std::unique_ptr<IBlockchainCache> createBlockchainCache(const Currency &currency, IBlockchainCache *parent, uint32_t startIndex = 0) = 0;
    };

} // namespace mevacoin
