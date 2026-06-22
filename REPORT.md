# REPORT — Status Sistem (diperbarui otomatis)

## Status Komponen

| Komponen | Status | Catatan |
|---|---|---|
| `contracts/ModbusSecurity.sol` | ✅ Siap | `addDevice(uint8,uint256)`, `verifyDevice` dua overload, `deviceUID` mapping |
| `src/blockchain_client` | ✅ Siap | Selector ABI terverifikasi via keccak256, dua overload `verifyDevice` disengaja |
| `src/security` + `src/main` | ✅ Siap | `readUID`, cek identitas kontak pertama/reconnect, `ANOMALY_IDENTITY = 5` |
| `tools/attacker/attacker_slave.py` | ✅ Siap | Mode normal/drop/jump, opsi `--uid` untuk evasif |
| `tools/logger/export_anomali.py` | ✅ Siap | CONTRACT = `0x3eC770D542c28cf75daf4882ea1D97ddb6937660`, JENIS 0–5 |
| `tools/logger/hitung_metrik.py` | ✅ Siap | Baca `hasil_pengujian.csv`, hitung confusion matrix + DR/FPR/Precision/Accuracy/F1 |
| `tools/logger/hasil_pengujian.csv` | ⏳ Kosong | Diisi setelah pengujian Skenario A–E |
| `platformio.ini` `monitor_filters` | ✅ Aktif | Serial otomatis ke `.pio/build/esp32dev/monitor-*.log` |
| `test/` | ✅ Dihapus | Validasi via hardware fisik (Skenario A–E), bukan unit test virtual |
| `LOCKED_DEFINITION.md` | ✅ Ada | Definisi sistem terkunci — jangan ubah tanpa tinjauan ulang |

## Selector ABI (diverifikasi, tidak perlu diubah)

| Fungsi | Selector |
|---|---|
| `verifyDevice(uint8)` | `0xeca8e63d` |
| `verifyDevice(uint8,uint256)` | `0xd14cf946` |
| `logTransaction(string,string)` | `0xd8628357` |
| `logAnomaly(uint8,uint8,string)` | `0x98bf92e5` |

## Yang Masih Harus Dilakukan (manual)

| # | Tugas | Keterangan |
|---|---|---|
| 1 | `addDevice(id, uid)` di Remix | DUA argumen — UID dari serial `[SEC] Slave N UID = 0x...` |
| 2 | Isi `hasil_pengujian.csv` | Satu baris per trial, kolom `Sel` = TP/FP/FN/TN |
| 3 | Jalankan `hitung_metrik.py` | Setelah CSV terisi → metrik otomatis keluar |
| 4 | Jalankan `export_anomali.py` | Setelah pengujian → `anomali_log.csv` untuk lampiran |
| 5 | Tulis Bab IV & V skripsi | Isi `[ISI: ...]` dari data pengujian |

> Detail langkah teknis ada di [DEPLOYMENT.md](DEPLOYMENT.md).
> Definisi sistem yang terkunci ada di [LOCKED_DEFINITION.md](LOCKED_DEFINITION.md).
