// Copyright (c) 2014-2024, The Monero Project / Mevacoin
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <vector>
#include <boost/uuid/uuid.hpp>
#include "cryptonote_protocol/cryptonote_protocol_defs.h"
#include "cryptonote_protocol/enums.h"
#include "net/net_utils_base.h"
#include "crypto/crypto.h"

namespace cryptonote
{
  struct cryptonote_connection_context;

  struct i_cryptonote_protocol
  {
    virtual bool relay_block(NOTIFY_NEW_FLUFFY_BLOCK::request& arg,
      cryptonote_connection_context& exclude_context) = 0;

    virtual bool relay_transactions(NOTIFY_NEW_TRANSACTIONS::request& arg,
      const boost::uuids::uuid& source,
      epee::net_utils::zone zone,
      relay_method tx_relay) = 0;

    virtual bool is_synchronized() const = 0;

    virtual bool send_mevatrust_challenge(
      const crypto::hash& target_node_id,
      const NOTIFY_MEVATRUST_CHALLENGE::request& req)
    { (void)target_node_id; (void)req; return false; }

    virtual bool broadcast_mevatrust_snapshot(
      const std::vector<uint8_t>& snapshot_extra, uint64_t height)
    { (void)snapshot_extra; (void)height; return false; }

    virtual bool broadcast_mevatrust_propose_block(
      const NOTIFY_MEVATRUST_PROPOSE_BLOCK::request& req)
    { (void)req; return false; }

    virtual bool broadcast_mevatrust_vote(
      const NOTIFY_MEVATRUST_VOTE::request& req)
    { (void)req; return false; }

    virtual bool broadcast_mevatrust_new_view(
      const NOTIFY_MEVATRUST_NEW_VIEW::request& req)
    { (void)req; return false; }

    virtual ~i_cryptonote_protocol() {}
  };

  struct cryptonote_protocol_stub : public i_cryptonote_protocol
  {
    bool relay_block(NOTIFY_NEW_FLUFFY_BLOCK::request&,
      cryptonote_connection_context&) override { return false; }
    bool relay_transactions(NOTIFY_NEW_TRANSACTIONS::request&,
      const boost::uuids::uuid&, epee::net_utils::zone,
      relay_method) override { return false; }
    bool is_synchronized() const override { return false; }
    bool send_mevatrust_challenge(const crypto::hash&,
      const NOTIFY_MEVATRUST_CHALLENGE::request&) override { return false; }
    bool broadcast_mevatrust_snapshot(const std::vector<uint8_t>&,
      uint64_t) override { return false; }
    bool broadcast_mevatrust_propose_block(
      const NOTIFY_MEVATRUST_PROPOSE_BLOCK::request&) override { return false; }
    bool broadcast_mevatrust_vote(
      const NOTIFY_MEVATRUST_VOTE::request&) override { return false; }
    bool broadcast_mevatrust_new_view(
      const NOTIFY_MEVATRUST_NEW_VIEW::request&) override { return false; }
  };

} // namespace cryptonote


