// Copyright (c) 2024, The Mevacoin Project
// pool_address.h — Deterministic MevaTrust pool address derivation
// NO private key exists. Address derived from: H("mevatrust_pool" || network_id)

#pragma once

#include <string>
#include <cstring>
#include "crypto/crypto.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_config.h"

namespace cryptonote {
namespace mevatrust {

// Domain separation constant for pool address derivation
static constexpr const char POOL_DOMAIN[] = "mevatrust_pool";

// Derive the pool spend public key deterministically
// Input: network_type (MAINNET/TESTNET/STAGENET)
// Output: spend public key (32 bytes)
// Algorithm: spend_key = H(domain || network_id) mod l (scalar), then scalar * G
inline crypto::public_key derive_pool_spend_key(network_type nettype) {
    std::string domain = POOL_DOMAIN;
    domain.push_back(static_cast<char>(nettype));  // 0=mainnet, 1=testnet, 2=stagenet
    
    crypto::hash h = crypto::cn_fast_hash(domain.data(), domain.size());
    
    // Reduce to scalar (mod l, the curve order)
    crypto::ec_scalar scalar;
    crypto::hash_to_scalar(h.data, 32, scalar);
    
    // Convert to secret_key (same underlying bytes) then multiply by G
    crypto::secret_key sk;
    static_assert(sizeof(sk) == sizeof(scalar), "size mismatch");
    memcpy(&sk, &scalar, sizeof(scalar));
    
    crypto::public_key spend_key;
    crypto::secret_key_to_public_key(sk, spend_key);
    
    return spend_key;
}

// Get the full pool address (spend_key, view_key = spend_key for simplicity)
// The pool address has NO private key — it's a "burn-like" address that can only receive
// Funds are spent via threshold-signed distribution transactions validated by consensus
inline account_public_address get_pool_address(network_type nettype) {
    crypto::public_key spend_key = derive_pool_spend_key(nettype);
    // View key = spend key (anyone can view incoming outputs)
    return account_public_address{spend_key, spend_key};
}

// Verify an address matches the deterministic pool address
inline bool is_pool_address(const account_public_address& addr, network_type nettype) {
    account_public_address pool_addr = get_pool_address(nettype);
    return addr.m_spend_public_key == pool_addr.m_spend_public_key &&
           addr.m_view_public_key == pool_addr.m_view_public_key;
}

} // namespace mevatrust
} // namespace cryptonote
