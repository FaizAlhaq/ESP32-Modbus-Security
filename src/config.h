#pragma once

// ============================================================
//  config.h — Konfigurasi global firmware ESP32 gateway
//  Ganti nilai-nilai di bawah sesuai lingkungan pengujian Anda
// ============================================================

// ------------------------------------------------------------
// WiFi
// ------------------------------------------------------------
#define WIFI_SSID        "vivobook go 14"       // Nama jaringan WiFi
#define WIFI_PASSWORD    "halohalohalo"   // Password WiFi
#define WIFI_TIMEOUT_MS  15000            // Maks waktu koneksi (ms)

// ------------------------------------------------------------
// Blockchain (Ganache via JSON-RPC HTTP)
// ------------------------------------------------------------
// Ganti IP dengan IP PC yang menjalankan Ganache
#define BLOCKCHAIN_RPC_URL   "http://10.60.47.82:7545"
// Smart contract address (isi setelah deploy)
#define CONTRACT_ADDRESS     "0x01F354872A49D65665648FC5fbc95dcF2eb7fd7a"
// Private key akun Ganache yang digunakan ESP32 (tanpa "0x")
#define SENDER_PRIVATE_KEY   "1ceb81864c83abdc1bb96f2282239e39795c0e934ba0db49a3a5014bb077f95c"
// Address Ethereum akun di atas — lihat di Ganache tab Accounts
#define SENDER_ADDRESS       "0x1941Ae81ecfe7907f95C88b2B35aD11b92547751"
// Gas limit untuk transaksi blockchain
#define TX_GAS_LIMIT         "0x30000"

// ------------------------------------------------------------
// Modbus RTU / RS485
// ------------------------------------------------------------
#define MODBUS_SERIAL_PORT   Serial2     // UART yang digunakan
#define MODBUS_BAUD_RATE     9600
#define MODBUS_TX_PIN        17          // GPIO TX ke MAX485
#define MODBUS_RX_PIN        16          // GPIO RX dari MAX485
#define MODBUS_DE_RE_PIN     4           // GPIO DE/RE MAX485

// ID slave yang valid di bus (ID 1–5)
#define SLAVE_COUNT          5
static const uint8_t SLAVE_IDS[SLAVE_COUNT] = {1, 2, 3, 4, 5};

// Register Modbus pada setiap slave
#define REG_FLOW_RATE        0x0000      // Debit air (L/min, float 2 reg)
#define REG_TOTAL_VOLUME     0x0002      // Volume total (L, float 2 reg)
#define REG_VALVE_STATUS     0x0004      // Status katup (0=tutup, 1=buka)

// Timeout respons slave (ms)
#define MODBUS_RESPONSE_TIMEOUT_MS  500

// ------------------------------------------------------------
// Deteksi anomali
// ------------------------------------------------------------
// Batas nilai debit (L/min) — di luar ini dianggap anomali
#define FLOW_RATE_MIN        0.0f
#define FLOW_RATE_MAX        100.0f

// Jendela waktu respons yang masih diterima (ms)
#define RESPONSE_WINDOW_MS   600

// ------------------------------------------------------------
// Logger / buffer blockchain
// ------------------------------------------------------------
// Kirim ke blockchain setiap N transaksi ATAU setiap T ms
#define TX_BUFFER_SIZE       10          // Flush setiap 10 transaksi
#define TX_FLUSH_INTERVAL_MS 30000       // Atau setiap 30 detik

// ------------------------------------------------------------
// Interval polling semua slave
// ------------------------------------------------------------
#define POLL_INTERVAL_MS     2000        // Jeda setelah satu putaran penuh
