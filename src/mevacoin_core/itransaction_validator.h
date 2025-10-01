// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#pragma once

#include "mevacoin_core/mevacoin_basic.h"

namespace mevacoin
{

    struct BlockInfo
    {
        uint32_t height;
        crypto::Hash id;

        BlockInfo()
        {
            clear();
        }

        void clear()
        {
            height = 0;
            id = mevacoin::NULL_HASH;
        }

        bool empty() const
        {
            return id == mevacoin::NULL_HASH;
        }
    };

    class ITransactionValidator
    {
    public:
        virtual ~ITransactionValidator() {}

        virtual bool checkTransactionInputs(const mevacoin::Transaction &tx, BlockInfo &maxUsedBlock) = 0;
        virtual bool checkTransactionInputs(const mevacoin::Transaction &tx, BlockInfo &maxUsedBlock, BlockInfo &lastFailed) = 0;
        virtual bool haveSpentKeyImages(const mevacoin::Transaction &tx) = 0;
        virtual bool checkTransactionSize(size_t blobSize) = 0;
    };

}
