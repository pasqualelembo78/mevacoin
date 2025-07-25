// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mevacoin
{
    class ICryptoNoteProtocolObserver;

    class ICryptoNoteProtocolQuery
    {
    public:
        virtual bool addObserver(ICryptoNoteProtocolObserver *observer) = 0;
        virtual bool removeObserver(ICryptoNoteProtocolObserver *observer) = 0;

        virtual uint32_t getObservedHeight() const = 0;
        virtual uint32_t getBlockchainHeight() const = 0;
        virtual size_t getPeerCount() const = 0;
        virtual bool isSynchronized() const = 0;
    };

} // namespace mevacoin
