// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <cstdint>
#include <future>
#include <system_error>
#include <unordered_set>

#include "crypto/crypto.h"
#include "mevacoin_core/mevacoin_basic.h"

#include "iobservable.h"
#include "istream_serializable.h"
#include "itransfers_synchronizer.h"

namespace mevacoin
{

    struct CompleteBlock;

    class IBlockchainSynchronizerObserver
    {
    public:
        virtual void synchronizationProgressUpdated(uint32_t processedBlockCount, uint32_t totalBlockCount) {}
        virtual void synchronizationCompleted(std::error_code result) {}
    };

    class IBlockchainConsumerObserver;

    class IBlockchainConsumer : public IObservable<IBlockchainConsumerObserver>
    {
    public:
        virtual ~IBlockchainConsumer() {}
        virtual SynchronizationStart getSyncStart() = 0;
        virtual const std::unordered_set<crypto::Hash> &getKnownPoolTxIds() const = 0;
        virtual void onBlockchainDetach(uint32_t height) = 0;
        virtual uint32_t onNewBlocks(const CompleteBlock *blocks, uint32_t startHeight, uint32_t count) = 0;
        virtual std::error_code onPoolUpdated(const std::vector<std::unique_ptr<ITransactionReader>> &addedTransactions, const std::vector<crypto::Hash> &deletedTransactions) = 0;

        virtual std::error_code addUnconfirmedTransaction(const ITransactionReader &transaction) = 0;
        virtual void removeUnconfirmedTransaction(const crypto::Hash &transactionHash) = 0;
    };

    class IBlockchainConsumerObserver
    {
    public:
        virtual void onBlocksAdded(IBlockchainConsumer *consumer, const std::vector<crypto::Hash> &blockHashes) {}
        virtual void onBlockchainDetach(IBlockchainConsumer *consumer, uint32_t blockIndex) {}
        virtual void onTransactionDeleteBegin(IBlockchainConsumer *consumer, crypto::Hash transactionHash) {}
        virtual void onTransactionDeleteEnd(IBlockchainConsumer *consumer, crypto::Hash transactionHash) {}
        virtual void onTransactionUpdated(IBlockchainConsumer *consumer, const crypto::Hash &transactionHash, const std::vector<ITransfersContainer *> &containers) {}
    };

    class IBlockchainSynchronizer : public IObservable<IBlockchainSynchronizerObserver>,
                                    public IStreamSerializable
    {
    public:
        virtual void addConsumer(IBlockchainConsumer *consumer) = 0;
        virtual bool removeConsumer(IBlockchainConsumer *consumer) = 0;
        virtual IStreamSerializable *getConsumerState(IBlockchainConsumer *consumer) const = 0;
        virtual std::vector<crypto::Hash> getConsumerKnownBlocks(IBlockchainConsumer &consumer) const = 0;

        virtual std::future<std::error_code> addUnconfirmedTransaction(const ITransactionReader &transaction) = 0;
        virtual std::future<void> removeUnconfirmedTransaction(const crypto::Hash &transactionHash) = 0;

        virtual void start() = 0;
        virtual void stop() = 0;
    };

}
