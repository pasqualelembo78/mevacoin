// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <functional>
#include <unordered_map>

#include <boost/uuid/uuid.hpp>
#include <boost/functional/hash.hpp>

#include <syst/context.h>
#include <syst/context_group.h>
#include <syst/dispatcher.h>
#include <syst/event.h>
#include <syst/timer.h>
#include <syst/tcp_connection.h>
#include <syst/tcp_listener.h>

#include "mevacoin_core/once_in_interval.h"
#include "mevacoin_protocol/mevacoin_protocol_handler.h"
#include "logging/logger_ref.h"

#include "connection_context.h"
#include "levin_protocol.h"
#include "net_node_common.h"
#include "net_node_config.h"
#include "p2p_protocol_definitions.h"
#include "peer_list_manager.h"

namespace syst
{
    class TcpConnection;
}

namespace mevacoin
{
    class LevinProtocol;
    class ISerializer;

    struct P2pMessage
    {
        enum Type
        {
            COMMAND,
            REPLY,
            NOTIFY
        };

        P2pMessage(Type type, uint32_t command, const BinaryArray &buffer, int32_t returnCode = 0) : type(type), command(command), buffer(buffer), returnCode(returnCode)
        {
        }

        P2pMessage(P2pMessage &&msg) : type(msg.type), command(msg.command), buffer(std::move(msg.buffer)), returnCode(msg.returnCode)
        {
        }

        size_t size()
        {
            return buffer.size();
        }

        Type type;
        uint32_t command;
        const BinaryArray buffer;
        int32_t returnCode;
    };

    struct P2pConnectionContext : public CryptoNoteConnectionContext
    {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        syst::Context<void> *context;
        uint64_t peerId;
        syst::TcpConnection connection;

        P2pConnectionContext(syst::Dispatcher &dispatcher, std::shared_ptr<logging::ILogger> log, syst::TcpConnection &&conn) : context(nullptr),
                                                                                                                                peerId(0),
                                                                                                                                connection(std::move(conn)),
                                                                                                                                logger(log, "node_server"),
                                                                                                                                queueEvent(dispatcher),
                                                                                                                                stopped(false)
        {
        }

        P2pConnectionContext(P2pConnectionContext &&ctx) : CryptoNoteConnectionContext(std::move(ctx)),
                                                           context(ctx.context),
                                                           peerId(ctx.peerId),
                                                           connection(std::move(ctx.connection)),
                                                           logger(ctx.logger.getLogger(), "node_server"),
                                                           queueEvent(std::move(ctx.queueEvent)),
                                                           stopped(std::move(ctx.stopped))
        {
        }

        bool pushMessage(P2pMessage &&msg);
        std::vector<P2pMessage> popBuffer();
        void interrupt();

        uint64_t writeDuration(TimePoint now) const;

    private:
        logging::LoggerRef logger;
        TimePoint writeOperationStartTime;
        syst::Event queueEvent;
        std::vector<P2pMessage> writeQueue;
        size_t writeQueueSize = 0;
        bool stopped;
    };

    class NodeServer : public IP2pEndpoint
    {
    public:
        NodeServer(syst::Dispatcher &dispatcher, mevacoin::CryptoNoteProtocolHandler &payload_handler, std::shared_ptr<logging::ILogger> log);

        bool run();
        bool init(const NetNodeConfig &config);
        bool deinit();
        bool sendStopSignal();
        uint32_t get_this_peer_port() { return m_listeningPort; }
        mevacoin::CryptoNoteProtocolHandler &get_payload_object();

        void serialize(ISerializer &s);

        // debug functions
        bool log_peerlist();
        bool log_connections();
        virtual uint64_t get_connections_count() override;
        size_t get_outgoing_connections_count();

        PeerlistManager &getPeerlistManager() { return m_peerlist; }

    private:
        int handleCommand(const LevinProtocol::Command &cmd, BinaryArray &buff_out, P2pConnectionContext &context, bool &handled);

        //----------------- commands handlers ----------------------------------------------
        int handle_handshake(int command, COMMAND_HANDSHAKE::request &arg, COMMAND_HANDSHAKE::response &rsp, P2pConnectionContext &context);
        int handle_timed_sync(int command, COMMAND_TIMED_SYNC::request &arg, COMMAND_TIMED_SYNC::response &rsp, P2pConnectionContext &context);
        int handle_ping(int command, COMMAND_PING::request &arg, COMMAND_PING::response &rsp, P2pConnectionContext &context);
#ifdef ALLOW_DEBUG_COMMANDS
        int handle_get_stat_info(int command, COMMAND_REQUEST_STAT_INFO::request &arg, COMMAND_REQUEST_STAT_INFO::response &rsp, P2pConnectionContext &context);
        int handle_get_network_state(int command, COMMAND_REQUEST_NETWORK_STATE::request &arg, COMMAND_REQUEST_NETWORK_STATE::response &rsp, P2pConnectionContext &context);
        int handle_get_peer_id(int command, COMMAND_REQUEST_PEER_ID::request &arg, COMMAND_REQUEST_PEER_ID::response &rsp, P2pConnectionContext &context);
        bool check_trust(const proof_of_trust &tr);
#endif

        bool init_config();
        bool make_default_config();
        bool store_config();
        void initUpnp();

        bool handshake(mevacoin::LevinProtocol &proto, P2pConnectionContext &context, bool just_take_peerlist = false);
        bool timedSync();
        bool handleTimedSyncResponse(const BinaryArray &in, P2pConnectionContext &context);
        void forEachConnection(std::function<void(P2pConnectionContext &)> action);

        void on_connection_new(P2pConnectionContext &context);
        void on_connection_close(P2pConnectionContext &context);

        //----------------- i_p2p_endpoint -------------------------------------------------------------
        virtual void relay_notify_to_all(int command, const BinaryArray &data_buff, const boost::uuids::uuid *excludeConnection) override;
        virtual bool invoke_notify_to_peer(int command, const BinaryArray &req_buff, const CryptoNoteConnectionContext &context) override;
        virtual void for_each_connection(std::function<void(mevacoin::CryptoNoteConnectionContext &, uint64_t)> f) override;
        virtual void externalRelayNotifyToAll(int command, const BinaryArray &data_buff, const boost::uuids::uuid *excludeConnection) override;
        virtual void externalRelayNotifyToList(int command, const BinaryArray &data_buff, const std::list<boost::uuids::uuid> relayList) override;

        //-----------------------------------------------------------------------------------------------
        bool handleConfig(const NetNodeConfig &config);
        bool append_net_address(std::vector<NetworkAddress> &nodes, const std::string &addr);
        bool idle_worker();
        bool handle_remote_peerlist(const std::list<PeerlistEntry> &peerlist, time_t local_time, const CryptoNoteConnectionContext &context);
        bool get_local_node_data(basic_node_data &node_data);

        bool merge_peerlist_with_local(const std::list<PeerlistEntry> &bs);
        bool fix_time_delta(std::list<PeerlistEntry> &local_peerlist, time_t local_time, int64_t &delta);

        bool connections_maker();
        bool make_new_connection_from_peerlist(bool use_white_list);
        bool try_to_connect_and_handshake_with_new_peer(const NetworkAddress &na, bool just_take_peerlist = false, uint64_t last_seen_stamp = 0, bool white = true);
        bool is_peer_used(const PeerlistEntry &peer);
        bool is_addr_connected(const NetworkAddress &peer);
        bool try_ping(basic_node_data &node_data, P2pConnectionContext &context);
        bool make_expected_connections_count(bool white_list, size_t expected_connections);

        bool connect_to_peerlist(const std::vector<NetworkAddress> &peers);

        // debug functions
        std::string print_connections_container();

        typedef std::unordered_map<boost::uuids::uuid, P2pConnectionContext, boost::hash<boost::uuids::uuid>> ConnectionContainer;
        typedef ConnectionContainer::iterator ConnectionIterator;
        ConnectionContainer m_connections;

        void acceptLoop();
        void connectionHandler(const boost::uuids::uuid &connectionId, P2pConnectionContext &connection);
        void writeHandler(P2pConnectionContext &ctx);
        void onIdle();
        void timedSyncLoop();
        void timeoutLoop();

        template <typename T>
        void safeInterrupt(T &obj);

        struct config
        {
            network_config m_net_config;
            uint64_t m_peer_id;

            void serialize(ISerializer &s)
            {
                KV_MEMBER(m_net_config)
                KV_MEMBER(m_peer_id)
            }
        };

        config m_config;
        std::string m_config_folder;

        bool m_have_address;
        bool m_first_connection_maker_call;
        uint32_t m_listeningPort;
        uint32_t m_external_port;
        uint32_t m_ip_address;
        bool m_allow_local_ip;
        bool m_hide_my_port;
        std::string m_p2p_state_filename;

        syst::Dispatcher &m_dispatcher;
        syst::ContextGroup m_workingContextGroup;
        syst::Event m_stopEvent;
        syst::Timer m_idleTimer;
        syst::Timer m_timeoutTimer;
        syst::TcpListener m_listener;
        logging::LoggerRef logger;
        std::atomic<bool> m_stop;

        CryptoNoteProtocolHandler &m_payload_handler;
        PeerlistManager m_peerlist;

        // OnceInInterval m_peer_handshake_idle_maker_interval;
        OnceInInterval m_connections_maker_interval;
        OnceInInterval m_peerlist_store_interval;
        syst::Timer m_timedSyncTimer;

        std::string m_bind_ip;
        std::string m_port;
#ifdef ALLOW_DEBUG_COMMANDS
        uint64_t m_last_stat_request_time;
#endif
        std::vector<NetworkAddress> m_priority_peers;
        std::vector<NetworkAddress> m_exclusive_peers;
        std::vector<NetworkAddress> m_seed_nodes;
        std::list<PeerlistEntry> m_command_line_peers;
        uint64_t m_peer_livetime;
        boost::uuids::uuid m_network_id;
    };
}
