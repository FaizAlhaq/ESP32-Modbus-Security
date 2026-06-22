# LOCKED_DEFINITION.md
# Definisi Sistem — TERKUNCI
# Jangan ubah tanpa meninjau ulang bersama pembimbing/penguji.
# Berlaku untuk seluruh kode, dokumen, dan skenario pengujian.

## Prinsip Fondasi (TIDAK BOLEH DILANGGAR)
- **ESP32 MENDETEKSI** anomali — di firmware, secara lokal
- **Blockchain HANYA MENCATAT** secara tamper-evident
- → Jangan pernah tulis "blockchain mendeteksi"

## Field yang Ditangkap per Slave

| Field | Sumber | Peran |
|---|---|---|
| `id` | Modbus slave address | Identitas — cek whitelist on-chain tiap poll |
| `uid` | 6 register @0x000D (96-bit) | Identitas — cek verifyDevice(id,uid) saat kontak pertama/reconnect |
| `forward` | uint32 @0x0000–0x0001 | Integritas — wajib monoton naik, delta ≤ 1000 |
| `backward` | uint32 @0x0002–0x0003 | Data proses pendukung (tercatat, bukan sinyal deteksi) |
| `accumulative` | derived | Data proses pendukung (tercatat, bukan sinyal deteksi) |

## 5 Jenis Anomali (sinkron: kode ↔ dokumen ↔ confusion matrix)

| Tipe | Nama | Keterangan |
|---|---|---|
| 0 | NO_REQUEST | Tidak diuji — di luar scope model polling |
| 1 | ROGUE_ID | ID tidak ada di whitelist blockchain |
| 2 | TIMING | Respons di luar window 600 ms |
| 3 | VALUE_RANGE | Forward pulse turun atau delta > 1000 |
| 4 | DEVICE_LOST | Slave pernah hadir, kini tidak merespons |
| 5 | IDENTITY | UID tidak cocok atau belum terdaftar on-chain |

## Scope Pengujian (Proof-of-Concept)
- 1 lantai, 2 slave fisik aktif (ID 1 dan ID 4)
- WiFi sebagai pengganti Ethernet
- Ganache 1-node lokal (instant mining = batas bawah response time)
- Sniffing pasif = di luar scope (gateway hanya evaluasi respons atas poll-nya sendiri)
- Peniruan sempurna dengan kloning UID = FN by design → future work kriptografi

## Batasan yang Tidak Boleh Diklaim Terdeteksi
- Frame RS485 dari ID yang tidak pernah di-poll
- Respons tanpa permintaan (unsolicited response)
- Sniffing (penyadapan pasif)
- Peniruan sempurna (attacker kloning UID + nilai wajar)

## Jika Ada Perubahan
Setiap perubahan yang menyentuh tabel di atas wajib:
1. Ditinjau ulang bersama pembimbing/penguji sebelum diimplementasi
2. Diperbarui di file ini, DEPLOYMENT.md, dan dokumen skripsi secara bersamaan
3. Di-commit dengan pesan yang menyebut "LOCKED_DEFINITION update"
