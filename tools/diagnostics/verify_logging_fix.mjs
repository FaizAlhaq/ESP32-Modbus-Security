// ============================================================
// verify_logging_fix.mjs — Verifikasi PERBAIKAN ABI-encoding pada
// BlockchainClient::logTransaction() dan ::logAnomaly() (fixed).
//
// Metodologi sama persis dengan verify_verifyDevice_real.mjs:
//   - calldata dibangun oleh snprintf() ASLI (msvcrt via ctypes),
//     dengan urutan pemanggilan yang di-copy verbatim dari
//     blockchain_client.cpp (fixed) — lihat exact_snprintf.py
//   - RPC method: eth_sendTransaction + tunggu receipt (state-changing,
//     bukan eth_call seperti verifyDevice)
//   - Argumen di-decode ULANG dari raw calldata mentah (bukan dari
//     variabel input Node ini), untuk membuktikan offset/length benar
//   - Event di receipt.logs di-decode dan dibandingkan ke nilai asli
// ============================================================

import { execFileSync } from "child_process";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import path from "path";
import solc from "solc";
import ganache from "ganache";
import { ethers } from "ethers";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const CONTRACT_PATH = path.resolve(__dirname, "../../contracts/ModbusSecurity.sol");

// Nilai representatif skenario nyata (slave 2, mirip Skenario A/B di DEPLOYMENT.md)
const TX_DATA = "2|3|0x0000|1024|1715000000"; // format HashUtil::buildTxString
const TX_HASH = "a1b2c3d4".repeat(8);          // 64 char, seperti SHA256 asli
const ANOMALY_SLAVE_ID = 2;
const ANOMALY_TYPE = 3; // ANOMALY_VALUE_RANGE
const ANOMALY_DETAIL = "Slave 2 forward pulse turun: 100 -> 50";

function genCalldataFromRealCode() {
  const out = execFileSync("python", [path.join(__dirname, "exact_snprintf.py"), "logtest"], { encoding: "utf8" });
  const m = [...out.matchAll(/data:\s+(0x[0-9a-f]+)/g)];
  return { logTx: m[0][1], logAnomaly: m[1][1] };
}

function decodeAbiStringArg(hexNoSelector, headSlotIndex) {
  // hexNoSelector: hex string TANPA "0x" dan TANPA selector (sudah dipotong 8 char)
  // headSlotIndex: indeks slot (0-based) yang berisi OFFSET ke data dinamis
  const words = [];
  for (let i = 0; i < hexNoSelector.length; i += 64) words.push(hexNoSelector.slice(i, i + 64));
  const offsetBytes = parseInt(words[headSlotIndex], 16);
  const offsetWordIdx = offsetBytes / 32; // offset dihitung dari AWAL calldata argumen (setelah selector)
  const lengthHex = words[offsetWordIdx];
  const length = parseInt(lengthHex, 16);
  const dataWordsNeeded = Math.ceil(length / 32);
  let hex = "";
  for (let w = 0; w < dataWordsNeeded; w++) hex += words[offsetWordIdx + 1 + w];
  const dataHex = hex.slice(0, length * 2);
  const bytes = Buffer.from(dataHex, "hex");
  return { offsetBytes, length, decoded: bytes.toString("utf8") };
}

function compile() {
  const source = readFileSync(CONTRACT_PATH, "utf8");
  const input = {
    language: "Solidity",
    sources: { "ModbusSecurity.sol": { content: source } },
    settings: { evmVersion: "paris", outputSelection: { "*": { "*": ["abi", "evm.bytecode.object"] } } },
  };
  const output = JSON.parse(solc.compile(JSON.stringify(input)));
  const c = output.contracts["ModbusSecurity.sol"]["ModbusSecurity"];
  return { abi: c.abi, bytecode: "0x" + c.evm.bytecode.object };
}

async function main() {
  const { abi, bytecode } = compile();
  const server = ganache.provider({ logging: { quiet: true } });
  const provider = new ethers.BrowserProvider(server);
  const signer = await provider.getSigner(0);

  const factory = new ethers.ContractFactory(abi, bytecode, signer);
  const contract = await factory.deploy();
  await contract.waitForDeployment();
  const contractAddress = await contract.getAddress();
  console.log("Kontrak sandbox (kode FIXED):", contractAddress);
  console.log("");

  const { logTx, logAnomaly } = genCalldataFromRealCode();
  const iface = new ethers.Interface(abi);
  const rows = [];

  // --- logTransaction ---
  {
    const argsHex = logTx.slice(10); // buang "0x" + 8 char selector
    const decodedTxData = decodeAbiStringArg(argsHex, 0);
    const decodedTxHash = decodeAbiStringArg(argsHex, 1);

    const tx = await signer.sendTransaction({ to: contractAddress, data: logTx, gasLimit: 300000n });
    const receipt = await tx.wait();

    let eventDecoded = "-";
    if (receipt.status === 1 && receipt.logs.length) {
      const parsed = iface.parseLog(receipt.logs[0]);
      eventDecoded = `${parsed.name}(slaveId=${parsed.args[0]}, txData="${parsed.args[1]}", txHash="${parsed.args[2]}")`;
    }

    rows.push({
      fn: "logTransaction(txData, txHash)",
      statusBefore: 0,
      statusAfter: receipt.status,
      decodedFromCalldata: `txData="${decodedTxData.decoded}" (len=${decodedTxData.length}), txHash="${decodedTxHash.decoded}" (len=${decodedTxHash.length})`,
      calldataMatchesInput: decodedTxData.decoded === TX_DATA && decodedTxHash.decoded === TX_HASH,
      eventDecoded,
      eventMatchesInput:
        receipt.status === 1 &&
        eventDecoded.includes(`txData="${TX_DATA}"`) &&
        eventDecoded.includes(`txHash="${TX_HASH}"`),
    });
  }

  // --- logAnomaly ---
  {
    const argsHex = logAnomaly.slice(10);
    const words = [];
    for (let i = 0; i < argsHex.length; i += 64) words.push(argsHex.slice(i, i + 64));
    const decodedSlaveId = parseInt(words[0], 16);
    const decodedType = parseInt(words[1], 16);
    const decodedDetail = decodeAbiStringArg(argsHex, 2);

    const tx = await signer.sendTransaction({ to: contractAddress, data: logAnomaly, gasLimit: 300000n });
    const receipt = await tx.wait();

    let eventDecoded = "-";
    if (receipt.status === 1 && receipt.logs.length) {
      const parsed = iface.parseLog(receipt.logs[0]);
      eventDecoded = `${parsed.name}(slaveId=${parsed.args[0]}, anomalyType=${parsed.args[1]}, detail="${parsed.args[2]}")`;
    }

    rows.push({
      fn: "logAnomaly(slaveId, type, detail)",
      statusBefore: 0,
      statusAfter: receipt.status,
      decodedFromCalldata: `slaveId=${decodedSlaveId}, type=${decodedType}, detail="${decodedDetail.decoded}" (len=${decodedDetail.length})`,
      calldataMatchesInput:
        decodedSlaveId === ANOMALY_SLAVE_ID && decodedType === ANOMALY_TYPE && decodedDetail.decoded === ANOMALY_DETAIL,
      eventDecoded,
      eventMatchesInput:
        receipt.status === 1 &&
        eventDecoded.includes(`slaveId=${ANOMALY_SLAVE_ID}`) &&
        eventDecoded.includes(`anomalyType=${ANOMALY_TYPE}`) &&
        eventDecoded.includes(`detail="${ANOMALY_DETAIL}"`),
    });
  }

  console.log("| Fungsi | Status sebelum | Status sesudah | Data ter-decode dari calldata mentah | Cocok input? | Event ter-decode | Verified |");
  console.log("|---|---|---|---|---|---|---|");
  for (const r of rows) {
    console.log(
      `| ${r.fn} | ${r.statusBefore} (revert) | ${r.statusAfter} | ${r.decodedFromCalldata} | ${r.calldataMatchesInput ? "YA" : "TIDAK"} | ${r.eventDecoded} | ${r.eventMatchesInput ? "Y" : "N"} |`
    );
  }

  await server.disconnect();
}

main().catch((e) => {
  console.error("FATAL:", e);
  process.exit(1);
});
