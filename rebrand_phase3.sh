#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════
# MEVACOIN REBRANDING — Phase 3: Aggressive cleanup
# Run AFTER Phase 1 + Phase 2
# ═══════════════════════════════════════════════════════════════════════

echo "Phase 3: Aggressive cleanup (excluding Trezor + Copyright)..."

# tests/libwallet_api_tests/main.cpp
sed -i \
  -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  -e 's/MONERO/MEVACOIN/g' \
  -e 's/XMR/MEVA/g' \
  tests/libwallet_api_tests/main.cpp

# tests/unit_tests/ — all files with residuals
for f in tests/unit_tests/lmdb.cpp tests/unit_tests/expect.cpp \
         tests/unit_tests/net.cpp tests/unit_tests/zmq_rpc.cpp \
         tests/unit_tests/uri.cpp tests/unit_tests/dns_resolver.cpp \
         tests/unit_tests/address_from_url.cpp tests/unit_tests/epee_utils.cpp \
         tests/unit_tests/rpc_version_str.cpp tests/unit_tests/ringct.cpp; do
  sed -i \
    -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
    -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
    -e 's/MONERO/MEVACOIN/g' \
    "$f" 2>/dev/null || true
done

# tests/functional_tests/
find tests/functional_tests/ -type f \( -name "*.py" -o -name "*.cpp" \) -exec sed -i \
  -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  {} + 2>/dev/null || true

# tests/libwallet_api_tests/scripts/
find tests/libwallet_api_tests/ -type f -exec sed -i \
  -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  {} + 2>/dev/null || true

# src/lmdb/
find src/lmdb/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i \
  -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  {} + 2>/dev/null || true

# src/rpc/ remaining
find src/rpc/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i \
  -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  {} + 2>/dev/null || true

# contrib/guix/
find contrib/guix/ -type f -exec sed -i \
  -e 's/monero-wallet/mevacoin-wallet/g' \
  -e 's/monero-blockchain/mevacoin-blockchain/g' \
  -e 's/monerod/mevacoind/g' \
  -e 's/monero-gen/mevacoin-gen/g' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  {} + 2>/dev/null || true

# src/cryptonote_protocol/
sed -i \
  -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  src/cryptonote_protocol/cryptonote_protocol_handler.inl 2>/dev/null || true

# src/debug_utilities/
find src/debug_utilities/ -type f -exec sed -i \
  -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  {} + 2>/dev/null || true

# contrib/epee/
find contrib/epee/src/ -type f -exec sed -i \
  -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  {} + 2>/dev/null || true

# src/p2p/
find src/p2p/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i \
  -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  {} + 2>/dev/null || true

# src/net/
find src/net/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i \
  -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
  -e '/Copyright/!{/Portions/!s/monero/mevacoin/g}' \
  {} + 2>/dev/null || true

# src/crypto/ (only comments, leave hash keys)
find src/crypto/ -type f \( -name "*.py" -o -name "*.c" -o -name "*.h" \) \
  -not -path "*/wallet/*" \
  -exec sed -i \
    -e '/Copyright/!{/Portions/!{/MoneroMessageSignature/!s/Monero/Mevacoin/g}}' \
    -e '/Copyright/!{/Portions/!{/MoneroMessageSignature/!s/monero/mevacoin/g}}' \
  {} + 2>/dev/null || true

# Remaining source catch-all (CAREFUL: skip device_trezor)
find src/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.inl" \) \
  -not -path "*/device_trezor/*" \
  -exec sed -i \
    -e '/Copyright/!{/Portions/!{/MoneroMessageSignature/!s/Monero/Mevacoin/g}}' \
    -e '/Copyright/!{/Portions/!{/MoneroMessageSignature/!s/ monero / mevacoin /g}}' \
  {} + 2>/dev/null || true

# tests/ remaining catch-all (skip trezor tests)
find tests/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.py" \) \
  -not -path "*/trezor/*" \
  -exec sed -i \
    -e '/Copyright/!{/Portions/!s/Monero/Mevacoin/g}' \
    -e '/Copyright/!{/Portions/!s/ monero / mevacoin /g}' \
  {} + 2>/dev/null || true

echo ""
echo "Phase 3 complete!"
echo "Remaining references (excluding Trezor + Copyright):"
grep -rin 'monero' src/ tests/ contrib/ docs/ translations/ cmake/ utils/ .github/ 2>/dev/null | \
  grep -v Copyright | grep -v Portions | grep -v external/ | \
  grep -v 'device_trezor' | wc -l
echo ""
echo "Breakdown:"
grep -rin 'monero' src/ tests/ contrib/ docs/ translations/ cmake/ utils/ .github/ 2>/dev/null | \
  grep -v Copyright | grep -v Portions | grep -v external/ | \
  grep -v 'device_trezor' | cut -d: -f1 | sort | uniq -c | sort -rn | head -15
