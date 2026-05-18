// ============================================================
//  hash_util.cpp — SHA-256 via mbedTLS (tersedia di ESP32 SDK)
// ============================================================

#include "hash_util.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------
void HashUtil::sha256(const uint8_t* data, size_t len, uint8_t outBytes[SHA256_BYTES]) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (bukan SHA-224)
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, outBytes);
    mbedtls_sha256_free(&ctx);
}

// ------------------------------------------------------------
void HashUtil::sha256Hex(const char* input, char outHex[SHA256_HEX_LEN]) {
    uint8_t digest[SHA256_BYTES];
    sha256(reinterpret_cast<const uint8_t*>(input), strlen(input), digest);

    // Konversi setiap byte ke dua karakter hex
    for (int i = 0; i < SHA256_BYTES; i++) {
        sprintf(outHex + (i * 2), "%02x", digest[i]);
    }
    outHex[SHA256_HEX_LEN - 1] = '\0';
}

// ------------------------------------------------------------
void HashUtil::buildTxString(uint8_t  slaveId,
                              uint8_t  funcCode,
                              uint16_t regAddr,
                              uint32_t value,
                              uint32_t timestampMs,
                              char*    outBuf,
                              size_t   outBufLen) {
    // Format: "slaveId|funcCode|regAddr|value|timestampMs"
    // Contoh: "3|3|0x0000|1024|1715000000"
    snprintf(outBuf, outBufLen,
             "%u|%u|0x%04X|%lu|%lu",
             slaveId, funcCode, regAddr,
             (unsigned long)value,
             (unsigned long)timestampMs);
}
