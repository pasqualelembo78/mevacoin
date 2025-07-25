// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#include "transaction_utils.h"

#include <unordered_set>

#include "crypto/crypto.h"
#include "mevacoin_core/account.h"
#include "mevacoin_format_utils.h"
#include "transaction_extra.h"

using namespace crypto;

namespace mevacoin
{

    bool checkInputsKeyimagesDiff(const mevacoin::TransactionPrefix &tx)
    {
        std::unordered_set<crypto::KeyImage> ki;
        for (const auto &in : tx.inputs)
        {
            if (in.type() == typeid(KeyInput))
            {
                if (!ki.insert(boost::get<KeyInput>(in).keyImage).second)
                    return false;
            }
        }

        return true;
    }

    // TransactionInput helper functions

    size_t getRequiredSignaturesCount(const TransactionInput &in)
    {
        if (in.type() == typeid(KeyInput))
        {
            return boost::get<KeyInput>(in).outputIndexes.size();
        }

        return 0;
    }

    uint64_t getTransactionInputAmount(const TransactionInput &in)
    {
        if (in.type() == typeid(KeyInput))
        {
            return boost::get<KeyInput>(in).amount;
        }

        return 0;
    }

    TransactionTypes::InputType getTransactionInputType(const TransactionInput &in)
    {
        if (in.type() == typeid(KeyInput))
        {
            return TransactionTypes::InputType::Key;
        }

        if (in.type() == typeid(BaseInput))
        {
            return TransactionTypes::InputType::Generating;
        }

        return TransactionTypes::InputType::Invalid;
    }

    const TransactionInput &getInputChecked(const mevacoin::TransactionPrefix &transaction, size_t index)
    {
        if (transaction.inputs.size() <= index)
        {
            throw std::runtime_error("Transaction input index out of range");
        }

        return transaction.inputs[index];
    }

    const TransactionInput &getInputChecked(const mevacoin::TransactionPrefix &transaction, size_t index, TransactionTypes::InputType type)
    {
        const auto &input = getInputChecked(transaction, index);
        if (getTransactionInputType(input) != type)
        {
            throw std::runtime_error("Unexpected transaction input type");
        }

        return input;
    }

    // TransactionOutput helper functions

    TransactionTypes::OutputType getTransactionOutputType(const TransactionOutputTarget &out)
    {
        if (out.type() == typeid(KeyOutput))
        {
            return TransactionTypes::OutputType::Key;
        }

        return TransactionTypes::OutputType::Invalid;
    }

    const TransactionOutput &getOutputChecked(const mevacoin::TransactionPrefix &transaction, size_t index)
    {
        if (transaction.outputs.size() <= index)
        {
            throw std::runtime_error("Transaction output index out of range");
        }

        return transaction.outputs[index];
    }

    const TransactionOutput &getOutputChecked(const mevacoin::TransactionPrefix &transaction, size_t index, TransactionTypes::OutputType type)
    {
        const auto &output = getOutputChecked(transaction, index);
        if (getTransactionOutputType(output.target) != type)
        {
            throw std::runtime_error("Unexpected transaction output target type");
        }

        return output;
    }

    bool findOutputsToAccount(const mevacoin::TransactionPrefix &transaction, const AccountPublicAddress &addr,
                              const SecretKey &viewSecretKey, std::vector<uint32_t> &out, uint64_t &amount)
    {
        AccountKeys keys;
        keys.address = addr;
        // only view secret key is used, spend key is not needed
        keys.viewSecretKey = viewSecretKey;

        crypto::PublicKey txPubKey = getTransactionPublicKeyFromExtra(transaction.extra);

        amount = 0;
        size_t keyIndex = 0;
        uint32_t outputIndex = 0;

        crypto::KeyDerivation derivation;
        generate_key_derivation(txPubKey, keys.viewSecretKey, derivation);

        for (const TransactionOutput &o : transaction.outputs)
        {
            assert(o.target.type() == typeid(KeyOutput));
            if (o.target.type() == typeid(KeyOutput))
            {
                if (is_out_to_acc(keys, boost::get<KeyOutput>(o.target), derivation, keyIndex))
                {
                    out.push_back(outputIndex);
                    amount += o.amount;
                }

                ++keyIndex;
            }

            ++outputIndex;
        }

        return true;
    }

}
