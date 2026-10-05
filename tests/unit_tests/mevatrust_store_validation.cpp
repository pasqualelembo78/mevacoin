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
    // Chiavi vere, non memset: 0x12 ripetuto 32 volte non e' quasi certamente un
    // punto valido della curva, e check_signature su una chiave pubblica non
    // valida solleva invece di verificare. Il test falliva per i dati, non per
    // il codice che voleva provare.
    crypto::public_key pk{};
    crypto::secret_key sk{};
    crypto::generate_keys(pk, sk);

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
    // Chiavi vere: vedi la nota sopra su check_signature e le chiavi non valide.
    crypto::public_key seller_pk{}, buyer_pk{};
    crypto::secret_key seller_sk{}, buyer_sk{};
    crypto::generate_keys(seller_pk, seller_sk);
    crypto::generate_keys(buyer_pk, buyer_sk);

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

// -- Regressions: due bug reali trovati dai test ----------------------------

// db_put_purchase() faceva sempre emplace(), quindi un aggiornamento non
// sostituiva la riga: la duplicava. confirm_purchase() su un acquisto ne
// lasciava due copie, una ancora PENDING. Il test se ne accorgeva per il
// conteggio, ma il danno vero e' altro: la copia stantia permette a un
// annullamento successivo di trovare un PENDING fantasma e incrementare la
// quantita' due volte per un solo acquisto. La disponibilita' si gonfia.
TEST(mevatrust_store, confirming_a_purchase_does_not_duplicate_it)
{
    auto tmpdir = create_temp_dir();
    {
        StoreRegistry reg(tmpdir);
        ASSERT_TRUE(reg.load_from_disk());

        crypto::public_key owner{}, buyer{};
        memset(owner.data, 0xAA, 32);
        memset(buyer.data, 0xBB, 32);

        crypto::hash sid = reg.create_store("S","d","","a", false,"",100,0,owner,1000);
        crypto::hash iid = reg.list_item(sid,"W","w",1000,3,"g","","mvc_only",1000);
        ASSERT_TRUE(reg.buy_item(sid, iid, buyer, 1001, 1000000, 1000, "", 0));
        ASSERT_EQ(reg.get_store_purchases(sid).size(), 1u);

        crypto::hash txid{};
        memset(txid.data, 0xCC, 32);
        ASSERT_TRUE(reg.confirm_purchase(sid, iid, buyer, owner, txid, 1005));

        // Un solo acquisto: quello stesso, ora confermato.
        auto purchases = reg.get_store_purchases(sid);
        ASSERT_EQ(purchases.size(), 1u) << "la conferma ha duplicato l'acquisto";
        EXPECT_EQ(purchases[0].status, PURCHASE_CONFIRMED);
        EXPECT_EQ(purchases[0].confirm_height, 1005u);

        StoreItemEntry item{};
        ASSERT_TRUE(reg.get_item(iid, item));
        EXPECT_EQ(item.quantity, 2u) << "la quantita' non doveva muoversi alla conferma";
    }
    cleanup_temp_dir(tmpdir);
}

// Il seguito del bug precedente: dopo una conferma un annullamento deve essere
// rifiutato. Se la copia PENDING stantia e' ancora in registro, l'annullamento la
// trova, rimette la quantita' e restituisce true: disponibilita' restituita senza
// che nessun pagamento sia mai avvenuto.
TEST(mevatrust_store, cancelling_after_a_confirm_does_not_restore_quantity)
{
    auto tmpdir = create_temp_dir();
    {
        StoreRegistry reg(tmpdir);
        ASSERT_TRUE(reg.load_from_disk());

        crypto::public_key owner{}, buyer{};
        memset(owner.data, 0xAA, 32);
        memset(buyer.data, 0xBB, 32);

        crypto::hash sid = reg.create_store("S","d","","a", false,"",100,0,owner,1000);
        crypto::hash iid = reg.list_item(sid,"W","w",1000,3,"g","","mvc_only",1000);
        ASSERT_TRUE(reg.buy_item(sid, iid, buyer, 1001, 1000000, 1000, "", 0));

        crypto::hash txid{};
        memset(txid.data, 0xCC, 32);
        ASSERT_TRUE(reg.confirm_purchase(sid, iid, buyer, owner, txid, 1005));

        EXPECT_FALSE(reg.cancel_purchase(sid, iid, buyer, owner, "late", txid, 1006))
            << "un acquisto gia' confermato e' stato annullato";

        StoreItemEntry item{};
        ASSERT_TRUE(reg.get_item(iid, item));
        EXPECT_EQ(item.quantity, 2u)
            << "la quantita' e' stata ripristinata da un annullamento non valido";
    }
    cleanup_temp_dir(tmpdir);
}

// La chiave LMDB degli acquisti era solo la chiave pubblica del buyer. Un buyer
// che comprava due articoli diversi scriveva due righe con la stessa chiave e la
// seconda sovrascriveva la prima: dopo un riavvio spariva un acquisto, senza
// errore e senza traccia.
TEST(mevatrust_store, two_items_bought_by_the_same_buyer_survive_a_restart)
{
    auto tmpdir = create_temp_dir();
    crypto::hash sid{};
    {
        StoreRegistry reg(tmpdir);
        ASSERT_TRUE(reg.load_from_disk());

        crypto::public_key owner{}, buyer{};
        memset(owner.data, 0xAA, 32);
        memset(buyer.data, 0xBB, 32);

        sid = reg.create_store("S","d","","a", false,"",100,0,owner,1000);
        crypto::hash iid1 = reg.list_item(sid,"Uno","w",1000,3,"g","","mvc_only",1000);
        crypto::hash iid2 = reg.list_item(sid,"Due","w",1000,3,"g","","mvc_only",1000);
        ASSERT_NE(iid1, crypto::hash{});
        ASSERT_NE(iid2, crypto::hash{});

        ASSERT_TRUE(reg.buy_item(sid, iid1, buyer, 1001, 1000000, 1000, "", 0));
        ASSERT_TRUE(reg.buy_item(sid, iid2, buyer, 1002, 1000000, 1000, "", 0));

        EXPECT_EQ(reg.get_store_purchases(sid).size(), 2u)
            << "i due acquisti non sono entrambi registrati in memoria";
    }
    // Riavvio: quello che si rilegge e' cio' che la catena potra' vedere.
    {
        StoreRegistry reg2(tmpdir);
        ASSERT_TRUE(reg2.load_from_disk());
        auto purchases = reg2.get_store_purchases(sid);
        ASSERT_EQ(purchases.size(), 2u)
            << "un acquisto e' sparito nel riavvio: la chiave LMDB non distingue i due articoli";
    }
    cleanup_temp_dir(tmpdir);
}

// Quale campo non sopravvive al round-trip? La firma e' firmata sull'hash dei
// campi e verificata sull'hash del campo riletto dalla transazione: se un campo
// coperto dall'hash cambia, non si autentica piu' nulla. Qui ogni campo viene
// confrontato uno a uno, perche' l'hash da solo dice solo "non torna".
TEST(mevatrust_store, store_message_hash_survives_serialization)
{
    crypto::public_key pk{}; memset(pk.data, 0x12, 32);
    tx_extra_mevatrust_store op{};
    op.op = tx_extra_mevatrust_store::STORE_CREATE;
    op.name = "Test Store"; op.description = "A test store";
    op.url = "https://example.com"; op.payment_address = "addr1";
    op.euro_enabled = true; op.euro_details = "IBAN123";
    op.mvc_percent = 70; op.euro_percent = 30; op.owner_pubkey = pk;

    std::vector<uint8_t> extra;
    ASSERT_TRUE(mevatrust::build_mevatrust_store_extra(op, extra));
    transaction tx{}; tx.extra = extra;
    tx_extra_mevatrust_store parsed{};
    ASSERT_TRUE(mevatrust::parse_mevatrust_store_from_tx(tx, parsed));

    EXPECT_EQ((int)parsed.op,               (int)op.op);
    EXPECT_EQ(parsed.store_id,              op.store_id);
    EXPECT_EQ(parsed.item_id,               op.item_id);
    EXPECT_EQ(parsed.name,                  op.name);
    EXPECT_EQ(parsed.description,           op.description);
    EXPECT_EQ(parsed.url,                   op.url);
    EXPECT_EQ(parsed.price,                 op.price);
    EXPECT_EQ(parsed.quantity,              op.quantity);
    EXPECT_EQ(parsed.category,              op.category);
    EXPECT_EQ(parsed.payment_address,       op.payment_address);
    EXPECT_EQ(parsed.euro_enabled,          op.euro_enabled);
    EXPECT_EQ(parsed.euro_details,          op.euro_details);
    EXPECT_EQ(parsed.mvc_percent,           op.mvc_percent);
    EXPECT_EQ(parsed.euro_percent,          op.euro_percent);
    EXPECT_EQ(parsed.payment_mode,          op.payment_mode);
    EXPECT_EQ(parsed.buyer_payment_method,  op.buyer_payment_method);
    EXPECT_EQ(parsed.euro_ref,              op.euro_ref);
    EXPECT_EQ(parsed.euro_amount,           op.euro_amount);
    EXPECT_EQ(parsed.seller_pubkey,         op.seller_pubkey);
    EXPECT_EQ(parsed.cancel_reason,         op.cancel_reason);
    EXPECT_EQ(parsed.confirm_expiry_height, op.confirm_expiry_height);

    // Il punto: i due hash devono coincidere, altrimenti la firma firmata prima
    // della serializzazione non verifica mai su una transazione reale.
    EXPECT_EQ(mevatrust::store_message_hash(parsed), mevatrust::store_message_hash(op))
        << "l'hash del messaggio cambia passando dalla transazione: ogni operazione "
           "store firmata prima di essere serializzata diventerebbe non verificabile";
}
