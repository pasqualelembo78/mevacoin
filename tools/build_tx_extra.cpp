/**
 * build_tx_extra — Construct governance/network-fund tx_extra fields.
 *
 * Usage:
 *   build_tx_extra treasury  <amount>  <to_address>  <signer0_privkey>  <signer1_privkey>
 *   build_tx_extra network   <amount>  <to_address>
 *
 * Outputs the hex-encoded serialised tx_extra field that can be
 * injected into a transaction's `extra` vector.
 *
 * NOTE: The signatures are over tx_prefix_hash computed with the
 * governance field already present, with EMPTY signatures.  See
 * the companion patch to the blockchain validation code for the
 * corresponding hashing convention.
 */

#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>

#include "crypto/crypto.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/tx_extra.h"
#include "serialization/binary_archive.h"
#include "serialization/serialization.h"
#include "string_tools.h"

using namespace cryptonote;

static void print_usage(const char* prog)
{
    std::cerr << "Usage:\n"
              << "  " << prog << " treasury  <amount> <to_address> <signer0_priv> <signer1_priv>\n"
              << "  " << prog << " network   <amount> <to_address>\n";
}

// ── helpers ──────────────────────────────────────────────────────────

static bool hex_to_skey(const std::string& hex, crypto::secret_key& sk)
{
    cryptonote::blobdata bin;
    if (!epee::string_tools::parse_hexstr_to_binbuff(hex, bin) || bin.size() != sizeof(crypto::secret_key))
        return false;
    memcpy(sk.data, bin.data(), sizeof(crypto::secret_key));
    return true;
}

static std::string hex(const void* data, size_t len)
{
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        os << std::setw(2) << (unsigned)((const unsigned char*)data)[i];
    return os.str();
}

// ── main ─────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    if (argc < 4) { print_usage(argv[0]); return 1; }

    std::string mode = argv[1];
    uint64_t amount = 0;
    {
        char* end = nullptr;
        amount = strtoull(argv[2], &end, 10);
        if (!end || *end) { std::cerr << "Invalid amount\n"; return 1; }
    }
    std::string to_addr = argv[3];

    // Parse recipient address
    cryptonote::address_parse_info info;
    if (!cryptonote::get_account_address_from_str(info, cryptonote::MAINNET, to_addr))
    {
        std::cerr << "Invalid MevaCoin address: " << to_addr << "\n";
        return 1;
    }

    std::vector<crypto::secret_key> signer_privkeys;

    if (mode == "treasury")
    {
        if (argc < 6) { print_usage(argv[0]); return 1; }
        for (int i = 4; i < argc; ++i)
        {
            crypto::secret_key sk;
            if (!hex_to_skey(argv[i], sk))
            {
                std::cerr << "Invalid signer private key: " << argv[i] << "\n";
                return 1;
            }
            // Reduce scalar to valid ed25519 scalar
            sc_reduce32((unsigned char*)sk.data);
            signer_privkeys.push_back(sk);
        }
        if (signer_privkeys.size() < 2)
        {
            std::cerr << "Need at least 2 signer private keys\n";
            return 1;
        }
    }
    else if (mode != "network")
    {
        print_usage(argv[0]);
        return 1;
    }

    // ── Build the governance tag ──────────────────────────────────────

    if (mode == "treasury")
    {
        tx_extra_governance_transfer tag;
        tag.amount = amount;
        tag.recipient_spend = info.address.m_spend_public_key;
        tag.recipient_view  = info.address.m_view_public_key;
        // Empty signatures initially — we'll sign then fill
        tag.signatures = {};

        // Serialize to raw bytes (tag + size + data)
        std::stringstream ss;
        binary_archive<true> ar(ss);
        tx_extra_field field = tag;
        if (!::serialization::serialize(ar, field))
        {
            std::cerr << "Failed to serialise governance transfer field\n";
            return 1;
        }
        std::string blob = ss.str();

        // We'll also need to construct a minimal tx_prefix to get the
        // hash that the signatures will cover.  The real hash is
        // computed over the final tx_prefix, which includes the extra
        // field.  We simulate that here with a minimal tx.
        //
        // NOTE: circular-dependency — the extra bytes contain the
        // governance signatures.  To break it we hash the tx_prefix
        // with EMPTY signatures, then insert real sigs.  The
        // blockchain validation code MUST compute the same hash
        // (zeroing out the governance sigs before hashing).  Patch
        // check_premine_spend accordingly.
        transaction_prefix txp;
        txp.version = 2;
        txp.unlock_time = 0;
        txp.extra = std::vector<uint8_t>(blob.begin(), blob.end());

        crypto::hash hash0 = get_transaction_prefix_hash(txp);

        // Sign with each signer
        for (size_t i = 0; i < signer_privkeys.size(); ++i)
        {
            crypto::public_key signer_pub;
            crypto::secret_key_to_public_key(signer_privkeys[i], signer_pub);

            governance_signature gs;
            gs.signer_key = signer_pub;
            crypto::generate_signature(hash0, signer_pub, signer_privkeys[i], gs.sig);
            tag.signatures.push_back(gs);
        }

        // Re-serialize now with real signatures
        ss.str("");
        ss.clear();
        binary_archive<true> ar2(ss);
        tx_extra_field field2 = tag;
        if (!::serialization::serialize(ar2, field2))
        {
            std::cerr << "Failed to serialise final governance transfer\n";
            return 1;
        }
        std::string final_blob = ss.str();

        std::cout << hex(final_blob.data(), final_blob.size());
        std::cerr << "\n"
                  << "Governance treasury transfer:\n"
                  << "  Amount:   " << amount << " atomic units ("
                  << print_money(amount) << ")\n"
                  << "  To:       " << to_addr << "\n"
                  << "  Signers:  " << signer_privkeys.size() << "\n"
                  << "  Bytes:    " << final_blob.size() << "\n"
                  << "  Hash0:    " << hash0 << "\n";
    }
    else // network
    {
        tx_extra_network_fund_transfer tag;
        tag.amount = amount;
        tag.recipient_spend = info.address.m_spend_public_key;
        tag.recipient_view  = info.address.m_view_public_key;

        std::stringstream ss;
        binary_archive<true> ar(ss);
        tx_extra_field field = tag;
        if (!::serialization::serialize(ar, field))
        {
            std::cerr << "Failed to serialise network fund transfer field\n";
            return 1;
        }
        std::string blob = ss.str();

        std::cout << hex(blob.data(), blob.size());
        std::cerr << "\n"
                  << "Network fund transfer (no signatures):\n"
                  << "  Amount:   " << amount << " atomic units ("
                  << print_money(amount) << ")\n"
                  << "  To:       " << to_addr << "\n"
                  << "  Bytes:    " << blob.size() << "\n";
    }

    return 0;
}
