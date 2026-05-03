#!/bin/bash
# Fix last 6 remaining references
echo "Fixing last 6 references..."

for f in \
  src/crypto/crypto_ops_builder/ref10CommentedCombined/description \
  src/crypto/crypto_ops_builder/ref10CommentedCombined/designers \
  tests/difficulty/generate-data \
  contrib/guix/manifest.scm \
  contrib/brew/Brewfile; do
  [ -f "$f" ] && sed -i \
    -e 's/monero/mevacoin/g' \
    -e 's/Monero/Mevacoin/g' \
    -e 's/MONERO/MEVACOIN/g' \
    "$f" && echo "  Fixed: $f"
done

echo ""
echo "═══════════════════════════════════════════════════════"
echo -n "TOTAL remaining (excluding device_trezor): "
grep -rin 'monero' src/ tests/ contrib/ docs/ translations/ cmake/ utils/ .github/ 2>/dev/null | \
  grep -v external/ | grep -v device_trezor | wc -l
echo "═══════════════════════════════════════════════════════"
