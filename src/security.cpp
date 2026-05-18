// ============================================================
//  security.cpp — Implementasi deteksi anomali Modbus
// ============================================================

#include "security.h"
#include <string.h>

// ------------------------------------------------------------
void Security::begin(BlockchainClient* bc) {
    _bc = bc;
    memset(_requestSent, false, sizeof(_requestSent));
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
        Serial.printf("[SEC] ANOMALI: %s\n", c.detail);
        return false;
    }

    if (!_requestSent[id]) {
        c.anomalyNoRequest = true;
        c.primaryAnomaly   = ANOMALY_NO_REQUEST;
        snprintf(c.detail, sizeof(c.detail),
                 "Respons dari slave %u tanpa request", id);
        Serial.printf("[SEC] ANOMALI: %s\n", c.detail);
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
        Serial.printf("[SEC] ANOMALI: %s\n", c.detail);
        return false;
    }
    return true;
}

// ------------------------------------------------------------
// Lapisan 3: nilai register debit dalam batas wajar
// ------------------------------------------------------------
bool Security::checkValueRange(const PollResult& r, SecurityCheck& c) {
    if (r.reg_addr != REG_FLOW_RATE) return true;

    float flow = ModbusHandler::registersToFloat(r.values[0], r.values[1]);
    if (flow < FLOW_RATE_MIN || flow > FLOW_RATE_MAX) {
        c.anomalyValueRange = true;
        c.primaryAnomaly    = ANOMALY_VALUE_RANGE;
        snprintf(c.detail, sizeof(c.detail),
                 "Slave %u debit %.2f L/min di luar batas [%.1f, %.1f]",
                 r.slave_id, flow, FLOW_RATE_MIN, FLOW_RATE_MAX);
        Serial.printf("[SEC] ANOMALI: %s\n", c.detail);
        return false;
    }
    return true;
}

// ------------------------------------------------------------
// Lapisan 4: verifikasi whitelist blockchain (jaringan)
// Hanya dipanggil jika tiga lapisan lokal sudah lolos
// ------------------------------------------------------------
bool Security::checkWhitelist(const PollResult& r, SecurityCheck& c) {
    if (_bc == nullptr) return true;

    bool whitelisted = _bc->verifyDevice(r.slave_id);
    if (!whitelisted) {
        c.anomalyRogueDevice = true;
        c.primaryAnomaly     = ANOMALY_ROGUE_SLAVE;
        snprintf(c.detail, sizeof(c.detail),
                 "Slave %u tidak ada di whitelist blockchain", r.slave_id);
        Serial.printf("[SEC] ANOMALI: %s\n", c.detail);
        return false;
    }
    return true;
}
