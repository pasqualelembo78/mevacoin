// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#include "p2p_node.h"

#include <boost/uuid/uuid_io.hpp>

#include <random>

#include <syst/context_group_timeout.h>
#include <syst/interrupted_exception.h>
#include <syst/ipv4_address.h>
#include <syst/operation_timeout.h>
#include <syst/tcp_connection.h>
#include <syst/tcp_connector.h>

#include <config/mevacoin_config.h>
#include <crypto/random.h>
#include "common/std_input_stream.h"
#include "common/std_output_stream.h"
#include "serialization/binary_input_stream_serializer.h"
#include "serialization/binary_output_stream_serializer.h"

#include "levin_protocol.h"
#include "p2p_connection_proxy.h"
#include "p2p_context.h"
#include "p2p_context_owner.h"

using namespace common;
using namespace logging;
using namespace syst;

namespace mevacoin
{

    namespace
    {

        class PeerIndexGenerator
        {
        public:
            PeerIndexGenerator(size_t maxIndex)
                : maxIndex(maxIndex), randCount(0)
            {
                assert(maxIndex > 0);
            }

            bool generate(size_t &num)
            {
                while (randCount < (maxIndex + 1) * 3)
                {
                    ++randCount;
                    auto idx = getRandomIndex();
                    if (visited.count(idx) == 0)
                    {
                        visited.insert(idx);
                        num = idx;
                        return true;
                    }
                }

                return false;
            }

        private:
            size_t getRandomIndex()
            {
                // divide by zero workaround
                if (maxIndex == 0)
                {
                    return 0;
                }

                size_t x = rnd::randomValue<size_t>() % (maxIndex + 1);
                return (x * x * x) / (maxIndex * maxIndex);
            }

            const size_t maxIndex;
            size_t randCount;
            std::set<size_t> visited;
        };

        NetworkAddress getRemoteAddress(const TcpConnection &connection)
        {
            auto addressAndPort = connection.getPeerAddressAndPort();
            NetworkAddress remoteAddress;
            remoteAddress.ip = hostToNetwork(addressAndPort.first.getValue());
            remoteAddress.port = addressAndPort.second;
            return remoteAddress;
        }

        void doWithTimeoutAndThrow(syst::Dispatcher &dispatcher, std::chrono::nanoseconds timeout, std::function<void()> f)
        {
            std::string result;
            syst::ContextGroup cg(dispatcher);
            syst::ContextGroupTimeout cgTimeout(dispatcher, cg, timeout);

            cg.spawn([&]
                     {
    try {
      f();
    } catch (syst::InterruptedException&) {
      result = "Operation timeout";
    } catch (std::exception& e) {
      result = e.what();
    } });

            cg.wait();

            if (!result.empty())
            {
                throw std::runtime_error(result);
            }
        }

    }

    P2pNode::P2pNode(const P2pNodeConfig &cfg, Dispatcher &dispatcher, std::shared_ptr<logging::ILogger> log, const crypto::Hash &genesisHash, uint64_t peerId) : logger(log, "P2pNode:" + std::to_string(cfg.getBindPort())),
                                                                                                                                                                  m_stopRequested(false),
                                                                                                                                                                  m_cfg(cfg),
                                                                                                                                                                  m_myPeerId(peerId),
                                                                                                                                                                  m_genesisPayload(CORE_SYNC_DATA{1, genesisHash}),
                                                                                                                                                                  m_dispatcher(dispatcher),
                                                                                                                                                                  workingContextGroup(dispatcher),
                                                                                                                                                                  m_connectorTimer(dispatcher),
                                                                                                                                                                  m_queueEvent(dispatcher)
    {
        m_peerlist.init(cfg.getAllowLocalIp());
        m_listener = TcpListener(m_dispatcher, Ipv4Address(m_cfg.getBindIp()), m_cfg.getBindPort());
        for (auto &peer : cfg.getPeers())
        {
            m_peerlist.append_with_peer_white(peer);
        }
    }

    P2pNode::~P2pNode()
    {
        assert(m_contexts.empty());
        assert(m_connectionQueue.empty());
    }

    void P2pNode::start()
    {
        workingContextGroup.spawn(std::bind(&P2pNode::acceptLoop, this));
        workingContextGroup.spawn(std::bind(&P2pNode::connectorLoop, this));
    }

    void P2pNode::stop()
    {
        if (m_stopRequested)
        {
            return; // already stopped
        }

        m_stopRequested = true;
        // clear prepared connections
        m_connectionQueue.clear();
        // stop processing
        m_queueEvent.set();
        workingContextGroup.interrupt();
        workingContextGroup.wait();
    }

    void P2pNode::serialize(ISerializer &s)
    {
        uint8_t version = 1;
        s(version, "version");

        if (version != 1)
        {
            return;
        }

        s(m_peerlist, "peerlist");
    }

    void P2pNode::save(std::ostream &os)
    {
        StdOutputStream stream(os);
        BinaryOutputStreamSerializer a(stream);
        mevacoin::serialize(*this, a);
    }

    void P2pNode::load(std::istream &in)
    {
        StdInputStream stream(in);
        BinaryInputStreamSerializer a(stream);
        mevacoin::serialize(*this, a);
    }

    void P2pNode::acceptLoop()
    {
        while (!m_stopRequested)
        {
            try
            {
                auto connection = m_listener.accept();
                auto ctx = new P2pContext(m_dispatcher, std::move(connection), true,
                                          getRemoteAddress(connection), m_cfg.getTimedSyncInterval(), getGenesisPayload());
                logger(INFO) << "Incoming connection from " << ctx->getRemoteAddress();
                workingContextGroup.spawn([this, ctx]
                                          { preprocessIncomingConnection(ContextPtr(ctx)); });
            }
            catch (InterruptedException &)
            {
                break;
            }
            catch (const std::exception &e)
            {
                logger(DEBUGGING) << "Exception in acceptLoop: " << e.what();
            }
        }

        logger(DEBUGGING) << "acceptLoop finished";
    }

    void P2pNode::connectorLoop()
    {
        while (!m_stopRequested)
        {
            try
            {
                connectPeers();
                m_connectorTimer.sleep(m_cfg.getConnectInterval());
            }
            catch (InterruptedException &)
            {
                break;
            }
            catch (const std::exception &e)
            {
                logger(WARNING) << "Exception in connectorLoop: " << e.what();
            }
        }
    }

    void P2pNode::connectPeers()
    {
        if (!m_cfg.getExclusiveNodes().empty())
        {
            connectPeerList(m_cfg.getExclusiveNodes());
            return;
        }

        // if white peer list is empty, get peers from seeds
        if (m_peerlist.get_white_peers_count() == 0 && !m_cfg.getSeedNodes().empty())
        {
            auto seedNodes = m_cfg.getSeedNodes();
            std::shuffle(seedNodes.begin(), seedNodes.end(), std::random_device{});
            for (const auto &seed : seedNodes)
            {
                auto conn = tryToConnectPeer(seed);
                if (conn != nullptr && fetchPeerList(std::move(conn)))
                {
                    break;
                }
            }
        }

        connectPeerList(m_cfg.getPriorityNodes());

        const size_t totalExpectedConnectionsCount = m_cfg.getExpectedOutgoingConnectionsCount();
        const size_t expectedWhiteConnections = (totalExpectedConnectionsCount * m_cfg.getWhiteListConnectionsPercent()) / 100;
        const size_t outgoingConnections = getOutgoingConnectionsCount();

        if (outgoingConnections < totalExpectedConnectionsCount)
        {
            if (outgoingConnections < expectedWhiteConnections)
            {
                // start from white list
                makeExpectedConnectionsCount(m_peerlist.getWhite(), expectedWhiteConnections);
                // and then do grey list
                makeExpectedConnectionsCount(m_peerlist.getGray(), totalExpectedConnectionsCount);
            }
            else
            {
                // start from grey list
                makeExpectedConnectionsCount(m_peerlist.getGray(), totalExpectedConnectionsCount);
                // and then do white list
                makeExpectedConnectionsCount(m_peerlist.getWhite(), totalExpectedConnectionsCount);
            }
        }
    }

    void P2pNode::makeExpectedConnectionsCount(const Peerlist &peerlist, size_t connectionsCount)
    {
        while (getOutgoingConnectionsCount() < connectionsCount)
        {
            if (peerlist.count() == 0)
            {
                return;
            }

            if (!makeNewConnectionFromPeerlist(peerlist))
            {
                break;
            }
        }
    }

    bool P2pNode::makeNewConnectionFromPeerlist(const Peerlist &peerlist)
    {
        size_t peerIndex;
        PeerIndexGenerator idxGen(std::min<uint64_t>(peerlist.count() - 1, m_cfg.getPeerListConnectRange()));

        for (size_t tryCount = 0; idxGen.generate(peerIndex) && tryCount < m_cfg.getPeerListGetTryCount(); ++tryCount)
        {
            PeerlistEntry peer;
            if (!peerlist.get(peer, peerIndex))
            {
                logger(WARNING) << "Failed to get peer from list, idx = " << peerIndex;
                continue;
            }

            if (isPeerUsed(peer))
            {
                continue;
            }

            logger(DEBUGGING) << "Selected peer: [" << peer.id << " " << peer.adr << "] last_seen: " << (peer.last_seen ? common::timeIntervalToString(time(NULL) - peer.last_seen) : "never");

            auto conn = tryToConnectPeer(peer.adr);
            if (conn.get())
            {
                enqueueConnection(createProxy(std::move(conn)));
                return true;
            }
        }

        return false;
    }

    void P2pNode::preprocessIncomingConnection(ContextPtr ctx)
    {
        try
        {
            logger(DEBUGGING) << *ctx << "preprocessIncomingConnection";

            OperationTimeout<P2pContext> timeout(m_dispatcher, *ctx, m_cfg.getHandshakeTimeout());

            // create proxy and process handshake
            auto proxy = createProxy(std::move(ctx));
            if (proxy->processIncomingHandshake())
            {
                enqueueConnection(std::move(proxy));
            }
        }
        catch (std::exception &e)
        {
            logger(WARNING) << " Failed to process connection: " << e.what();
        }
    }

    void P2pNode::connectPeerList(const std::vector<NetworkAddress> &peers)
    {
        for (const auto &address : peers)
        {
            if (!isPeerConnected(address))
            {
                auto conn = tryToConnectPeer(address);
                if (conn != nullptr)
                {
                    enqueueConnection(createProxy(std::move(conn)));
                }
            }
        }
    }

    bool P2pNode::isPeerConnected(const NetworkAddress &address)
    {
        for (const auto &c : m_contexts)
        {
            if (!c->isIncoming() && c->getRemoteAddress() == address)
            {
                return true;
            }
        }

        return false;
    }

    bool P2pNode::isPeerUsed(const PeerlistEntry &peer)
    {
        if (m_myPeerId == peer.id)
        {
            return true; // dont make connections to ourself
        }

        for (const auto &c : m_contexts)
        {
            if (c->getPeerId() == peer.id || (!c->isIncoming() && c->getRemoteAddress() == peer.adr))
            {
                return true;
            }
        }

        return false;
    }

    P2pNode::ContextPtr P2pNode::tryToConnectPeer(const NetworkAddress &address)
    {
        try
        {
            TcpConnector connector(m_dispatcher);
            TcpConnection tcpConnection;

            doWithTimeoutAndThrow(m_dispatcher, m_cfg.getConnectTimeout(), [&]
                                  { tcpConnection = connector.connect(
                                        Ipv4Address(common::ipAddressToString(address.ip)),
                                        static_cast<uint16_t>(address.port)); });

            logger(DEBUGGING) << "connection established to " << address;

            return ContextPtr(new P2pContext(m_dispatcher, std::move(tcpConnection), false, address, m_cfg.getTimedSyncInterval(), getGenesisPayload()));
        }
        catch (std::exception &e)
        {
            logger(DEBUGGING) << "Connection to " << address << " failed: " << e.what();
        }

        return ContextPtr();
    }

    bool P2pNode::fetchPeerList(ContextPtr connection)
    {
        try
        {
            COMMAND_HANDSHAKE::request request{getNodeData(), getGenesisPayload()};
            COMMAND_HANDSHAKE::response response;

            OperationTimeout<P2pContext> timeout(m_dispatcher, *connection, m_cfg.getHandshakeTimeout());

            connection->writeMessage(makeRequest(COMMAND_HANDSHAKE::ID, LevinProtocol::encode(request)));

            LevinProtocol::Command cmd;
            if (!connection->readCommand(cmd))
            {
                throw std::runtime_error("Connection closed unexpectedly");
            }

            if (!cmd.isResponse || cmd.command != COMMAND_HANDSHAKE::ID)
            {
                throw std::runtime_error("Received unexpected reply");
            }

            if (!LevinProtocol::decode(cmd.buf, response))
            {
                throw std::runtime_error("Invalid reply format");
            }

            if (response.node_data.network_id != request.node_data.network_id)
            {
                logger(DEBUGGING) << *connection << "COMMAND_HANDSHAKE failed, wrong network: " << response.node_data.network_id;
                return false;
            }

            if (response.node_data.version < mevacoin::P2P_MINIMUM_VERSION)
            {
                logger(DEBUGGING) << *connection << "COMMAND_HANDSHAKE Failed, peer is wrong version: " << std::to_string(response.node_data.version);
                return false;
            }
            else if ((response.node_data.version - mevacoin::P2P_CURRENT_VERSION) >= mevacoin::P2P_UPGRADE_WINDOW)
            {
                logger(WARNING) << *connection << "COMMAND_HANDSHAKE Warning, your software may be out of date. Please visit: "
                                << mevacoin::LATEST_VERSION_URL << " for the latest version.";
            }

            return handleRemotePeerList(response.local_peerlist, response.node_data.local_time);
        }
        catch (std::exception &e)
        {
            logger(INFO) << *connection << "Failed to obtain peer list: " << e.what();
        }

        return false;
    }

    namespace
    {

        std::list<PeerlistEntry> fixTimeDelta(const std::list<PeerlistEntry> &peerlist, time_t remoteTime)
        {
            // fix time delta
            int64_t delta = time(nullptr) - remoteTime;
            std::list<PeerlistEntry> peerlistCopy(peerlist);

            for (PeerlistEntry &be : peerlistCopy)
            {
                if (be.last_seen > uint64_t(remoteTime))
                {
                    throw std::runtime_error("Invalid peerlist entry (time in future)");
                }

                be.last_seen += delta;
            }

            return peerlistCopy;
        }
    }

    bool P2pNode::handleRemotePeerList(const std::list<PeerlistEntry> &peerlist, time_t remoteTime)
    {
        return m_peerlist.merge_peerlist(fixTimeDelta(peerlist, remoteTime));
    }

    const CORE_SYNC_DATA &P2pNode::getGenesisPayload() const
    {
        return m_genesisPayload;
    }

    std::list<PeerlistEntry> P2pNode::getLocalPeerList()
    {
        std::list<PeerlistEntry> peerlist;
        m_peerlist.get_peerlist_head(peerlist);
        return peerlist;
    }

    basic_node_data P2pNode::getNodeData() const
    {
        basic_node_data nodeData;
        nodeData.network_id = m_cfg.getNetworkId();
        nodeData.version = mevacoin::P2P_CURRENT_VERSION;
        nodeData.local_time = time(nullptr);
        nodeData.peer_id = m_myPeerId;

        if (m_cfg.getHideMyPort())
        {
            nodeData.my_port = 0;
        }
        else
        {
            nodeData.my_port = m_cfg.getExternalPort() ? m_cfg.getExternalPort() : m_cfg.getBindPort();
        }

        return nodeData;
    }

    uint64_t P2pNode::getPeerId() const
    {
        return m_myPeerId;
    }

    size_t P2pNode::getOutgoingConnectionsCount() const
    {
        size_t count = 0;

        for (const auto &ctx : m_contexts)
        {
            if (!ctx->isIncoming())
            {
                ++count;
            }
        }

        return count;
    }

    std::unique_ptr<P2pConnectionProxy> P2pNode::createProxy(ContextPtr ctx)
    {
        return std::unique_ptr<P2pConnectionProxy>(
            new P2pConnectionProxy(P2pContextOwner(ctx.release(), m_contexts), *this));
    }

    void P2pNode::enqueueConnection(std::unique_ptr<P2pConnectionProxy> proxy)
    {
        if (m_stopRequested)
        {
            return; // skip operation
        }

        m_connectionQueue.push_back(std::move(proxy));
        m_queueEvent.set();
    }

    void P2pNode::tryPing(P2pContext &ctx)
    {
        if (ctx.getPeerId() == m_myPeerId || !m_peerlist.is_ip_allowed(ctx.getRemoteAddress().ip) || ctx.getPeerPort() == 0)
        {
            return;
        }

        NetworkAddress peerAddress;
        peerAddress.ip = ctx.getRemoteAddress().ip;
        peerAddress.port = ctx.getPeerPort();

        try
        {
            TcpConnector connector(m_dispatcher);
            TcpConnection connection;

            doWithTimeoutAndThrow(m_dispatcher, m_cfg.getConnectTimeout(), [&]
                                  { connection = connector.connect(Ipv4Address(common::ipAddressToString(peerAddress.ip)), static_cast<uint16_t>(peerAddress.port)); });

            doWithTimeoutAndThrow(m_dispatcher, m_cfg.getHandshakeTimeout(), [&]
                                  {
      LevinProtocol proto(connection);
      COMMAND_PING::request request;
      COMMAND_PING::response response;
      proto.invoke(COMMAND_PING::ID, request, response);

      if (response.status == PING_OK_RESPONSE_STATUS_TEXT && response.peer_id == ctx.getPeerId()) {
        PeerlistEntry entry;
        entry.adr = peerAddress;
        entry.id = ctx.getPeerId();
        entry.last_seen = time(nullptr);
        m_peerlist.append_with_peer_white(entry);
      } else {
        logger(logging::DEBUGGING) << ctx << "back ping invoke wrong response \"" << response.status << "\" from"
          << peerAddress << ", expected peerId=" << ctx.getPeerId() << ", got " << response.peer_id;
      } });
        }
        catch (std::exception &e)
        {
            logger(DEBUGGING) << "Ping to " << peerAddress << " failed: " << e.what();
        }
    }

    void P2pNode::handleNodeData(const basic_node_data &node, P2pContext &context)
    {
        if (node.network_id != m_cfg.getNetworkId())
        {
            std::ostringstream msg;
            msg << context << "COMMAND_HANDSHAKE Failed, wrong network!  (" << node.network_id << ")";
            throw std::runtime_error(msg.str());
        }

        if (node.version < mevacoin::P2P_MINIMUM_VERSION)
        {
            std::ostringstream msg;
            msg << context << "COMMAND_HANDSHAKE Failed, peer is wrong version! (" << std::to_string(node.version) << ")";
            throw std::runtime_error(msg.str());
        }

        if (node.peer_id == m_myPeerId)
        {
            throw std::runtime_error("Connection to self detected");
        }

        context.setPeerInfo(node.version, node.peer_id, static_cast<uint16_t>(node.my_port));
        if (!context.isIncoming())
        {
            m_peerlist.set_peer_just_seen(node.peer_id, context.getRemoteAddress());
        }
    }

}
