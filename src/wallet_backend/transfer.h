// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#include <mevacoin.h>

#include <mevacoin_core/mevacoin_format_utils.h>

#include <errors/errors.h>

#include <nigel/nigel.h>

#include <sub_wallets/sub_wallets.h>

#include <vector>

#include <wallet_types.h>

namespace send_transaction
{
    std::tuple<Error, crypto::Hash> sendFusionTransactionBasic(
        const std::shared_ptr<Nigel> daemon,
        const std::shared_ptr<SubWallets> subWallets);

    std::tuple<Error, crypto::Hash> sendFusionTransactionAdvanced(
        const uint64_t mixin,
        const std::vector<std::string> addressesToTakeFrom,
        std::string destination,
        const std::shared_ptr<Nigel> daemon,
        const std::shared_ptr<SubWallets> subWallets);

    std::tuple<Error, crypto::Hash> sendTransactionBasic(
        std::string destination,
        const uint64_t amount,
        std::string paymentID,
        const std::shared_ptr<Nigel> daemon,
        const std::shared_ptr<SubWallets> subWallets);

    std::tuple<Error, crypto::Hash> sendTransactionAdvanced(
        std::vector<std::pair<std::string, uint64_t>> addressesAndAmounts,
        const uint64_t mixin,
        const uint64_t fee,
        std::string paymentID,
        const std::vector<std::string> addressesToTakeFrom,
        std::string changeAddress,
        const std::shared_ptr<Nigel> daemon,
        const std::shared_ptr<SubWallets> subWallets,
        const uint64_t unlockTime);

    std::vector<wallet_types::TransactionDestination> setupDestinations(
        std::vector<std::pair<std::string, uint64_t>> addressesAndAmounts,
        const uint64_t changeRequired,
        const std::string changeAddress);

    std::tuple<Error, std::vector<wallet_types::ObscuredInput>> prepareRingParticipants(
        std::vector<wallet_types::TxInputAndOwner> sources,
        const uint64_t mixin,
        const std::shared_ptr<Nigel> daemon);

    std::tuple<Error, std::vector<mevacoin::KeyInput>, std::vector<crypto::SecretKey>> setupInputs(
        const std::vector<wallet_types::ObscuredInput> inputsAndFakes,
        const crypto::SecretKey privateViewKey);

    std::tuple<std::vector<wallet_types::KeyOutput>, mevacoin::KeyPair> setupOutputs(
        std::vector<wallet_types::TransactionDestination> destinations);

    std::tuple<Error, mevacoin::Transaction> generateRingSignatures(
        mevacoin::Transaction tx,
        const std::vector<wallet_types::ObscuredInput> inputsAndFakes,
        const std::vector<crypto::SecretKey> tmpSecretKeys);

    std::vector<uint64_t> splitAmountIntoDenominations(uint64_t amount);

    std::vector<mevacoin::TransactionInput> keyInputToTransactionInput(
        const std::vector<mevacoin::KeyInput> keyInputs);

    std::vector<mevacoin::TransactionOutput> keyOutputToTransactionOutput(
        const std::vector<wallet_types::KeyOutput> keyOutputs);

    crypto::Hash getTransactionHash(mevacoin::Transaction tx);

    std::tuple<Error, std::vector<mevacoin::RandomOuts>> getRingParticipants(
        const uint64_t mixin,
        const std::shared_ptr<Nigel> daemon,
        const std::vector<wallet_types::TxInputAndOwner> sources);

    struct TransactionResult
    {
        /* The error, if any */
        Error error;

        /* The raw transaction */
        mevacoin::Transaction transaction;

        /* The transaction outputs, before converted into boost uglyness, used
           for determining key inputs from the tx that belong to us */
        std::vector<wallet_types::KeyOutput> outputs;

        /* The random key pair we generated */
        mevacoin::KeyPair txKeyPair;
    };

    TransactionResult makeTransaction(
        const uint64_t mixin,
        const std::shared_ptr<Nigel> daemon,
        const std::vector<wallet_types::TxInputAndOwner> ourInputs,
        const std::string paymentID,
        const std::vector<wallet_types::TransactionDestination> destinations,
        const std::shared_ptr<SubWallets> subWallets,
        const uint64_t unlockTime);

    std::tuple<Error, crypto::Hash> relayTransaction(
        const mevacoin::Transaction tx,
        const std::shared_ptr<Nigel> daemon);

    std::tuple<mevacoin::KeyPair, crypto::KeyImage> genKeyImage(
        const wallet_types::ObscuredInput input,
        const crypto::SecretKey privateViewKey);

    void storeSentTransaction(
        const crypto::Hash hash,
        const uint64_t fee,
        const std::string paymentID,
        const std::vector<wallet_types::TxInputAndOwner> ourInputs,
        const std::string changeAddress,
        const uint64_t changeRequired,
        const std::shared_ptr<SubWallets> subWallets);

    Error isTransactionPayloadTooBig(
        const mevacoin::Transaction tx,
        const uint64_t currentHeight);

    void storeUnconfirmedIncomingInputs(
        const std::shared_ptr<SubWallets> subWallets,
        const std::vector<wallet_types::KeyOutput> keyOutputs,
        const crypto::PublicKey txPublicKey,
        const crypto::Hash txHash);

    /* Verify all amounts in the transaction given are PRETTY_AMOUNTS */
    bool verifyAmounts(const mevacoin::Transaction tx);

    /* Verify all amounts given are PRETTY_AMOUNTS */
    bool verifyAmounts(const std::vector<uint64_t> amounts);

    /* Verify fee is as expected */
    bool verifyTransactionFee(const uint64_t expectedFee, mevacoin::Transaction tx);
}
