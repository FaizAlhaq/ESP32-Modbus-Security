#pragma once

// ============================================================
//  blockchain_client.h — Klien HTTP ke node blockchain (Ganache)
//
//  Tiga fungsi utama:
//    verifyDevice()    — cek apakah slave ada di whitelist
//    logTransaction()  — kirim hash transaksi ke blockchain
//    logAnomaly()      — kirim alert anomali ke blockchain
// ============================================================

#include <Arduino.h>

// Tipe anomali yang dapat dilaporkan
enum AnomalyType {
    ANOMALY_NO_REQUEST    = 0,  // Respons tanpa request sebelumnya
    ANOMALY_ROGUE_SLAVE   = 1,  // Slave ID tidak dikenal
    ANOMALY_TIMING        = 2,  // Respons di luar jendela waktu
    ANOMALY_VALUE_RANGE   = 3,  // Nilai register di luar batas
};

class BlockchainClient {
public:
    // Inisialisasi (tidak membutuhkan koneksi; WiFi sudah harus aktif)
    void begin();

    // Cek apakah slaveId terdaftar di whitelist blockchain
    // Return: true jika terdaftar, false jika tidak atau error
    bool verifyDevice(uint8_t slaveId);

    // Kirim satu record transaksi ke blockchain
    // txData: string yang sudah dibentuk di logger
    // txHash: hash SHA-256 dari txData (64 karakter hex)
    void logTransaction(const char* txData, const char* txHash);

    // Kirim alert anomali ke blockchain
    void logAnomaly(uint8_t slaveId, AnomalyType type, const char* detail);

private:
    // Kirim POST JSON ke RPC endpoint; return HTTP status code
    int  postJson(const char* payload);

    // Buat JSON-RPC envelope untuk pemanggilan smart contract
    // methodName: nama fungsi ABI, params: string JSON array argumen
    void buildRpcPayload(const char* methodName,
                         const char* params,
                         char*       outBuf,
                         size_t      outBufLen);
};
