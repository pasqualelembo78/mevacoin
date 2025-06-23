// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "common/iinput_stream.h"
#include "common/ioutput_stream.h"
#include "serialization/iserializer.h"
#include "transfers/transfers_synchronizer.h"
#include "wallet/wallet_indices.h"

namespace mevacoin
{

    class WalletSerializerV2
    {
    public:
        WalletSerializerV2(
            ITransfersObserver &transfersObserver,
            crypto::PublicKey &viewPublicKey,
            crypto::SecretKey &viewSecretKey,
            uint64_t &actualBalance,
            uint64_t &pendingBalance,
            WalletsContainer &walletsContainer,
            TransfersSyncronizer &synchronizer,
            UnlockTransactionJobs &unlockTransactions,
            WalletTransactions &transactions,
            WalletTransfers &transfers,
            UncommitedTransactions &uncommitedTransactions,
            std::string &extra,
            uint32_t transactionSoftLockTime);

        void load(common::IInputStream &source, uint8_t version);
        void save(common::IOutputStream &destination, WalletSaveLevel saveLevel);

        std::unordered_set<crypto::PublicKey> &addedKeys();
        std::unordered_set<crypto::PublicKey> &deletedKeys();

        static const uint8_t MIN_VERSION = 6;
        static const uint8_t SERIALIZATION_VERSION = 6;

    private:
        void loadKeyListAndBalances(mevacoin::ISerializer &serializer, bool saveCache);
        void saveKeyListAndBalances(mevacoin::ISerializer &serializer, bool saveCache);

        void loadTransactions(mevacoin::ISerializer &serializer);
        void saveTransactions(mevacoin::ISerializer &serializer);

        void loadTransfers(mevacoin::ISerializer &serializer);
        void saveTransfers(mevacoin::ISerializer &serializer);

        void loadTransfersSynchronizer(mevacoin::ISerializer &serializer);
        void saveTransfersSynchronizer(mevacoin::ISerializer &serializer);

        void loadUnlockTransactionsJobs(mevacoin::ISerializer &serializer);
        void saveUnlockTransactionsJobs(mevacoin::ISerializer &serializer);

        uint64_t &m_actualBalance;
        uint64_t &m_pendingBalance;
        WalletsContainer &m_walletsContainer;
        TransfersSyncronizer &m_synchronizer;
        UnlockTransactionJobs &m_unlockTransactions;
        WalletTransactions &m_transactions;
        WalletTransfers &m_transfers;
        UncommitedTransactions &m_uncommitedTransactions;
        std::string &m_extra;

        std::unordered_set<crypto::PublicKey> m_addedKeys;
        std::unordered_set<crypto::PublicKey> m_deletedKeys;
    };

} // namespace mevacoin
