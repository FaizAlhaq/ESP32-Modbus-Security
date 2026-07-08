// ============================================================
//  main.cpp — Entry point firmware ESP32 Gateway Modbus RTU
//
//  Alur utama (loop):
//    Untuk setiap slave (ID 1–5):
//      1. Catat request → recordRequest()
//      2. Poll slave via Modbus RTU
//      3. Periksa keamanan → checkPollResult()
//      4a. Valid  : tambah ke buffer transaksi
//      4b. Invalid: kirim anomali ke blockchain langsung
//    Flush buffer jika waktunya tiba
//    Delay antar putaran
// ============================================================

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "modbus_handler.h"
#include "hash_util.h"
#include "blockchain_client.h"
#include "security.h"
#include "logger.h"

// ------------------------------------------------------------
// Objek global (satu instance masing-masing)
// ------------------------------------------------------------
static ModbusHandler    g_modbus;
static BlockchainClient g_bc;
static Security         g_security;
static Logger           g_logger;

// ------------------------------------------------------------
// Koneksi WiFi dengan retry sederhana
// ------------------------------------------------------------
static void connectWifi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("[WIFI] Menghubungkan ke %s ...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < WIFI_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] Terhubung. IP: %s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WIFI] Gagal terhubung. Blockchain tidak tersedia.");
    }
}

// ------------------------------------------------------------
// Polling satu slave + pemeriksaan keamanan
// ------------------------------------------------------------
static void pollAndCheck(uint8_t slaveId) {
    PollResult    result;
    SecurityCheck check;

    // Tandai bahwa kita akan mengirim request ke slave ini
    g_security.recordRequest(slaveId);

    // Poll 4 register AGNIKA: forward pulse (0x0000-0x0001) + backward pulse (0x0002-0x0003)
    bool polled = g_modbus.pollSlave(slaveId, REG_FORWARD_PULSE, 4, result);

    if (!polled) {
        if (g_security.wasPresent(slaveId)) {
            // Perangkat yang sebelumnya aktif kini tidak merespons — anomali
            SecurityCheck check;
            check.passed             = false;
            check.anomalyRogueDevice = false;
            check.anomalyNoRequest   = false;
            check.anomalyDeviceLost  = true;
            check.anomalyValueRange  = false;
            check.primaryAnomaly     = ANOMALY_DEVICE_LOST;
            snprintf(check.detail, sizeof(check.detail),
                     "Slave %u tidak merespons setelah sebelumnya aktif", slaveId);
            Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=DEVICE_LOST"
                          " | detail=%s\n",
                          (unsigned long)millis(), slaveId, check.detail);
            g_logger.reportAnomaly(result, check);
            g_security.markLost(slaveId); // reset agar reconnect memicu identity check
        } else {
            Serial.printf("[MAIN] Slave %u tidak merespons\n", slaveId);
        }
        return;
    }

    // Kontak pertama ATAU reconnect setelah DEVICE_LOST → verifikasi identitas
    if (!g_security.isCurrentlyPresent(slaveId)) {
        uint8_t uid32[32];
        if (g_modbus.readUID(slaveId, uid32)) {
            // Cetak UID 24-hex (12 byte bermakna, byte 20–31)
            Serial.printf("[SEC] Slave %u UID = 0x", slaveId);
            for (int i = 20; i < 32; i++) Serial.printf("%02x", uid32[i]);
            Serial.println();

            if (g_bc.verifyDevice(slaveId, uid32)) {
                Serial.printf("[SEC] identitas slave %u terverifikasi (UID cocok)\n", slaveId);
            } else {
                SecurityCheck idCheck;
                idCheck.passed             = false;
                idCheck.anomalyRogueDevice = false;
                idCheck.anomalyNoRequest   = false;
                idCheck.anomalyDeviceLost  = false;
                idCheck.anomalyValueRange  = false;
                idCheck.primaryAnomaly     = ANOMALY_IDENTITY;
                snprintf(idCheck.detail, sizeof(idCheck.detail),
                         "Slave %u UID tidak cocok / belum terdaftar", slaveId);
                Serial.printf("[SEC] >>> ANOMALI @t=%lums | slave=%u | jenis=IDENTITY"
                              " | detail=%s\n",
                              (unsigned long)millis(), slaveId, idCheck.detail);
                g_logger.reportAnomaly(result, idCheck);
            }
        } else {
            Serial.printf("[SEC] Slave %u: gagal baca UID, verifikasi identitas dilewati\n",
                          slaveId);
        }
    }

    g_security.markPresent(slaveId); // poll sukses — catat kehadiran slave ini

    // Periksa keamanan
    bool safe = g_security.checkPollResult(result, check);

    if (safe) {
        // Transaksi valid → masukkan ke buffer log
        g_logger.addTransaction(result);
    } else {
        // Anomali terdeteksi → kirim alert langsung ke blockchain
        g_logger.reportAnomaly(result, check);
    }
}

// ------------------------------------------------------------
#ifndef UNIT_TEST
void setup() {
    Serial.begin(115200);
    Serial.println("\n[MAIN] ===== ESP32 Modbus Security Gateway =====");

    // Inisialisasi hardware Modbus
    g_modbus.begin();

    // Koneksi WiFi
    connectWifi();

    // Inisialisasi modul blockchain, security, logger
    g_bc.begin();
    g_security.begin(&g_bc);
    g_logger.begin(&g_bc);

    Serial.println("[MAIN] Setup selesai. Mulai polling...\n");
}

// ------------------------------------------------------------
void loop() {
    // Reset state request untuk putaran baru
    g_security.resetRequestState();

    // Poll semua slave secara berurutan
    for (uint8_t i = 0; i < SLAVE_COUNT; i++) {
        pollAndCheck(SLAVE_IDS[i]);
        delay(50); // Jeda singkat antar request agar bus tidak tabrakan
    }

    // Flush buffer transaksi ke blockchain jika waktunya tiba
    g_logger.flushIfNeeded();

    // Jeda sebelum putaran berikutnya
    delay(POLL_INTERVAL_MS);
}
#endif // UNIT_TEST