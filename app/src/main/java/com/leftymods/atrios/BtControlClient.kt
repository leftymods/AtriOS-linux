package com.leftymods.atrios

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import com.leftymods.atrios.model.AiStatus
import com.leftymods.atrios.model.AlarmItem
import com.leftymods.atrios.model.DeviceCapabilities
import com.leftymods.atrios.model.OtaStatus
import com.leftymods.atrios.model.SpeakerStatus
import com.leftymods.atrios.model.TimerItem
import com.leftymods.atrios.model.YMusicStatus
import com.leftymods.atrios.model.YMusicAlbum
import com.leftymods.atrios.model.YMusicTrack
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.net.URL
import javax.net.ssl.HttpsURLConnection
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.util.UUID

class BtControlClient(
    private val scope: CoroutineScope,
    private val onStatus: (SpeakerStatus) -> Unit,
    private val onVolumePush: (Int, Boolean) -> Unit,
    private val onButtonsPush: (Boolean, Boolean) -> Unit = { _, _ -> },
    private val onOtaUpdate: (OtaStatus) -> Unit = {},
    private val onYMusicUpdate: (YMusicStatus) -> Unit = {},
    private val onYMusicSearchResults: (List<YMusicTrack>, List<YMusicAlbum>) -> Unit = { _, _ -> },
    private val onTimersUpdate: (List<TimerItem>) -> Unit = {},
    private val onAlarmsUpdate: (List<AlarmItem>) -> Unit = {},
    private val onBtSpeakerPush: (Boolean) -> Unit = {},
    private val onRelayInfo: (String, String) -> Unit = { _, _ -> },
    private val onMessage: (String, Boolean) -> Unit = { _, _ -> },
    private val onConnectionState: (Boolean, String, BluetoothDevice?) -> Unit
) {
    companion object {
        val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
        private val DEVICE_ID_RE = Regex("^[a-zA-Z0-9_-]{3,48}$")
    }

    private var socket: BluetoothSocket? = null
    private var writer: OutputStreamWriter? = null
    private var readJob: Job? = null
    private val writeMutex = Mutex()
    private var activeDevice: BluetoothDevice? = null
    private var relayJob: Job? = null
    @Volatile private var relayGateway: String = ""
    @Volatile private var relayDeviceId: String = ""
    @Volatile private var relayToken: String = ""
    @Volatile private var relayActive: Boolean = false

    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        disconnect(silent = true)
        activeDevice = device
        readJob = scope.launch(Dispatchers.IO) {
            try {
                withContext(Dispatchers.Main) {
                    onConnectionState(false, "Подключение к ${device.name ?: device.address}...", device)
                }
                val sock = device.createRfcommSocketToServiceRecord(SPP_UUID)
                sock.connect()
                socket = sock
                writer = OutputStreamWriter(sock.outputStream, Charsets.UTF_8)
                withContext(Dispatchers.Main) {
                    onConnectionState(true, "Подключено: ${device.name ?: device.address}", device)
                }

                // Initial sync: status, OTA, Yandex Music, timers, alarms
                writeJson(JSONObject().put("cmd", "get_status"))
                writeJson(JSONObject().put("cmd", "ota_status"))
                writeJson(JSONObject().put("cmd", "ymusic_status"))
                writeJson(JSONObject().put("cmd", "get_timers"))
                writeJson(JSONObject().put("cmd", "get_alarms"))
                writeJson(JSONObject().put("cmd", "get_relay_info"))

                val reader = BufferedReader(InputStreamReader(sock.inputStream, Charsets.UTF_8))
                while (true) {
                    val line = reader.readLine() ?: break
                    if (line.isBlank()) continue
                    try {
                        handlePacket(JSONObject(line))
                    } catch (_: Exception) {
                        // Ignore malformed frames cleanly
                    }
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    onConnectionState(false, "Отключено: ${e.message ?: "ошибка соединения"}", null)
                }
            } finally {
                closeQuietly()
            }
        }
    }


    fun connectRelay(gatewayUrl: String, deviceId: String, token: String) {
        val cleanUrl = gatewayUrl.trim().trimEnd('/')
        val cleanDid = deviceId.trim()
        val cleanTok = token.trim()
        if (!cleanUrl.startsWith("https://")) {
            onMessage("Только шифрованный HTTPS-адрес шлюза", true)
            return
        }
        if (!DEVICE_ID_RE.matches(cleanDid)) {
            onMessage("Некорректный ID колонки (пройдите настройку по Bluetooth)", true)
            return
        }
        if (cleanTok.isEmpty()) {
            onMessage("Укажите токен доступа к шлюзу", true)
            return
        }
        disconnect(silent = true)
        relayGateway = cleanUrl
        relayDeviceId = cleanDid
        relayToken = cleanTok
        relayActive = true
        relayJob = scope.launch(Dispatchers.IO) {
            try {
                withContext(Dispatchers.Main) {
                    onConnectionState(false, "Wi-Fi подключение к $cleanDid...", null)
                }
                val first = relayHttpGet("$cleanUrl/relay/v1/app/$cleanDid/status", cleanTok)
                val firstStatus = first.optJSONObject("status")
                val online = first.optBoolean("online", false)
                withContext(Dispatchers.Main) {
                    val label = if (online) "Wi-Fi (TLS): $cleanDid" else "Wi-Fi (ожидание колонки): $cleanDid"
                    onConnectionState(true, label, null)
                }
                if (firstStatus != null && firstStatus.length() > 0) {
                    handlePacket(firstStatus)
                }
                // Sync timers/alarms/ymusic over encrypted relay
                sendCommand(JSONObject().put("cmd", "get_timers"))
                sendCommand(JSONObject().put("cmd", "get_alarms"))
                sendCommand(JSONObject().put("cmd", "ymusic_status"))

                while (isActive && relayActive) {
                    delay(4000)
                    try {
                        val resp = relayHttpGet("$cleanUrl/relay/v1/app/$cleanDid/status", cleanTok)
                        val st = resp.optJSONObject("status")
                        if (st != null && st.length() > 0) {
                            handlePacket(st)
                        }
                    } catch (_: Exception) {
                        // Transient network hiccup; keep retrying
                    }
                }
            } catch (e: Exception) {
                relayActive = false
                withContext(Dispatchers.Main) {
                    onConnectionState(false, "Wi-Fi релей: ${e.message ?: "ошибка"}", null)
                }
            }
        }
    }

    private fun relayHttpGet(urlStr: String, token: String): JSONObject {
        val conn = (URL(urlStr).openConnection() as HttpsURLConnection).apply {
            requestMethod = "GET"
            connectTimeout = 8000
            readTimeout = 10000
            setRequestProperty("Authorization", "Bearer $token")
            setRequestProperty("Accept", "application/json")
        }
        try {
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val body = stream?.bufferedReader(Charsets.UTF_8)?.readText().orEmpty()
            if (code !in 200..299) {
                throw IllegalStateException("HTTP $code")
            }
            return JSONObject(body)
        } finally {
            conn.disconnect()
        }
    }

    private fun relayHttpPost(urlStr: String, token: String, payload: JSONObject): JSONObject {
        val conn = (URL(urlStr).openConnection() as HttpsURLConnection).apply {
            requestMethod = "POST"
            doOutput = true
            connectTimeout = 8000
            readTimeout = 15000
            setRequestProperty("Authorization", "Bearer $token")
            setRequestProperty("Content-Type", "application/json; charset=utf-8")
            setRequestProperty("Accept", "application/json")
        }
        try {
            conn.outputStream.use { it.write(payload.toString().toByteArray(Charsets.UTF_8)) }
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val body = stream?.bufferedReader(Charsets.UTF_8)?.readText().orEmpty()
            if (code !in 200..299) {
                throw IllegalStateException("HTTP $code")
            }
            return JSONObject(body)
        } finally {
            conn.disconnect()
        }
    }

    fun sendCommand(payload: JSONObject) {
        scope.launch(Dispatchers.IO) {
            if (relayActive && writer == null) {
                try {
                    val url = "$relayGateway/relay/v1/app/$relayDeviceId/cmd"
                    val resp = relayHttpPost(url, relayToken, payload)
                    val reply = resp.optJSONObject("reply")
                    if (reply != null) {
                        handlePacket(reply)
                    }
                } catch (e: Exception) {
                    withContext(Dispatchers.Main) {
                        onMessage("Ошибка Wi-Fi команды: ${e.message ?: "таймаут"}", true)
                    }
                }
            } else {
                writeJson(payload)
            }
        }
    }

    private suspend fun writeJson(payload: JSONObject) {
        writeMutex.withLock {
            try {
                writer?.let {
                    it.write(payload.toString() + "\n")
                    it.flush()
                }
            } catch (_: Exception) {
            }
        }
    }

    // ---- Typed command helpers ----

    fun requestStatus() = sendCommand(JSONObject().put("cmd", "get_status"))

    fun setVolume(volume: Int) = sendCommand(
        JSONObject().put("cmd", "set_volume").put("volume", volume.coerceIn(0, 100))
    )

    fun setMute(mute: Boolean) = sendCommand(
        JSONObject().put("cmd", "set_mute").put("mute", mute)
    )

    fun audioTest() = sendCommand(JSONObject().put("cmd", "audio_test"))

    fun setScreen(mode: String, state: String? = null, text: String? = null) {
        val obj = JSONObject().put("cmd", "set_screen").put("mode", mode)
        if (!state.isNullOrBlank()) obj.put("state", state)
        if (!text.isNullOrBlank()) obj.put("text", text)
        sendCommand(obj)
    }

    fun setRing(mode: String) = sendCommand(
        JSONObject().put("cmd", "set_ring").put("mode", mode)
    )

    // OTA (#7 contract)
    fun otaStatus() = sendCommand(JSONObject().put("cmd", "ota_status"))

    fun otaCheck() = sendCommand(JSONObject().put("cmd", "ota_check"))

    fun otaApply(reboot: Boolean = true) = sendCommand(
        JSONObject().put("cmd", "ota_apply").put("reboot", reboot)
    )

    fun otaRollback(reboot: Boolean = false) = sendCommand(
        JSONObject().put("cmd", "ota_rollback").put("reboot", reboot)
    )

    // Yandex Music (#30 contract)
    fun ymusicStatus() = sendCommand(JSONObject().put("cmd", "ymusic_status"))

    fun ymusicLogin(token: String) = sendCommand(
        JSONObject().put("cmd", "ymusic_login").put("token", token.trim())
    )

    fun ymusicLogout() = sendCommand(JSONObject().put("cmd", "ymusic_logout"))

    fun ymusicPlay(mode: String, query: String? = null) {
        val obj = JSONObject().put("cmd", "ymusic_play").put("mode", mode)
        if (!query.isNullOrBlank()) obj.put("query", query.trim())
        sendCommand(obj)
    }

    fun ymusicSearch(query: String) = sendCommand(JSONObject().put("cmd", "ymusic_search").put("query", query))

    fun ymusicControl(action: String) = sendCommand(
        JSONObject().put("cmd", "ymusic_control").put("action", action)
    )

    // Timers (#10 contract)
    fun getTimers() = sendCommand(JSONObject().put("cmd", "get_timers"))

    fun setTimer(secs: Int) = sendCommand(
        JSONObject().put("cmd", "set_timer").put("secs", secs.coerceIn(1, 86400))
    )

    fun cancelTimers(id: Int? = null) {
        val obj = JSONObject().put("cmd", "cancel_timers")
        if (id != null) obj.put("id", id)
        sendCommand(obj)
    }

    // Alarms (#10 contract)
    fun getAlarms() = sendCommand(JSONObject().put("cmd", "get_alarms"))

    fun setAlarm(hour: Int, minute: Int, enabled: Boolean = true) = sendCommand(
        JSONObject()
            .put("cmd", "set_alarm")
            .put("hour", hour.coerceIn(0, 23))
            .put("minute", minute.coerceIn(0, 59))
            .put("enabled", enabled)
    )

    fun cancelAlarms(id: Int? = null) {
        val obj = JSONObject().put("cmd", "cancel_alarms")
        if (id != null) obj.put("id", id)
        sendCommand(obj)
    }

    // Time & Timezone (#10 contract)
    fun ntpSync() = sendCommand(JSONObject().put("cmd", "ntp_sync"))

    fun setTimezone(tz: String) = sendCommand(
        JSONObject().put("cmd", "set_timezone").put("tz", tz.trim())
    )

    // Bluetooth (#10 contract)
    fun btPair(seconds: Int = 120) = sendCommand(
        JSONObject().put("cmd", "bt_pair").put("seconds", seconds.coerceIn(10, 300))
    )

    fun btHide() = sendCommand(JSONObject().put("cmd", "bt_hide"))

    fun btUnpair(mac: String) = sendCommand(
        JSONObject().put("cmd", "bt_unpair").put("mac", mac)
    )

    fun setBtSpeakerMode(enabled: Boolean) = sendCommand(
        JSONObject().put("cmd", "bt_speaker").put("enabled", enabled)
    )

    // AI & Qwen3-TTS (#10 contract)
    fun setAi(
        enabled: Boolean? = null,
        mode: String? = null,
        server: String? = null,
        wakeWord: String? = null,
        ttsVoice: String? = null,
        ttsEmotion: String? = null
    ) {
        val obj = JSONObject().put("cmd", "set_ai")
        if (enabled != null) obj.put("enabled", enabled)
        if (!mode.isNullOrBlank()) obj.put("mode", mode)
        if (server != null) obj.put("server", server.trim())
        if (wakeWord != null) obj.put("wake_word", wakeWord.trim())
        if (!ttsVoice.isNullOrBlank()) obj.put("tts_voice", ttsVoice.trim())
        if (!ttsEmotion.isNullOrBlank()) obj.put("tts_emotion", ttsEmotion.trim())
        sendCommand(obj)
    }

    // Wi-Fi & System
    fun wifiConnect(ssid: String, psk: String) = sendCommand(
        JSONObject().put("cmd", "wifi_connect").put("ssid", ssid).put("psk", psk)
    )

    fun reboot() = sendCommand(JSONObject().put("cmd", "reboot"))

    fun disconnect(silent: Boolean = false) {
        relayActive = false
        relayJob?.cancel()
        relayJob = null
        readJob?.cancel()
        readJob = null
        activeDevice = null
        closeQuietly()
        if (!silent) {
            scope.launch(Dispatchers.Main) {
                onConnectionState(false, "Не подключено", null)
            }
        }
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
                val model = dev.optString("model", "AtriOS Speaker")
                val codename = dev.optString("codename", "")
                val caps = DeviceCapabilities.infer(
                    codename = codename,
                    model = model,
                    explicit = dev.optJSONObject("capabilities")
                )
                val ota = OtaStatus.fromJson(json.optJSONObject("ota"))
                val ai = AiStatus.fromJson(json.optJSONObject("ai"))
                val ymusic = YMusicStatus.fromJson(json.optJSONObject("ymusic"))
                val status = SpeakerStatus(
                    model = model,
                    codename = codename,
                    rev = dev.optString("rev", ""),
                    soc = dev.optString("soc", ""),
                    arch = dev.optString("arch", ""),
                    os = dev.optString("os", "AtriOS"),
                    fwVersion = dev.optString("fw_version", ota.currentVersion),
                    kernel = dev.optString("kernel", ""),
                    localTime = dev.optString("local_time", ""),
                    timezone = dev.optString("timezone", "UTC"),
                    cpuTemp = dev.optDouble("cpu_temperature", 0.0),
                    lux = if (dev.isNull("lux")) null else dev.optInt("lux"),
                    memUsedMb = mem.optInt("used_mb", 0),
                    memTotalMb = mem.optInt("total_mb", 0),
                    uptimeSec = dev.optInt("uptime_seconds", 0),
                    volume = audio.optInt("volume", 60),
                    mute = audio.optBoolean("mute", false),
                    speakersDesc = audio.optString("speakers", ""),
                    micMuted = buttons.optBoolean("mic_muted", false),
                    actionPressed = buttons.optBoolean("action_pressed", false),
                    screenMode = json.optString("screen_mode", "clock"),
                    ringMode = json.optString("ring_mode", "rainbow"),
                    wifiConnected = wifi.optBoolean("connected", false),
                    wifiSsid = wifi.optString("ssid", ""),
                    wifiIp = wifi.optString("ip", ""),
                    btSpeakerMode = bt.optBoolean("is_speaker_mode", false),
                    btHidden = bt.optBoolean("hidden", false),
                    btDiscoverable = bt.optBoolean("discoverable", false),
                    btPairRemainingSec = bt.optInt("pair_remaining_sec", 0),
                    pairedDevices = paired,
                    ai = ai,
                    ota = ota,
                    ymusic = ymusic,
                    capabilities = caps
                )
                withContext(Dispatchers.Main) { onStatus(status) }
            }
            "volume_change", "volume_response" -> {
                val vol = json.optInt("volume", 60)
                val mute = json.optBoolean("mute", vol == 0)
                withContext(Dispatchers.Main) { onVolumePush(vol, mute) }
            }
            "buttons_change" -> {
                val ap = json.optBoolean("action_pressed", false)
                val mm = json.optBoolean("mic_muted", false)
                withContext(Dispatchers.Main) { onButtonsPush(ap, mm) }
            }
            "ota_status", "ota_check_response", "ota_apply_response", "ota_rollback_response" -> {
                val otaObj = json.optJSONObject("ota")
                val msg = json.optString("message", "")
                val ok = json.optBoolean("success", true)
                withContext(Dispatchers.Main) {
                    if (otaObj != null) onOtaUpdate(OtaStatus.fromJson(otaObj))
                    if (msg.isNotBlank() && json.optString("type") != "ota_status") {
                        onMessage(msg, !ok)
                    }
                }
            }
            "ymusic_search_response" -> {
                val tracks = YMusicTrack.fromJsonArray(json.optJSONArray("tracks"))
                val albums = YMusicAlbum.fromJsonArray(json.optJSONArray("albums"))
                withContext(Dispatchers.Main) {
                    onYMusicSearchResults(tracks, albums)
                }
            }
            "ymusic_status", "ymusic_login_response", "ymusic_logout_response",
            "ymusic_play_response", "ymusic_control_response" -> {
                val ymObj = json.optJSONObject("ymusic")
                val msg = json.optString("message", "")
                val ok = json.optBoolean("success", true)
                withContext(Dispatchers.Main) {
                    if (ymObj != null) onYMusicUpdate(YMusicStatus.fromJson(ymObj))
                    if (msg.isNotBlank() && json.optString("type") != "ymusic_status") {
                        onMessage(msg, !ok)
                    }
                }
            }
            "timers_response" -> {
                val timers = TimerItem.fromJsonArray(json.optJSONArray("timers"))
                val msg = json.optString("message", "")
                val ok = json.optBoolean("success", true)
                withContext(Dispatchers.Main) {
                    onTimersUpdate(timers)
                    if (msg.isNotBlank()) onMessage(msg, !ok)
                }
            }
            "alarms_response" -> {
                val alarms = AlarmItem.fromJsonArray(json.optJSONArray("alarms"))
                val msg = json.optString("message", "")
                val ok = json.optBoolean("success", true)
                withContext(Dispatchers.Main) {
                    onAlarmsUpdate(alarms)
                    if (msg.isNotBlank()) onMessage(msg, !ok)
                }
            }
            "action_response" -> {
                val msg = json.optString("message", "")
                val ok = json.optBoolean("success", true)
                val hasSpeakerMode = json.has("is_speaker_mode")
                val speakerMode = json.optBoolean("is_speaker_mode", false)
                withContext(Dispatchers.Main) {
                    if (hasSpeakerMode) onBtSpeakerPush(speakerMode)
                    if (msg.isNotBlank()) onMessage(msg, !ok)
                }
            }
            "relay_info" -> {
                val did = json.optString("device_id", "").trim()
                val gw = json.optString("gateway", "").trim()
                if (did.isNotBlank()) {
                    withContext(Dispatchers.Main) { onRelayInfo(did, gw) }
                }
            }
            "error" -> {
                val msg = json.optString("message", "Ошибка команды")
                // Gracefully translate unknown commands from older firmware
                val friendly = if (msg.startsWith("Unknown command:", ignoreCase = true)) {
                    val cmdName = msg.substringAfter(":").trim()
                    "Команда «$cmdName» не поддерживается текущей прошивкой колонки"
                } else {
                    msg
                }
                withContext(Dispatchers.Main) { onMessage(friendly, true) }
            }
        }
    }
}
