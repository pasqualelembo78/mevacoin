// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#include "p2p_node_config.h"

#include <config/mevacoin_config.h>

namespace mevacoin
{

    namespace
    {

        const std::chrono::nanoseconds P2P_DEFAULT_CONNECT_INTERVAL = std::chrono::seconds(2);
        const size_t P2P_DEFAULT_CONNECT_RANGE = 20;
        const size_t P2P_DEFAULT_PEERLIST_GET_TRY_COUNT = 10;

    }

    P2pNodeConfig::P2pNodeConfig() : timedSyncInterval(std::chrono::seconds(P2P_DEFAULT_HANDSHAKE_INTERVAL)),
                                     handshakeTimeout(std::chrono::milliseconds(P2P_DEFAULT_HANDSHAKE_INVOKE_TIMEOUT)),
                                     connectInterval(P2P_DEFAULT_CONNECT_INTERVAL),
                                     connectTimeout(std::chrono::milliseconds(P2P_DEFAULT_CONNECTION_TIMEOUT)),
                                     networkId(mevacoin::MEVACOIN_NETWORK),
                                     expectedOutgoingConnectionsCount(P2P_DEFAULT_CONNECTIONS_COUNT),
                                     whiteListConnectionsPercent(P2P_DEFAULT_WHITELIST_CONNECTIONS_PERCENT),
                                     peerListConnectRange(P2P_DEFAULT_CONNECT_RANGE),
                                     peerListGetTryCount(P2P_DEFAULT_PEERLIST_GET_TRY_COUNT)
    {
    }

    // getters

    std::chrono::nanoseconds P2pNodeConfig::getTimedSyncInterval() const
    {
        return timedSyncInterval;
    }

    std::chrono::nanoseconds P2pNodeConfig::getHandshakeTimeout() const
    {
        return handshakeTimeout;
    }

    std::chrono::nanoseconds P2pNodeConfig::getConnectInterval() const
    {
        return connectInterval;
    }

    std::chrono::nanoseconds P2pNodeConfig::getConnectTimeout() const
    {
        return connectTimeout;
    }

    size_t P2pNodeConfig::getExpectedOutgoingConnectionsCount() const
    {
        return expectedOutgoingConnectionsCount;
    }

    size_t P2pNodeConfig::getWhiteListConnectionsPercent() const
    {
        return whiteListConnectionsPercent;
    }

    boost::uuids::uuid P2pNodeConfig::getNetworkId() const
    {
        if (getTestnet())
        {
            boost::uuids::uuid copy = networkId;
            copy.data[0] += 1;
            return copy;
        }
        return networkId;
    }

    size_t P2pNodeConfig::getPeerListConnectRange() const
    {
        return peerListConnectRange;
    }

    size_t P2pNodeConfig::getPeerListGetTryCount() const
    {
        return peerListGetTryCount;
    }

}
