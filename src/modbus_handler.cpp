// ============================================================
//  modbus_handler.cpp — Implementasi polling Modbus RTU
//
//  Topologi: ESP32 → TX/RX → MAX485 DE/RE → RS485 bus → slave
//  Library : ModbusMaster (4-20ma/ModbusMaster)
// ============================================================

#include "modbus_handler.h"

// ------------------------------------------------------------
// Callback static untuk toggle pin DE/RE MAX485
// HIGH = transmit (ESP32 mengirim ke bus)
// LOW  = receive  (ESP32 mendengarkan bus)
// ------------------------------------------------------------
static void preTransmission() {
    digitalWrite(MODBUS_DE_RE_PIN, HIGH);
}

static void postTransmission() {
    digitalWrite(MODBUS_DE_RE_PIN, LOW);
}

// ------------------------------------------------------------
void ModbusHandler::begin() {
    // Siapkan pin DE/RE sebelum Serial diinisialisasi
    pinMode(MODBUS_DE_RE_PIN, OUTPUT);
    digitalWrite(MODBUS_DE_RE_PIN, LOW); // Mode receive default

    // Inisialisasi UART untuk RS485
    MODBUS_SERIAL_PORT.begin(MODBUS_BAUD_RATE,
                              SERIAL_8N1,
                              MODBUS_RX_PIN,
                              MODBUS_TX_PIN);

    // Daftarkan callback ke library (slave_id diset ulang tiap poll)
    _node.preTransmission(preTransmission);
    _node.postTransmission(postTransmission);

    Serial.println("[MODBUS] Handler siap");
}

// ------------------------------------------------------------
bool ModbusHandler::pollSlave(uint8_t  slaveId,
                               uint16_t regAddr,
                               uint8_t  regCount,
                               PollResult& result) {
    // Batasi jumlah register agar tidak melebihi buffer PollResult
    if (regCount == 0 || regCount > 8) regCount = 8;

    // Isi metadata hasil polling
    result.slave_id        = slaveId;
    result.reg_addr        = regAddr;
    result.reg_count       = regCount;
    result.success         = false;
    result.error_code      = 0xFF;
    result.response_time_ms = 0;
    memset(result.values, 0, sizeof(result.values));

    // Set slave yang akan diajak bicara
    _node.begin(slaveId, MODBUS_SERIAL_PORT);

    // Catat waktu mulai sebelum request
    result.timestamp_ms = millis();
    uint32_t t_start    = millis();

    // Kirim Function Code 03 — Read Holding Registers
    uint8_t status = _node.readHoldingRegisters(regAddr, regCount);

    result.response_time_ms = millis() - t_start;
    result.error_code       = status;

    if (status == ModbusMaster::ku8MBSuccess) {
        // Salin nilai register ke buffer hasil
        for (uint8_t i = 0; i < regCount; i++) {
            result.values[i] = _node.getResponseBuffer(i);
        }
        result.success = true;

        if (regAddr == REG_FORWARD_PULSE && regCount >= 4) {
            uint32_t fwd = ModbusHandler::registersToUint32(result.values[0], result.values[1]);
            uint32_t bwd = ModbusHandler::registersToUint32(result.values[2], result.values[3]);
            int32_t  acc = (int32_t)(fwd - bwd);
            Serial.printf("[MODBUS] Slave %u | fwd=%lu bwd=%lu acc=%ld | OK | %u ms\n",
                          slaveId, (unsigned long)fwd, (unsigned long)bwd,
                          (long)acc, result.response_time_ms);
        } else {
            Serial.printf("[MODBUS] Slave %u | Reg 0x%04X | OK | %u ms\n",
                          slaveId, regAddr, result.response_time_ms);
        }
    } else {
        Serial.printf("[MODBUS] Slave %u | Reg 0x%04X | ERR 0x%02X | %u ms\n",
                      slaveId, regAddr, status, result.response_time_ms);
    }

    return result.success;
}

// ------------------------------------------------------------
// Konversi dua register 16-bit → float IEEE-754
// Konvensi: hi = word tinggi (MSW), lo = word rendah (LSW)
// ------------------------------------------------------------
float ModbusHandler::registersToFloat(uint16_t hi, uint16_t lo) {
    uint32_t raw = ((uint32_t)hi << 16) | lo;
    float    val;
    memcpy(&val, &raw, sizeof(val));
    return val;
}

// ------------------------------------------------------------
// Konversi dua register 16-bit → uint32 — LSW dulu (konvensi AGNIKA)
// getResponseBuffer(N)   = register alamat rendah = LSW
// getResponseBuffer(N+1) = register alamat tinggi = MSW
// ------------------------------------------------------------
uint32_t ModbusHandler::registersToUint32(uint16_t lo, uint16_t hi) {
    return ((uint32_t)hi << 16) | lo;
}
