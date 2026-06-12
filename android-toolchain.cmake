set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_HOME})
set(CMAKE_SYSTEM_VERSION 24)      # min API level
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
set(CMAKE_ANDROID_STL_TYPE c++_static)
set(CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION clang)

# Percorsi librerie
set(BOOST_ROOT /opt/boost/build/out/arm64-v8a)
set(OPENSSL_ROOT_DIR /opt/openssl/android/arm64-v8a)
set(LIBSODIUM_ROOT /root/libsodium/android/arm64-v8a)
