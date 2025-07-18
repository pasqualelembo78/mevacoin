// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <string>
#include <sstream>

#include "common/std_output_stream.h"
#include "serialization/kv_binary_output_stream_serializer.h"
#include "serialization/serialization_overloads.h"
#include "mevacoin_core/mevacoin_format_utils.h"
#include "mevacoin_core/mevacoin_serialization.h"
#include "common/std_input_stream.h"
#include "serialization/kv_binary_input_stream_serializer.h"

namespace mevacoin
{
    namespace db
    {
        const std::string BLOCK_INDEX_TO_KEY_IMAGE_PREFIX = "0";
        const std::string BLOCK_INDEX_TO_TX_HASHES_PREFIX = "1";
        const std::string BLOCK_INDEX_TO_TRANSACTION_INFO_PREFIX = "2";
        const std::string BLOCK_INDEX_TO_RAW_BLOCK_PREFIX = "4";

        const std::string BLOCK_HASH_TO_BLOCK_INDEX_PREFIX = "5";
        const std::string BLOCK_INDEX_TO_BLOCK_INFO_PREFIX = "6";

        const std::string KEY_IMAGE_TO_BLOCK_INDEX_PREFIX = "7";
        const std::string BLOCK_INDEX_TO_BLOCK_HASH_PREFIX = "8";

        const std::string TRANSACTION_HASH_TO_TRANSACTION_INFO_PREFIX = "a";

        const std::string KEY_OUTPUT_AMOUNT_PREFIX = "b";

        const std::string CLOSEST_TIMESTAMP_BLOCK_INDEX_PREFIX = "e";

        const std::string PAYMENT_ID_TO_TX_HASH_PREFIX = "f";

        const std::string TIMESTAMP_TO_BLOCKHASHES_PREFIX = "g";

        const std::string KEY_OUTPUT_AMOUNTS_COUNT_PREFIX = "h";

        const std::string LAST_BLOCK_INDEX_KEY = "last_block_index";

        const std::string KEY_OUTPUT_AMOUNTS_COUNT_KEY = "key_amounts_count";

        const std::string TRANSACTIONS_COUNT_KEY = "txs_count";

        const std::string KEY_OUTPUT_KEY_PREFIX = "j";

        template <class Value>
        std::string serialize(const Value &value, const std::string &name)
        {
            mevacoin::KVBinaryOutputStreamSerializer serializer;
            std::stringstream ss;
            common::StdOutputStream stream(ss);

            serializer(const_cast<Value &>(value), name);
            serializer.dump(stream);

            return ss.str();
        }

        std::string serialize(const RawBlock &value, const std::string &name);

        template <class Key, class Value>
        std::pair<std::string, std::string> serialize(const std::string &keyPrefix, const Key &key, const Value &value)
        {
            return {db::serialize(std::make_pair(keyPrefix, key), keyPrefix), db::serialize(value, keyPrefix)};
        }

        template <class Key>
        std::string serializeKey(const std::string &keyPrefix, const Key &key)
        {
            return db::serialize(std::make_pair(keyPrefix, key), keyPrefix);
        }

        template <class Value>
        void deserialize(const std::string &serialized, Value &value, const std::string &name)
        {
            std::stringstream ss(serialized);
            common::StdInputStream stream(ss);
            mevacoin::KVBinaryInputStreamSerializer serializer(stream);
            serializer(value, name);
        }

        void deserialize(const std::string &serialized, RawBlock &value, const std::string &name);

        template <class Key, class Value>
        void serializeKeys(std::vector<std::string> &rawKeys, const std::string keyPrefix, const std::unordered_map<Key, Value> &map)
        {
            for (const std::pair<Key, Value> &kv : map)
            {
                rawKeys.emplace_back(db::serializeKey(keyPrefix, kv.first));
            }
        }

        template <class Key, class Value, class Iterator>
        void deserializeValues(std::unordered_map<Key, Value> &map, Iterator &serializedValuesIter, const std::string &name)
        {
            for (auto iter = map.begin(); iter != map.end(); ++serializedValuesIter)
            {
                if (boost::get<1>(*serializedValuesIter))
                {
                    db::deserialize(boost::get<0>(*serializedValuesIter), iter->second, name);
                    ++iter;
                }
                else
                {
                    iter = map.erase(iter);
                }
            }
        }

        template <class Value, class Iterator>
        void deserializeValue(std::pair<Value, bool> &pair, Iterator &serializedValuesIter, const std::string &name)
        {
            if (pair.second)
            {
                if (boost::get<1>(*serializedValuesIter))
                {
                    db::deserialize(boost::get<0>(*serializedValuesIter), pair.first, name);
                }
                else
                {
                    pair = {Value{}, false};
                }
                ++serializedValuesIter;
            }
        }
    }
}
