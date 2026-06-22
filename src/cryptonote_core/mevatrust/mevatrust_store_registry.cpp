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
#include <set>

using namespace cryptonote::mevatrust;

extern "C" {
#include "lmdb.h"
}

namespace cryptonote {

// ── Serialization helpers ──────────────────────────────────────────────────
static void pack_store(const StoreEntry& e, std::string& out) {
  out.clear();
  const uint8_t version = 2;
  mevatrust::lmdb_write_pod(out, version);
  out.append(reinterpret_cast<const char*>(e.store_id.data), 32);
  mevatrust::lmdb_write_str(out, e.name);
  mevatrust::lmdb_write_str(out, e.description);
  mevatrust::lmdb_write_str(out, e.url);
  out.append(reinterpret_cast<const char*>(e.owner_pubkey.data), 32);
  mevatrust::lmdb_write_str(out, e.payment_address);
  mevatrust::lmdb_write_pod(out, e.created_height);
  mevatrust::lmdb_write_pod(out, e.created_timestamp);
  mevatrust::lmdb_write_pod(out, e.active);
  mevatrust::lmdb_write_pod(out, e.item_count);
  // v2: euro fields
  mevatrust::lmdb_write_pod(out, e.euro_enabled);
  mevatrust::lmdb_write_str(out, e.euro_details);
  mevatrust::lmdb_write_pod(out, e.mvc_percent);
  mevatrust::lmdb_write_pod(out, e.euro_percent);
}

static bool unpack_store(const std::string& data, StoreEntry& e) {
  if (data.size() < 33) return false; // minimo: version + store_id
  const char* p = data.data();
  const char* end = p + data.size();
  uint8_t version;
  memcpy(&version, p, 1); p += 1;
  memcpy(e.store_id.data, p, 32); p += 32;
  if (!mevatrust::lmdb_read_str(p, end, e.name)) return false;
  if (!mevatrust::lmdb_read_str(p, end, e.description)) return false;
  if (!mevatrust::lmdb_read_str(p, end, e.url)) return false;
  if (p + 32 > end) return false;
  memcpy(e.owner_pubkey.data, p, 32); p += 32;
  if (version >= 1) {
    if (!mevatrust::lmdb_read_str(p, end, e.payment_address)) return false;
  } else {
    e.payment_address.clear();
  }
  if (!mevatrust::lmdb_read_pod(p, end, e.created_height)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.created_timestamp)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.active)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.item_count)) return false;
  // v2: euro fields
  if (version >= 2) {
    if (!mevatrust::lmdb_read_pod(p, end, e.euro_enabled)) return false;
    if (!mevatrust::lmdb_read_str(p, end, e.euro_details)) return false;
    if (!mevatrust::lmdb_read_pod(p, end, e.mvc_percent)) return false;
    if (!mevatrust::lmdb_read_pod(p, end, e.euro_percent)) return false;
  } else {
    e.euro_enabled = false;
    e.euro_details.clear();
    e.mvc_percent = 100;
    e.euro_percent = 0;
  }
  return true;
}

static void pack_item(const StoreItemEntry& e, std::string& out) {
  out.clear();
  const uint8_t version = 1;
  mevatrust::lmdb_write_pod(out, version);
  out.append(reinterpret_cast<const char*>(e.item_id.data), 32);
  out.append(reinterpret_cast<const char*>(e.store_id.data), 32);
  mevatrust::lmdb_write_str(out, e.name);
  mevatrust::lmdb_write_str(out, e.description);
  mevatrust::lmdb_write_pod(out, e.price);
  mevatrust::lmdb_write_str(out, e.category);
  mevatrust::lmdb_write_str(out, e.metadata);
  mevatrust::lmdb_write_pod(out, e.quantity);
  mevatrust::lmdb_write_pod(out, e.active);
  mevatrust::lmdb_write_pod(out, e.listed_height);
  // v1: payment_mode
  mevatrust::lmdb_write_str(out, e.payment_mode);
}

static bool unpack_item(const std::string& data, StoreItemEntry& e) {
  if (data.size() < 65) return false;
  const char* p = data.data();
  const char* end = p + data.size();
  uint8_t version;
  memcpy(&version, p, 1); p += 1;
  memcpy(e.item_id.data, p, 32); p += 32;
  memcpy(e.store_id.data, p, 32); p += 32;
  if (!mevatrust::lmdb_read_str(p, end, e.name)) return false;
  if (!mevatrust::lmdb_read_str(p, end, e.description)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.price)) return false;
  if (!mevatrust::lmdb_read_str(p, end, e.category)) return false;
  if (!mevatrust::lmdb_read_str(p, end, e.metadata)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.quantity)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.active)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.listed_height)) return false;
  // v1: payment_mode
  if (version >= 1) {
    if (!mevatrust::lmdb_read_str(p, end, e.payment_mode)) return false;
  } else {
    e.payment_mode = "mvc_only";
  }
  return true;
}

static void pack_purchase(const StorePurchaseEntry& e, std::string& out) {
  out.clear();
  const uint8_t version = 1;
  mevatrust::lmdb_write_pod(out, version);
  out.append(reinterpret_cast<const char*>(e.store_id.data), 32);
  out.append(reinterpret_cast<const char*>(e.item_id.data), 32);
  out.append(reinterpret_cast<const char*>(e.buyer_pubkey.data), 32);
  mevatrust::lmdb_write_pod(out, e.purchase_height);
  mevatrust::lmdb_write_pod(out, e.purchase_timestamp);
  // v1 fields
  mevatrust::lmdb_write_pod(out, e.mvc_amount_paid);
  mevatrust::lmdb_write_str(out, e.euro_ref);
  mevatrust::lmdb_write_pod(out, e.euro_amount);
  mevatrust::lmdb_write_pod(out, static_cast<uint8_t>(e.status));
  out.append(reinterpret_cast<const char*>(e.confirm_txid.data), 32);
  mevatrust::lmdb_write_pod(out, e.confirm_height);
  mevatrust::lmdb_write_pod(out, e.confirm_expiry_height);
}

static bool unpack_purchase(const std::string& data, StorePurchaseEntry& e) {
  if (data.size() < 96) return false;
  const char* p = data.data();
  const char* end = p + data.size();
  uint8_t version = 0;
  // Se >= 97 byte, il primo byte e' il version marker (v1+)
  if (data.size() >= 97) {
    memcpy(&version, p, 1);
    if (version == 1) {
      p += 1;
    } else {
      version = 0; // no version byte, old format
    }
  }
  memcpy(e.store_id.data, p, 32); p += 32;
  memcpy(e.item_id.data, p, 32); p += 32;
  memcpy(e.buyer_pubkey.data, p, 32); p += 32;
  if (!mevatrust::lmdb_read_pod(p, end, e.purchase_height)) return false;
  if (!mevatrust::lmdb_read_pod(p, end, e.purchase_timestamp)) return false;
  // defaults for v0
  e.mvc_amount_paid = 0;
  e.euro_ref.clear();
  e.euro_amount = 0;
  e.status = PURCHASE_PENDING;
  e.confirm_txid = crypto::hash{};
  e.confirm_height = 0;
  e.confirm_expiry_height = 0;
  if (version >= 1) {
    if (!mevatrust::lmdb_read_pod(p, end, e.mvc_amount_paid)) return false;
    if (!mevatrust::lmdb_read_str(p, end, e.euro_ref)) return false;
    if (!mevatrust::lmdb_read_pod(p, end, e.euro_amount)) return false;
    uint8_t status_byte;
    if (!mevatrust::lmdb_read_pod(p, end, status_byte)) return false;
    e.status = static_cast<PurchaseStatus>(status_byte);
    if (p + 32 > end) return false;
    memcpy(e.confirm_txid.data, p, 32); p += 32;
    if (!mevatrust::lmdb_read_pod(p, end, e.confirm_height)) return false;
    if (!mevatrust::lmdb_read_pod(p, end, e.confirm_expiry_height)) return false;
  }
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
                                          const std::string& payment_address,
                                          bool euro_enabled,
                                          const std::string& euro_details,
                                          uint8_t mvc_percent,
                                          uint8_t euro_percent,
                                          const crypto::public_key& owner_pubkey,
                                          uint64_t height) {
  std::lock_guard<std::mutex> lk(lock_);

  // Validazione: MVC percentuale mai 0, somma = 100
  if (euro_enabled) {
    if (mvc_percent == 0 || mvc_percent > 100) return crypto::hash{};
    if (euro_percent > 99) return crypto::hash{};
    if (static_cast<uint16_t>(mvc_percent) + static_cast<uint16_t>(euro_percent) != 100)
      return crypto::hash{};
  } else {
    mvc_percent = 100;
    euro_percent = 0;
  }

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
  e.payment_address = payment_address;
  e.euro_enabled = euro_enabled;
  e.euro_details = euro_details;
  e.mvc_percent = mvc_percent;
  e.euro_percent = euro_percent;
  e.created_height = height;
  e.created_timestamp = static_cast<uint64_t>(time(nullptr));
  e.active = true;
  e.item_count = 0;

  db_put_store(e);
  return store_id;
}

bool StoreRegistry::update_store(const crypto::hash& store_id, const std::string& name,
                                  const std::string& description, const std::string& url,
                                  const std::string& payment_address,
                                  bool euro_enabled,
                                  const std::string& euro_details,
                                  uint8_t mvc_percent,
                                  uint8_t euro_percent,
                                  const crypto::public_key& caller) {
  std::lock_guard<std::mutex> lk(lock_);
  std::string key(reinterpret_cast<const char*>(store_id.data), 32);
  auto it = stores_.find(key);
  if (it == stores_.end() || !it->second.active) return false;
  if (memcmp(it->second.owner_pubkey.data, caller.data, 32) != 0) return false;

  // Validazione: MVC percentuale mai 0, somma = 100
  if (euro_enabled) {
    if (mvc_percent == 0 || mvc_percent > 100) return false;
    if (euro_percent > 99) return false;
    if (static_cast<uint16_t>(mvc_percent) + static_cast<uint16_t>(euro_percent) != 100)
      return false;
  } else {
    mvc_percent = 100;
    euro_percent = 0;
  }

  it->second.name = name;
  it->second.description = description;
  it->second.url = url;
  if (!payment_address.empty())
    it->second.payment_address = payment_address;
  it->second.euro_enabled = euro_enabled;
  it->second.euro_details = euro_details;
  it->second.mvc_percent = mvc_percent;
  it->second.euro_percent = euro_percent;
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
                                        uint64_t quantity,
                                        const std::string& category,
                                        const std::string& metadata,
                                        const std::string& payment_mode,
                                        uint64_t height) {
  std::lock_guard<std::mutex> lk(lock_);
  std::string skey(reinterpret_cast<const char*>(store_id.data), 32);
  auto sit = stores_.find(skey);
  if (sit == stores_.end() || !sit->second.active) return crypto::hash{};
  if (sit->second.item_count >= MAX_ITEMS_PER_STORE) return crypto::hash{};

  // Prezzo MVC mai zero
  if (price == 0) return crypto::hash{};

  // Se lo store ha euro disabilitato, forza mvc_only
  std::string pm = payment_mode;
  if (!sit->second.euro_enabled)
    pm = "mvc_only";
  if (pm != "mvc_only" && pm != "mvc_euro")
    pm = "mvc_only";

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
  e.quantity = quantity > 0 ? quantity : 1;
  e.category = category;
  e.metadata = metadata;
  e.payment_mode = pm;
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
                              uint64_t height, uint64_t timestamp,
                              uint64_t mvc_amount_paid, const std::string& euro_ref,
                              uint64_t euro_amount) {
  std::lock_guard<std::mutex> lk(lock_);

  std::string ikey(reinterpret_cast<const char*>(item_id.data), 32);
  auto it = items_.find(ikey);
  if (it == items_.end() || !it->second.active) return false;
  if (memcmp(it->second.store_id.data, store_id.data, 32) != 0) return false;

  // Check not already purchased by same buyer
  if (is_item_purchased(item_id, buyer_pubkey)) return false;

  // Decrement quantity; se zero, auto-delist
  if (it->second.quantity > 0)
    it->second.quantity--;
  if (it->second.quantity == 0)
    it->second.active = false;

  if (!db_put_item(it->second))
    return false;

  StorePurchaseEntry pe;
  pe.store_id = store_id;
  pe.item_id = item_id;
  pe.buyer_pubkey = buyer_pubkey;
  pe.purchase_height = height;
  pe.purchase_timestamp = timestamp;
  pe.mvc_amount_paid = mvc_amount_paid;
  pe.euro_ref = euro_ref;
  pe.euro_amount = euro_amount;
  pe.status = PURCHASE_PENDING;
  pe.confirm_expiry_height = height + CONFIRM_WINDOW_BLOCKS;

  return db_put_purchase(pe);
}

// ── Two-phase confirm/cancel ────────────────────────────────────────────────
bool StoreRegistry::confirm_purchase(const crypto::hash& store_id, const crypto::hash& item_id,
                                      const crypto::public_key& buyer_pubkey,
                                      const crypto::public_key& seller_pubkey,
                                      const crypto::hash& confirm_txid, uint64_t height) {
  std::lock_guard<std::mutex> lk(lock_);
  std::string key(reinterpret_cast<const char*>(buyer_pubkey.data), 32);
  auto range = purchases_.equal_range(key);
  for (auto it = range.first; it != range.second; ++it) {
    if (memcmp(it->second.store_id.data, store_id.data, 32) != 0) continue;
    if (memcmp(it->second.item_id.data, item_id.data, 32) != 0) continue;
    if (it->second.status != PURCHASE_PENDING) return false;
    if (height > it->second.confirm_expiry_height) return false;
    it->second.status = PURCHASE_CONFIRMED;
    it->second.confirm_txid = confirm_txid;
    it->second.confirm_height = height;
    return db_put_purchase(it->second);
  }
  return false;
}

bool StoreRegistry::cancel_purchase(const crypto::hash& store_id, const crypto::hash& item_id,
                                     const crypto::public_key& buyer_pubkey,
                                     const crypto::public_key& seller_pubkey,
                                     const std::string& reason,
                                     const crypto::hash& cancel_txid, uint64_t height) {
  std::lock_guard<std::mutex> lk(lock_);
  std::string key(reinterpret_cast<const char*>(buyer_pubkey.data), 32);
  auto range = purchases_.equal_range(key);
  for (auto it = range.first; it != range.second; ++it) {
    if (memcmp(it->second.store_id.data, store_id.data, 32) != 0) continue;
    if (memcmp(it->second.item_id.data, item_id.data, 32) != 0) continue;
    if (it->second.status != PURCHASE_PENDING) return false;
    if (height > it->second.confirm_expiry_height) return false;
    it->second.status = PURCHASE_CANCELLED;
    it->second.confirm_txid = cancel_txid;
    it->second.confirm_height = height;
    // Ripristina quantita' item
    std::string ikey(reinterpret_cast<const char*>(item_id.data), 32);
    auto iit = items_.find(ikey);
    if (iit != items_.end()) {
      iit->second.quantity++;
      iit->second.active = true;
      db_put_item(iit->second);
    }
    return db_put_purchase(it->second);
  }
  return false;
}

bool StoreRegistry::auto_refund_expired(const crypto::hash& store_id, const crypto::hash& item_id,
                                         const crypto::public_key& buyer_pubkey,
                                         uint64_t current_height,
                                         const crypto::hash& refund_txid) {
  std::lock_guard<std::mutex> lk(lock_);
  std::string key(reinterpret_cast<const char*>(buyer_pubkey.data), 32);
  auto range = purchases_.equal_range(key);
  for (auto it = range.first; it != range.second; ++it) {
    if (memcmp(it->second.store_id.data, store_id.data, 32) != 0) continue;
    if (memcmp(it->second.item_id.data, item_id.data, 32) != 0) continue;
    if (it->second.status != PURCHASE_PENDING) return false;
    if (current_height < it->second.confirm_expiry_height) return false;
    it->second.status = PURCHASE_REFUNDED;
    it->second.confirm_txid = refund_txid;
    it->second.confirm_height = current_height;
    // Ripristina quantita' item
    std::string ikey(reinterpret_cast<const char*>(item_id.data), 32);
    auto iit = items_.find(ikey);
    if (iit != items_.end()) {
      iit->second.quantity++;
      iit->second.active = true;
      db_put_item(iit->second);
    }
    return db_put_purchase(it->second);
  }
  return false;
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

static std::string str_lower(const std::string& s) {
  std::string r = s;
  for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

StoreSearchResult StoreRegistry::list_stores(const StoreSearchParams& params) const {
  std::lock_guard<std::mutex> lk(lock_);

  std::vector<StoreEntry> all;
  for (const auto& kv : stores_) {
    if (!kv.second.active) continue;

    // Category filter (item-level: include store if any item matches category)
    if (!params.category.empty()) {
      bool cat_match = false;
      for (const auto& ikv : items_) {
        if (memcmp(ikv.second.store_id.data, kv.second.store_id.data, 32) != 0) continue;
        if (!ikv.second.active) continue;
        if (str_lower(ikv.second.category) == str_lower(params.category)) {
          cat_match = true; break;
        }
      }
      if (!cat_match) continue;
    }

    // Price range filter (item-level: include store if any item in price range)
    if (params.min_price > 0 || params.max_price > 0) {
      bool price_match = false;
      for (const auto& ikv : items_) {
        if (memcmp(ikv.second.store_id.data, kv.second.store_id.data, 32) != 0) continue;
        if (!ikv.second.active) continue;
        if (params.min_price > 0 && ikv.second.price < params.min_price) continue;
        if (params.max_price > 0 && ikv.second.price > params.max_price) continue;
        price_match = true; break;
      }
      if (!price_match) continue;
    }

    all.push_back(kv.second);
  }

  // Sort
  if (params.sort_by == "price_asc" || params.sort_by == "price_desc") {
    // Sort by min item price in store
    for (auto& s : all) {
      uint64_t min_p = UINT64_MAX;
      for (const auto& ikv : items_) {
        if (memcmp(ikv.second.store_id.data, s.store_id.data, 32) != 0) continue;
        if (!ikv.second.active) continue;
        if (ikv.second.price < min_p) min_p = ikv.second.price;
      }
      s.item_count = (min_p == UINT64_MAX) ? 0 : (uint32_t)min_p; // temp reuse
    }
    if (params.sort_by == "price_asc")
      std::sort(all.begin(), all.end(), [](const StoreEntry& a, const StoreEntry& b) { return a.item_count < b.item_count; });
    else
      std::sort(all.begin(), all.end(), [](const StoreEntry& a, const StoreEntry& b) { return a.item_count > b.item_count; });
    // Restore real item counts
    for (auto& s : all) {
      auto it = stores_.find(std::string((const char*)s.store_id.data, 32));
      if (it != stores_.end()) s.item_count = it->second.item_count;
    }
  } else if (params.sort_by == "newest") {
    std::sort(all.begin(), all.end(), [](const StoreEntry& a, const StoreEntry& b) { return a.created_height > b.created_height; });
  } else if (params.sort_by == "oldest") {
    std::sort(all.begin(), all.end(), [](const StoreEntry& a, const StoreEntry& b) { return a.created_height < b.created_height; });
  } else if (params.sort_by == "name") {
    std::sort(all.begin(), all.end(), [](const StoreEntry& a, const StoreEntry& b) { return str_lower(a.name) < str_lower(b.name); });
  } else {
    // relevance = by item_count descending
    std::sort(all.begin(), all.end(), [](const StoreEntry& a, const StoreEntry& b) {
      if (a.item_count != b.item_count) return a.item_count > b.item_count;
      return a.created_height > b.created_height;
    });
  }

  uint32_t total = (uint32_t)all.size();
  uint32_t per_page = params.per_page > 0 ? params.per_page : 20;
  uint32_t total_pages = (total + per_page - 1) / per_page;
  if (total_pages < 1) total_pages = 1;
  uint32_t page = params.page < 1 ? 1 : (params.page > total_pages ? total_pages : params.page);
  uint32_t start = (page - 1) * per_page;
  uint32_t end = std::min(start + per_page, total);

  StoreSearchResult result;
  result.total_count = total;
  result.page = page;
  result.total_pages = total_pages;
  if (start < end)
    result.results.assign(all.begin() + start, all.begin() + end);
  return result;
}

StoreSearchResult StoreRegistry::search_stores(const StoreSearchParams& params) const {
  std::lock_guard<std::mutex> lk(lock_);
  std::string kw_lower = str_lower(params.keyword);

  // Collect stores that directly match keyword
  std::map<std::string, StoreEntry> matched;
  for (const auto& kv : stores_) {
    if (!kv.second.active) continue;

    // Category filter
    if (!params.category.empty()) {
      bool cat_match = false;
      for (const auto& ikv : items_) {
        if (memcmp(ikv.second.store_id.data, kv.second.store_id.data, 32) != 0) continue;
        if (!ikv.second.active) continue;
        if (str_lower(ikv.second.category) == str_lower(params.category)) { cat_match = true; break; }
      }
      if (!cat_match) continue;
    }

    // Price range filter
    if (params.min_price > 0 || params.max_price > 0) {
      bool price_match = false;
      for (const auto& ikv : items_) {
        if (memcmp(ikv.second.store_id.data, kv.second.store_id.data, 32) != 0) continue;
        if (!ikv.second.active) continue;
        if (params.min_price > 0 && ikv.second.price < params.min_price) continue;
        if (params.max_price > 0 && ikv.second.price > params.max_price) continue;
        price_match = true; break;
      }
      if (!price_match) continue;
    }

    // Keyword match on store fields
    if (kw_lower.empty() ||
        str_lower(kv.second.name).find(kw_lower) != std::string::npos ||
        str_lower(kv.second.description).find(kw_lower) != std::string::npos ||
        str_lower(kv.second.url).find(kw_lower) != std::string::npos ||
        str_lower(epee::string_tools::pod_to_hex(kv.second.owner_pubkey)).find(kw_lower) != std::string::npos) {
      matched[kv.first] = kv.second;
    }
  }

  // Search items and include their stores
  if (params.search_items && !kw_lower.empty()) {
    for (const auto& kv : items_) {
      if (!kv.second.active) continue;

      // Category filter
      if (!params.category.empty() && str_lower(kv.second.category) != str_lower(params.category)) continue;
      // Price range filter
      if (params.min_price > 0 && kv.second.price < params.min_price) continue;
      if (params.max_price > 0 && kv.second.price > params.max_price) continue;

      if (str_lower(kv.second.name).find(kw_lower) != std::string::npos ||
          str_lower(kv.second.description).find(kw_lower) != std::string::npos ||
          str_lower(kv.second.category).find(kw_lower) != std::string::npos) {
        std::string skey(reinterpret_cast<const char*>(kv.second.store_id.data), 32);
        auto sit = stores_.find(skey);
        if (sit != stores_.end() && sit->second.active)
          matched[skey] = sit->second;
      }
    }
  }

  // Convert to vector
  std::vector<StoreEntry> res;
  for (const auto& m : matched) res.push_back(m.second);

  // Sort
  if (params.sort_by == "price_asc" || params.sort_by == "price_desc") {
    for (auto& s : res) {
      uint64_t min_p = UINT64_MAX;
      for (const auto& ikv : items_) {
        if (memcmp(ikv.second.store_id.data, s.store_id.data, 32) != 0) continue;
        if (!ikv.second.active) continue;
        if (ikv.second.price < min_p) min_p = ikv.second.price;
      }
      s.item_count = (min_p == UINT64_MAX) ? 0 : (uint32_t)min_p;
    }
    if (params.sort_by == "price_asc")
      std::sort(res.begin(), res.end(), [](const StoreEntry& a, const StoreEntry& b) { return a.item_count < b.item_count; });
    else
      std::sort(res.begin(), res.end(), [](const StoreEntry& a, const StoreEntry& b) { return a.item_count > b.item_count; });
    for (auto& s : res) {
      auto it = stores_.find(std::string((const char*)s.store_id.data, 32));
      if (it != stores_.end()) s.item_count = it->second.item_count;
    }
  } else if (params.sort_by == "newest") {
    std::sort(res.begin(), res.end(), [](const StoreEntry& a, const StoreEntry& b) { return a.created_height > b.created_height; });
  } else if (params.sort_by == "oldest") {
    std::sort(res.begin(), res.end(), [](const StoreEntry& a, const StoreEntry& b) { return a.created_height < b.created_height; });
  } else if (params.sort_by == "name") {
    std::sort(res.begin(), res.end(), [](const StoreEntry& a, const StoreEntry& b) { return str_lower(a.name) < str_lower(b.name); });
  } else {
    std::sort(res.begin(), res.end(), [](const StoreEntry& a, const StoreEntry& b) {
      if (a.item_count != b.item_count) return a.item_count > b.item_count;
      return a.created_height > b.created_height;
    });
  }

  uint32_t total = (uint32_t)res.size();
  uint32_t per_page = params.per_page > 0 ? params.per_page : 20;
  uint32_t total_pages = (total + per_page - 1) / per_page;
  if (total_pages < 1) total_pages = 1;
  uint32_t page = params.page < 1 ? 1 : (params.page > total_pages ? total_pages : params.page);
  uint32_t start = (page - 1) * per_page;
  uint32_t end = std::min(start + per_page, total);

  StoreSearchResult result;
  result.total_count = total;
  result.page = page;
  result.total_pages = total_pages;
  if (start < end)
    result.results.assign(res.begin() + start, res.begin() + end);
  return result;
}

std::vector<std::string> StoreRegistry::list_categories() const {
  std::lock_guard<std::mutex> lk(lock_);
  std::set<std::string> cats;
  for (const auto& kv : items_) {
    if (!kv.second.active) continue;
    if (!kv.second.category.empty())
      cats.insert(kv.second.category);
  }
  return std::vector<std::string>(cats.begin(), cats.end());
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
