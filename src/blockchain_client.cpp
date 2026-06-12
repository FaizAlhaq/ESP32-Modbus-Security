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

// Keccak-4 byte selector fungsi contract (hitung dari ABI)
// Ganti dengan selector dari contract yang Anda deploy
#define SEL_VERIFY_DEVICE    "0xeca8e63d"  // verifyDevice(uint8)
#define SEL_LOG_TRANSACTION  "0xd8628357"  // logTransaction(string,string)
#define SEL_LOG_ANOMALY      "0x98bf92e5"  // logAnomaly(uint8,uint8,string)

// Ukuran buffer payload JSON — cukup untuk satu RPC call
#define RPC_BUF_SIZE  768

// ------------------------------------------------------------
void BlockchainClient::begin() {
    Serial.println("[BC] BlockchainClient siap, RPC: " BLOCKCHAIN_RPC_URL);
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

    http.end();
    return verified;
}

// ------------------------------------------------------------
void BlockchainClient::logTransaction(const char* txData, const char* txHash) {
    // Encode dua string sebagai parameter ABI (simplified)
    char params[RPC_BUF_SIZE];
    snprintf(params, sizeof(params),
             "[{\"from\":\"%s\",\"to\":\"%s\","
             "\"data\":\"%s\",\"gas\":\"%s\"}]",
             SENDER_ADDRESS,
             CONTRACT_ADDRESS,
             SEL_LOG_TRANSACTION,
             TX_GAS_LIMIT);

    char payload[RPC_BUF_SIZE];
    // Gunakan eth_sendTransaction (Ganache tidak perlu signed tx)
    buildRpcPayload("eth_sendTransaction", params, payload, sizeof(payload));

    int code = postJson(payload);
    Serial.printf("[BC] logTransaction → HTTP %d | hash: %.16s...\n", code, txHash);
}

// ------------------------------------------------------------
void BlockchainClient::logAnomaly(uint8_t slaveId, AnomalyType type, const char* detail) {
    char params[RPC_BUF_SIZE];
    snprintf(params, sizeof(params),
             "[{\"from\":\"%s\",\"to\":\"%s\","
             "\"data\":\"%s%02x%02x\",\"gas\":\"%s\"}]",
             SENDER_ADDRESS,
             CONTRACT_ADDRESS,
             SEL_LOG_ANOMALY,
             slaveId, (uint8_t)type,
             TX_GAS_LIMIT);

    char payload[RPC_BUF_SIZE];
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
