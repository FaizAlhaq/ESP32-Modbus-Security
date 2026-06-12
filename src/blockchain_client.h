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
    ANOMALY_DEVICE_LOST   = 4,  // Perangkat pernah hadir, kini tidak merespons
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

    // True jika node blockchain berhasil dihubungi pada call terakhir
    bool isReachable() const;

private:
    bool _reachable;  // Status koneksi terakhir ke node blockchain

    // Kirim POST JSON ke RPC endpoint; return HTTP status code
    // Jika outTxHash tidak null, isi dengan tx hash dari respons Ganache
    int  postJson(const char* payload,
                  char*       outTxHash = nullptr,
                  size_t      hashLen   = 0);

    // Buat JSON-RPC envelope untuk pemanggilan smart contract
    // methodName: nama fungsi ABI, params: string JSON array argumen
    void buildRpcPayload(const char* methodName,
                         const char* params,
                         char*       outBuf,
                         size_t      outBufLen);

    // Update _reachable; cetak pesan hanya saat terjadi transisi naik/turun
    void updateReachability(bool success);
};
