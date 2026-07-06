# ============================================================
# exact_snprintf.py - Bangkitkan payload "data" persis seperti yang
# dibangun BlockchainClient::verifyDevice() di blockchain_client.cpp,
# TANPA menulis ulang logika padding/hex secara manual di Python.
#
# Format string, urutan argumen, dan TIPE argumen di-copy verbatim
# dari blockchain_client.cpp:52-57 dan :98-108. Pemformatan hex/padding
# yang sebenarnya dieksekusi oleh snprintf() asli dari libc (msvcrt),
# bukan oleh kode Python ini.
# ============================================================
import ctypes
import re
import sys
import json

libc = ctypes.CDLL("msvcrt")

CONTRACT_ADDRESS = b"0x3eC770D542c28cf75daf4882ea1D97ddb6937660"
SEL_VERIFY_DEVICE = b"0xeca8e63d"
SEL_VERIFY_DEVICE_UID = b"0xd14cf946"


def snprintf_1arg(slave_id):
    # blockchain_client.cpp:54-57 (verbatim):
    #   char params[256];
    #   snprintf(params, sizeof(params),
    #            "[{\"to\":\"%s\",\"data\":\"%s%064x\"},\"latest\"]",
    #            CONTRACT_ADDRESS, SEL_VERIFY_DEVICE, slaveId);
    # slaveId adalah uint8_t -> integer promotion ke `int` saat dilewatkan
    # ke fungsi variadic (perilaku C standar, sama di semua platform
    # mainstream termasuk toolchain ESP32/newlib karena int==unsigned int
    # secara representasi 32-bit).
    buf = ctypes.create_string_buffer(256)
    fmt = b'[{"to":"%s","data":"%s%064x"},"latest"]'
    libc._snprintf.restype = ctypes.c_int
    libc._snprintf.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p,
                                ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    libc._snprintf(buf, 256, fmt, CONTRACT_ADDRESS, SEL_VERIFY_DEVICE, ctypes.c_int(slave_id))
    return buf.value.decode()


def snprintf_2arg(slave_id, uid32_bytes):
    assert len(uid32_bytes) == 32
    # blockchain_client.cpp:100-101 (verbatim):
    #   char uidHex[65];
    #   for (int i = 0; i < 32; i++) snprintf(uidHex + i * 2, 3, "%02x", uid32[i]);
    #   uidHex[64] = '\0';
    uid_hex_buf = ctypes.create_string_buffer(65)
    libc._snprintf.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_int]
    for i in range(32):
        # Tiap panggilan snprintf menulis 2 hex char + null ke offset i*2,
        # persis seperti kode asli (null terminator lokal ditimpa iterasi berikut).
        tmp = ctypes.create_string_buffer(3)
        libc._snprintf(tmp, 3, b"%02x", ctypes.c_int(uid32_bytes[i]))
        uid_hex_buf.raw = uid_hex_buf.raw[:i * 2] + tmp.value + uid_hex_buf.raw[i * 2 + 2:]
    uid_hex = uid_hex_buf.raw[:64].decode()

    # blockchain_client.cpp:104-108 (verbatim):
    #   char params[256];
    #   snprintf(params, sizeof(params),
    #            "[{\"to\":\"%s\",\"data\":\"%s%064x%s\"},\"latest\"]",
    #            CONTRACT_ADDRESS, SEL_VERIFY_DEVICE_UID,
    #            (unsigned int)slaveId, uidHex);
    buf = ctypes.create_string_buffer(256)
    fmt = b'[{"to":"%s","data":"%s%064x%s"},"latest"]'
    libc._snprintf.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p,
                                ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint, ctypes.c_char_p]
    libc._snprintf(buf, 256, fmt, CONTRACT_ADDRESS, SEL_VERIFY_DEVICE_UID,
                   ctypes.c_uint(slave_id), uid_hex.encode())
    return buf.value.decode(), uid_hex


def extract_data_field(params_json_fragment):
    m = re.search(r'"data":"(0x[0-9a-fA-F]+)"', params_json_fragment)
    if not m:
        raise ValueError(f"Tidak menemukan field data di: {params_json_fragment}")
    return m.group(1)


SEL_LOG_TRANSACTION = b"0xd8628357"
SEL_LOG_ANOMALY = b"0x98bf92e5"


def _snprintf_str(fmt_bytes, *args_bytes_or_int, cap=64):
    # Wrapper generik: satu panggilan snprintf() ASLI, argtypes diset
    # per-panggilan sesuai tipe argumen (persis pola di blockchain_client.cpp).
    buf = ctypes.create_string_buffer(cap)
    argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
    call_args = [buf, cap, fmt_bytes]
    for a in args_bytes_or_int:
        if isinstance(a, bytes):
            argtypes.append(ctypes.c_char_p)
        else:
            argtypes.append(ctypes.c_uint)
        call_args.append(a)
    libc._snprintf.argtypes = argtypes
    libc._snprintf.restype = ctypes.c_int
    n = libc._snprintf(*call_args)
    return buf.value[:n] if n >= 0 else buf.value


def abi_encode_dynamic_string(s: bytes):
    # Verbatim dari abiEncodeDynamicString() di blockchain_client.cpp (fixed):
    #   pos += snprintf(out+pos, outCap-pos, "%064lx", (unsigned long)len);
    #   for i in len: pos += snprintf(out+pos, outCap-pos, "%02x", (uint8_t)str[i]);
    #   for i in len..padded: pos += snprintf(out+pos, outCap-pos, "00");
    length = len(s)
    padded = ((length + 31) // 32) * 32
    out = _snprintf_str(b"%064lx", length, cap=80)
    for byte in s:
        out += _snprintf_str(b"%02x", byte, cap=8)
    for _ in range(length, padded):
        out += b"00"
    return out.decode()


def build_logTransaction_calldata(tx_data: bytes, tx_hash: bytes):
    # Verbatim dari BlockchainClient::logTransaction() (fixed):
    #   size_t paddedData = ((strlen(txData)+31)/32)*32;
    #   uint32_t offset1 = 64;
    #   uint32_t offset2 = offset1 + 32 + paddedData;
    #   dataHex = SEL + hex(offset1) + hex(offset2) + encode(txData) + encode(txHash)
    padded_data = ((len(tx_data) + 31) // 32) * 32
    offset1 = 64
    offset2 = offset1 + 32 + padded_data

    parts = [SEL_LOG_TRANSACTION.decode()]
    parts.append(_snprintf_str(b"%064lx", offset1, cap=80).decode())
    parts.append(_snprintf_str(b"%064lx", offset2, cap=80).decode())
    parts.append(abi_encode_dynamic_string(tx_data))
    parts.append(abi_encode_dynamic_string(tx_hash))
    return "".join(parts)


def build_logAnomaly_calldata(slave_id: int, anomaly_type: int, detail: bytes):
    # Verbatim dari BlockchainClient::logAnomaly() (fixed):
    #   uint32_t offsetDetail = 96;
    #   dataHex = SEL + hex(slaveId) + hex(type) + hex(offsetDetail) + encode(detail)
    offset_detail = 96
    parts = [SEL_LOG_ANOMALY.decode()]
    parts.append(_snprintf_str(b"%064x", slave_id, cap=80).decode())
    parts.append(_snprintf_str(b"%064x", anomaly_type, cap=80).decode())
    parts.append(_snprintf_str(b"%064lx", offset_detail, cap=80).decode())
    parts.append(abi_encode_dynamic_string(detail))
    return "".join(parts)


if __name__ == "__main__" and len(sys.argv) > 1 and sys.argv[1] == "logtest":
    tx_data = b"2|3|0x0000|1024|1715000000"
    tx_hash = ("a1b2c3d4" * 8).encode()  # 64 char hex, seperti SHA256 asli
    print("logTransaction data:", build_logTransaction_calldata(tx_data, tx_hash))
    print()
    detail = b"Slave 2 forward pulse turun: 100 -> 50"
    print("logAnomaly data:", build_logAnomaly_calldata(2, 3, detail))
    sys.exit(0)

if __name__ == "__main__":
    slave_id = int(sys.argv[1]) if len(sys.argv) > 1 else 2

    params1 = snprintf_1arg(slave_id)
    data1 = extract_data_field(params1)
    print(f"=== verifyDevice({slave_id}) 1-arg ===")
    print("params (raw snprintf output) :", params1)
    print("data field                   :", data1)
    print("panjang data (hex chars)     :", len(data1) - 2, "(harus 8 selector + 64 slaveId = 72)")
    print()

    uid32 = bytes(range(32))  # UID contoh 32 byte, seperti buffer readUID()
    params2, uid_hex = snprintf_2arg(slave_id, uid32)
    data2 = extract_data_field(params2)
    print(f"=== verifyDevice({slave_id}, uid) 2-arg ===")
    print("uidHex yang dibentuk         :", uid_hex)
    print("params (raw snprintf output) :", params2)
    print("data field                   :", data2)
    print("panjang data (hex chars)     :", len(data2) - 2, "(harus 8 selector + 64 slaveId + 64 uid = 136)")
