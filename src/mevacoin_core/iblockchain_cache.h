// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <unordered_map>

#include <vector>

#include <mevacoin.h>

#include "mevacoin_core/cached_block.h"
#include "mevacoin_core/cached_transaction.h"
#include "mevacoin_core/transaction_validatior_state.h"
#include "common/array_view.h"

namespace mevacoin
{

    class ISerializer;
    struct TransactionValidatorState;

    enum class ExtractOutputKeysResult
    {
        SUCCESS,
        INVALID_GLOBAL_INDEX,
        OUTPUT_LOCKED
    };

    union PackedOutIndex
    {
        struct
        {
            uint32_t blockIndex;
            uint16_t transactionIndex;
            uint16_t outputIndex;
        };

        uint64_t packedValue;
    };

    const uint32_t INVALID_BLOCK_INDEX = std::numeric_limits<uint32_t>::max();

    struct PushedBlockInfo
    {
        RawBlock rawBlock;
        TransactionValidatorState validatorState;
        size_t blockSize;
        uint64_t generatedCoins;
        uint64_t blockDifficulty;
    };

    class UseGenesis
    {
    public:
        explicit UseGenesis(bool u) : use(u) {}
        // emulate boolean flag
        operator bool()
        {
            return use;
        }

    private:
        bool use = false;
    };

    struct CachedBlockInfo;
    struct CachedTransactionInfo;
    class ITransactionPool;

    class IBlockchainCache
    {
    public:
        using BlockIndex = uint32_t;
        using GlobalOutputIndex = uint32_t;
        using Amount = uint64_t;

        virtual ~IBlockchainCache() {}

        virtual RawBlock getBlockByIndex(uint32_t index) const = 0;
        virtual BinaryArray getRawTransaction(uint32_t blockIndex, uint32_t transactionIndex) const = 0;
        virtual std::unique_ptr<IBlockchainCache> split(uint32_t splitBlockIndex) = 0;
        virtual void pushBlock(
            const CachedBlock &cachedBlock,
            const std::vector<CachedTransaction> &cachedTransactions,
            const TransactionValidatorState &validatorState,
            size_t blockSize,
            uint64_t generatedCoins,
            uint64_t blockDifficulty,
            RawBlock &&rawBlock) = 0;
        virtual PushedBlockInfo getPushedBlockInfo(uint32_t index) const = 0;
        virtual bool checkIfSpent(const crypto::KeyImage &keyImage, uint32_t blockIndex) const = 0;
        virtual bool checkIfSpent(const crypto::KeyImage &keyImage) const = 0;

        virtual bool isTransactionSpendTimeUnlocked(uint64_t unlockTime) const = 0;
        virtual bool isTransactionSpendTimeUnlocked(uint64_t unlockTime, uint32_t blockIndex) const = 0;

        virtual ExtractOutputKeysResult extractKeyOutputKeys(uint64_t amount, common::ArrayView<uint32_t> globalIndexes, std::vector<crypto::PublicKey> &publicKeys) const = 0;
        virtual ExtractOutputKeysResult extractKeyOutputKeys(uint64_t amount, uint32_t blockIndex, common::ArrayView<uint32_t> globalIndexes, std::vector<crypto::PublicKey> &publicKeys) const = 0;

        virtual ExtractOutputKeysResult extractKeyOtputIndexes(uint64_t amount, common::ArrayView<uint32_t> globalIndexes, std::vector<PackedOutIndex> &outIndexes) const = 0;
        virtual ExtractOutputKeysResult extractKeyOtputReferences(uint64_t amount, common::ArrayView<uint32_t> globalIndexes, std::vector<std::pair<crypto::Hash, size_t>> &outputReferences) const = 0;
        // TODO: get rid of pred in this method. return vector of KeyOutputInfo structures
        virtual ExtractOutputKeysResult extractKeyOutputs(
            uint64_t amount, uint32_t blockIndex, common::ArrayView<uint32_t> globalIndexes,
            std::function<ExtractOutputKeysResult(const CachedTransactionInfo &info, PackedOutIndex index, uint32_t globalIndex)> pred) const = 0;

        virtual uint32_t getTopBlockIndex() const = 0;
        virtual const crypto::Hash &getTopBlockHash() const = 0;
        virtual uint32_t getBlockCount() const = 0;
        virtual bool hasBlock(const crypto::Hash &blockHash) const = 0;
        virtual uint32_t getBlockIndex(const crypto::Hash &blockHash) const = 0;

        virtual bool hasTransaction(const crypto::Hash &transactionHash) const = 0;

        virtual std::vector<uint64_t> getLastTimestamps(size_t count) const = 0;
        virtual std::vector<uint64_t> getLastTimestamps(size_t count, uint32_t blockIndex, UseGenesis) const = 0;

        virtual std::vector<uint64_t> getLastBlocksSizes(size_t count) const = 0;
        virtual std::vector<uint64_t> getLastBlocksSizes(size_t count, uint32_t blockIndex, UseGenesis) const = 0;

        virtual std::vector<uint64_t> getLastCumulativeDifficulties(size_t count, uint32_t blockIndex, UseGenesis) const = 0;
        virtual std::vector<uint64_t> getLastCumulativeDifficulties(size_t count) const = 0;

        virtual uint64_t getDifficultyForNextBlock() const = 0;
        virtual uint64_t getDifficultyForNextBlock(uint32_t blockIndex) const = 0;

        virtual uint64_t getCurrentCumulativeDifficulty() const = 0;
        virtual uint64_t getCurrentCumulativeDifficulty(uint32_t blockIndex) const = 0;

        virtual uint64_t getAlreadyGeneratedCoins() const = 0;
        virtual uint64_t getAlreadyGeneratedCoins(uint32_t blockIndex) const = 0;

        virtual uint64_t getAlreadyGeneratedTransactions(uint32_t blockIndex) const = 0;

        virtual crypto::Hash getBlockHash(uint32_t blockIndex) const = 0;
        virtual std::vector<crypto::Hash> getBlockHashes(uint32_t startIndex, size_t maxCount) const = 0;

        virtual IBlockchainCache *getParent() const = 0;
        virtual void setParent(IBlockchainCache *parent) = 0;
        virtual uint32_t getStartBlockIndex() const = 0;

        virtual size_t getKeyOutputsCountForAmount(uint64_t amount, uint32_t blockIndex) const = 0;

        virtual std::tuple<bool, uint64_t> getBlockHeightForTimestamp(uint64_t timestamp) const = 0;

        virtual uint32_t getTimestampLowerBoundBlockIndex(uint64_t timestamp) const = 0;

        // NOTE: shouldn't be recursive otherwise we'll get quadratic complexity
        virtual void getRawTransactions(const std::vector<crypto::Hash> &transactions,
                                        std::vector<BinaryArray> &foundTransactions, std::vector<crypto::Hash> &missedTransactions) const = 0;
        virtual std::vector<BinaryArray> getRawTransactions(const std::vector<crypto::Hash> &transactions,
                                                            std::vector<crypto::Hash> &missedTransactions) const = 0;
        virtual std::vector<BinaryArray> getRawTransactions(const std::vector<crypto::Hash> &transactions) const = 0;

        // NOTE: not recursive!
        virtual bool getTransactionGlobalIndexes(const crypto::Hash &transactionHash, std::vector<uint32_t> &globalIndexes) const = 0;

        virtual std::unordered_map<crypto::Hash, std::vector<uint64_t>> getGlobalIndexes(
            const std::vector<crypto::Hash> transactionHashes) const = 0;

        virtual size_t getTransactionCount() const = 0;

        virtual uint32_t getBlockIndexContainingTx(const crypto::Hash &transactionHash) const = 0;

        virtual size_t getChildCount() const = 0;
        virtual void addChild(IBlockchainCache *) = 0;
        virtual bool deleteChild(IBlockchainCache *) = 0;

        virtual void save() = 0;
        virtual void load() = 0;

        virtual std::vector<uint64_t> getLastUnits(size_t count, uint32_t blockIndex, UseGenesis use,
                                                   std::function<uint64_t(const CachedBlockInfo &)> pred) const = 0;
        virtual std::vector<crypto::Hash> getTransactionHashes() const = 0;
        virtual std::vector<uint32_t> getRandomOutsByAmount(uint64_t amount, size_t count, uint32_t blockIndex) const = 0;

        virtual std::vector<crypto::Hash> getTransactionHashesByPaymentId(const crypto::Hash &paymentId) const = 0;
        virtual std::vector<crypto::Hash> getBlockHashesByTimestamps(uint64_t timestampBegin, size_t secondsCount) const = 0;

        virtual std::vector<RawBlock> getBlocksByHeight(
            const uint64_t startHeight,
            uint64_t endHeight) const = 0;
    };

}
