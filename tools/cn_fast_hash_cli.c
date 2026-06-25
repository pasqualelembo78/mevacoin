#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "crypto/hash-ops.h"

static int from_hex_char(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int from_hex(const char* hex, unsigned char* out, size_t out_len) {
    size_t len = strlen(hex);
    if (len != out_len * 2) return -1;
    for (size_t i = 0; i < out_len; i++) {
        int hi = from_hex_char(hex[i*2]);
        int lo = from_hex_char(hex[i*2+1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (hi << 4) | lo;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: cn_fast_hash_cli <hex_data>\n");
        return 1;
    }
    size_t data_len = strlen(argv[1]) / 2;
    unsigned char* data = malloc(data_len);
    if (!data || from_hex(argv[1], data, data_len) < 0) {
        fprintf(stderr, "Invalid hex\n");
        free(data);
        return 1;
    }
    char hash[HASH_SIZE];
    cn_fast_hash(data, data_len, hash);
    for (int i = 0; i < HASH_SIZE; i++)
        printf("%02x", (unsigned char)hash[i]);
    printf("\n");
    free(data);
    return 0;
}
