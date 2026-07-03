# Definisi Sistem — TERKUNCI

> Jangan ubah tanpa meninjau ulang bersama pembimbing/penguji.
> Berlaku untuk seluruh kode, dokumen, dan skenario pengujian.

## Prinsip Fondasi

ESP32 **mendeteksi** anomali secara lokal di firmware.
Blockchain **hanya mencatat** secara tamper-evident.
Jangan pernah tulis "blockchain mendeteksi".

## Field yang Ditangkap per Slave

| Field | Sumber | Peran |
|---|---|---|
| `id` | Modbus slave address | Identitas — cek whitelist on-chain tiap poll |
| `uid` | 6 register @0x000D (96-bit) | Identitas — cek verifyDevice(id,uid) saat kontak pertama/reconnect |
| `forward` | uint32 @0x0000–0x0001 | Integritas — wajib monoton naik, delta ≤ 1000 |
| `backward` | uint32 @0x0002–0x0003 | Data proses pendukung (tercatat, bukan sinyal deteksi) |
| `accumulative` | derived | Data proses pendukung (tercatat, bukan sinyal deteksi) |

## Jenis Anomali

| Tipe | Nama | Keterangan |
|---|---|---|
| 0 | NO_REQUEST | Tidak diuji — di luar scope model polling |
| 1 | ROGUE_ID | ID tidak ada di whitelist blockchain |
| 2 | TIMING | Respons di luar window 600 ms |
| 3 | VALUE_RANGE | Forward pulse turun atau delta > 1000 |
| 4 | DEVICE_LOST | Slave pernah hadir, kini tidak merespons |
| 5 | IDENTITY | UID tidak cocok atau belum terdaftar on-chain |

Tabel ini harus sinkron dengan: kode (`blockchain_client.h`) ↔ dokumen skripsi ↔
confusion matrix pengujian.

## Scope Pengujian (Proof-of-Concept)

| Aspek | Nilai |
|---|---|
| Slave fisik aktif | ID 1 (referensi dasar, selalu tersambung) dan ID 2 (sensor kedua; identitas yang sama juga digunakan penyerang untuk "berbaur" pada skenario spoofing/impersonation — bukan ID asing terpisah). Keduanya sensor-only, tanpa aktuator. (1 lantai) |
| Jaringan | WiFi (pengganti Ethernet) |
| Blockchain | Ganache 1-node lokal (instant mining = batas bawah response time) |
| Sniffing pasif | Di luar scope — gateway hanya evaluasi respons atas poll-nya sendiri |
| Peniruan sempurna | FN by design — future work kriptografi |

## Batasan Deteksi (Tidak Boleh Diklaim Terdeteksi)

- Frame RS485 dari ID yang tidak pernah di-poll
- Respons tanpa permintaan (unsolicited response)
- Sniffing (penyadapan pasif)
- Peniruan sempurna (attacker kloning UID + nilai wajar)

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
