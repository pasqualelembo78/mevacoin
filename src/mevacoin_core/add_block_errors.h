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

        enum class AddBlockErrorCode
        {
            ADDED_TO_MAIN = 1,
            ADDED_TO_ALTERNATIVE,
            ADDED_TO_ALTERNATIVE_AND_SWITCHED,
            ALREADY_EXISTS,
            REJECTED_AS_ORPHANED,
            DESERIALIZATION_FAILED
        };

        // custom category:
        class AddBlockErrorCategory : public std::error_category
        {
        public:
            static AddBlockErrorCategory INSTANCE;

            virtual const char *name() const throw()
            {
                return "AddBlockErrorCategory";
            }

            virtual std::error_condition default_error_condition(int ev) const throw()
            {
                return std::error_condition(ev, *this);
            }

            virtual std::string message(int ev) const
            {
                AddBlockErrorCode code = static_cast<AddBlockErrorCode>(ev);

                switch (code)
                {
                case AddBlockErrorCode::ADDED_TO_MAIN:
                    return "Block added to main chain";
                case AddBlockErrorCode::ADDED_TO_ALTERNATIVE:
                    return "Block added to alternative chain";
                case AddBlockErrorCode::ADDED_TO_ALTERNATIVE_AND_SWITCHED:
                    return "Chain switched";
                case AddBlockErrorCode::ALREADY_EXISTS:
                    return "Block already exists";
                case AddBlockErrorCode::REJECTED_AS_ORPHANED:
                    return "Block rejected as orphaned";
                case AddBlockErrorCode::DESERIALIZATION_FAILED:
                    return "Deserialization error";
                default:
                    return "Unknown error";
                }
            }

        private:
            AddBlockErrorCategory()
            {
            }
        };

        inline std::error_code make_error_code(mevacoin::error::AddBlockErrorCode e)
        {
            return std::error_code(static_cast<int>(e), mevacoin::error::AddBlockErrorCategory::INSTANCE);
        }

    }
}

namespace std
{

    template <>
    struct is_error_code_enum<mevacoin::error::AddBlockErrorCode> : public true_type
    {
    };

}
