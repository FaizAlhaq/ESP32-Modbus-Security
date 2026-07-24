# REPORT — Diagnosis Konektivitas Blockchain (sebelum push)

> **Metode:** scan langsung ke `src/config.h`, `src/blockchain_client.h`,
> `src/blockchain_client.cpp`, dan status git. Tidak ada perubahan logika/isi file `.cpp`/`.h`
> — laporan ini murni observasi. Nomor baris merujuk kondisi kode saat scan (commit `acf4d84`).
> `SENDER_PRIVATE_KEY` sengaja disamarkan, hanya 4 karakter terakhir yang ditampilkan.

---

## Langkah 1 — Konfigurasi koneksi (`src/config.h`)

| Item | Nilai |
|---|---|
| `BLOCKCHAIN_RPC_URL` | `http://192.168.0.102:7545` |
| `CONTRACT_ADDRESS` | `0xA16c51D62C84f055b048524278cF3b2746Fa2cfA` |
| `SENDER_ADDRESS` | `0xe13378cDc652b5C7dF2576C76d825164C142828C` |
| `SENDER_PRIVATE_KEY` | `....a325` (60 karakter awal disembunyikan) |
| `SLAVE_COUNT` | `2` |
| `SLAVE_IDS` | `{1, 2}` |

---

## Langkah 2 — Penanganan koneksi HTTP (`blockchain_client.cpp` / `.h`)

| Item | Temuan |
|---|---|
| Timeout HTTP eksplisit | **Hanya 1 tempat**: `http.setTimeout(5000);` di `postJson()` — `blockchain_client.cpp:242` (dipakai oleh `logTransaction`/`logAnomaly`, keduanya lewat `postJson`) |
| Timeout di `verifyDevice()` | **Tidak ada** `setTimeout`/`setConnectTimeout` eksplisit di kedua overload `verifyDevice()` (`blockchain_client.cpp:80-120` dan `126-165`) — memakai default bawaan `HTTPClient` |
| Titik kirim request (1-arg `verifyDevice`) | `http.begin(BLOCKCHAIN_RPC_URL);` → `blockchain_client.cpp:92`; `http.POST(payload)` → `blockchain_client.cpp:95` |
| Titik kirim request (2-arg `verifyDevice`) | `http.begin(BLOCKCHAIN_RPC_URL);` → `blockchain_client.cpp:142`; `http.POST(payload)` → `blockchain_client.cpp:145` |
| Titik kirim request (`postJson`, dipakai `logTransaction`/`logAnomaly`) | `http.begin(BLOCKCHAIN_RPC_URL);` → `blockchain_client.cpp:240`; `http.POST(payload)` → `blockchain_client.cpp:244` |
| Retry logic | **Tidak ada.** Setiap gagal langsung dilewati (single-shot); tidak ada loop retry di manapun dalam file ini |

### Kutipan persis — status koneksi (Serial.printf)

```cpp
60   Serial.println("[BC] BlockchainClient siap, RPC: " BLOCKCHAIN_RPC_URL);
72   Serial.println("[BC] tersambung kembali");
75   Serial.println("[BC] NODE BLOCKCHAIN TIDAK TERJANGKAU");
112  Serial.printf("[BC] verifyDevice(%u) → %s\n", slaveId, verified ? "OK" : "TOLAK");
114  Serial.printf("[BC] verifyDevice HTTP error: %d\n", code);
156  Serial.printf("[BC] verifyDevice(%u, UID) → %s\n",
157               slaveId, verified ? "OK" : "TOLAK");
159  Serial.printf("[BC] verifyDevice(UID) HTTP error: %d\n", code);
200  Serial.printf("[BC] logTransaction → HTTP %d | hash: %.16s...\n", code, txHash);
233  Serial.printf("[BC] anomaly committed @t=%lums | slave=%u | jenis=%u | HTTP %d | hash=%s | %s\n",
                  (unsigned long)millis(), slaveId, (uint8_t)type, code, txHash, detail);
```

Transisi reachability (`updateReachability()`, `blockchain_client.cpp:69-77`) hanya mencetak `"tersambung kembali"`/`"NODE BLOCKCHAIN TIDAK TERJANGKAU"` **saat status berubah**, bukan tiap panggilan — jadi kegagalan yang berulang di HTTP-level tidak menghasilkan baris log baru setelah baris pertama, kecuali dicetak lewat baris "HTTP error: %d" masing-masing fungsi pemanggil.

---

## Langkah 3 — Status git (sebelum push)

```
$ git status --short
 M REPORT.md
?? contracts/.states/

$ git diff --stat
REPORT.md | 158 +++++++++++++++++++++++++++++++++++++++++++++++---------------
1 file changed, 120 insertions(+), 38 deletions(-)
```

- **`REPORT.md`** — modified (isi diagnosis IDENTITY dari sesi sebelumnya; akan ditimpa laporan ini).
- **`contracts/.states/`** — untracked, belum pernah masuk repo (kemungkinan artefak lokal Remix/Truffle).
- `src/config.h` **tidak muncul** di `git status` — artinya nilai RPC/kontrak/kunci di Langkah 1 **sudah ter-commit** pada commit sebelumnya (`acf4d84`), bukan perubahan lokal baru.
- `tools/` dan `logs/` tetap di luar tracking (sudah ter-gitignore, tidak muncul di status) — tidak disentuh.

---

## Ringkasan

Tidak ada perubahan kode dilakukan pada scan ini. Satu-satunya berkas yang berubah adalah `REPORT.md` ini sendiri. `config.h` sudah bersih (tidak ada perubahan lokal tertunda) — nilai RPC/kontrak yang tercatat di atas adalah nilai yang sudah live di repo sejak commit `acf4d84`.
