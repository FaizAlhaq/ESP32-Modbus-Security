# Panduan Deployment — ESP32 Modbus Security Gateway

Dokumen ini mencatat langkah deployment yang harus diulang setiap berpindah device,
karena Ganache berjalan secara lokal dan CONTRACT_ADDRESS bisa berbeda tiap deploy.

---

## Yang Perlu Diulang di Device Baru

### 1. Jalankan Ganache
- Download Ganache Desktop: https://trufflesuite.com/ganache/
- Buat workspace baru → Ethereum
- Pastikan port: **7545**
- Catat **IP PC** (`ipconfig` → IPv4 Address)

### 2. Deploy Kontrak via Remix IDE
- Buka https://remix.ethereum.org
- Buat file baru → copy isi `contracts/ModbusSecurity.sol`
- Tab **Solidity Compiler** → pilih `0.8.x`
  - > ⚠️ **Sebelum compile:** di bagian Advanced Configuration, set **EVM Version ke `paris`**
    > (Ganache tidak support opcode `PUSH0` dari Shanghai ke atas)
  - Klik **Compile**
- Tab **Deploy & Run**:
  - Environment: **Custom - External Http Provider**
  - URL: `http://127.0.0.1:7545`
  - Klik **Deploy**

### 3. Daftarkan Slave ke Whitelist
Di Remix panel "Deployed Contracts" → fungsi `addDevice`:
- Input `1` → transact
- Input `2` → transact
- Input `3` → transact
- Input `4` → transact
- Input `5` → transact

### 4. Catat Nilai Baru & Update config.h

| Field | Cara dapat |
|---|---|
| `BLOCKCHAIN_RPC_URL` | `ipconfig` → IPv4, port 7545 |
| `CONTRACT_ADDRESS` | Remix → Deployed Contracts → copy address |
| `SENDER_PRIVATE_KEY` | Ganache → tab Accounts → ikon kunci |
| `SENDER_ADDRESS` | Ganache → tab Accounts → address baris pertama |

Edit `src/config.h`:
```cpp
#define BLOCKCHAIN_RPC_URL   "http://<IP_PC>:7545"
#define CONTRACT_ADDRESS     "<dari Remix>"
#define SENDER_PRIVATE_KEY   "<dari Ganache, tanpa 0x>"
#define SENDER_ADDRESS       "<dari Ganache>"
```

### 5. Build & Flash

> **Tip:** Buka terminal VS Code dengan shortcut **Esc + `** (backtick)

Gunakan path PlatformIO lengkap agar tidak bergantung pada PATH sistem:
```powershell
%USERPROFILE%\.platformio\penv\Scripts\pio run -e esp32dev --target upload
```

### 6. Jalankan Unit Test
```powershell
%USERPROFILE%\.platformio\penv\Scripts\pio test -e esp32dev
```

### 7. Verifikasi Koneksi via Serial Monitor

Setelah flash, pantau output ESP32 untuk memastikan koneksi blockchain berhasil:
```powershell
%USERPROFILE%\.platformio\penv\Scripts\pio device monitor -e esp32dev
```
Cari log seperti `[Blockchain] verifyDevice OK` atau `[Blockchain] Connected`. Jika muncul `Connection refused`, periksa IP Ganache dan pastikan firewall tidak memblokir port 7545.

---

## Nilai Terakhir (device sebelumnya — tidak berlaku di device lain)

| Field | Nilai |
|---|---|
| `BLOCKCHAIN_RPC_URL` | `http://10.60.47.82:7545` |
| `CONTRACT_ADDRESS` | `0x01F354872A49D65665648FC5fbc95dcF2eb7fd7a` |
| `SENDER_ADDRESS` | `0x1941Ae81ecfe7907f95C88b2B35aD11b92547751` |

> **Catatan:** Nilai di atas hanya berlaku selama Ganache berjalan di PC yang sama
> dengan IP yang sama. Deploy ulang = CONTRACT_ADDRESS baru.

---

## Selector ABI Kontrak (tetap sama, tidak perlu diubah)

Selector dihitung dari nama fungsi dan sudah benar di `blockchain_client.cpp`:

| Fungsi | Selector |
|---|---|
| `verifyDevice(uint8)` | `0xeca8e63d` |
| `logTransaction(string,string)` | `0xd8628357` |
| `logAnomaly(uint8,uint8,string)` | `0x98bf92e5` |
