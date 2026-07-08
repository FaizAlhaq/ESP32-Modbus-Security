# Definisi Sistem — TERKUNCI

> Jangan ubah tanpa meninjau ulang bersama pembimbing/penguji.
> Berlaku untuk seluruh kode, dokumen, dan skenario pengujian.

## Prinsip Fondasi

ESP32 **mendeteksi** anomali secara lokal di firmware.
Blockchain **hanya mencatat** secara tamper-evident.
Jangan pernah tulis "blockchain mendeteksi".

## Peta Arsitektur Fisik

![Peta arsitektur fisik: PC admin (Ganache + Remix) via WiFi ke ESP32 gateway; bus RS485 ke Slave ID 1 (referensi permanen) dan Slave ID 2 (target impersonation); PC penyerang tersambung ke bus lewat USB-RS485](assets/arsitektur-fisik.svg)

## Field yang Ditangkap per Slave

| Field | Sumber | Peran |
|---|---|---|
| `id` | Modbus slave address | Identitas — cek whitelist on-chain tiap poll |
| `uid` | 6 register @0x000D (96-bit) | Identitas — cek verifyDevice(id,uid) saat kontak pertama/reconnect |
| `forward` | uint32 @0x0000–0x0001 | Integritas — wajib monoton naik, delta ≤ 1000 |
| `backward` | uint32 @0x0002–0x0003 | Data proses pendukung (tercatat, bukan sinyal deteksi) |
| `accumulative` | derived | Data proses pendukung (tercatat, bukan sinyal deteksi) |

## Jenis Anomali

4 kelas final yang diuji dan dilaporkan di confusion matrix:

| Tipe | Nama | Keterangan |
|---|---|---|
| 1 | ROGUE_ID | ID tidak ada di whitelist blockchain |
| 3 | VALUE_RANGE | Forward pulse turun atau delta > 1000 |
| 4 | DEVICE_LOST | Slave pernah hadir, kini tidak merespons |
| 5 | IDENTITY | UID tidak cocok atau belum terdaftar on-chain |

Dua nilai enum lain di kode (`blockchain_client.h`) TIDAK dihitung sebagai Tipe independen:
- **0 — NO_REQUEST**: tidak diuji, di luar scope model polling.
- **2 — TIMING**: bukan Tipe independen. Ini mekanisme di dalam DEVICE_LOST — respons yang melewati window 600 ms (`RESPONSE_WINDOW_MS`, `security.cpp::checkTiming()`) adalah tanda slave mulai tidak sehat sebelum akhirnya benar-benar hilang. Nilai enum tetap ada di kode untuk kompatibilitas, tapi secara metodologis/confusion-matrix dilaporkan di bawah DEVICE_LOST, bukan Tipe terpisah.

Tabel ini harus sinkron dengan: kode (`blockchain_client.h`) ↔ dokumen skripsi ↔
confusion matrix pengujian.

Urutan gerbang deteksi keempat kelas anomali di firmware — dari `pollAndCheck()` (`main.cpp`) lalu `checkPollResult()` (`security.cpp`):

![Alur keputusan deteksi anomali: cek respons (DEVICE_LOST), cek UID saat reconnect (IDENTITY), cek nilai forward (VALUE_RANGE), cek whitelist (ROGUE_ID), lalu transaksi valid ke Ganache](assets/alur-deteksi.svg)

## Scope Pengujian (Proof-of-Concept)

| Aspek | Nilai |
|---|---|
| Slave fisik aktif | ID 1 (referensi dasar, selalu tersambung) dan ID 2 (sensor kedua; identitas yang sama juga digunakan penyerang untuk "berbaur" pada skenario spoofing/impersonation — bukan ID asing terpisah). Keduanya sensor-only, tanpa aktuator. (1 lantai) |
| Jaringan | WiFi (pengganti Ethernet) |
| Blockchain | Ganache 1-node lokal (instant mining = batas bawah response time) |
| Sniffing pasif | Di luar scope — gateway hanya evaluasi respons atas poll-nya sendiri |
| Peniruan sempurna | FN by design — future work kriptografi |

## Kriteria Pengujian (Jumlah Sampel)

Kriteria keberhasilan pengujian per parameter dinyatakan sebagai **jumlah sampel (n)**, bukan durasi waktu:

| Parameter | Kriteria n |
|---|---|
| ROGUE_ID | Polling kontinu otomatis — n besar, tidak perlu target eksplisit (n mengikuti durasi proses berjalan) |
| VALUE_RANGE | Polling kontinu otomatis — n besar, tidak perlu target eksplisit |
| DEVICE_LOST | WAJIB eksplisit n ≥ 10 siklus (cabut-pasang kabel berulang) — hanya terpicu saat transisi status, bukan tiap poll |
| IDENTITY | WAJIB eksplisit n ≥ 10 siklus (reconnect berulang) — hanya terpicu saat transisi status, bukan tiap poll |
| Tamper-Evidence | n = 1 record — prosedural (edit-and-compare), bukan berbasis n/durasi |

Detail langkah teknis ada di DEPLOYMENT.md Bab E (Pengujian per Parameter).

## Batasan Deteksi (Tidak Boleh Diklaim Terdeteksi)

- Frame RS485 dari ID yang tidak pernah di-poll
- Respons tanpa permintaan (unsolicited response)
- Sniffing (penyadapan pasif)
- Peniruan sempurna (attacker kloning UID + nilai wajar)

## Keterbatasan Tambahan (Terverifikasi Kode)

- **Blockchain unreachable → data hilang diam-diam, bukan diretry.** `Security::checkWhitelist()` (`security.cpp:197-212`) memang fail-closed (anomali ROGUE_ID jika BC tidak terjangkau). Tapi untuk logging: `Logger::reportAnomaly()` (`logger.cpp:49-56`) hanya menambah counter `_pendingAnomaly` saat BC down — detail anomali tidak disimpan untuk dikirim ulang. `Logger::flush()` (`logger.cpp:90-97`) membuang seluruh buffer transaksi valid (`_count = 0`) saat BC tidak terjangkau. Tidak ada retry maupun penyimpanan non-volatile di `logger.cpp`/`blockchain_client.cpp`; saat BC pulih, sistem hanya mencetak peringatan jumlah yang hilang, bukan mengirim ulang.
- **Restart total mereset status kehadiran tanpa peringatan.** `Security::begin()` (`security.cpp:21-30`) di-panggil tiap `setup()` (`main.cpp:155`), me-`memset` `_wasPresent` dan `_currentlyPresent` ke `false` untuk semua slave. Akibatnya: kegagalan poll pada siklus pertama pasca-boot TIDAK memicu DEVICE_LOST (karena `wasPresent()` baru true setelah `markPresent()` dipanggil sekali — `main.cpp:73,127`), dan setiap slave otomatis masuk ulang ke jalur verifikasi UID/TOFU (`main.cpp:96`) tanpa ada log eksplisit bahwa gateway baru saja restart. Reboot yang bertepatan dengan upaya impersonasi akan terlihat identik dengan kontak pertama yang sah.

## Future Work (TERKUNCI)

Pengembangan lanjutan dibatasi pada DUA arah saja:

1. **Autentikasi kriptografis (HMAC / ECDSA) pada frame Modbus**
   - Lapisan pencegahan yang menutup batas Skenario D (perfect impersonation).

2. **Blockchain multi-node lokal (beberapa node pada PC)**
   - Mewujudkan immutability terdistribusi, menggantikan Ganache node-tunggal.

DI LUAR future work (JANGAN dicantumkan):
- Topologi multi-gateway (5 lantai / 25 sensor) — terlalu jauh.
- Fokus tetap pada penyempurnaan SATU gateway.

> Alasan: multi-gateway itu kuat secara metodologis (menambah gateway tidak
> menyentuh mekanisme yang dibuktikan: deteksi + tamper-evidence). Ia hanya
> replikasi topologi — bukan kontribusi ilmiah. Membatasinya membuat future
> work fokus dan bisa dipertahankan, bukan daftar keinginan.

## Prosedur Jika Ada Perubahan

Setiap perubahan yang menyentuh isi file ini wajib:
1. Ditinjau ulang bersama pembimbing/penguji sebelum diimplementasi
2. Diperbarui serentak di file ini, DEPLOYMENT.md, dan dokumen skripsi
3. Di-commit dengan pesan yang menyebut `LOCKED_DEFINITION update`
