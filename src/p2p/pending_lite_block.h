// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#pragma once

#include <unordered_set>

#include "mevacoin_protocol/mevacoin_protocol_definitions.h"

namespace mevacoin
{
    struct PendingLiteBlock
    {
        NOTIFY_NEW_LITE_BLOCK_request request;
        std::unordered_set<crypto::Hash> missed_transactions;
    };
} // namespace mevacoin
