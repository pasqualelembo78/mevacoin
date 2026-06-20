// Copyright (c) 2026, The MevaCoin Project
// SPDX-License-Identifier: BSD-3-Clause
//
// mevatrust_store_registry.h — On-chain store registry (LMDB-backed)
// Chiunque puo' creare un negozio personalizzato, listare item e acquistarli.

#pragma once
struct MDB_env;
typedef unsigned int MDB_dbi;

#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <mutex>
#include <ctime>
#include "crypto/crypto.h"

namespace cryptonote {

struct StoreEntry {
  crypto::hash store_id;
  std::string name;
  std::string description;
  std::string url;
  crypto::public_key owner_pubkey;
  uint64_t created_height;
  uint64_t created_timestamp;
  bool active = true;
  uint32_t item_count = 0;
};

struct StoreItemEntry {
  crypto::hash item_id;
  crypto::hash store_id;
  std::string name;
  std::string description;
  uint64_t price;
  std::string category;
  std::string metadata;
  bool active = true;
  uint64_t listed_height;
};

struct StorePurchaseEntry {
  crypto::hash store_id;
  crypto::hash item_id;
  crypto::public_key buyer_pubkey;
  uint64_t purchase_height;
  uint64_t purchase_timestamp;
};

class StoreRegistry {
public:
  StoreRegistry(const std::string& db_path);
  ~StoreRegistry();

  // ── Store CRUD ──────────────────────────────────────────────────────────
  crypto::hash create_store(const std::string& name, const std::string& description,
                            const std::string& url, const crypto::public_key& owner_pubkey,
                            uint64_t height);
  bool update_store(const crypto::hash& store_id, const std::string& name,
                    const std::string& description, const std::string& url,
                    const crypto::public_key& caller);
  bool deactivate_store(const crypto::hash& store_id, const crypto::public_key& caller);

  // ── Item CRUD ───────────────────────────────────────────────────────────
  crypto::hash list_item(const crypto::hash& store_id, const std::string& name,
                         const std::string& description, uint64_t price,
                         const std::string& category, const std::string& metadata,
                         uint64_t height);
  bool delist_item(const crypto::hash& store_id, const crypto::hash& item_id,
                   const crypto::public_key& caller);

  // ── Purchase ────────────────────────────────────────────────────────────
  bool buy_item(const crypto::hash& store_id, const crypto::hash& item_id,
                const crypto::public_key& buyer_pubkey, uint64_t height, uint64_t timestamp);

  // ── Query ───────────────────────────────────────────────────────────────
  bool get_store(const crypto::hash& store_id, StoreEntry& out) const;
  bool get_item(const crypto::hash& item_id, StoreItemEntry& out) const;
  std::vector<StoreEntry> list_stores(bool active_only = true) const;
  std::vector<StoreItemEntry> list_items(const crypto::hash& store_id, bool active_only = true) const;
  std::vector<StoreItemEntry> list_items_by_category(const std::string& category) const;
  std::vector<StorePurchaseEntry> get_purchases(const crypto::public_key& buyer) const;
  std::vector<StorePurchaseEntry> get_store_purchases(const crypto::hash& store_id) const;
  std::vector<StoreEntry> get_stores_by_owner(const crypto::public_key& owner) const;
  std::vector<StoreEntry> list_top_stores(uint32_t limit, bool active_only = true) const;
  std::vector<StoreEntry> search_stores(const std::string& keyword, bool search_items = true) const;
  bool is_item_purchased(const crypto::hash& item_id, const crypto::public_key& buyer) const;

  // ── Limits ──────────────────────────────────────────────────────────────
  static constexpr size_t MAX_ITEMS_PER_STORE = 100;
  static constexpr uint64_t STORE_DEPOSIT = 10ULL * 1'000'000'000'000ULL;   // 10 MVC
  static constexpr uint64_t ITEM_DEPOSIT  = 1ULL * 1'000'000'000'000ULL;    // 1 MVC

  // ── Persistence ─────────────────────────────────────────────────────────
  bool load_from_disk();
  bool save_to_disk() const;

  // ── Reorg helpers ───────────────────────────────────────────────────────
  bool restore_item(const StoreItemEntry& e);
  bool reverse_purchase(const crypto::hash& store_id, const crypto::hash& item_id,
                        const crypto::public_key& buyer_pubkey);

private:
  std::string db_path_;
  MDB_env* m_env_{nullptr};
  MDB_dbi  m_dbi_stores_{0};
  MDB_dbi  m_dbi_items_{0};
  MDB_dbi  m_dbi_purchases_{0};

  std::map<std::string, StoreEntry> stores_;
  std::map<std::string, StoreItemEntry> items_;
  std::multimap<std::string, StorePurchaseEntry> purchases_;
  mutable std::mutex lock_;

  bool db_put_store(const StoreEntry& e);
  bool db_del_store(const crypto::hash& store_id);
  bool db_put_item(const StoreItemEntry& e);
  bool db_del_item(const crypto::hash& item_id);
  bool db_put_purchase(const StorePurchaseEntry& e);
  bool db_del_purchase(const crypto::hash& store_id, const crypto::hash& item_id,
                       const crypto::public_key& buyer);
  bool open_database();
  bool close_database();
};

} // namespace cryptonote
