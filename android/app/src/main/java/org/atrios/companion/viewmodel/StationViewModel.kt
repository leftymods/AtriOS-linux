package org.atrios.companion.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.atrios.companion.data.model.AccessPointItem
import org.atrios.companion.data.model.StationStatusResponse
import org.atrios.companion.data.repository.StationRepository

data class StationUiState(
    val isConnecting: Boolean = false,
    val isConnected: Boolean = false,
    val hostIp: String = "192.168.1.100",
    val status: StationStatusResponse? = null,
    val errorMessage: String? = null,
    val isScanningWifi: Boolean = false,
    val wifiAps: List<AccessPointItem> = emptyList(),
    val actionSuccessMessage: String? = null
)

class StationViewModel(
    private val repository: StationRepository = StationRepository()
) : ViewModel() {

    private val _uiState = MutableStateFlow(StationUiState(hostIp = repository.getHost()))
    val uiState: StateFlow<StationUiState> = _uiState.asStateFlow()

    private var pollJob: Job? = null

    init {
        startPolling()
    }

    fun updateHostIp(ip: String) {
        _uiState.update { it.copy(hostIp = ip) }
        repository.updateHost(ip)
        refreshStatus()
    }

    fun refreshStatus() {
        viewModelScope.launch {
            _uiState.update { it.copy(isConnecting = true, errorMessage = null) }
            val result = repository.fetchStatus()
            result.onSuccess { data ->
                _uiState.update {
                    it.copy(
                        isConnecting = false,
                        isConnected = true,
                        status = data,
                        errorMessage = null
                    )
                }
            }.onFailure { err ->
                _uiState.update {
                    it.copy(
                        isConnecting = false,
                        isConnected = false,
                        errorMessage = err.localizedMessage ?: "Connection error"
                    )
                }
            }
        }
    }

    fun setVolume(volume: Int) {
        // Optimistic UI update
        val cur = _uiState.value.status
        if (cur != null) {
            val updated = cur.copy(audio = cur.audio.copy(volume = volume, mute = volume == 0))
            _uiState.update { it.copy(status = updated) }
        }
        viewModelScope.launch {
            repository.setVolume(volume).onFailure { err ->
                _uiState.update { it.copy(errorMessage = "Failed to set volume: ${err.message}") }
            }
        }
    }

    fun toggleMute() {
        val currentMute = _uiState.value.status?.audio?.mute ?: false
        val newMute = !currentMute
        viewModelScope.launch {
            repository.setMute(newMute).onSuccess {
                refreshStatus()
            }
        }
    }

    fun testAudio() {
        viewModelScope.launch {
            repository.testAudio().onSuccess {
                _uiState.update { it.copy(actionSuccessMessage = "Audio test chime triggered") }
            }.onFailure {
                _uiState.update { it.copy(errorMessage = "Audio test failed: ${it.errorMessage}") }
            }
        }
    }

    fun reboot() {
        viewModelScope.launch {
            repository.reboot().onSuccess {
                _uiState.update { it.copy(actionSuccessMessage = "Reboot command sent to station") }
            }
        }
    }

    fun scanWifi() {
        viewModelScope.launch {
            _uiState.update { it.copy(isScanningWifi = true) }
            val res = repository.scanWifi()
            res.onSuccess { list ->
                _uiState.update { it.copy(isScanningWifi = false, wifiAps = list) }
            }.onFailure {
                _uiState.update { it.copy(isScanningWifi = false) }
            }
        }
    }

    fun clearMessages() {
        _uiState.update { it.copy(errorMessage = null, actionSuccessMessage = null) }
    }

    private fun startPolling() {
        pollJob?.cancel()
        pollJob = viewModelScope.launch {
            while (true) {
                val res = repository.fetchStatus()
                res.onSuccess { data ->
                    _uiState.update { it.copy(isConnected = true, status = data) }
                }.onFailure {
                    _uiState.update { it.copy(isConnected = false) }
                }
                delay(3000)
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        pollJob?.cancel()
    }
}
