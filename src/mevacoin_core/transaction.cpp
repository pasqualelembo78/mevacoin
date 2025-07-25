// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#include "itransaction.h"
#include "transaction_api_extra.h"
#include "transaction_utils.h"

#include "account.h"
#include "mevacoin_core/mevacoin_tools.h"
#include <config/mevacoin_config.h>

#include <boost/optional.hpp>
#include <numeric>
#include <unordered_set>
#include <memory>

using namespace crypto;

namespace
{

    using namespace mevacoin;

    void derivePublicKey(const AccountPublicAddress &to, const SecretKey &txKey, size_t outputIndex, PublicKey &ephemeralKey)
    {
        KeyDerivation derivation;
        generate_key_derivation(to.viewPublicKey, txKey, derivation);
        derive_public_key(derivation, outputIndex, to.spendPublicKey, ephemeralKey);
    }

}

namespace mevacoin
{

    using namespace crypto;

    ////////////////////////////////////////////////////////////////////////
    // class Transaction declaration
    ////////////////////////////////////////////////////////////////////////

    class TransactionImpl : public ITransaction
    {
    public:
        TransactionImpl();
        TransactionImpl(const BinaryArray &txblob);
        TransactionImpl(const mevacoin::Transaction &tx);

        // ITransactionReader
        virtual Hash getTransactionHash() const override;
        virtual Hash getTransactionPrefixHash() const override;
        virtual PublicKey getTransactionPublicKey() const override;
        virtual uint64_t getUnlockTime() const override;
        virtual bool getPaymentId(Hash &hash) const override;
        virtual bool getExtraNonce(BinaryArray &nonce) const override;
        virtual BinaryArray getExtra() const override;

        // inputs
        virtual size_t getInputCount() const override;
        virtual uint64_t getInputTotalAmount() const override;
        virtual TransactionTypes::InputType getInputType(size_t index) const override;
        virtual void getInput(size_t index, KeyInput &input) const override;

        // outputs
        virtual size_t getOutputCount() const override;
        virtual uint64_t getOutputTotalAmount() const override;
        virtual TransactionTypes::OutputType getOutputType(size_t index) const override;
        virtual void getOutput(size_t index, KeyOutput &output, uint64_t &amount) const override;

        virtual size_t getRequiredSignaturesCount(size_t index) const override;
        virtual bool findOutputsToAccount(const AccountPublicAddress &addr, const SecretKey &viewSecretKey, std::vector<uint32_t> &outs, uint64_t &outputAmount) const override;

        // get serialized transaction
        virtual BinaryArray getTransactionData() const override;

        // ITransactionWriter

        virtual void setUnlockTime(uint64_t unlockTime) override;
        virtual void setExtraNonce(const BinaryArray &nonce) override;
        virtual void appendExtra(const BinaryArray &extraData) override;

        // Inputs/Outputs
        virtual size_t addInput(const KeyInput &input) override;
        virtual size_t addInput(const AccountKeys &senderKeys, const TransactionTypes::InputKeyInfo &info, KeyPair &ephKeys) override;

        virtual size_t addOutput(uint64_t amount, const AccountPublicAddress &to) override;
        virtual size_t addOutput(uint64_t amount, const KeyOutput &out) override;

        virtual void signInputKey(size_t input, const TransactionTypes::InputKeyInfo &info, const KeyPair &ephKeys) override;

    private:
        void invalidateHash();

        std::vector<Signature> &getSignatures(size_t input);

        const SecretKey &txSecretKey() const
        {
            if (!secretKey)
            {
                throw std::runtime_error("Operation requires transaction secret key");
            }
            return *secretKey;
        }

        void checkIfSigning() const
        {
            if (!transaction.signatures.empty())
            {
                throw std::runtime_error("Cannot perform requested operation, since it will invalidate transaction signatures");
            }
        }

        mevacoin::Transaction transaction;
        boost::optional<SecretKey> secretKey;
        mutable boost::optional<Hash> transactionHash;
        TransactionExtra extra;
    };

    ////////////////////////////////////////////////////////////////////////
    // class Transaction implementation
    ////////////////////////////////////////////////////////////////////////

    std::unique_ptr<ITransaction> createTransaction()
    {
        return std::unique_ptr<ITransaction>(new TransactionImpl());
    }

    std::unique_ptr<ITransaction> createTransaction(const BinaryArray &transactionBlob)
    {
        return std::unique_ptr<ITransaction>(new TransactionImpl(transactionBlob));
    }

    std::unique_ptr<ITransaction> createTransaction(const mevacoin::Transaction &tx)
    {
        return std::unique_ptr<ITransaction>(new TransactionImpl(tx));
    }

    TransactionImpl::TransactionImpl()
    {
        mevacoin::KeyPair txKeys(mevacoin::generateKeyPair());

        TransactionExtraPublicKey pk = {txKeys.publicKey};
        extra.set(pk);

        transaction.version = CURRENT_TRANSACTION_VERSION;
        transaction.unlockTime = 0;
        transaction.extra = extra.serialize();

        secretKey = txKeys.secretKey;
    }

    TransactionImpl::TransactionImpl(const BinaryArray &ba)
    {
        if (!fromBinaryArray(transaction, ba))
        {
            throw std::runtime_error("Invalid transaction data");
        }

        extra.parse(transaction.extra);
        transactionHash = getBinaryArrayHash(ba); // avoid serialization if we already have blob
    }

    TransactionImpl::TransactionImpl(const mevacoin::Transaction &tx) : transaction(tx)
    {
        extra.parse(transaction.extra);
    }

    void TransactionImpl::invalidateHash()
    {
        if (transactionHash.is_initialized())
        {
            transactionHash = decltype(transactionHash)();
        }
    }

    Hash TransactionImpl::getTransactionHash() const
    {
        if (!transactionHash.is_initialized())
        {
            transactionHash = getObjectHash(transaction);
        }

        return transactionHash.get();
    }

    Hash TransactionImpl::getTransactionPrefixHash() const
    {
        return getObjectHash(*static_cast<const TransactionPrefix *>(&transaction));
    }

    PublicKey TransactionImpl::getTransactionPublicKey() const
    {
        PublicKey pk(NULL_PUBLIC_KEY);
        extra.getPublicKey(pk);
        return pk;
    }

    uint64_t TransactionImpl::getUnlockTime() const
    {
        return transaction.unlockTime;
    }

    void TransactionImpl::setUnlockTime(uint64_t unlockTime)
    {
        checkIfSigning();
        transaction.unlockTime = unlockTime;
        invalidateHash();
    }

    size_t TransactionImpl::addInput(const KeyInput &input)
    {
        checkIfSigning();
        transaction.inputs.emplace_back(input);
        invalidateHash();
        return transaction.inputs.size() - 1;
    }

    size_t TransactionImpl::addInput(const AccountKeys &senderKeys, const TransactionTypes::InputKeyInfo &info, KeyPair &ephKeys)
    {
        checkIfSigning();
        KeyInput input;
        input.amount = info.amount;

        generate_key_image_helper(
            senderKeys,
            info.realOutput.transactionPublicKey,
            info.realOutput.outputInTransaction,
            ephKeys,
            input.keyImage);

        // fill outputs array and use relative offsets
        for (const auto &out : info.outputs)
        {
            input.outputIndexes.push_back(out.outputIndex);
        }

        input.outputIndexes = absolute_output_offsets_to_relative(input.outputIndexes);
        return addInput(input);
    }

    size_t TransactionImpl::addOutput(uint64_t amount, const AccountPublicAddress &to)
    {
        checkIfSigning();

        KeyOutput outKey;
        derivePublicKey(to, txSecretKey(), transaction.outputs.size(), outKey.key);
        TransactionOutput out = {amount, outKey};
        transaction.outputs.emplace_back(out);
        invalidateHash();

        return transaction.outputs.size() - 1;
    }

    size_t TransactionImpl::addOutput(uint64_t amount, const KeyOutput &out)
    {
        checkIfSigning();
        size_t outputIndex = transaction.outputs.size();
        TransactionOutput realOut = {amount, out};
        transaction.outputs.emplace_back(realOut);
        invalidateHash();
        return outputIndex;
    }

    void TransactionImpl::signInputKey(size_t index, const TransactionTypes::InputKeyInfo &info, const KeyPair &ephKeys)
    {
        const auto &input = boost::get<KeyInput>(getInputChecked(transaction, index, TransactionTypes::InputType::Key));
        Hash prefixHash = getTransactionPrefixHash();

        std::vector<PublicKey> publicKeys;

        for (const auto &o : info.outputs)
        {
            publicKeys.push_back(o.targetKey);
        }

        const auto [success, signatures] = crypto_ops::generateRingSignatures(
            prefixHash,
            input.keyImage,
            publicKeys,
            ephKeys.secretKey,
            info.realOutput.transactionIndex);

        getSignatures(index) = signatures;

        invalidateHash();
    }

    std::vector<Signature> &TransactionImpl::getSignatures(size_t input)
    {
        // update signatures container size if needed
        if (transaction.signatures.size() < transaction.inputs.size())
        {
            transaction.signatures.resize(transaction.inputs.size());
        }
        // check range
        if (input >= transaction.signatures.size())
        {
            throw std::runtime_error("Invalid input index");
        }

        return transaction.signatures[input];
    }

    BinaryArray TransactionImpl::getTransactionData() const
    {
        return toBinaryArray(transaction);
    }

    bool TransactionImpl::getPaymentId(Hash &hash) const
    {
        BinaryArray nonce;
        if (getExtraNonce(nonce))
        {
            Hash paymentId;
            if (getPaymentIdFromTransactionExtraNonce(nonce, paymentId))
            {
                hash = reinterpret_cast<const Hash &>(paymentId);
                return true;
            }
        }
        return false;
    }

    void TransactionImpl::setExtraNonce(const BinaryArray &nonce)
    {
        checkIfSigning();
        TransactionExtraNonce extraNonce = {nonce};
        extra.set(extraNonce);
        transaction.extra = extra.serialize();
        invalidateHash();
    }

    void TransactionImpl::appendExtra(const BinaryArray &extraData)
    {
        checkIfSigning();
        transaction.extra.insert(
            transaction.extra.end(), extraData.begin(), extraData.end());
    }

    bool TransactionImpl::getExtraNonce(BinaryArray &nonce) const
    {
        TransactionExtraNonce extraNonce;
        if (extra.get(extraNonce))
        {
            nonce = extraNonce.nonce;
            return true;
        }
        return false;
    }

    BinaryArray TransactionImpl::getExtra() const
    {
        return transaction.extra;
    }

    size_t TransactionImpl::getInputCount() const
    {
        return transaction.inputs.size();
    }

    uint64_t TransactionImpl::getInputTotalAmount() const
    {
        return std::accumulate(transaction.inputs.begin(), transaction.inputs.end(), 0ULL, [](uint64_t val, const TransactionInput &in)
                               { return val + getTransactionInputAmount(in); });
    }

    TransactionTypes::InputType TransactionImpl::getInputType(size_t index) const
    {
        return getTransactionInputType(getInputChecked(transaction, index));
    }

    void TransactionImpl::getInput(size_t index, KeyInput &input) const
    {
        input = boost::get<KeyInput>(getInputChecked(transaction, index, TransactionTypes::InputType::Key));
    }

    size_t TransactionImpl::getOutputCount() const
    {
        return transaction.outputs.size();
    }

    uint64_t TransactionImpl::getOutputTotalAmount() const
    {
        return std::accumulate(transaction.outputs.begin(), transaction.outputs.end(), 0ULL, [](uint64_t val, const TransactionOutput &out)
                               { return val + out.amount; });
    }

    TransactionTypes::OutputType TransactionImpl::getOutputType(size_t index) const
    {
        return getTransactionOutputType(getOutputChecked(transaction, index).target);
    }

    void TransactionImpl::getOutput(size_t index, KeyOutput &output, uint64_t &amount) const
    {
        const auto &out = getOutputChecked(transaction, index, TransactionTypes::OutputType::Key);
        output = boost::get<KeyOutput>(out.target);
        amount = out.amount;
    }

    bool TransactionImpl::findOutputsToAccount(const AccountPublicAddress &addr, const SecretKey &viewSecretKey, std::vector<uint32_t> &out, uint64_t &amount) const
    {
        return ::mevacoin::findOutputsToAccount(transaction, addr, viewSecretKey, out, amount);
    }

    size_t TransactionImpl::getRequiredSignaturesCount(size_t index) const
    {
        return ::getRequiredSignaturesCount(getInputChecked(transaction, index));
    }
}
