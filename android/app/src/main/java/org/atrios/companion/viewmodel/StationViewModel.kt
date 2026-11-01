package org.atrios.companion.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import org.atrios.companion.data.bluetooth.BtConnectionState
import org.atrios.companion.data.bluetooth.BluetoothStationManager
import org.atrios.companion.data.bluetooth.PairedBluetoothDevice
import org.atrios.companion.data.model.StationStatusResponse

data class StationUiState(
    val btState: BtConnectionState = BtConnectionState.Disconnected,
    val isConnected: Boolean = false,
    val deviceName: String = "Yandex Station Max",
    val pairedDevices: List<PairedBluetoothDevice> = emptyList(),
    val status: StationStatusResponse? = null,
    val errorMessage: String? = null,
    val actionSuccessMessage: String? = null
)

class StationViewModel(application: Application) : AndroidViewModel(application) {

    private val btManager = BluetoothStationManager(application.applicationContext)

    private val _uiState = MutableStateFlow(StationUiState())
    val uiState: StateFlow<StationUiState> = _uiState.asStateFlow()

    init {
        // Observe Bluetooth Connection State
        viewModelScope.launch {
            btManager.connectionState.collect { state ->
                val connected = state is BtConnectionState.Connected
                val devName = if (state is BtConnectionState.Connected) state.deviceName else "Yandex Station Max"
                val err = if (state is BtConnectionState.Error) state.message else null
                _uiState.update {
                    it.copy(
                        btState = state,
                        isConnected = connected,
                        deviceName = devName,
                        errorMessage = err
                    )
                }
            }
        }

        // Observe Live Status received over Bluetooth
        viewModelScope.launch {
            btManager.stationStatus.collect { st ->
                if (st != null) {
                    _uiState.update { it.copy(status = st) }
                }
            }
        }

        // Observe Paired Bluetooth Devices
        viewModelScope.launch {
            btManager.pairedDevices.collect { list ->
                _uiState.update { it.copy(pairedDevices = list) }
            }
        }

        // Initial scan of paired Bluetooth devices & auto-connect
        refreshDevices()
        autoConnect()
    }

    fun refreshDevices() {
        btManager.refreshPairedDevices()
    }

    fun autoConnect() {
        btManager.autoConnect()
    }

    fun connectToDevice(address: String) {
        btManager.connectToDevice(address)
    }

    fun disconnect() {
        btManager.disconnect()
    }

    fun setVolume(volume: Int) {
        val cur = _uiState.value.status
        if (cur != null) {
            val updated = cur.copy(audio = cur.audio.copy(volume = volume, mute = (volume == 0)))
            _uiState.update { it.copy(status = updated) }
        }
        btManager.sendCommand("set_volume", mapOf("volume" to volume))
    }

    fun toggleMute() {
        val currentMute = _uiState.value.status?.audio?.mute ?: false
        val newMute = !currentMute
        btManager.sendCommand("set_mute", mapOf("mute" to newMute))
    }

    fun setLedMode(mode: String, brightness: Int) {
        btManager.sendCommand("set_led", mapOf("mode" to mode, "brightness" to brightness))
        _uiState.update { it.copy(actionSuccessMessage = "LED Ring updated: $mode") }
    }

    fun setScreenMode(mode: String, brightness: Int) {
        btManager.sendCommand("set_screen", mapOf("mode" to mode, "brightness" to brightness))
        _uiState.update { it.copy(actionSuccessMessage = "Front screen updated: $mode") }
    }

    fun configureWifiOverBluetooth(ssid: String, psk: String) {
        if (ssid.isNotBlank()) {
            btManager.sendCommand("wifi_connect", mapOf("ssid" to ssid, "psk" to psk))
            _uiState.update { it.copy(actionSuccessMessage = "Sent Wi-Fi credentials over Bluetooth") }
        }
    }

    fun testAudio() {
        btManager.sendCommand("audio_test")
        _uiState.update { it.copy(actionSuccessMessage = "Audio test chime triggered over Bluetooth") }
    }

    fun reboot() {
        btManager.sendCommand("reboot")
        _uiState.update { it.copy(actionSuccessMessage = "Reboot command sent over Bluetooth") }
    }

    fun requestStatus() {
        btManager.sendCommand("get_status")
    }

    fun clearMessages() {
        _uiState.update { it.copy(errorMessage = null, actionSuccessMessage = null) }
    }

    override fun onCleared() {
        super.onCleared()
        btManager.disconnect()
    }
}
