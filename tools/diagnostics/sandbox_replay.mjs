// ============================================================
// sandbox_replay.mjs — Reproduksi terkontrol dugaan salah ABI-encode
//
// Ganache Desktop pengguna sedang tidak menyala, jadi skrip ini
// men-deploy ULANG contracts/ModbusSecurity.sol persis apa adanya ke
// instance Ganache in-process (paket npm `ganache`), lalu mengirim
// transaksi dengan payload "data" PERSIS seperti yang dibangun oleh
// src/blockchain_client.cpp (selector mentah, tanpa ABI-encode
// argumen) untuk logTransaction()/logAnomaly(). Hasilnya dibandingkan
// dengan addDevice()/verifyDevice() yang encoding-nya benar.
//
// Tujuan: jawab pertanyaan "apakah transaksi ini revert?" secara
// empiris, bukan spekulasi baca-kode.
// ============================================================

import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import path from "path";
import solc from "solc";
import ganache from "ganache";
import { ethers } from "ethers";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const CONTRACT_PATH = path.resolve(__dirname, "../../contracts/ModbusSecurity.sol");

// Selector yang sama persis dengan blockchain_client.cpp
const SEL = {
  verifyDevice1: "0xeca8e63d",   // verifyDevice(uint8)
  verifyDevice2: "0xd14cf946",   // verifyDevice(uint8,uint256)
  logTransaction: "0xd8628357",  // logTransaction(string,string)
  logAnomaly: "0x98bf92e5",      // logAnomaly(uint8,uint8,string)
};

function compile() {
  const source = readFileSync(CONTRACT_PATH, "utf8");
  const input = {
    language: "Solidity",
    sources: { "ModbusSecurity.sol": { content: source } },
    settings: {
      evmVersion: "paris",
      outputSelection: { "*": { "*": ["abi", "evm.bytecode.object"] } },
    },
  };
  const output = JSON.parse(solc.compile(JSON.stringify(input)));
  if (output.errors) {
    const fatal = output.errors.filter((e) => e.severity === "error");
    if (fatal.length) {
      console.error("Compile error:", fatal.map((e) => e.formattedMessage).join("\n"));
      process.exit(1);
    }
  }
  const c = output.contracts["ModbusSecurity.sol"]["ModbusSecurity"];
  return { abi: c.abi, bytecode: "0x" + c.evm.bytecode.object };
}

async function main() {
  console.log("Selector yang dihitung ulang dari signature (verifikasi kecocokan dengan blockchain_client.cpp):");
  console.log("  addDevice(uint8,uint256)      :", ethers.id("addDevice(uint8,uint256)").slice(0, 10));
  console.log("  removeDevice(uint8)           :", ethers.id("removeDevice(uint8)").slice(0, 10));
  console.log("  verifyDevice(uint8)           :", ethers.id("verifyDevice(uint8)").slice(0, 10), "(cpp:", SEL.verifyDevice1, ")");
  console.log("  verifyDevice(uint8,uint256)   :", ethers.id("verifyDevice(uint8,uint256)").slice(0, 10), "(cpp:", SEL.verifyDevice2, ")");
  console.log("  logTransaction(string,string) :", ethers.id("logTransaction(string,string)").slice(0, 10), "(cpp:", SEL.logTransaction, ")");
  console.log("  logAnomaly(uint8,uint8,string):", ethers.id("logAnomaly(uint8,uint8,string)").slice(0, 10), "(cpp:", SEL.logAnomaly, ")");
  console.log("");

  const { abi, bytecode } = compile();

  const server = ganache.provider({ logging: { quiet: true } });
  const provider = new ethers.BrowserProvider(server);
  const signer = await provider.getSigner(0);
  const from = await signer.getAddress();

  const factory = new ethers.ContractFactory(abi, bytecode, signer);
  const contract = await factory.deploy();
  await contract.waitForDeployment();
  const contractAddress = await contract.getAddress();
  console.log("Kontrak dideploy di:", contractAddress);
  console.log("");

  const iface = new ethers.Interface(abi);
  const results = [];

  async function sendRaw(label, data) {
    const tx = await signer.sendTransaction({ to: contractAddress, data, gasLimit: 300000n });
    const receipt = await tx.wait().catch((e) => e.receipt || null);
    let decoded = "-";
    if (receipt && receipt.status === 1) {
      const parsedLogs = [];
      for (const log of receipt.logs) {
        try {
          const parsed = iface.parseLog(log);
          parsedLogs.push(parsed.name + "(" + parsed.args.map(String).join(",") + ")");
        } catch (e) {
          parsedLogs.push("GAGAL DECODE");
        }
      }
      decoded = parsedLogs.length ? parsedLogs.join(" | ") : "TIDAK ADA EVENT";
    }
    results.push({
      label,
      hash: tx.hash,
      status: receipt ? receipt.status : "tx tidak pernah mined",
      gasUsed: receipt ? receipt.gasUsed.toString() : "-",
      decoded,
    });
  }

  // Baseline: addDevice, benar-benar ABI-encoded (via ethers, seperti Remix)
  const addDeviceCall = iface.encodeFunctionData("addDevice", [1, 12345]);
  await sendRaw("addDevice(1,12345) [baseline, encoding BENAR]", addDeviceCall);

  // verifyDevice(uint8) — encoding manual ala blockchain_client.cpp: selector + 64-hex slaveId
  const verify1Data = SEL.verifyDevice1 + BigInt(1).toString(16).padStart(64, "0");
  await sendRaw("verifyDevice(1) [encoding manual cpp, view-only lewat sendTransaction]", verify1Data);

  // logTransaction(string,string) — PERSIS seperti blockchain_client.cpp: HANYA selector, tanpa argumen
  await sendRaw("logTransaction(...) [BUG DIDUGA: selector saja, tanpa data]", SEL.logTransaction);

  // logAnomaly(uint8,uint8,string) — PERSIS seperti blockchain_client.cpp: selector + 2 byte mentah (%02x%02x)
  const logAnomalyData = SEL.logAnomaly + (1).toString(16).padStart(2, "0") + (3).toString(16).padStart(2, "0");
  await sendRaw("logAnomaly(...) [BUG DIDUGA: selector+2 byte mentah, bukan ABI-encoded]", logAnomalyData);

  console.log("| Label | Hash | Status | GasUsed | Decoded logs |");
  console.log("|---|---|---|---|---|");
  for (const r of results) {
    console.log(`| ${r.label} | ${r.hash.slice(0, 14)}... | ${r.status} | ${r.gasUsed} | ${r.decoded} |`);
  }

  await server.disconnect();
}

main().catch((e) => {
  console.error("FATAL:", e);
  process.exit(1);
});
