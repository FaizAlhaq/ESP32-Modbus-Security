#pragma once

// ============================================================
//  logger.h — Buffer transaksi + flush terjadwal ke blockchain
//
//  Alur:
//    addTransaction() → isi buffer lokal
//    loop() memanggil flushIfNeeded() → kirim ke blockchain
//    jika anomali → logAnomaly() langsung tanpa buffer
// ============================================================

#include <Arduino.h>
#include "modbus_handler.h"
#include "blockchain_client.h"
#include "hash_util.h"
#include "security.h"
#include "config.h"

// Satu entri dalam buffer transaksi
struct TxEntry {
    char txString[128];          // String canonical transaksi
    char txHash[SHA256_HEX_LEN]; // Hash SHA-256 dari txString
};

class Logger {
public:
    // Inisialisasi — simpan pointer ke blockchain client
    void begin(BlockchainClient* bc);

    // Tambah transaksi valid ke buffer; flush otomatis jika penuh
    void addTransaction(const PollResult& result);

    // Laporkan anomali langsung (tanpa buffer) ke blockchain
    void reportAnomaly(const PollResult& result, const SecurityCheck& check);

    // Dipanggil setiap iterasi loop() — flush jika waktunya tiba
    void flushIfNeeded();

    // Paksa flush sekarang (misal sebelum deep sleep)
    void flushNow();

private:
    BlockchainClient* _bc;
    TxEntry  _buffer[TX_BUFFER_SIZE];
    uint8_t  _count;            // Jumlah entri di buffer saat ini
    uint32_t _lastFlushMs;      // Waktu flush terakhir
    uint16_t _pendingAnomaly;   // Anomali yang gagal dikirim saat BC down

    void flush();
};
