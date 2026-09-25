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
#include "cryptonote_core/mevatrust/frost_threshold.h"
extern "C" {
#include "crypto/keccak.h"
}

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
              << "  genwallet [nettype]                             Generate full wallet (keys+address)\n"
              << "    nettype: 0=mainnet 1=testnet 2=stagenet (default 0)\n"
              << "  wallet-from-spend <spend_sec_hex> [nettype]      Build wallet from existing spend key\n"
              << "  pubkey <privkey_hex>                            Derive public key from private\n"
              << "  viewkey <spend_priv_hex>                        Derive view private key from spend private\n"
              << "  decode <address>                                Decode address to spend+view keys\n"
              << "  sign <privkey_hex> <hash_hex>                   Sign hash with private key\n"
              << "  verify <pubkey_hex> <sig_hex> <hash_hex>        Verify signature\n"
              << "  derive-wallet <domain> <nettype>                Derive deterministic wallet keys\n"
              << "    domain: \"mevacoin_governance\" or \"mevacoin_network_fund\"\n"
              << "    nettype: 0=mainnet 1=testnet 2=stagenet\n"
              << "  frost-keygen                                     Generate MevaTrust pool distribution FROST ceremony set\n"
              << "  frost-verify <R> <z> <msg_hash> <group_pubkey>   Verify a FROST aggregate signature\n";
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

    if (cmd == "genwallet")
    {
        int nettype = 0;
        if (argc >= 3) nettype = atoi(argv[2]);

        uint64_t addr_prefix;
        switch (nettype)
        {
            case 0: addr_prefix = ::config::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX; break;
            case 1: addr_prefix = ::config::testnet::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX; break;
            case 2: addr_prefix = ::config::stagenet::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX; break;
            default: std::cerr << "Invalid nettype (0=mainnet, 1=testnet, 2=stagenet)\n"; return 1;
        }

        // Generate random spend key pair
        crypto::public_key spend_pub;
        crypto::secret_key spend_sec;
        crypto::generate_keys(spend_pub, spend_sec);

        // Derive view private key from spend private key (matching wallet2 derivation)
        // NOTE: must be reduced mod L, else secret_key_to_public_key (sc_check) fails
        // and the resulting view_pub is garbage -> unspendable address.
        crypto::secret_key view_sec;
        crypto::hash_to_scalar(spend_sec.data, sizeof(spend_sec), view_sec);

        // Compute view public key
        crypto::public_key view_pub;
        crypto::secret_key_to_public_key(view_sec, view_pub);

        // Build address: base58(prefix + spend_pub + view_pub)
        std::string addr_bin;
        addr_bin.append((const char*)spend_pub.data, sizeof(spend_pub));
        addr_bin.append((const char*)view_pub.data, sizeof(view_pub));
        std::string address = tools::base58::encode_addr(addr_prefix, addr_bin);

        std::cout << "address:    " << address << std::endl;
        std::cout << "spend_sec:  " << hex(spend_sec.data, 32) << std::endl;
        std::cout << "view_sec:   " << hex(view_sec.data, 32) << std::endl;
        std::cout << "spend_pub:  " << hex(spend_pub.data, 32) << std::endl;
        std::cout << "view_pub:   " << hex(view_pub.data, 32) << std::endl;
        return 0;
    }

    if (cmd == "wallet-from-spend")
    {
        if (argc < 3 || argc > 4)
        {
            std::cerr << "Usage: gov_crypto wallet-from-spend <spend_sec_hex> [nettype]\n";
            return 1;
        }
        int nettype = 0;
        if (argc == 4) nettype = atoi(argv[3]);

        uint64_t addr_prefix;
        switch (nettype)
        {
            case 0: addr_prefix = ::config::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX; break;
            case 1: addr_prefix = ::config::testnet::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX; break;
            case 2: addr_prefix = ::config::stagenet::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX; break;
            default: std::cerr << "Invalid nettype (0=mainnet, 1=testnet, 2=stagenet)\n"; return 1;
        }

        std::vector<uint8_t> sk_bin;
        if (!from_hex(argv[2], sk_bin) || sk_bin.size() != 32) { std::cerr << "Invalid spend private key\n"; return 1; }

        crypto::public_key spend_pub;
        crypto::secret_key spend_sec;
        memcpy(spend_sec.data, sk_bin.data(), 32);
        if (!crypto::secret_key_to_public_key(spend_sec, spend_pub))
        {
            std::cerr << "Invalid spend private key (not reduced mod L)\n";
            return 1;
        }

        crypto::secret_key view_sec;
        crypto::hash_to_scalar(spend_sec.data, sizeof(spend_sec), view_sec);
        crypto::public_key view_pub;
        crypto::secret_key_to_public_key(view_sec, view_pub);

        std::string addr_bin;
        addr_bin.append((const char*)spend_pub.data, sizeof(spend_pub));
        addr_bin.append((const char*)view_pub.data, sizeof(view_pub));
        std::string address = tools::base58::encode_addr(addr_prefix, addr_bin);

        std::cout << "address:    " << address << std::endl;
        std::cout << "spend_sec:  " << hex(spend_sec.data, 32) << std::endl;
        std::cout << "view_sec:   " << hex(view_sec.data, 32) << std::endl;
        std::cout << "spend_pub:  " << hex(spend_pub.data, 32) << std::endl;
        std::cout << "view_pub:   " << hex(view_pub.data, 32) << std::endl;
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

    if (cmd == "viewkey")
    {
        if (argc != 3) { std::cerr << "Usage: gov_crypto viewkey <spend_priv_hex>\n"; return 1; }
        std::vector<uint8_t> sk_bin;
        if (!from_hex(argv[2], sk_bin) || sk_bin.size() != 32) { std::cerr << "Invalid privkey\n"; return 1; }
        crypto::secret_key sk;
        memcpy(sk.data, sk_bin.data(), 32);
        crypto::secret_key view_sec;
        crypto::hash_to_scalar(sk.data, sizeof(sk), view_sec);
        std::cout << epee::string_tools::pod_to_hex(view_sec) << std::endl;
        return 0;
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
        std::cout << "view: " << epee::string_tools::pod_to_hex(view) << "\n";
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
        std::cout << "view_sec: " << hex(sk.data, 32) << std::endl;
        std::cout << "spend_pub: " << hex(pk.data, 32) << std::endl;
        std::cout << "view_pub: " << hex(pk.data, 32) << std::endl;
        std::cout << "address: " << address << std::endl;
        return 0;
    }

    if (cmd == "frost-keygen")
    {
        using namespace cryptonote::mevatrust::frost;
        FrostKeyPackage pkg;
        if (!frost_keygen(pkg)) { std::cerr << "frost_keygen failed\n"; return 1; }
        std::cout << "group_public_key: " << hex(&pkg.group_public_key, 32) << "\n";
        for (int i = 0; i < FROST_N; ++i)
            std::cout << "signer_pub[" << (int)pkg.signer_keypairs[i].index << "]: "
                      << hex(&pkg.signer_keypairs[i].pub, 32) << "\n";
        std::cout << "--- private shares (ceremony secret, do not distribute in public logs) ---\n";
        for (int i = 0; i < FROST_N; ++i)
            std::cout << "signer_sec[" << (int)pkg.signer_keypairs[i].index << "]: "
                      << hex(&pkg.signer_keypairs[i].sec, 32) << "\n";
        return 0;
    }

    if (cmd == "frost-verify")
    {
        if (argc != 6) { std::cerr << "Usage: gov_crypto frost-verify <R> <z> <msg_hash> <group_pubkey>\n"; return 1; }
        using namespace cryptonote::mevatrust::frost;
        FrostSignature sig;
        std::vector<uint8_t> rv, zv, mv, yv;
        if (!from_hex(argv[2], rv) || !from_hex(argv[3], zv) || !from_hex(argv[4], mv) || !from_hex(argv[5], yv))
        { std::cerr << "bad hex\n"; return 1; }
        if (rv.size() != 32 || zv.size() != 32 || mv.size() != 32 || yv.size() != 32)
        { std::cerr << "bad length\n"; return 1; }
        memcpy(&sig.R, rv.data(), 32);
        memcpy(&sig.z, zv.data(), 32);
        memcpy(sig.msg_hash.data, mv.data(), 32);
        PublicKeyPackage pkg;
        pkg.agg_pubkey = *reinterpret_cast<crypto::public_key*>(yv.data());
        pkg.signer_pubkeys = CONSENSUS_PROPOSER_PUBKEYS;
        std::string err;
        if (!verify_signature(sig, pkg, err)) { std::cerr << "INVALID: " << err << "\n"; return 1; }
        std::cout << "VALID\n";
        return 0;
    }

    if (cmd == "frost-test")
    {
        using namespace cryptonote::mevatrust::frost;
        if (argc < 4) { std::cerr << "Usage: gov_crypto frost-test <sec1> <sec2> <sec3>\n"; return 1; }
        std::vector<uint8_t> s1, s2, s3;
        if (!from_hex(argv[2], s1) || !from_hex(argv[3], s2) || !from_hex(argv[4], s3)) { std::cerr << "bad hex\n"; return 1; }
        crypto::secret_key k1, k2, k3;
        memcpy(&k1, s1.data(), 32); memcpy(&k2, s2.data(), 32); memcpy(&k3, s3.data(), 32);

        // message
        crypto::public_key rp; memset(rp.data, 7, 32);
        std::vector<std::pair<crypto::public_key, uint64_t>> outs = { {rp, 12345}, {rp, 42} };
        crypto::hash msg = create_distribution_message_hash(1000, 5, outs);

        // signer indices: 1,2,3
        std::vector<uint8_t> indices = {1, 2, 3};
        std::vector<crypto::ec_scalar> lambdas;
        if (!compute_lagrange_coeffs(indices, lambdas)) { std::cerr << "lagrange failed\n"; return 1; }

        // round 1: nonces + commitments
        NoncePair n1, n2, n3;
        generate_nonces(n1); generate_nonces(n2); generate_nonces(n3);
        crypto::public_key D1, E1, D2, E2, D3, E3;
        nonce_commitments(n1, D1, E1);
        nonce_commitments(n2, D2, E2);
        nonce_commitments(n3, D3, E3);

        // binding factors
        std::vector<std::pair<crypto::public_key, crypto::public_key>> comms = {{D1,E1},{D2,E2},{D3,E3}};
        std::vector<crypto::ec_scalar> rho;
        if (!compute_binding_factors(indices, msg, comms, rho)) { std::cerr << "rho failed\n"; return 1; }

        // aggregate R
        crypto::public_key R;
        if (!compute_aggregate_r(comms, rho, R)) { std::cerr << "aggregate_r failed\n"; return 1; }

        // round 2: sign partials
        PartialSignature p1, p2, p3;
        p1.signer_index = 1; p2.signer_index = 2; p3.signer_index = 3;
        if (!sign_partial(msg, n1, k1, lambdas[0], rho[0], R, CONSENSUS_GROUP_PUBKEY, p1)) { std::cerr << "sign1 failed\n"; return 1; }
        if (!sign_partial(msg, n2, k2, lambdas[1], rho[1], R, CONSENSUS_GROUP_PUBKEY, p2)) { std::cerr << "sign2 failed\n"; return 1; }
        if (!sign_partial(msg, n3, k3, lambdas[2], rho[2], R, CONSENSUS_GROUP_PUBKEY, p3)) { std::cerr << "sign3 failed\n"; return 1; }

        // verify each partial
        std::string err;
        if (!verify_partial(msg, p1, lambdas[0], rho[0], R, CONSENSUS_GROUP_PUBKEY, CONSENSUS_PROPOSER_PUBKEYS[0], err)) { std::cerr << "verify p1: " << err << "\n"; return 1; }
        if (!verify_partial(msg, p2, lambdas[1], rho[1], R, CONSENSUS_GROUP_PUBKEY, CONSENSUS_PROPOSER_PUBKEYS[1], err)) { std::cerr << "verify p2: " << err << "\n"; return 1; }
        if (!verify_partial(msg, p3, lambdas[2], rho[2], R, CONSENSUS_GROUP_PUBKEY, CONSENSUS_PROPOSER_PUBKEYS[2], err)) { std::cerr << "verify p3: " << err << "\n"; return 1; }
        std::cout << "partial verify: OK\n";

        // also check that an INVALID partial is caught
        PartialSignature bad = p1;
        bad.sig_share.data[0] ^= 1;
        if (verify_partial(msg, bad, lambdas[0], rho[0], R, CONSENSUS_GROUP_PUBKEY, CONSENSUS_PROPOSER_PUBKEYS[0], err)) { std::cerr << "BAD: corrupted partial verified\n"; return 1; }
        std::cout << "negative partial: OK (" << err << ")\n";

        // aggregate (recomputes R from commitments)
        FrostSignature sig;
        std::vector<PartialSignature> partials = {p1, p2, p3};
        if (!aggregate_signatures(msg, indices, partials, rho, CONSENSUS_GROUP_PUBKEY, sig)) { std::cerr << "aggregate failed\n"; return 1; }

        // full verify against hardcoded ceremony Y
        PublicKeyPackage pkg;
        pkg.agg_pubkey = CONSENSUS_GROUP_PUBKEY;
        pkg.signer_pubkeys = CONSENSUS_PROPOSER_PUBKEYS;
        if (!verify_signature(sig, pkg, err)) { std::cerr << "final verify FAIL: " << err << "\n"; return 1; }
        std::cout << "final verify (vs compact Y): OK\n";
        std::cout << "R: " << hex(&sig.R, 32) << "\n";
        std::cout << "z: " << hex(&sig.z, 32) << "\n";
        std::cout << "msg: " << hex(&sig.msg_hash, 32) << "\n";
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
