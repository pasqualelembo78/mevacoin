// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <string>
#include <system_error>

namespace mevacoin
{
    namespace error
    {

        enum HttpParserErrorCodes
        {
            STREAM_NOT_GOOD = 1,
            END_OF_STREAM,
            UNEXPECTED_SYMBOL,
            EMPTY_HEADER
        };

        // custom category:
        class HttpParserErrorCategory : public std::error_category
        {
        public:
            static HttpParserErrorCategory INSTANCE;

            virtual const char *name() const throw() override
            {
                return "HttpParserErrorCategory";
            }

            virtual std::error_condition default_error_condition(int ev) const throw() override
            {
                return std::error_condition(ev, *this);
            }

            virtual std::string message(int ev) const override
            {
                switch (ev)
                {
                case STREAM_NOT_GOOD:
                    return "The stream is not good";
                case END_OF_STREAM:
                    return "The stream is ended";
                case UNEXPECTED_SYMBOL:
                    return "Unexpected symbol";
                case EMPTY_HEADER:
                    return "The header name is empty";
                default:
                    return "Unknown error";
                }
            }

        private:
            HttpParserErrorCategory()
            {
            }
        };

    } // namespace error
} // namespace mevacoin

inline std::error_code make_error_code(mevacoin::error::HttpParserErrorCodes e)
{
    return std::error_code(static_cast<int>(e), mevacoin::error::HttpParserErrorCategory::INSTANCE);
}
