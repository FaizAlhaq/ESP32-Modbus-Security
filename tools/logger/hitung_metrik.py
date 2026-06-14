#!/usr/bin/env python3
# ============================================================
#  hitung_metrik.py — Hitung metrik kinerja deteksi keamanan
#
#  Membaca hasil_pengujian.csv (satu baris = satu trial) lalu:
#    - tally kolom "Sel" yang berisi label TP / FP / FN / TN
#    - susun confusion matrix
#    - hitung Detection Rate (Recall), FPR, Precision, Accuracy, F1
#    - hitung response time: mean & standard deviation (kolom "response(ms)")
#
#  Pemakaian:
#    python tools/logger/hitung_metrik.py [hasil_pengujian.csv]
#
#  Tidak butuh dependensi eksternal (cukup Python 3 standar:
#  modul csv + statistics).
# ============================================================

import sys
import csv
import statistics

# Definisi singkat tiap label confusion matrix (untuk dicetak di laporan)
LABEL_INFO = {
    "TP": "True Positive  - anomali nyata, terdeteksi",
    "FP": "False Positive - normal, tetapi ditandai anomali",
    "FN": "False Negative - anomali nyata, lolos (tidak terdeteksi)",
    "TN": "True Negative  - normal, benar tidak ditandai",
}


def normalize(s: str) -> str:
    """Lowercase + buang spasi, agar pencocokan header tahan variasi."""
    return "".join((s or "").lower().split())


def sniff_reader(path):
    """Buka CSV, deteksi delimiter (',' atau ';'), kembalikan DictReader + handle file."""
    f = open(path, "r", newline="", encoding="utf-8-sig")
    sample = f.read(4096)
    f.seek(0)
    try:
        dialect = csv.Sniffer().sniff(sample, delimiters=",;\t")
        delim = dialect.delimiter
    except csv.Error:
        # Fallback: pilih yang paling sering muncul di baris pertama
        head = sample.splitlines()[0] if sample else ""
        delim = ";" if head.count(";") > head.count(",") else ","
    return csv.DictReader(f, delimiter=delim), f, delim


def find_column(fieldnames, *candidates_contains):
    """Cari nama kolom yang (setelah normalize) mengandung salah satu kata kunci."""
    norm_map = {normalize(fn): fn for fn in fieldnames}
    # 1) cocok persis dulu
    for cand in candidates_contains:
        if cand in norm_map:
            return norm_map[cand]
    # 2) cocok sebagian (substring)
    for cand in candidates_contains:
        for norm, original in norm_map.items():
            if cand in norm:
                return original
    return None


def pct(x):
    return "n/a" if x is None else f"{x * 100:.2f}%"


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "hasil_pengujian.csv"

    try:
        reader, fh, delim = sniff_reader(path)
    except FileNotFoundError:
        print(f"[ERROR] File tidak ditemukan: {path}")
        print("        Letakkan hasil_pengujian.csv di folder kerja, atau berikan path:")
        print("        python tools/logger/hitung_metrik.py <path_ke_csv>")
        sys.exit(1)

    fieldnames = reader.fieldnames or []
    sel_col = find_column(fieldnames, "sel")
    resp_col = find_column(fieldnames, "response(ms)", "responsems", "response", "respon")

    if sel_col is None:
        print(f"[ERROR] Kolom 'Sel' tidak ditemukan. Header terbaca: {fieldnames}")
        fh.close()
        sys.exit(1)

    counts = {"TP": 0, "FP": 0, "FN": 0, "TN": 0}
    response_ms = []
    n_rows = 0
    unknown_labels = {}

    for row in reader:
        n_rows += 1
        label = (row.get(sel_col) or "").strip().upper()
        if label in counts:
            counts[label] += 1
        elif label == "":
            unknown_labels["(kosong)"] = unknown_labels.get("(kosong)", 0) + 1
        else:
            unknown_labels[label] = unknown_labels.get(label, 0) + 1

        if resp_col is not None:
            raw = (row.get(resp_col) or "").strip().replace(",", ".")
            if raw:
                try:
                    response_ms.append(float(raw))
                except ValueError:
                    pass

    fh.close()

    TP, FP, FN, TN = counts["TP"], counts["FP"], counts["FN"], counts["TN"]
    total = TP + FP + FN + TN

    def safe_div(a, b):
        return (a / b) if b else None

    detection_rate = safe_div(TP, TP + FN)          # Recall / Sensitivity / TPR
    fpr            = safe_div(FP, FP + TN)           # False Positive Rate
    precision      = safe_div(TP, TP + FP)           # Positive Predictive Value
    accuracy       = safe_div(TP + TN, total)
    if precision and detection_rate and (precision + detection_rate) > 0:
        f1 = 2 * precision * detection_rate / (precision + detection_rate)
    else:
        f1 = None

    # ---- Cetak laporan ----
    print("=" * 60)
    print(" HITUNG METRIK - Deteksi Keamanan Modbus")
    print("=" * 60)
    print(f" Sumber data : {path}  (delimiter '{delim}')")
    print(f" Total baris : {n_rows}  | trial valid (TP/FP/FN/TN): {total}")
    if unknown_labels:
        print(f" Label 'Sel' tidak dikenal (diabaikan): {unknown_labels}")
    print()

    print(" Confusion Matrix")
    print(" " + "-" * 40)
    for k in ("TP", "FP", "FN", "TN"):
        print(f"   {k} = {counts[k]:<5d}  {LABEL_INFO[k]}")
    print()
    print("                 Prediksi: ANOMALI   Prediksi: NORMAL")
    print(f"   Aktual ANOMALI     TP={TP:<6d}          FN={FN:<6d}")
    print(f"   Aktual NORMAL      FP={FP:<6d}          TN={TN:<6d}")
    print()

    print(" Metrik")
    print(" " + "-" * 40)
    print(f"   Detection Rate (Recall/TPR) = {pct(detection_rate)}   [TP/(TP+FN)]")
    print(f"   False Positive Rate (FPR)   = {pct(fpr)}   [FP/(FP+TN)]")
    print(f"   Precision                   = {pct(precision)}   [TP/(TP+FP)]")
    print(f"   Accuracy                    = {pct(accuracy)}   [(TP+TN)/total]")
    print(f"   F1-Score                    = {pct(f1)}   [2PR/(P+R)]")
    print()

    print(" Response Time (ms)")
    print(" " + "-" * 40)
    if resp_col is None:
        print("   Kolom 'response(ms)' tidak ditemukan — dilewati.")
    elif not response_ms:
        print("   Tidak ada nilai response(ms) numerik yang terbaca.")
    else:
        n = len(response_ms)
        mean = statistics.mean(response_ms)
        # SD sampel (n-1, Bessel). Butuh minimal 2 data; jika 1, SD = 0.
        sd = statistics.stdev(response_ms) if n >= 2 else 0.0
        print(f"   n     = {n}")
        print(f"   mean  = {mean:.2f} ms")
        print(f"   SD    = {sd:.2f} ms   (sampel, n-1)")
        print(f"   min   = {min(response_ms):.2f} ms")
        print(f"   max   = {max(response_ms):.2f} ms")
    print("=" * 60)


if __name__ == "__main__":
    main()
