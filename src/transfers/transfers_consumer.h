// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018, The BBSCoin Developers
// Copyright (c) 2018, The Karbo Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "iblockchain_synchronizer.h"
#include "itransfers_synchronizer.h"
#include "transfers_subscription.h"
#include "type_helpers.h"

#include "crypto/crypto.h"
#include "logging/logger_ref.h"

#include "iobservable_impl.h"

#include <unordered_set>

namespace mevacoin
{

    class INode;

    class TransfersConsumer : public IObservableImpl<IBlockchainConsumerObserver, IBlockchainConsumer>
    {
    public:
        TransfersConsumer(const mevacoin::Currency &currency, INode &node, std::shared_ptr<logging::ILogger> logger, const crypto::SecretKey &viewSecret);

        ITransfersSubscription &addSubscription(const AccountSubscription &subscription);
        // returns true if no subscribers left
        bool removeSubscription(const AccountPublicAddress &address);
        ITransfersSubscription *getSubscription(const AccountPublicAddress &acc);
        void getSubscriptions(std::vector<AccountPublicAddress> &subscriptions);

        void initTransactionPool(const std::unordered_set<crypto::Hash> &uncommitedTransactions);
        void addPublicKeysSeen(const crypto::Hash &transactionHash, const crypto::PublicKey &outputKey);

        // IBlockchainConsumer
        virtual SynchronizationStart getSyncStart() override;
        virtual void onBlockchainDetach(uint32_t height) override;
        virtual uint32_t onNewBlocks(const CompleteBlock *blocks, uint32_t startHeight, uint32_t count) override;
        virtual std::error_code onPoolUpdated(const std::vector<std::unique_ptr<ITransactionReader>> &addedTransactions, const std::vector<crypto::Hash> &deletedTransactions) override;
        virtual const std::unordered_set<crypto::Hash> &getKnownPoolTxIds() const override;

        virtual std::error_code addUnconfirmedTransaction(const ITransactionReader &transaction) override;
        virtual void removeUnconfirmedTransaction(const crypto::Hash &transactionHash) override;

    private:
        template <typename F>
        void forEachSubscription(F action)
        {
            for (const auto &kv : m_subscriptions)
            {
                action(*kv.second);
            }
        }

        struct PreprocessInfo
        {
            std::unordered_map<crypto::PublicKey, std::vector<TransactionOutputInformationIn>> outputs;
            std::vector<uint32_t> globalIdxs;
        };

        std::error_code preprocessOutputs(const TransactionBlockInfo &blockInfo, const ITransactionReader &tx, PreprocessInfo &info);
        std::error_code processTransaction(const TransactionBlockInfo &blockInfo, const ITransactionReader &tx);
        void processTransaction(const TransactionBlockInfo &blockInfo, const ITransactionReader &tx, const PreprocessInfo &info);
        void processOutputs(const TransactionBlockInfo &blockInfo, TransfersSubscription &sub, const ITransactionReader &tx,
                            const std::vector<TransactionOutputInformationIn> &outputs, const std::vector<uint32_t> &globalIdxs, bool &contains, bool &updated);

        std::error_code getGlobalIndices(const crypto::Hash &transactionHash, std::vector<uint32_t> &outsGlobalIndices);

        void updateSyncStart();

        SynchronizationStart m_syncStart;
        const crypto::SecretKey m_viewSecret;
        // map { spend public key -> subscription }
        std::unordered_map<crypto::PublicKey, std::unique_ptr<TransfersSubscription>> m_subscriptions;
        std::unordered_set<crypto::PublicKey> m_spendKeys;
        std::unordered_set<crypto::Hash> m_poolTxs;

        INode &m_node;
        const mevacoin::Currency &m_currency;
        logging::LoggerRef m_logger;
    };

}
