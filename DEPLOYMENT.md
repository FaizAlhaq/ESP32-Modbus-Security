# Panduan Deployment — ESP32 Modbus Security Gateway

Dokumen ini mencatat langkah deployment dari nol **dan** langkah singkat saat hanya
melanjutkan pengujian (PC sudah pernah di-setup). Ganache berjalan lokal, jadi
`CONTRACT_ADDRESS` ditentukan saat deploy — tetapi **selama workspace Ganache yang sama
dipakai, alamat itu tidak berubah** (lihat bagian Persistensi di bawah).

> **TL;DR alur uji singkat:**
> At Address / deploy → set `CONTRACT_ADDRESS` di `src/config.h` → reflash →
> baca UID dari serial → `addDevice(id, uid)` (DUA argumen) → jalankan attacker →
> rekam serial + export CSV. Detail lengkap di bagian [Alur Uji Singkat](#alur-uji-singkat-end-to-end).

---

## 0. Persistensi Ganache (workspace tersimpan)

Ganache Desktop menyimpan **workspace** (blockchain + akun + kontrak yang sudah
ter-deploy) ke disk. Selama Anda membuka **workspace yang sama**:

- Semua block, akun, private key, dan kontrak yang sudah di-deploy **tetap ada**.
- **`CONTRACT_ADDRESS` tidak berubah** — kontrak yang sudah di-deploy permanen di
  workspace tersebut.
- Daftar `addDevice(slaveId, uid)` yang sudah dikirim **tetap tersimpan** (tidak perlu
  daftar ulang).

> **Gunakan workspace, bukan QUICKSTART.** Quickstart membuat chain baru sekali pakai
> yang hilang saat Ganache ditutup. Buat **New Workspace → Ethereum**, beri nama, lalu
> **Save**. Mulai dari sini, tutup/buka Ganache tidak akan menghapus kontrak.

---

## A. Setelah PC Restart — JANGAN Deploy Ulang

Jika PC (atau Ganache) di-restart dan Anda membuka **workspace yang sama**, kontrak masih
ada di alamat yang sama. **Jangan deploy ulang** (deploy ulang = alamat baru = harus
update config + daftar ulang device).

Cara reconnect ke kontrak yang sudah ada di Remix:

1. Buka https://remix.ethereum.org
2. Pastikan file `contracts/ModbusSecurity.sol` ada & ter-compile (EVM Version **`paris`**).
3. Tab **Deploy & Run**:
   - Environment: **Custom - External Http Provider**, URL: `http://127.0.0.1:7545`
   - Pada bagian **"At Address"** (bukan tombol "Deploy" oranye), **tempel
     `CONTRACT_ADDRESS`** yang lama → klik **At Address** (tombol biru).
4. Kontrak muncul lagi di panel "Deployed Contracts", lengkap dengan state-nya
   (whitelist + UID yang sudah didaftarkan tetap ada).

Karena `CONTRACT_ADDRESS` tidak berubah, **`src/config.h` tidak perlu diubah** dan ESP32
**tidak perlu di-reflash** — cukup pastikan IP Ganache di `BLOCKCHAIN_RPC_URL` masih sama
(`ipconfig` → IPv4). Jika IP PC berubah, update hanya baris `BLOCKCHAIN_RPC_URL` lalu reflash.

---

## B. Setup Pertama Kali (atau Pindah PC / Deploy Baru)

Lakukan bagian ini **hanya** jika belum pernah deploy, atau sengaja deploy kontrak baru.

### B.1. Jalankan Ganache
- Download Ganache Desktop: https://trufflesuite.com/ganache/
- **New Workspace → Ethereum** (bukan Quickstart), beri nama, **Save**.
- Pastikan port: **7545**
- Catat **IP PC** (`ipconfig` → IPv4 Address)

### B.2. Deploy Kontrak via Remix IDE
- Buka https://remix.ethereum.org
- Buat file baru → copy isi `contracts/ModbusSecurity.sol`
- Tab **Solidity Compiler** → pilih `0.8.x`
  - > ⚠️ **Sebelum compile:** di Advanced Configuration, set **EVM Version ke `paris`**
    > (Ganache tidak mendukung opcode `PUSH0` dari Shanghai ke atas)
  - Klik **Compile**
- Tab **Deploy & Run**:
  - Environment: **Custom - External Http Provider**, URL: `http://127.0.0.1:7545`
  - Klik **Deploy** (oranye)
  - Salin **alamat kontrak** dari "Deployed Contracts" → ini `CONTRACT_ADDRESS` baru.

### B.3. Update `src/config.h`

| Field | Cara dapat |
|---|---|
| `BLOCKCHAIN_RPC_URL` | `ipconfig` → IPv4, port 7545 |
| `CONTRACT_ADDRESS` | Remix → Deployed Contracts → copy address |
| `SENDER_PRIVATE_KEY` | Ganache → tab Accounts → ikon kunci (tanpa `0x`) |
| `SENDER_ADDRESS` | Ganache → tab Accounts → address baris pertama |

```cpp
#define BLOCKCHAIN_RPC_URL   "http://<IP_PC>:7545"
#define CONTRACT_ADDRESS     "<dari Remix>"
#define SENDER_PRIVATE_KEY   "<dari Ganache, tanpa 0x>"
#define SENDER_ADDRESS       "<dari Ganache>"
```

### B.4. Build & Flash

> **Tip:** Buka terminal VS Code dengan shortcut **Esc + `** (backtick)

```powershell
%USERPROFILE%\.platformio\penv\Scripts\pio run -e esp32dev --target upload
```

---

## C. Registrasi Perangkat — `addDevice` WAJIB DUA Argumen

> ⚠️ **PENTING:** Kontrak sekarang memakai identitas berbasis UID. Fungsi
> **`addDevice(uint8 slaveId, uint256 uid)` membutuhkan DUA argumen**. Memanggil dengan
> satu argumen (seperti versi lama) **tidak akan meng-compile / akan ditolak Remix**.

UID hardware tiap slave dibaca otomatis oleh ESP32 dari 6 register AGNIKA mulai `0x000D`
(96-bit, big-endian) dan dicetak ke serial saat kontak pertama / reconnect:

```
[SEC] Slave 1 UID = 0x0000000000000000000000AB
```

Ambil 24-hex itu (12 byte bermakna) dan daftarkan di Remix → panel "Deployed Contracts"
→ fungsi `addDevice`:

```
addDevice(1, 0x0000000000000000000000AB)   → transact
addDevice(2, 0x........................)   → transact
...
addDevice(5, 0x........................)   → transact
```

- Argumen pertama = `slaveId` (1–5).
- Argumen kedua = `uid` dalam hex `0x...` persis seperti yang dicetak ESP32.
- Setelah ini, ESP32 yang mem-poll slave tsb akan melihat
  `[BC] verifyDevice(N, UID) → OK` dan `[SEC] identitas slave N terverifikasi (UID cocok)`.

> Slave yang **belum** didaftarkan (atau UID-nya salah) akan memicu anomali
> `IDENTITY` (jenis 5) saat kontak pertama, lalu anomali `ROGUE_ID/WHITELIST`
> pada pengecekan whitelist per-poll.

---

## D. Menjalankan Attacker (slave Modbus palsu)

Skrip ada di `tools/attacker/attacker_slave.py`. Butuh **pyserial**:

```powershell
pip install pyserial
```

`<COM_ATTACKER>` adalah port USB-RS485 yang dipakai PC attacker (placeholder — cek di
Device Manager, mis. `COM7`). Jangan tertukar dengan port ESP32 gateway.

```powershell
python tools/attacker/attacker_slave.py --port <COM_ATTACKER> --id 2 --mode normal --base 1000
python tools/attacker/attacker_slave.py --port <COM_ATTACKER> --id 2 --mode drop   --base 1000
python tools/attacker/attacker_slave.py --port <COM_ATTACKER> --id 2 --mode jump   --base 1000
```

Opsi:

| Opsi | Arti |
|---|---|
| `--port` | COM port RS485 attacker (placeholder, sesuaikan) |
| `--id` | ID slave yang dipalsukan (mis. `2`) |
| `--mode normal` | Forward pulse monoton naik → **lolos** deteksi nilai (uji evasif) |
| `--mode drop` | Forward pulse turun → memicu anomali `VALUE_RANGE` |
| `--mode jump` | Lonjakan pulse besar → memicu anomali `VALUE_RANGE` (delta > batas) |
| `--base` | Nilai forward pulse awal |
| `--uid 0x<UID>` | Jawab register UID `0x000D` dengan UID tertentu (uji evasif identitas) |

**Uji evasif identitas:** untuk meniru slave sah, tambahkan UID asli yang sudah
didaftarkan di kontrak:

```powershell
python tools/attacker/attacker_slave.py --port <COM_ATTACKER> --id 2 --mode normal --base 1000 --uid 0x0000000000000000000000AB
```

Tanpa `--uid` (default semua nol), slave palsu akan gagal verifikasi identitas dan
memicu anomali `IDENTITY`.

---

## E. Menjalankan Data Logger (export anomali → CSV)

Skrip ada di `tools/logger/export_anomali.py`. Butuh **web3**:

```powershell
pip install web3
```

**Sebelum dijalankan, set dua variabel di dalam file** agar menunjuk ke Ganache &
kontrak yang benar:

```python
RPC      = "http://<IP_PC>:7545"     # samakan dengan BLOCKCHAIN_RPC_URL di config.h
CONTRACT = "0x<CONTRACT_ADDRESS>"    # samakan dengan CONTRACT_ADDRESS di config.h (BUKAN SENDER_ADDRESS)
```

> ⚠️ Nilai `CONTRACT` pada salinan saat ini masih placeholder dan **harus** diganti ke
> `CONTRACT_ADDRESS` (alamat kontrak hasil deploy), bukan alamat akun pengirim.

Jalankan:

```powershell
python tools/logger/export_anomali.py
```

Menghasilkan `anomali_log.csv` (kolom: `no, waktu, block, slaveId, jenis, detail, txHash`)
yang bisa langsung dibuka di Excel.

> Catatan: peta `JENIS` di skrip memetakan tipe 1–4 (`ROGUE_ID/WHITELIST`, `TIMING`,
> `VALUE_RANGE`, `DEVICE_LOST`). Tipe `0` (`NO_REQUEST`) dan `5` (`IDENTITY`) akan tampil
> sebagai angka mentah di kolom `jenis` — cocok dengan `enum AnomalyType` di
> `src/blockchain_client.h`.

---

## Alur Uji Singkat (end-to-end)

1. **At Address** (kontrak lama, lihat bagian A) **atau Deploy** (kontrak baru, bagian B).
2. Set `CONTRACT_ADDRESS` (dan `BLOCKCHAIN_RPC_URL` bila IP berubah) di `src/config.h`.
3. **Reflash** ESP32:
   `%USERPROFILE%\.platformio\penv\Scripts\pio run -e esp32dev --target upload`
4. **Baca UID** dari serial monitor — cari baris `[SEC] Slave N UID = 0x...`.
5. **`addDevice(id, uid)`** di Remix untuk tiap slave sah (DUA argumen).
6. **Jalankan attacker** pada slave target (bagian D) untuk membangkitkan anomali.
7. **Rekam serial** (otomatis tersimpan ke file `.log`, lihat bagian Serial di bawah)
   **+ export CSV** (`python tools/logger/export_anomali.py`).

### Verifikasi koneksi & log serial

Pantau output ESP32:
```powershell
%USERPROFILE%\.platformio\penv\Scripts\pio device monitor -e esp32dev
```

Berkat `monitor_filters = log2file` di `platformio.ini`, semua output serial **otomatis
tersimpan** ke `.pio/build/esp32dev/monitor-*.log` (tanpa perlu screenshot panjang).

Tanda koneksi sehat:
- `[BC] BlockchainClient siap, RPC: http://...`
- `[BC] verifyDevice(N) → OK` / `[BC] verifyDevice(N, UID) → OK`
- `[SEC] identitas slave N terverifikasi (UID cocok)`

Jika muncul `[BC] NODE BLOCKCHAIN TIDAK TERJANGKAU` atau `Connection refused`: periksa
IP Ganache di `config.h`, pastikan Ganache jalan di port 7545, dan firewall tidak
memblokir port tersebut.

### Jalankan Unit Test (opsional)
```powershell
%USERPROFILE%\.platformio\penv\Scripts\pio test -e esp32dev
```

---

## Selector ABI Kontrak (tetap sama, tidak perlu diubah)

Selector dihitung dari signature fungsi dan sudah benar di `src/blockchain_client.cpp`
(diverifikasi ulang via keccak256):

| Fungsi | Selector |
|---|---|
| `verifyDevice(uint8)` | `0xeca8e63d` |
| `verifyDevice(uint8,uint256)` | `0xd14cf946` |
| `logTransaction(string,string)` | `0xd8628357` |
| `logAnomaly(uint8,uint8,string)` | `0x98bf92e5` |

---

## Nilai Terakhir (device sebelumnya — referensi, sesuaikan dengan workspace Anda)

| Field | Nilai (saat ini di `src/config.h`) |
|---|---|
| `BLOCKCHAIN_RPC_URL` | `http://192.168.0.100:7545` |
| `CONTRACT_ADDRESS` | `0x3eC770D542c28cf75daf4882ea1D97ddb6937660` |
| `SENDER_ADDRESS` | `0xD501FBA17fc20de2aDb9491252E5c64E499B596D` |

> **Catatan:** Nilai di atas hanya berlaku selama Ganache membuka **workspace yang sama**
> dengan IP yang sama. Selama itu, `CONTRACT_ADDRESS` tetap (lihat bagian Persistensi).
> Deploy kontrak baru = `CONTRACT_ADDRESS` baru = update `config.h` + reflash + daftar ulang.
