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

        enum class DataBaseErrorCodes : int
        {
            NOT_INITIALIZED = 1,
            ALREADY_INITIALIZED,
            INTERNAL_ERROR,
            IO_ERROR
        };

        class DataBaseErrorCategory : public std::error_category
        {
        public:
            static DataBaseErrorCategory INSTANCE;

            virtual const char *name() const throw() override
            {
                return "DataBaseErrorCategory";
            }

            virtual std::error_condition default_error_condition(int ev) const throw() override
            {
                return std::error_condition(ev, *this);
            }

            virtual std::string message(int ev) const override
            {
                switch (ev)
                {
                case static_cast<int>(DataBaseErrorCodes::NOT_INITIALIZED):
                    return "Object was not initialized";
                case static_cast<int>(DataBaseErrorCodes::ALREADY_INITIALIZED):
                    return "Object has been already initialized";
                case static_cast<int>(DataBaseErrorCodes::INTERNAL_ERROR):
                    return "Internal error";
                case static_cast<int>(DataBaseErrorCodes::IO_ERROR):
                    return "IO error";
                default:
                    return "Unknown error";
                }
            }

        private:
            DataBaseErrorCategory()
            {
            }
        };

    } // namespace error
} // namespace mevacoin

inline std::error_code make_error_code(mevacoin::error::DataBaseErrorCodes e)
{
    return std::error_code(static_cast<int>(e), mevacoin::error::DataBaseErrorCategory::INSTANCE);
}
