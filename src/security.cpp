// ============================================================
//  security.cpp — Implementasi deteksi anomali Modbus
// ============================================================

#include "security.h"
#include <string.h>

static const char* anomalyName(AnomalyType t) {
    switch (t) {
        case ANOMALY_NO_REQUEST:  return "NO_REQUEST";
        case ANOMALY_ROGUE_SLAVE: return "ROGUE_ID";
        case ANOMALY_TIMING:      return "TIMING";
        case ANOMALY_VALUE_RANGE: return "VALUE_RANGE";
        default:                  return "UNKNOWN";
    }
}

// ------------------------------------------------------------
void Security::begin(BlockchainClient* bc) {
    _bc = bc;
    memset(_requestSent, false, sizeof(_requestSent));
    for (uint8_t i = 0; i <= SLAVE_COUNT; i++) {
        _lastForwardPulse[i] = UINT32_MAX; // UINT32_MAX = belum ada pembacaan
    }
    Serial.println("[SEC] Security module siap");
}

// ------------------------------------------------------------
void Security::recordRequest(uint8_t slaveId) {
    if (slaveId >= 1 && slaveId <= SLAVE_COUNT) {
        _requestSent[slaveId] = true;
    }
}

// ------------------------------------------------------------
void Security::resetRequestState() {
    memset(_requestSent, false, sizeof(_requestSent));
}

// ------------------------------------------------------------
bool Security::checkPollResult(const PollResult& result, SecurityCheck& check) {
    check.passed             = true;
    check.anomalyRogueDevice = false;
    check.anomalyNoRequest   = false;
    check.anomalyTiming      = false;
    check.anomalyValueRange  = false;
    check.detail[0]          = '\0';

    // Kegagalan Modbus biasa (timeout/CRC) bukan anomali keamanan
    if (!result.success) {
        snprintf(check.detail, sizeof(check.detail),
                 "Poll gagal, slave=%u err=0x%02X",
                 result.slave_id, result.error_code);
        return false;
    }

    // Empat lapisan; hentikan di anomali pertama
    if (!checkSlaveId(result, check))    { check.passed = false; return false; }
    if (!checkTiming(result, check))     { check.passed = false; return false; }
    if (!checkValueRange(result, check)) { check.passed = false; return false; }
    if (!checkWhitelist(result, check))  { check.passed = false; return false; }

    return true;
}

// ------------------------------------------------------------
// Lapisan 1: ID slave dikenal dan request memang pernah dikirim
// ------------------------------------------------------------
bool Security::checkSlaveId(const PollResult& r, SecurityCheck& c) {
    uint8_t id = r.slave_id;
    bool knownId = false;
    for (uint8_t i = 0; i < SLAVE_COUNT; i++) {
        if (SLAVE_IDS[i] == id) { knownId = true; break; }
    }

    if (!knownId) {
        c.anomalyRogueDevice = true;
        c.primaryAnomaly     = ANOMALY_ROGUE_SLAVE;
        snprintf(c.detail, sizeof(c.detail), "Slave ID %u tidak dikenal", id);
        Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=%s | detail=%s\n",
                     (unsigned long)millis(), r.slave_id,
                     anomalyName(c.primaryAnomaly), c.detail);
        return false;
    }

    if (!_requestSent[id]) {
        c.anomalyNoRequest = true;
        c.primaryAnomaly   = ANOMALY_NO_REQUEST;
        snprintf(c.detail, sizeof(c.detail),
                 "Respons dari slave %u tanpa request", id);
        Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=%s | detail=%s\n",
                     (unsigned long)millis(), r.slave_id,
                     anomalyName(c.primaryAnomaly), c.detail);
        return false;
    }
    return true;
}

// ------------------------------------------------------------
// Lapisan 2: waktu respons dalam jendela yang wajar
// ------------------------------------------------------------
bool Security::checkTiming(const PollResult& r, SecurityCheck& c) {
    if (r.response_time_ms > RESPONSE_WINDOW_MS) {
        c.anomalyTiming  = true;
        c.primaryAnomaly = ANOMALY_TIMING;
        snprintf(c.detail, sizeof(c.detail),
                 "Slave %u respons %lu ms (maks %u ms)",
                 r.slave_id,
                 (unsigned long)r.response_time_ms,
                 RESPONSE_WINDOW_MS);
        Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=%s | detail=%s\n",
                     (unsigned long)millis(), r.slave_id,
                     anomalyName(c.primaryAnomaly), c.detail);
        return false;
    }
    return true;
}

// ------------------------------------------------------------
// Lapisan 3: validasi pulse totalizer AGNIKA
//   - Forward pulse hanya boleh naik (nilai kumulatif)
//   - Kenaikan per polling tidak boleh melebihi MAX_PULSE_DELTA
// ------------------------------------------------------------
bool Security::checkValueRange(const PollResult& r, SecurityCheck& c) {
    if (r.reg_addr != REG_FORWARD_PULSE) return true;

    uint8_t  id  = r.slave_id;
    uint32_t fwd = ModbusHandler::registersToUint32(r.values[0], r.values[1]);

    if (_lastForwardPulse[id] != UINT32_MAX) {
        // Anomali: forward pulse turun — totalizer tidak boleh berkurang
        if (fwd < _lastForwardPulse[id]) {
            c.anomalyValueRange = true;
            c.primaryAnomaly    = ANOMALY_VALUE_RANGE;
            snprintf(c.detail, sizeof(c.detail),
                     "Slave %u forward pulse turun: %lu -> %lu",
                     id, (unsigned long)_lastForwardPulse[id], (unsigned long)fwd);
            Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=%s | detail=%s\n",
                     (unsigned long)millis(), r.slave_id,
                     anomalyName(c.primaryAnomaly), c.detail);
            return false;
        }

        // Anomali: kenaikan melebihi batas kalibrasi
        if ((fwd - _lastForwardPulse[id]) > MAX_PULSE_DELTA) {
            c.anomalyValueRange = true;
            c.primaryAnomaly    = ANOMALY_VALUE_RANGE;
            snprintf(c.detail, sizeof(c.detail),
                     "Slave %u delta pulse %lu melebihi batas %lu",
                     id,
                     (unsigned long)(fwd - _lastForwardPulse[id]),
                     (unsigned long)MAX_PULSE_DELTA);
            Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=%s | detail=%s\n",
                     (unsigned long)millis(), r.slave_id,
                     anomalyName(c.primaryAnomaly), c.detail);
            return false;
        }
    }

    _lastForwardPulse[id] = fwd;
    return true;
}

// ------------------------------------------------------------
// Lapisan 4: verifikasi whitelist blockchain (jaringan)
// Hanya dipanggil jika tiga lapisan lokal sudah lolos
// ------------------------------------------------------------
bool Security::checkWhitelist(const PollResult& r, SecurityCheck& c) {
    if (_bc == nullptr) return true;

    bool whitelisted = _bc->verifyDevice(r.slave_id);

    if (!_bc->isReachable()) {
        // Node tidak terjangkau — JANGAN diam-diam loloskan
        c.anomalyRogueDevice = true;
        c.primaryAnomaly     = ANOMALY_ROGUE_SLAVE;
        snprintf(c.detail, sizeof(c.detail),
                 "Slave %u whitelist TAK TERVERIFIKASI (BC tidak terjangkau)", r.slave_id);
        Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=%s | detail=%s\n",
                     (unsigned long)millis(), r.slave_id,
                     anomalyName(c.primaryAnomaly), c.detail);
        return false;
    }

    if (!whitelisted) {
        c.anomalyRogueDevice = true;
        c.primaryAnomaly     = ANOMALY_ROGUE_SLAVE;
        snprintf(c.detail, sizeof(c.detail),
                 "Slave %u tidak ada di whitelist blockchain", r.slave_id);
        Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=%s | detail=%s\n",
                     (unsigned long)millis(), r.slave_id,
                     anomalyName(c.primaryAnomaly), c.detail);
        return false;
    }
    return true;
}
