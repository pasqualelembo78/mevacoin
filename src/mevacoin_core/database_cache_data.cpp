// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#include <mevacoin_core/mevacoin_serialization.h>
#include <mevacoin_core/mevacoin_tools.h>
#include <mevacoin_core/database_cache_data.h>
#include <serialization/serialization_overloads.h>

namespace mevacoin
{

    void ExtendedTransactionInfo::serialize(mevacoin::ISerializer &s)
    {
        s(static_cast<CachedTransactionInfo &>(*this), "cached_transaction");
        s(amountToKeyIndexes, "key_indexes");
    }

    void KeyOutputInfo::serialize(ISerializer &s)
    {
        s(publicKey, "public_key");
        s(transactionHash, "transaction_hash");
        s(unlockTime, "unlock_time");
        s(outputIndex, "output_index");
    }

}
