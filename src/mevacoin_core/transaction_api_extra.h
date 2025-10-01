// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#pragma once

#include "mevacoin_format_utils.h"
#include "transaction_extra.h"

namespace mevacoin
{

    class TransactionExtra
    {
    public:
        TransactionExtra() {}
        TransactionExtra(const std::vector<uint8_t> &extra)
        {
            parse(extra);
        }

        bool parse(const std::vector<uint8_t> &extra)
        {
            fields.clear();
            return mevacoin::parseTransactionExtra(extra, fields);
        }

        template <typename T>
        bool get(T &value) const
        {
            auto it = find(typeid(T));
            if (it == fields.end())
            {
                return false;
            }
            value = boost::get<T>(*it);
            return true;
        }

        template <typename T>
        void set(const T &value)
        {
            auto it = find(typeid(T));
            if (it != fields.end())
            {
                *it = value;
            }
            else
            {
                fields.push_back(value);
            }
        }

        template <typename T>
        void append(const T &value)
        {
            fields.push_back(value);
        }

        bool getPublicKey(crypto::PublicKey &pk) const
        {
            mevacoin::TransactionExtraPublicKey extraPk;
            if (!get(extraPk))
            {
                return false;
            }
            pk = extraPk.publicKey;
            return true;
        }

        std::vector<uint8_t> serialize() const
        {
            std::vector<uint8_t> extra;
            writeTransactionExtra(extra, fields);
            return extra;
        }

    private:
        std::vector<mevacoin::TransactionExtraField>::const_iterator find(const std::type_info &t) const
        {
            return std::find_if(fields.begin(), fields.end(), [&t](const mevacoin::TransactionExtraField &f)
                                { return t == f.type(); });
        }

        std::vector<mevacoin::TransactionExtraField>::iterator find(const std::type_info &t)
        {
            return std::find_if(fields.begin(), fields.end(), [&t](const mevacoin::TransactionExtraField &f)
                                { return t == f.type(); });
        }

        std::vector<mevacoin::TransactionExtraField> fields;
    };

}
