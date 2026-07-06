// ============================================================
// check_tx_status.mjs — Cek status riil transaksi ke ModbusSecurity.sol
// di Ganache workspace Anda (bukan sandbox — RPC & alamat kontrak
// diambil langsung dari src/config.h).
//
// Pakai setelah Ganache Desktop menyala dan workspace yang berisi
// riwayat pengujian sudah dibuka.
//
// Jalankan: node tools/diagnostics/check_tx_status.mjs
// ============================================================

import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import path from "path";
import { ethers } from "ethers";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const CONFIG_PATH = path.resolve(__dirname, "../../src/config.h");

function readConfigDefine(name) {
  const src = readFileSync(CONFIG_PATH, "utf8");
  const re = new RegExp(`#define\\s+${name}\\s+"([^"]+)"`);
  const m = src.match(re);
  if (!m) throw new Error(`Tidak menemukan #define ${name} di config.h`);
  return m[1];
}

const RPC_URL = readConfigDefine("BLOCKCHAIN_RPC_URL");
const CONTRACT_ADDRESS = readConfigDefine("CONTRACT_ADDRESS");

const ABI = [
  "event DeviceAdded(uint8 indexed slaveId)",
  "event DeviceRemoved(uint8 indexed slaveId)",
  "event TransactionLogged(uint8 indexed slaveId, string txData, string txHash, uint256 timestamp)",
  "event AnomalyLogged(uint8 indexed slaveId, uint8 anomalyType, string detail, uint256 timestamp)",
];

// Selector yang dipakai blockchain_client.cpp — untuk memberi nama fungsi
const SELECTOR_NAMES = {
  "0xeca8e63d": "verifyDevice(uint8)",
  "0xd14cf946": "verifyDevice(uint8,uint256)",
  "0xd8628357": "logTransaction(string,string)",
  "0x98bf92e5": "logAnomaly(uint8,uint8,string)",
};
// addDevice/removeDevice dipanggil manual via Remix — Remix meng-encode
// selector dari ABI kontrak sendiri, hitung agar bisa dikenali juga.
SELECTOR_NAMES[ethers.id("addDevice(uint8,uint256)").slice(0, 10)] = "addDevice(uint8,uint256)";
SELECTOR_NAMES[ethers.id("removeDevice(uint8)").slice(0, 10)] = "removeDevice(uint8)";

async function main() {
  console.log(`RPC     : ${RPC_URL}`);
  console.log(`Kontrak : ${CONTRACT_ADDRESS}`);

  const provider = new ethers.JsonRpcProvider(RPC_URL);

  let latest;
  try {
    latest = await provider.getBlockNumber();
  } catch (e) {
    console.error(`\nTidak bisa connect ke ${RPC_URL}.`);
    console.error("Pastikan Ganache Desktop menyala dan workspace yang benar sudah dibuka.");
    console.error("Detail error:", e.message);
    process.exit(1);
  }
  console.log(`Blok terbaru: ${latest}\n`);

  const iface = new ethers.Interface(ABI);
  const rows = [];

  for (let n = 0; n <= latest; n++) {
    const block = await provider.getBlock(n, true);
    if (!block || !block.prefetchedTransactions) continue;

    for (const tx of block.prefetchedTransactions) {
      if (!tx.to || tx.to.toLowerCase() !== CONTRACT_ADDRESS.toLowerCase()) continue;

      const receipt = await provider.getTransactionReceipt(tx.hash);
      const selector = tx.data.slice(0, 10);
      const fnName = SELECTOR_NAMES[selector] || `selector tak dikenal (${selector})`;

      let decoded = "-";
      if (receipt.status === 1) {
        const parsed = [];
        for (const log of receipt.logs) {
          try {
            const p = iface.parseLog(log);
            parsed.push(`${p.name}(${p.args.map(String).join(",")})`);
          } catch {
            parsed.push("GAGAL DECODE");
          }
        }
        decoded = parsed.length ? parsed.join(" | ") : "TIDAK ADA EVENT";
      }

      rows.push({
        hash: tx.hash,
        fn: fnName,
        status: receipt.status,
        gasUsed: receipt.gasUsed.toString(),
        decoded,
      });
    }
  }

  if (rows.length === 0) {
    console.log("Tidak ada transaksi ke kontrak ini di seluruh blok. Belum pernah ada panggilan tercatat.");
    return;
  }

  console.log("| Hash | Fungsi | Status | GasUsed | Decoded logs |");
  console.log("|---|---|---|---|---|");
  for (const r of rows) {
    console.log(`| ${r.hash.slice(0, 14)}... | ${r.fn} | ${r.status} | ${r.gasUsed} | ${r.decoded} |`);
  }

  // Baseline gasUsed dari fungsi admin yang pasti sukses
  const baseline = rows.filter((r) => r.fn.startsWith("addDevice") || r.fn.startsWith("removeDevice"));
  const suspect = rows.filter((r) => r.fn.startsWith("logTransaction") || r.fn.startsWith("logAnomaly"));
  const avg = (arr) => (arr.length ? (arr.reduce((s, r) => s + Number(r.gasUsed), 0) / arr.length).toFixed(0) : "-");

  console.log(`\nRata-rata gasUsed baseline (addDevice/removeDevice): ${avg(baseline)}`);
  console.log(`Rata-rata gasUsed logTransaction/logAnomaly       : ${avg(suspect)}`);
  const failedSuspect = suspect.filter((r) => r.status !== 1).length;
  if (suspect.length) {
    console.log(`\n${failedSuspect}/${suspect.length} panggilan logTransaction/logAnomaly berstatus REVERT (status=0).`);
  } else {
    console.log("\nTidak ada panggilan logTransaction/logAnomaly ditemukan di histori chain ini.");
  }
}

main().catch((e) => {
  console.error("FATAL:", e);
  process.exit(1);
});
