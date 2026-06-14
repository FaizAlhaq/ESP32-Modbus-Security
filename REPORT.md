# REPORT — Verifikasi, Konsolidasi & Dokumentasi Workspace

Tanggal: 2026-06-14
Ruang lingkup: rapikan struktur + lengkapi konfigurasi & dokumentasi.
**Logika deteksi firmware TIDAK diubah** (sesuai instruksi — sudah benar).

---

## 1. Status Verifikasi (read-only, tanpa perubahan logika)

### 1.1 `contracts/ModbusSecurity.sol`

| Item | Status | Lokasi |
|---|---|---|
| `mapping(uint8 => uint256) deviceUID` | ✅ ADA | [ModbusSecurity.sol:24](contracts/ModbusSecurity.sol:24) |
| `addDevice(uint8, uint256)` (2-arg) | ✅ ADA | [ModbusSecurity.sol:53](contracts/ModbusSecurity.sol:53) |
| `verifyDevice(uint8)` (cek whitelist) | ✅ ADA | [ModbusSecurity.sol:68](contracts/ModbusSecurity.sol:68) |
| `verifyDevice(uint8, uint256)` (cek UID) | ✅ ADA | [ModbusSecurity.sol:73](contracts/ModbusSecurity.sol:73) |

`addDevice` mengisi `whitelist[slaveId] = true` **dan** `deviceUID[slaveId] = uid`.
`verifyDevice(uint8,uint256)` mengembalikan `whitelist[slaveId] && deviceUID[slaveId] == uid`.
Dua `verifyDevice` adalah **overload yang disengaja** (cek ringan per-poll vs cek identitas).

### 1.2 Firmware `src/`

| Item | Status | Lokasi |
|---|---|---|
| `readUID(slaveId, outUid32[32])` di modbus_handler | ✅ ADA | [modbus_handler.h:48](src/modbus_handler.h:48), [modbus_handler.cpp:124](src/modbus_handler.cpp:124) |
| `verifyDevice(uint8, const uint8_t[32])` 2-arg | ✅ ADA | [blockchain_client.h:34](src/blockchain_client.h:34), [blockchain_client.cpp:98](src/blockchain_client.cpp:98) |
| Selector `0xd14cf946` untuk `verifyDevice(uint8,uint256)` | ✅ BENAR | [blockchain_client.cpp:22](src/blockchain_client.cpp:22) |
| `ANOMALY_IDENTITY = 5` | ✅ ADA | [blockchain_client.h:21](src/blockchain_client.h:21) |
| Cek identitas + cetak UID di main | ✅ ADA | [main.cpp:96-125](src/main.cpp:96) |

Detail alur identitas di [main.cpp](src/main.cpp): pada kontak pertama / reconnect
(`!isCurrentlyPresent`), firmware memanggil `readUID()`, mencetak
`[SEC] Slave N UID = 0x...` (12 byte bermakna, byte 20–31), lalu memanggil
`verifyDevice(slaveId, uid32)`. Jika gagal → anomali `ANOMALY_IDENTITY`.

### 1.3 `verifyDevice(uint8)` MASIH dipakai (JANGAN dihapus)

✅ Dikonfirmasi: `verifyDevice(uint8)` dipanggil oleh `checkWhitelist()` di
[security.cpp:200](src/security.cpp:200) (`_bc->verifyDevice(r.slave_id)`), dipakai
tiap poll sebagai lapisan ke-4. Dua overload `verifyDevice` memang disengaja dan
**keduanya dipertahankan**.

### 1.4 Verifikasi independen selector ABI (keccak256)

Seluruh selector di [blockchain_client.cpp:21-24](src/blockchain_client.cpp:21) dihitung
ulang via keccak256 dan **cocok semua**:

| Fungsi | Selector (dihitung) | Di firmware |
|---|---|---|
| `verifyDevice(uint8)` | `0xeca8e63d` | ✅ `SEL_VERIFY_DEVICE` |
| `verifyDevice(uint8,uint256)` | `0xd14cf946` | ✅ `SEL_VERIFY_DEVICE_UID` |
| `logTransaction(string,string)` | `0xd8628357` | ✅ `SEL_LOG_TRANSACTION` |
| `logAnomaly(uint8,uint8,string)` | `0x98bf92e5` | ✅ `SEL_LOG_ANOMALY` |

---

## 2. Perubahan yang Dibuat (struktur/config/docs — bukan logika)

1. **`platformio.ini`** — ditambahkan `monitor_filters = log2file`
   ([platformio.ini:16](platformio.ini:16)). Output serial kini otomatis tersimpan ke
   `.pio/build/esp32dev/monitor-*.log` (tidak perlu screenshot panjang).

2. **Konsolidasi tooling ke dalam repo** (tidak mengganggu build — PlatformIO hanya
   meng-compile `src/` + `lib/`):
   - `tools/attacker/attacker_slave.py` ← disalin dari folder Attacker eksternal.
   - `tools/logger/export_anomali.py` ← disalin dari folder Data eksternal.
   - Salinan **identik** dengan sumber (tidak ada perubahan kode pada skrip).

3. **`DEPLOYMENT.md`** — diperbarui menyeluruh:
   - Bagian **Persistensi Ganache** (workspace tersimpan, `CONTRACT_ADDRESS` tetap).
   - Bagian **Setelah PC restart**: pakai **"At Address"** di Remix untuk reconnect,
     **jangan deploy ulang**.
   - **Registrasi device** diperbaiki ke bentuk **2 argumen** `addDevice(slaveId, uid)`
     dengan UID dari serial ESP32 (sebelumnya masih contoh 1-argumen yang sudah usang).
   - Cara menjalankan **attacker** (`tools/attacker/...`, COM sebagai placeholder, mode
     normal/drop/jump, opsi `--uid` untuk uji evasif).
   - Cara menjalankan **data logger** (`tools/logger/...`, set `RPC` & `CONTRACT`).
   - **Alur uji singkat** end-to-end + tanda log serial (`[BC]`, `[SEC]`) yang benar
     (sebelumnya menyebut `[Blockchain]` yang tidak sesuai output aktual).

4. **`REPORT.md`** — dokumen ini.

> Tidak ada file di `src/` maupun `contracts/` yang diubah isinya. `contracts/artifacts/`
> tetap di-`.gitignore` (artefak hasil compile, tidak ikut commit).

---

## 3. Langkah Manual yang MASIH Harus Dilakukan User

Hal-hal berikut bergantung pada hardware/Ganache dan **tidak bisa diotomatiskan di sini**:

1. **Set `CONTRACT_ADDRESS` di `src/config.h`** ke alamat kontrak aktif.
   - Jika kontrak lama (workspace sama): pakai **At Address** di Remix, alamat tidak berubah.
   - Jika deploy baru: salin alamat baru dari Remix → tempel ke
     [config.h:21](src/config.h:21). Cek juga `BLOCKCHAIN_RPC_URL`
     ([config.h:19](src/config.h:19)) bila IP PC berubah.

2. **Reflash ESP32** setelah mengubah `config.h`:
   `%USERPROFILE%\.platformio\penv\Scripts\pio run -e esp32dev --target upload`

3. **Baca UID tiap slave** dari serial monitor — cari baris
   `[SEC] Slave N UID = 0x...` (24 hex).

4. **`addDevice(id, uid)` dengan DUA argumen** di Remix untuk tiap slave sah, mis.
   `addDevice(1, 0x0000000000000000000000AB)`. Memanggil dengan satu argumen akan ditolak.

5. **Set `RPC` & `CONTRACT` di `tools/logger/export_anomali.py`** sebelum export CSV.
   - ⚠️ Nilai `CONTRACT` pada salinan saat ini = `0xD501FBA17fc20de2aDb9491252E5c64E499B596D`,
     yang sebenarnya adalah **`SENDER_ADDRESS`**, **bukan** `CONTRACT_ADDRESS`. Harus
     diganti ke `CONTRACT_ADDRESS` (mis. `0x3eC770D542c28cf75daf4882ea1D97ddb6937660`)
     agar event `AnomalyLogged` terbaca.

6. **Install dependensi Python** di mesin masing-masing: `pip install pyserial` (attacker)
   dan `pip install web3` (logger).

7. **Tentukan `<COM_ATTACKER>`** (port RS485 PC attacker via Device Manager) — placeholder
   di dokumentasi, jangan tertukar dengan port ESP32 gateway.

---

## 4. Catatan Tambahan (tidak diubah, sekadar flag)

- **Peta `JENIS` di `export_anomali.py`** hanya memetakan tipe 1–4
  (`ROGUE_ID/WHITELIST`, `TIMING`, `VALUE_RANGE`, `DEVICE_LOST`). Tipe `0`
  (`NO_REQUEST`) dan `5` (`IDENTITY`) — yang valid menurut
  [blockchain_client.h:15-22](src/blockchain_client.h:15) — akan tampil sebagai angka
  mentah di CSV. Dibiarkan apa adanya (salinan identik); cukup diketahui saat membaca CSV.
- **WiFi & private key** di `src/config.h` berisi kredensial pengujian asli. Pertimbangkan
  memindahkannya ke `src/config_secret.h` (sudah ada di `.gitignore`) bila repo akan dibagikan.

---

## 5. Perubahan Lanjutan (instruksi TAMBAHAN)

Setiap perubahan di bawah langsung `git add` + `commit` + `push` ke
`origin/main` (sesuai instruksi, agar bisa diperiksa dari repo).

1. **Hapus unit test virtual** — seluruh `test/` dihapus
   (`test/test_security/test_main.cpp` + README boilerplate). Pengujian dilakukan
   langsung di hardware fisik. Catatan: test lama juga sudah **stale** (mengacu
   `REG_FLOW_RATE`/`FLOW_RATE_MIN`/`FLOW_RATE_MAX` yang tidak ada lagi setelah pindah ke
   peta register pulse totalizer AGNIKA). Setelan test di `platformio.ini`
   (`test_framework`, `test_build_src`, `lib_ignore`) dibiarkan — tidak mengganggu build.

2. **`tools/logger/hitung_metrik.py`** (baru) — baca `hasil_pengujian.csv` (satu baris =
   satu trial), tally kolom `Sel` (TP/FP/FN/TN) + `response(ms)`, lalu cetak confusion
   matrix + Detection Rate, FPR, Precision, Accuracy, F1, dan response time (mean & SD,
   sampel n-1). Pure Python (`csv` + `statistics`), toleran delimiter `,`/`;`, desimal
   koma, dan variasi nama header. Sudah di-smoke-test dengan data sintetis.
   > Skrip referensi tidak terlampir di sesi ini, jadi diimplementasikan sesuai spesifikasi
   > yang diberikan. Jika format `hasil_pengujian.csv` Anda berbeda, beri tahu untuk disesuaikan.

3. **`DEPLOYMENT.md`** — dua bagian baru: **"Pemulihan Setelah Mati Daya"** (workspace
   Ganache tersimpan → At Address di Remix tanpa deploy → ESP32 auto re-baseline + totalizer
   lanjut dari memori NV, tanpa `addDevice` ulang) dan **"Data yang Ditangkap per Slave"**
   (id, uid, forward, backward, accumulative; deteksi fokus pada IDENTITAS + INTEGRITAS
   NILAI, backward & accumulative = data proses pendukung).

### Langkah manual tambahan
- **Sediakan `hasil_pengujian.csv`** dengan minimal kolom `Sel` (isi TP/FP/FN/TN per trial)
  dan `response(ms)` agar `hitung_metrik.py` bisa menghitung. Jalankan:
  `python tools/logger/hitung_metrik.py hasil_pengujian.csv`.
