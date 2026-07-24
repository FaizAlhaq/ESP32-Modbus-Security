# REPORT — Rangkuman Seluruh Perubahan Sesi Ini

> Dokumen ini merangkum **15 commit** yang dibuat sepanjang sesi kerja ini, dari
> `6776e75` (konsolidasi ID slave) sampai `38b9420` (perbaikan visibilitas error
> blockchain). Diorganisir per tema, bukan kronologis, agar mudah dipakai sebagai
> bahan evaluasi Bab IV/V. Setiap perubahan kode disertai file:baris kondisi
> **saat ini** (bukan snapshot commit), diverifikasi ulang saat menulis laporan ini.

---

## Daftar Commit (kronologis)

| # | Hash | Pesan |
|---|---|---|
| 1 | `6776e75` | fix: consolidate physical slaves to ID 1 & 2, remove ID 4 references |
| 2 | `7f191b6` | docs: finalize ID 2 registration reconciliation as idempotent, not redundant |
| 3 | `c3104ae` | fix: correct ABI-encoding for logTransaction/logAnomaly on-chain calls |
| 4 | `d93b055` | chore: remove unused valve register code from config.h |
| 5 | `2a06387` | docs: restructure testing docs to match Persiapan Awal → Pengolahan Data flow |
| 6 | `fda2cb2` | docs: add architecture/flow diagrams and unify chapter formatting |
| 7 | `ace1b33` | fix: reroute slow-response classification from TIMING to DEVICE_LOST |
| 8 | `3d8de3c` | feat: IDENTITY persisten blok tiap poll; chore: untrack tools/ |
| 9 | `c215472` | feat: block transaction on IDENTITY failure; align docs to per-poll IDENTITY |
| 10 | `e8f1462` | fix: ROGUE_ID tidak memicu IDENTITY; feat: gerbang backward pulse pada VALUE_RANGE |
| 11 | `9ae7724` | fix: baseline backward dikunci per sesi agar aliran balik lambat terdeteksi |
| 12 | `c4e462b` | fix: presedensi lapisan perangkat (ROGUE_ID/IDENTITY) di atas VALUE_RANGE |
| 13 | `acf4d84` | fix: re-atestasi identitas berkala agar pergantian UID mid-sesi terdeteksi |
| 14 | `76da8de` | chore: sinkronkan config lokal (RPC/kontrak) + catatan diagnosis konektivitas |
| 15 | `38b9420` | fix: kegagalan koneksi & error JSON-RPC kini terlihat; perbaiki log logTransaction |

---

## Tema 1 — Identitas Slave Fisik (ID 1 & 2)

**Commit:** `6776e75`, `7f191b6`

Repo sebelumnya menyebut 2 slave fisik sebagai ID 1 dan ID 4 di berbagai dokumen, padahal
hardware sebenarnya ID 1 dan ID 2. Diselaraskan di:
- `src/config.h:43-44` — `SLAVE_COUNT=2`, `SLAVE_IDS={1,2}`
- `LOCKED_DEFINITION.md`, `DEPLOYMENT.md` — semua rujukan "ID 4" diganti "ID 2", termasuk
  contoh log serial dan langkah `addDevice()`.
- Ditemukan tumpang-tindih prosedural: Bagian B (registrasi permanen ID 2) vs Skenario A/B
  (yang mencabut lalu mendaftarkan ulang ID 2). Diselesaikan dengan catatan bahwa
  `addDevice()` di kontrak bersifat idempotent (`contracts/ModbusSecurity.sol:53-57`), jadi
  langkah re-registrasi aman dipertahankan.

---

## Tema 2 — Bug Kritis: ABI-Encoding `logTransaction`/`logAnomaly`

**Commit:** `c3104ae`

**Temuan:** `logTransaction()` dan `logAnomaly()` di `blockchain_client.cpp` mengirim
calldata **tanpa ABI-encoding argumen `string` yang benar** — hanya selector 4-byte, tanpa
offset/length/padding. Diverifikasi empiris lewat sandbox Ganache-kompatibel
(`tools/diagnostics/sandbox_replay.mjs`): setiap panggilan **revert** (status=0), meski
firmware selalu mencetak seolah sukses (`postJson()` hanya cek HTTP 200, tidak cek
`receipt.status`).

**Perbaikan** (`blockchain_client.cpp:41-54` — `abiEncodeDynamicString()`, dan
`blockchain_client.cpp:172-201`, `208-235`): implementasi ABI-encoding penuh
(offset+length+data padded 32-byte) untuk kedua fungsi. Diverifikasi ulang dengan metodologi
sama: calldata dibangun dari `snprintf()` asli (bukan reimplementasi manual), dikirim via
`eth_sendTransaction`, `receipt.status=1` dan event ter-decode cocok persis dengan input.

**Catatan yang tidak diperbaiki (di luar cakupan saat itu):**
`verifyDevice()` (kedua overload) encoding-nya **sudah benar sejak awal** — terverifikasi
terpisah, ROGUE_ID/IDENTITY tidak terdampak bug ini. Event `TransactionLogged` di
`contracts/ModbusSecurity.sol:82` masih hardcode `slaveId=0` — bukan bug sesi ini, kontrak
tidak disentuh.

---

## Tema 3 — Restrukturisasi Dokumentasi & Diagram

**Commit:** `d93b055`, `2a06387`, `fda2cb2`

- `config.h` — sisa kode valve/aktuator mati (`REG_VALVE_STATUS`, tak pernah dipakai) dihapus.
- `DEPLOYMENT.md` disusun ulang jadi 7 bab: **A. Persiapan Awal → B. Daftarkan Perangkat →
  C. Monitoring Serial → D. Persiapan Attacker → E. Pengujian per Parameter → F. Pengolahan
  Data → G. Pemulihan**, mengikuti diagram Figma peneliti. Label "Skenario A-F" diganti nama
  parameter langsung (ROGUE_ID, VALUE_RANGE, DEVICE_LOST, IDENTITY, dst).
- Kriteria pengujian diubah dari durasi waktu ke **jumlah sampel (n)**: ROGUE_ID/VALUE_RANGE
  = polling kontinu tanpa target eksplisit; DEVICE_LOST/IDENTITY = wajib n≥10 siklus manual;
  Tamper-Evidence = n=1 record prosedural (langkah formal ditambahkan: picu → salin JSON →
  edit → hash ulang → bandingkan `receipt.logs`).
- `PENGOLAHAN_DATA.md` dipisah dari `DEPLOYMENT.md` sebagai file mandiri.
- 3 diagram SVG baru di `assets/`: `arsitektur-fisik.svg`, `alur-deteksi.svg`,
  `dua-jalur-data.svg` — dirujuk dari `LOCKED_DEFINITION.md` dan `DEPLOYMENT.md`.

---

## Tema 4 — Reroute TIMING → DEVICE_LOST

**Commit:** `ace1b33`

**Keputusan terkunci:** tidak ada kelas TIMING independen; respons lambat = perangkat tidak
responsif = DEVICE_LOST.

- `security.cpp:164-179` (`checkTiming()`): respons `> RESPONSE_WINDOW_MS` kini menghasilkan
  `ANOMALY_DEVICE_LOST`, bukan `ANOMALY_TIMING`.
- `security.h:22`: field `anomalyTiming` → `anomalyDeviceLost`.
- `config.h`: konstanta mati `MODBUS_RESPONSE_TIMEOUT_MS` dihapus (dikonfirmasi tak terpakai
  di mana pun).
- Enum `ANOMALY_TIMING=2` (`blockchain_client.h:18`) tetap ada untuk kompatibilitas, tapi
  tidak ada lagi jalur kode yang men-set-nya.
- **Ditemukan & dilaporkan (tidak diubah):** `tools/logger/export_anomali.py` (sudah di-untrack
  sejak Tema 6) masih punya entri label `2: "TIMING"` — jadi inert, bukan bug.

---

## Tema 5 — IDENTITY: dari Sekali Tembak ke Persisten & Memblokir Transaksi

**Commit:** `3d8de3c`, `c215472`

**Masalah awal:** kegagalan verifikasi UID hanya dilaporkan sekali, dan transaksi dari
perangkat ber-UID salah tetap tercatat sebagai valid (karena `checkPollResult()` tidak tahu
soal kegagalan UID).

**Perbaikan bertahap:**
1. `identityOk` diperkenalkan — kalau UID gagal, `markPresent()` dilewati sehingga
   `_currentlyPresent` tetap `false` dan verifikasi UID berulang tiap poll (IDENTITY jadi
   persisten, setara ROGUE_ID).
2. Transaksi digerbangi `if (safe && identityOk)` — sekarang perangkat ber-UID salah **tidak
   pernah** tercatat sebagai transaksi valid.

Dokumentasi diselaraskan: `LOCKED_DEFINITION.md` kriteria n IDENTITY diubah dari "n≥10 manual
reconnect" ke "polling kontinu, n besar"; `DEPLOYMENT.md` E.6 disederhanakan (tak perlu lagi
loop Ctrl+C/jalankan ulang); `assets/alur-deteksi.svg` footnote diperbarui.

*(Struktur `identityOk` ini kemudian di-refactor total di Tema 8 — lihat di bawah.)*

---

## Tema 6 — Gerbang Backward Pulse pada VALUE_RANGE

**Commit:** `e8f1462` (implementasi awal, ada bug), `9ae7724` (perbaikan)

**Temuan awal:** backward pulse (register `0x0002-0x0003`) dibaca tapi tidak pernah
diperiksa untuk anomali — hanya forward pulse yang divalidasi.

**Implementasi awal** (`e8f1462`): state `_lastBackwardPulse[id]` yang di-update **tiap
poll** (sliding baseline), plus `MAX_BACKWARD_DELTA=5` di `config.h:59`. Sekaligus:
`SLAVE_COUNT` dikembalikan ke 2 (sempat berubah jadi 1 secara lokal), dan `tools/` diuntrack
dari repo (`.gitignore` ditambah `tools/`, 11 file di-`git rm --cached` tapi tetap ada di
disk).

**Bug ditemukan lewat diagnosis** (dilaporkan sebelum diperbaiki): dengan sliding baseline,
putaran mundur pelan (+1..2 pulsa/poll) tak pernah mencapai ambang 5 walau totalnya mundur 10
pulsa — karena yang dibandingkan hanya selisih antar-2-poll-berurutan.

**Perbaikan** (`9ae7724`, `security.cpp:222-257`): `_lastBackwardPulse` → `_baseBackwardPulse`,
baseline direkam **sekali** saat kontak pertama (`== UINT32_MAX`) lalu **dikunci** — tidak
pernah digeser lagi. Delta kini dihitung terhadap baseline sesi, sehingga aliran balik pelan
yang terakumulasi tetap terdeteksi.

---

## Tema 7 — Presedensi Lapisan Perangkat di Atas Lapisan Data

**Commit:** `c4e462b`

**Gejala yang didiagnosis:** setelah `removeDevice()`, ROGUE_ID kadang "hilang" dan digantikan
VALUE_RANGE+IDENTITY pada satu perangkat yang sama, berganti-ganti antar siklus.

**Akar masalah:** urutan gerbang lama adalah `checkSlaveId → checkTiming → checkValueRange →
checkWhitelist` — lapisan DATA (value-range) dievaluasi **sebelum** lapisan PERANGKAT
(whitelist). Karena `checkPollResult()` berhenti di anomali pertama, saat VALUE_RANGE
terpicu duluan, `checkWhitelist()` tak pernah jalan → ROGUE_ID tak pernah di-set. Guard
IDENTITY di `main.cpp` bergantung pada flag itu, jadi ikut bocor.

**Perbaikan** — `checkPollResult()` dipecah jadi dua metode publik:
- `checkDeviceLayer()` (`security.cpp:88-110`): `checkSlaveId → checkTiming → checkWhitelist`
- `checkDataLayer()` (`security.cpp:113-116`): `checkValueRange` saja
- `checkPollResult()` lama jadi pembungkus tipis (`security.cpp:119-122`) untuk kompatibilitas

`main.cpp:95-149` disusun ulang jadi presedensi eksplisit dan linear: (1) lapisan perangkat →
gagal = `return` sebelum payload dievaluasi; (2) lapisan identitas → gagal = `return`,
tidak ditandai hadir; (3) `markPresent()` hanya untuk perangkat tepercaya; (4) lapisan data;
(5) transaksi sah. Variabel `bool safe`/`bool identityOk` dari Tema 5 dihapus karena tidak
lagi diperlukan — digantikan pola early-`return` per lapisan.

**Hasil:** ROGUE_ID kini konsisten menang (payload tidak pernah dievaluasi untuk perangkat
rogue), sesuai flowchart `DEVICE_LOST → ROGUE_ID → IDENTITY → VALUE_RANGE`.

---

## Tema 8 — Re-atestasi Identitas Berkala

**Commit:** `acf4d84`

**Gejala yang didiagnosis:** penggantian UID di tengah sesi (setelah slave sudah `markPresent`)
tidak terdeteksi sampai ada reset atau DEVICE_LOST — karena `readUID()`+`verifyDevice()` hanya
dipanggil di dalam gerbang `!isCurrentlyPresent()` (`main.cpp:105`, sebelum refactor Tema 8),
yang hanya true saat kontak pertama/reconnect.

**Perbaikan:**
- `config.h:77`: `IDENTITY_RECHECK_MS = 30000UL`
- `security.h:61-64`, `86`: metode `identityPerluCek()`/`identitySudahCek()` + state
  `_lastIdentityMs[SLAVE_COUNT+1]`
- `security.cpp:66-77`: `identityPerluCek()` — true bila belum pernah dicek (`==0`) atau
  interval terlampaui; `identitySudahCek()` mencatat `millis()` saat verifikasi berhasil
- `main.cpp:105-106`: gerbang identitas kini terbuka bila
  `!isCurrentlyPresent(slaveId) || identityPerluCek(slaveId)`
- `main.cpp:116`: `identitySudahCek(slaveId)` dipanggil **hanya** di cabang sukses — cabang
  gagal tetap diperiksa tiap poll (perilaku Tema 5 dipertahankan)

**Efek:** slave yang sudah hadir kini diverifikasi ulang UID-nya setiap ≥30 detik; pergantian
UID mid-sesi terdeteksi dalam ≤30 detik tanpa perlu reset/serial monitor.

---

## Tema 9 — Konektivitas Blockchain: Diagnosis & Perbaikan Visibilitas Error

**Commit:** `76da8de` (diagnosis), `38b9420` (perbaikan)

**Diagnosis:** timeout eksplisit hanya ada di `postJson()` (dipakai `logTransaction`/
`logAnomaly`); kedua overload `verifyDevice()` tidak punya timeout eksplisit. Tidak ada retry
logic di mana pun — semua kegagalan langsung dilewati (single-shot).

**Perbaikan** `postJson()` (`blockchain_client.cpp:238-296`):
- `http.setConnectTimeout(5000)` ditambahkan (sebelumnya hanya `setTimeout`)
- Kegagalan level-TCP (`code <= 0`) kini eksplisit: `[BC] KONEKSI GAGAL ke <url> | code=%d
  (%s)` memakai `http.errorToString()`
- Body respons **selalu** dibaca & dicetak (`[BC] RPC raw response: %s`), apa pun status
  HTTP-nya — sebelumnya hanya dibaca saat `code==200` dan tidak pernah dicetak mentah
- Deteksi `"error"` di body JSON → `[BC] RPC/EVM MENOLAK transaksi` — menutup celah "HTTP 200
  tapi sebenarnya ditolak EVM" yang sebelumnya tak terlihat sama sekali

**Perbaikan `logTransaction()`** (`blockchain_client.cpp:172-201`): sebelumnya memanggil
`postJson(payload)` **tanpa buffer**, lalu mencetak variabel `txHash` (hash SHA-256 input,
bukan hash on-chain) — jadi hash transaksi on-chain tak pernah benar-benar terlihat.
Sekarang memakai buffer lokal `outTxHash[68]` lewat `postJson(payload, outTxHash, ...)`, log
jadi `[BC] logTransaction | HTTP %d | hash=%s` dengan hash on-chain yang sebenarnya.

**Catatan:** komentar menyesatkan "Ganti selector dari contract Anda" dihapus
(`blockchain_client.cpp:20`) — keempat selector sudah diverifikasi keccak256 dan tidak pernah
perlu diganti untuk kontrak ini.

---

## Ringkasan Konstanta Kunci (state saat ini)

| Konstanta | Nilai | Lokasi |
|---|---|---|
| `SLAVE_COUNT` / `SLAVE_IDS` | `2` / `{1,2}` | `config.h:43-44` |
| `RESPONSE_WINDOW_MS` | `600` | `config.h:62` |
| `MAX_PULSE_DELTA` | `1000UL` | `config.h:58` |
| `MAX_BACKWARD_DELTA` | `5UL` (kumulatif sejak baseline sesi) | `config.h:59` |
| `IDENTITY_RECHECK_MS` | `30000UL` | `config.h:77` |
| HTTP connect/read timeout (`postJson`) | `5000` ms / `5000` ms | `blockchain_client.cpp:242-243` |

---

## Item Terbuka / Belum Diterapkan

1. **`config.h` mengandung kredensial Ganache in-repo** (RPC URL, contract address, private
   key, sender address) — sudah diberitahukan berkali-kali sepanjang sesi. Aman untuk
   testnet lokal, tapi disarankan dipindah ke `src/config_secret.h` (sudah ada di
   `.gitignore`) untuk kebersihan repo publik. **Belum dilakukan.**
2. **`config.h` saat laporan ini ditulis memiliki perubahan lokal belum di-commit**
   (kredensial Ganache baru — `CONTRACT_ADDRESS`, `SENDER_PRIVATE_KEY`, `SENDER_ADDRESS`
   berbeda dari commit `acf4d84`). **Sengaja tidak diikutkan dalam push laporan ini**,
   konsisten dengan pola sesi: perubahan kredensial adalah keputusan Anda sendiri.
3. **`contracts/.states/` ter-track secara tidak sengaja** sejak `76da8de` (artefak state
   lokal Remix/Ganache, bukan source) — belum dipertimbangkan untuk di-gitignore.
4. **`ModbusSecurity.sol` — event `TransactionLogged` masih hardcode `slaveId=0`**
   (`ModbusSecurity.sol:82`) — ditemukan saat Tema 2, di luar cakupan perbaikan (kontrak
   tidak disentuh sepanjang sesi ini).
5. **Opsi perbaikan yang pernah diusulkan tapi belum diterapkan** (dari diagnosis-diagnosis
   sebelumnya, untuk referensi bila ingin dilanjutkan): akumulator total-mundur untuk
   backward pulse sebagai alternatif baseline-terkunci; verifikasi UID kontinu tiap poll
   (bukan berkala 30 detik) bila definisi metodologi IDENTITY di Bab IV menuntutnya.

---

## Verifikasi Kompilasi

Setiap perubahan kode pada Tema 4–9 telah diverifikasi lolos `pio run -e esp32dev` (SUCCESS)
sebelum di-commit. Flash usage naik bertahap dari ~895KB menjadi ~896KB (68.3%→68.4%) seiring
penambahan fitur; RAM tetap ~48KB (14.7%) sepanjang sesi.
