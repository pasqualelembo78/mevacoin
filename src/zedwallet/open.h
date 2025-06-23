// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <zedwallet/types.h>

std::shared_ptr<WalletInfo> importFromKeys(mevacoin::WalletGreen &wallet,
                                           crypto::SecretKey privateSpendKey,
                                           crypto::SecretKey privateViewKey);

std::shared_ptr<WalletInfo> openWallet(mevacoin::WalletGreen &wallet,
                                       Config &config);

std::shared_ptr<WalletInfo> createViewWallet(mevacoin::WalletGreen &wallet);

std::shared_ptr<WalletInfo> importWallet(mevacoin::WalletGreen &wallet);

std::shared_ptr<WalletInfo> createViewWallet(mevacoin::WalletGreen &wallet);

std::shared_ptr<WalletInfo> mnemonicImportWallet(mevacoin::WalletGreen
                                                     &wallet);

std::shared_ptr<WalletInfo> generateWallet(mevacoin::WalletGreen &wallet);

crypto::SecretKey getPrivateKey(std::string outputMsg);

std::string getNewWalletFileName();

std::string getExistingWalletFileName(Config &config);

std::string getWalletPassword(bool verifyPwd, std::string msg);

bool isValidMnemonic(std::string &mnemonic_phrase,
                     crypto::SecretKey &private_spend_key);

void logIncorrectMnemonicWords(std::vector<std::string> words);

void viewWalletMsg();

void connectingMsg();

void promptSaveKeys(mevacoin::WalletGreen &wallet);
