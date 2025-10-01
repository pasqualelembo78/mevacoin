// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#pragma once

#include <wallet/wallet_green.h>

bool fusionTX(mevacoin::WalletGreen &wallet,
              mevacoin::TransactionParameters p,
              uint64_t height);

bool optimize(mevacoin::WalletGreen &wallet, uint64_t threshold,
              uint64_t height);

void fullOptimize(mevacoin::WalletGreen &wallet, uint64_t height);

size_t makeFusionTransaction(mevacoin::WalletGreen &wallet,
                             uint64_t threshold, uint64_t height);
