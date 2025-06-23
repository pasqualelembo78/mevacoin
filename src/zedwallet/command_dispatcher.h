// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <zedwallet/types.h>

bool handleCommand(const std::string command,
                   std::shared_ptr<WalletInfo> walletInfo,
                   mevacoin::INode &node);

std::shared_ptr<WalletInfo> handleLaunchCommand(mevacoin::WalletGreen &wallet,
                                                std::string launchCommand,
                                                Config &config);
