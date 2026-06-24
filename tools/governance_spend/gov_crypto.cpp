#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <vector>

#define CRYPTONOTE_DEFINES_H
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "string_tools.h"
#include "common/base58.h"
#include "cryptonote_config.h"

static std::string hex(const void* data, size_t len)
{
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        os << std::setw(2) << (unsigned)((const unsigned char*)data)[i];
    return os.str();
}

static bool from_hex(const std::string& hex, std::vector<uint8_t>& out)
{
    std::string bin;
    if (!epee::string_tools::parse_hexstr_to_binbuff(hex, bin))
        return false;
    out.assign(bin.begin(), bin.end());
    return true;
}

static bool decode_address(const std::string& addr, crypto::public_key& spend, crypto::public_key& view) {
    uint64_t tag;
    std::string data;
    if (!tools::base58::decode_addr(addr, tag, data))
        return false;
    if (data.size() != 64)
        return false;
    memcpy(spend.data, data.data(), 32);
    memcpy(view.data, data.data() + 32, 32);
    return true;
}

static void print_usage(const char* prog)
{
    std::cerr << "Usage: " << prog << " <command> [args]\n\n"
              << "Commands:\n"
              << "  genkey                                          Generate random key pair\n"
              << "  pubkey <privkey_hex>                            Derive public key from private\n"
              << "  decode <address>                                Decode address to spend+view keys\n"
              << "  sign <privkey_hex> <hash_hex>                   Sign hash with private key\n"
              << "  verify <pubkey_hex> <sig_hex> <hash_hex>        Verify signature\n"
              << "  derive-wallet <domain> <nettype>                Derive deterministic wallet keys\n"
              << "    domain: \"mevacoin_governance\" or \"mevacoin_network_fund\"\n"
              << "    nettype: 0=mainnet 1=testnet 2=stagenet\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2) { print_usage(argv[0]); return 1; }

    std::string cmd = argv[1];

    if (cmd == "sign")
    {
        if (argc < 4) { std::cerr << "Usage: gov_crypto sign <privkey_hex> <hash_hex>\n"; return 1; }
        std::vector<uint8_t> sk_bin, h_bin;
        if (!from_hex(argv[2], sk_bin) || sk_bin.size() != 32) { std::cerr << "Invalid privkey\n"; return 1; }
        if (!from_hex(argv[3], h_bin) || h_bin.size() != 32) { std::cerr << "Invalid hash\n"; return 1; }
        crypto::secret_key sk;
        crypto::hash h;
        memcpy(sk.data, sk_bin.data(), 32);
        memcpy(h.data, h_bin.data(), 32);
        crypto::public_key pk;
        crypto::secret_key_to_public_key(sk, pk);
        crypto::signature sig;
        crypto::generate_signature(h, pk, sk, sig);
        std::cout << hex(&sig, sizeof(sig)) << std::endl;
        return 0;
    }

    if (cmd == "genkey")
    {
        crypto::secret_key sk;
        crypto::public_key pk;
        crypto::generate_keys(pk, sk);
        std::cout << "privkey: " << hex(sk.data, 32) << std::endl;
        std::cout << "pubkey:  " << hex(pk.data, 32) << std::endl;
        return 0;
    }

    if (cmd == "verify")
    {
        if (argc < 5) { std::cerr << "Usage: gov_crypto verify <pubkey_hex> <sig_hex> <hash_hex>\n"; return 1; }
        std::vector<uint8_t> pk_bin, sig_bin, h_bin;
        if (!from_hex(argv[2], pk_bin) || pk_bin.size() != 32) { std::cerr << "Invalid pubkey\n"; return 1; }
        if (!from_hex(argv[3], sig_bin) || sig_bin.size() != 64) { std::cerr << "Invalid signature\n"; return 1; }
        if (!from_hex(argv[4], h_bin) || h_bin.size() != 32) { std::cerr << "Invalid hash\n"; return 1; }
        crypto::public_key pk;
        crypto::signature sig;
        crypto::hash h;
        memcpy(pk.data, pk_bin.data(), 32);
        memcpy(sig.c.data, sig_bin.data(), 32);
        memcpy(sig.r.data, sig_bin.data() + 32, 32);
        memcpy(h.data, h_bin.data(), 32);
        bool ok = crypto::check_signature(h, pk, sig);
        std::cout << (ok ? "VALID" : "INVALID") << std::endl;
        return ok ? 0 : 1;
    }

    if (cmd == "pubkey") {
        if (argc != 3) { std::cerr << "Usage: gov_crypto pubkey <privkey_hex>\n"; return 1; }
        std::vector<uint8_t> sk_bin;
        if (!from_hex(argv[2], sk_bin) || sk_bin.size() != 32) { std::cerr << "Invalid privkey\n"; return 1; }
        crypto::secret_key sk;
        crypto::public_key pk;
        memcpy(sk.data, sk_bin.data(), 32);
        crypto::secret_key_to_public_key(sk, pk);
        std::cout << epee::string_tools::pod_to_hex(pk) << std::endl;
        return 0;
    }

    if (cmd == "decode") {
        if (argc != 3) { std::cerr << "Usage: gov_crypto decode <address>\n"; return 1; }
        crypto::public_key spend, view;
        if (!decode_address(argv[2], spend, view)) {
            std::cerr << "Invalid address\n"; return 1;
        }
        std::cout << "spend: " << epee::string_tools::pod_to_hex(spend) << "\n";
        std::cout << "view:  " << epee::string_tools::pod_to_hex(view) << "\n";
        return 0;
    }

    if (cmd == "derive-wallet")
    {
        if (argc < 4)
        {
            std::cerr << "Usage: gov_crypto derive-wallet <domain> <nettype>\n";
            return 1;
        }

        std::string domain = argv[2];
        int nettype = atoi(argv[3]);

        // Get address prefix for this network type
        uint64_t addr_prefix;
        switch (nettype)
        {
            case 0: addr_prefix = ::config::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX; break;
            case 1: addr_prefix = ::config::testnet::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX; break;
            case 2: addr_prefix = ::config::stagenet::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX; break;
            default:
                std::cerr << "Invalid nettype (0=mainnet, 1=testnet, 2=stagenet)\n";
                return 1;
        }

        // Derive keys: spend = view = hash_to_scalar(cn_fast_hash(domain + nettype_byte))
        std::string data = domain;
        data.push_back(static_cast<char>(nettype));

        crypto::hash h = crypto::cn_fast_hash(data.data(), data.size());

        crypto::secret_key sk;
        crypto::hash_to_scalar(h.data, sizeof(h.data), sk);

        crypto::public_key pk;
        crypto::secret_key_to_public_key(sk, pk);

        // Build address: base58(prefix + spend_pub + view_pub)
        std::string addr_bin;
        addr_bin.append((const char*)pk.data, sizeof(pk.data));
        addr_bin.append((const char*)pk.data, sizeof(pk.data)); // view = spend

        std::string address = tools::base58::encode_addr(addr_prefix, addr_bin);

        std::cout << "spend_sec: " << hex(sk.data, 32) << std::endl;
        std::cout << "view_sec:  " << hex(sk.data, 32) << std::endl;
        std::cout << "spend_pub: " << hex(pk.data, 32) << std::endl;
        std::cout << "view_pub:  " << hex(pk.data, 32) << std::endl;
        std::cout << "address:   " << address << std::endl;
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
