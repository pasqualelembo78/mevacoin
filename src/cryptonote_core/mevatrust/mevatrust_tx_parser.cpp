// Copyright (c) 2024, The Mevacoin Project
// mevatrust_tx_parser.cpp — FASE 2
#include "mevatrust_tx_parser.h"
#include "mevatrust_consensus.h"
#include "serialization/binary_utils.h"
#include "crypto/crypto.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/account.h"
#include "ringct/rctSigs.h"
#include "device/device.hpp"
#include "../foundation_vesting.h"
#include "misc_log_ex.h"
#include <cstring>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.parser"

namespace cryptonote { namespace mevatrust {

static bool find_mevatrust_tag(const std::vector<uint8_t>& extra, uint8_t target_tag, std::vector<uint8_t>& blob_out) {
    const size_t n = extra.size(); size_t i = 0;
    while (i < n) {
        uint8_t tag = extra[i++];
        if (tag == 0x00) { while (i < n && extra[i] == 0x00) ++i; continue; }
        if (tag == 0x01) { if (i + 32 > n) return false; i += 32; continue; }
        if (tag == TX_EXTRA_TAG_MEVATRUST_REGISTRATION ||
            tag == TX_EXTRA_TAG_MEVATRUST_DEREGISTER   ||
            tag == TX_EXTRA_TAG_MEVATRUST_SNAPSHOT     ||
            tag == TX_EXTRA_TAG_MEVATRUST_CIRCLE       ||
            tag == TX_EXTRA_TAG_MEVATRUST_PENALTY      ||
            tag == TX_EXTRA_TAG_MEVATRUST_UPTIME       ||
            tag == TX_EXTRA_TAG_MEVATRUST_CHALLENGE    ||
            tag == TX_EXTRA_TAG_MEVATRUST_STATE_ROOT   ||
            tag == TX_EXTRA_TAG_MEVATRUST_STORE        ||
            tag == TX_EXTRA_TAG_MEVATRUST_CIRCLE_VOTE ||
            tag == TX_EXTRA_TAG_MEVATRUST_POOL_DISTRIBUTION ||
            tag == TX_EXTRA_TAG_MEVATRUST_VALIDATOR ||
            tag == TX_EXTRA_TAG_MEVATRUST_CHAIN_PROOF) {
            if (i + 2 > n) return false;
            uint16_t len = static_cast<uint16_t>(extra[i]) | (static_cast<uint16_t>(extra[i+1]) << 8);
            i += 2;
            if (i + len > n) return false;
            if (tag == target_tag) { blob_out.assign(extra.begin()+i, extra.begin()+i+len); return true; }
            i += len;
        } else {
            if (i >= n) return false;
            uint64_t len = 0; int shift = 0;
            while (i < n) { uint8_t b = extra[i++]; len |= static_cast<uint64_t>(b & 0x7F) << shift; if (!(b & 0x80)) break; shift += 7; if (shift >= 63) return false; }
            if (i + static_cast<size_t>(len) > n) return false;
            i += static_cast<size_t>(len);
        }
    }
    return false;
}

static bool pack_blob(uint8_t tag, const std::string& blob, std::vector<uint8_t>& out) {
    if (blob.size() > 0xFFFF) return false;
    uint16_t len = static_cast<uint16_t>(blob.size());
    out.push_back(tag); out.push_back(len & 0xFF); out.push_back((len >> 8) & 0xFF);
    out.insert(out.end(), blob.begin(), blob.end()); return true;
}

template<typename T>
static bool parse_tagged(const transaction& tx, uint8_t tag, const std::string& name, T& out) {
    try {
        std::vector<uint8_t> b;
        if (!find_mevatrust_tag(tx.extra, tag, b)) return false;
        return ::serialization::parse_binary(std::string(b.begin(),b.end()), out);
    } catch (const std::exception& e) {
        MWARNING("mevatrust parser: " << name << " failed: " << e.what());
        return false;
    }
}

bool parse_mevatrust_registration_from_tx(const transaction& tx, tx_extra_mevatrust_registration& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_REGISTRATION, "registration", out);
}
bool parse_mevatrust_deregister_from_tx(const transaction& tx, tx_extra_mevatrust_deregister& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_DEREGISTER, "deregister", out);
}
bool parse_mevatrust_snapshot_from_tx(const transaction& tx, tx_extra_mevatrust_snapshot& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_SNAPSHOT, "snapshot", out);
}

bool build_mevatrust_registration_extra(const tx_extra_mevatrust_registration& reg, std::vector<uint8_t>& o) { std::string b; tx_extra_mevatrust_registration c=reg; if (!::serialization::dump_binary(c,b)) return false; return pack_blob(TX_EXTRA_TAG_MEVATRUST_REGISTRATION,b,o); }
bool build_mevatrust_deregister_extra(const tx_extra_mevatrust_deregister& dereg, std::vector<uint8_t>& o) { std::string b; tx_extra_mevatrust_deregister c=dereg; if (!::serialization::dump_binary(c,b)) return false; return pack_blob(TX_EXTRA_TAG_MEVATRUST_DEREGISTER,b,o); }
bool build_mevatrust_snapshot_extra(const tx_extra_mevatrust_snapshot& snap, std::vector<uint8_t>& o) { std::string b; tx_extra_mevatrust_snapshot c=snap; if (!::serialization::dump_binary(c,b)) return false; return pack_blob(TX_EXTRA_TAG_MEVATRUST_SNAPSHOT,b,o); }

bool verify_registration_signature(const tx_extra_mevatrust_registration& reg) {
    try { return crypto::check_signature(registration_message_hash(reg.node_id, reg.node_pubkey), reg.wallet_pubkey, reg.signature); } catch (const std::exception& e) { MWARNING("mevatrust: verify_registration_signature failed: " << e.what()); return false; }
}
bool verify_deregister_signature(const tx_extra_mevatrust_deregister& dereg) {
    try { return crypto::check_signature(deregister_message_hash(dereg.node_id), dereg.wallet_pubkey, dereg.signature); } catch (const std::exception& e) { MWARNING("mevatrust: verify_deregister_signature failed: " << e.what()); return false; }
}

bool parse_mevatrust_circle_from_tx(const transaction& tx, tx_extra_mevatrust_circle& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_CIRCLE, "circle", out);
}

bool build_mevatrust_circle_extra(const tx_extra_mevatrust_circle& op, std::vector<uint8_t>& o) {
    std::string b; tx_extra_mevatrust_circle c = op; if (!::serialization::dump_binary(c, b)) return false;
    return pack_blob(TX_EXTRA_TAG_MEVATRUST_CIRCLE, b, o);
}

bool verify_circle_signature(const tx_extra_mevatrust_circle& op) {
    try {
        crypto::hash h = circle_message_hash(op.circle_id, static_cast<uint8_t>(op.op_type),
                                              op.target_pubkey, op.circle_name);
        return crypto::check_signature(h, op.signer_pubkey, op.signature);
    } catch (const std::exception& e) { MWARNING("mevatrust: verify_circle_signature failed: " << e.what()); return false; }
}

// ── Tag 0xA4: Penalty ──────────────────────────────────────────────────────
bool parse_mevatrust_penalty_from_tx(const transaction& tx, tx_extra_mevatrust_penalty& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_PENALTY, "penalty", out);
}
bool build_mevatrust_penalty_extra(const tx_extra_mevatrust_penalty& p, std::vector<uint8_t>& o) {
    std::string b; tx_extra_mevatrust_penalty c = p; if (!::serialization::dump_binary(c,b)) return false;
    return pack_blob(TX_EXTRA_TAG_MEVATRUST_PENALTY, b, o);
}
bool verify_penalty_signature(const tx_extra_mevatrust_penalty& p) {
    try { return crypto::check_signature(penalty_message_hash(p), p.signer_pubkey, p.signature); } catch (const std::exception& e) { MWARNING("mevatrust: verify_penalty_signature failed: " << e.what()); return false; }
}

// ── Tag 0xA5: Uptime Commitment ───────────────────────────────────────────
bool parse_mevatrust_uptime_from_tx(const transaction& tx, tx_extra_mevatrust_uptime& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_UPTIME, "uptime", out);
}
bool build_mevatrust_uptime_extra(const tx_extra_mevatrust_uptime& u, std::vector<uint8_t>& o) {
    std::string b; tx_extra_mevatrust_uptime c = u; if (!::serialization::dump_binary(c,b)) return false;
    return pack_blob(TX_EXTRA_TAG_MEVATRUST_UPTIME, b, o);
}
bool verify_uptime_signature(const tx_extra_mevatrust_uptime& u) {
    try { return crypto::check_signature(uptime_message_hash(u), u.node_pubkey, u.signature); } catch (const std::exception& e) { MWARNING("mevatrust: verify_uptime_signature failed: " << e.what()); return false; }
}

// ── Tag 0xA6: Challenge Result ────────────────────────────────────────────
bool parse_mevatrust_challenge_from_tx(const transaction& tx, tx_extra_mevatrust_challenge& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_CHALLENGE, "challenge", out);
}
bool verify_challenge_signatures(const tx_extra_mevatrust_challenge& c) {
    try {
        crypto::hash h = challenge_message_hash(c);
        if (!crypto::check_signature(h, c.challenger_pubkey, c.challenger_sig)) return false;
        if (!crypto::check_signature(h, c.challenged_pubkey, c.challenged_sig)) return false;
        return true;
    } catch (const std::exception& e) { MWARNING("mevatrust: verify_challenge_signatures failed: " << e.what()); return false; }
}

// ── Tag 0xAC: Chain Proof (Anti-Sybil Fase 2) ──────────────────────────────
bool chain_proof_target(const crypto::hash& node_id, uint64_t anchor_height, uint64_t& target_out) {
    using namespace cryptonote::mevatrust::consensus;
    // Sotto questa soglia non esiste un bersaglio a profondita' minima: la catena
    // e' troppo corta perche' la domanda abbia senso, e accettare una prova qui
    // vorrebbe dire ammettere risposte senza profondita'.
    if (anchor_height < CHAIN_PROOF_MIN_DEPTH + CHAIN_PROOF_SPAN)
        return false;
    std::string m;
    m.append(reinterpret_cast<const char*>(node_id.data), sizeof(node_id.data));
    m.append(reinterpret_cast<const char*>(&anchor_height), sizeof(anchor_height));
    const crypto::hash h = crypto::cn_fast_hash(m.data(), m.size());
    uint64_t r = 0;
    static_assert(sizeof(r) <= sizeof(h.data), "hash too small");
    std::memcpy(&r, h.data, sizeof(r));
    target_out = anchor_height - CHAIN_PROOF_MIN_DEPTH - (r % CHAIN_PROOF_SPAN);
    return true;
}

bool parse_mevatrust_chain_proof_from_tx(const transaction& tx, tx_extra_mevatrust_chain_proof& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_CHAIN_PROOF, "chain_proof", out);
}
bool build_mevatrust_chain_proof_extra(const tx_extra_mevatrust_chain_proof& cp, std::vector<uint8_t>& o) {
    std::string b; tx_extra_mevatrust_chain_proof c = cp; if (!::serialization::dump_binary(c,b)) return false;
    return pack_blob(TX_EXTRA_TAG_MEVATRUST_CHAIN_PROOF, b, o);
}
bool verify_chain_proof_signature(const tx_extra_mevatrust_chain_proof& cp) {
    try { return crypto::check_signature(chain_proof_message_hash(cp), cp.node_pubkey, cp.signature); }
    catch (const std::exception& e) { MWARNING("mevatrust: verify_chain_proof_signature failed: " << e.what()); return false; }
}

// ── Tag 0xA7: State Root ───────────────────────────────────────────────────
bool parse_mevatrust_state_root_from_tx(const transaction& tx, tx_extra_mevatrust_state_root& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_STATE_ROOT, "state_root", out);
}
bool build_mevatrust_state_root_extra(const tx_extra_mevatrust_state_root& sr, std::vector<uint8_t>& o) {
    std::string b; tx_extra_mevatrust_state_root c = sr; if (!::serialization::dump_binary(c,b)) return false;
    return pack_blob(TX_EXTRA_TAG_MEVATRUST_STATE_ROOT, b, o);
}

// ── Tag 0xA8: Store ─────────────────────────────────────────────────────────
bool parse_mevatrust_store_from_tx(const transaction& tx, tx_extra_mevatrust_store& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_STORE, "store", out);
}

bool build_mevatrust_store_extra(const tx_extra_mevatrust_store& op, std::vector<uint8_t>& o) {
    std::string b; tx_extra_mevatrust_store c = op;
    if (!::serialization::dump_binary(c,b)) return false;
    return pack_blob(TX_EXTRA_TAG_MEVATRUST_STORE, b, o);
}

bool verify_store_signature(const tx_extra_mevatrust_store& op) {
    try {
        crypto::hash h = store_message_hash(op);
        return crypto::check_signature(h, op.owner_pubkey, op.owner_sig);
    } catch (const std::exception& e) { MWARNING("mevatrust: verify_store_signature failed: " << e.what()); return false; }
}

bool verify_store_confirm_signature(const tx_extra_mevatrust_store& op) {
    try {
        crypto::hash h = store_confirm_message_hash(op);
        return crypto::check_signature(h, op.seller_pubkey, op.seller_sig);
    } catch (const std::exception& e) { MWARNING("mevatrust: verify_store_confirm_signature failed: " << e.what()); return false; }
}

bool verify_store_cancel_signature(const tx_extra_mevatrust_store& op) {
    try {
        crypto::hash h = store_confirm_message_hash(op);
        return crypto::check_signature(h, op.seller_pubkey, op.seller_sig);
    } catch (const std::exception& e) { MWARNING("mevatrust: verify_store_cancel_signature failed: " << e.what()); return false; }
}

bool verify_store_buyer_cancel_signature(const tx_extra_mevatrust_store& op) {
    try {
        crypto::hash h = buyer_action_message_hash(op);
        return crypto::check_signature(h, op.buyer_pubkey, op.owner_sig);
    } catch (const std::exception& e) { MWARNING("mevatrust: verify_store_buyer_cancel_signature failed: " << e.what()); return false; }
}

bool verify_store_buyer_confirm_receipt_signature(const tx_extra_mevatrust_store& op) {
    try {
        crypto::hash h = buyer_action_message_hash(op);
        return crypto::check_signature(h, op.buyer_pubkey, op.owner_sig);
    } catch (const std::exception& e) { MWARNING("mevatrust: verify_store_buyer_confirm_receipt_signature failed: " << e.what()); return false; }
}

// ── Tag 0xA9: Circle Vote ───────────────────────────────────────────────────
bool parse_mevatrust_circle_vote_from_tx(const transaction& tx, tx_extra_mevatrust_circle_vote& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_CIRCLE_VOTE, "circle_vote", out);
}

bool build_mevatrust_circle_vote_extra(const tx_extra_mevatrust_circle_vote& op, std::vector<uint8_t>& o) {
    std::string b; tx_extra_mevatrust_circle_vote c = op;
    if (!::serialization::dump_binary(c,b)) return false;
    return pack_blob(TX_EXTRA_TAG_MEVATRUST_CIRCLE_VOTE, b, o);
}

bool verify_circle_vote_signature(const tx_extra_mevatrust_circle_vote& op) {
    try {
        crypto::hash h = circle_vote_message_hash(op);
        return crypto::check_signature(h, op.signer_pubkey, op.signature);
    } catch (const std::exception& e) { MWARNING("mevatrust: verify_circle_vote_signature failed: " << e.what()); return false; }
}

// ── Tag 0xAA: Pool Distribution ────────────────────────────────────────────
bool parse_mevatrust_pool_distribution_from_tx(const transaction& tx, tx_extra_mevatrust_pool_distribution& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_POOL_DISTRIBUTION, "pool_distribution", out);
}
bool build_mevatrust_pool_distribution_extra(const tx_extra_mevatrust_pool_distribution& dist, std::vector<uint8_t>& o) {
    std::string b; tx_extra_mevatrust_pool_distribution c = dist;
    if (!::serialization::dump_binary(c,b)) return false;
    return pack_blob(TX_EXTRA_TAG_MEVATRUST_POOL_DISTRIBUTION, b, o);
}

// ── Tag 0xAB: Validator ────────────────────────────────────────────────────
bool parse_mevatrust_validator_from_tx(const transaction& tx, tx_extra_mevatrust_validator& out) {
    return parse_tagged(tx, TX_EXTRA_TAG_MEVATRUST_VALIDATOR, "validator", out);
}
bool build_mevatrust_validator_extra(const tx_extra_mevatrust_validator& v, std::vector<uint8_t>& o) {
    std::string b; tx_extra_mevatrust_validator c = v;
    if (!::serialization::dump_binary(c,b)) return false;
    return pack_blob(TX_EXTRA_TAG_MEVATRUST_VALIDATOR, b, o);
}
bool verify_validator_signature(const tx_extra_mevatrust_validator& v) {
    const crypto::hash h = validator_message_hash(v.node_id, v.wallet_pubkey);
    return crypto::check_signature(h, v.wallet_pubkey, v.signature);
}

// ── Anti-Sybil Fase 3: bond di registrazione (importo verso il network fund) ─
//
// Riconosce un output appartenente a `addr` ricostruendo la derivazione, come fa
// is_out_to_acc(): prima con la tx pubkey principale, poi con le additional tx
// pubkeys (una per output). L'indice e' quello GLOBALE del vout.
static bool derive_owned_output(const transaction& tx, size_t out_index,
                                const account_public_address& addr,
                                const crypto::secret_key& view_sk,
                                const crypto::public_key& tx_pub_key,
                                const std::vector<crypto::public_key>& additional_tx_pub_keys,
                                crypto::key_derivation& deriv_out)
{
    crypto::public_key out_pub;
    if (!get_output_public_key(tx.vout[out_index], out_pub))
        return false;

    const auto matches = [&](const crypto::public_key& pub_r) -> bool {
        crypto::key_derivation d;
        if (!crypto::generate_key_derivation(pub_r, view_sk, d))
            return false;
        crypto::public_key derived;
        if (!crypto::derive_public_key(d, out_index, addr.m_spend_public_key, derived))
            return false;
        if (derived == out_pub) { deriv_out = d; return true; }
        return false;
    };

    if (matches(tx_pub_key))
        return true;
    if (!additional_tx_pub_keys.empty() && out_index < additional_tx_pub_keys.size())
        return matches(additional_tx_pub_keys[out_index]);
    return false;
}

// Decodifica l'importo dell'output: trasparente se non-RCT, altrimenti via ECDH.
static uint64_t decode_owned_output_amount(const transaction& tx, const crypto::key_derivation& deriv,
                                           size_t out_index, hw::device& hwdev)
{
    if (out_index >= tx.vout.size())
        return 0;
    if (tx.vout[out_index].amount > 0)   // trasparente (non-RCT / v1)
        return tx.vout[out_index].amount;

    crypto::secret_key scalar1;
    hwdev.derivation_to_scalar(deriv, out_index, scalar1);
    rct::key mask;
    try {
        switch (tx.rct_signatures.type) {
        case rct::RCTTypeSimple:
        case rct::RCTTypeBulletproof:
        case rct::RCTTypeBulletproof2:
        case rct::RCTTypeCLSAG:
        case rct::RCTTypeBulletproofPlus:
            return rct::decodeRctSimple(tx.rct_signatures, rct::sk2rct(scalar1), out_index, mask, hwdev);
        case rct::RCTTypeFull:
            return rct::decodeRct(tx.rct_signatures, rct::sk2rct(scalar1), out_index, mask, hwdev);
        default:
            return 0;
        }
    } catch (const std::exception& e) {
        MWARNING("mevatrust: decode_owned_output_amount failed: " << e.what());
        return 0;
    }
}

// Scansiona gli output della tx appartenenti a `addr`. Per ogni output
// riconosciuto: se key_images != nullptr ne aggiunge la key image (derivando la
// one-time secret key, possibile perche' la spend key del destinatario e'
// nota), e somma l'importo in total_amount. Il consenso la usa per proteggere
// gli output verso la tesoreria.
static void scan_address_outputs(const transaction& tx,
                                 const account_public_address& addr,
                                 const crypto::secret_key& view_sk,
                                 const crypto::secret_key& spend_sk,
                                 std::vector<crypto::key_image>* key_images,
                                 uint64_t* total_amount)
{
    const crypto::public_key tx_pub_key = get_tx_pub_key_from_extra(tx);
    if (tx_pub_key == crypto::null_pkey)
        return;
    const std::vector<crypto::public_key> additional_tx_pub_keys =
        get_additional_tx_pub_keys_from_extra(tx);

    hw::device& hwdev = hw::get_device("default");
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        crypto::key_derivation deriv;
        if (!derive_owned_output(tx, i, addr, view_sk, tx_pub_key, additional_tx_pub_keys, deriv))
            continue;
        if (total_amount)
            *total_amount += decode_owned_output_amount(tx, deriv, i, hwdev);
        if (key_images) {
            crypto::public_key out_pub;
            if (!get_output_public_key(tx.vout[i], out_pub))
                continue;
            crypto::secret_key out_sec;
            crypto::derive_secret_key(deriv, i, spend_sk, out_sec);
            crypto::key_image ki;
            crypto::generate_key_image(out_pub, out_sec, ki);
            key_images->push_back(ki);
        }
    }
}

uint64_t network_fund_deposit_amount(const transaction& tx, network_type nettype) {
    uint64_t total = 0;
    scan_address_outputs(tx, get_network_fund_address(nettype),
                         derive_premine_secret_key("mevacoin_network_fund", nettype),
                         derive_premine_secret_key("mevacoin_network_fund", nettype),
                         nullptr, &total);
    return total;
}

uint64_t governance_deposit_amount(const transaction& tx, network_type nettype) {
    uint64_t total = 0;
    scan_address_outputs(tx, get_governance_address(nettype),
                         derive_premine_secret_key("mevacoin_governance", nettype),
                         derive_premine_secret_key("mevacoin_governance", nettype),
                         nullptr, &total);
    return total;
}

void governance_owned_outputs(const transaction& tx, network_type nettype,
                              std::vector<crypto::key_image>& key_images,
                              uint64_t& total_amount) {
    scan_address_outputs(tx, get_governance_address(nettype),
                         derive_premine_secret_key("mevacoin_governance", nettype),
                         derive_premine_secret_key("mevacoin_governance", nettype),
                         &key_images, &total_amount);
}

}} // namespace cryptonote::mevatrust





