// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once
#include "cached_transaction.h"

namespace mevacoin
{

    struct TransactionValidatorState;

    class ITransactionPool
    {
    public:
        virtual ~ITransactionPool(){};

        virtual bool pushTransaction(CachedTransaction &&tx, TransactionValidatorState &&transactionState) = 0;
        virtual const CachedTransaction &getTransaction(const crypto::Hash &hash) const = 0;
        virtual bool removeTransaction(const crypto::Hash &hash) = 0;

        virtual size_t getTransactionCount() const = 0;
        virtual std::vector<crypto::Hash> getTransactionHashes() const = 0;
        virtual bool checkIfTransactionPresent(const crypto::Hash &hash) const = 0;

        virtual const TransactionValidatorState &getPoolTransactionValidationState() const = 0;
        virtual std::vector<CachedTransaction> getPoolTransactions() const = 0;

        virtual uint64_t getTransactionReceiveTime(const crypto::Hash &hash) const = 0;
        virtual std::vector<crypto::Hash> getTransactionHashesByPaymentId(const crypto::Hash &paymentId) const = 0;

        virtual void flush() = 0;
    };

}
