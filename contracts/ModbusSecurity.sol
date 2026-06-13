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
    mapping(uint8 => bool)    public whitelist;
    // UID hardware per slave (96-bit disimpan sebagai uint256)
    mapping(uint8 => uint256) public deviceUID;

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

    function addDevice(uint8 slaveId, uint256 uid) external onlyOwner {
        whitelist[slaveId]  = true;
        deviceUID[slaveId]  = uid;
        emit DeviceAdded(slaveId);
    }

    function removeDevice(uint8 slaveId) external onlyOwner {
        whitelist[slaveId] = false;
        deviceUID[slaveId] = 0;
        emit DeviceRemoved(slaveId);
    }

    // ---- Dipanggil oleh ESP32 ----

    // Cek whitelist saja (dipanggil tiap poll oleh firmware)
    function verifyDevice(uint8 slaveId) external view returns (bool) {
        return whitelist[slaveId];
    }

    // Cek whitelist DAN kecocokan UID (dipanggil saat first-contact/reconnect)
    function verifyDevice(uint8 slaveId, uint256 uid) external view returns (bool) {
        return whitelist[slaveId] && deviceUID[slaveId] == uid;
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
