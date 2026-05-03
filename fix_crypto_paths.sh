#!/bin/bash
echo "Fixing crypto include paths..."

# 1. Rename physical directory
if [ -d "external/supercop/include/monero" ]; then
  mv external/supercop/include/monero external/supercop/include/mevacoin
  echo "  Renamed: external/supercop/include/monero → mevacoin"
fi

# 2. Update template crypto.h.in
find external/supercop/ -name "*.in" -exec sed -i \
  -e 's/monero/mevacoin/g' \
  -e 's/Monero/Mevacoin/g' \
  -e 's/MONERO/MEVACOIN/g' \
  {} + 2>/dev/null

# 3. Update all source files in external/supercop/
find external/supercop/ -type f \( -name "*.h" -o -name "*.c" -o -name "*.cpp" \) \
  -exec sed -i \
    -e 's|monero/crypto|mevacoin/crypto|g' \
    -e 's/MONERO/MEVACOIN/g' \
    -e 's/monero/mevacoin/g' \
    -e 's/Monero/Mevacoin/g' \
  {} + 2>/dev/null

# 4. Update src/ include paths referencing monero/crypto
find src/ -type f \( -name "*.h" -o -name "*.cpp" -o -name "*.c" -o -name "*.hpp" \) \
  -not -path "*/device_trezor/*" \
  -exec grep -l 'monero/crypto' {} + 2>/dev/null | while read f; do
    sed -i 's|monero/crypto|mevacoin/crypto|g' "$f"
    echo "  Fixed include path in: $f"
  done

# 5. Also fix any remaining monero references in external/supercop CMake
find external/supercop/ -type f \( -name "CMakeLists.txt" -o -name "*.cmake" \) \
  -exec sed -i \
    -e 's|include/monero|include/mevacoin|g' \
    -e 's/monero/mevacoin/g' \
    -e 's/Monero/Mevacoin/g' \
    -e 's/MONERO/MEVACOIN/g' \
  {} + 2>/dev/null

echo ""
echo "Cleaning build and recompiling..."
rm -rf build/
make -j$(nproc) 2>&1 | tail -30
