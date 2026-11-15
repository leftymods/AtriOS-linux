package com.leftymods.atrios

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.util.UUID

data class SpeakerStatus(
    val model: String = "AtriStation Max",
    val os: String = "AtriOS Nebula",
    val kernel: String = "",
    val cpuTemp: Double = 0.0,
    val lux: Int? = null,
    val memUsedMb: Int = 0,
    val memTotalMb: Int = 0,
    val uptimeSec: Int = 0,
    val volume: Int = 60,
    val mute: Boolean = false,
    val wifiConnected: Boolean = false,
    val wifiSsid: String = "",
    val wifiIp: String = "",
    val btHidden: Boolean = false,
    val btDiscoverable: Boolean = false,
    val btPairRemainingSec: Int = 0,
    val pairedDevices: List<String> = emptyList(),
    val actionPressed: Boolean = false,
    val micMuted: Boolean = false,
    val screenMode: String = "clock"
)

class BtControlClient(
    private val scope: CoroutineScope,
    private val onStatus: (SpeakerStatus) -> Unit,
    private val onVolumePush: (Int) -> Unit,
    private val onButtonsPush: (Boolean, Boolean) -> Unit,
    private val onConnectionState: (Boolean, String) -> Unit
) {
    companion object {
        val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
    }

    private var socket: BluetoothSocket? = null
    private var writer: OutputStreamWriter? = null
    private var readJob: Job? = null

    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        disconnect()
        readJob = scope.launch(Dispatchers.IO) {
            try {
                onConnectionState(false, "Подключение к ${device.name ?: device.address}...")
                val sock = device.createRfcommSocketToServiceRecord(SPP_UUID)
                sock.connect()
                socket = sock
                writer = OutputStreamWriter(sock.outputStream, Charsets.UTF_8)
                onConnectionState(true, "Подключено: ${device.name ?: device.address}")
                sendCommand(JSONObject().put("cmd", "get_status"))

                val reader = BufferedReader(InputStreamReader(sock.inputStream, Charsets.UTF_8))
                while (true) {
                    val line = reader.readLine() ?: break
                    if (line.isBlank()) continue
                    handlePacket(JSONObject(line))
                }
            } catch (e: Exception) {
                onConnectionState(false, "Отключено: ${e.message ?: "ошибка соединения"}")
            } finally {
                closeQuietly()
            }
        }
    }

    fun sendCommand(payload: JSONObject) {
        scope.launch(Dispatchers.IO) {
            try {
                writer?.let {
                    it.write(payload.toString() + "\n")
                    it.flush()
                }
            } catch (_: Exception) {
            }
        }
    }

    fun disconnect() {
        readJob?.cancel()
        readJob = null
        closeQuietly()
        onConnectionState(false, "Не подключено")
    }

    private fun closeQuietly() {
        try { writer?.close() } catch (_: Exception) {}
        try { socket?.close() } catch (_: Exception) {}
        writer = null
        socket = null
    }

    private suspend fun handlePacket(json: JSONObject) {
        when (json.optString("type")) {
            "status" -> {
                val dev = json.optJSONObject("device") ?: JSONObject()
                val mem = dev.optJSONObject("memory") ?: JSONObject()
                val audio = json.optJSONObject("audio") ?: JSONObject()
                val wifi = json.optJSONObject("wifi") ?: JSONObject()
                val bt = json.optJSONObject("bluetooth") ?: JSONObject()
                val buttons = json.optJSONObject("buttons") ?: JSONObject()
                val pairedArr: JSONArray = bt.optJSONArray("paired_devices") ?: JSONArray()
                val paired = buildList {
                    for (i in 0 until pairedArr.length()) add(pairedArr.optString(i))
                }
                val status = SpeakerStatus(
                    model = dev.optString("model", "AtriStation Max"),
                    os = dev.optString("os", "AtriOS Nebula"),
                    kernel = dev.optString("kernel", ""),
                    cpuTemp = dev.optDouble("cpu_temperature", 0.0),
                    lux = if (dev.isNull("lux")) null else dev.optInt("lux"),
                    memUsedMb = mem.optInt("used_mb", 0),
                    memTotalMb = mem.optInt("total_mb", 0),
                    uptimeSec = dev.optInt("uptime_seconds", 0),
                    volume = audio.optInt("volume", 60),
                    mute = audio.optBoolean("mute", false),
                    wifiConnected = wifi.optBoolean("connected", false),
                    wifiSsid = wifi.optString("ssid", ""),
                    wifiIp = wifi.optString("ip", ""),
                    btHidden = bt.optBoolean("hidden", false),
                    btDiscoverable = bt.optBoolean("discoverable", false),
                    btPairRemainingSec = bt.optInt("pair_remaining_sec", 0),
                    pairedDevices = paired,
                    actionPressed = buttons.optBoolean("action_pressed", false),
                    micMuted = buttons.optBoolean("mic_muted", false),
                    screenMode = json.optString("screen_mode", "clock")
                )
                withContext(Dispatchers.Main) { onStatus(status) }
            }
            "volume_change" -> {
                val vol = json.optInt("volume", 60)
                withContext(Dispatchers.Main) { onVolumePush(vol) }
            }
            "buttons_change" -> {
                val action = json.optBoolean("action_pressed", false)
                val mic = json.optBoolean("mic_muted", false)
                withContext(Dispatchers.Main) { onButtonsPush(action, mic) }
            }
        }
    }
}
