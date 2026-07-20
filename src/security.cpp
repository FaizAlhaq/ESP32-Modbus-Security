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
        case ANOMALY_DEVICE_LOST: return "DEVICE_LOST";
        case ANOMALY_IDENTITY:    return "IDENTITY";
        default:                  return "UNKNOWN";
    }
}

// ------------------------------------------------------------
void Security::begin(BlockchainClient* bc) {
    _bc = bc;
    memset(_requestSent,      false, sizeof(_requestSent));
    memset(_wasPresent,       false, sizeof(_wasPresent));
    memset(_currentlyPresent, false, sizeof(_currentlyPresent));
    for (uint8_t i = 0; i <= SLAVE_COUNT; i++) {
        _lastForwardPulse[i]  = UINT32_MAX; // UINT32_MAX = belum ada pembacaan
        _lastBackwardPulse[i] = UINT32_MAX; // UINT32_MAX = belum ada pembacaan
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
void Security::markPresent(uint8_t slaveId) {
    if (slaveId >= 1 && slaveId <= SLAVE_COUNT) {
        _wasPresent[slaveId]       = true;
        _currentlyPresent[slaveId] = true;
    }
}

// ------------------------------------------------------------
bool Security::wasPresent(uint8_t slaveId) {
    if (slaveId >= 1 && slaveId <= SLAVE_COUNT) return _wasPresent[slaveId];
    return false;
}

// ------------------------------------------------------------
void Security::markLost(uint8_t slaveId) {
    if (slaveId >= 1 && slaveId <= SLAVE_COUNT) _currentlyPresent[slaveId] = false;
}

// ------------------------------------------------------------
bool Security::isCurrentlyPresent(uint8_t slaveId) {
    if (slaveId >= 1 && slaveId <= SLAVE_COUNT) return _currentlyPresent[slaveId];
    return false;
}

// ------------------------------------------------------------
bool Security::checkPollResult(const PollResult& result, SecurityCheck& check) {
    check.passed             = true;
    check.anomalyRogueDevice = false;
    check.anomalyNoRequest   = false;
    check.anomalyDeviceLost  = false;
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
// Respons yang melewati RESPONSE_WINDOW_MS diklasifikasikan sebagai
// DEVICE_LOST (bukan TIMING terpisah) — perangkat yang merespons
// terlalu lambat dianggap efektif tidak responsif. Keputusan terkunci,
// lihat LOCKED_DEFINITION.md.
// ------------------------------------------------------------
bool Security::checkTiming(const PollResult& r, SecurityCheck& c) {
    if (r.response_time_ms > RESPONSE_WINDOW_MS) {
        c.anomalyDeviceLost = true;
        c.primaryAnomaly    = ANOMALY_DEVICE_LOST;
        snprintf(c.detail, sizeof(c.detail),
                 "Slave %u respons %lu ms (maks %u ms) - dianggap tidak responsif",
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

    // Gerbang backward pulse: totalizer aliran balik tidak boleh mundur dan
    // kenaikannya tidak boleh mencapai ambang aliran balik wajar.
    if (r.reg_count >= 4) {
        uint32_t bwd = ModbusHandler::registersToUint32(r.values[2], r.values[3]);

        if (_lastBackwardPulse[id] != UINT32_MAX) {
            if (bwd < _lastBackwardPulse[id]) {
                c.anomalyValueRange = true;
                c.primaryAnomaly    = ANOMALY_VALUE_RANGE;
                snprintf(c.detail, sizeof(c.detail),
                         "Backward pulse turun: %lu -> %lu",
                         (unsigned long)_lastBackwardPulse[id], (unsigned long)bwd);
                Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=%s | detail=%s\n",
                     (unsigned long)millis(), r.slave_id,
                     anomalyName(c.primaryAnomaly), c.detail);
                return false;
            }
            if ((bwd - _lastBackwardPulse[id]) >= MAX_BACKWARD_DELTA) {
                c.anomalyValueRange = true;
                c.primaryAnomaly    = ANOMALY_VALUE_RANGE;
                snprintf(c.detail, sizeof(c.detail),
                         "Aliran balik %lu pulsa (ambang %lu)",
                         (unsigned long)(bwd - _lastBackwardPulse[id]),
                         (unsigned long)MAX_BACKWARD_DELTA);
                Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=%s | detail=%s\n",
                     (unsigned long)millis(), r.slave_id,
                     anomalyName(c.primaryAnomaly), c.detail);
                return false;
            }
        }
        _lastBackwardPulse[id] = bwd;
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
