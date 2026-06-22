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
  std::string payment_address; // indirizzo MVC per pagamenti (obbligatorio, non vuoto)
  // ── Euro payment option (optional, on-chain: store questo viene passato nel tx_extra) ──
  bool euro_enabled = false;
  std::string euro_details;    // IBAN, PayPal email, etc.
  uint8_t mvc_percent = 100;   // % obbligatoria in MVC (1-100, mai 0)
  uint8_t euro_percent = 0;    // % in Euro (0-99, mvc+euro = 100)
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
  uint64_t price;          // Prezzo totale in MVC (MAI zero)
  uint64_t quantity{1};
  std::string category;
  std::string metadata;
  bool active = true;
  uint64_t listed_height;
  // ── Euro payment mode ──
  // payment_mode: "mvc_only" (default), "mvc_euro" (split permesso)
  std::string payment_mode = "mvc_only";
};

enum PurchaseStatus : uint8_t {
    PURCHASE_PENDING    = 0,
    PURCHASE_CONFIRMED  = 1,
    PURCHASE_CANCELLED  = 2,
    PURCHASE_REFUNDED   = 3,
    PURCHASE_COMPLETED  = 4,
};

struct StorePurchaseEntry {
  crypto::hash store_id;
  crypto::hash item_id;
  crypto::public_key buyer_pubkey;
  uint64_t purchase_height;
  uint64_t purchase_timestamp;
  uint64_t mvc_amount_paid = 0;  // MVC effettivamente pagati on-chain
  std::string euro_ref = "";     // hash/ref del pagamento Euro off-chain (vuoto = solo MVC)
  uint64_t euro_amount = 0;     // importo Euro pagato (in cent)
  // ── Two-phase purchase status ──────────────────────────────────────
  PurchaseStatus status{PURCHASE_PENDING};
  crypto::hash confirm_txid{};   // tx della conferma/cancellazione/rimborso
  uint64_t confirm_height{0};
  uint64_t confirm_expiry_height{0}; // ITEM_BUY: deadline per conferma
};

struct StoreSearchParams {
  std::string keyword;
  std::string category;
  uint64_t min_price{0};
  uint64_t max_price{0};
  std::string sort_by{"relevance"}; // relevance, price_asc, price_desc, newest, oldest, name
  bool search_items{true};
  uint32_t page{1};
  uint32_t per_page{20};
};

struct StoreSearchResult {
  std::vector<StoreEntry> results;
  uint32_t total_count{0};
  uint32_t page{1};
  uint32_t total_pages{1};
};

class StoreRegistry {
public:
  StoreRegistry(const std::string& db_path);
  ~StoreRegistry();

  // ── Store CRUD ──────────────────────────────────────────────────────────
  crypto::hash create_store(const std::string& name, const std::string& description,
                            const std::string& url, const std::string& payment_address,
                            bool euro_enabled, const std::string& euro_details,
                            uint8_t mvc_percent, uint8_t euro_percent,
                            const crypto::public_key& owner_pubkey,
                            uint64_t height);
  bool update_store(const crypto::hash& store_id, const std::string& name,
                    const std::string& description, const std::string& url,
                    const std::string& payment_address,
                    bool euro_enabled, const std::string& euro_details,
                    uint8_t mvc_percent, uint8_t euro_percent,
                    const crypto::public_key& caller);
  bool deactivate_store(const crypto::hash& store_id, const crypto::public_key& caller);

  // ── Item CRUD ───────────────────────────────────────────────────────────
  crypto::hash list_item(const crypto::hash& store_id, const std::string& name,
                         const std::string& description, uint64_t price,
                         uint64_t quantity,
                         const std::string& category, const std::string& metadata,
                         const std::string& payment_mode,
                         uint64_t height);
  bool delist_item(const crypto::hash& store_id, const crypto::hash& item_id,
                   const crypto::public_key& caller);

  // ── Purchase ────────────────────────────────────────────────────────────
  bool buy_item(const crypto::hash& store_id, const crypto::hash& item_id,
                const crypto::public_key& buyer_pubkey, uint64_t height, uint64_t timestamp,
                uint64_t mvc_amount_paid = 0, const std::string& euro_ref = "",
                uint64_t euro_amount = 0);

  // ── Two-phase confirm/cancel ────────────────────────────────────────────
  bool confirm_purchase(const crypto::hash& store_id, const crypto::hash& item_id,
                        const crypto::public_key& buyer_pubkey,
                        const crypto::public_key& seller_pubkey,
                        const crypto::hash& confirm_txid, uint64_t height);
  bool cancel_purchase(const crypto::hash& store_id, const crypto::hash& item_id,
                       const crypto::public_key& buyer_pubkey,
                       const crypto::public_key& seller_pubkey,
                       const std::string& reason,
                       const crypto::hash& cancel_txid, uint64_t height);
  bool auto_refund_expired(const crypto::hash& store_id, const crypto::hash& item_id,
                           const crypto::public_key& buyer_pubkey, uint64_t current_height,
                           const crypto::hash& refund_txid);
  bool buyer_cancel_purchase(const crypto::hash& store_id, const crypto::hash& item_id,
                             const crypto::public_key& buyer_pubkey,
                             const crypto::hash& cancel_txid, uint64_t height);
  bool buyer_confirm_receipt(const crypto::hash& store_id, const crypto::hash& item_id,
                             const crypto::public_key& buyer_pubkey,
                             uint64_t height);

  // ── Query ───────────────────────────────────────────────────────────────
  bool get_store(const crypto::hash& store_id, StoreEntry& out) const;
  bool get_item(const crypto::hash& item_id, StoreItemEntry& out) const;
  StoreSearchResult list_stores(const StoreSearchParams& params) const;
  std::vector<StoreItemEntry> list_items(const crypto::hash& store_id, bool active_only = true) const;
  std::vector<StoreItemEntry> list_items_by_category(const std::string& category) const;
  std::vector<StorePurchaseEntry> get_purchases(const crypto::public_key& buyer) const;
  std::vector<StorePurchaseEntry> get_store_purchases(const crypto::hash& store_id) const;
  std::vector<StoreEntry> get_stores_by_owner(const crypto::public_key& owner) const;
  StoreSearchResult search_stores(const StoreSearchParams& params) const;
  std::vector<std::string> list_categories() const;
  bool is_item_purchased(const crypto::hash& item_id, const crypto::public_key& buyer) const;

  // ── Persistence ─────────────────────────────────────────────────────────
  bool load_from_disk();
  bool save_to_disk() const;

  // ── Limits ──────────────────────────────────────────────────────────────
  static constexpr size_t MAX_ITEMS_PER_STORE = 100;
  static constexpr uint64_t STORE_DEPOSIT = 10ULL * 1'000'000'000'000ULL;   // 10 MVC
  static constexpr uint64_t ITEM_DEPOSIT  = 1ULL * 1'000'000'000'000ULL;    // 1 MVC
  static constexpr uint64_t CONFIRM_WINDOW_BLOCKS = 1440;                   // ~24h a 1 min/blocco

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
