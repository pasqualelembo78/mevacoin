#pragma once

extern const char* const MEVACOIN_VERSION_TAG;
extern const char* const MEVACOIN_VERSION;
extern const char* const MEVACOIN_RELEASE_NAME;
extern const char* const MEVACOIN_VERSION_FULL;
extern const bool MEVACOIN_VERSION_IS_RELEASE;

// Compatibilità con il codice che usa ancora MONERO_*
#define MONERO_VERSION_TAG      MEVACOIN_VERSION_TAG
#define MONERO_VERSION          MEVACOIN_VERSION
#define MONERO_RELEASE_NAME     MEVACOIN_RELEASE_NAME
#define MONERO_VERSION_FULL     MEVACOIN_VERSION_FULL
#define MONERO_VERSION_IS_RELEASE MEVACOIN_VERSION_IS_RELEASE
