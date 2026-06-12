// ============================================================
//  logger.cpp — Buffered transaction log + anomaly reporter
// ============================================================

#include "logger.h"
#include <string.h>

// ------------------------------------------------------------
void Logger::begin(BlockchainClient* bc) {
    _bc             = bc;
    _count          = 0;
    _lastFlushMs    = 0;
    _pendingAnomaly = 0;
    memset(_buffer, 0, sizeof(_buffer));
    Serial.println("[LOG] Logger siap");
}

// ------------------------------------------------------------
void Logger::addTransaction(const PollResult& result) {
    if (_count >= TX_BUFFER_SIZE) {
        // Buffer penuh sebelum waktunya — flush sekarang
        Serial.println("[LOG] Buffer penuh, flush paksa");
        flush();
    }

    TxEntry& entry = _buffer[_count];

    // Bangun string canonical dan hitung hash-nya
    HashUtil::buildTxString(
        result.slave_id,
        0x03,                    // Function Code 03 (Read Holding Registers)
        result.reg_addr,
        result.values[0],        // Nilai register pertama sebagai representasi
        result.timestamp_ms,
        entry.txString,
        sizeof(entry.txString));

    HashUtil::sha256Hex(entry.txString, entry.txHash);

    _count++;

    Serial.printf("[LOG] +tx buffer[%u/%u] slave=%u hash=%.8s...\n",
                  _count, TX_BUFFER_SIZE,
                  result.slave_id,
                  entry.txHash);
}

// ------------------------------------------------------------
void Logger::reportAnomaly(const PollResult& result,
                            const SecurityCheck& check) {
    if (_bc == nullptr || !_bc->isReachable()) {
        _pendingAnomaly++;
        Serial.printf("[LOG] ANOMALI lokal [tertunda=%u] @t=%lums: slave=%u | %s\n",
                      _pendingAnomaly, (unsigned long)millis(),
                      result.slave_id, check.detail);
        return;
    }

    // BC tersedia — kirim langsung
    _bc->logAnomaly(result.slave_id, check.primaryAnomaly, check.detail);

    if (_pendingAnomaly > 0) {
        Serial.printf("[LOG] PERINGATAN: %u anomali tidak terkirim selama BC down\n",
                      _pendingAnomaly);
        _pendingAnomaly = 0;
    }

    Serial.printf("[LOG] ANOMALI dikirim: slave=%u type=%u | %s\n",
                  result.slave_id,
                  (uint8_t)check.primaryAnomaly,
                  check.detail);
}

// ------------------------------------------------------------
void Logger::flushIfNeeded() {
    bool timeElapsed = (millis() - _lastFlushMs) >= TX_FLUSH_INTERVAL_MS;
    bool bufferFull  = (_count >= TX_BUFFER_SIZE);

    if (_count > 0 && (timeElapsed || bufferFull)) {
        flush();
    }
}

// ------------------------------------------------------------
void Logger::flushNow() {
    if (_count > 0) flush();
}

// ------------------------------------------------------------
void Logger::flush() {
    if (_bc == nullptr || !_bc->isReachable()) {
        Serial.printf("[LOG] Flush diabaikan — BC tidak terjangkau. %u tx dibuang, anomali tertunda: %u\n",
                      _count, _pendingAnomaly);
        _count       = 0;
        _lastFlushMs = millis();
        return;
    }

    if (_pendingAnomaly > 0) {
        Serial.printf("[LOG] PERINGATAN: %u anomali tidak terkirim selama BC down\n",
                      _pendingAnomaly);
        _pendingAnomaly = 0;
    }

    Serial.printf("[LOG] Flush %u transaksi ke blockchain...\n", _count);

    for (uint8_t i = 0; i < _count; i++) {
        _bc->logTransaction(_buffer[i].txString, _buffer[i].txHash);
    }

    _count       = 0;
    _lastFlushMs = millis();

    Serial.println("[LOG] Flush selesai");
}
