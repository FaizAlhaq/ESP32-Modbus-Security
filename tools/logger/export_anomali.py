from web3 import Web3
import csv, datetime

RPC      = "http://192.168.0.104:7545"   # IP & port Ganache-mu
CONTRACT = "0x3eC770D542c28cf75daf4882ea1D97ddb6937660"               # alamat contract (dari Remix/Ganache)

ABI = [{
  "anonymous": False, "name": "AnomalyLogged", "type": "event",
  "inputs": [
    {"indexed": False, "name": "slaveId",     "type": "uint8"},
    {"indexed": False, "name": "anomalyType", "type": "uint8"},
    {"indexed": False, "name": "detail",      "type": "string"},
    {"indexed": False, "name": "timestamp",   "type": "uint256"}
  ]
}]
JENIS = {
    0: "NO_REQUEST",
    1: "ROGUE_ID/WHITELIST",
    2: "TIMING",
    3: "VALUE_RANGE",
    4: "DEVICE_LOST",
    5: "IDENTITY",
}

w3 = Web3(Web3.HTTPProvider(RPC))
if not w3.is_connected():
    print("[ERROR] Tidak dapat terhubung ke Ganache. Pastikan Ganache berjalan.")
    exit(1)
c  = w3.eth.contract(address=Web3.to_checksum_address(CONTRACT), abi=ABI)
events = c.events.AnomalyLogged().get_logs(from_block=0)

with open("anomali_log.csv", "w", newline="", encoding="utf-8") as f:
    wr = csv.writer(f)
    wr.writerow(["no", "waktu", "block", "slaveId", "jenis", "detail", "txHash"])
    for i, e in enumerate(events, 1):
        a = e["args"]
        waktu = datetime.datetime.fromtimestamp(a["timestamp"]).strftime("%Y-%m-%d %H:%M:%S")
        wr.writerow([i, waktu, e["blockNumber"], a["slaveId"],
                     JENIS.get(a["anomalyType"], a["anomalyType"]), a["detail"],
                     e["transactionHash"].hex()])
print(f"{len(events)} anomali diekspor -> anomali_log.csv (buka di Excel)")