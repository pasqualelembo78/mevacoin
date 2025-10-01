// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#pragma once

#include <zedwallet/types.h>

template <typename T>
std::string parseCommand(const std::vector<T> &printableCommands,
                         const std::vector<T> &availableCommands,
                         std::string prompt,
                         bool backgroundRefresh,
                         std::shared_ptr<WalletInfo> walletInfo);

std::tuple<bool, std::shared_ptr<WalletInfo>>
selectionScreen(Config &config, mevacoin::WalletGreen &wallet,
                mevacoin::INode &node);

bool checkNodeStatus(mevacoin::INode &node);

std::string getAction(Config &config);

void mainLoop(std::shared_ptr<WalletInfo> walletInfo, mevacoin::INode &node);

template <typename T>
void printCommands(const std::vector<T> &commands, size_t offset = 0);
