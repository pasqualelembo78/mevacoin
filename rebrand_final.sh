#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════
# MEVACOIN REBRANDING — Phase FINAL: Total aggressive replacement
# Replaces ALL monero/Monero/MONERO except in device_trezor/ and
# MoneroMessageSignature hash key
# ═══════════════════════════════════════════════════════════════════════

echo "Phase FINAL: Total aggressive replacement..."
echo "(excluding device_trezor/ and MoneroMessageSignature)"

# All source, headers, scripts, docs, translations, build files
find src/ tests/ contrib/ docs/ translations/ cmake/ utils/ .github/ \
  -type f \
  -not -path "*/device_trezor/*" \
  -not -path "*/external/*" \
  -not -path "*/build/*" \
  \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.c" \
     -o -name "*.inl" -o -name "*.in" -o -name "*.py" -o -name "*.sh" \
     -o -name "*.md" -o -name "*.ts" -o -name "*.txt" -o -name "*.cmake" \
     -o -name "*.yml" -o -name "*.yaml" -o -name "*.conf" -o -name "*.service" \
     -o -name "*.proto" -o -name "CMakeLists.txt" -o -name "Makefile" \
     -o -name "Dockerfile" -o -name "Doxyfile" -o -name "*.mk" \
     -o -name "*.cc" \) \
  -exec sed -i \
    -e '/MoneroMessageSignature/!s/monero/mevacoin/g' \
    -e '/MoneroMessageSignature/!s/Monero/Mevacoin/g' \
    -e '/MoneroMessageSignature/!s/MONERO/MEVACOIN/g' \
  {} +

# Root-level files
for f in Makefile Dockerfile Doxyfile cleanup.sh; do
  [ -f "$f" ] && sed -i \
    -e 's/monero/mevacoin/g' \
    -e 's/Monero/Mevacoin/g' \
    -e 's/MONERO/MEVACOIN/g' \
    "$f"
done

# Fish completions (any extension)
find utils/fish/ -type f -exec sed -i \
  -e 's/monero/mevacoin/g' \
  -e 's/Monero/Mevacoin/g' \
  {} + 2>/dev/null || true

# guix-build (no extension)
find contrib/guix/ -type f -name "guix-*" -exec sed -i \
  -e 's/monero/mevacoin/g' \
  -e 's/Monero/Mevacoin/g' \
  -e 's/MONERO/MEVACOIN/g' \
  {} + 2>/dev/null || true

echo ""
echo "═══════════════════════════════════════════════════════"
echo "RISULTATO FINALE (escludendo solo device_trezor)"
echo "═══════════════════════════════════════════════════════"
echo -n "Riferimenti 'monero' rimasti: "
grep -rin 'monero' src/ tests/ contrib/ docs/ translations/ cmake/ utils/ .github/ 2>/dev/null | \
  grep -v external/ | grep -v device_trezor | wc -l

echo ""
echo "Top file con residui:"
grep -rin 'monero' src/ tests/ contrib/ docs/ translations/ cmake/ utils/ .github/ 2>/dev/null | \
  grep -v external/ | grep -v device_trezor | \
  cut -d: -f1 | sort | uniq -c | sort -rn | head -15
