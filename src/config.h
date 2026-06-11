#pragma once

// ============================================================
//  config.h — Konfigurasi global firmware ESP32 gateway
//  Ganti nilai-nilai di bawah sesuai lingkungan pengujian Anda
// ============================================================

// ------------------------------------------------------------
// WiFi
// ------------------------------------------------------------
#define WIFI_SSID        "YOUR_WIFI_SSID"     // ← ganti dengan nama WiFi Anda
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD" // ← ganti dengan password WiFi Anda
#define WIFI_TIMEOUT_MS  15000                // Maks waktu koneksi (ms)

// ------------------------------------------------------------
// Blockchain (Ganache via JSON-RPC HTTP)
// ------------------------------------------------------------
// Ganti IP dengan hasil `ipconfig` (IPv4) pada PC yang menjalankan Ganache
#define BLOCKCHAIN_RPC_URL   "http://<IP_PC>:7545"        // ← ganti IP
// Smart contract address — didapat dari Remix setelah deploy (EVM: Paris)
#define CONTRACT_ADDRESS     "0x<CONTRACT_ADDRESS>"        // ← ganti setelah deploy
// Private key akun Ganache baris pertama — lihat ikon kunci di tab Accounts (tanpa "0x")
#define SENDER_PRIVATE_KEY   "<GANACHE_PRIVATE_KEY>"       // ← ganti dari Ganache
// Address Ethereum akun baris pertama Ganache
#define SENDER_ADDRESS       "0x<GANACHE_ADDRESS>"         // ← ganti dari Ganache
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

// Register Modbus pada setiap slave — AGNIKA pulse totalizer
// Format: uint32, LSW di alamat rendah, MSW di alamat tinggi
// Contoh: forward pulse = ((reg[0x0001] << 16) | reg[0x0000])
// 1 pulse = 10 liter. Nilai kumulatif (selalu naik, tidak pernah turun).
#define REG_FORWARD_PULSE    0x0000      // Forward pulse uint32 (LSW 0x0000, MSW 0x0001)
#define REG_BACKWARD_PULSE   0x0002      // Backward pulse uint32 (LSW 0x0002, MSW 0x0003)
// TODO: REG_VALVE_STATUS — belum dikonfirmasi ada di AGNIKA. Jangan poll dulu.
// #define REG_VALVE_STATUS  0x0004

// Timeout respons slave (ms)
#define MODBUS_RESPONSE_TIMEOUT_MS  500

// ------------------------------------------------------------
// Deteksi anomali
// ------------------------------------------------------------
// Kenaikan forward pulse maks wajar dalam satu interval polling (kalibrasi).
// Default 1000 pulse = 10.000 liter/polling. Sesuaikan dengan debit maks pipa.
#define MAX_PULSE_DELTA      1000UL

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
