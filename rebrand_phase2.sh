#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════
# MEVACOIN REBRANDING — Phase 2: Remaining references cleanup
# Run AFTER rebrand_mevacoin.sh (Phase 1)
# ═══════════════════════════════════════════════════════════════════════

set -e
echo "Phase 2: Cleaning remaining references (excluding Trezor)..."

# ─── tests/libwallet_api_tests/main.cpp (150 match) ─────────────────
sed -i \
  -e 's/"monero:"/"mevacoin:"/g' \
  -e 's/unsigned_monero_tx/unsigned_mevacoin_tx/g' \
  -e 's/signed_monero_tx/signed_mevacoin_tx/g' \
  -e 's/multisig_monero_tx/multisig_mevacoin_tx/g' \
  -e 's/raw_monero_tx/raw_mevacoin_tx/g' \
  -e 's/monero_tx_proof/mevacoin_tx_proof/g' \
  -e 's/monero_spend_proof/mevacoin_spend_proof/g' \
  -e 's/monero_reserve_proof/mevacoin_reserve_proof/g' \
  -e 's/Monero wallet/Mevacoin wallet/g' \
  -e 's/Monero address/Mevacoin address/g' \
  -e 's/Monero Address/Mevacoin Address/g' \
  -e 's/monero_address/mevacoin_address/g' \
  -e 's/\.bitmonero/.mevacoin/g' \
  -e 's/bitmonero/mevacoin/g' \
  -e 's/monerod/mevacoind/g' \
  -e 's/monero-wallet-cli/mevacoin-wallet-cli/g' \
  -e 's/monero-wallet-rpc/mevacoin-wallet-rpc/g' \
  -e 's/getmonero\.org/mevacoin.org/g' \
  -e 's|github.com/monero-project/monero|github.com/pasqualelembo78/mevacoin|g' \
  -e 's/the Monero network/the Mevacoin network/g' \
  -e 's/MoneroAsciiDataV1/MevacoinAsciiDataV1/g' \
  -e 's/Monero unsigned tx set/Mevacoin unsigned tx set/g' \
  -e 's/Monero signed tx set/Mevacoin signed tx set/g' \
  -e 's/Monero multisig/Mevacoin multisig/g' \
  -e 's/Monero key image/Mevacoin key image/g' \
  -e 's/Monero output export/Mevacoin output export/g' \
  -e 's/ monero / mevacoin /g' \
  -e 's/ Monero / Mevacoin /g' \
  tests/libwallet_api_tests/main.cpp

# ─── tests/functional_tests/uri.py (43 match) ───────────────────────
sed -i \
  -e "s/'monero:/'mevacoin:/g" \
  -e 's/"monero:/"mevacoin:/g' \
  -e 's/monero:/mevacoin:/g' \
  tests/functional_tests/uri.py

# ─── src/wallet/message_store.cpp + .h (39 match) ───────────────────
# This has the struct field monero_address and related logic
sed -i \
  -e 's/monero_address/mevacoin_address/g' \
  -e 's/Monero address/Mevacoin address/g' \
  -e 's/Monero Address/Mevacoin Address/g' \
  -e 's/Monero network/Mevacoin network/g' \
  -e 's/the Monero/the Mevacoin/g' \
  -e 's/monerod/mevacoind/g' \
  -e 's|github.com/monero-project/monero|github.com/pasqualelembo78/mevacoin|g' \
  src/wallet/message_store.cpp src/wallet/message_store.h

# ─── Translations residuals (aggressive catch-all) ───────────────────
find translations/ -name "*.ts" -exec sed -i \
  -e 's/Monero/Mevacoin/g' \
  -e 's/monero-wallet/mevacoin-wallet/g' \
  -e 's/monero wallet/mevacoin wallet/g' \
  -e 's/monero address/mevacoin address/g' \
  -e 's/ monero/ mevacoin/g' \
  -e 's/\.bitmonero/.mevacoin/g' \
  {} +

# ─── tests/unit_tests/ remaining ─────────────────────────────────────
sed -i \
  -e 's/\.bitmonero/.mevacoin/g' \
  -e 's/bitmonero/mevacoin/g' \
  -e 's|monero.lmdb|mevacoin.lmdb|g' \
  -e 's/ monero / mevacoin /g' \
  -e 's/ Monero / Mevacoin /g' \
  -e 's/"monero"/"mevacoin"/g' \
  -e 's/monero_address/mevacoin_address/g' \
  -e 's|github.com/monero-project/monero|github.com/pasqualelembo78/mevacoin|g' \
  -e 's/getmonero\.org/mevacoin.org/g' \
  -e 's/"monero:"/"mevacoin:"/g' \
  tests/unit_tests/lmdb.cpp tests/unit_tests/expect.cpp tests/unit_tests/net.cpp \
  tests/unit_tests/uri.cpp tests/unit_tests/dns_resolver.cpp \
  tests/unit_tests/zmq_rpc.cpp tests/unit_tests/address_from_url.cpp \
  tests/unit_tests/epee_utils.cpp tests/unit_tests/rpc_version_str.cpp

# ─── src/lmdb/database.cpp ───────────────────────────────────────────
sed -i \
  -e 's/\.bitmonero/.mevacoin/g' \
  -e 's/bitmonero/mevacoin/g' \
  -e 's/ monero / mevacoin /g' \
  -e 's/ Monero / Mevacoin /g' \
  src/lmdb/database.cpp

# ─── src/rpc/zmq_server.cpp ──────────────────────────────────────────
sed -i \
  -e 's/ Monero / Mevacoin /g' \
  -e 's/ monero / mevacoin /g' \
  -e 's/monero_address/mevacoin_address/g' \
  src/rpc/zmq_server.cpp

# ─── contrib/guix/guix-build ─────────────────────────────────────────
sed -i \
  -e 's/monerod/mevacoind/g' \
  -e 's/monero-wallet/mevacoin-wallet/g' \
  -e 's/monero-blockchain/mevacoin-blockchain/g' \
  -e 's/monero-gen/mevacoin-gen/g' \
  contrib/guix/guix-build 2>/dev/null || true

# ─── Catch-all: remaining src/ patterns ──────────────────────────────
find src/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) \
  -not -path "*/device_trezor/*" \
  -exec sed -i \
    -e 's/ Monero / Mevacoin /g' \
    -e 's/ Monero\./ Mevacoin./g' \
    -e 's/ Monero,/ Mevacoin,/g' \
    -e 's/\"Monero /\"Mevacoin /g' \
    -e 's/monero_address/mevacoin_address/g' \
    -e 's/MONERO_DONATION_ADDR/MEVACOIN_DONATION_ADDR/g' \
  {} +

# ─── Catch-all: remaining tests/ patterns ────────────────────────────
find tests/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.py" \) \
  -not -path "*/trezor/*" \
  -exec sed -i \
    -e 's/ Monero / Mevacoin /g' \
    -e 's/ monero / mevacoin /g' \
    -e 's/monero_address/mevacoin_address/g' \
    -e 's/Monero wallet/Mevacoin wallet/g' \
    -e 's/Monero Wallet/Mevacoin Wallet/g' \
  {} +

echo ""
echo "Phase 2 complete!"
echo "Remaining references (excluding Trezor + Copyright):"
grep -rin 'monero' src/ tests/ contrib/ docs/ translations/ cmake/ utils/ .github/ 2>/dev/null | \
  grep -v Copyright | grep -v Portions | grep -v external/ | \
  grep -v 'device_trezor' | wc -l
