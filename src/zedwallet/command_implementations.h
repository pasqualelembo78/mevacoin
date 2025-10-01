// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#pragma once

#include <zedwallet/types.h>

#include <wallet/wallet_green.h>

bool handleCommand(const std::string command,
                   std::shared_ptr<WalletInfo> walletInfo,
                   mevacoin::INode &node);

void changePassword(std::shared_ptr<WalletInfo> walletInfo);

void printPrivateKeys(mevacoin::WalletGreen &wallet, bool viewWallet);

void reset(mevacoin::INode &node, std::shared_ptr<WalletInfo> walletInfo);

void status(mevacoin::INode &node, mevacoin::WalletGreen &wallet);

void printHeights(uint32_t localHeight, uint32_t remoteHeight,
                  uint32_t walletHeight);

void printSyncStatus(uint32_t localHeight, uint32_t remoteHeight,
                     uint32_t walletHeight);

void printSyncSummary(uint32_t localHeight, uint32_t remoteHeight,
                      uint32_t walletHeight);

void printPeerCount(size_t peerCount);

void printHashrate(uint64_t difficulty);

void balance(mevacoin::INode &node, mevacoin::WalletGreen &wallet,
             bool viewWallet);

void exportKeys(std::shared_ptr<WalletInfo> walletInfo);

void saveCSV(mevacoin::WalletGreen &wallet, mevacoin::INode &node);

void save(mevacoin::WalletGreen &wallet);

void listTransfers(bool incoming, bool outgoing,
                   mevacoin::WalletGreen &wallet, mevacoin::INode &node);

void printOutgoingTransfer(mevacoin::WalletTransaction t,
                           mevacoin::INode &node);

void printIncomingTransfer(mevacoin::WalletTransaction t,
                           mevacoin::INode &node);

void createIntegratedAddress();

void help(std::shared_ptr<WalletInfo> wallet);

void advanced(std::shared_ptr<WalletInfo> wallet);
