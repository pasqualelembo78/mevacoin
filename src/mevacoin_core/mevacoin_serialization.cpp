// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#include "mevacoin_serialization.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include <boost/variant/static_visitor.hpp>
#include <boost/variant/apply_visitor.hpp>

#include "serialization/iserializer.h"
#include "serialization/serialization_overloads.h"
#include "serialization/binary_input_stream_serializer.h"
#include "serialization/binary_output_stream_serializer.h"

#include "common/string_output_stream.h"
#include "crypto/crypto.h"

#include "account.h"
#include <config/mevacoin_config.h>
#include "mevacoin_format_utils.h"
#include "mevacoin_tools.h"
#include "transaction_extra.h"

using namespace common;

namespace
{

    using namespace mevacoin;
    using namespace common;

    uint64_t getSignaturesCount(const TransactionInput &input)
    {
        struct txin_signature_size_visitor : public boost::static_visitor<uint64_t>
        {
            uint64_t operator()(const BaseInput &txin) const { return 0; }
            uint64_t operator()(const KeyInput &txin) const { return txin.outputIndexes.size(); }
        };

        return boost::apply_visitor(txin_signature_size_visitor(), input);
    }

    struct BinaryVariantTagGetter : boost::static_visitor<uint8_t>
    {
        uint8_t operator()(const mevacoin::BaseInput) { return 0xff; }
        uint8_t operator()(const mevacoin::KeyInput) { return 0x2; }
        uint8_t operator()(const mevacoin::KeyOutput) { return 0x2; }
        uint8_t operator()(const mevacoin::Transaction) { return 0xcc; }
        uint8_t operator()(const mevacoin::BlockTemplate) { return 0xbb; }
    };

    struct VariantSerializer : boost::static_visitor<>
    {
        VariantSerializer(mevacoin::ISerializer &serializer, const std::string &name) : s(serializer), name(name) {}

        template <typename T>
        void operator()(T &param) { s(param, name); }

        mevacoin::ISerializer &s;
        std::string name;
    };

    void getVariantValue(mevacoin::ISerializer &serializer, uint8_t tag, mevacoin::TransactionInput &in)
    {
        switch (tag)
        {
        case 0xff:
        {
            mevacoin::BaseInput v;
            serializer(v, "value");
            in = v;
            break;
        }
        case 0x2:
        {
            mevacoin::KeyInput v;
            serializer(v, "value");
            in = v;
            break;
        }
        default:
            throw std::runtime_error("Unknown variant tag");
        }
    }

    void getVariantValue(mevacoin::ISerializer &serializer, uint8_t tag, mevacoin::TransactionOutputTarget &out)
    {
        switch (tag)
        {
        case 0x2:
        {
            mevacoin::KeyOutput v;
            serializer(v, "data");
            out = v;
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

    bool serializeVarintVector(std::vector<uint32_t> &vector, mevacoin::ISerializer &serializer, common::StringView name)
    {
        uint64_t size = vector.size();

        if (!serializer.beginArray(size, name))
        {
            vector.clear();
            return false;
        }

        vector.resize(size);

        for (uint64_t i = 0; i < size; ++i)
        {
            serializer(vector[i], "");
        }

        serializer.endArray();
        return true;
    }

}

namespace crypto
{

    bool serialize(PublicKey &pubKey, common::StringView name, mevacoin::ISerializer &serializer)
    {
        return serializePod(pubKey, name, serializer);
    }

    bool serialize(SecretKey &secKey, common::StringView name, mevacoin::ISerializer &serializer)
    {
        return serializePod(secKey, name, serializer);
    }

    bool serialize(Hash &h, common::StringView name, mevacoin::ISerializer &serializer)
    {
        return serializePod(h, name, serializer);
    }

    bool serialize(KeyImage &keyImage, common::StringView name, mevacoin::ISerializer &serializer)
    {
        return serializePod(keyImage, name, serializer);
    }

    bool serialize(chacha8_iv &chacha, common::StringView name, mevacoin::ISerializer &serializer)
    {
        return serializePod(chacha, name, serializer);
    }

    bool serialize(Signature &sig, common::StringView name, mevacoin::ISerializer &serializer)
    {
        return serializePod(sig, name, serializer);
    }

    bool serialize(EllipticCurveScalar &ecScalar, common::StringView name, mevacoin::ISerializer &serializer)
    {
        return serializePod(ecScalar, name, serializer);
    }

    bool serialize(EllipticCurvePoint &ecPoint, common::StringView name, mevacoin::ISerializer &serializer)
    {
        return serializePod(ecPoint, name, serializer);
    }

}

namespace mevacoin
{

    void serialize(TransactionPrefix &txP, ISerializer &serializer)
    {
        serializer(txP.version, "version");

        if (CURRENT_TRANSACTION_VERSION < txP.version && serializer.type() == ISerializer::INPUT)
        {
            throw std::runtime_error("Wrong transaction version");
        }

        serializer(txP.unlockTime, "unlock_time");
        serializer(txP.inputs, "vin");
        serializer(txP.outputs, "vout");
        serializeAsBinary(txP.extra, "extra", serializer);
    }

    void serialize(BaseTransaction &tx, ISerializer &serializer)
    {
        serializer(tx.version, "version");
        serializer(tx.unlockTime, "unlock_time");
        serializer(tx.inputs, "vin");
        serializer(tx.outputs, "vout");
        serializeAsBinary(tx.extra, "extra", serializer);

        if (tx.version >= TRANSACTION_VERSION_2)
        {
            uint64_t ignored = 0;
            serializer(ignored, "ignored");
        }
    }

    void serialize(Transaction &tx, ISerializer &serializer)
    {
        serialize(static_cast<TransactionPrefix &>(tx), serializer);

        uint64_t sigSize = tx.inputs.size();
        // TODO: make arrays without sizes
        //  serializer.beginArray(sigSize, "signatures");

        // ignore base transaction
        if (serializer.type() == ISerializer::INPUT && !(sigSize == 1 && tx.inputs[0].type() == typeid(BaseInput)))
        {
            tx.signatures.resize(sigSize);
        }

        bool signaturesNotExpected = tx.signatures.empty();
        if (!signaturesNotExpected && tx.inputs.size() != tx.signatures.size())
        {
            throw std::runtime_error("Serialization error: unexpected signatures size");
        }

        for (uint64_t i = 0; i < tx.inputs.size(); ++i)
        {
            uint64_t signatureSize = getSignaturesCount(tx.inputs[i]);
            if (signaturesNotExpected)
            {
                if (signatureSize == 0)
                {
                    continue;
                }
                else
                {
                    throw std::runtime_error("Serialization error: signatures are not expected");
                }
            }

            if (serializer.type() == ISerializer::OUTPUT)
            {
                if (signatureSize != tx.signatures[i].size())
                {
                    throw std::runtime_error("Serialization error: unexpected signatures size");
                }

                for (crypto::Signature &sig : tx.signatures[i])
                {
                    serializePod(sig, "", serializer);
                }
            }
            else
            {
                std::vector<crypto::Signature> signatures(signatureSize);
                for (crypto::Signature &sig : signatures)
                {
                    serializePod(sig, "", serializer);
                }

                tx.signatures[i] = std::move(signatures);
            }
        }
        //  serializer.endArray();
    }

    void serialize(TransactionInput &in, ISerializer &serializer)
    {
        if (serializer.type() == ISerializer::OUTPUT)
        {
            BinaryVariantTagGetter tagGetter;
            uint8_t tag = boost::apply_visitor(tagGetter, in);
            serializer.binary(&tag, sizeof(tag), "type");

            VariantSerializer visitor(serializer, "value");
            boost::apply_visitor(visitor, in);
        }
        else
        {
            uint8_t tag;
            serializer.binary(&tag, sizeof(tag), "type");

            getVariantValue(serializer, tag, in);
        }
    }

    void serialize(BaseInput &gen, ISerializer &serializer)
    {
        serializer(gen.blockIndex, "height");
    }

    void serialize(KeyInput &key, ISerializer &serializer)
    {
        serializer(key.amount, "amount");
        serializeVarintVector(key.outputIndexes, serializer, "key_offsets");
        serializer(key.keyImage, "k_image");
    }

    void serialize(TransactionOutput &output, ISerializer &serializer)
    {
        serializer(output.amount, "amount");
        serializer(output.target, "target");
    }

    void serialize(TransactionOutputTarget &output, ISerializer &serializer)
    {
        if (serializer.type() == ISerializer::OUTPUT)
        {
            BinaryVariantTagGetter tagGetter;
            uint8_t tag = boost::apply_visitor(tagGetter, output);
            serializer.binary(&tag, sizeof(tag), "type");

            VariantSerializer visitor(serializer, "data");
            boost::apply_visitor(visitor, output);
        }
        else
        {
            uint8_t tag;
            serializer.binary(&tag, sizeof(tag), "type");

            getVariantValue(serializer, tag, output);
        }
    }

    void serialize(KeyOutput &key, ISerializer &serializer)
    {
        serializer(key.key, "key");
    }

    void serialize(ParentBlockSerializer &pbs, ISerializer &serializer)
    {
        serializer(pbs.m_parentBlock.majorVersion, "majorVersion");

        serializer(pbs.m_parentBlock.minorVersion, "minorVersion");
        serializer(pbs.m_timestamp, "timestamp");
        serializer(pbs.m_parentBlock.previousBlockHash, "prevId");
        serializer.binary(&pbs.m_nonce, sizeof(pbs.m_nonce), "nonce");

        if (pbs.m_hashingSerialization)
        {
            crypto::Hash minerTxHash;
            if (!getBaseTransactionHash(pbs.m_parentBlock.baseTransaction, minerTxHash))
            {
                throw std::runtime_error("Get transaction hash error");
            }

            crypto::Hash merkleRoot;
            crypto::tree_hash_from_branch(pbs.m_parentBlock.baseTransactionBranch.data(), pbs.m_parentBlock.baseTransactionBranch.size(), minerTxHash, 0, merkleRoot);

            serializer(merkleRoot, "merkleRoot");
        }

        uint64_t txNum = static_cast<uint64_t>(pbs.m_parentBlock.transactionCount);
        serializer(txNum, "numberOfTransactions");
        pbs.m_parentBlock.transactionCount = static_cast<uint16_t>(txNum);
        if (pbs.m_parentBlock.transactionCount < 1)
        {
            throw std::runtime_error("Wrong transactions number");
        }

        if (pbs.m_headerOnly)
        {
            return;
        }

        uint64_t branchSize = crypto::tree_depth(pbs.m_parentBlock.transactionCount);
        if (serializer.type() == ISerializer::OUTPUT)
        {
            if (pbs.m_parentBlock.baseTransactionBranch.size() != branchSize)
            {
                throw std::runtime_error("Wrong miner transaction branch size");
            }
        }
        else
        {
            pbs.m_parentBlock.baseTransactionBranch.resize(branchSize);
        }

        //  serializer(m_parentBlock.baseTransactionBranch, "baseTransactionBranch");
        // TODO: Make arrays with computable size! This code won't work with json serialization!
        for (crypto::Hash &hash : pbs.m_parentBlock.baseTransactionBranch)
        {
            serializer(hash, "");
        }

        serializer(pbs.m_parentBlock.baseTransaction, "minerTx");

        TransactionExtraMergeMiningTag mmTag;
        if (!getMergeMiningTagFromExtra(pbs.m_parentBlock.baseTransaction.extra, mmTag))
        {
            throw std::runtime_error("Can't get extra merge mining tag");
        }

        if (mmTag.depth > 8 * sizeof(crypto::Hash))
        {
            throw std::runtime_error("Wrong merge mining tag depth");
        }

        if (serializer.type() == ISerializer::OUTPUT)
        {
            if (mmTag.depth != pbs.m_parentBlock.blockchainBranch.size())
            {
                throw std::runtime_error("Blockchain branch size must be equal to merge mining tag depth");
            }
        }
        else
        {
            pbs.m_parentBlock.blockchainBranch.resize(mmTag.depth);
        }

        //  serializer(m_parentBlock.blockchainBranch, "blockchainBranch");
        // TODO: Make arrays with computable size! This code won't work with json serialization!
        for (crypto::Hash &hash : pbs.m_parentBlock.blockchainBranch)
        {
            serializer(hash, "");
        }
    }

    void serializeBlockHeader(BlockHeader &header, ISerializer &serializer)
    {
        serializer(header.majorVersion, "major_version");
        if (header.majorVersion > BLOCK_MAJOR_VERSION_5)
        {
            throw std::runtime_error("Wrong major version");
        }

        serializer(header.minorVersion, "minor_version");
        if (header.majorVersion == BLOCK_MAJOR_VERSION_1)
        {
            serializer(header.timestamp, "timestamp");
            serializer(header.previousBlockHash, "prev_id");
            serializer.binary(&header.nonce, sizeof(header.nonce), "nonce");
        }
        else if (header.majorVersion >= BLOCK_MAJOR_VERSION_2)
        {
            serializer(header.previousBlockHash, "prev_id");
        }
        else
        {
            throw std::runtime_error("Wrong major version");
        }
    }

    void serialize(BlockHeader &header, ISerializer &serializer)
    {
        serializeBlockHeader(header, serializer);
    }

    void serialize(BlockTemplate &block, ISerializer &serializer)
    {
        serializeBlockHeader(block, serializer);

        if (block.majorVersion >= BLOCK_MAJOR_VERSION_2)
        {
            auto parentBlockSerializer = makeParentBlockSerializer(block, false, false);
            serializer(parentBlockSerializer, "parent_block");
        }

        serializer(block.baseTransaction, "miner_tx");
        serializer(block.transactionHashes, "tx_hashes");
    }

    void serialize(AccountPublicAddress &address, ISerializer &serializer)
    {
        serializer(address.spendPublicKey, "m_spend_public_key");
        serializer(address.viewPublicKey, "m_view_public_key");
    }

    void serialize(AccountKeys &keys, ISerializer &s)
    {
        s(keys.address, "m_account_address");
        s(keys.spendSecretKey, "m_spend_secret_key");
        s(keys.viewSecretKey, "m_view_secret_key");
    }

    void doSerialize(TransactionExtraMergeMiningTag &tag, ISerializer &serializer)
    {
        uint64_t depth = static_cast<uint64_t>(tag.depth);
        serializer(depth, "depth");
        tag.depth = static_cast<uint64_t>(depth);
        serializer(tag.merkleRoot, "merkle_root");
    }

    void serialize(TransactionExtraMergeMiningTag &tag, ISerializer &serializer)
    {
        if (serializer.type() == ISerializer::OUTPUT)
        {
            std::string field;
            StringOutputStream os(field);
            BinaryOutputStreamSerializer output(os);
            doSerialize(tag, output);
            serializer(field, "");
        }
        else
        {
            std::string field;
            serializer(field, "mm_tag");
            MemoryInputStream stream(field.data(), field.size());
            BinaryInputStreamSerializer input(stream);
            doSerialize(tag, input);
        }
    }

    void serialize(KeyPair &keyPair, ISerializer &serializer)
    {
        serializer(keyPair.secretKey, "secret_key");
        serializer(keyPair.publicKey, "public_key");
    }

    // unpack to strings to maintain protocol compatibility with older versions
    void serialize(RawBlock &rawBlock, ISerializer &serializer)
    {
        if (serializer.type() == ISerializer::INPUT)
        {
            uint64_t blockSize;
            serializer(blockSize, "block_size");
            rawBlock.block.resize(static_cast<uint64_t>(blockSize));
        }
        else
        {
            uint64_t blockSize = rawBlock.block.size();
            serializer(blockSize, "block_size");
        }

        serializer.binary(rawBlock.block.data(), rawBlock.block.size(), "block");

        if (serializer.type() == ISerializer::INPUT)
        {
            uint64_t txCount;
            serializer(txCount, "tx_count");
            rawBlock.transactions.resize(static_cast<uint64_t>(txCount));

            for (auto &txBlob : rawBlock.transactions)
            {
                uint64_t txSize;
                serializer(txSize, "tx_size");
                txBlob.resize(txSize);
                serializer.binary(txBlob.data(), txBlob.size(), "transaction");
            }
        }
        else
        {
            uint64_t txCount = rawBlock.transactions.size();
            serializer(txCount, "tx_count");

            for (auto &txBlob : rawBlock.transactions)
            {
                uint64_t txSize = txBlob.size();
                serializer(txSize, "tx_size");
                serializer.binary(txBlob.data(), txBlob.size(), "transaction");
            }
        }
    }

} // namespace mevacoin
