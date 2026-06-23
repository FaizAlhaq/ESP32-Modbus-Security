#!/usr/bin/env python3
"""
hitung_metrik.py — Hitung confusion matrix + metrik kinerja deteksi keamanan

Input (dibuat otomatis oleh parse_serial.py):
  confusion.csv       — kolom: trial, scenario, target, expected, detected, sel
  response_times.csv  — kolom: trial, t_deteksi_ms, t_catat_ms, response_ms

Pemakaian:
  python tools/logger/hitung_metrik.py
  python tools/logger/hitung_metrik.py --confusion path/confusion.csv --rt path/response_times.csv

Tidak butuh dependensi eksternal (cukup Python 3 standar: csv + statistics).
"""

import sys
import csv
import statistics
import argparse
from pathlib import Path


def read_csv(path: Path):
    with open(path, 'r', newline='', encoding='utf-8-sig') as f:
        return list(csv.DictReader(f))


def pct(x):
    return 'n/a' if x is None else f'{x * 100:.2f}%'


def safe_div(a, b):
    return (a / b) if b else None


def main():
    ap = argparse.ArgumentParser(
        description='Hitung metrik dari confusion.csv + response_times.csv'
    )
    ap.add_argument('--confusion', default='confusion.csv',
                    help='Path ke confusion.csv (default: confusion.csv)')
    ap.add_argument('--rt',        default='response_times.csv',
                    help='Path ke response_times.csv (default: response_times.csv)')
    args = ap.parse_args()

    conf_path = Path(args.confusion)
    rt_path   = Path(args.rt)

    # ── Baca confusion.csv ─────────────────────────────────────────────
    if not conf_path.exists():
        print(f'[ERROR] File tidak ditemukan: {conf_path}')
        print('        Jalankan parse_serial.py terlebih dahulu untuk membuat file ini.')
        print('        Contoh:')
        print('          python tools/logger/parse_serial.py \\')
        print('              --log <monitor.log> --scenario A --target 2 \\')
        print('              --expected ROGUE_ID --trials 10')
        sys.exit(1)

    rows = read_csv(conf_path)
    counts = {'TP': 0, 'FP': 0, 'FN': 0, 'TN': 0}
    unknown = {}
    for row in rows:
        sel = (row.get('sel') or '').strip().upper()
        if sel in counts:
            counts[sel] += 1
        else:
            unknown[sel] = unknown.get(sel, 0) + 1

    TP, FP, FN, TN = counts['TP'], counts['FP'], counts['FN'], counts['TN']
    total = TP + FP + FN + TN

    dr        = safe_div(TP, TP + FN)
    fpr       = safe_div(FP, FP + TN)
    precision = safe_div(TP, TP + FP)
    accuracy  = safe_div(TP + TN, total)
    f1 = (2 * precision * dr / (precision + dr)
          if precision and dr and (precision + dr) > 0 else None)

    # ── Baca response_times.csv ───────────────────────────────────────
    response_ms = []
    rt_warn = None
    if rt_path.exists():
        for row in read_csv(rt_path):
            raw = (row.get('response_ms') or '').strip().replace(',', '.')
            if raw:
                try:
                    response_ms.append(float(raw))
                except ValueError:
                    pass
    else:
        rt_warn = f'[WARN] {rt_path} tidak ditemukan — statistik response time dilewati.'

    # ── Cetak laporan ─────────────────────────────────────────────────
    print('=' * 60)
    print(' METRIK KINERJA — Deteksi Keamanan Modbus')
    print('=' * 60)
    print(f' confusion      : {conf_path}')
    print(f' response times : {rt_path}')
    print(f' Total trial    : {total}')
    if unknown:
        print(f' Label tidak dikenal (dilewati): {unknown}')
    print()

    print(' Confusion Matrix')
    print(' ' + '-' * 42)
    print(f'   TP = {TP:<5d}  True Positive  — anomali nyata, terdeteksi')
    print(f'   FP = {FP:<5d}  False Positive — normal, ditandai anomali')
    print(f'   FN = {FN:<5d}  False Negative — anomali nyata, lolos')
    print(f'   TN = {TN:<5d}  True Negative  — normal, benar tidak ditandai')
    print()
    print('                  Prediksi: ANOMALI   Prediksi: NORMAL')
    print(f'   Aktual ANOMALI     TP={TP:<6d}          FN={FN:<6d}')
    print(f'   Aktual NORMAL      FP={FP:<6d}          TN={TN:<6d}')
    print()

    print(' Metrik')
    print(' ' + '-' * 42)
    print(f'   Detection Rate (Recall/TPR) = {pct(dr)}   [TP/(TP+FN)]')
    print(f'   False Positive Rate (FPR)   = {pct(fpr)}   [FP/(FP+TN)]')
    print(f'   Precision                   = {pct(precision)}   [TP/(TP+FP)]')
    print(f'   Accuracy                    = {pct(accuracy)}   [(TP+TN)/total]')
    print(f'   F1-Score                    = {pct(f1)}   [2PR/(P+R)]')
    print()

    print(' Response Time (ms)')
    print(' ' + '-' * 42)
    if rt_warn:
        print(f'   {rt_warn}')
    elif not response_ms:
        print('   Tidak ada data response time di file.')
    else:
        n    = len(response_ms)
        mean = statistics.mean(response_ms)
        sd   = statistics.stdev(response_ms) if n >= 2 else 0.0
        print(f'   n    = {n}')
        print(f'   mean = {mean:.2f} ms')
        print(f'   SD   = {sd:.2f} ms  (sampel, n-1)')
        print(f'   min  = {min(response_ms):.2f} ms')
        print(f'   max  = {max(response_ms):.2f} ms')
    print('=' * 60)


if __name__ == '__main__':
    main()
