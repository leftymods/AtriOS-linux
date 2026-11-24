package com.leftymods.atrios

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.os.Handler
import android.os.Looper
import com.leftymods.atrios.model.AiStatus
import com.leftymods.atrios.model.AlarmItem
import com.leftymods.atrios.model.DeviceCapabilities
import com.leftymods.atrios.model.EqStatus
import com.leftymods.atrios.model.OtaStatus
import com.leftymods.atrios.model.SpeakerStatus
import com.leftymods.atrios.model.TimerItem
import com.leftymods.atrios.model.VqeStatus
import com.leftymods.atrios.model.YMusicStatus
import com.leftymods.atrios.model.YMusicAlbum
import com.leftymods.atrios.model.YMusicCollection
import com.leftymods.atrios.model.YMusicOrb
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
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicInteger

data class DebugLogEntry(
    val time: String,
    val level: String, // "OK", "INFO", "WARN", "ERR"
    val tag: String,   // "RELAY", "OFFLOAD", "BT", "SYS"
    val message: String
)

object DebugLogStore {
    private const val MAX_ENTRIES = 150
    private val mainHandler = Handler(Looper.getMainLooper())
    private val timeFmt = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    val entries = CopyOnWriteArrayList<DebugLogEntry>()
    val totalRequests = AtomicInteger(0)
    val errorCount = AtomicInteger(0)
    val retryCount = AtomicInteger(0)
    @Volatile var version: Int = 0
    @Volatile var onChanged: (() -> Unit)? = null

    private fun notifyChangedOnMain() {
        val cb = onChanged ?: return
        if (Looper.myLooper() == Looper.getMainLooper()) {
            cb.invoke()
        } else {
            mainHandler.post { onChanged?.invoke() }
        }
    }

    fun log(level: String, tag: String, msg: String) {
        val ts = synchronized(timeFmt) { timeFmt.format(Date()) }
        entries.add(0, DebugLogEntry(ts, level, tag, msg))
        while (entries.size > MAX_ENTRIES) {
            entries.removeAt(entries.size - 1)
        }
        if (level == "ERR") errorCount.incrementAndGet()
        if (level == "WARN") retryCount.incrementAndGet()
        version++
        notifyChangedOnMain()
    }

    fun clear() {
        entries.clear()
        totalRequests.set(0)
        errorCount.set(0)
        retryCount.set(0)
        version++
        notifyChangedOnMain()
    }

    fun dumpText(): String = entries.joinToString("\n") { "[${it.time}] [${it.level}] [${it.tag}] ${it.message}" }
}

class BtControlClient(
    private val scope: CoroutineScope,
    private val onStatus: (SpeakerStatus) -> Unit,
    private val onVolumePush: (Int, Boolean) -> Unit,
    private val onButtonsPush: (Boolean, Boolean) -> Unit = { _, _ -> },
    private val onOtaUpdate: (OtaStatus) -> Unit = {},
    private val onYMusicUpdate: (YMusicStatus) -> Unit = {},
    private val onYMusicSearchResults: (List<YMusicTrack>, List<YMusicAlbum>) -> Unit = { _, _ -> },
    private val onYMusicOrbsUpdate: (List<YMusicOrb>) -> Unit = {},
    private val onYMusicCollectionUpdate: (YMusicCollection) -> Unit = {},
    private val onTimersUpdate: (List<TimerItem>) -> Unit = {},
    private val onAlarmsUpdate: (List<AlarmItem>) -> Unit = {},
    private val onBtSpeakerPush: (Boolean) -> Unit = {},
    private val onRelayInfo: (String, String, String) -> Unit = { _, _, _ -> },
    private val onTermE2eeState: (Boolean, String) -> Unit = { _, _ -> },
    private val onTermE2eeOutput: (String) -> Unit = {},
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
    private val coalescedJobs = java.util.concurrent.ConcurrentHashMap<String, Job>()
    private var activeDevice: BluetoothDevice? = null
    private var relayJob: Job? = null
    @Volatile private var relayGateway: String = ""
    @Volatile private var relayDeviceId: String = ""
    @Volatile private var relayToken: String = ""
    @Volatile private var relayActive: Boolean = false
    @Volatile private var userDisconnected: Boolean = false
    @Volatile private var directYmToken: String = ""
    @Volatile private var directYmUid: Long = 0L
    @Volatile private var searchJob: Job? = null
    @Volatile private var termSid: String = ""
    @Volatile private var termSharedKey: ByteArray? = null
    @Volatile private var termPhonePubHex: String = ""
    @Volatile private var termPhonePriv: ByteArray? = null
    @Volatile private var termInitInFlight: Boolean = false
    @Volatile private var pendingTermCommand: String? = null

    fun isRelayActive(): Boolean = relayActive && writer == null

    fun termE2eeInit(queuedCommand: String? = null) {
        if (!queuedCommand.isNullOrBlank()) {
            pendingTermCommand = queuedCommand
        }
        if (termInitInFlight) return
        termInitInFlight = true
        val priv = E2eeCrypto.randomBytes(32)
        val pub = E2eeCrypto.x25519PublicKey(priv)
        termPhonePriv = priv
        termPhonePubHex = E2eeCrypto.hexEncode(pub)
        termSid = ""
        termSharedKey = null
        sendCommand(JSONObject().put("cmd", "term_e2ee_init"))
    }

    fun termE2eeExec(command: String) {
        val sid = termSid
        val shared = termSharedKey
        val pubHex = termPhonePubHex
        if (sid.isBlank() || shared == null || pubHex.isBlank()) {
            termE2eeInit(queuedCommand = command)
            onTermE2eeState(false, "Подключение E2EE… команда выполнится автоматически")
            return
        }
        val iv = E2eeCrypto.randomBytes(12)
        val (ct, mac) = E2eeCrypto.encrypt(shared, iv, command.toByteArray(Charsets.UTF_8))
        DebugLogStore.log("INFO", "E2EE", "tx encrypted cmd (${ct.size}B ciphertext; relay cannot read)")
        sendCommand(
            JSONObject()
                .put("cmd", "term_e2ee_exec")
                .put("sid", sid)
                .put("pub", pubHex)
                .put("iv", E2eeCrypto.hexEncode(iv))
                .put("ct", E2eeCrypto.hexEncode(ct))
                .put("mac", E2eeCrypto.hexEncode(mac))
        )
    }

    fun setDirectYMusicAuth(token: String, uid: Long = 0L) {
        if (token.isNotBlank()) directYmToken = token.trim().removePrefix("OAuth ").trim()
        if (uid > 0L) directYmUid = uid
    }

    fun setSavedRelayConfig(gatewayUrl: String, deviceId: String, token: String) {
        if (gatewayUrl.startsWith("https://")) relayGateway = gatewayUrl.trim().trimEnd('/')
        if (deviceId.isNotBlank()) relayDeviceId = deviceId.trim()
        if (token.isNotBlank()) relayToken = token.trim()
    }

    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        disconnect(silent = true)
        userDisconnected = false
        activeDevice = device
        readJob = scope.launch(Dispatchers.IO) {
            while (isActive && !userDisconnected) {
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

                    // Initial sync (get_status already embeds ota & ymusic)
                    writeJson(JSONObject().put("cmd", "get_status"))
                    writeJson(JSONObject().put("cmd", "get_timers"))
                    writeJson(JSONObject().put("cmd", "get_alarms"))
                    writeJson(JSONObject().put("cmd", "get_relay_info"))

                    val reader = BufferedReader(InputStreamReader(sock.inputStream, Charsets.UTF_8))
                    while (isActive && !userDisconnected) {
                        val line = reader.readLine() ?: break
                        if (line.isBlank()) continue
                        try {
                            handlePacket(JSONObject(line))
                        } catch (_: Exception) {
                            // Ignore malformed frames cleanly
                        }
                    }
                    closeQuietly()
                    if (userDisconnected || !isActive) return@launch
                    if (trySeamlessRelayFailover()) return@launch
                    DebugLogStore.log("WARN", "BT", "RFCOMM disconnected; auto-reconnecting in 3s")
                    withContext(Dispatchers.Main) {
                        onConnectionState(false, "Переподключение к колонке...", device)
                    }
                    delay(3000)
                } catch (e: kotlinx.coroutines.CancellationException) {
                    closeQuietly()
                    throw e
                } catch (e: Exception) {
                    closeQuietly()
                    if (userDisconnected || !isActive) return@launch
                    if (trySeamlessRelayFailover()) return@launch
                    DebugLogStore.log("WARN", "BT", "RFCOMM error: ${e.message}; retrying in 3.5s")
                    withContext(Dispatchers.Main) {
                        onConnectionState(false, "Переподключение: ${e.message ?: "ожидание сети"}", device)
                    }
                    delay(3500)
                }
            }
            closeQuietly()
        }
    }

    private suspend fun trySeamlessRelayFailover(): Boolean {
        val gw = relayGateway.trim().trimEnd('/')
        val did = relayDeviceId.trim()
        val tok = relayToken.trim()
        if (!gw.startsWith("https://") || !DEVICE_ID_RE.matches(did) || tok.isEmpty()) {
            return false
        }
        relayActive = true
        withContext(Dispatchers.Main) {
            onConnectionState(true, "Интернет (авто): $did", null)
        }
        startRelayLoop(gw, did, tok, keepConnectedOnStart = true)
        return true
    }

    private fun startRelayLoop(cleanUrl: String, cleanDid: String, cleanTok: String, keepConnectedOnStart: Boolean) {
        relayJob?.cancel()
        relayJob = scope.launch(Dispatchers.IO) {
            var initialSynced = false
            var failStreak = 0
            while (isActive && relayActive && !userDisconnected) {
                try {
                    if (!initialSynced && !keepConnectedOnStart) {
                        withContext(Dispatchers.Main) {
                            onConnectionState(false, "Интернет подключение к $cleanDid...", null)
                        }
                    }
                    val resp = relayHttpGet("$cleanUrl/relay/v1/app/$cleanDid/status", cleanTok)
                    val st = resp.optJSONObject("status")
                    val online = resp.optBoolean("online", false)
                    if (!initialSynced || failStreak > 0) {
                        failStreak = 0
                        withContext(Dispatchers.Main) {
                            val label = if (online) "Интернет (TLS): $cleanDid" else "Интернет (ожидание): $cleanDid"
                            onConnectionState(true, label, null)
                        }
                    }
                    if (st != null && st.length() > 0) {
                        handlePacket(st)
                    }
                    if (!initialSynced) {
                        initialSynced = true
                        DebugLogStore.log("OK", "RELAY", "Connected to $cleanDid (online=$online)")
                        sendCommand(JSONObject().put("cmd", "get_timers"))
                        sendCommand(JSONObject().put("cmd", "get_alarms"))
                    }
                    delay(1500)
                } catch (e: kotlinx.coroutines.CancellationException) {
                    throw e
                } catch (e: Exception) {
                    failStreak++
                    val waitMs = (1500L * failStreak).coerceAtMost(5000L)
                    DebugLogStore.log("WARN", "RELAY", "Poll hiccup #$failStreak (${e.message}); retry in ${waitMs}ms")
                    delay(waitMs)
                }
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
        userDisconnected = false
        relayGateway = cleanUrl
        relayDeviceId = cleanDid
        relayToken = cleanTok
        relayActive = true
        startRelayLoop(cleanUrl, cleanDid, cleanTok, keepConnectedOnStart = false)
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
        val t0 = System.currentTimeMillis()
        DebugLogStore.totalRequests.incrementAndGet()
        val conn = (URL(urlStr).openConnection() as HttpsURLConnection).apply {
            requestMethod = "POST"
            doOutput = true
            connectTimeout = 8000
            readTimeout = 20000
            setRequestProperty("Authorization", "Bearer $token")
            setRequestProperty("Content-Type", "application/json; charset=utf-8")
            setRequestProperty("Accept", "application/json")
        }
        try {
            conn.outputStream.use { it.write(payload.toString().toByteArray(Charsets.UTF_8)) }
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val body = stream?.bufferedReader(Charsets.UTF_8)?.readText().orEmpty()
            val dt = System.currentTimeMillis() - t0
            if (code !in 200..299) {
                val snippet = body.take(160).replace("\n", " ")
                throw IllegalStateException("HTTP $code (${dt}ms) $snippet")
            }
            return JSONObject(body)
        } finally {
            conn.disconnect()
        }
    }

    fun sendCommand(payload: JSONObject) {
        val cmdName = payload.optString("cmd", "")
        val isCoalesceable = cmdName == "set_volume" || cmdName == "set_eq"
        val job = scope.launch(Dispatchers.IO) {
            if (relayActive && writer == null) {
                val tStart = System.currentTimeMillis()
                var lastErr: Exception? = null
                val maxAttempts = if (isCoalesceable) 2 else 3
                for (attempt in 0 until maxAttempts) {
                    try {
                        val url = "$relayGateway/relay/v1/app/$relayDeviceId/cmd"
                        val resp = relayHttpPost(url, relayToken, payload)
                        val dt = System.currentTimeMillis() - tStart
                        val reply = resp.optJSONObject("reply")
                        val rtype = reply?.optString("type", "") ?: ""
                        if (cmdName != "ymusic_status" && cmdName != "get_status") {
                            DebugLogStore.log("OK", "RELAY", "cmd=$cmdName -> 200 (${dt}ms) reply=$rtype")
                        }
                        if (reply != null && rtype != "coalesced") {
                            handlePacket(reply)
                        }
                        return@launch
                    } catch (e: kotlinx.coroutines.CancellationException) {
                        throw e
                    } catch (e: Exception) {
                        lastErr = e
                        val msg = e.message ?: e.javaClass.simpleName
                        val isSilentPoll = cmdName == "ymusic_status" || cmdName == "get_status"
                        val isDnsOffline = e is java.net.UnknownHostException || msg.contains("Unable to resolve host")
                        val isRetryable = !isDnsOffline && (msg.contains("HTTP 429") || msg.contains("HTTP 502") ||
                            msg.contains("HTTP 503") || msg.contains("HTTP 504") ||
                            e is java.net.SocketTimeoutException || e is java.io.IOException)
                        if (attempt < maxAttempts - 1 && isRetryable && !isSilentPoll) {
                            val backoffMs = if (msg.contains("HTTP 429")) (450L * (attempt + 1)) else (320L * (attempt + 1))
                            DebugLogStore.log("WARN", "RELAY", "cmd=$cmdName retry #${attempt + 1} in ${backoffMs}ms: $msg")
                            delay(backoffMs)
                        } else {
                            if (!isSilentPoll || !isDnsOffline) {
                                DebugLogStore.log("ERR", "RELAY", "cmd=$cmdName FAILED: $msg")
                            }
                            break
                        }
                    }
                }
                // Don't spam UI snackbars on silent background polls or superseded live slider drags
                if (cmdName != "ymusic_status" && cmdName != "get_status" && !isCoalesceable) {
                    withContext(Dispatchers.Main) {
                        onMessage("Нет связи с сервером ($cmdName): ${lastErr?.message ?: "таймаут"}", true)
                    }
                }
            } else {
                DebugLogStore.log("INFO", "BT", "tx cmd=$cmdName")
                writeJson(payload)
            }
        }
        if (isCoalesceable) {
            coalescedJobs.put(cmdName, job)?.cancel()
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

    fun setEq(enabled: Boolean, preset: String, bands: List<Float>) {
        val arr = JSONArray()
        bands.take(5).forEach { arr.put(it.coerceIn(-12f, 12f).toDouble()) }
        sendCommand(
            JSONObject()
                .put("cmd", "set_eq")
                .put("enabled", enabled)
                .put("preset", preset)
                .put("bands", arr)
        )
    }

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

    fun ymusicLogin(token: String) {
        val clean = token.trim().removePrefix("OAuth ").trim()
        directYmToken = clean
        YandexMusicDirectClient.clearCache()
        sendCommand(JSONObject().put("cmd", "ymusic_login").put("token", clean))
    }

    fun ymusicLogout() {
        directYmToken = ""
        directYmUid = 0L
        YandexMusicDirectClient.clearCache()
        sendCommand(JSONObject().put("cmd", "ymusic_logout"))
    }

    fun ymusicPlay(mode: String, query: String? = null) {
        val obj = JSONObject().put("cmd", "ymusic_play").put("mode", mode)
        if (!query.isNullOrBlank()) obj.put("query", query.trim())
        sendCommand(obj)
    }

    fun ymusicSearch(query: String) {
        searchJob?.cancel()
        val trimmed = query.trim()
        if (trimmed.isEmpty()) {
            scope.launch(Dispatchers.Main) { onYMusicSearchResults(emptyList(), emptyList()) }
            return
        }
        val tok = directYmToken
        if (tok.isNotBlank()) {
            searchJob = scope.launch(Dispatchers.IO) {
                try {
                    val (tracks, albums) = YandexMusicDirectClient.searchCatalog(tok, trimmed)
                    withContext(Dispatchers.Main) { onYMusicSearchResults(tracks, albums) }
                    return@launch
                } catch (e: kotlinx.coroutines.CancellationException) {
                    throw e
                } catch (e: Exception) {
                    DebugLogStore.log("WARN", "DIRECT_YM", "search fallback: ${e.message}")
                }
                sendServerOffloadOrFallback("search", JSONObject().put("cmd", "ymusic_search").put("query", trimmed))
            }
            return
        }
        val payload = JSONObject().put("cmd", "ymusic_search").put("query", trimmed)
        sendServerOffloadOrFallback("search", payload)
    }

    fun ymusicOrbs() {
        val tok = directYmToken
        if (tok.isNotBlank()) {
            scope.launch(Dispatchers.IO) {
                try {
                    val orbs = YandexMusicDirectClient.fetchOrbs(tok, directYmUid)
                    if (orbs.isNotEmpty()) {
                        withContext(Dispatchers.Main) { onYMusicOrbsUpdate(orbs) }
                        return@launch
                    }
                } catch (e: Exception) {
                    DebugLogStore.log("WARN", "DIRECT_YM", "orbs fallback: ${e.message}")
                }
                sendServerOffloadOrFallback("orbs", JSONObject().put("cmd", "ymusic_orbs"))
            }
            return
        }
        val payload = JSONObject().put("cmd", "ymusic_orbs")
        sendServerOffloadOrFallback("orbs", payload)
    }

    fun ymusicCollection(force: Boolean = false) {
        val tok = directYmToken
        if (tok.isNotBlank()) {
            scope.launch(Dispatchers.IO) {
                try {
                    val col = YandexMusicDirectClient.fetchCollection(tok, directYmUid, force = force)
                    withContext(Dispatchers.Main) { onYMusicCollectionUpdate(col) }
                    return@launch
                } catch (e: Exception) {
                    DebugLogStore.log("WARN", "DIRECT_YM", "collection fallback: ${e.message}")
                }
                sendServerOffloadOrFallback("collection", JSONObject().put("cmd", "ymusic_collection"))
            }
            return
        }
        val payload = JSONObject().put("cmd", "ymusic_collection")
        sendServerOffloadOrFallback("collection", payload)
    }

    /**
     * Offloads routine Yandex Music catalog/collection metadata queries to the relay server
     * (/relay/v1/app/{deviceId}/ymusic/{op}) so the speaker CPU isn't loaded with UI browsing;
     * transparently falls back to the speaker's cached handler if the server doesn't expose the route yet.
     */
    private fun sendServerOffloadOrFallback(op: String, fallbackCmd: JSONObject) {
        val gw = relayGateway.trim().trimEnd('/')
        val did = relayDeviceId.trim()
        val tok = relayToken.trim()
        if (gw.startsWith("https://") && DEVICE_ID_RE.matches(did) && tok.isNotEmpty()) {
            scope.launch(Dispatchers.IO) {
                val t0 = System.currentTimeMillis()
                try {
                    val resp = relayHttpPost("$gw/relay/v1/app/$did/ymusic/$op", tok, fallbackCmd)
                    val dt = System.currentTimeMillis() - t0
                    val cached = resp.optBoolean("cached", false)
                    val reply = resp.optJSONObject("reply") ?: resp
                    if (reply.has("type")) {
                        DebugLogStore.log("OK", "OFFLOAD", "ymusic/$op (${dt}ms, cache=$cached)")
                        handlePacket(reply)
                        return@launch
                    }
                } catch (e: Exception) {
                    DebugLogStore.log("WARN", "OFFLOAD", "ymusic/$op fallback to speaker: ${e.message}")
                }
                sendCommand(fallbackCmd)
            }
        } else {
            sendCommand(fallbackCmd)
        }
    }

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
        userDisconnected = true
        relayActive = false
        relayJob?.cancel()
        relayJob = null
        readJob?.cancel()
        readJob = null
        searchJob?.cancel()
        searchJob = null
        activeDevice = null
        termSid = ""
        termSharedKey = null
        termPhonePubHex = ""
        termPhonePriv = null
        termInitInFlight = false
        pendingTermCommand = null
        closeQuietly()
        if (!silent) {
            scope.launch(Dispatchers.Main) {
                onTermE2eeState(false, "E2EE не подключён (X25519 + ChaCha20)")
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
                val vqe = VqeStatus.fromJson(json.optJSONObject("vqe"))
                val eq = EqStatus.fromJson(json.optJSONObject("eq"))
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
                    vqe = vqe,
                    eq = eq,
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
            "ymusic_orbs_response" -> {
                val orbs = YMusicOrb.fromJsonArray(json.optJSONArray("orbs"))
                withContext(Dispatchers.Main) {
                    onYMusicOrbsUpdate(orbs)
                }
            }
            "ymusic_collection_response" -> {
                val colObj = json.optJSONObject("collection")
                if (colObj != null) {
                    val col = YMusicCollection.fromJson(colObj)
                    withContext(Dispatchers.Main) {
                        onYMusicCollectionUpdate(col)
                    }
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
            "term_e2ee_init_response" -> {
                termInitInFlight = false
                val sid = json.optString("sid", "").trim()
                val spkPubHex = json.optString("pub", "").trim()
                val priv = termPhonePriv
                if (sid.isNotEmpty() && spkPubHex.length == 64 && priv != null) {
                    try {
                        val spkPub = E2eeCrypto.hexDecode(spkPubHex)
                        termSharedKey = E2eeCrypto.x25519(priv, spkPub)
                        termSid = sid
                        DebugLogStore.log("OK", "E2EE", "X25519 handshake complete (sid=${sid.take(8)}…)")
                        val queued = pendingTermCommand
                        pendingTermCommand = null
                        withContext(Dispatchers.Main) {
                            onTermE2eeState(true, "E2EE активен (X25519 + ChaCha20, sid=${sid.take(6)})")
                        }
                        if (!queued.isNullOrBlank()) {
                            termE2eeExec(queued)
                        }
                    } catch (e: Exception) {
                        pendingTermCommand = null
                        withContext(Dispatchers.Main) {
                            onTermE2eeState(false, "Ошибка E2EE handshake: ${e.message}")
                        }
                    }
                }
            }
            "term_e2ee_response" -> {
                val shared = termSharedKey
                val ivHex = json.optString("iv", "")
                val ctHex = json.optString("ct", "")
                val macHex = json.optString("mac", "")
                if (shared != null && ivHex.length == 24 && macHex.length == 64) {
                    try {
                        val pt = E2eeCrypto.decrypt(
                            shared,
                            E2eeCrypto.hexDecode(ivHex),
                            E2eeCrypto.hexDecode(ctHex),
                            E2eeCrypto.hexDecode(macHex)
                        )
                        val text = String(pt, Charsets.UTF_8)
                        DebugLogStore.log("OK", "E2EE", "rx decrypted ${pt.size}B output")
                        withContext(Dispatchers.Main) { onTermE2eeOutput(text) }
                    } catch (e: Exception) {
                        DebugLogStore.log("ERR", "E2EE", "MAC/decrypt failed: ${e.message}")
                        withContext(Dispatchers.Main) { onTermE2eeOutput("[E2EE ERROR] ${e.message}") }
                    }
                }
            }
            "relay_info" -> {
                val did = json.optString("device_id", "").trim()
                val gw = json.optString("gateway", "").trim().trimEnd('/')
                val tok = json.optString("token", "").trim()
                if (did.isNotEmpty()) {
                    if (gw.startsWith("https://")) relayGateway = gw
                    relayDeviceId = did
                    if (tok.isNotEmpty()) relayToken = tok
                    withContext(Dispatchers.Main) {
                        onRelayInfo(did, gw, tok)
                    }
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
