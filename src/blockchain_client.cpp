// ============================================================
//  blockchain_client.cpp — HTTP JSON-RPC ke Ganache
//
//  Semua call menggunakan eth_sendRawTransaction / eth_call
//  melalui HTTPClient bawaan ESP32.
//
//  CATATAN: Implementasi ini menggunakan pendekatan "mock-first":
//  verifyDevice() membaca dari mapping on-chain via eth_call,
//  logTransaction() dan logAnomaly() mengirim event via eth_call
//  ke contract sederhana. Ganti ABI selector sesuai contract Anda.
// ============================================================

#include "blockchain_client.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>

// Keccak-4 byte selector fungsi contract (hitung dari ABI)
// Ganti dengan selector dari contract yang Anda deploy
#define SEL_VERIFY_DEVICE     "0xeca8e63d"  // verifyDevice(uint8)
#define SEL_VERIFY_DEVICE_UID "0xd14cf946"  // verifyDevice(uint8,uint256)
#define SEL_LOG_TRANSACTION   "0xd8628357"  // logTransaction(string,string)
#define SEL_LOG_ANOMALY       "0x98bf92e5"  // logAnomaly(uint8,uint8,string)

// Ukuran buffer payload JSON — cukup untuk satu RPC call
#define RPC_BUF_SIZE  768

// logTransaction()/logAnomaly() mengandung argumen `string` yang perlu
// di-ABI-encode penuh (offset + length + data padded ke 32 byte) —
// jauh lebih besar dari RPC_BUF_SIZE, jadi pakai buffer khusus.
#define LOG_DATA_HEX_CAP    700
#define LOG_PARAMS_BUF_SIZE 900

// ------------------------------------------------------------
// Encode satu argumen `string` sesuai ABI Ethereum: 32-byte length word
// (hex) + data (hex) + zero-padding hex hingga kelipatan 32 byte.
// Dipakai HANYA oleh logTransaction()/logAnomaly() — verifyDevice()
// tidak menyentuh fungsi ini karena parameternya semua fixed-size.
// Return: jumlah karakter hex yang ditulis ke `out`.
static size_t abiEncodeDynamicString(const char* str, char* out, size_t outCap) {
    size_t len    = strlen(str);
    size_t padded = ((len + 31) / 32) * 32;
    size_t pos    = 0;

    pos += snprintf(out + pos, outCap - pos, "%064lx", (unsigned long)len);
    for (size_t i = 0; i < len; i++) {
        pos += snprintf(out + pos, outCap - pos, "%02x", (unsigned int)(uint8_t)str[i]);
    }
    for (size_t i = len; i < padded; i++) {
        pos += snprintf(out + pos, outCap - pos, "00");
    }
    return pos;
}

// ------------------------------------------------------------
void BlockchainClient::begin() {
    _reachable = true; // Optimis: asumsikan aktif, deteksi dari call pertama
    Serial.println("[BC] BlockchainClient siap, RPC: " BLOCKCHAIN_RPC_URL);
}

// ------------------------------------------------------------
bool BlockchainClient::isReachable() const {
    return _reachable;
}

// ------------------------------------------------------------
void BlockchainClient::updateReachability(bool success) {
    if (success && !_reachable) {
        _reachable = true;
        Serial.println("[BC] tersambung kembali");
    } else if (!success && _reachable) {
        _reachable = false;
        Serial.println("[BC] NODE BLOCKCHAIN TIDAK TERJANGKAU");
    }
}

// ------------------------------------------------------------
bool BlockchainClient::verifyDevice(uint8_t slaveId) {
    // Buat parameter: address contract + data (selector + slaveId)
    char params[256];
    snprintf(params, sizeof(params),
             "[{\"to\":\"%s\",\"data\":\"%s%064x\"},\"latest\"]",
             CONTRACT_ADDRESS, SEL_VERIFY_DEVICE, slaveId);

    char payload[RPC_BUF_SIZE];
    buildRpcPayload("eth_call", params, payload, sizeof(payload));

    // Kirim dan baca respons
    HTTPClient http;
    http.begin(BLOCKCHAIN_RPC_URL);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(payload);
    bool verified = false;

    if (code == 200) {
        String body = http.getString();
        // Respons eth_call: {"jsonrpc":"2.0","result":"0x0000...0001"}
        // Nilai non-zero = terdaftar
        if (body.indexOf("\"result\":\"0x") >= 0) {
            // Cari nilai result; non-zero = true
            int idx = body.indexOf("\"result\":\"0x") + 12;
            String hexVal = body.substring(idx, body.indexOf("\"", idx));
            // Hapus leading zero, cek apakah ada digit non-nol
            hexVal.trim();
            for (char c : hexVal) {
                if (c != '0') { verified = true; break; }
            }
        }
        Serial.printf("[BC] verifyDevice(%u) → %s\n", slaveId, verified ? "OK" : "TOLAK");
    } else {
        Serial.printf("[BC] verifyDevice HTTP error: %d\n", code);
    }

    updateReachability(code > 0);
    http.end();
    return verified;
}

// ------------------------------------------------------------
// Verifikasi identitas: cek whitelist DAN kecocokan UID on-chain.
// Dipanggil hanya pada first-contact dan reconnect (bukan tiap poll).
// ------------------------------------------------------------
bool BlockchainClient::verifyDevice(uint8_t slaveId, const uint8_t uid32[32]) {
    // Encode uid32 (32 byte) sebagai 64 char hex untuk ABI uint256
    char uidHex[65];
    for (int i = 0; i < 32; i++) snprintf(uidHex + i * 2, 3, "%02x", uid32[i]);
    uidHex[64] = '\0';

    char params[256];
    snprintf(params, sizeof(params),
             "[{\"to\":\"%s\",\"data\":\"%s%064x%s\"},\"latest\"]",
             CONTRACT_ADDRESS, SEL_VERIFY_DEVICE_UID,
             (unsigned int)slaveId, uidHex);

    char payload[RPC_BUF_SIZE];
    buildRpcPayload("eth_call", params, payload, sizeof(payload));

    HTTPClient http;
    http.begin(BLOCKCHAIN_RPC_URL);
    http.addHeader("Content-Type", "application/json");

    int  code     = http.POST(payload);
    bool verified = false;

    if (code == 200) {
        String body = http.getString();
        if (body.indexOf("\"result\":\"0x") >= 0) {
            int    idx    = body.indexOf("\"result\":\"0x") + 12;
            String hexVal = body.substring(idx, body.indexOf("\"", idx));
            hexVal.trim();
            for (char c : hexVal) { if (c != '0') { verified = true; break; } }
        }
        Serial.printf("[BC] verifyDevice(%u, UID) → %s\n",
                      slaveId, verified ? "OK" : "TOLAK");
    } else {
        Serial.printf("[BC] verifyDevice(UID) HTTP error: %d\n", code);
    }

    updateReachability(code > 0);
    http.end();
    return verified;
}

// ------------------------------------------------------------
// ABI encoding logTransaction(string txData, string txHash):
//   selector(4B) + offset1(32B) + offset2(32B)
//   + [length(32B) + data padded ke 32B] untuk txData
//   + [length(32B) + data padded ke 32B] untuk txHash
// ------------------------------------------------------------
void BlockchainClient::logTransaction(const char* txData, const char* txHash) {
    size_t   paddedData = ((strlen(txData) + 31) / 32) * 32;
    uint32_t offset1    = 64;                       // 2 head word * 32 byte
    uint32_t offset2    = offset1 + 32 + paddedData; // + length word + data txData

    char   dataHex[LOG_DATA_HEX_CAP];
    size_t pos = 0;
    pos += snprintf(dataHex + pos, sizeof(dataHex) - pos, "%s", SEL_LOG_TRANSACTION);
    pos += snprintf(dataHex + pos, sizeof(dataHex) - pos, "%064lx", (unsigned long)offset1);
    pos += snprintf(dataHex + pos, sizeof(dataHex) - pos, "%064lx", (unsigned long)offset2);
    pos += abiEncodeDynamicString(txData, dataHex + pos, sizeof(dataHex) - pos);
    pos += abiEncodeDynamicString(txHash, dataHex + pos, sizeof(dataHex) - pos);

    char params[LOG_PARAMS_BUF_SIZE];
    snprintf(params, sizeof(params),
             "[{\"from\":\"%s\",\"to\":\"%s\","
             "\"data\":\"%s\",\"gas\":\"%s\"}]",
             SENDER_ADDRESS,
             CONTRACT_ADDRESS,
             dataHex,
             TX_GAS_LIMIT);

    char payload[LOG_PARAMS_BUF_SIZE];
    // Gunakan eth_sendTransaction (Ganache tidak perlu signed tx)
    buildRpcPayload("eth_sendTransaction", params, payload, sizeof(payload));

    int code = postJson(payload);
    Serial.printf("[BC] logTransaction → HTTP %d | hash: %.16s...\n", code, txHash);
}

// ------------------------------------------------------------
// ABI encoding logAnomaly(uint8 slaveId, uint8 anomalyType, string detail):
//   selector(4B) + slaveId(32B) + anomalyType(32B) + offsetDetail(32B)
//   + [length(32B) + data padded ke 32B] untuk detail
// ------------------------------------------------------------
void BlockchainClient::logAnomaly(uint8_t slaveId, AnomalyType type, const char* detail) {
    uint32_t offsetDetail = 96; // 3 head word * 32 byte

    char   dataHex[LOG_DATA_HEX_CAP];
    size_t pos = 0;
    pos += snprintf(dataHex + pos, sizeof(dataHex) - pos, "%s", SEL_LOG_ANOMALY);
    pos += snprintf(dataHex + pos, sizeof(dataHex) - pos, "%064x", (unsigned int)slaveId);
    pos += snprintf(dataHex + pos, sizeof(dataHex) - pos, "%064x", (unsigned int)type);
    pos += snprintf(dataHex + pos, sizeof(dataHex) - pos, "%064lx", (unsigned long)offsetDetail);
    pos += abiEncodeDynamicString(detail, dataHex + pos, sizeof(dataHex) - pos);

    char params[LOG_PARAMS_BUF_SIZE];
    snprintf(params, sizeof(params),
             "[{\"from\":\"%s\",\"to\":\"%s\","
             "\"data\":\"%s\",\"gas\":\"%s\"}]",
             SENDER_ADDRESS,
             CONTRACT_ADDRESS,
             dataHex,
             TX_GAS_LIMIT);

    char payload[LOG_PARAMS_BUF_SIZE];
    buildRpcPayload("eth_sendTransaction", params, payload, sizeof(payload));

    char txHash[68] = "N/A"; // "0x" + 64 hex + null
    int  code = postJson(payload, txHash, sizeof(txHash));
    Serial.printf("[BC] anomaly committed @t=%lums | slave=%u | jenis=%u | HTTP %d | hash=%s | %s\n",
                  (unsigned long)millis(), slaveId, (uint8_t)type, code, txHash, detail);
}

// ------------------------------------------------------------
int BlockchainClient::postJson(const char* payload, char* outTxHash, size_t hashLen) {
    HTTPClient http;
    http.begin(BLOCKCHAIN_RPC_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000); // 5 detik timeout agar tidak blokir lama

    int code = http.POST(payload);
    updateReachability(code > 0);

    if (outTxHash != nullptr && hashLen > 0) {
        if (code == 200) {
            String body = http.getString();
            JsonDocument doc;
            if (deserializeJson(doc, body) == DeserializationError::Ok) {
                const char* result = doc["result"];
                if (result != nullptr) {
                    strncpy(outTxHash, result, hashLen - 1);
                    outTxHash[hashLen - 1] = '\0';
                } else {
                    strncpy(outTxHash, "no-result", hashLen);
                }
            } else {
                strncpy(outTxHash, "parse-err", hashLen);
            }
        } else {
            snprintf(outTxHash, hashLen, "http-%d", code);
        }
    }

    http.end();
    return code;
}

// ------------------------------------------------------------
void BlockchainClient::buildRpcPayload(const char* methodName,
                                        const char* params,
                                        char*       outBuf,
                                        size_t      outBufLen) {
    // Envelope JSON-RPC 2.0 standar
    snprintf(outBuf, outBufLen,
             "{\"jsonrpc\":\"2.0\","
             "\"method\":\"%s\","
             "\"params\":%s,"
             "\"id\":1}",
             methodName, params);
}
