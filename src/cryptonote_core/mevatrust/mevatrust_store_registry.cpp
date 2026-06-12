// Copyright (c) 2026, The MevaCoin Project
// SPDX-License-Identifier: BSD-3-Clause

#include "mevatrust_store_registry.h"
#include "mevatrust_lmdb.h"
#include "string_tools.h"
#include <cstring>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cctype>

using namespace cryptonote::mevatrust;

extern "C" {
#include "lmdb.h"
}

namespace cryptonote {

// ── Serialization helpers ──────────────────────────────────────────────────
static void pack_store(const StoreEntry& e, std::string& out) {
  out.clear();
  out.append(reinterpret_cast<const char*>(e.store_id.data), 32);
  mevatrust::lmdb_write_str(out, e.name);
  mevatrust::lmdb_write_str(out, e.description);
  mevatrust::lmdb_write_str(out, e.url);
  out.append(reinterpret_cast<const char*>(e.owner_pubkey.data), 32);
  mevatrust::lmdb_write_pod(out, e.created_height);
  mevatrust::lmdb_write_pod(out, e.created_timestamp);
  mevatrust::lmdb_write_pod(out, e.active);
  mevatrust::lmdb_write_pod(out, e.item_count);
}

static bool unpack_store(const std::string& data, StoreEntry& e) {
  if (data.size() < 32) return false;
  const char* p = data.data();
  const char* end = p + data.size();
  memcpy(e.store_id.data, p, 32); p += 32;
  if (!mevatrust::lmdb_read_str(p, end, e.name)) return false;
  if (!mevatrust::lmdb_read_str(p, end, e.description)) return false;
  if (!mevatrust::lmdb_read_str(p, end, e.url)) return false;
  if (p + 32 > end) return false;
  memcpy(e.owner_pubkey.data, p, 32); p += 32;
  if (!mevatrust::lmdb_read_pod(p, end, e.created_height)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.created_timestamp)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.active)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.item_count)) return false;
  return true;
}

static void pack_item(const StoreItemEntry& e, std::string& out) {
  out.clear();
  out.append(reinterpret_cast<const char*>(e.item_id.data), 32);
  out.append(reinterpret_cast<const char*>(e.store_id.data), 32);
  mevatrust::lmdb_write_str(out, e.name);
  mevatrust::lmdb_write_str(out, e.description);
  mevatrust::lmdb_write_pod(out, e.price);
  mevatrust::lmdb_write_str(out, e.category);
  mevatrust::lmdb_write_str(out, e.metadata);
  mevatrust::lmdb_write_pod(out, e.active);
  mevatrust::lmdb_write_pod(out, e.listed_height);
}

static bool unpack_item(const std::string& data, StoreItemEntry& e) {
  if (data.size() < 64) return false;
  const char* p = data.data();
  const char* end = p + data.size();
  memcpy(e.item_id.data, p, 32); p += 32;
  memcpy(e.store_id.data, p, 32); p += 32;
  if (!mevatrust::lmdb_read_str(p, end, e.name)) return false;
  if (!mevatrust::lmdb_read_str(p, end, e.description)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.price)) return false;
  if (!mevatrust::lmdb_read_str(p, end, e.category)) return false;
  if (!mevatrust::lmdb_read_str(p, end, e.metadata)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.active)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.listed_height)) return false;
  return true;
}

static void pack_purchase(const StorePurchaseEntry& e, std::string& out) {
  out.clear();
  out.append(reinterpret_cast<const char*>(e.store_id.data), 32);
  out.append(reinterpret_cast<const char*>(e.item_id.data), 32);
  out.append(reinterpret_cast<const char*>(e.buyer_pubkey.data), 32);
  mevatrust::lmdb_write_pod(out, e.purchase_height);
  mevatrust::lmdb_write_pod(out, e.purchase_timestamp);
}

static bool unpack_purchase(const std::string& data, StorePurchaseEntry& e) {
  if (data.size() < 96) return false;
  const char* p = data.data();
  const char* end = p + data.size();
  memcpy(e.store_id.data, p, 32); p += 32;
  memcpy(e.item_id.data, p, 32); p += 32;
  memcpy(e.buyer_pubkey.data, p, 32); p += 32;
  if (!mevatrust::lmdb_read_pod(p, end, e.purchase_height)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.purchase_timestamp)) return false;
  return true;
}

// ── Constructor / Destructor ───────────────────────────────────────────────
StoreRegistry::StoreRegistry(const std::string& db_path) : db_path_(db_path) {}

StoreRegistry::~StoreRegistry() { close_database(); }

// ── Database ───────────────────────────────────────────────────────────────
bool StoreRegistry::open_database() {
  if (m_env_) return true;
  try {
    m_env_ = MevaTrustLMDB::open(db_path_, 12, 512ULL << 20);
    MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
    m_dbi_stores_    = MevaTrustLMDB::open_dbi(txn, "MT_STORES");
    m_dbi_items_     = MevaTrustLMDB::open_dbi(txn, "MT_ITEMS");
    m_dbi_purchases_ = MevaTrustLMDB::open_dbi(txn, "MT_PURCHASES");
    mdb_txn_commit(txn);
    return true;
  } catch (...) { return false; }
}

bool StoreRegistry::close_database() {
  if (!m_env_) return true;
  MevaTrustLMDB::release(db_path_);
  m_env_ = nullptr;
  return true;
}

bool StoreRegistry::load_from_disk() {
  std::lock_guard<std::mutex> lk(lock_);
  if (!open_database()) return false;
  stores_.clear(); items_.clear(); purchases_.clear();

  MDB_txn* rtxn = MevaTrustLMDB::begin_read(m_env_);
  MDB_cursor* cur = nullptr;
  MDB_val k, v;

  // Load stores
  if (mdb_cursor_open(rtxn, m_dbi_stores_, &cur) == 0) {
    while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
      StoreEntry e;
      std::string data(static_cast<const char*>(v.mv_data), v.mv_size);
      if (unpack_store(data, e)) {
        std::string key(static_cast<const char*>(k.mv_data), k.mv_size);
        stores_[key] = e;
      }
    }
    mdb_cursor_close(cur);
  }

  // Load items
  if (mdb_cursor_open(rtxn, m_dbi_items_, &cur) == 0) {
    while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
      StoreItemEntry e;
      std::string data(static_cast<const char*>(v.mv_data), v.mv_size);
      if (unpack_item(data, e)) {
        std::string key(static_cast<const char*>(k.mv_data), k.mv_size);
        items_[key] = e;
      }
    }
    mdb_cursor_close(cur);
  }

  // Load purchases
  if (mdb_cursor_open(rtxn, m_dbi_purchases_, &cur) == 0) {
    while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
      StorePurchaseEntry e;
      std::string data(static_cast<const char*>(v.mv_data), v.mv_size);
      if (unpack_purchase(data, e)) {
        std::string key(static_cast<const char*>(k.mv_data), k.mv_size);
        purchases_.emplace(key, e);
      }
    }
    mdb_cursor_close(cur);
  }

  mdb_txn_abort(rtxn);
  return true;
}

bool StoreRegistry::save_to_disk() const {
  std::lock_guard<std::mutex> lk(lock_);
  if (!m_env_) return false;
  try {
    MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
    mdb_drop(txn, m_dbi_stores_, 0);
    mdb_drop(txn, m_dbi_items_, 0);
    mdb_drop(txn, m_dbi_purchases_, 0);

    for (const auto& kv : stores_) {
      std::string data; pack_store(kv.second, data);
      MDB_val mk{ kv.first.size(), const_cast<char*>(kv.first.data()) };
      MDB_val mv{ data.size(), const_cast<char*>(data.data()) };
      if (mdb_put(txn, m_dbi_stores_, &mk, &mv, 0)) { mdb_txn_abort(txn); return false; }
    }
    for (const auto& kv : items_) {
      std::string data; pack_item(kv.second, data);
      MDB_val mk{ kv.first.size(), const_cast<char*>(kv.first.data()) };
      MDB_val mv{ data.size(), const_cast<char*>(data.data()) };
      if (mdb_put(txn, m_dbi_items_, &mk, &mv, 0)) { mdb_txn_abort(txn); return false; }
    }
    for (const auto& kv : purchases_) {
      std::string data; pack_purchase(kv.second, data);
      MDB_val mk{ kv.first.size(), const_cast<char*>(kv.first.data()) };
      MDB_val mv{ data.size(), const_cast<char*>(data.data()) };
      if (mdb_put(txn, m_dbi_purchases_, &mk, &mv, 0)) { mdb_txn_abort(txn); return false; }
    }

    mdb_txn_commit(txn);
    return true;
  } catch (...) { return false; }
}

// ── DB helpers ─────────────────────────────────────────────────────────────
bool StoreRegistry::db_put_store(const StoreEntry& e) {
  std::string key(reinterpret_cast<const char*>(e.store_id.data), 32);
  std::string data; pack_store(e, data);
  MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
  MDB_val mk{ key.size(), const_cast<char*>(key.data()) };
  MDB_val mv{ data.size(), const_cast<char*>(data.data()) };
  int rc = mdb_put(txn, m_dbi_stores_, &mk, &mv, 0);
  if (rc) { mdb_txn_abort(txn); return false; }
  mdb_txn_commit(txn);
  stores_[key] = e;
  return true;
}

bool StoreRegistry::db_del_store(const crypto::hash& store_id) {
  std::string key(reinterpret_cast<const char*>(store_id.data), 32);
  MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
  MDB_val mk{ key.size(), const_cast<char*>(key.data()) };
  mdb_del(txn, m_dbi_stores_, &mk, nullptr);
  mdb_txn_commit(txn);
  stores_.erase(key);
  return true;
}

bool StoreRegistry::db_put_item(const StoreItemEntry& e) {
  std::string key(reinterpret_cast<const char*>(e.item_id.data), 32);
  std::string data; pack_item(e, data);
  MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
  MDB_val mk{ key.size(), const_cast<char*>(key.data()) };
  MDB_val mv{ data.size(), const_cast<char*>(data.data()) };
  int rc = mdb_put(txn, m_dbi_items_, &mk, &mv, 0);
  if (rc) { mdb_txn_abort(txn); return false; }
  mdb_txn_commit(txn);
  items_[key] = e;
  return true;
}

bool StoreRegistry::db_del_item(const crypto::hash& item_id) {
  std::string key(reinterpret_cast<const char*>(item_id.data), 32);
  MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
  MDB_val mk{ key.size(), const_cast<char*>(key.data()) };
  mdb_del(txn, m_dbi_items_, &mk, nullptr);
  mdb_txn_commit(txn);
  items_.erase(key);
  return true;
}

bool StoreRegistry::db_put_purchase(const StorePurchaseEntry& e) {
  std::string key(reinterpret_cast<const char*>(e.buyer_pubkey.data), 32);
  std::string data; pack_purchase(e, data);
  MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
  MDB_val mk{ key.size(), const_cast<char*>(key.data()) };
  MDB_val mv{ data.size(), const_cast<char*>(data.data()) };
  int rc = mdb_put(txn, m_dbi_purchases_, &mk, &mv, 0);
  if (rc) { mdb_txn_abort(txn); return false; }
  mdb_txn_commit(txn);
  purchases_.emplace(key, e);
  return true;
}

bool StoreRegistry::db_del_purchase(const crypto::hash& store_id, const crypto::hash& item_id,
                                     const crypto::public_key& buyer) {
  std::string key(reinterpret_cast<const char*>(buyer.data), 32);
  MDB_txn* txn = MevaTrustLMDB::begin_write(m_env_);
  MDB_val mk{ key.size(), const_cast<char*>(key.data()) };
  mdb_del(txn, m_dbi_purchases_, &mk, nullptr);
  mdb_txn_commit(txn);
  auto range = purchases_.equal_range(key);
  for (auto it = range.first; it != range.second; ) {
    if (memcmp(it->second.store_id.data, store_id.data, 32) == 0 &&
        memcmp(it->second.item_id.data, item_id.data, 32) == 0) {
      it = purchases_.erase(it);
    } else { ++it; }
  }
  return true;
}

// ── Store CRUD ─────────────────────────────────────────────────────────────
crypto::hash StoreRegistry::create_store(const std::string& name,
                                          const std::string& description,
                                          const std::string& url,
                                          const crypto::public_key& owner_pubkey,
                                          uint64_t height) {
  std::lock_guard<std::mutex> lk(lock_);

  crypto::hash store_id;
  crypto::cn_fast_hash(owner_pubkey.data, 32, store_id.data);
  std::string mix(name);
  mix.append(reinterpret_cast<const char*>(&height), 8);
  crypto::hash h2;
  crypto::cn_fast_hash(mix.data(), mix.size(), h2.data);
  for (size_t i = 0; i < 32; ++i) store_id.data[i] ^= h2.data[i];

  if (stores_.count(std::string(reinterpret_cast<const char*>(store_id.data), 32)))
    return store_id;

  StoreEntry e;
  e.store_id = store_id;
  e.name = name;
  e.description = description;
  e.url = url;
  e.owner_pubkey = owner_pubkey;
  e.created_height = height;
  e.created_timestamp = static_cast<uint64_t>(time(nullptr));
  e.active = true;
  e.item_count = 0;

  db_put_store(e);
  return store_id;
}

bool StoreRegistry::update_store(const crypto::hash& store_id, const std::string& name,
                                  const std::string& description, const std::string& url,
                                  const crypto::public_key& caller) {
  std::lock_guard<std::mutex> lk(lock_);
  std::string key(reinterpret_cast<const char*>(store_id.data), 32);
  auto it = stores_.find(key);
  if (it == stores_.end() || !it->second.active) return false;
  if (memcmp(it->second.owner_pubkey.data, caller.data, 32) != 0) return false;

  it->second.name = name;
  it->second.description = description;
  it->second.url = url;
  return db_put_store(it->second);
}

bool StoreRegistry::deactivate_store(const crypto::hash& store_id,
                                      const crypto::public_key& caller) {
  std::lock_guard<std::mutex> lk(lock_);
  std::string key(reinterpret_cast<const char*>(store_id.data), 32);
  auto it = stores_.find(key);
  if (it == stores_.end()) return false;
  if (memcmp(it->second.owner_pubkey.data, caller.data, 32) != 0) return false;

  it->second.active = false;
  return db_put_store(it->second);
}

// ── Item CRUD ──────────────────────────────────────────────────────────────
crypto::hash StoreRegistry::list_item(const crypto::hash& store_id,
                                       const std::string& name,
                                       const std::string& description,
                                       uint64_t price,
                                       const std::string& category,
                                       const std::string& metadata,
                                       uint64_t height) {
  std::lock_guard<std::mutex> lk(lock_);
  std::string skey(reinterpret_cast<const char*>(store_id.data), 32);
  auto sit = stores_.find(skey);
  if (sit == stores_.end() || !sit->second.active) return crypto::hash{};
  if (sit->second.item_count >= MAX_ITEMS_PER_STORE) return crypto::hash{};

  crypto::hash item_id;
  std::string mix(reinterpret_cast<const char*>(store_id.data), 32);
  mix.append(name);
  mix.append(reinterpret_cast<const char*>(&height), 8);
  crypto::cn_fast_hash(mix.data(), mix.size(), item_id.data);

  StoreItemEntry e;
  e.item_id = item_id;
  e.store_id = store_id;
  e.name = name;
  e.description = description;
  e.price = price;
  e.category = category;
  e.metadata = metadata;
  e.active = true;
  e.listed_height = height;

  if (db_put_item(e)) {
    sit->second.item_count++;
    db_put_store(sit->second);
    return item_id;
  }
  return crypto::hash{};
}

bool StoreRegistry::delist_item(const crypto::hash& store_id, const crypto::hash& item_id,
                                 const crypto::public_key& caller) {
  std::lock_guard<std::mutex> lk(lock_);
  std::string ikey(reinterpret_cast<const char*>(item_id.data), 32);
  auto it = items_.find(ikey);
  if (it == items_.end() || !it->second.active) return false;
  if (memcmp(it->second.store_id.data, store_id.data, 32) != 0) return false;

  std::string skey(reinterpret_cast<const char*>(store_id.data), 32);
  auto sit = stores_.find(skey);
  if (sit == stores_.end()) return false;
  if (memcmp(sit->second.owner_pubkey.data, caller.data, 32) != 0) return false;

  it->second.active = false;
  if (db_put_item(it->second)) {
    if (sit->second.item_count > 0) sit->second.item_count--;
    db_put_store(sit->second);
    return true;
  }
  return false;
}

// ── Purchase ───────────────────────────────────────────────────────────────
bool StoreRegistry::buy_item(const crypto::hash& store_id, const crypto::hash& item_id,
                              const crypto::public_key& buyer_pubkey,
                              uint64_t height, uint64_t timestamp) {
  std::lock_guard<std::mutex> lk(lock_);

  std::string ikey(reinterpret_cast<const char*>(item_id.data), 32);
  auto it = items_.find(ikey);
  if (it == items_.end() || !it->second.active) return false;
  if (memcmp(it->second.store_id.data, store_id.data, 32) != 0) return false;

  // Check not already purchased by same buyer
  if (is_item_purchased(item_id, buyer_pubkey)) return false;

  StorePurchaseEntry pe;
  pe.store_id = store_id;
  pe.item_id = item_id;
  pe.buyer_pubkey = buyer_pubkey;
  pe.purchase_height = height;
  pe.purchase_timestamp = timestamp;

  return db_put_purchase(pe);
}

// ── Query ──────────────────────────────────────────────────────────────────
bool StoreRegistry::get_store(const crypto::hash& store_id, StoreEntry& out) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::string key(reinterpret_cast<const char*>(store_id.data), 32);
  auto it = stores_.find(key);
  if (it == stores_.end()) return false;
  out = it->second;
  return true;
}

bool StoreRegistry::get_item(const crypto::hash& item_id, StoreItemEntry& out) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::string key(reinterpret_cast<const char*>(item_id.data), 32);
  auto it = items_.find(key);
  if (it == items_.end()) return false;
  out = it->second;
  return true;
}

std::vector<StoreEntry> StoreRegistry::list_stores(bool active_only) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::vector<StoreEntry> res;
  for (const auto& kv : stores_) {
    if (!active_only || kv.second.active)
      res.push_back(kv.second);
  }
  return res;
}

std::vector<StoreItemEntry> StoreRegistry::list_items(const crypto::hash& store_id,
                                                       bool active_only) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::vector<StoreItemEntry> res;
  for (const auto& kv : items_) {
    if (memcmp(kv.second.store_id.data, store_id.data, 32) != 0) continue;
    if (!active_only || kv.second.active)
      res.push_back(kv.second);
  }
  return res;
}

std::vector<StoreItemEntry> StoreRegistry::list_items_by_category(const std::string& category) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::vector<StoreItemEntry> res;
  for (const auto& kv : items_) {
    if (kv.second.category != category) continue;
    if (kv.second.active)
      res.push_back(kv.second);
  }
  return res;
}

std::vector<StorePurchaseEntry> StoreRegistry::get_purchases(const crypto::public_key& buyer) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::vector<StorePurchaseEntry> res;
  std::string key(reinterpret_cast<const char*>(buyer.data), 32);
  auto range = purchases_.equal_range(key);
  for (auto it = range.first; it != range.second; ++it)
    res.push_back(it->second);
  return res;
}

std::vector<StorePurchaseEntry> StoreRegistry::get_store_purchases(const crypto::hash& store_id) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::vector<StorePurchaseEntry> res;
  for (const auto& kv : purchases_) {
    if (memcmp(kv.second.store_id.data, store_id.data, 32) == 0)
      res.push_back(kv.second);
  }
  return res;
}

std::vector<StoreEntry> StoreRegistry::get_stores_by_owner(const crypto::public_key& owner) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::vector<StoreEntry> res;
  for (const auto& kv : stores_) {
    if (memcmp(kv.second.owner_pubkey.data, owner.data, 32) == 0)
      res.push_back(kv.second);
  }
  return res;
}

std::vector<StoreEntry> StoreRegistry::list_top_stores(uint32_t limit, bool active_only) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::vector<StoreEntry> res;
  for (const auto& kv : stores_) {
    if (active_only && !kv.second.active) continue;
    res.push_back(kv.second);
  }
  std::sort(res.begin(), res.end(), [](const StoreEntry& a, const StoreEntry& b) {
    if (a.item_count != b.item_count) return a.item_count > b.item_count;
    return a.created_height > b.created_height;
  });
  if (res.size() > limit) res.resize(limit);
  return res;
}

std::vector<StoreEntry> StoreRegistry::search_stores(const std::string& keyword, bool search_items) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::string kw_lower = keyword;
  for (auto& c : kw_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  // Collect stores that directly match
  std::map<std::string, StoreEntry> matched;
  for (const auto& kv : stores_) {
    if (!kv.second.active) continue;
    std::string name_l = kv.second.name;
    for (auto& c : name_l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string desc_l = kv.second.description;
    for (auto& c : desc_l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string url_l = kv.second.url;
    for (auto& c : url_l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string owner_h = epee::string_tools::pod_to_hex(kv.second.owner_pubkey);

    if (name_l.find(kw_lower) != std::string::npos ||
        desc_l.find(kw_lower) != std::string::npos ||
        url_l.find(kw_lower) != std::string::npos ||
        owner_h.find(kw_lower) != std::string::npos) {
      matched[kv.first] = kv.second;
    }
  }

  // Search items and include their stores
  if (search_items) {
    for (const auto& kv : items_) {
      if (!kv.second.active) continue;
      std::string iname_l = kv.second.name;
      for (auto& c : iname_l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      std::string idesc_l = kv.second.description;
      for (auto& c : idesc_l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      std::string icat_l = kv.second.category;
      for (auto& c : icat_l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      if (iname_l.find(kw_lower) != std::string::npos ||
          idesc_l.find(kw_lower) != std::string::npos ||
          icat_l.find(kw_lower) != std::string::npos) {
        std::string skey(reinterpret_cast<const char*>(kv.second.store_id.data), 32);
        auto sit = stores_.find(skey);
        if (sit != stores_.end() && sit->second.active)
          matched[skey] = sit->second;
      }
    }
  }

  std::vector<StoreEntry> res;
  for (const auto& m : matched) res.push_back(m.second);
  return res;
}

bool StoreRegistry::is_item_purchased(const crypto::hash& item_id,
                                       const crypto::public_key& buyer) const {
  std::string key(reinterpret_cast<const char*>(buyer.data), 32);
  auto range = purchases_.equal_range(key);
  for (auto it = range.first; it != range.second; ++it) {
    if (memcmp(it->second.item_id.data, item_id.data, 32) == 0)
      return true;
  }
  return false;
}

// ── Reorg helpers ──────────────────────────────────────────────────────────
bool StoreRegistry::restore_item(const StoreItemEntry& e) {
  std::lock_guard<std::mutex> lk(lock_);
  std::string ikey(reinterpret_cast<const char*>(e.item_id.data), 32);
  items_[ikey] = e;
  return db_put_item(e);
}

bool StoreRegistry::reverse_purchase(const crypto::hash& store_id, const crypto::hash& item_id,
                                      const crypto::public_key& buyer_pubkey) {
  return db_del_purchase(store_id, item_id, buyer_pubkey);
}

} // namespace cryptonote
