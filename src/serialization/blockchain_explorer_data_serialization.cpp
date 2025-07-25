// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#include "blockchain_explorer_data_serialization.h"

#include <stdexcept>

#include <boost/variant/static_visitor.hpp>
#include <boost/variant/apply_visitor.hpp>

#include "mevacoin_core/mevacoin_serialization.h"

#include "serialization/serialization_overloads.h"

namespace mevacoin
{

    using mevacoin::SerializationTag;

    namespace
    {

        struct BinaryVariantTagGetter : boost::static_visitor<uint8_t>
        {
            uint8_t operator()(const mevacoin::BaseInputDetails) { return static_cast<uint8_t>(SerializationTag::Base); }
            uint8_t operator()(const mevacoin::KeyInputDetails) { return static_cast<uint8_t>(SerializationTag::Key); }
        };

        struct VariantSerializer : boost::static_visitor<>
        {
            VariantSerializer(mevacoin::ISerializer &serializer, const std::string &name) : s(serializer), name(name) {}

            template <typename T>
            void operator()(T &param) { s(param, name); }

            mevacoin::ISerializer &s;
            const std::string name;
        };

        void getVariantValue(mevacoin::ISerializer &serializer, uint8_t tag, boost::variant<mevacoin::BaseInputDetails, mevacoin::KeyInputDetails> &in)
        {
            switch (static_cast<SerializationTag>(tag))
            {
            case SerializationTag::Base:
            {
                mevacoin::BaseInputDetails v;
                serializer(v, "data");
                in = v;
                break;
            }
            case SerializationTag::Key:
            {
                mevacoin::KeyInputDetails v;
                serializer(v, "data");
                in = v;
                break;
            }
            default:
                throw std::runtime_error("Unknown variant tag");
            }
        }

        template <typename T>
        bool serializePod(T &v, common::StringView name, mevacoin::ISerializer &serializer)
        {
            return serializer.binary(&v, sizeof(v), name);
        }

    } // namespace

    // namespace mevacoin {

    void serialize(TransactionOutputDetails &output, ISerializer &serializer)
    {
        serializer(output.output, "output");
        serializer(output.globalIndex, "globalIndex");
    }

    void serialize(TransactionOutputReferenceDetails &outputReference, ISerializer &serializer)
    {
        serializePod(outputReference.transactionHash, "transactionHash", serializer);
        serializer(outputReference.number, "number");
    }

    void serialize(BaseInputDetails &inputBase, ISerializer &serializer)
    {
        serializer(inputBase.input, "input");
        serializer(inputBase.amount, "amount");
    }

    void serialize(KeyInputDetails &inputToKey, ISerializer &serializer)
    {
        serializer(inputToKey.input, "input");
        serializer(inputToKey.mixin, "mixin");
        serializer(inputToKey.output, "output");
    }

    void serialize(TransactionInputDetails &input, ISerializer &serializer)
    {
        if (serializer.type() == ISerializer::OUTPUT)
        {
            BinaryVariantTagGetter tagGetter;
            uint8_t tag = boost::apply_visitor(tagGetter, input);
            serializer.binary(&tag, sizeof(tag), "type");

            VariantSerializer visitor(serializer, "data");
            boost::apply_visitor(visitor, input);
        }
        else
        {
            uint8_t tag;
            serializer.binary(&tag, sizeof(tag), "type");

            getVariantValue(serializer, tag, input);
        }
    }

    void serialize(TransactionExtraDetails &extra, ISerializer &serializer)
    {
        serializePod(extra.publicKey, "publicKey", serializer);
        serializer(extra.nonce, "nonce");
        serializeAsBinary(extra.raw, "raw", serializer);
    }

    void serialize(TransactionDetails &transaction, ISerializer &serializer)
    {
        serializePod(transaction.hash, "hash", serializer);
        serializer(transaction.size, "size");
        serializer(transaction.fee, "fee");
        serializer(transaction.totalInputsAmount, "totalInputsAmount");
        serializer(transaction.totalOutputsAmount, "totalOutputsAmount");
        serializer(transaction.mixin, "mixin");
        serializer(transaction.unlockTime, "unlockTime");
        serializer(transaction.timestamp, "timestamp");
        serializePod(transaction.paymentId, "paymentId", serializer);
        serializer(transaction.inBlockchain, "inBlockchain");
        serializePod(transaction.blockHash, "blockHash", serializer);
        serializer(transaction.blockIndex, "blockIndex");
        serializer(transaction.extra, "extra");
        serializer(transaction.inputs, "inputs");
        serializer(transaction.outputs, "outputs");

        // serializer(transaction.signatures, "signatures");
        if (serializer.type() == ISerializer::OUTPUT)
        {
            std::vector<std::pair<uint64_t, crypto::Signature>> signaturesForSerialization;
            signaturesForSerialization.reserve(transaction.signatures.size());
            uint64_t ctr = 0;
            for (const auto &signaturesV : transaction.signatures)
            {
                for (auto signature : signaturesV)
                {
                    signaturesForSerialization.emplace_back(ctr, std::move(signature));
                }
                ++ctr;
            }
            uint64_t size = transaction.signatures.size();
            serializer(size, "signaturesSize");
            serializer(signaturesForSerialization, "signatures");
        }
        else
        {
            uint64_t size = 0;
            serializer(size, "signaturesSize");
            transaction.signatures.resize(size);

            std::vector<std::pair<uint64_t, crypto::Signature>> signaturesForSerialization;
            serializer(signaturesForSerialization, "signatures");

            for (const auto &signatureWithIndex : signaturesForSerialization)
            {
                transaction.signatures[signatureWithIndex.first].push_back(signatureWithIndex.second);
            }
        }
    }

    void serialize(BlockDetails &block, ISerializer &serializer)
    {
        serializer(block.majorVersion, "majorVersion");
        serializer(block.minorVersion, "minorVersion");
        serializer(block.timestamp, "timestamp");
        serializePod(block.prevBlockHash, "prevBlockHash", serializer);
        serializer(block.nonce, "nonce");
        serializer(block.index, "index");
        serializePod(block.hash, "hash", serializer);
        serializer(block.difficulty, "difficulty");
        serializer(block.reward, "reward");
        serializer(block.baseReward, "baseReward");
        serializer(block.blockSize, "blockSize");
        serializer(block.transactionsCumulativeSize, "transactionsCumulativeSize");
        serializer(block.alreadyGeneratedCoins, "alreadyGeneratedCoins");
        serializer(block.alreadyGeneratedTransactions, "alreadyGeneratedTransactions");
        serializer(block.sizeMedian, "sizeMedian");
        /* Some serializers don't support doubles, which causes this to fail and
           not serialize the whole object
        serializer(block.penalty, "penalty");
        */
        serializer(block.totalFeeAmount, "totalFeeAmount");
        serializer(block.transactions, "transactions");
    }

} // namespace mevacoin
