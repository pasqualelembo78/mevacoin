// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "common/string_view.h"
#include "currency.h"
#include "iblockchain_cache.h"
#include "mevacoin_core/upgrade_manager.h"
#include <idatabase.h>
#include <mevacoin_core/blockchain_read_batch.h>
#include <mevacoin_core/blockchain_write_batch.h>
#include <mevacoin_core/database_cache_data.h>
#include <mevacoin_core/iblockchain_cache_factory.h>

namespace mevacoin
{

    /*
     * Implementation of IBlockchainCache that uses database to store internal indexes.
     * Current implementation is designed to always be the root of blockchain, ie
     * start index is always zero, parent is always nullptr, no methods
     * do recursive calls to parent.
     */
    class DatabaseBlockchainCache : public IBlockchainCache
    {
    public:
        using BlockIndex = uint32_t;
        using GlobalOutputIndex = uint32_t;
        using Amount = uint64_t;

        /*
         * Constructs new DatabaseBlockchainCache object. Currnetly, only factories that produce
         * BlockchainCache objects as children are supported.
         */
        DatabaseBlockchainCache(const Currency &currency, IDataBase &dataBase,
                                IBlockchainCacheFactory &blockchainCacheFactory, std::shared_ptr<logging::ILogger> logger);

        static bool checkDBSchemeVersion(IDataBase &dataBase, std::shared_ptr<logging::ILogger> logger);

        /*
         * This methods splits cache, upper part (ie blocks with indexes larger than splitBlockIndex)
         * is copied to new BlockchainCache. Unfortunately, implementation requires return value to be of
         * BlockchainCache type.
         */
        std::unique_ptr<IBlockchainCache> split(uint32_t splitBlockIndex) override;
        void pushBlock(const CachedBlock &cachedBlock, const std::vector<CachedTransaction> &cachedTransactions,
                       const TransactionValidatorState &validatorState, size_t blockSize, uint64_t generatedCoins,
                       uint64_t blockDifficulty, RawBlock &&rawBlock) override;
        virtual PushedBlockInfo getPushedBlockInfo(uint32_t index) const override;
        bool checkIfSpent(const crypto::KeyImage &keyImage, uint32_t blockIndex) const override;
        bool checkIfSpent(const crypto::KeyImage &keyImage) const override;

        bool isTransactionSpendTimeUnlocked(uint64_t unlockTime) const override;
        bool isTransactionSpendTimeUnlocked(uint64_t unlockTime, uint32_t blockIndex) const override;

        ExtractOutputKeysResult extractKeyOutputKeys(uint64_t amount, common::ArrayView<uint32_t> globalIndexes,
                                                     std::vector<crypto::PublicKey> &publicKeys) const override;
        ExtractOutputKeysResult extractKeyOutputKeys(uint64_t amount, uint32_t blockIndex,
                                                     common::ArrayView<uint32_t> globalIndexes,
                                                     std::vector<crypto::PublicKey> &publicKeys) const override;

        ExtractOutputKeysResult extractKeyOtputIndexes(uint64_t amount, common::ArrayView<uint32_t> globalIndexes,
                                                       std::vector<PackedOutIndex> &outIndexes) const override;
        ExtractOutputKeysResult
        extractKeyOtputReferences(uint64_t amount, common::ArrayView<uint32_t> globalIndexes,
                                  std::vector<std::pair<crypto::Hash, size_t>> &outputReferences) const override;

        uint32_t getTopBlockIndex() const override;
        const crypto::Hash &getTopBlockHash() const override;
        uint32_t getBlockCount() const override;
        bool hasBlock(const crypto::Hash &blockHash) const override;
        uint32_t getBlockIndex(const crypto::Hash &blockHash) const override;

        bool hasTransaction(const crypto::Hash &transactionHash) const override;

        std::vector<uint64_t> getLastTimestamps(size_t count) const override;
        std::vector<uint64_t> getLastTimestamps(size_t count, uint32_t blockIndex, UseGenesis) const override;

        std::vector<uint64_t> getLastBlocksSizes(size_t count) const override;
        std::vector<uint64_t> getLastBlocksSizes(size_t count, uint32_t blockIndex, UseGenesis) const override;

        std::vector<uint64_t> getLastCumulativeDifficulties(size_t count, uint32_t blockIndex, UseGenesis) const override;
        std::vector<uint64_t> getLastCumulativeDifficulties(size_t count) const override;

        uint64_t getDifficultyForNextBlock() const override;
        uint64_t getDifficultyForNextBlock(uint32_t blockIndex) const override;

        virtual uint64_t getCurrentCumulativeDifficulty() const override;
        virtual uint64_t getCurrentCumulativeDifficulty(uint32_t blockIndex) const override;

        uint64_t getAlreadyGeneratedCoins() const override;
        uint64_t getAlreadyGeneratedCoins(uint32_t blockIndex) const override;
        uint64_t getAlreadyGeneratedTransactions(uint32_t blockIndex) const override;
        std::vector<uint64_t> getLastUnits(size_t count, uint32_t blockIndex, UseGenesis use,
                                           std::function<uint64_t(const CachedBlockInfo &)> pred) const override;

        crypto::Hash getBlockHash(uint32_t blockIndex) const override;
        virtual std::vector<crypto::Hash> getBlockHashes(uint32_t startIndex, size_t maxCount) const override;

        /*
         * This method always returns zero
         */
        virtual uint32_t getStartBlockIndex() const override;

        virtual size_t getKeyOutputsCountForAmount(uint64_t amount, uint32_t blockIndex) const override;

        std::tuple<bool, uint64_t> getBlockHeightForTimestamp(uint64_t timestamp) const override;

        virtual uint32_t getTimestampLowerBoundBlockIndex(uint64_t timestamp) const override;

        virtual std::unordered_map<crypto::Hash, std::vector<uint64_t>> getGlobalIndexes(
            const std::vector<crypto::Hash> transactionHashes) const override;

        virtual bool getTransactionGlobalIndexes(const crypto::Hash &transactionHash,
                                                 std::vector<uint32_t> &globalIndexes) const override;
        virtual size_t getTransactionCount() const override;
        virtual uint32_t getBlockIndexContainingTx(const crypto::Hash &transactionHash) const override;

        virtual size_t getChildCount() const override;

        /*
         * This method always returns nullptr
         */
        virtual IBlockchainCache *getParent() const override;
        /*
         * This method does nothing, is here only to support full interface
         */
        virtual void setParent(IBlockchainCache *ptr) override;
        virtual void addChild(IBlockchainCache *ptr) override;
        virtual bool deleteChild(IBlockchainCache *ptr) override;

        virtual void save() override;
        virtual void load() override;

        virtual std::vector<BinaryArray> getRawTransactions(const std::vector<crypto::Hash> &transactions,
                                                            std::vector<crypto::Hash> &missedTransactions) const override;
        virtual std::vector<BinaryArray> getRawTransactions(const std::vector<crypto::Hash> &transactions) const override;
        void getRawTransactions(const std::vector<crypto::Hash> &transactions, std::vector<BinaryArray> &foundTransactions,
                                std::vector<crypto::Hash> &missedTransactions) const override;
        virtual RawBlock getBlockByIndex(uint32_t index) const override;
        virtual BinaryArray getRawTransaction(uint32_t blockIndex, uint32_t transactionIndex) const override;
        virtual std::vector<crypto::Hash> getTransactionHashes() const override;
        virtual std::vector<uint32_t> getRandomOutsByAmount(uint64_t amount, size_t count,
                                                            uint32_t blockIndex) const override;
        virtual ExtractOutputKeysResult
        extractKeyOutputs(uint64_t amount, uint32_t blockIndex, common::ArrayView<uint32_t> globalIndexes,
                          std::function<ExtractOutputKeysResult(const CachedTransactionInfo &info, PackedOutIndex index,
                                                                uint32_t globalIndex)>
                              pred) const override;

        virtual std::vector<crypto::Hash> getTransactionHashesByPaymentId(const crypto::Hash &paymentId) const override;
        virtual std::vector<crypto::Hash> getBlockHashesByTimestamps(uint64_t timestampBegin, size_t secondsCount) const override;

        virtual std::vector<RawBlock> getBlocksByHeight(
            const uint64_t startHeight,
            const uint64_t endHeight) const override;

    private:
        const Currency &currency;
        IDataBase &database;
        IBlockchainCacheFactory &blockchainCacheFactory;
        mutable boost::optional<uint32_t> topBlockIndex;
        mutable boost::optional<crypto::Hash> topBlockHash;
        mutable boost::optional<uint64_t> transactionsCount;
        mutable boost::optional<uint32_t> keyOutputAmountsCount;
        mutable std::unordered_map<Amount, int32_t> keyOutputCountsForAmounts;
        std::vector<IBlockchainCache *> children;
        logging::LoggerRef logger;
        std::deque<CachedBlockInfo> unitsCache;
        const size_t unitsCacheSize = 1000;

        struct ExtendedPushedBlockInfo;
        ExtendedPushedBlockInfo getExtendedPushedBlockInfo(uint32_t blockIndex) const;

        void deleteClosestTimestampBlockIndex(BlockchainWriteBatch &writeBatch, uint32_t splitBlockIndex);
        CachedBlockInfo getCachedBlockInfo(uint32_t index) const;
        BlockchainReadResult readDatabase(BlockchainReadBatch &batch) const;

        void addSpentKeyImage(const crypto::KeyImage &keyImage, uint32_t blockIndex);
        void pushTransaction(const CachedTransaction &cachedTransaction,
                             uint32_t blockIndex,
                             uint16_t transactionBlockIndex,
                             BlockchainWriteBatch &batch);

        uint32_t insertKeyOutputToGlobalIndex(uint64_t amount, PackedOutIndex output); // TODO not implemented. Should it be removed?
        uint32_t updateKeyOutputCount(Amount amount, int32_t diff) const;
        void insertPaymentId(BlockchainWriteBatch &batch, const crypto::Hash &transactionHash, const crypto::Hash &paymentId);
        void insertBlockTimestamp(BlockchainWriteBatch &batch, uint64_t timestamp, const crypto::Hash &blockHash);

        void addGenesisBlock(CachedBlock &&genesisBlock);

        enum class OutputSearchResult : uint8_t
        {
            FOUND,
            NOT_FOUND,
            INVALID_ARGUMENT
        };

        TransactionValidatorState fillOutputsSpentByBlock(uint32_t blockIndex) const;

        crypto::Hash pushBlockToAnotherCache(IBlockchainCache &segment, PushedBlockInfo &&pushedBlockInfo);
        void requestDeleteSpentOutputs(BlockchainWriteBatch &writeBatch, uint32_t splitBlockIndex, const TransactionValidatorState &spentOutputs);
        std::vector<crypto::Hash> requestTransactionHashesFromBlockIndex(uint32_t splitBlockIndex);
        void requestDeleteTransactions(BlockchainWriteBatch &writeBatch, const std::vector<crypto::Hash> &transactionHashes);
        void requestDeletePaymentIds(BlockchainWriteBatch &writeBatch, const std::vector<crypto::Hash> &transactionHashes);
        void requestDeletePaymentId(BlockchainWriteBatch &writeBatch, const crypto::Hash &paymentId, size_t toDelete);
        void requestDeleteKeyOutputs(BlockchainWriteBatch &writeBatch, const std::map<IBlockchainCache::Amount, IBlockchainCache::GlobalOutputIndex> &boundaries);
        void requestDeleteKeyOutputsAmount(BlockchainWriteBatch &writeBatch, IBlockchainCache::Amount amount, IBlockchainCache::GlobalOutputIndex boundary, uint32_t outputsCount);
        void requestRemoveTimestamp(BlockchainWriteBatch &batch, uint64_t timestamp, const crypto::Hash &blockHash);

        uint8_t getBlockMajorVersionForHeight(uint32_t height) const;
        uint64_t getCachedTransactionsCount() const;

        std::vector<CachedBlockInfo> getLastCachedUnits(uint32_t blockIndex, size_t count, UseGenesis useGenesis) const;
        std::vector<CachedBlockInfo> getLastDbUnits(uint32_t blockIndex, size_t count, UseGenesis useGenesis) const;
    };
}
