#pragma once

#include <stdint.h>
#include <stddef.h>

#define CHACHA8_KEY_SIZE 32
#define CHACHA8_IV_SIZE 8

#if defined(__cplusplus)
#include <memory.h>
#include <string>

#include "hash.h"

namespace Crypto {
  extern "C" {
#endif
    void chacha8(const void* data, size_t length, const uint8_t* key, const uint8_t* iv, char* cipher);
#if defined(__cplusplus)
  }

#pragma pack(push, 1)
  struct chacha8_key {
    uint8_t data[CHACHA8_KEY_SIZE];

    ~chacha8_key()
    {
      memset(data, 0, sizeof(data));
    }
  };

  struct chacha8_iv {
    uint8_t data[CHACHA8_IV_SIZE];
  };
#pragma pack(pop)

  static_assert(sizeof(chacha8_key) == CHACHA8_KEY_SIZE && sizeof(chacha8_iv) == CHACHA8_IV_SIZE, "Invalid structure size");

  inline void chacha8(const void* data, size_t length, const chacha8_key& key, const chacha8_iv& iv, char* cipher) {
    chacha8(data, length, reinterpret_cast<const uint8_t*>(&key), reinterpret_cast<const uint8_t*>(&iv), cipher);
  }

  inline void generate_chacha8_key(Crypto::cn_context &context, const std::string& password, chacha8_key& key) {
    static_assert(sizeof(chacha8_key) <= sizeof(Hash), "Size of hash must be at least that of chacha8_key");
    Hash pwd_hash;
    cn_slow_hash(context, password.data(), password.size(), pwd_hash);
    
    // Usa memcpy per copiare i dati da pwd_hash a key.data
    memcpy(key.data, &pwd_hash, CHACHA8_KEY_SIZE);
  }
}

#endif