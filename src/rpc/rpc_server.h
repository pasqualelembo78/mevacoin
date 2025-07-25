// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018, The TurtleCoin Developers
// Copyright (c) 2018, The Karai Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include "http_server.h"

#include <functional>
#include <unordered_map>

#include <logging/logger_ref.h>
#include "common/math.h"
#include "core_rpc_server_commands_definitions.h"
#include "json_rpc.h"

namespace mevacoin
{

    class Core;
    class NodeServer;
    struct ICryptoNoteProtocolHandler;

    class RpcServer : public HttpServer
    {
    public:
        RpcServer(syst::Dispatcher &dispatcher, std::shared_ptr<logging::ILogger> log, Core &c, NodeServer &p2p, ICryptoNoteProtocolHandler &protocol);

        typedef std::function<bool(RpcServer *, const HttpRequest &request, HttpResponse &response)> HandlerFunction;
        bool enableCors(const std::vector<std::string> domains);
        bool setFeeAddress(const std::string fee_address);
        bool setFeeAmount(const uint32_t fee_amount);
        std::vector<std::string> getCorsDomains();

        bool on_get_block_headers_range(const COMMAND_RPC_GET_BLOCK_HEADERS_RANGE::request &req, COMMAND_RPC_GET_BLOCK_HEADERS_RANGE::response &res, json_rpc::JsonRpcError &error_resp);
        bool on_get_info(const COMMAND_RPC_GET_INFO::request &req, COMMAND_RPC_GET_INFO::response &res);

    private:
        template <class Handler>
        struct RpcHandler
        {
            const Handler handler;
            const bool allowBusyCore;
        };

        typedef void (RpcServer::*HandlerPtr)(const HttpRequest &request, HttpResponse &response);
        static std::unordered_map<std::string, RpcHandler<HandlerFunction>> s_handlers;

        virtual void processRequest(const HttpRequest &request, HttpResponse &response) override;
        bool processJsonRpcRequest(const HttpRequest &request, HttpResponse &response);
        bool isCoreReady();

        // json handlers
        bool on_get_blocks(const COMMAND_RPC_GET_BLOCKS_FAST::request &req, COMMAND_RPC_GET_BLOCKS_FAST::response &res);
        bool on_query_blocks(const COMMAND_RPC_QUERY_BLOCKS::request &req, COMMAND_RPC_QUERY_BLOCKS::response &res);
        bool on_query_blocks_lite(const COMMAND_RPC_QUERY_BLOCKS_LITE::request &req, COMMAND_RPC_QUERY_BLOCKS_LITE::response &res);
        bool on_query_blocks_detailed(const COMMAND_RPC_QUERY_BLOCKS_DETAILED::request &req, COMMAND_RPC_QUERY_BLOCKS_DETAILED::response &res);
        bool on_get_wallet_sync_data(const COMMAND_RPC_GET_WALLET_SYNC_DATA::request &req, COMMAND_RPC_GET_WALLET_SYNC_DATA::response &res);
        bool on_get_indexes(const COMMAND_RPC_GET_TX_GLOBAL_OUTPUTS_INDEXES::request &req, COMMAND_RPC_GET_TX_GLOBAL_OUTPUTS_INDEXES::response &res);

        bool onGetTransactionsStatus(
            const COMMAND_RPC_GET_TRANSACTIONS_STATUS::request &req,
            COMMAND_RPC_GET_TRANSACTIONS_STATUS::response &res);

        bool onGetGlobalIndexesForRange(
            const COMMAND_RPC_GET_GLOBAL_INDEXES_FOR_RANGE::request &req,
            COMMAND_RPC_GET_GLOBAL_INDEXES_FOR_RANGE::response &res);

        bool on_get_random_outs(const COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::request &req, COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::response &res);
        bool onGetPool(const COMMAND_RPC_GET_POOL::request &req, COMMAND_RPC_GET_POOL::response &rsp);
        bool onGetPoolChanges(const COMMAND_RPC_GET_POOL_CHANGES::request &req, COMMAND_RPC_GET_POOL_CHANGES::response &rsp);
        bool onGetPoolChangesLite(const COMMAND_RPC_GET_POOL_CHANGES_LITE::request &req, COMMAND_RPC_GET_POOL_CHANGES_LITE::response &rsp);
        bool onGetBlocksDetailsByHeights(const COMMAND_RPC_GET_BLOCKS_DETAILS_BY_HEIGHTS::request &req, COMMAND_RPC_GET_BLOCKS_DETAILS_BY_HEIGHTS::response &rsp);
        bool onGetBlocksDetailsByHashes(const COMMAND_RPC_GET_BLOCKS_DETAILS_BY_HASHES::request &req, COMMAND_RPC_GET_BLOCKS_DETAILS_BY_HASHES::response &rsp);
        bool onGetBlockDetailsByHeight(const COMMAND_RPC_GET_BLOCK_DETAILS_BY_HEIGHT::request &req, COMMAND_RPC_GET_BLOCK_DETAILS_BY_HEIGHT::response &rsp);
        bool onGetBlocksHashesByTimestamps(const COMMAND_RPC_GET_BLOCKS_HASHES_BY_TIMESTAMPS::request &req, COMMAND_RPC_GET_BLOCKS_HASHES_BY_TIMESTAMPS::response &rsp);
        bool onGetTransactionDetailsByHashes(const COMMAND_RPC_GET_TRANSACTION_DETAILS_BY_HASHES::request &req, COMMAND_RPC_GET_TRANSACTION_DETAILS_BY_HASHES::response &rsp);
        bool onGetTransactionHashesByPaymentId(const COMMAND_RPC_GET_TRANSACTION_HASHES_BY_PAYMENT_ID::request &req, COMMAND_RPC_GET_TRANSACTION_HASHES_BY_PAYMENT_ID::response &rsp);
        bool on_get_height(const COMMAND_RPC_GET_HEIGHT::request &req, COMMAND_RPC_GET_HEIGHT::response &res);
        bool on_get_transactions(const COMMAND_RPC_GET_TRANSACTIONS::request &req, COMMAND_RPC_GET_TRANSACTIONS::response &res);
        bool on_send_raw_tx(const COMMAND_RPC_SEND_RAW_TX::request &req, COMMAND_RPC_SEND_RAW_TX::response &res);
        bool on_get_fee_info(const COMMAND_RPC_GET_FEE_ADDRESS::request &req, COMMAND_RPC_GET_FEE_ADDRESS::response &res);
        bool on_get_peers(const COMMAND_RPC_GET_PEERS::request &req, COMMAND_RPC_GET_PEERS::response &res);

        // json rpc
        bool on_getblockcount(const COMMAND_RPC_GETBLOCKCOUNT::request &req, COMMAND_RPC_GETBLOCKCOUNT::response &res);
        bool on_getblockhash(const COMMAND_RPC_GETBLOCKHASH::request &req, COMMAND_RPC_GETBLOCKHASH::response &res);
        bool on_getblocktemplate(const COMMAND_RPC_GETBLOCKTEMPLATE::request &req, COMMAND_RPC_GETBLOCKTEMPLATE::response &res);
        bool on_get_currency_id(const COMMAND_RPC_GET_CURRENCY_ID::request &req, COMMAND_RPC_GET_CURRENCY_ID::response &res);
        bool on_submitblock(const COMMAND_RPC_SUBMITBLOCK::request &req, COMMAND_RPC_SUBMITBLOCK::response &res);
        bool on_get_last_block_header(const COMMAND_RPC_GET_LAST_BLOCK_HEADER::request &req, COMMAND_RPC_GET_LAST_BLOCK_HEADER::response &res);
        bool on_get_block_header_by_hash(const COMMAND_RPC_GET_BLOCK_HEADER_BY_HASH::request &req, COMMAND_RPC_GET_BLOCK_HEADER_BY_HASH::response &res);
        bool on_get_block_header_by_height(const COMMAND_RPC_GET_BLOCK_HEADER_BY_HEIGHT::request &req, COMMAND_RPC_GET_BLOCK_HEADER_BY_HEIGHT::response &res);

        void fill_block_header_response(const BlockTemplate &blk, bool orphan_status, uint32_t index, const crypto::Hash &hash, block_header_response &responce);
        RawBlockLegacy prepareRawBlockLegacy(BinaryArray &&blockBlob);

        bool f_on_blocks_list_json(const F_COMMAND_RPC_GET_BLOCKS_LIST::request &req, F_COMMAND_RPC_GET_BLOCKS_LIST::response &res);
        bool f_on_block_json(const F_COMMAND_RPC_GET_BLOCK_DETAILS::request &req, F_COMMAND_RPC_GET_BLOCK_DETAILS::response &res);
        bool f_on_transaction_json(const F_COMMAND_RPC_GET_TRANSACTION_DETAILS::request &req, F_COMMAND_RPC_GET_TRANSACTION_DETAILS::response &res);
        bool f_on_transactions_pool_json(const F_COMMAND_RPC_GET_POOL::request &req, F_COMMAND_RPC_GET_POOL::response &res);
        bool f_getMixin(const Transaction &transaction, uint64_t &mixin);

        logging::LoggerRef logger;
        Core &m_core;
        NodeServer &m_p2p;
        ICryptoNoteProtocolHandler &m_protocol;
        std::vector<std::string> m_cors_domains;
        std::string m_fee_address;
        uint32_t m_fee_amount;
    };

}
