// ============================================================
// verify_verifyDevice_real.mjs — Verifikasi PANGGILAN ASLI
// BlockchainClient::verifyDevice() (1-arg dan 2-arg) yang dipakai
// Security::checkWhitelist() (ROGUE_ID, tiap poll) dan main.cpp
// (IDENTITY, first-contact/reconnect).
//
// Calldata "data" TIDAK ditulis ulang manual di sini — diambil dari
// exact_snprintf.py, yang menjalankan snprintf() ASLI (libc msvcrt)
// dengan format string + argumen yang di-copy verbatim dari
// blockchain_client.cpp:52-57 dan :98-108.
//
// RPC method yang dipakai juga PERSIS sama dengan kode asli: eth_call
// (bukan eth_sendTransaction) — lihat blockchain_client.cpp:60
// buildRpcPayload("eth_call", ...).
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

function genCalldata(slaveId) {
  const out = execFileSync("python", [path.join(__dirname, "exact_snprintf.py"), String(slaveId)], {
    encoding: "utf8",
  });
  const dataFields = [...out.matchAll(/data field\s+:\s+(0x[0-9a-f]+)/g)].map((m) => m[1]);
  const uidHexLine = out.match(/uidHex yang dibentuk\s+:\s+([0-9a-f]+)/)[1];
  return { data1: dataFields[0], data2: dataFields[1], uidHex: uidHexLine };
}

// Meniru parsing hasil eth_call PERSIS seperti blockchain_client.cpp:70-83
function parseVerifiedLikeCpp(resultHex) {
  // body.indexOf("\"result\":\"0x") ... hexVal = substring setelah "0x"
  const hexVal = resultHex.replace(/^0x/, "");
  for (const c of hexVal) {
    if (c !== "0") return true;
  }
  return false;
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
  console.log("Kontrak sandbox:", contractAddress);

  // Setup state: slave 2 terdaftar dengan UID = bytes(0..31), slave 1 TIDAK terdaftar
  const uidBytesForSlave2 = "0x" + Array.from({ length: 32 }, (_, i) => i.toString(16).padStart(2, "0")).join("");
  const uidAsUint256Slave2 = BigInt(uidBytesForSlave2);
  await (await contract.addDevice(2, uidAsUint256Slave2)).wait();
  console.log(`Setup: whitelist[2]=true, deviceUID[2] = UID contoh (bytes 0..31). whitelist[1] tetap false (belum didaftarkan).`);
  console.log("");

  const results = [];

  async function testEthCall(label, calldata, expectDecodedSlaveId) {
    // eth_call PERSIS metode yang dipakai blockchain_client.cpp (bukan sendTransaction)
    const raw = await provider.send("eth_call", [{ to: contractAddress, data: calldata }, "latest"]);
    const verified = parseVerifiedLikeCpp(raw);

    // Decode ulang argumen yang TERTANAM di calldata asli (bukan input awal kita)
    // untuk membuktikan ID yang dikirim = ID yang benar-benar ada di dalam byte calldata.
    const selector = calldata.slice(0, 10);
    const argsHex = calldata.slice(10);
    const embeddedSlaveId = BigInt("0x" + argsHex.slice(0, 64)).toString();

    results.push({
      label,
      selector,
      embeddedSlaveId,
      expectDecodedSlaveId,
      match: embeddedSlaveId === String(expectDecodedSlaveId),
      rawResult: raw,
      verified,
    });
  }

  const cd2 = genCalldata(2);
  const cd1 = genCalldata(1);

  await testEthCall("verifyDevice(2) 1-arg — slave TERDAFTAR, harus TRUE", cd2.data1, 2);
  await testEthCall("verifyDevice(1) 1-arg — slave TIDAK terdaftar, harus FALSE", cd1.data1, 1);
  await testEthCall("verifyDevice(2, uidBenar) 2-arg — harus TRUE", cd2.data2, 2);

  // uid SALAH untuk slave 2 (pakai calldata slave1's 2-arg tapi ID diganti manual TIDAK dilakukan —
  // kita pakai uid dari slave1 generation, yang beda kontennya karena uid contoh sama tapi
  // untuk membuktikan mismatch kita perlu uid berbeda: pakai uid all-zero via slaveId=2 tapi ganti isi uid)
  const wrongUidCalldata = cd2.data2.slice(0, -64) + "00".repeat(32); // 64 hex char terakhir = uid, dipaksa jadi 0
  await testEthCall("verifyDevice(2, uidSALAH=0) 2-arg — harus FALSE", wrongUidCalldata, 2);

  console.log("| Panggilan | Selector | ID tertanam di calldata asli | ID diharapkan | Cocok? | result mentah | verified (logika parsing cpp) |");
  console.log("|---|---|---|---|---|---|---|");
  for (const r of results) {
    console.log(
      `| ${r.label} | ${r.selector} | ${r.embeddedSlaveId} | ${r.expectDecodedSlaveId} | ${r.match ? "YA" : "TIDAK"} | ${r.rawResult} | ${r.verified} |`
    );
  }

  await server.disconnect();
}

main().catch((e) => {
  console.error("FATAL:", e);
  process.exit(1);
});
