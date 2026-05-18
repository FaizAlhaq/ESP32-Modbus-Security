#pragma once

// ============================================================
//  hash_util.h — Wrapper SHA-256 menggunakan mbedTLS (built-in ESP32)
// ============================================================

#include <Arduino.h>

// Panjang output SHA-256 dalam byte
#define SHA256_BYTES  32
// Panjang representasi hex SHA-256 (64 karakter + null terminator)
#define SHA256_HEX_LEN 65

class HashUtil {
public:
    // Hitung SHA-256 dari buffer data mentah → output 32 byte
    static void sha256(const uint8_t* data, size_t len, uint8_t outBytes[SHA256_BYTES]);

    // Hitung SHA-256 dari string → output string hex 64 karakter
    static void sha256Hex(const char* input, char outHex[SHA256_HEX_LEN]);

    // Buat string ringkasan transaksi untuk di-hash:
    // Format: "slaveId|funcCode|regAddr|value|timestampMs"
    static void buildTxString(uint8_t  slaveId,
                               uint8_t  funcCode,
                               uint16_t regAddr,
                               uint32_t value,
                               uint32_t timestampMs,
                               char*    outBuf,
                               size_t   outBufLen);
};
