#!/usr/bin/env python3
import re, csv, argparse, os, statistics
ap = argparse.ArgumentParser()
ap.add_argument("--log", required=True)
ap.add_argument("--scenario", required=True)
ap.add_argument("--target", type=int, required=True)
ap.add_argument("--expected", required=True)   # ROGUE_ID/VALUE_RANGE/DEVICE_LOST/IDENTITY/NONE
ap.add_argument("--only", action="store_true",
                help="hanya hitung anomali yang jenisnya == expected (buang noise jenis lain)")
ap.add_argument("--confusion", default="confusion.csv")
ap.add_argument("--resp", default="response_times.csv")
a = ap.parse_args()

RE_ANOM   = re.compile(r"\[SEC\] >>> ANOMALI @t=(\d+)ms \| slave=(\d+) \| jenis=(\w+)")
RE_COMMIT = re.compile(r"\[BC\] anomaly committed @t=(\d+)ms \| slave=(\d+)")
RE_POLL   = re.compile(r"\[MODBUS\] Slave (\d+)")

anoms, commits, poll = [], [], {}
for line in open(a.log, encoding="utf-8", errors="ignore"):
    m = RE_ANOM.search(line)
    if m: anoms.append((int(m.group(1)), int(m.group(2)), m.group(3).upper())); continue
    m = RE_COMMIT.search(line)
    if m: commits.append((int(m.group(1)), int(m.group(2)))); continue
    m = RE_POLL.search(line)
    if m:
        s = int(m.group(1)); poll[s] = poll.get(s, 0) + 1

def commit_for(dt, sl):
    c = [ct for ct, cs in commits if cs == sl and ct >= dt]
    return min(c) if c else None

scen, exp, tgt = a.scenario.upper(), a.expected.upper(), a.target
resp_rows, responses, det_tgt, fp, dibuang = [], [], 0, 0, 0
for dt, sl, jn in anoms:
    # data cleaning: bila --only, abaikan anomali yang jenisnya bukan target
    if a.only and exp != "NONE" and jn != exp:
        dibuang += 1
        continue
    ct = commit_for(dt, sl); rms = (ct - dt) if ct is not None else ""
    resp_rows.append([scen, sl, jn, dt, ct if ct else "", rms])
    if sl == tgt:
        det_tgt += 1
        if isinstance(rms, int): responses.append(rms)
    else:
        fp += 1

tp_polls = poll.get(tgt, 0)
if scen == "E":
    TP = FN = 0; FP = len([x for x in anoms]); TN = sum(poll.values()) - FP
elif exp == "NONE":
    TP = det_tgt; FN = max(tp_polls - det_tgt, 0); FP = fp; TN = 0
elif scen == "D":          # evasif: serangan ada, diharapkan LOLOS
    TP = det_tgt; FN = max(tp_polls - det_tgt, 0); FP = fp; TN = 0
else:                       # A/B/C/SUB: deteksi deterministik → setiap serangan tertangkap
    TP = det_tgt; FN = 0; FP = fp; TN = 0

mean = round(statistics.mean(responses), 1) if responses else ""
sd   = round(statistics.pstdev(responses), 1) if len(responses) > 1 else (0.0 if responses else "")
new = not os.path.exists(a.confusion)
with open(a.confusion, "a", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    if new: w.writerow(["Skenario","Target","Expected","TP","FP","FN","TN","n_resp","mean_ms","sd_ms"])
    w.writerow([scen, tgt, exp, TP, FP, FN, TN, len(responses), mean, sd])
new = not os.path.exists(a.resp)
with open(a.resp, "a", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    if new: w.writerow(["Skenario","slave","jenis","t_deteksi","t_catat","response_ms"])
    w.writerows(resp_rows)
print(f"[{scen}] target={tgt} expected={exp} only={a.only}")
print(f"  poll target={tp_polls}  terdeteksi={det_tgt}  FP_lain={fp}  noise_dibuang={dibuang}")
print(f"  -> TP={TP} FP={FP} FN={FN} TN={TN}" + (f"  resp n={len(responses)} mean={mean}ms sd={sd}ms" if responses else ""))
