// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#pragma once

#include <zedwallet/types.h>

void syncWallet(mevacoin::INode &node,
                std::shared_ptr<WalletInfo> walletInfo);

void checkForNewTransactions(std::shared_ptr<WalletInfo> walletInfo);
