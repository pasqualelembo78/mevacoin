// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <syst/context_group.h>
#include <syst/dispatcher.h>
#include <syst/event.h>
#include "iwallet.h"
#include "inode.h"
#include "mevacoin_core/currency.h"
#include "payment_service_json_rpc_messages.h"
#undef ERROR // TODO: workaround for windows build. fix it
#include "logging/logger_ref.h"

#include <fstream>
#include <memory>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/hashed_index.hpp>

namespace mevacoin
{
    class IFusionManager;
}

namespace payment_service
{

    struct WalletConfiguration
    {
        std::string walletFile;
        std::string walletPassword;
        bool syncFromZero;
        std::string secretViewKey;
        std::string secretSpendKey;
        std::string mnemonicSeed;
        uint64_t scanHeight;
    };

    void generateNewWallet(const mevacoin::Currency &currency, const WalletConfiguration &conf, std::shared_ptr<logging::ILogger> logger, syst::Dispatcher &dispatcher);

    struct TransactionsInBlockInfoFilter;

    class WalletService
    {
    public:
        WalletService(const mevacoin::Currency &currency, syst::Dispatcher &sys, mevacoin::INode &node, mevacoin::IWallet &wallet,
                      mevacoin::IFusionManager &fusionManager, const WalletConfiguration &conf, std::shared_ptr<logging::ILogger> logger);
        virtual ~WalletService();

        void init();
        void saveWallet();

        std::error_code saveWalletNoThrow();
        std::error_code exportWallet(const std::string &fileName);
        std::error_code resetWallet(const uint64_t scanHeight);
        std::error_code createAddress(const std::string &spendSecretKeyText, const uint64_t scanHeight, const bool newAddress, std::string &address);
        std::error_code createAddressList(const std::vector<std::string> &spendSecretKeysText, const uint64_t scanHeight, const bool newAddress, std::vector<std::string> &addresses);
        std::error_code createAddress(std::string &address);
        std::error_code createTrackingAddress(const std::string &spendPublicKeyText, uint64_t scanHeight, bool newAddress, std::string &address);
        std::error_code deleteAddress(const std::string &address);
        std::error_code getSpendkeys(const std::string &address, std::string &publicSpendKeyText, std::string &secretSpendKeyText);
        std::error_code getBalance(const std::string &address, uint64_t &availableBalance, uint64_t &lockedAmount);
        std::error_code getBalance(uint64_t &availableBalance, uint64_t &lockedAmount);
        std::error_code getBlockHashes(uint32_t firstBlockIndex, uint32_t blockCount, std::vector<std::string> &blockHashes);
        std::error_code getViewKey(std::string &viewSecretKey);
        std::error_code getMnemonicSeed(const std::string &address, std::string &mnemonicSeed);
        std::error_code getTransactionHashes(const std::vector<std::string> &addresses, const std::string &blockHash,
                                             uint32_t blockCount, const std::string &paymentId, std::vector<TransactionHashesInBlockRpcInfo> &transactionHashes);
        std::error_code getTransactionHashes(const std::vector<std::string> &addresses, uint32_t firstBlockIndex,
                                             uint32_t blockCount, const std::string &paymentId, std::vector<TransactionHashesInBlockRpcInfo> &transactionHashes);
        std::error_code getTransactions(const std::vector<std::string> &addresses, const std::string &blockHash,
                                        uint32_t blockCount, const std::string &paymentId, std::vector<TransactionsInBlockRpcInfo> &transactionHashes);
        std::error_code getTransactions(const std::vector<std::string> &addresses, uint32_t firstBlockIndex,
                                        uint32_t blockCount, const std::string &paymentId, std::vector<TransactionsInBlockRpcInfo> &transactionHashes);
        std::error_code getTransaction(const std::string &transactionHash, TransactionRpcInfo &transaction);
        std::error_code getAddresses(std::vector<std::string> &addresses);
        std::error_code sendTransaction(SendTransaction::Request &request, std::string &transactionHash);
        std::error_code createDelayedTransaction(CreateDelayedTransaction::Request &request, std::string &transactionHash);
        std::error_code getDelayedTransactionHashes(std::vector<std::string> &transactionHashes);
        std::error_code deleteDelayedTransaction(const std::string &transactionHash);
        std::error_code sendDelayedTransaction(const std::string &transactionHash);
        std::error_code getUnconfirmedTransactionHashes(const std::vector<std::string> &addresses, std::vector<std::string> &transactionHashes);
        std::error_code getStatus(uint32_t &blockCount, uint32_t &knownBlockCount, uint64_t &localDaemonBlockCount, std::string &lastBlockHash, uint32_t &peerCount);
        std::error_code sendFusionTransaction(uint64_t threshold, uint32_t anonymity, const std::vector<std::string> &addresses,
                                              const std::string &destinationAddress, std::string &transactionHash);
        std::error_code estimateFusion(uint64_t threshold, const std::vector<std::string> &addresses, uint32_t &fusionReadyCount, uint32_t &totalOutputCount);
        std::error_code createIntegratedAddress(const std::string &address, const std::string &paymentId, std::string &integratedAddress);
        std::error_code getFeeInfo(std::string &address, uint32_t &amount);
        uint64_t getDefaultMixin() const;
        std::error_code validateAddress(const std::string &address, bool &isValid);

    private:
        void refresh();
        void reset(const uint64_t scanHeight);

        void loadWallet();
        void loadTransactionIdIndex();
        void getNodeFee();

        std::vector<mevacoin::TransactionsInBlockInfo> getTransactions(const crypto::Hash &blockHash, size_t blockCount) const;
        std::vector<mevacoin::TransactionsInBlockInfo> getTransactions(uint32_t firstBlockIndex, size_t blockCount) const;

        std::vector<TransactionHashesInBlockRpcInfo> getRpcTransactionHashes(const crypto::Hash &blockHash, size_t blockCount, const TransactionsInBlockInfoFilter &filter) const;
        std::vector<TransactionHashesInBlockRpcInfo> getRpcTransactionHashes(uint32_t firstBlockIndex, size_t blockCount, const TransactionsInBlockInfoFilter &filter) const;

        std::vector<TransactionsInBlockRpcInfo> getRpcTransactions(const crypto::Hash &blockHash, size_t blockCount, const TransactionsInBlockInfoFilter &filter) const;
        std::vector<TransactionsInBlockRpcInfo> getRpcTransactions(uint32_t firstBlockIndex, size_t blockCount, const TransactionsInBlockInfoFilter &filter) const;

        const mevacoin::Currency &currency;
        mevacoin::IWallet &wallet;
        mevacoin::IFusionManager &fusionManager;
        mevacoin::INode &node;
        const WalletConfiguration &config;
        bool inited;
        logging::LoggerRef logger;
        syst::Dispatcher &dispatcher;
        syst::Event readyEvent;
        syst::ContextGroup refreshContext;
        std::string m_node_address;
        uint32_t m_node_fee;

        std::map<std::string, size_t> transactionIdIndex;
    };

} // namespace payment_service
