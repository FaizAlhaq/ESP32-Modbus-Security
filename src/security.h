#pragma once

// ============================================================
//  security.h — Deteksi anomali pada transaksi Modbus
//
//  Tiga lapisan pengecekan:
//    1. Validasi respons (slave ID, timing)
//    2. Validasi nilai (range debit air)
//    3. Verifikasi whitelist blockchain (opsional, dilakukan async)
// ============================================================

#include <Arduino.h>
#include "modbus_handler.h"
#include "blockchain_client.h"
#include "config.h"

// Hasil pemeriksaan keamanan satu PollResult
struct SecurityCheck {
    bool    passed;             // true = lolos semua pemeriksaan
    bool    anomalyRogueDevice; // Slave tidak ada di whitelist
    bool    anomalyNoRequest;   // Respons datang tanpa request tercatat
    bool    anomalyTiming;      // Respons terlalu lambat
    bool    anomalyValueRange;  // Nilai di luar batas wajar
    AnomalyType primaryAnomaly; // Tipe anomali utama (jika ada)
    char    detail[64];         // Deskripsi singkat untuk log
};

class Security {
public:
    // Inisialisasi — simpan pointer ke blockchain client
    void begin(BlockchainClient* bc);

    // Periksa satu PollResult; isi SecurityCheck dan return true jika aman
    bool checkPollResult(const PollResult& result, SecurityCheck& check);

    // Tandai bahwa request untuk slaveId telah dikirim (panggil SEBELUM poll)
    void recordRequest(uint8_t slaveId);

    // Reset state request setelah satu putaran penuh
    void resetRequestState();

private:
    BlockchainClient* _bc;

    // Catat slave mana yang sudah diberi request dalam putaran ini
    bool _requestSent[SLAVE_COUNT + 1]; // indeks = slave_id (1–5)

    bool checkSlaveId(const PollResult& r, SecurityCheck& c);
    bool checkTiming(const PollResult& r, SecurityCheck& c);
    bool checkValueRange(const PollResult& r, SecurityCheck& c);
    bool checkWhitelist(const PollResult& r, SecurityCheck& c);
};
