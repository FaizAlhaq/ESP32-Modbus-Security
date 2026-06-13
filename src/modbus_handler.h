#pragma once

// ============================================================
//  modbus_handler.h — Antarmuka polling Modbus RTU (master)
// ============================================================

#include <Arduino.h>
#include <ModbusMaster.h>
#include "config.h"

// Alamat awal 6 register UID AGNIKA (96-bit, big-endian)
#define REG_UID  0x000D

// Hasil satu kali polling ke satu slave
struct PollResult {
    uint8_t  slave_id;
    uint16_t reg_addr;
    uint8_t  reg_count;
    uint16_t values[8];         // Nilai register mentah (maks 8)
    uint32_t timestamp_ms;      // millis() saat request dikirim
    uint32_t response_time_ms;  // Durasi hingga respons diterima
    bool     success;
    uint8_t  error_code;        // Kode error ModbusMaster (0 = OK)
};

class ModbusHandler {
public:
    // Inisialisasi Serial2 dan pin MAX485 DE/RE
    void begin();

    // Poll satu slave: baca regCount register mulai regAddr
    // Hasil disimpan di result; return true jika sukses
    bool pollSlave(uint8_t  slaveId,
                   uint16_t regAddr,
                   uint8_t  regCount,
                   PollResult& result);

    // Konversi dua register 16-bit menjadi float IEEE-754 (MSW dulu)
    static float registersToFloat(uint16_t hi, uint16_t lo);

    // Konversi dua register 16-bit menjadi uint32 — LSW dulu (konvensi AGNIKA)
    // Panggil: registersToUint32(values[N], values[N+1])
    static uint32_t registersToUint32(uint16_t lo, uint16_t hi);

    // Baca UID AGNIKA: 6 register mulai REG_UID (0x000D), big-endian → 96-bit.
    // outUid32: buffer 32 byte (byte 0-19 = 0x00, byte 20-31 = UID big-endian).
    // Return true jika berhasil.
    bool readUID(uint8_t slaveId, uint8_t outUid32[32]);

private:
    ModbusMaster _node;
};
