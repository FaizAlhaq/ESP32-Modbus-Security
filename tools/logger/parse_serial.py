#!/usr/bin/env python3
"""
parse_serial.py — Parse log serial PlatformIO → confusion.csv + response_times.csv

Pemakaian:
  python tools/logger/parse_serial.py \\
      --log .pio/build/esp32dev/monitor-XXXXXX.log \\
      --scenario A --target 2 --expected ROGUE_ID --trials 10

Argumen:
  --log       Path ke file .log dari PlatformIO (monitor_filters = log2file)
  --scenario  Label skenario: A | B | C | D | E | SUB
  --target    ID slave yang diuji (1-5)
  --expected  Jenis anomali yang diharapkan:
              ROGUE_ID | VALUE_RANGE | DEVICE_LOST | IDENTITY | NONE
              (NONE untuk skenario evasif / operasi normal)
  --trials    Total trial yang direncanakan — dipakai untuk menghitung FN / TN.
              Default: sama dengan jumlah deteksi (tidak ada FN/TN).
  --out-dir   Direktori output (default: direktori kerja saat ini)

Output:
  confusion.csv       — satu baris per trial
                        kolom: trial, scenario, target, expected, detected, sel
  response_times.csv  — satu baris per anomali terdeteksi
                        kolom: trial, t_deteksi_ms, t_catat_ms, response_ms

Pemetaan skenario → expected:
  A   → ROGUE_ID
  B   → VALUE_RANGE
  C   → DEVICE_LOST
  D   → NONE  (evasif — tidak seharusnya terdeteksi)
  E   → NONE  (operasi normal — ukur FPR)
  SUB → IDENTITY
"""

import re
import csv
import sys
import argparse
import statistics
from pathlib import Path

# Pola baris log anomali firmware
# Contoh: [SEC] >>> ANOMALI @t=42ms | slave=2 | jenis=ROGUE_ID
RE_ANOMALY = re.compile(
    r'\[SEC\]\s*>>>\s*ANOMALI\s+@t=(\d+)ms\s*\|\s*slave=(\d+)\s*\|\s*jenis=(\w+)',
    re.IGNORECASE
)

# Contoh: [BC] anomaly committed @t=198ms | HTTP 200
RE_COMMITTED = re.compile(
    r'\[BC\]\s*anomaly\s+committed\s+@t=(\d+)ms',
    re.IGNORECASE
)

SCENARIO_EXPECTED = {
    'A':   'ROGUE_ID',
    'B':   'VALUE_RANGE',
    'C':   'DEVICE_LOST',
    'D':   'NONE',
    'E':   'NONE',
    'SUB': 'IDENTITY',
}


def parse_log(log_path: Path, target_id: int):
    """
    Baca log line-by-line. Kumpulkan pasangan (t_deteksi, t_catat, jenis)
    untuk slave target. Mengembalikan list dict.
    """
    events = []
    pending = None  # event yang sudah punya t_deteksi, menunggu t_catat

    with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            m = RE_ANOMALY.search(line)
            if m:
                t_det = int(m.group(1))
                slave = int(m.group(2))
                jenis = m.group(3).upper()
                if slave == target_id:
                    # Jika masih ada pending yang belum dapat t_catat, simpan dulu
                    if pending is not None:
                        events.append(pending)
                    pending = {'t_deteksi': t_det, 'jenis': jenis, 't_catat': None}
                continue

            m = RE_COMMITTED.search(line)
            if m and pending is not None:
                pending['t_catat'] = int(m.group(1))
                events.append(pending)
                pending = None

    # Anomali tanpa t_catat (koneksi blockchain putus / belum commit)
    if pending is not None:
        events.append(pending)

    return events


def classify(events, expected: str, n_trials: int):
    """
    Klasifikasi setiap event sebagai TP/FP/FN/TN.
    Kembalikan (confusion_rows, rt_rows).
    """
    confusion_rows = []
    rt_rows = []
    trial = 1

    if expected == 'NONE':
        # Skenario evasif / normal: setiap deteksi = FP, sisanya = TN
        for ev in events:
            confusion_rows.append({
                'trial': trial,
                'expected': expected,
                'detected': ev['jenis'],
                'sel': 'FP',
            })
            if ev['t_catat'] is not None:
                rt_rows.append({
                    'trial': trial,
                    't_deteksi_ms': ev['t_deteksi'],
                    't_catat_ms':   ev['t_catat'],
                    'response_ms':  ev['t_catat'] - ev['t_deteksi'],
                })
            trial += 1

        tn_count = max(0, n_trials - len(events))
        for _ in range(tn_count):
            confusion_rows.append({
                'trial': trial, 'expected': expected,
                'detected': '', 'sel': 'TN',
            })
            trial += 1

    else:
        # Skenario dengan anomali yang diharapkan
        for ev in events:
            sel = 'TP' if ev['jenis'] == expected else 'FP'
            confusion_rows.append({
                'trial': trial,
                'expected': expected,
                'detected': ev['jenis'],
                'sel': sel,
            })
            if sel == 'TP' and ev['t_catat'] is not None:
                rt_rows.append({
                    'trial': trial,
                    't_deteksi_ms': ev['t_deteksi'],
                    't_catat_ms':   ev['t_catat'],
                    'response_ms':  ev['t_catat'] - ev['t_deteksi'],
                })
            trial += 1

        fn_count = max(0, n_trials - len(events))
        for _ in range(fn_count):
            confusion_rows.append({
                'trial': trial, 'expected': expected,
                'detected': '', 'sel': 'FN',
            })
            trial += 1

    return confusion_rows, rt_rows


def main():
    ap = argparse.ArgumentParser(
        description='Parse log serial PlatformIO → confusion.csv + response_times.csv'
    )
    ap.add_argument('--log',      required=True,
                    help='Path ke file .log PlatformIO')
    ap.add_argument('--scenario', required=True,
                    choices=['A', 'B', 'C', 'D', 'E', 'SUB'],
                    help='Label skenario pengujian')
    ap.add_argument('--target',   required=True, type=int,
                    help='ID slave yang diuji (1-5)')
    ap.add_argument('--expected', required=True,
                    choices=['ROGUE_ID', 'VALUE_RANGE', 'DEVICE_LOST', 'IDENTITY', 'NONE'],
                    help='Jenis anomali yang diharapkan (NONE = evasif/normal)')
    ap.add_argument('--trials',   type=int, default=None,
                    help='Total trial direncanakan (untuk FN/TN). Default: jumlah deteksi.')
    ap.add_argument('--out-dir',  default='.',
                    help='Direktori output (default: .)')
    args = ap.parse_args()

    log_path = Path(args.log)
    if not log_path.exists():
        print(f'[ERROR] File log tidak ditemukan: {log_path}')
        sys.exit(1)

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f'[PARSE] {log_path.name}  skenario={args.scenario}  slave={args.target}  expected={args.expected}')
    events = parse_log(log_path, args.target)
    print(f'[PARSE] Ditemukan {len(events)} deteksi anomali untuk slave {args.target}')

    n_trials = args.trials if args.trials is not None else len(events)
    confusion_rows, rt_rows = classify(events, args.expected, n_trials)

    # tulis confusion.csv
    conf_path = out_dir / 'confusion.csv'
    with open(conf_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=['trial', 'scenario', 'target', 'expected', 'detected', 'sel'])
        w.writeheader()
        for row in confusion_rows:
            w.writerow({**row, 'scenario': args.scenario, 'target': args.target})
    print(f'[OUT]   {conf_path}  ({len(confusion_rows)} baris)')

    # tulis response_times.csv
    rt_path = out_dir / 'response_times.csv'
    with open(rt_path, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=['trial', 't_deteksi_ms', 't_catat_ms', 'response_ms'])
        w.writeheader()
        for row in rt_rows:
            w.writerow(row)
    print(f'[OUT]   {rt_path}  ({len(rt_rows)} baris timing)')

    # ringkasan
    counts = {k: 0 for k in ('TP', 'FP', 'FN', 'TN')}
    for r in confusion_rows:
        if r['sel'] in counts:
            counts[r['sel']] += 1
    total = sum(counts.values())
    print(f'\n[RINGKASAN]  TP={counts["TP"]}  FP={counts["FP"]}  FN={counts["FN"]}  TN={counts["TN"]}  total={total}')
    if rt_rows:
        times = [r['response_ms'] for r in rt_rows]
        mean  = statistics.mean(times)
        sd    = statistics.stdev(times) if len(times) >= 2 else 0.0
        print(f'             Response time  mean={mean:.1f}ms  SD={sd:.1f}ms  n={len(times)}')
    print('\nSelesai. Jalankan hitung_metrik.py untuk metrik lengkap.')


if __name__ == '__main__':
    main()
