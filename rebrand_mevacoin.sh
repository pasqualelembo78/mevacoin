#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════
# MEVACOIN REBRANDING SCRIPT — Complete Monero → Mevacoin migration
# Repository: https://github.com/pasqualelembo78/mevacoin.git
# ═══════════════════════════════════════════════════════════════════════
# USAGE:  cd /path/to/mevacoin && bash rebrand_mevacoin.sh
#
# NOTE:
# - Trezor/Ledger protocol files are LEFT UNCHANGED (compatibility)
# - Copyright headers "The Monero Project" are LEFT UNCHANGED (BSD-3-Clause)
# - MoneroMessageSignature hash key is LEFT UNCHANGED (consensus critical)
# - The build/ directory should be deleted and regenerated after running
# ═══════════════════════════════════════════════════════════════════════

set -e  # Exit on error

echo "═══════════════════════════════════════════════════════"
echo " MEVACOIN REBRANDING — Starting..."
echo "═══════════════════════════════════════════════════════"

# ─── CATEGORIA 1: Costanti di Versione ────────────────────────────────
echo "[1/9] Version constants (version.cpp.in + 28 dependent files)..."

find src/ tests/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.in" \) \
  -exec sed -i \
    -e 's/\bMONERO_VERSION_IS_RELEASE\b/MEVACOIN_VERSION_IS_RELEASE/g' \
    -e 's/\bMONERO_VERSION_FULL\b/MEVACOIN_VERSION_FULL/g' \
    -e 's/\bMONERO_VERSION_TAG\b/MEVACOIN_VERSION_TAG/g' \
    -e 's/\bMONERO_RELEASE_NAME\b/MEVACOIN_RELEASE_NAME/g' \
    -e 's/\bMONERO_VERSION\b/MEVACOIN_VERSION/g' \
    -e 's/\bDEF_MONERO_/DEF_MEVACOIN_/g' \
  {} +

# ─── CATEGORIA 2A: simplewallet.cpp — Safe Branding (UI text) ────────
echo "[2/9] Wallet CLI — safe branding (UI text)..."

sed -i \
  -e 's/Welcome to Monero/Welcome to Mevacoin/g' \
  -e 's/Monero, like Bitcoin/Mevacoin, like Bitcoin/g' \
  -e 's/your Monero transactions/your Mevacoin transactions/g' \
  -e 's/the Monero Project/the Mevacoin Project/g' \
  -e 's/The Monero Project/The Mevacoin Project/g' \
  -e 's/the Monero network/the Mevacoin network/g' \
  -e 's/Monero network/Mevacoin network/g' \
  -e 's/new to Monero/new to Mevacoin/g' \
  -e 's/your Monero wallet/your Mevacoin wallet/g' \
  -e 's/Monero wallet/Mevacoin wallet/g' \
  -e 's/Monero address/Mevacoin address/g' \
  -e 's/Monero Address/Mevacoin Address/g' \
  -e 's/supporting the Monero/supporting the Mevacoin/g' \
  -e 's/command line monero wallet/command line mevacoin wallet/g' \
  -e 's/Send XMR/Send MEVA/g' \
  -e 's/Donate XMR/Donate MEVA/g' \
  -e 's|github.com/monero-project/monero|github.com/pasqualelembo78/mevacoin|g' \
  -e 's/getmonero\.org/mevacoin.org/g' \
  -e 's/GetMonero\.org/mevacoin.org/g' \
  src/simplewallet/simplewallet.cpp

# ─── CATEGORIA 2B: simplewallet.cpp — Technical Elements ─────────────
echo "      Wallet CLI — technical elements..."

sed -i \
  -e 's/monero-wallet-cli/mevacoin-wallet-cli/g' \
  -e 's/"monero:"/"mevacoin:"/g' \
  -e 's/unsigned_monero_tx/unsigned_mevacoin_tx/g' \
  -e 's/signed_monero_tx_raw/signed_mevacoin_tx_raw/g' \
  -e 's/signed_monero_tx/signed_mevacoin_tx/g' \
  -e 's/raw_multisig_monero_tx/raw_multisig_mevacoin_tx/g' \
  -e 's/multisig_monero_tx/multisig_mevacoin_tx/g' \
  -e 's/raw_monero_tx/raw_mevacoin_tx/g' \
  -e 's/monero_tx_proof/mevacoin_tx_proof/g' \
  -e 's/monero_spend_proof/mevacoin_spend_proof/g' \
  -e 's/monero_reserve_proof/mevacoin_reserve_proof/g' \
  -e 's/unit == "monero"/unit == "mevacoin"/g' \
  -e 's/monero|millinero|micronero|nanonero|piconero/mevacoin|millimeva|micromeva|nanomeva|picomeva/g' \
  -e 's/monero, millinero, micronero, nanonero, piconero/mevacoin, millimeva, micromeva, nanomeva, picomeva/g' \
  -e 's/Set the default monero/Set the default mevacoin/g' \
  -e 's/receive new monero/receive new mevacoin/g' \
  -e 's/relayed to the monero/relayed to the mevacoin/g' \
  -e 's/MONERO_DONATION_ADDR/MEVACOIN_DONATION_ADDR/g' \
  -e 's/connect to a monero/connect to a mevacoin/g' \
  -e 's/reuse your Monero keys/reuse your Mevacoin keys/g' \
  -e 's/both Monero AND/both Mevacoin AND/g' \
  -e 's/Monero fork/Mevacoin fork/g' \
  -e 's/info about Monero/info about Mevacoin/g' \
  -e 's/locked your Monero wallet/locked your Mevacoin wallet/g' \
  -e 's/Monero protects/Mevacoin protects/g' \
  -e 's/Monero strives/Mevacoin strives/g' \
  -e 's/Monero included/Mevacoin included/g' \
  -e 's/Monero cannot/Mevacoin cannot/g' \
  -e 's/Flaws in Monero/Flaws in Mevacoin/g' \
  -e 's/privacy Monero provides/privacy Mevacoin provides/g' \
  -e 's/donate\.getmonero\.org/donate.mevacoin.org/g' \
  -e 's/monero_address/mevacoin_address/g' \
  src/simplewallet/simplewallet.cpp

# ─── CATEGORIA 3A: Daemon files — Banner e messaggi ──────────────────
echo "[3/9] Daemon banner & messages..."

find src/daemon/ src/wallet/wallet_args.cpp -type f \( -name "*.cpp" -o -name "*.h" \) \
  -exec sed -i \
    -e "s/\"Monero '/\"Mevacoin '/g" \
    -e 's/Monero Daemon/Mevacoin Daemon/g' \
    -e 's/monerod is running/mevacoind is running/g' \
    -e 's/monerod is NOT running/mevacoind is NOT running/g' \
    -e 's/monerod will not shrink/mevacoind will not shrink/g' \
    -e 's/exit monerod and run monero-blockchain-prune/exit mevacoind and run mevacoin-blockchain-prune/g' \
    -e 's/monero daily/mevacoin daily/g' \
    -e 's/monero monthly/mevacoin monthly/g' \
    -e 's/BitMonero Daemon/MevaCoin Daemon/g' \
    -e 's/\.bitmonero/.mevacoin/g' \
    -e 's/command line monero wallet/command line mevacoin wallet/g' \
    -e 's/connect to a monero/connect to a mevacoin/g' \
    -e 's/Monero Version/Mevacoin Version/g' \
    -e 's/MONERO_LOGS/MEVACOIN_LOGS/g' \
  {} +

# i18n language prefix
sed -i 's/i18n_set_language("translations", "monero"/i18n_set_language("translations", "mevacoin"/' \
  src/wallet/wallet_args.cpp

# ─── CATEGORIA 3B: wallet2.cpp — Magic strings + URI ─────────────────
echo "      wallet2.cpp — magic strings + URI..."

sed -i \
  -e 's/Monero unsigned tx set/Mevacoin unsigned tx set/g' \
  -e 's/Monero signed tx set/Mevacoin signed tx set/g' \
  -e 's/Monero multisig unsigned tx set/Mevacoin multisig unsigned tx set/g' \
  -e 's/Monero key image export/Mevacoin key image export/g' \
  -e 's/Monero multisig export/Mevacoin multisig export/g' \
  -e 's/Monero output export/Mevacoin output export/g' \
  -e 's/MoneroAsciiDataV1/MevacoinAsciiDataV1/g' \
  -e 's/"monero:"/"mevacoin:"/g' \
  -e 's/expected \\"monero:\\"/expected \\"mevacoin:\\"/g' \
  -e 's/incoming monero/incoming mevacoin/g' \
  -e 's/the Monero network/the Mevacoin network/g' \
  -e 's/original Monero address/original Mevacoin address/g' \
  -e 's/\.bitmonero/.mevacoin/g' \
  -e 's|github.com/monero-project/monero|github.com/pasqualelembo78/mevacoin|g' \
  src/wallet/wallet2.cpp

# ─── CATEGORIA 4: Logging macro (globale) ─────────────────────────────
echo "[4/9] MONERO_DEFAULT_LOG_CATEGORY → MEVACOIN_DEFAULT_LOG_CATEGORY..."

find src/ contrib/epee/ tests/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.inl" \) \
  -exec sed -i \
    -e 's/\bMONERO_DEFAULT_LOG_CATEGORY\b/MEVACOIN_DEFAULT_LOG_CATEGORY/g' \
  {} +

# ─── CATEGORIA 5: CMake Build System ──────────────────────────────────
echo "[5/9] CMake build system..."

find . -type f \( -name "CMakeLists.txt" -o -name "*.cmake" \) | \
  xargs sed -i \
    -e 's/project(monero)/project(mevacoin)/g' \
    -e 's/\bmonero_clang_tidy\b/mevacoin_clang_tidy/g' \
    -e 's/\bmonero_set_target_no_relink\b/mevacoin_set_target_no_relink/g' \
    -e 's/\bmonero_set_target_strip\b/mevacoin_set_target_strip/g' \
    -e 's/\bmonero_add_minimal_executable\b/mevacoin_add_minimal_executable/g' \
    -e 's/\bmonero_find_all_headers\b/mevacoin_find_all_headers/g' \
    -e 's/\bmonero_enable_coverage\b/mevacoin_enable_coverage/g' \
    -e 's/\bmonero_add_library_with_deps\b/mevacoin_add_library_with_deps/g' \
    -e 's/\bmonero_add_library\b/mevacoin_add_library/g' \
    -e 's/\bmonero_add_executable\b/mevacoin_add_executable/g' \
    -e 's/\bmonero_private_headers\b/mevacoin_private_headers/g' \
    -e 's/\bmonero_install_headers\b/mevacoin_install_headers/g' \
    -e 's/\bmonero_crypto_autodetect\b/mevacoin_crypto_autodetect/g' \
    -e 's/\bmonero_crypto_valid\b/mevacoin_crypto_valid/g' \
    -e 's/\bmonero_crypto_get_target\b/mevacoin_crypto_get_target/g' \
    -e 's/\bmonero_crypto_get_namespace\b/mevacoin_crypto_get_namespace/g' \
    -e 's/\bmonero_crypto_libraries\b/mevacoin_crypto_libraries/g' \
    -e 's/\bmonero_crypto_generate_header\b/mevacoin_crypto_generate_header/g' \
    -e 's/\bMONERO_PARALLEL_COMPILE_JOBS\b/MEVACOIN_PARALLEL_COMPILE_JOBS/g' \
    -e 's/\bMONERO_PARALLEL_LINK_JOBS\b/MEVACOIN_PARALLEL_LINK_JOBS/g' \
    -e 's/\bMONERO_GENERATED_HEADERS_DIR\b/MEVACOIN_GENERATED_HEADERS_DIR/g' \
    -e 's/\bMONERO_ADD_LIBRARY_NAME\b/MEVACOIN_ADD_LIBRARY_NAME/g' \
    -e 's/\bMONERO_ADD_LIBRARY_SOURCES\b/MEVACOIN_ADD_LIBRARY_SOURCES/g' \
    -e 's/\bMONERO_ADD_LIBRARY_DEPENDS\b/MEVACOIN_ADD_LIBRARY_DEPENDS/g' \
    -e 's/\bMONERO_ADD_LIBRARY\b/MEVACOIN_ADD_LIBRARY/g' \
    -e 's/\bMONERO_WALLET_CRYPTO_BENCH_NAMES\b/MEVACOIN_WALLET_CRYPTO_BENCH_NAMES/g' \
    -e 's/\bMONERO_WALLET_CRYPTO_BENCH\b/MEVACOIN_WALLET_CRYPTO_BENCH/g' \
    -e 's/\bMONERO_WALLET_CRYPTO_LIBRARY\b/MEVACOIN_WALLET_CRYPTO_LIBRARY/g' \
    -e 's/\bMONERO_CRYPTO_DIR\b/MEVACOIN_CRYPTO_DIR/g' \
    -e 's/\bMONERO_CRYPTO_LIBRARY\b/MEVACOIN_CRYPTO_LIBRARY/g' \
    -e 's/\bMONERO_CRYPTO_NAMESPACE\b/MEVACOIN_CRYPTO_NAMESPACE/g' \
    -e 's/\bMONERO_CLANG_BIN\b/MEVACOIN_CLANG_BIN/g' \
    -e 's/\bMONERO_CLANG_TIDY_CHECKS\b/MEVACOIN_CLANG_TIDY_CHECKS/g' \
    -e 's/\bMONERO_CLANG_TIDY_MIN_VERSION\b/MEVACOIN_CLANG_TIDY_MIN_VERSION/g' \
    -e 's/\bmonero_SOURCE_DIR\b/mevacoin_SOURCE_DIR/g' \
    -e 's|include/monero/crypto|include/mevacoin/crypto|g' \
    -e 's|monero_crypto_src|mevacoin_crypto_src|g' \
    -e 's|monero/crypto.h|mevacoin/crypto.h|g'

# ─── CATEGORIA 6: Scripts, Config, Systemd ────────────────────────────
echo "[6/9] Scripts, config, systemd..."

# Systemd
sed -i \
  -e 's/Monero Full Node/Mevacoin Full Node/g' \
  -e 's/User=monero/User=mevacoin/g' \
  -e 's/Group=monero/Group=mevacoin/g' \
  -e 's/StateDirectory=monero/StateDirectory=mevacoin/g' \
  -e 's/LogsDirectory=monero/LogsDirectory=mevacoin/g' \
  -e 's|/usr/bin/monerod|/usr/bin/mevacoind|g' \
  -e 's|/etc/monerod.conf|/etc/mevacoind.conf|g' \
  utils/systemd/monerod.service 2>/dev/null || true

# Config
sed -i \
  -e 's/Configuration for monerod/Configuration for mevacoind/g' \
  -e "s/See 'monerod/See 'mevacoind/g" \
  -e 's|/var/lib/monero|/var/lib/mevacoin|g' \
  -e 's|/var/log/monero/monero.log|/var/log/mevacoin/mevacoin.log|g' \
  utils/conf/monerod.conf 2>/dev/null || true

# Rename files
mv utils/systemd/monerod.service utils/systemd/mevacoind.service 2>/dev/null || true
mv utils/conf/monerod.conf utils/conf/mevacoind.conf 2>/dev/null || true

# Tor script
if [ -f contrib/tor/monero-over-tor.sh ]; then
  sed -i \
    -e 's/monerod/mevacoind/g' \
    -e 's/monero-over-tor/mevacoin-over-tor/g' \
    -e 's/\.bitmonero/.mevacoin/g' \
    -e 's/bitmonero\.log/mevacoin.log/g' \
    contrib/tor/monero-over-tor.sh
  mv contrib/tor/monero-over-tor.sh contrib/tor/mevacoin-over-tor.sh
fi

# cleanup.sh
sed -i \
  -e 's/monerod/mevacoind/g' \
  -e 's/monero-wallet-cli/mevacoin-wallet-cli/g' \
  -e 's/monero-wallet-rpc/mevacoin-wallet-rpc/g' \
  -e 's/monero-blockchain/mevacoin-blockchain/g' \
  -e 's/monero-gen-ssl-cert/mevacoin-gen-ssl-cert/g' \
  -e 's/monero-gen-trusted-multisig/mevacoin-gen-trusted-multisig/g' \
  cleanup.sh

# GitHub Actions
find .github/ -name "*.yml" -exec sed -i \
  -e 's/monerod/mevacoind/g' \
  -e 's/monero-wallet/mevacoin-wallet/g' \
  -e 's/monero-blockchain/mevacoin-blockchain/g' \
  {} + 2>/dev/null || true

# Guix build
sed -i \
  -e 's/monerod/mevacoind/g' \
  -e 's/monero-wallet/mevacoin-wallet/g' \
  -e 's/monero-blockchain/mevacoin-blockchain/g' \
  contrib/guix/libexec/build.sh 2>/dev/null || true

# Doxygen
sed -i -e 's/monerod/mevacoind/g' utils/doxygen-publish.sh 2>/dev/null || true

# ─── CATEGORIA 7: Traduzioni ──────────────────────────────────────────
echo "[7/9] Translations (rename files + content)..."

# Rename translation files
cd translations/
for f in monero_*.ts; do
  newname=$(echo "$f" | sed 's/^monero_/mevacoin_/')
  mv "$f" "$newname" 2>/dev/null || true
done
mv monero.ts mevacoin.ts 2>/dev/null || true
rm -f *.qm  # Remove compiled translations (regenerate later)
cd ..

# Content replacement in translations
find translations/ -name "*.ts" -exec sed -i \
  -e 's/Welcome to Monero/Welcome to Mevacoin/g' \
  -e 's/Monero, like Bitcoin/Mevacoin, like Bitcoin/g' \
  -e 's/your Monero transactions/your Mevacoin transactions/g' \
  -e 's/the Monero Project/the Mevacoin Project/g' \
  -e 's/The Monero Project/The Mevacoin Project/g' \
  -e 's/the Monero network/the Mevacoin network/g' \
  -e 's/Monero network/Mevacoin network/g' \
  -e 's/Monero wallet/Mevacoin wallet/g' \
  -e 's/Monero address/Mevacoin address/g' \
  -e 's/Monero Address/Mevacoin Address/g' \
  -e 's/new to Monero/new to Mevacoin/g' \
  -e 's/supporting the Monero/supporting the Mevacoin/g' \
  -e 's/Monero protects/Mevacoin protects/g' \
  -e 's/Monero included/Mevacoin included/g' \
  -e 's/Monero cannot/Mevacoin cannot/g' \
  -e 's/Flaws in Monero/Flaws in Mevacoin/g' \
  -e 's/privacy Monero provides/privacy Mevacoin provides/g' \
  -e 's/your Monero keys/your Mevacoin keys/g' \
  -e 's/locked your Monero/locked your Mevacoin/g' \
  -e 's/Monero fork/Mevacoin fork/g' \
  -e 's/info about Monero/info about Mevacoin/g' \
  -e 's/both Monero AND/both Mevacoin AND/g' \
  -e 's/command line monero wallet/command line mevacoin wallet/g' \
  -e 's/connect to a monero/connect to a mevacoin/g' \
  -e 's/relayed to the monero/relayed to the mevacoin/g' \
  -e 's/receive new monero/receive new mevacoin/g' \
  -e 's/incoming monero/incoming mevacoin/g' \
  -e 's/Send XMR/Send MEVA/g' \
  -e 's/Donate XMR/Donate MEVA/g' \
  -e 's/monero-wallet-cli/mevacoin-wallet-cli/g' \
  -e 's/monero-wallet-rpc/mevacoin-wallet-rpc/g' \
  -e 's/getmonero\.org/mevacoin.org/g' \
  -e 's/GetMonero\.org/mevacoin.org/g' \
  -e 's/donate\.getmonero\.org/donate.mevacoin.org/g' \
  -e 's/unsigned_monero_tx/unsigned_mevacoin_tx/g' \
  -e 's/signed_monero_tx/signed_mevacoin_tx/g' \
  -e 's/multisig_monero_tx/multisig_mevacoin_tx/g' \
  -e 's/raw_monero_tx/raw_mevacoin_tx/g' \
  -e 's/monero_tx_proof/mevacoin_tx_proof/g' \
  -e 's/monero_spend_proof/mevacoin_spend_proof/g' \
  -e 's/monero_reserve_proof/mevacoin_reserve_proof/g' \
  -e 's/monerod/mevacoind/g' \
  -e 's/monero, millinero, micronero, nanonero, piconero/mevacoin, millimeva, micromeva, nanomeva, picomeva/g' \
  -e 's/ Monero / Mevacoin /g' \
  -e 's/ Monero\./ Mevacoin./g' \
  -e 's/ Monero,/ Mevacoin,/g' \
  -e 's/"Monero /"Mevacoin /g' \
  -e 's/\.bitmonero/.mevacoin/g' \
  {} +

# ─── CATEGORIA 8: Documentazione ──────────────────────────────────────
echo "[8/9] Documentation (.md files)..."

find . -name "*.md" -not -path "./external/*" -not -path "./build/*" -exec sed -i \
  -e 's/Monero Project/Mevacoin Project/g' \
  -e 's/Monero software/Mevacoin software/g' \
  -e 's/Monero network/Mevacoin network/g' \
  -e 's/Monero wallet/Mevacoin wallet/g' \
  -e 's/Monero daemon/Mevacoin daemon/g' \
  -e 's/Monero node/Mevacoin node/g' \
  -e 's/Monero blockchain/Mevacoin blockchain/g' \
  -e 's/Monero address/Mevacoin address/g' \
  -e 's/Monero GUI/Mevacoin GUI/g' \
  -e 's/Monero CLI/Mevacoin CLI/g' \
  -e 's/Monero RPC/Mevacoin RPC/g' \
  -e 's/monerod/mevacoind/g' \
  -e 's/Monerod/Mevacoind/g' \
  -e 's/monero-wallet-cli/mevacoin-wallet-cli/g' \
  -e 's/monero-wallet-rpc/mevacoin-wallet-rpc/g' \
  -e 's/monero-blockchain-export/mevacoin-blockchain-export/g' \
  -e 's/monero-blockchain-import/mevacoin-blockchain-import/g' \
  -e 's/monero-blockchain-prune/mevacoin-blockchain-prune/g' \
  -e 's/monero-blockchain-stats/mevacoin-blockchain-stats/g' \
  -e 's/monero-blockchain-mark-spent-outputs/mevacoin-blockchain-mark-spent-outputs/g' \
  -e 's/monero-blockchain-ancestry/mevacoin-blockchain-ancestry/g' \
  -e 's/monero-blockchain-depth/mevacoin-blockchain-depth/g' \
  -e 's/monero-blockchain-usage/mevacoin-blockchain-usage/g' \
  -e 's|getmonero\.org|mevacoin.org|g' \
  -e 's|GetMonero\.org|mevacoin.org|g' \
  -e 's|github\.com/monero-project/monero|github.com/pasqualelembo78/mevacoin|g' \
  -e 's|github\.com/monero-project|github.com/pasqualelembo78|g' \
  -e 's/\.bitmonero/.mevacoin/g' \
  -e 's/bitmonero/mevacoin/g' \
  -e 's/ Monero / Mevacoin /g' \
  -e 's/ Monero\./ Mevacoin./g' \
  -e 's/ Monero,/ Mevacoin,/g' \
  -e 's/Contributing to Monero/Contributing to Mevacoin/g' \
  -e 's/#monero-dev/#mevacoin-dev/g' \
  -e 's/#monero-translations/#mevacoin-translations/g' \
  -e 's/Monero Core GUI/Mevacoin Core GUI/g' \
  -e 's|monero-gui|mevacoin-gui|g' \
  -e 's|translations/monero|translations/mevacoin|g' \
  -e 's/ZMQ in Monero/ZMQ in Mevacoin/g' \
  -e 's/Monero-announce/Mevacoin-announce/g' \
  -e 's/MyMonero/MyMevacoin/g' \
  -e 's/Monero can be compiled/Mevacoin can be compiled/g' \
  -e 's/compile Monero/compile Mevacoin/g' \
  -e 's/monero\.cbp/mevacoin.cbp/g' \
  -e 's/^monero:/mevacoin:/g' \
  -e 's/monero_wallet:/mevacoin_wallet:/g' \
  -e 's|/var/lib/tor/data/monero|/var/lib/tor/data/mevacoin|g' \
  -e "s/Monero's/Mevacoin's/g" \
  -e "s/integrated into Monero/integrated into Mevacoin/g" \
  -e "s/Networks with Monero/Networks with Mevacoin/g" \
  -e 's|projects/monero|projects/mevacoin|g' \
  -e 's|build_fuzzers monero|build_fuzzers mevacoin|g' \
  -e 's|build/out/monero|build/out/mevacoin|g' \
  -e 's|run_fuzzer monero|run_fuzzer mevacoin|g' \
  -e 's|project=monero|project=mevacoin|g' \
  -e 's|/path/to/monero|/path/to/mevacoin|g' \
  -e 's|Building Monero|Building Mevacoin|g' \
  {} +

# ─── CATEGORIA 9: Catch-all (fish, tests, remaining source) ──────────
echo "[9/9] Fish completions, test suite, remaining source..."

# Fish completions
find utils/fish/ -type f -exec sed -i \
  -e 's/monerod/mevacoind/g' \
  -e 's/monero-wallet-cli/mevacoin-wallet-cli/g' \
  -e 's/monero-wallet-rpc/mevacoin-wallet-rpc/g' \
  -e 's/monero-blockchain/mevacoin-blockchain/g' \
  -e 's/monero-gen-ssl-cert/mevacoin-gen-ssl-cert/g' \
  -e 's/monero-gen-trusted-multisig/mevacoin-gen-trusted-multisig/g' \
  -e 's/\.bitmonero/.mevacoin/g' \
  -e 's/Monero daemon/Mevacoin daemon/g' \
  -e 's/Monero wallet/Mevacoin wallet/g' \
  {} + 2>/dev/null || true

# Rename fish files
for f in utils/fish/*monero*; do
  mv "$f" "$(echo $f | sed 's/monero/mevacoin/g')" 2>/dev/null || true
done

# Test suite (exclude trezor)
find tests/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.py" -o -name "*.txt" \) \
  -not -path "*/trezor/*" \
  -exec sed -i \
    -e 's/"monero:"/"mevacoin:"/g' \
    -e 's/unsigned_monero_tx/unsigned_mevacoin_tx/g' \
    -e 's/signed_monero_tx/signed_mevacoin_tx/g' \
    -e 's/multisig_monero_tx/multisig_mevacoin_tx/g' \
    -e 's/raw_monero_tx/raw_mevacoin_tx/g' \
    -e 's/monero_tx_proof/mevacoin_tx_proof/g' \
    -e 's/monero_spend_proof/mevacoin_spend_proof/g' \
    -e 's/monero_reserve_proof/mevacoin_reserve_proof/g' \
    -e 's/monerod/mevacoind/g' \
    -e 's/monero-wallet-cli/mevacoin-wallet-cli/g' \
    -e 's/monero-wallet-rpc/mevacoin-wallet-rpc/g' \
    -e 's/monero-blockchain/mevacoin-blockchain/g' \
    -e 's/\.bitmonero/.mevacoin/g' \
    -e 's/bitmonero/mevacoin/g' \
    -e 's|getmonero\.org|mevacoin.org|g' \
    -e 's|github\.com/monero-project/monero|github.com/pasqualelembo78/mevacoin|g' \
    -e 's/the Monero network/the Mevacoin network/g' \
    -e 's/Monero wallet/Mevacoin wallet/g' \
    -e 's/monero_address/mevacoin_address/g' \
    -e 's/Monero Address/Mevacoin Address/g' \
    -e 's/MoneroAsciiDataV1/MevacoinAsciiDataV1/g' \
    -e 's/Monero unsigned tx set/Mevacoin unsigned tx set/g' \
    -e 's/Monero signed tx set/Mevacoin signed tx set/g' \
    -e 's/Monero multisig unsigned tx set/Mevacoin multisig unsigned tx set/g' \
    -e 's/Monero key image export/Mevacoin key image export/g' \
    -e 's/Monero multisig export/Mevacoin multisig export/g' \
    -e 's/Monero output export/Mevacoin output export/g' \
  {} +

# Remaining source code (exclude trezor)
find src/ -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) \
  -not -path "*/device_trezor/*" \
  -exec sed -i \
    -e 's/monerod/mevacoind/g' \
    -e 's/monero-wallet-cli/mevacoin-wallet-cli/g' \
    -e 's/monero-wallet-rpc/mevacoin-wallet-rpc/g' \
    -e 's/monero-blockchain/mevacoin-blockchain/g' \
    -e 's/monero-gen-ssl/mevacoin-gen-ssl/g' \
    -e 's/\.bitmonero/.mevacoin/g' \
    -e 's/bitmonero/mevacoin/g' \
    -e 's|getmonero\.org|mevacoin.org|g' \
    -e 's|github\.com/monero-project/monero|github.com/pasqualelembo78/mevacoin|g' \
  {} +

# ─── CLEANUP ──────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════"
echo " Deleting build/ directory (needs regeneration)..."
echo "═══════════════════════════════════════════════════════"
rm -rf build/

echo ""
echo "═══════════════════════════════════════════════════════"
echo " REBRANDING COMPLETE!"
echo "═══════════════════════════════════════════════════════"
echo ""
echo "Remaining references (expected: Trezor protocol + Copyright headers):"
grep -rin 'monero' src/ tests/ contrib/ docs/ translations/ cmake/ utils/ .github/ 2>/dev/null | \
  grep -v Copyright | grep -v Portions | grep -v external/ | \
  grep -v 'device_trezor/trezor/messages' | \
  grep -v 'device_trezor/trezor/protob' | wc -l

echo ""
echo "Next steps:"
echo "  1. Run: mkdir build && cd build && cmake .. && make -j$(nproc)"
echo "  2. Fix any compilation errors from missed references"
echo "  3. Test: ./build/bin/mevacoind --version"
echo ""
