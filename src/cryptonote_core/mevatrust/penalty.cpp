#include "penalty.h"
#include "node_registry.h"
#include "badge_system.h"
#include "mevatrust_manager.h"
#include "misc_log_ex.h"
#include "string_tools.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "mevatrust.penalty"

namespace cryptonote { namespace mevatrust {

std::string offense_type_to_string(OffenseType t) {
    switch (t) {
        case OffenseType::UPTIME_VIOLATION:    return "uptime_violation";
        case OffenseType::CHALLENGE_FAILURE:     return "challenge_failure";
        case OffenseType::SYNC_FAILURE:          return "sync_failure";
        case OffenseType::DOUBLE_REGISTRATION:  return "double_registration";
        case OffenseType::BYZANTINE_BEHAVIOR:   return "byzantine_behavior";
        case OffenseType::MALICIOUS_ACTIVITY:   return "malicious_activity";
        default: return "unknown";
    }
}

bool apply_penalty(
    const crypto::hash& node_id,
    OffenseType offense,
    uint64_t height)
{
    float delta = 0.0f;
    NodeStatus new_status = NodeStatus::ACTIVE;

    switch (offense) {
        case OffenseType::UPTIME_VIOLATION:
            delta = -0.05f;
            break;
        case OffenseType::CHALLENGE_FAILURE:
            delta = -0.02f;
            break;
        case OffenseType::SYNC_FAILURE:
            delta = -0.03f;
            break;
        case OffenseType::DOUBLE_REGISTRATION:
            delta = -0.10f;
            break;
        case OffenseType::BYZANTINE_BEHAVIOR:
            delta = -0.25f;
            new_status = NodeStatus::SUSPENDED;
            break;
        case OffenseType::MALICIOUS_ACTIVITY:
            delta = -0.50f;
            new_status = NodeStatus::BANNED;
            break;
    }

    auto mgr = get_manager();
    if (!mgr || !mgr->node_registry()) {
        MWARNING("[Penalty] manager o registry non disponibile");
        return false;
    }
    auto reg = mgr->node_registry();

    reg->update_reputation(node_id, delta);

    NodeRegistryEntry entry;
    if (!reg->get_node_by_id(node_id, entry)) {
        MWARNING("[Penalty] nodo non trovato: " << epee::string_tools::pod_to_hex(node_id));
        return false;
    }

    if (entry.reputation_score < 0.2f && new_status == NodeStatus::ACTIVE)
        new_status = NodeStatus::SUSPENDED;

    if (new_status != NodeStatus::ACTIVE) {
        std::string reason = std::string("auto-penalty:") + offense_type_to_string(offense);
        reg->update_node_status(node_id, new_status, reason);
    }

    if (new_status == NodeStatus::BANNED && mgr->badge_system()) {
        auto all = mgr->badge_system()->get_node_badges(node_id);
        for (const auto& b : all) {
            if (b.is_active)
                mgr->badge_system()->revoke_badge(node_id, b.type,
                    "auto-revoke:node-banned");
        }
    }

    MINFO("[Penalty] applied " << offense_type_to_string(offense)
          << " delta=" << delta
          << " new_status=" << node_status_to_string(new_status)
          << " node=" << epee::string_tools::pod_to_hex(node_id));

    return true;
}

}} // namespace
