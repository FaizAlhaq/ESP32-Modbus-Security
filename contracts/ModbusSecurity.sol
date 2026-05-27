// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

// ============================================================
//  ModbusSecurity.sol
//
//  Tiga fungsi utama yang dipanggil oleh ESP32:
//    verifyDevice()    — cek apakah slave ada di whitelist
//    logTransaction()  — catat transaksi valid
//    logAnomaly()      — catat anomali keamanan
//
//  Fungsi admin (dipanggil via Remix / Truffle):
//    addDevice()       — daftarkan slave ke whitelist
//    removeDevice()    — hapus slave dari whitelist
// ============================================================

contract ModbusSecurity {

    address public owner;

    // Whitelist: slave_id => terdaftar?
    mapping(uint8 => bool) public whitelist;

    // ---- Events (tersimpan di blockchain log) ----
    event DeviceAdded(uint8 indexed slaveId);
    event DeviceRemoved(uint8 indexed slaveId);
    event TransactionLogged(
        uint8  indexed slaveId,
        string txData,
        string txHash,
        uint256 timestamp
    );
    event AnomalyLogged(
        uint8  indexed slaveId,
        uint8  anomalyType,
        string detail,
        uint256 timestamp
    );

    modifier onlyOwner() {
        require(msg.sender == owner, "Hanya owner");
        _;
    }

    constructor() {
        owner = msg.sender;
    }

    // ---- Admin: kelola whitelist ----

    function addDevice(uint8 slaveId) external onlyOwner {
        whitelist[slaveId] = true;
        emit DeviceAdded(slaveId);
    }

    function removeDevice(uint8 slaveId) external onlyOwner {
        whitelist[slaveId] = false;
        emit DeviceRemoved(slaveId);
    }

    // ---- Dipanggil oleh ESP32 ----

    // Cek apakah slave terdaftar (eth_call, tidak membutuhkan gas)
    function verifyDevice(uint8 slaveId) external view returns (bool) {
        return whitelist[slaveId];
    }

    // Catat transaksi debit air yang valid
    function logTransaction(
        string calldata txData,
        string calldata txHash
    ) external {
        emit TransactionLogged(0, txData, txHash, block.timestamp);
    }

    // Catat anomali keamanan
    function logAnomaly(
        uint8  slaveId,
        uint8  anomalyType,
        string calldata detail
    ) external {
        emit AnomalyLogged(slaveId, anomalyType, detail, block.timestamp);
    }
}
