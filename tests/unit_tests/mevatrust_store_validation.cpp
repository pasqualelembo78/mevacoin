#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "cryptonote_core/mevatrust/mevatrust_store_registry.h"
#include "cryptonote_core/mevatrust/mevatrust_tx_parser.h"
#include "cryptonote_core/mevatrust/mevatrust_manager.h"
#include "cryptonote_basic/tx_extra.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "string_tools.h"

using namespace cryptonote;

namespace {

std::string create_temp_dir() {
    char tmpl[] = "/tmp/mevatrust_test_XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) throw std::runtime_error("mkdtemp failed");
    return std::string(dir);
}

void cleanup_temp_dir(const std::string& dir) {
    std::string lmdb = dir + "/lmdb";
    rmdir(lmdb.c_str());
    rmdir(dir.c_str());
}

} // anonymous namespace

// ── Store CRUD Tests ─────────────────────────────────────────────────────

TEST(mevatrust_store, create_and_get_store)
{
    auto tmpdir = create_temp_dir();
    {
        StoreRegistry reg(tmpdir);
        ASSERT_TRUE(reg.load_from_disk());

        crypto::public_key owner{};
        memset(owner.data, 0xAB, 32);
        crypto::public_key owner2{};
        memset(owner2.data, 0xCD, 32);

        crypto::hash sid = reg.create_store(
            "Test Store", "A test store", "https://example.com",
            "addr1", false, "", 100, 0, owner, 1000);
        ASSERT_NE(sid, crypto::hash{});

        StoreEntry out;
        ASSERT_TRUE(reg.get_store(sid, out));
        EXPECT_EQ(out.name, "Test Store");
        EXPECT_EQ(out.description, "A test store");
        EXPECT_EQ(out.url, "https://example.com");
        EXPECT_EQ(out.payment_address, "addr1");
        EXPECT_EQ(out.mvc_percent, 100);
        EXPECT_EQ(out.euro_percent, 0);
        EXPECT_EQ(out.euro_enabled, false);
        EXPECT_TRUE(out.active);

        // Invalid percentages should fail
        crypto::hash bad = reg.create_store(
            "Bad", "", "", "addr1",
            true, "", 0, 100, owner, 1000);
        EXPECT_EQ(bad, crypto::hash{});

        bad = reg.create_store("Bad2", "", "", "addr1",
            true, "", 50, 60, owner, 1000);
        EXPECT_EQ(bad, crypto::hash{});

        // euro_enabled=false resets to 100/0
        crypto::hash sid2 = reg.create_store(
            "Euro Store", "", "", "addr2",
            true, "IBAN123", 70, 30, owner2, 1000);
        ASSERT_NE(sid2, crypto::hash{});
        ASSERT_TRUE(reg.get_store(sid2, out));
        EXPECT_EQ(out.mvc_percent, 70);
        EXPECT_EQ(out.euro_percent, 30);
        EXPECT_TRUE(out.euro_enabled);
        EXPECT_EQ(out.euro_details, "IBAN123");
    }
    cleanup_temp_dir(tmpdir);
}

TEST(mevatrust_store, update_and_deactivate_store)
{
    auto tmpdir = create_temp_dir();
    {
        StoreRegistry reg(tmpdir);
        ASSERT_TRUE(reg.load_from_disk());

        crypto::public_key owner{};
        memset(owner.data, 0xAB, 32);
        crypto::public_key wrong_owner{};
        memset(wrong_owner.data, 0xFF, 32);

        crypto::hash sid = reg.create_store(
            "Original", "Original desc", "", "addr1",
            false, "", 100, 0, owner, 1000);
        ASSERT_NE(sid, crypto::hash{});

        // Update with owner
        ASSERT_TRUE(reg.update_store(sid, "Updated", "Updated desc", "https://new.url",
            "addr_new", true, "IBAN999", 50, 50, owner));

        StoreEntry out;
        ASSERT_TRUE(reg.get_store(sid, out));
        EXPECT_EQ(out.name, "Updated");
        EXPECT_EQ(out.euro_percent, 50);
        EXPECT_EQ(out.mvc_percent, 50);

        // Update with wrong owner — should fail
        ASSERT_FALSE(reg.update_store(sid, "Hacked", "", "", "addr1",
            false, "", 100, 0, wrong_owner));

        // Deactivate with wrong owner — should fail
        ASSERT_FALSE(reg.deactivate_store(sid, wrong_owner));
        ASSERT_TRUE(reg.get_store(sid, out));
        EXPECT_TRUE(out.active);

        // Deactivate with correct owner
        ASSERT_TRUE(reg.deactivate_store(sid, owner));
        ASSERT_TRUE(reg.get_store(sid, out));
        EXPECT_FALSE(out.active);
    }
    cleanup_temp_dir(tmpdir);
}

// ── Item Tests ───────────────────────────────────────────────────────────

TEST(mevatrust_store, list_and_delist_items)
{
    auto tmpdir = create_temp_dir();
    {
        StoreRegistry reg(tmpdir);
        ASSERT_TRUE(reg.load_from_disk());

        crypto::public_key owner{};
        memset(owner.data, 0xAB, 32);

        crypto::hash sid = reg.create_store(
            "Store", "Store desc", "", "addr1",
            false, "", 100, 0, owner, 1000);
        ASSERT_NE(sid, crypto::hash{});

        // List item with price=0 should be allowed by registry (price is uint64_t)
        // But mempool pre-validation will reject it
        crypto::hash iid = reg.list_item(
            sid, "Item 1", "Item 1 desc", 5000, 10,
            "electronics", "", "mvc_only", 1000);
        ASSERT_NE(iid, crypto::hash{});

        StoreItemEntry item{};
        ASSERT_TRUE(reg.get_item(iid, item));
        EXPECT_EQ(item.name, "Item 1");
        EXPECT_EQ(item.price, 5000);
        EXPECT_EQ(item.quantity, 10);
        EXPECT_EQ(item.payment_mode, "mvc_only");
        EXPECT_TRUE(item.active);

        // List another item
        crypto::hash iid2 = reg.list_item(
            sid, "Item 2", "", 10000, 5,
            "books", "", "mvc_euro", 1001);
        ASSERT_NE(iid2, crypto::hash{});

        auto items = reg.list_items(sid, true);
        EXPECT_EQ(items.size(), 2);

        // Delist
        ASSERT_TRUE(reg.delist_item(sid, iid, owner));
        ASSERT_TRUE(reg.get_item(iid, item));
        EXPECT_FALSE(item.active);

        // Delist with wrong owner should fail
        crypto::public_key wrong{};
        memset(wrong.data, 0xFF, 32);
        ASSERT_FALSE(reg.delist_item(sid, iid2, wrong));

        // Active-only list should show 1
        items = reg.list_items(sid, true);
        EXPECT_EQ(items.size(), 1);

        // All items list should show 2
        items = reg.list_items(sid, false);
        EXPECT_EQ(items.size(), 2);
    }
    cleanup_temp_dir(tmpdir);
}

// ── Purchase Flow Tests ──────────────────────────────────────────────────

TEST(mevatrust_store, full_purchase_flow)
{
    auto tmpdir = create_temp_dir();
    {
        StoreRegistry reg(tmpdir);
        ASSERT_TRUE(reg.load_from_disk());

        crypto::public_key owner{};
        memset(owner.data, 0xAA, 32);
        crypto::public_key buyer{};
        memset(buyer.data, 0xBB, 32);

        // Create store + item
        crypto::hash sid = reg.create_store(
            "Shop", "Shop desc", "", "addr1",
            false, "", 100, 0, owner, 1000);
        ASSERT_NE(sid, crypto::hash{});

        crypto::hash iid = reg.list_item(
            sid, "Widget", "A widget", 1000, 3,
            "general", "", "mvc_only", 1000);
        ASSERT_NE(iid, crypto::hash{});

        // 1. BUY → PENDING
        ASSERT_TRUE(reg.buy_item(sid, iid, buyer, 1001, 1000000, 1000, "", 0));

        auto purchases = reg.get_store_purchases(sid);
        ASSERT_EQ(purchases.size(), 1);
        EXPECT_EQ(purchases[0].status, PURCHASE_PENDING);
        EXPECT_EQ(purchases[0].mvc_amount_paid, 1000);
        EXPECT_EQ(purchases[0].purchase_height, 1001);

        // Item quantity should have decremented
        StoreItemEntry item{};
        ASSERT_TRUE(reg.get_item(iid, item));
        EXPECT_EQ(item.quantity, 2);

        // 2. CONFIRM → CONFIRMED
        crypto::hash confirm_txid{};
        memset(confirm_txid.data, 0xCC, 32);
        ASSERT_TRUE(reg.confirm_purchase(sid, iid, buyer, owner, confirm_txid, 1005));

        purchases = reg.get_store_purchases(sid);
        ASSERT_EQ(purchases.size(), 1);
        EXPECT_EQ(purchases[0].status, PURCHASE_CONFIRMED);
        EXPECT_EQ(purchases[0].confirm_height, 1005);

        // Confirm again should fail (already confirmed)
        ASSERT_FALSE(reg.confirm_purchase(sid, iid, buyer, owner, confirm_txid, 1006));
    }
    cleanup_temp_dir(tmpdir);
}

TEST(mevatrust_store, purchase_cancel_and_refund)
{
    auto tmpdir = create_temp_dir();
    {
        StoreRegistry reg(tmpdir);
        ASSERT_TRUE(reg.load_from_disk());

        crypto::public_key owner{};
        memset(owner.data, 0xAA, 32);
        crypto::public_key buyer{};
        memset(buyer.data, 0xBB, 32);

        crypto::hash sid = reg.create_store(
            "Shop", "", "", "addr1",
            false, "", 100, 0, owner, 1000);
        ASSERT_NE(sid, crypto::hash{});

        crypto::hash iid = reg.list_item(
            sid, "Widget", "", 1000, 3,
            "", "", "mvc_only", 1000);
        ASSERT_NE(iid, crypto::hash{});

        ASSERT_TRUE(reg.buy_item(sid, iid, buyer, 1001, 1000000, 1000, "", 0));

        // Cancel by seller
        crypto::hash cancel_txid{};
        memset(cancel_txid.data, 0xDD, 32);
        ASSERT_TRUE(reg.cancel_purchase(sid, iid, buyer, owner, "out of stock", cancel_txid, 1005));

        auto purchases = reg.get_store_purchases(sid);
        ASSERT_EQ(purchases.size(), 1);
        EXPECT_EQ(purchases[0].status, PURCHASE_CANCELLED);

        // Quantity should be restored
        StoreItemEntry item{};
        ASSERT_TRUE(reg.get_item(iid, item));
        EXPECT_EQ(item.quantity, 3);

        // Cancel again should fail
        ASSERT_FALSE(reg.cancel_purchase(sid, iid, buyer, owner, "", cancel_txid, 1006));

        // New purchase — test auto_refund_expired
        crypto::public_key buyer2{};
        memset(buyer2.data, 0xCC, 32);
        ASSERT_TRUE(reg.buy_item(sid, iid, buyer2, 1010, 1000010, 500, "", 0));
        ASSERT_TRUE(reg.get_item(iid, item));
        EXPECT_EQ(item.quantity, 2);

        // Auto-refund
        crypto::hash refund_txid{};
        memset(refund_txid.data, 0xEE, 32);
        ASSERT_TRUE(reg.auto_refund_expired(sid, iid, buyer2, 2500, refund_txid));

        purchases = reg.get_store_purchases(sid);
        EXPECT_EQ(purchases[1].status, PURCHASE_REFUNDED);

        // Quantity restored again
        ASSERT_TRUE(reg.get_item(iid, item));
        EXPECT_EQ(item.quantity, 3);
    }
    cleanup_temp_dir(tmpdir);
}

// ── Tx Extra Build / Parse Tests ────────────────────────────────────────

TEST(mevatrust_store, build_and_parse_store_create)
{
    crypto::public_key pk{};
    memset(pk.data, 0x12, 32);
    crypto::secret_key sk{};
    memset(sk.data, 0x34, 32);

    tx_extra_mevatrust_store op{};
    op.op = tx_extra_mevatrust_store::STORE_CREATE;
    op.name = "Test Store";
    op.description = "A test store";
    op.url = "https://example.com";
    op.payment_address = "addr1";
    op.euro_enabled = true;
    op.euro_details = "IBAN123";
    op.mvc_percent = 70;
    op.euro_percent = 30;
    op.owner_pubkey = pk;

    // Sign
    crypto::hash msg_hash = mevatrust::store_message_hash(op);
    crypto::generate_signature(msg_hash, pk, sk, op.owner_sig);

    // Serialize
    std::vector<uint8_t> extra;
    ASSERT_TRUE(mevatrust::build_mevatrust_store_extra(op, extra));
    EXPECT_FALSE(extra.empty());

    // Deserialize
    transaction tx{};
    tx.extra = extra;
    tx_extra_mevatrust_store parsed{};
    ASSERT_TRUE(mevatrust::parse_mevatrust_store_from_tx(tx, parsed));
    EXPECT_EQ(parsed.op, tx_extra_mevatrust_store::STORE_CREATE);
    EXPECT_EQ(parsed.name, "Test Store");
    EXPECT_EQ(parsed.mvc_percent, 70);
    EXPECT_EQ(parsed.euro_percent, 30);
    EXPECT_TRUE(parsed.euro_enabled);

    // Verify signature
    EXPECT_TRUE(mevatrust::verify_store_signature(parsed));
}

TEST(mevatrust_store, build_and_verify_confirm_cancel)
{
    crypto::public_key seller_pk{};
    memset(seller_pk.data, 0x12, 32);
    crypto::secret_key seller_sk{};
    memset(seller_sk.data, 0x34, 32);
    crypto::public_key buyer_pk{};
    memset(buyer_pk.data, 0x56, 32);

    crypto::hash store_id{};
    memset(store_id.data, 0x78, 32);
    crypto::hash item_id{};
    memset(item_id.data, 0x90, 32);

    // Build CONFIRM
    {
        tx_extra_mevatrust_store op{};
        op.op = tx_extra_mevatrust_store::STORE_CONFIRM;
        op.store_id = store_id;
        op.item_id = item_id;
        op.buyer_pubkey = buyer_pk;
        op.seller_pubkey = seller_pk;

        crypto::hash msg_hash = mevatrust::store_confirm_message_hash(op);
        crypto::generate_signature(msg_hash, seller_pk, seller_sk, op.seller_sig);

        std::vector<uint8_t> extra;
        ASSERT_TRUE(mevatrust::build_mevatrust_store_extra(op, extra));

        transaction tx{};
        tx.extra = extra;
        tx_extra_mevatrust_store parsed{};
        ASSERT_TRUE(mevatrust::parse_mevatrust_store_from_tx(tx, parsed));
        EXPECT_EQ(parsed.op, tx_extra_mevatrust_store::STORE_CONFIRM);
        EXPECT_TRUE(mevatrust::verify_store_confirm_signature(parsed));
    }

    // Build CANCEL
    {
        tx_extra_mevatrust_store op{};
        op.op = tx_extra_mevatrust_store::STORE_CANCEL;
        op.store_id = store_id;
        op.item_id = item_id;
        op.buyer_pubkey = buyer_pk;
        op.seller_pubkey = seller_pk;
        op.cancel_reason = "out of stock";

        crypto::hash msg_hash = mevatrust::store_confirm_message_hash(op);
        crypto::generate_signature(msg_hash, seller_pk, seller_sk, op.seller_sig);

        std::vector<uint8_t> extra;
        ASSERT_TRUE(mevatrust::build_mevatrust_store_extra(op, extra));

        transaction tx{};
        tx.extra = extra;
        tx_extra_mevatrust_store parsed{};
        ASSERT_TRUE(mevatrust::parse_mevatrust_store_from_tx(tx, parsed));
        EXPECT_EQ(parsed.op, tx_extra_mevatrust_store::STORE_CANCEL);
        EXPECT_EQ(parsed.cancel_reason, "out of stock");
        EXPECT_TRUE(mevatrust::verify_store_cancel_signature(parsed));
    }

    // Bad signature should fail
    {
        tx_extra_mevatrust_store op{};
        op.op = tx_extra_mevatrust_store::STORE_CONFIRM;
        op.store_id = store_id;
        op.item_id = item_id;
        op.buyer_pubkey = buyer_pk;
        op.seller_pubkey = seller_pk;
        // seller_sig left uninitialized (all zeros)
        EXPECT_FALSE(mevatrust::verify_store_confirm_signature(op));
    }
}
