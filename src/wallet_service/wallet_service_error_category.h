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

        enum class WalletServiceErrorCode
        {
            WRONG_KEY_FORMAT = 1,
            WRONG_PAYMENT_ID_FORMAT,
            WRONG_HASH_FORMAT,
            OBJECT_NOT_FOUND,
            DUPLICATE_KEY,
            KEYS_NOT_DETERMINISTIC,
        };

        // custom category:
        class WalletServiceErrorCategory : public std::error_category
        {
        public:
            static WalletServiceErrorCategory INSTANCE;

            virtual const char *name() const throw() override
            {
                return "WalletServiceErrorCategory";
            }

            virtual std::error_condition default_error_condition(int ev) const throw() override
            {
                return std::error_condition(ev, *this);
            }

            virtual std::string message(int ev) const override
            {
                WalletServiceErrorCode code = static_cast<WalletServiceErrorCode>(ev);

                switch (code)
                {
                case WalletServiceErrorCode::WRONG_KEY_FORMAT:
                    return "Wrong key format";
                case WalletServiceErrorCode::WRONG_PAYMENT_ID_FORMAT:
                    return "Wrong payment id format";
                case WalletServiceErrorCode::WRONG_HASH_FORMAT:
                    return "Wrong block id format";
                case WalletServiceErrorCode::OBJECT_NOT_FOUND:
                    return "Requested object not found";
                case WalletServiceErrorCode::DUPLICATE_KEY:
                    return "Duplicate key";
                case WalletServiceErrorCode::KEYS_NOT_DETERMINISTIC:
                    return "Keys not deterministic";
                default:
                    return "Unknown error";
                }
            }

        private:
            WalletServiceErrorCategory()
            {
            }
        };

    } // namespace error
} // namespace mevacoin

inline std::error_code make_error_code(mevacoin::error::WalletServiceErrorCode e)
{
    return std::error_code(static_cast<int>(e), mevacoin::error::WalletServiceErrorCategory::INSTANCE);
}

namespace std
{

    template <>
    struct is_error_code_enum<mevacoin::error::WalletServiceErrorCode> : public true_type
    {
    };

}
