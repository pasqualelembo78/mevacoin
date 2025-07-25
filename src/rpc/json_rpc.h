// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <boost/optional.hpp>
#include <boost/foreach.hpp>
#include <functional>

#include "core_rpc_server_commands_definitions.h"
#include <common/json_value.h>
#include "serialization/iserializer.h"
#include "serialization/serialization_tools.h"

namespace mevacoin
{

    class HttpClient;

    namespace json_rpc
    {

        const int errParseError = -32700;
        const int errInvalidRequest = -32600;
        const int errMethodNotFound = -32601;
        const int errInvalidParams = -32602;
        const int errInternalError = -32603;
        const int errInvalidPassword = -32604;

        class JsonRpcError : public std::exception
        {
        public:
            JsonRpcError();
            JsonRpcError(int c);
            JsonRpcError(int c, const std::string &msg);

#ifdef _MSC_VER
            virtual const char *what() const override
            {
#else
            virtual const char *what() const noexcept override
            {
#endif
                return message.c_str();
            }

            void serialize(ISerializer &s)
            {
                s(code, "code");
                s(message, "message");
            }

            int code;
            std::string message;
        };

        typedef boost::optional<common::JsonValue> OptionalId;
        typedef boost::optional<common::JsonValue> OptionalPassword;

        class JsonRpcRequest
        {
        public:
            JsonRpcRequest() : psReq(common::JsonValue::OBJECT) {}

            bool parseRequest(const std::string &requestBody)
            {
                try
                {
                    psReq = common::JsonValue::fromString(requestBody);
                }
                catch (std::exception &)
                {
                    throw JsonRpcError(errParseError);
                }

                if (!psReq.contains("method"))
                {
                    throw JsonRpcError(errInvalidRequest);
                }

                method = psReq("method").getString();

                if (psReq.contains("id"))
                {
                    id = psReq("id");
                }

                if (psReq.contains("password"))
                {
                    password = psReq("password");
                }

                return true;
            }

            template <typename T>
            bool loadParams(T &v) const
            {
                loadFromJsonValue(v, psReq.contains("params") ? psReq("params") : common::JsonValue(common::JsonValue::NIL));
                return true;
            }

            template <typename T>
            bool setParams(const T &v)
            {
                psReq.set("params", storeToJsonValue(v));
                return true;
            }

            const std::string &getMethod() const
            {
                return method;
            }

            void setMethod(const std::string &m)
            {
                method = m;
            }

            const OptionalId &getId() const
            {
                return id;
            }

            const OptionalPassword &getPassword() const
            {
                return password;
            }

            std::string getBody()
            {
                psReq.set("jsonrpc", std::string("2.0"));
                psReq.set("method", method);
                return psReq.toString();
            }

        private:
            common::JsonValue psReq;
            OptionalId id;
            OptionalPassword password;
            std::string method;
        };

        class JsonRpcResponse
        {
        public:
            JsonRpcResponse() : psResp(common::JsonValue::OBJECT) {}

            void parse(const std::string &responseBody)
            {
                try
                {
                    psResp = common::JsonValue::fromString(responseBody);
                }
                catch (std::exception &)
                {
                    throw JsonRpcError(errParseError);
                }
            }

            void setId(const OptionalId &id)
            {
                if (id.is_initialized())
                {
                    psResp.insert("id", id.get());
                }
            }

            void setError(const JsonRpcError &err)
            {
                psResp.set("error", storeToJsonValue(err));
            }

            bool getError(JsonRpcError &err) const
            {
                if (!psResp.contains("error"))
                {
                    return false;
                }

                loadFromJsonValue(err, psResp("error"));
                return true;
            }

            std::string getBody()
            {
                psResp.set("jsonrpc", std::string("2.0"));
                return psResp.toString();
            }

            template <typename T>
            bool setResult(const T &v)
            {
                psResp.set("result", storeToJsonValue(v));
                return true;
            }

            template <typename T>
            bool getResult(T &v) const
            {
                if (!psResp.contains("result"))
                {
                    return false;
                }

                loadFromJsonValue(v, psResp("result"));
                return true;
            }

        private:
            common::JsonValue psResp;
        };

        void invokeJsonRpcCommand(HttpClient &httpClient, JsonRpcRequest &req, JsonRpcResponse &res);

        template <typename Request, typename Response>
        void invokeJsonRpcCommand(HttpClient &httpClient, const std::string &method, const Request &req, Response &res)
        {
            JsonRpcRequest jsReq;
            JsonRpcResponse jsRes;

            jsReq.setMethod(method);
            jsReq.setParams(req);

            invokeJsonRpcCommand(httpClient, jsReq, jsRes);

            jsRes.getResult(res);
        }

        template <typename Request, typename Response, typename Handler>
        bool invokeMethod(const JsonRpcRequest &jsReq, JsonRpcResponse &jsRes, Handler handler)
        {
            Request req;
            Response res;

            if (!std::is_same<Request, mevacoin::EMPTY_STRUCT>::value && !jsReq.loadParams(req))
            {
                throw JsonRpcError(json_rpc::errInvalidParams);
            }

            bool result = handler(req, res);

            if (result)
            {
                if (!jsRes.setResult(res))
                {
                    throw JsonRpcError(json_rpc::errInternalError);
                }
            }
            return result;
        }

        typedef std::function<bool(void *, const JsonRpcRequest &req, JsonRpcResponse &res)> JsonMemberMethod;

        template <typename Class, typename Params, typename Result>
        JsonMemberMethod makeMemberMethod(bool (Class::*handler)(const Params &, Result &))
        {
            return [handler](void *obj, const JsonRpcRequest &req, JsonRpcResponse &res)
            {
                return json_rpc::invokeMethod<Params, Result>(
                    req, res, std::bind(handler, static_cast<Class *>(obj), std::placeholders::_1, std::placeholders::_2));
            };
        }

    }

}
