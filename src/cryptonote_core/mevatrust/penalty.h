#pragma once

#include <cstdint>
#include <string>
#include "crypto/crypto.h"

namespace cryptonote { namespace mevatrust {

enum class OffenseType : uint8_t {
    UPTIME_VIOLATION     = 0,
    CHALLENGE_FAILURE     = 1,
    SYNC_FAILURE          = 2,
    DOUBLE_REGISTRATION  = 3,
    BYZANTINE_BEHAVIOR   = 4,
    MALICIOUS_ACTIVITY   = 5
};

std::string offense_type_to_string(OffenseType t);

bool apply_penalty(
    const crypto::hash& node_id,
    OffenseType offense,
    uint64_t height
);

}} // namespace
