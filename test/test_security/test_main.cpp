// ============================================================
//  test/test_security/test_main.cpp
//
//  Test on-device menggunakan Unity (framework bawaan PlatformIO)
//  Jalankan dengan: pio test -e esp32dev
//
//  Semua test hanya menggunakan PollResult buatan (mock) —
//  tidak membutuhkan hardware RS485 maupun koneksi blockchain.
//
//  Skenario yang diuji:
//    1. Transaksi valid dari slave dikenal
//    2. Slave ID tidak dikenal (rogue device)
//    3. Respons tanpa request (injection attempt)
//    4. Waktu respons melebihi jendela (timing anomaly)
//    5. Debit di bawah batas minimum (value range)
//    6. Debit di atas batas maksimum (value range)
//    7. Poll gagal (timeout Modbus) — bukan anomali keamanan
// ============================================================

#include <Arduino.h>
#include <unity.h>

#include "security.h"
#include "modbus_handler.h"
#include "config.h"

// ---- Helper: buat PollResult dari float debit ----
// Mengemas float IEEE-754 ke dua register uint16_t (big-endian word)
static PollResult makePollResult(uint8_t  slaveId,
                                  float    flowRate,
                                  uint32_t responseTimeMs,
                                  bool     success = true,
                                  uint8_t  errorCode = 0) {
    PollResult r;
    r.slave_id         = slaveId;
    r.reg_addr         = REG_FLOW_RATE;
    r.reg_count        = 2;
    r.timestamp_ms     = millis();
    r.response_time_ms = responseTimeMs;
    r.success          = success;
    r.error_code       = errorCode;

    // Kemas float ke dua register (hi word, lo word)
    uint32_t raw;
    memcpy(&raw, &flowRate, sizeof(raw));
    r.values[0] = (uint16_t)(raw >> 16); // word tinggi
    r.values[1] = (uint16_t)(raw & 0xFFFF); // word rendah

    return r;
}

// ---- Instance global Security (blockchain = nullptr, whitelist dilewati) ----
static Security g_sec;

void setUp(void) {
    // Dipanggil sebelum setiap test — reset state request
    g_sec.begin(nullptr); // nullptr = blockchain tidak tersedia
}

void tearDown(void) {}

// ============================================================
// TEST 1: Transaksi valid dari slave yang dikenal
// ============================================================
void test_valid_transaction(void) {
    g_sec.recordRequest(1); // Tandai bahwa request ke slave 1 sudah dikirim

    PollResult    r = makePollResult(1, 25.0f, 100); // 25 L/min, 100ms
    SecurityCheck c;

    bool safe = g_sec.checkPollResult(r, c);

    TEST_ASSERT_TRUE_MESSAGE(safe, "Transaksi valid seharusnya lolos");
    TEST_ASSERT_FALSE(c.anomalyRogueDevice);
    TEST_ASSERT_FALSE(c.anomalyNoRequest);
    TEST_ASSERT_FALSE(c.anomalyTiming);
    TEST_ASSERT_FALSE(c.anomalyValueRange);
}

// ============================================================
// TEST 2: Slave ID tidak dikenal (rogue device, ID = 99)
// ============================================================
void test_rogue_slave_id(void) {
    // Tidak ada recordRequest — slave 99 tidak pernah diminta
    PollResult    r = makePollResult(99, 10.0f, 100);
    SecurityCheck c;

    bool safe = g_sec.checkPollResult(r, c);

    TEST_ASSERT_FALSE_MESSAGE(safe, "Slave ID tidak dikenal harus ditolak");
    TEST_ASSERT_TRUE(c.anomalyRogueDevice);
    TEST_ASSERT_EQUAL(ANOMALY_ROGUE_SLAVE, c.primaryAnomaly);
}

// ============================================================
// TEST 3: Respons dari slave dikenal tapi tanpa request (injection)
// ============================================================
void test_response_without_request(void) {
    // recordRequest() TIDAK dipanggil → simulasi respons tak terduga
    PollResult    r = makePollResult(2, 30.0f, 150);
    SecurityCheck c;

    bool safe = g_sec.checkPollResult(r, c);

    TEST_ASSERT_FALSE_MESSAGE(safe, "Respons tanpa request harus ditolak");
    TEST_ASSERT_TRUE(c.anomalyNoRequest);
    TEST_ASSERT_EQUAL(ANOMALY_NO_REQUEST, c.primaryAnomaly);
}

// ============================================================
// TEST 4: Waktu respons melebihi RESPONSE_WINDOW_MS
// ============================================================
void test_timing_anomaly(void) {
    g_sec.recordRequest(3);

    // Response time = RESPONSE_WINDOW_MS + 1 ms → anomali
    PollResult    r = makePollResult(3, 20.0f, RESPONSE_WINDOW_MS + 1);
    SecurityCheck c;

    bool safe = g_sec.checkPollResult(r, c);

    TEST_ASSERT_FALSE_MESSAGE(safe, "Respons terlalu lambat harus ditolak");
    TEST_ASSERT_TRUE(c.anomalyTiming);
    TEST_ASSERT_EQUAL(ANOMALY_TIMING, c.primaryAnomaly);
}

// ============================================================
// TEST 5: Tepat di batas RESPONSE_WINDOW_MS — masih valid
// ============================================================
void test_timing_at_limit_ok(void) {
    g_sec.recordRequest(4);

    PollResult    r = makePollResult(4, 15.0f, RESPONSE_WINDOW_MS); // tepat di batas
    SecurityCheck c;

    bool safe = g_sec.checkPollResult(r, c);

    TEST_ASSERT_TRUE_MESSAGE(safe, "Respons tepat di batas waktu seharusnya lolos");
    TEST_ASSERT_FALSE(c.anomalyTiming);
}

// ============================================================
// TEST 6: Debit di bawah FLOW_RATE_MIN (negatif)
// ============================================================
void test_flow_below_minimum(void) {
    g_sec.recordRequest(1);

    // -5.0 L/min tidak mungkin secara fisik → anomali
    PollResult    r = makePollResult(1, -5.0f, 100);
    SecurityCheck c;

    bool safe = g_sec.checkPollResult(r, c);

    TEST_ASSERT_FALSE_MESSAGE(safe, "Debit negatif harus ditolak");
    TEST_ASSERT_TRUE(c.anomalyValueRange);
    TEST_ASSERT_EQUAL(ANOMALY_VALUE_RANGE, c.primaryAnomaly);
}

// ============================================================
// TEST 7: Debit di atas FLOW_RATE_MAX
// ============================================================
void test_flow_above_maximum(void) {
    g_sec.recordRequest(2);

    // 200.0 L/min melebihi batas sensor → anomali (mungkin data palsu)
    PollResult    r = makePollResult(2, 200.0f, 100);
    SecurityCheck c;

    bool safe = g_sec.checkPollResult(r, c);

    TEST_ASSERT_FALSE_MESSAGE(safe, "Debit di atas maksimum harus ditolak");
    TEST_ASSERT_TRUE(c.anomalyValueRange);
    TEST_ASSERT_EQUAL(ANOMALY_VALUE_RANGE, c.primaryAnomaly);
}

// ============================================================
// TEST 8: Debit tepat di batas FLOW_RATE_MAX — masih valid
// ============================================================
void test_flow_at_max_ok(void) {
    g_sec.recordRequest(3);

    PollResult    r = makePollResult(3, FLOW_RATE_MAX, 100);
    SecurityCheck c;

    bool safe = g_sec.checkPollResult(r, c);

    TEST_ASSERT_TRUE_MESSAGE(safe, "Debit tepat di batas maksimum seharusnya lolos");
    TEST_ASSERT_FALSE(c.anomalyValueRange);
}

// ============================================================
// TEST 9: Poll gagal (Modbus timeout) — bukan anomali keamanan
// ============================================================
void test_poll_failure_not_anomaly(void) {
    g_sec.recordRequest(5);

    // success=false, errorCode=0xE2 (ModbusMaster timeout)
    PollResult    r = makePollResult(5, 0.0f, 500, false, 0xE2);
    SecurityCheck c;

    bool safe = g_sec.checkPollResult(r, c);

    TEST_ASSERT_FALSE_MESSAGE(safe, "Poll gagal mengembalikan false");
    // Tidak ada flag anomali keamanan yang menyala
    TEST_ASSERT_FALSE(c.anomalyRogueDevice);
    TEST_ASSERT_FALSE(c.anomalyNoRequest);
    TEST_ASSERT_FALSE(c.anomalyTiming);
    TEST_ASSERT_FALSE(c.anomalyValueRange);
}

// ============================================================
// TEST 10: Verifikasi konversi float registersToFloat()
// ============================================================
void test_float_register_conversion(void) {
    // 50.0f → 0x42480000 → hi=0x4248, lo=0x0000
    float result = ModbusHandler::registersToFloat(0x4248, 0x0000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, result);

    // 0.0f → hi=0x0000, lo=0x0000
    result = ModbusHandler::registersToFloat(0x0000, 0x0000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, result);

    // 100.0f → 0x42C80000 → hi=0x42C8, lo=0x0000
    result = ModbusHandler::registersToFloat(0x42C8, 0x0000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, result);
}

// ============================================================
// Entry point PlatformIO test runner
// ============================================================
void setup() {
    delay(2000); // Beri waktu serial monitor terhubung
    UNITY_BEGIN();

    RUN_TEST(test_valid_transaction);
    RUN_TEST(test_rogue_slave_id);
    RUN_TEST(test_response_without_request);
    RUN_TEST(test_timing_anomaly);
    RUN_TEST(test_timing_at_limit_ok);
    RUN_TEST(test_flow_below_minimum);
    RUN_TEST(test_flow_above_maximum);
    RUN_TEST(test_flow_at_max_ok);
    RUN_TEST(test_poll_failure_not_anomaly);
    RUN_TEST(test_float_register_conversion);

    UNITY_END();
}

void loop() {}
