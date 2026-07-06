# Pengolahan Data — ESP32 Modbus Security Gateway

> Lanjutan dari [DEPLOYMENT.md](DEPLOYMENT.md) Bab E (Pengujian per Parameter).
> Jalankan bab ini setelah seluruh parameter (E.1–E.7) selesai diuji dan tiap file `.log` sudah tercatat.

## Olah Data Otomatis

Tujuan bab ini: mengubah file `.log` menjadi confusion matrix + metrik, otomatis tanpa hitung manual.

### 1. Install dependensi (sekali)

```powershell
pip install openpyxl
```

### 2. Parse tiap log (sekali per skenario)

```powershell
python tools/logger/parse_serial.py --log <log_A> --scenario A   --target 2 --expected ROGUE_ID
python tools/logger/parse_serial.py --log <log_B> --scenario B   --target 2 --expected VALUE_RANGE
python tools/logger/parse_serial.py --log <log_C> --scenario C   --target 1 --expected DEVICE_LOST
python tools/logger/parse_serial.py --log <log_D> --scenario D   --target 2 --expected NONE
python tools/logger/parse_serial.py --log <log_E> --scenario E   --target 0 --expected NONE
python tools/logger/parse_serial.py --log <log_S> --scenario SUB --target 2 --expected IDENTITY
```

Tiap perintah menambah satu baris ke `confusion.csv` dan mengisi `response_times.csv`.

> Huruf `A`, `B`, `C`, `D`, `E`, `SUB` di atas adalah **kode file log**, bukan nama parameter. Pemetaan kode file log → parameter (lihat [DEPLOYMENT.md](DEPLOYMENT.md) Bab E): A=ROGUE_ID, B=VALUE_RANGE, C=DEVICE_LOST, D=NONE/Peniruan Sempurna, E=NONE/Operasi Normal (FPR), SUB=IDENTITY. Tamper-Evidence (n=1 record) diuji terpisah lewat prosedur manual di Bab E.7 — tidak diparse lewat `parse_serial.py`.

### 3. Buat Excel + cetak metrik

```powershell
python tools/logger/buat_excel.py
```

Menghasilkan `hasil_pengujian.xlsx` (sheet: **Ringkasan**, **Metrik**, **Data Mentah**) dan mencetak confusion matrix + DR / FPR / Precision / Accuracy / F1-score ke terminal — angka untuk Tabel di Bab IV.

### 4. Ekspor log audit blockchain

```powershell
python tools/logger/export_anomali.py
```

Menghasilkan `anomali_log.csv` (waktu nyata, jenis, detail, txHash) — bukti audit immutable untuk lampiran Bab IV.

> 🔁 Untuk mengulang dari awal: hapus `confusion.csv` dan `response_times.csv`, lalu ulangi langkah 2.
