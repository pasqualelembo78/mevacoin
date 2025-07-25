// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "inode.h"
#include "synchronization_state.h"
#include "iblockchain_synchronizer.h"
#include "iobservable_impl.h"
#include "istream_serializable.h"

#include <condition_variable>
#include <mutex>
#include <atomic>
#include <future>

#include "logging/logger_ref.h"

namespace mevacoin
{

    class BlockchainSynchronizer : public IObservableImpl<IBlockchainSynchronizerObserver, IBlockchainSynchronizer>,
                                   public INodeObserver
    {
    public:
        BlockchainSynchronizer(INode &node, std::shared_ptr<logging::ILogger> logger, const crypto::Hash &genesisBlockHash);
        ~BlockchainSynchronizer();

        // IBlockchainSynchronizer
        virtual void addConsumer(IBlockchainConsumer *consumer) override;
        virtual bool removeConsumer(IBlockchainConsumer *consumer) override;
        virtual IStreamSerializable *getConsumerState(IBlockchainConsumer *consumer) const override;
        virtual std::vector<crypto::Hash> getConsumerKnownBlocks(IBlockchainConsumer &consumer) const override;

        virtual std::future<std::error_code> addUnconfirmedTransaction(const ITransactionReader &transaction) override;
        virtual std::future<void> removeUnconfirmedTransaction(const crypto::Hash &transactionHash) override;

        virtual void start() override;
        virtual void stop() override;

        // IStreamSerializable
        virtual void save(std::ostream &os) override;
        virtual void load(std::istream &in) override;

        // INodeObserver
        virtual void localBlockchainUpdated(uint32_t height) override;
        virtual void lastKnownBlockHeightUpdated(uint32_t height) override;
        virtual void poolChanged() override;

    private:
        struct GetBlocksResponse
        {
            uint32_t startHeight;
            std::vector<BlockShortEntry> newBlocks;
        };

        struct GetBlocksRequest
        {
            GetBlocksRequest()
            {
                syncStart.timestamp = 0;
                syncStart.height = 0;
            }
            SynchronizationStart syncStart;
            std::vector<crypto::Hash> knownBlocks;
        };

        struct GetPoolResponse
        {
            bool isLastKnownBlockActual;
            std::vector<std::unique_ptr<ITransactionReader>> newTxs;
            std::vector<crypto::Hash> deletedTxIds;
        };

        struct GetPoolRequest
        {
            std::vector<crypto::Hash> knownTxIds;
            crypto::Hash lastKnownBlock;
        };

        enum class State
        {                       // prioritized finite states
            idle = 0,           // DO
            poolSync = 1,       // NOT
            blockchainSync = 2, // REORDER
            deleteOldTxs = 3,   //!!!
            stopped = 4         //!!!
        };

        enum class UpdateConsumersResult
        {
            nothingChanged = 0,
            addedNewBlocks = 1,
            errorOccurred = 2
        };

        // void startSync();
        void removeOutdatedTransactions();
        void startPoolSync();
        void startBlockchainSync();

        void processBlocks(GetBlocksResponse &response);
        UpdateConsumersResult updateConsumers(const BlockchainInterval &interval, const std::vector<CompleteBlock> &blocks);
        std::error_code processPoolTxs(GetPoolResponse &response);
        std::error_code getPoolSymmetricDifferenceSync(GetPoolRequest &&request, GetPoolResponse &response);
        std::error_code doAddUnconfirmedTransaction(const ITransactionReader &transaction);
        void doRemoveUnconfirmedTransaction(const crypto::Hash &transactionHash);

        /// second parameter is used only in case of errors returned into callback from INode, such as aborted or connection lost
        bool setFutureState(State s);
        bool setFutureStateIf(State s, std::function<bool(void)> &&pred);

        void actualizeFutureState();
        bool checkIfShouldStop() const;
        bool checkIfStopped() const;

        void workingProcedure();

        GetBlocksRequest getCommonHistory();
        void getPoolUnionAndIntersection(std::unordered_set<crypto::Hash> &poolUnion, std::unordered_set<crypto::Hash> &poolIntersection) const;
        SynchronizationState *getConsumerSynchronizationState(IBlockchainConsumer *consumer) const;

        typedef std::map<IBlockchainConsumer *, std::shared_ptr<SynchronizationState>> ConsumersMap;

        mutable logging::LoggerRef m_logger;
        ConsumersMap m_consumers;
        INode &m_node;
        const crypto::Hash m_genesisBlockHash;

        crypto::Hash lastBlockId;

        State m_currentState;
        State m_futureState;
        std::unique_ptr<std::thread> workingThread;
        std::list<std::pair<const ITransactionReader *, std::promise<std::error_code>>> m_addTransactionTasks;
        std::list<std::pair<const crypto::Hash *, std::promise<void>>> m_removeTransactionTasks;

        mutable std::mutex m_consumersMutex;
        mutable std::mutex m_stateMutex;
        std::condition_variable m_hasWork;

        bool wasStarted = false;
    };

}
