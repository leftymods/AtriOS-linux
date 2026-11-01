package org.atrios.companion.data.bluetooth

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.Context
import com.google.gson.Gson
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import org.atrios.companion.data.model.StationStatusResponse
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStream
import java.util.UUID

sealed class BtConnectionState {
    object Disconnected : BtConnectionState()
    object Scanning : BtConnectionState()
    data class Connecting(val deviceName: String) : BtConnectionState()
    data class Connected(val deviceName: String, val address: String) : BtConnectionState()
    data class Error(val message: String) : BtConnectionState()
}

data class PairedBluetoothDevice(
    val name: String,
    val address: String,
    val isStation: Boolean
)

class BluetoothStationManager(private val context: Context) {
    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val adapter: BluetoothAdapter? = bluetoothManager?.adapter
    private val gson = Gson()

    private val _connectionState = MutableStateFlow<BtConnectionState>(BtConnectionState.Disconnected)
    val connectionState: StateFlow<BtConnectionState> = _connectionState.asStateFlow()

    private val _stationStatus = MutableStateFlow<StationStatusResponse?>(null)
    val stationStatus: StateFlow<StationStatusResponse?> = _stationStatus.asStateFlow()

    private val _pairedDevices = MutableStateFlow<List<PairedBluetoothDevice>>(emptyList())
    val pairedDevices: StateFlow<List<PairedBluetoothDevice>> = _pairedDevices.asStateFlow()

    private var socket: BluetoothSocket? = null
    private var outputStream: OutputStream? = null
    private var ioJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    // Standard Bluetooth Serial Port Profile (SPP) UUID
    private val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

    @SuppressLint("MissingPermission")
    fun refreshPairedDevices() {
        if (adapter == null || !adapter.isEnabled) {
            _pairedDevices.value = emptyList()
            return
        }
        val devices = adapter.bondedDevices?.map { dev ->
            val name = dev.name ?: "Unknown Device"
            PairedBluetoothDevice(
                name = name,
                address = dev.address,
                isStation = name.contains("Yandex", ignoreCase = true) || name.contains("Station", ignoreCase = true)
            )
        } ?: emptyList()
        _pairedDevices.value = devices
    }

    @SuppressLint("MissingPermission")
    fun autoConnect() {
        refreshPairedDevices()
        val station = _pairedDevices.value.firstOrNull { it.isStation }
        if (station != null) {
            connectToDevice(station.address)
        }
    }

    @SuppressLint("MissingPermission")
    fun connectToDevice(address: String) {
        if (adapter == null || !adapter.isEnabled) {
            _connectionState.value = BtConnectionState.Error("Bluetooth is turned off")
            return
        }
        val device: BluetoothDevice = try {
            adapter.getRemoteDevice(address)
        } catch (e: Exception) {
            _connectionState.value = BtConnectionState.Error("Invalid Bluetooth address: $address")
            return
        }

        disconnect()

        val devName = device.name ?: "Yandex Station Max"
        _connectionState.value = BtConnectionState.Connecting(devName)

        scope.launch {
            try {
                // Try standard SPP UUID first
                var connectedSocket: BluetoothSocket? = null
                try {
                    val s = device.createRfcommSocketToServiceRecord(SPP_UUID)
                    s.connect()
                    connectedSocket = s
                } catch (e1: Exception) {
                    // Fallback to reflection on RFCOMM channel 1
                    try {
                        val method = device.javaClass.getMethod("createRfcommSocket", Int::class.javaPrimitiveType)
                        val s = method.invoke(device, 1) as BluetoothSocket
                        s.connect()
                        connectedSocket = s
                    } catch (e2: Exception) {
                        throw Exception("Failed to connect via RFCOMM: ${e1.message}")
                    }
                }

                socket = connectedSocket
                outputStream = connectedSocket.outputStream
                _connectionState.value = BtConnectionState.Connected(devName, address)

                startIoLoop(connectedSocket)
            } catch (e: Exception) {
                _connectionState.value = BtConnectionState.Error(e.localizedMessage ?: "Connection failed")
            }
        }
    }

    private fun startIoLoop(sock: BluetoothSocket) {
        ioJob?.cancel()
        ioJob = scope.launch {
            try {
                val reader = BufferedReader(InputStreamReader(sock.inputStream, Charsets.UTF_8))
                // Request initial status
                sendCommand("get_status")

                while (isActive) {
                    val line = reader.readLine() ?: break
                    if (line.isBlank()) continue
                    handleIncomingPacket(line)
                }
            } catch (e: Exception) {
                // Connection closed or broken
            } finally {
                disconnect()
            }
        }
    }

    private fun handleIncomingPacket(rawJson: String) {
        try {
            val status = gson.fromJson(rawJson, StationStatusResponse::class.java)
            if (status.device != null && status.audio != null) {
                _stationStatus.value = status
            }
        } catch (e: Exception) {
            // Non-status or auxiliary packet
        }
    }

    fun sendCommand(cmd: String, extra: Map<String, Any> = emptyMap()) {
        scope.launch {
            try {
                val map = mutableMapOf<String, Any>("cmd" to cmd)
                map.putAll(extra)
                val json = gson.toJson(map) + "\n"
                outputStream?.write(json.toByteArray(Charsets.UTF_8))
                outputStream?.flush()
            } catch (e: Exception) {
                // Write failure
            }
        }
    }

    fun disconnect() {
        ioJob?.cancel()
        try {
            outputStream?.close()
        } catch (e: Exception) {}
        try {
            socket?.close()
        } catch (e: Exception) {}
        outputStream = null
        socket = null
        _connectionState.value = BtConnectionState.Disconnected
    }
}
