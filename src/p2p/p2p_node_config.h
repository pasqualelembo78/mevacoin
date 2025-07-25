// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <boost/uuid/uuid.hpp>
#include <chrono>
#include "net_node_config.h"

namespace mevacoin
{

    class P2pNodeConfig : public NetNodeConfig
    {
    public:
        P2pNodeConfig();

        // getters
        std::chrono::nanoseconds getTimedSyncInterval() const;
        std::chrono::nanoseconds getHandshakeTimeout() const;
        std::chrono::nanoseconds getConnectInterval() const;
        std::chrono::nanoseconds getConnectTimeout() const;
        size_t getExpectedOutgoingConnectionsCount() const;
        size_t getWhiteListConnectionsPercent() const;
        boost::uuids::uuid getNetworkId() const;
        size_t getPeerListConnectRange() const;
        size_t getPeerListGetTryCount() const;

    private:
        std::chrono::nanoseconds timedSyncInterval;
        std::chrono::nanoseconds handshakeTimeout;
        std::chrono::nanoseconds connectInterval;
        std::chrono::nanoseconds connectTimeout;
        boost::uuids::uuid networkId;
        size_t expectedOutgoingConnectionsCount;
        size_t whiteListConnectionsPercent;
        size_t peerListConnectRange;
        size_t peerListGetTryCount;
    };

}
