// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#include "json_rpc.h"
#include "rpc/http_client.h"

namespace mevacoin
{

    namespace json_rpc
    {

        JsonRpcError::JsonRpcError() : code(0) {}

        JsonRpcError::JsonRpcError(int c) : code(c)
        {
            switch (c)
            {
            case errParseError:
                message = "Parse error";
                break;
            case errInvalidRequest:
                message = "Invalid request";
                break;
            case errMethodNotFound:
                message = "Method not found";
                break;
            case errInvalidParams:
                message = "Invalid params";
                break;
            case errInternalError:
                message = "Internal error";
                break;
            case errInvalidPassword:
                message = "Invalid or no password supplied";
                break;
            default:
                message = "Unknown error";
                break;
            }
        }

        JsonRpcError::JsonRpcError(int c, const std::string &msg) : code(c), message(msg)
        {
        }

        void invokeJsonRpcCommand(HttpClient &httpClient, JsonRpcRequest &jsReq, JsonRpcResponse &jsRes)
        {
            HttpRequest httpReq;
            HttpResponse httpRes;

            httpReq.addHeader("Content-Type", "application/json");
            httpReq.setUrl("/json_rpc");
            httpReq.setBody(jsReq.getBody());

            httpClient.request(httpReq, httpRes);

            if (httpRes.getStatus() != HttpResponse::STATUS_200)
            {
                throw std::runtime_error("JSON-RPC call failed, HTTP status = " + std::to_string(httpRes.getStatus()));
            }

            jsRes.parse(httpRes.getBody());

            JsonRpcError err;
            if (jsRes.getError(err))
            {
                throw err;
            }
        }

    }
}
