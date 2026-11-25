package com.leftymods.atrios

import android.Manifest
import android.accounts.AccountManager
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import com.leftymods.atrios.ui.theme.TypeUI
import androidx.compose.animation.togetherWith
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.scaleOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.leftymods.atrios.data.RelayConfig
import com.leftymods.atrios.data.SpeakerStorage
import com.leftymods.atrios.model.AlarmItem
import com.leftymods.atrios.model.SavedSpeaker
import com.leftymods.atrios.model.SpeakerStatus
import com.leftymods.atrios.model.TimerItem
import com.leftymods.atrios.model.YMusicAlbum
import com.leftymods.atrios.model.YMusicCollection
import com.leftymods.atrios.model.YMusicOrb
import com.leftymods.atrios.model.YMusicTrack
import com.leftymods.atrios.ui.AiAssistantCard
import com.leftymods.atrios.ui.AsyncCoverArt
import com.leftymods.atrios.ui.AtriHomeScreen
import com.leftymods.atrios.ui.AudioCard
import com.leftymods.atrios.ui.EqualizerPanel
import com.leftymods.atrios.ui.IntegrationsScreen
import com.leftymods.atrios.ui.BluetoothCard
import com.leftymods.atrios.ui.DevicePickerSection
import com.leftymods.atrios.ui.LedAndScreenCard
import com.leftymods.atrios.ui.OtaUpdateCard
import com.leftymods.atrios.ui.TelemetryCard
import com.leftymods.atrios.ui.TimeAndTimezoneCard
import com.leftymods.atrios.ui.TimersAndAlarmsCard
import com.leftymods.atrios.ui.WifiCard
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import com.leftymods.atrios.ui.theme.AppPalette
import com.leftymods.atrios.ui.theme.AtriOSTheme
import com.leftymods.atrios.ui.theme.OledBlack
import com.leftymods.atrios.ui.theme.OledCard
import com.leftymods.atrios.ui.theme.OledCardHigh
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.roundToInt

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        applyFullScreenImmersive()
        setContent {
            AtriOSRoot()
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) applyFullScreenImmersive()
    }

    private fun applyFullScreenImmersive() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val ctrl = WindowInsetsControllerCompat(window, window.decorView)
        ctrl.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        ctrl.hide(WindowInsetsCompat.Type.navigationBars())
    }
}

private enum class AppTab(val label: String, val icon: ImageVector) {
    HOME("Главная", Icons.Rounded.GraphicEq),
    INTEGRATIONS("Сервисы", Icons.Rounded.Extension),
    TIMERS("Таймеры", Icons.Rounded.Timer),
    SETTINGS("Настройки", Icons.Rounded.Tune)
}

private enum class SettingsPage(val title: String, val subtitle: String, val icon: ImageVector) {
    ROOT("Настройки", "", Icons.Rounded.Tune),
    ACCOUNT("Аккаунт Яндекс", "Яндекс Музыка, вход и профиль", Icons.Rounded.Person),
    WIFI("Wi-Fi и сеть", "Подключение колонки к Wi-Fi, интернет-пара", Icons.Rounded.Wifi),
    BLUETOOTH("Bluetooth", "Режим BT-колонки и сопряжение", Icons.Rounded.Bluetooth),
    PALETTE("Оформление и палитра", "Цвет интерфейса и анимации волны", Icons.Rounded.Palette),
    TIME("Время и регион", "Синхронизация NTP и часовой пояс", Icons.Rounded.Schedule),
    ABOUT("Об устройстве", "Кодовое имя, ревизия, прошивка и OTA", Icons.Rounded.Info),
    DEBUG("Debug-меню", "ИИ-сервер, Qwen3-TTS, анимации, RGB, диагностика", Icons.Rounded.BugReport)
}

@Composable
fun AtriOSRoot() {
    val context = LocalContext.current
    val storage = remember { SpeakerStorage(context) }
    val initCustom = remember { storage.loadCustomColors() }
    var palette by remember {
        mutableStateOf(AppPalette.fromId(storage.loadPaletteId(), initCustom.first, initCustom.second))
    }

    AtriOSTheme(palette = palette) {
        AtriOSApp(
            storage = storage,
            palette = palette,
            onPaletteChange = { p ->
                palette = p
                storage.savePaletteId(p.id)
                if (p.id == "custom") {
                    storage.saveCustomColors(p.accent.toArgb(), p.aura2.toArgb())
                }
            }
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@SuppressLint("MissingPermission")
@Composable
fun AtriOSApp(
    storage: SpeakerStorage,
    palette: AppPalette,
    onPaletteChange: (AppPalette) -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    val btManager = remember { context.getSystemService(BluetoothManager::class.java) }
    val adapter: BluetoothAdapter? = btManager?.adapter

    var hasPerms by remember {
        mutableStateOf(
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) ==
                    PackageManager.PERMISSION_GRANTED
            } else true
        )
    }
    val permLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { res -> hasPerms = res.values.all { it } }

    var connected by remember { mutableStateOf(false) }
    var activeDevice by remember { mutableStateOf<BluetoothDevice?>(null) }
    var connText by remember { mutableStateOf("Подключение к колонке...") }
    var autoConnectState by remember { mutableStateOf("trying") }
    var autoAttemptSeq by remember { mutableIntStateOf(0) }

    var status by remember { mutableStateOf(SpeakerStatus()) }
    var sliderVol by remember { mutableFloatStateOf(60f) }
    var timers by remember { mutableStateOf<List<TimerItem>>(emptyList()) }
    var alarms by remember { mutableStateOf<List<AlarmItem>>(emptyList()) }
    var ymSearchResults by remember { mutableStateOf<List<YMusicTrack>>(emptyList()) }
    var ymAlbumResults by remember { mutableStateOf<List<YMusicAlbum>>(emptyList()) }
    var ymOrbs by remember { mutableStateOf<List<YMusicOrb>>(emptyList()) }
    var ymCollection by remember { mutableStateOf(YMusicCollection()) }
    var termE2eeReady by remember { mutableStateOf(false) }
    var termE2eeStatus by remember { mutableStateOf("E2EE не подключён (X25519 + ChaCha20)") }
    var termOutputLog by remember { mutableStateOf("") }
    var termCmdInput by remember { mutableStateOf("") }
    var savedSpeakers by remember { mutableStateOf(storage.loadSpeakers()) }
    var relayConfig by remember { mutableStateOf(storage.loadRelayConfig()) }

    val accountClient = remember { AtriAccountClient(context) }
    var accountState by remember { mutableStateOf(accountClient.loadCachedState()) }
    var accountBusy by remember { mutableStateOf(false) }
    var accountError by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        if (accountState.loggedIn) {
            withContext(Dispatchers.IO) { accountClient.syncVault() }
                .onSuccess { accountState = it }
        }
    }

    var selectedTab by remember { mutableStateOf(AppTab.HOME) }
    var settingsPage by remember { mutableStateOf(SettingsPage.ROOT) }
    var showBigPlayer by remember { mutableStateOf(false) }
    val lastUserActionRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val rejectedOldTitleRef = remember { java.util.concurrent.atomic.AtomicReference<String?>(null) }
    val rejectedOldTrackIdRef = remember { java.util.concurrent.atomic.AtomicReference<String?>(null) }
    val rejectedUntilRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val pendingPlayStateRef = remember { java.util.concurrent.atomic.AtomicReference<String?>(null) }
    val pendingPlayStateUntilRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val lastYmSeqRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val pendingVolRef = remember { java.util.concurrent.atomic.AtomicInteger(-1) }
    val pendingVolUntilRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val latestSliderPctRef = remember { java.util.concurrent.atomic.AtomicInteger(60) }
    val pendingEqUntilRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val lastVolumeDragRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val lastVolumeSentRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val lastVolumeSentPctRef = remember { java.util.concurrent.atomic.AtomicInteger(-1) }
    val lastNextSkipMsRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val trackTransitionJobRef = remember { java.util.concurrent.atomic.AtomicReference<Job?>(null) }
    val lastSavedSpeakerKeyRef = remember { java.util.concurrent.atomic.AtomicReference<String>("") }
    var isTrackTransitioning by remember { mutableStateOf(false) }
    var showVolumeSheet by remember { mutableStateOf(false) }
    var showEqSheet by remember { mutableStateOf(false) }
    var liveElapsedMs by remember(status.ymusic.trackId) { mutableLongStateOf(status.ymusic.positionMs.coerceAtLeast(0L)) }
    LaunchedEffect(status.ymusic.positionMs) {
        val srv = status.ymusic.positionMs.coerceAtLeast(0L)
        if (kotlin.math.abs(srv - liveElapsedMs) >= 2000L) liveElapsedMs = srv
    }
    LaunchedEffect(status.ymusic.trackId, status.ymusic.state, selectedTab) {
        if (selectedTab != AppTab.HOME) {
            while (status.ymusic.state == "playing") {
                delay(500)
                val dur = status.ymusic.durationMs
                liveElapsedMs = if (dur > 0L) (liveElapsedMs + 500L).coerceAtMost(dur) else (liveElapsedMs + 500L)
            }
        }
    }
    var checkingAppUpdate by remember { mutableStateOf(false) }
    var appUpdateStatusMsg by remember { mutableStateOf<String?>(null) }
    var showRebootDialog by remember { mutableStateOf(false) }
    var availableAppUpdate by remember { mutableStateOf<AppReleaseInfo?>(null) }
    var appUpdateProgress by remember { mutableStateOf<Float?>(null) }

    LaunchedEffect(Unit) {
        while (true) {
            val rel = AppUpdater.checkForUpdate(context)
            if (rel != null) {
                availableAppUpdate = rel
                DebugLogStore.log("INFO", "UPDATER", "Найдена новая версия v${rel.version} — автозагрузка APK...")
                val dl = AppUpdater.downloadOnly(context, rel) { p -> appUpdateProgress = p }
                appUpdateProgress = null
                dl.onSuccess { apkFile ->
                    DebugLogStore.log("OK", "UPDATER", "APK v${rel.version} скачан (${apkFile.length() / 1024} КБ) — запуск установки")
                    AppUpdater.installApk(context, apkFile)
                }.onFailure { err ->
                    DebugLogStore.log("WARN", "UPDATER", "Ошибка автозагрузки v${rel.version}: ${err.message}")
                }
            }
            delay(30L * 60L * 1000L)
        }
    }

    // Persistent auto-reconnect watchdog if connection ever drops unexpectedly
    LaunchedEffect(connected, autoConnectState) {
        if (!connected && autoConnectState != "manual") {
            delay(4500L)
            if (!connected && autoConnectState != "manual") {
                autoAttemptSeq++
            }
        }
    }

    val client = remember {
        BtControlClient(
            scope = scope,
            onStatus = { s ->
                val nowMs = System.currentTimeMillis()
                val staleSeq = s.ymusic.seq > 0L && s.ymusic.seq < lastYmSeqRef.get()
                val lockedYm = nowMs - lastUserActionRef.get() < 1800L
                val rejectedOld = nowMs < rejectedUntilRef.get() && (
                    (s.ymusic.title.isNotBlank() && s.ymusic.title == rejectedOldTitleRef.get()) ||
                    (s.ymusic.trackId.isNotBlank() && s.ymusic.trackId == rejectedOldTrackIdRef.get())
                )
                val wantVol = pendingVolRef.get()
                val volEchoStale = wantVol >= 0 && nowMs < pendingVolUntilRef.get() && s.volume != wantVol
                if (wantVol >= 0 && s.volume == wantVol) pendingVolRef.set(-1)
                val lockedVol = (nowMs - lastVolumeDragRef.get() < 1200L) || volEchoStale
                val lockedEq = nowMs < pendingEqUntilRef.get()
                val incomingTrackReady = isTrackTransitioning && !staleSeq && !rejectedOld &&
                    s.ymusic.trackId.isNotBlank() && !s.ymusic.trackId.startsWith("opt_") && s.ymusic.state != "loading"
                val externalYnisonUpdate = s.ymusic.mode == "ynison" && !staleSeq && (
                    s.ymusic.trackId != status.ymusic.trackId ||
                        s.ymusic.title != status.ymusic.title ||
                        s.ymusic.state != status.ymusic.state
                    )
                val nextYm = if (staleSeq || (!externalYnisonUpdate && (rejectedOld || (lockedYm && !incomingTrackReady)))) {
                    status.ymusic
                } else {
                    if (s.ymusic.seq > lastYmSeqRef.get()) lastYmSeqRef.set(s.ymusic.seq)
                    if (incomingTrackReady || externalYnisonUpdate) isTrackTransitioning = false
                    val wantState = pendingPlayStateRef.get()
                    if (wantState != null && nowMs < pendingPlayStateUntilRef.get() && s.ymusic.state != wantState) {
                        s.ymusic.copy(state = wantState)
                    } else {
                        if (wantState != null && s.ymusic.state == wantState) pendingPlayStateRef.set(null)
                        s.ymusic
                    }
                }
                val nextEq = if (lockedEq) status.eq else s.eq
                status = if (lockedVol) {
                    val curPct = sliderVol.roundToInt()
                    s.copy(ymusic = nextYm, eq = nextEq, volume = curPct, mute = curPct == 0)
                } else {
                    sliderVol = s.volume.toFloat()
                    latestSliderPctRef.set(s.volume)
                    s.copy(ymusic = nextYm, eq = nextEq)
                }
                activeDevice?.let { dev ->
                    val fw = s.fwVersion.ifBlank { s.ota.currentVersion }
                    val spkKey = "${dev.address}|${dev.name}|${s.model}|${s.codename}|$fw"
                    if (lastSavedSpeakerKeyRef.getAndSet(spkKey) != spkKey) {
                        savedSpeakers = storage.upsertSpeaker(
                            SavedSpeaker(
                                mac = dev.address,
                                name = dev.name ?: s.model,
                                model = s.model,
                                codename = s.codename,
                                fwVersion = fw,
                                lastSeenTs = System.currentTimeMillis()
                            )
                        )
                    }
                }
            },
            onVolumePush = { v, m ->
                val nowMs = System.currentTimeMillis()
                val wantVol = pendingVolRef.get()
                if (wantVol >= 0 && v == wantVol) pendingVolRef.set(-1)
                val stale = (nowMs - lastVolumeDragRef.get() < 1200L) ||
                    (wantVol >= 0 && nowMs < pendingVolUntilRef.get() && v != wantVol)
                if (!stale) {
                    sliderVol = v.toFloat()
                    latestSliderPctRef.set(v)
                    status = status.copy(volume = v, mute = m)
                }
            },
            onButtonsPush = { ap, mm ->
                status = status.copy(actionPressed = ap, micMuted = mm)
            },
            onOtaUpdate = { ota -> status = status.copy(ota = ota) },
            onYMusicSearchResults = { tracks, albums ->
                ymSearchResults = tracks
                ymAlbumResults = albums
            },
            onYMusicOrbsUpdate = { orbs -> ymOrbs = orbs },
            onYMusicCollectionUpdate = { col -> ymCollection = col },
            onYMusicUpdate = { ym ->
                val nowMs = System.currentTimeMillis()
                // Ynison-style monotonic sequence guard: reject older snapshots from in-flight polls!
                if (ym.seq > 0L && ym.seq < lastYmSeqRef.get()) return@BtControlClient
                val rejectedOld = nowMs < rejectedUntilRef.get() && (
                    (ym.title.isNotBlank() && ym.title == rejectedOldTitleRef.get()) ||
                    (ym.trackId.isNotBlank() && ym.trackId == rejectedOldTrackIdRef.get())
                )
                if (rejectedOld) return@BtControlClient
                if (ym.seq > lastYmSeqRef.get()) lastYmSeqRef.set(ym.seq)
                val wantState = pendingPlayStateRef.get()
                val mergedYm = if (wantState != null && nowMs < pendingPlayStateUntilRef.get() && ym.state != wantState) {
                    ym.copy(state = wantState)
                } else {
                    if (wantState != null && ym.state == wantState) pendingPlayStateRef.set(null)
                    ym
                }
                val locked = nowMs - lastUserActionRef.get() < 1600L
                if (!locked || (mergedYm.title.isNotBlank() && mergedYm.title != status.ymusic.title) || mergedYm.state == wantState || isTrackTransitioning) {
                    status = status.copy(ymusic = mergedYm)
                    if (isTrackTransitioning && mergedYm.trackId.isNotBlank() && !mergedYm.trackId.startsWith("opt_") && mergedYm.state != "loading") {
                        isTrackTransitioning = false
                    }
                }
            },
            onTimersUpdate = { list -> timers = list },
            onAlarmsUpdate = { list -> alarms = list },
            onRelayInfo = { did, gw, tok ->
                val updated = relayConfig.copy(
                    deviceId = did,
                    gatewayUrl = gw.ifBlank { relayConfig.gatewayUrl },
                    token = tok.ifBlank { relayConfig.token }
                )
                relayConfig = updated
                storage.saveRelayConfig(updated)
            },
            onBtSpeakerPush = { enabled -> status = status.copy(btSpeakerMode = enabled) },
            onTermE2eeState = { ok, msg ->
                termE2eeReady = ok
                termE2eeStatus = msg
            },
            onTermE2eeOutput = { out ->
                val combined = (termOutputLog + "\n" + out).trim()
                termOutputLog = if (combined.length > 6000) combined.takeLast(6000) else combined
            },
            onMessage = { msg, isError ->
                if (isError && msg.isNotBlank()) {
                    scope.launch {
                        snackbarHostState.showSnackbar(msg, duration = SnackbarDuration.Short)
                    }
                }
            },
            onConnectionState = { ok, msg, dev ->
                connected = ok
                connText = msg
                activeDevice = dev
                if (ok) {
                    autoConnectState = "connected"
                } else {
                    timers = emptyList()
                    alarms = emptyList()
                    ymSearchResults = emptyList()
                    ymAlbumResults = emptyList()
                    ymCollection = YMusicCollection()
                    termE2eeReady = false
                    termE2eeStatus = "E2EE не подключён (X25519 + ChaCha20)"
                }
            }
        ).also { c ->
            c.setSavedRelayConfig(relayConfig.gatewayUrl, relayConfig.deviceId, relayConfig.token)
            val savedYmTok = storage.loadYMusicToken()
            if (savedYmTok.isNotBlank()) c.setDirectYMusicAuth(savedYmTok)
        }
    }

    fun dispatchVolumeLive(vol: Float, forceCommit: Boolean = false) {
        val nowMs = System.currentTimeMillis()
        lastVolumeDragRef.set(nowMs)
        val clamped = vol.coerceIn(0f, 100f)
        val pct = clamped.roundToInt()
        latestSliderPctRef.set(pct)
        sliderVol = clamped
        pendingVolRef.set(pct)
        pendingVolUntilRef.set(nowMs + 3500L)
        status = status.copy(volume = pct, mute = pct == 0)
        val changed = pct != lastVolumeSentPctRef.get()
        if ((forceCommit && changed) || (pct == 0 && changed) || (changed && nowMs - lastVolumeSentRef.get() >= 110L)) {
            lastVolumeSentRef.set(nowMs)
            lastVolumeSentPctRef.set(pct)
            client.setVolume(pct)
        }
    }

    fun dispatchYMusicPlay(mode: String, query: String? = null) {
        val nowMs = System.currentTimeMillis()
        lastUserActionRef.set(nowMs)
        pendingPlayStateRef.set(null)
        status = status.copy(ymusic = status.ymusic.copy(mode = mode, state = "loading", error = null))
        client.ymusicPlay(mode, query)
        if (!client.isRelayActive()) {
            scope.launch {
                delay(750)
                client.ymusicStatus()
            }
        }
    }

    fun dispatchYMusicControl(action: String) {
        val nowMs = System.currentTimeMillis()
        // Guard forward/backward skip against accidental double-triggers while an optimistic transition is in flight
        if (action == "next") {
            if (isTrackTransitioning || nowMs - lastNextSkipMsRef.get() < 500L) return
            lastNextSkipMsRef.set(nowMs)
        }
        if (action == "prev") {
            if (!status.ymusic.canPrev || isTrackTransitioning) return
        }
        lastUserActionRef.set(nowMs)
        val cur = status.ymusic
        when (action) {
            "pause" -> {
                pendingPlayStateRef.set("paused")
                pendingPlayStateUntilRef.set(nowMs + 1600L)
                status = status.copy(ymusic = cur.copy(state = "paused"))
            }
            "play" -> {
                pendingPlayStateRef.set("playing")
                pendingPlayStateUntilRef.set(nowMs + 1600L)
                status = status.copy(ymusic = cur.copy(state = "playing"))
            }
            "repeat" -> status = status.copy(ymusic = cur.copy(repeat = !cur.repeat))
            "like" -> status = status.copy(ymusic = cur.copy(liked = !cur.liked))
            "next" -> {
                isTrackTransitioning = true
                liveElapsedMs = 0L
                rejectedOldTitleRef.set(cur.title.ifBlank { null })
                rejectedOldTrackIdRef.set(cur.trackId.takeIf { it.isNotBlank() && !it.startsWith("opt_") })
                rejectedUntilRef.set(nowMs + 4500L)
                if (cur.nextTitle.isNotBlank()) {
                    status = status.copy(
                        ymusic = cur.copy(
                            state = "loading",
                            trackId = "opt_next_${nowMs}",
                            title = cur.nextTitle,
                            artist = cur.nextArtist,
                            coverUrl = cur.nextCoverUrl.ifBlank { cur.coverUrl },
                            nextTitle = "",
                            nextArtist = "",
                            nextCoverUrl = "",
                            prevTitle = cur.title,
                            prevArtist = cur.artist,
                            prevCoverUrl = cur.coverUrl,
                            canPrev = true,
                            durationMs = 0L,
                            positionMs = 0L
                        )
                    )
                } else {
                    status = status.copy(ymusic = cur.copy(state = "loading", durationMs = 0L, positionMs = 0L))
                }
            }
            "prev" -> {
                isTrackTransitioning = true
                liveElapsedMs = 0L
                rejectedOldTitleRef.set(cur.title.ifBlank { null })
                rejectedOldTrackIdRef.set(cur.trackId.takeIf { it.isNotBlank() && !it.startsWith("opt_") })
                rejectedUntilRef.set(nowMs + 4500L)
                if (cur.prevTitle.isNotBlank()) {
                    status = status.copy(
                        ymusic = cur.copy(
                            state = "loading",
                            trackId = "opt_prev_${nowMs}",
                            title = cur.prevTitle,
                            artist = cur.prevArtist,
                            coverUrl = cur.prevCoverUrl.ifBlank { cur.coverUrl },
                            nextTitle = cur.title,
                            nextArtist = cur.artist,
                            nextCoverUrl = cur.coverUrl,
                            prevTitle = "",
                            prevArtist = "",
                            prevCoverUrl = "",
                            durationMs = 0L,
                            positionMs = 0L
                        )
                    )
                } else {
                    status = status.copy(ymusic = cur.copy(state = "loading", durationMs = 0L, positionMs = 0L))
                }
            }
        }
        client.ymusicControl(action)
        if (action == "next" || action == "prev") {
            trackTransitionJobRef.getAndSet(
                scope.launch {
                    delay(750)
                    if (!client.isRelayActive()) client.ymusicStatus()
                    delay(1800)
                    isTrackTransitioning = false
                }
            )?.cancel()
        }
    }

    val bondedDevices: List<BluetoothDevice> = remember(hasPerms, adapter, connected) {
        if (hasPerms && adapter != null) adapter.bondedDevices.toList() else emptyList()
    }

    // System Back gesture navigates cleanly inside the app instead of closing it!
    BackHandler(enabled = connected && (showVolumeSheet || showEqSheet || settingsPage != SettingsPage.ROOT || selectedTab != AppTab.HOME)) {
        when {
            showEqSheet -> showEqSheet = false
            showVolumeSheet -> showVolumeSheet = false
            selectedTab == AppTab.SETTINGS && settingsPage != SettingsPage.ROOT -> settingsPage = SettingsPage.ROOT
            selectedTab != AppTab.HOME -> selectedTab = AppTab.HOME
        }
    }

    // Auto-connect to any available transport (BT first, then Internet Relay)
    LaunchedEffect(autoAttemptSeq, hasPerms) {
        if (connected || autoConnectState == "manual") return@LaunchedEffect
        autoConnectState = "trying"
        val saved = storage.loadSpeakers()
        val relay = storage.loadRelayConfig()

        val btTarget = if (hasPerms && adapter?.isEnabled == true) {
            val bonded = adapter.bondedDevices.orEmpty()
            saved.firstNotNullOfOrNull { s -> bonded.firstOrNull { it.address.equals(s.mac, ignoreCase = true) } }
                ?: bonded.firstOrNull { (it.name ?: "").contains("Atri", ignoreCase = true) }
        } else null

        if (btTarget != null) {
            client.connect(btTarget)
            val deadline = System.currentTimeMillis() + 5500L
            while (!connected && System.currentTimeMillis() < deadline) delay(200)
            if (connected) return@LaunchedEffect
        }

        if (relay.gatewayUrl.startsWith("https://") && relay.deviceId.isNotBlank() && relay.token.isNotBlank()) {
            client.connectRelay(relay.gatewayUrl, relay.deviceId, relay.token)
            val deadline = System.currentTimeMillis() + 6500L
            while (!connected && System.currentTimeMillis() < deadline) delay(200)
            if (connected) return@LaunchedEffect
        }

        if (!connected && autoConnectState != "manual") autoConnectState = "failed"
    }

    LaunchedEffect(connected) {
        if (connected) {
            // BtControlClient.connect() and startRelayLoop() already fetch get_status, get_timers, get_alarms
            client.ymusicOrbs()
        }
    }

    // Automatically persist relayConfig into encrypted AtriOS Vault whenever logged in and configured
    LaunchedEffect(accountState.loggedIn, relayConfig.deviceId, relayConfig.token, relayConfig.gatewayUrl) {
        if (accountState.loggedIn && relayConfig.deviceId.isNotBlank() && relayConfig.token.isNotBlank()) {
            val v = accountState.vault
            val spkName = activeDevice?.name?.takeIf { it.isNotBlank() } ?: status.model.ifBlank { "AtriStation" }
            if (v.relayDeviceId != relayConfig.deviceId || v.relayToken != relayConfig.token || v.relayUrl != relayConfig.gatewayUrl) {
                val updated = v.copy(
                    relayDeviceId = relayConfig.deviceId,
                    relayToken = relayConfig.token,
                    relayUrl = relayConfig.gatewayUrl,
                    speakerName = spkName
                )
                withContext(Dispatchers.IO) { accountClient.saveVault(updated) }
                    .onSuccess { accountState = it }
            }
        }
    }

    LaunchedEffect(connected, status.ymusic.isActive) {
        // Only poll ymusic_status on BT transport; Relay already polls full /status (with ymusic) every 1.5s
        while (connected && status.ymusic.isActive && !client.isRelayActive()) {
            delay(1800)
            client.ymusicStatus()
        }
    }

    DisposableEffect(Unit) { onDispose { client.disconnect(silent = true) } }

    Box(modifier = Modifier.fillMaxSize().background(OledBlack)) {
        if (!connected) {
            when (autoConnectState) {
                "trying" -> {
                    Column(
                        modifier = Modifier.fillMaxSize().padding(32.dp),
                        verticalArrangement = Arrangement.Center,
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        CircularProgressIndicator(color = palette.accent, strokeWidth = 3.dp, modifier = Modifier.size(44.dp))
                        Spacer(Modifier.height(20.dp))
                        Text("Подключаемся к колонке...", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 18.sp)
                        Spacer(Modifier.height(6.dp))
                        Text(connText, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 13.sp, textAlign = TextAlign.Center)
                        Spacer(Modifier.height(24.dp))
                        TextButton(onClick = { autoConnectState = "manual" }) {
                            Text("Выбрать вручную", color = palette.accent)
                        }
                    }
                }
                "failed" -> {
                    Column(
                        modifier = Modifier.fillMaxSize().padding(32.dp),
                        verticalArrangement = Arrangement.Center,
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Box(
                            modifier = Modifier.size(72.dp).clip(CircleShape).background(OledCardHigh),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(Icons.Rounded.WifiOff, contentDescription = null, tint = palette.accent, modifier = Modifier.size(34.dp))
                        }
                        Spacer(Modifier.height(20.dp))
                        Text(
                            "Упс, не удалось подключиться к колонке",
                            color = Color.White,
                            fontWeight = FontWeight.Bold,
                            fontSize = 20.sp,
                            textAlign = TextAlign.Center
                        )
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "Проверьте, что колонка включена, рядом по Bluetooth или подключена к интернету.",
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            fontSize = 13.sp,
                            textAlign = TextAlign.Center
                        )
                        Spacer(Modifier.height(28.dp))
                        Button(
                            onClick = { autoConnectState = "trying"; autoAttemptSeq++ },
                            modifier = Modifier.fillMaxWidth().height(52.dp),
                            shape = RoundedCornerShape(99.dp),
                            colors = ButtonDefaults.buttonColors(containerColor = palette.accent, contentColor = palette.onAccent)
                        ) {
                            Icon(Icons.Rounded.Refresh, contentDescription = null)
                            Spacer(Modifier.width(8.dp))
                            Text("Попробовать ещё раз", fontWeight = FontWeight.Bold, fontSize = 15.sp)
                        }
                        Spacer(Modifier.height(12.dp))
                        OutlinedButton(
                            onClick = { autoConnectState = "manual" },
                            modifier = Modifier.fillMaxWidth().height(50.dp),
                            shape = RoundedCornerShape(99.dp)
                        ) {
                            Icon(Icons.Rounded.Tune, contentDescription = null)
                            Spacer(Modifier.width(8.dp))
                            Text("Подключить вручную", fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
                        }
                    }
                }
                else -> {
                    LazyColumn(
                        modifier = Modifier.fillMaxSize().statusBarsPadding().padding(horizontal = 16.dp),
                        verticalArrangement = Arrangement.spacedBy(14.dp),
                        contentPadding = PaddingValues(vertical = 16.dp)
                    ) {
                        item {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.SpaceBetween,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text("Подключение вручную", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 22.sp)
                                TextButton(onClick = { autoConnectState = "trying"; autoAttemptSeq++ }) {
                                    Text("Автопоиск", color = palette.accent)
                                }
                            }
                        }
                        if (!accountState.loggedIn) {
                            item {
                                AtriIdOfflineLoginCard(
                                    palette = palette,
                                    busy = accountBusy,
                                    error = accountError,
                                    onLogin = { u, p ->
                                        accountBusy = true; accountError = null
                                        scope.launch {
                                            val res = withContext(Dispatchers.IO) { accountClient.login(u, p) }
                                            res.onSuccess { st ->
                                                accountState = st
                                                val v = st.vault
                                                if (v.yandexMusicToken.isNotBlank()) {
                                                    storage.saveYMusicToken(v.yandexMusicToken)
                                                    client.setDirectYMusicAuth(v.yandexMusicToken)
                                                }
                                                if (v.relayDeviceId.isNotBlank() && v.relayToken.isNotBlank()) {
                                                    val cfg = RelayConfig(
                                                        gatewayUrl = v.relayUrl.ifBlank { "https://ai.leftymods.tech" },
                                                        deviceId = v.relayDeviceId,
                                                        token = v.relayToken
                                                    )
                                                    relayConfig = cfg
                                                    storage.saveRelayConfig(cfg)
                                                    autoConnectState = "trying"
                                                    client.connectRelay(cfg.gatewayUrl, cfg.deviceId, cfg.token)
                                                } else {
                                                    accountError = "Аккаунт открыт, но в облаке ещё нет сохранённой колонки"
                                                }
                                            }.onFailure { accountError = it.message }
                                            accountBusy = false
                                        }
                                    }
                                )
                            }
                        }
                        if (!hasPerms) {
                            item {
                                Surface(shape = RoundedCornerShape(22.dp), color = OledCard) {
                                    Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                                        Text("Разрешение Bluetooth", fontWeight = FontWeight.Bold)
                                        Text("Нужно для поиска колонки по Bluetooth.", style = MaterialTheme.typography.bodySmall)
                                        Button(
                                            onClick = {
                                                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                                                    permLauncher.launch(arrayOf(Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN))
                                                }
                                            },
                                            shape = RoundedCornerShape(99.dp)
                                        ) { Text("Разрешить") }
                                    }
                                }
                            }
                        } else {
                            item {
                                DevicePickerSection(
                                    savedSpeakers = savedSpeakers,
                                    bondedDevices = bondedDevices,
                                    onConnect = { dev -> client.connect(dev) },
                                    onForgetSaved = { mac -> savedSpeakers = storage.removeSpeaker(mac) }
                                )
                            }
                        }
                        item {
                            var gwInput by remember(relayConfig.gatewayUrl) { mutableStateOf(relayConfig.gatewayUrl) }
                            var didInput by remember(relayConfig.deviceId) { mutableStateOf(relayConfig.deviceId) }
                            var tokInput by remember(relayConfig.token) { mutableStateOf(relayConfig.token) }
                            Surface(shape = RoundedCornerShape(22.dp), color = OledCard) {
                                Column(Modifier.fillMaxWidth().padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                                    Text("Подключение через интернет", fontWeight = FontWeight.Bold, color = Color.White)
                                    OutlinedTextField(value = gwInput, onValueChange = { gwInput = it }, label = { Text("HTTPS шлюз") }, singleLine = true, modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(14.dp))
                                    OutlinedTextField(value = didInput, onValueChange = { didInput = it }, label = { Text("Device ID") }, singleLine = true, modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(14.dp))
                                    OutlinedTextField(value = tokInput, onValueChange = { tokInput = it }, label = { Text("Bearer токен") }, singleLine = true, modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(14.dp))
                                    Button(
                                        onClick = {
                                            val cfg = RelayConfig(gwInput.trim(), didInput.trim(), tokInput.trim())
                                            relayConfig = cfg
                                            storage.saveRelayConfig(cfg)
                                            client.connectRelay(cfg.gatewayUrl, cfg.deviceId, cfg.token)
                                        },
                                        enabled = gwInput.trim().startsWith("https://") && didInput.isNotBlank() && tokInput.isNotBlank(),
                                        modifier = Modifier.fillMaxWidth().height(48.dp),
                                        shape = RoundedCornerShape(99.dp)
                                    ) { Text("Подключиться", fontWeight = FontWeight.Bold) }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            Scaffold(
                containerColor = OledBlack,
                contentWindowInsets = WindowInsets(0, 0, 0, 0),
                snackbarHost = {
                    SnackbarHost(snackbarHostState) { data ->
                        val dismissState = rememberSwipeToDismissBoxState(
                            confirmValueChange = { value ->
                                if (value != SwipeToDismissBoxValue.Settled) {
                                    data.dismiss()
                                    true
                                } else false
                            }
                        )
                        SwipeToDismissBox(
                            state = dismissState,
                            backgroundContent = {},
                            content = {
                                Snackbar(
                                    containerColor = Color(0xFF2B121A),
                                    contentColor = Color(0xFFFFD6DC),
                                    shape = RoundedCornerShape(14.dp),
                                    dismissAction = {
                                        IconButton(onClick = { data.dismiss() }) {
                                            Icon(Icons.Rounded.Close, contentDescription = "Закрыть", tint = Color(0xFFFFD6DC), modifier = Modifier.size(18.dp))
                                        }
                                    }
                                ) {
                                    Text(data.visuals.message, color = Color(0xFFFFD6DC), fontSize = 13.sp)
                                }
                            }
                        )
                    }
                },
                bottomBar = {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(Color.Transparent)
                            .padding(bottom = 12.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        // Floating Rounded Capsule Bottom Navigation Bar!
                        Surface(
                            shape = RoundedCornerShape(99.dp),
                            color = Color(0xEE16161B),
                            modifier = Modifier
                                .padding(horizontal = 24.dp, vertical = 4.dp)
                                .border(1.dp, Color.White.copy(alpha = 0.08f), RoundedCornerShape(99.dp))
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 8.dp, vertical = 6.dp),
                                horizontalArrangement = Arrangement.spacedBy(6.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                AppTab.entries.forEach { tab ->
                                    val sel = selectedTab == tab
                                    val pillBg by animateColorAsState(
                                        if (sel) palette.accent.copy(alpha = 0.18f) else Color.Transparent,
                                        tween(240),
                                        label = "navBg"
                                    )
                                    val iconTint by animateColorAsState(
                                        if (sel) palette.accent else Color(0xFF7A7A86),
                                        tween(240),
                                        label = "navTint"
                                    )
                                    Row(
                                        modifier = Modifier
                                            .clip(RoundedCornerShape(99.dp))
                                            .background(pillBg)
                                            .clickable {
                                                selectedTab = tab
                                                if (tab == AppTab.SETTINGS) settingsPage = SettingsPage.ROOT
                                                when (tab) {
                                                    AppTab.HOME -> client.ymusicStatus()
                                                    AppTab.INTEGRATIONS -> {
                                                        client.ymusicStatus()
                                                        if (accountState.loggedIn) {
                                                            scope.launch {
                                                                withContext(Dispatchers.IO) { accountClient.syncVault() }
                                                                    .onSuccess { accountState = it }
                                                            }
                                                        }
                                                    }
                                                    AppTab.TIMERS -> { client.getTimers(); client.getAlarms() }
                                                    AppTab.SETTINGS -> client.otaStatus()
                                                }
                                            }
                                            .padding(horizontal = 16.dp, vertical = 10.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                                    ) {
                                        Icon(tab.icon, contentDescription = tab.label, tint = iconTint, modifier = Modifier.size(22.dp))
                                        AnimatedVisibility(visible = sel) {
                                            Text(tab.label, color = palette.accent, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            ) { pad ->
                AnimatedContent(
                    targetState = selectedTab,
                    modifier = Modifier.fillMaxSize().padding(pad),
                    transitionSpec = {
                        val dir = if (targetState.ordinal >= initialState.ordinal) 1 else -1
                        (slideInHorizontally(
                            animationSpec = spring(dampingRatio = 0.86f, stiffness = 320f)
                        ) { (it * 0.28f).toInt() * dir } +
                            scaleIn(initialScale = 0.95f, animationSpec = tween(240, easing = FastOutSlowInEasing)) +
                            fadeIn(tween(220)))
                            .togetherWith(
                                slideOutHorizontally(
                                    animationSpec = spring(dampingRatio = 0.86f, stiffness = 320f)
                                ) { -(it * 0.22f).toInt() * dir } +
                                    scaleOut(targetScale = 0.96f, animationSpec = tween(180)) +
                                    fadeOut(tween(160))
                            )
                    },
                    label = "tabTransition"
                ) { currentTab ->
                    val currentDeviceId = relayConfig.deviceId
                    val currentSpeakerName = activeDevice?.name?.takeIf { it.isNotBlank() } ?: "AtriStation"
                    val transportLabel = if (client.isRelayActive()) "Через интернет" else "Bluetooth"

                    when (currentTab) {
                        AppTab.HOME -> {
                            AtriHomeScreen(
                                status = status,
                                speakerName = currentSpeakerName,
                                transportLabel = transportLabel,
                                accountState = accountState,
                                palette = palette,
                                volumePct = sliderVol.roundToInt().coerceIn(0, 100),
                                onBlobTap = {
                                    if (status.ymusic.state == "playing") {
                                        dispatchYMusicControl("pause")
                                    } else if (status.ymusic.title.isNotBlank()) {
                                        dispatchYMusicControl("play")
                                    } else if (status.ymusic.loggedIn) {
                                        dispatchYMusicPlay("wave", null)
                                    } else {
                                        selectedTab = AppTab.INTEGRATIONS
                                    }
                                },
                                onPrev = { dispatchYMusicControl("prev") },
                                onPlayPause = {
                                    dispatchYMusicControl(if (status.ymusic.state == "playing") "pause" else "play")
                                },
                                onNext = { dispatchYMusicControl("next") },
                                onOpenVolume = { showVolumeSheet = true },
                                onOpenEq = { showEqSheet = true },
                                onOpenIntegrations = { selectedTab = AppTab.INTEGRATIONS },
                                onOpenTimers = { selectedTab = AppTab.TIMERS }
                            )
                        }
                        AppTab.INTEGRATIONS -> {
                            IntegrationsScreen(
                                accountState = accountState,
                                accountBusy = accountBusy,
                                accountError = accountError,
                                currentDeviceId = currentDeviceId,
                                currentSpeakerName = currentSpeakerName,
                                ymStatus = status.ymusic,
                                palette = palette,
                                onAccountRegister = { u, p ->
                                    accountBusy = true; accountError = null
                                    scope.launch {
                                        val initVault = AtriVaultData(
                                            yandexMusicToken = storage.loadYMusicToken(),
                                            yandexMusicLogin = status.ymusic.login,
                                            boundDeviceIds = if (currentDeviceId.isNotBlank()) listOf(currentDeviceId) else emptyList(),
                                            relayDeviceId = relayConfig.deviceId,
                                            relayToken = relayConfig.token,
                                            relayUrl = relayConfig.gatewayUrl,
                                            speakerName = currentSpeakerName
                                        )
                                        val res = withContext(Dispatchers.IO) { accountClient.register(u, p, initVault) }
                                        res.onSuccess { st ->
                                            accountState = st
                                            if (currentDeviceId.isNotBlank()) {
                                                withContext(Dispatchers.IO) { accountClient.bindDevice(currentDeviceId, currentSpeakerName) }
                                                    .onSuccess { devs -> accountState = accountState.copy(devices = devs) }
                                            }
                                        }.onFailure { accountError = it.message }
                                        accountBusy = false
                                    }
                                },
                                onAccountLogin = { u, p ->
                                    accountBusy = true; accountError = null
                                    scope.launch {
                                        val res = withContext(Dispatchers.IO) { accountClient.login(u, p) }
                                        res.onSuccess { st ->
                                            accountState = st
                                            val tok = st.vault.yandexMusicToken
                                            if (tok.isNotBlank() && !status.ymusic.loggedIn) {
                                                storage.saveYMusicToken(tok)
                                                client.setDirectYMusicAuth(tok)
                                                client.ymusicLogin(tok)
                                            }
                                        }.onFailure { accountError = it.message }
                                        accountBusy = false
                                    }
                                },
                                onAccountSync = {
                                    accountBusy = true; accountError = null
                                    scope.launch {
                                        val res = withContext(Dispatchers.IO) { accountClient.syncVault() }
                                        res.onSuccess { st ->
                                            accountState = st
                                            val tok = st.vault.yandexMusicToken
                                            if (tok.isNotBlank() && !status.ymusic.loggedIn) {
                                                storage.saveYMusicToken(tok)
                                                client.setDirectYMusicAuth(tok)
                                                client.ymusicLogin(tok)
                                            }
                                        }.onFailure { accountError = it.message }
                                        accountBusy = false
                                    }
                                },
                                onAccountLogout = {
                                    scope.launch {
                                        withContext(Dispatchers.IO) { accountClient.logout() }
                                        accountState = AtriAccountState()
                                    }
                                },
                                onBindCurrentSpeaker = {
                                    if (currentDeviceId.isNotBlank()) {
                                        accountBusy = true
                                        scope.launch {
                                            withContext(Dispatchers.IO) { accountClient.bindDevice(currentDeviceId, currentSpeakerName) }
                                                .onSuccess { devs -> accountState = accountState.copy(devices = devs) }
                                                .onFailure { accountError = it.message }
                                            accountBusy = false
                                        }
                                    }
                                },
                                onUnbindSpeaker = { did ->
                                    accountBusy = true
                                    scope.launch {
                                        withContext(Dispatchers.IO) { accountClient.unbindDevice(did) }
                                            .onSuccess { devs -> accountState = accountState.copy(devices = devs) }
                                            .onFailure { accountError = it.message }
                                        accountBusy = false
                                    }
                                },
                                onYandexLoginToken = { tok ->
                                    storage.saveYMusicToken(tok)
                                    client.setDirectYMusicAuth(tok)
                                    client.ymusicLogin(tok)
                                    if (accountState.loggedIn) {
                                        scope.launch {
                                            val updated = accountState.vault.copy(yandexMusicToken = tok)
                                            withContext(Dispatchers.IO) { accountClient.saveVault(updated) }
                                                .onSuccess { accountState = it }
                                        }
                                    }
                                },
                                onYandexLogout = {
                                    storage.saveYMusicToken("")
                                    client.ymusicLogout()
                                    if (accountState.loggedIn) {
                                        scope.launch {
                                            val updated = accountState.vault.copy(yandexMusicToken = "", yandexMusicLogin = "")
                                            withContext(Dispatchers.IO) { accountClient.saveVault(updated) }
                                                .onSuccess { accountState = it }
                                        }
                                    }
                                },
                                onToggleYnison = { enabled ->
                                    status = status.copy(ymusic = status.ymusic.copy(ynisonEnabled = enabled))
                                    client.setYnisonEnabled(enabled)
                                }
                            )
                        }

                        AppTab.TIMERS -> {
                            LazyColumn(
                                modifier = Modifier.fillMaxSize().statusBarsPadding().padding(horizontal = 16.dp),
                                verticalArrangement = Arrangement.spacedBy(14.dp),
                                contentPadding = PaddingValues(vertical = 14.dp)
                            ) {
                                item {
                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.SpaceBetween,
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        Text("Таймеры и будильники", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 24.sp)
                                        IconButton(onClick = { client.getTimers(); client.getAlarms() }) {
                                            Icon(Icons.Rounded.Refresh, contentDescription = "Обновить", tint = Color.White)
                                        }
                                    }
                                }
                                item {
                                    TimersAndAlarmsCard(
                                        timers = timers,
                                        alarms = alarms,
                                        onRefresh = { client.getTimers(); client.getAlarms() },
                                        onAddTimer = { secs ->
                                            val nowSec = System.currentTimeMillis() / 1000L
                                            val nextId = (timers.maxOfOrNull { it.id } ?: 0) + 1
                                            timers = timers + TimerItem(id = nextId, fireAt = nowSec + secs, totalSecs = secs, remainingSecs = secs)
                                            client.setTimer(secs)
                                        },
                                        onCancelTimer = { id ->
                                            timers = if (id == null) emptyList() else timers.filterNot { it.id == id }
                                            client.cancelTimers(id)
                                        },
                                        onAddAlarm = { h, m -> client.setAlarm(h, m) },
                                        onCancelAlarm = { id -> client.cancelAlarms(id) },
                                        showHeader = false
                                    )
                                }
                            }
                        }

                        AppTab.SETTINGS -> {
                            BackHandler(enabled = settingsPage != SettingsPage.ROOT && !showBigPlayer && !showVolumeSheet && !showEqSheet) {
                                settingsPage = SettingsPage.ROOT
                            }
                            AnimatedContent(
                                targetState = settingsPage,
                                transitionSpec = {
                                    val forward = targetState != SettingsPage.ROOT
                                    val dir = if (forward) 1 else -1
                                    (slideInHorizontally(spring(dampingRatio = 0.86f, stiffness = 340f)) { (it * 0.32f).toInt() * dir } +
                                        scaleIn(initialScale = 0.96f, animationSpec = tween(220)) +
                                        fadeIn(tween(200)))
                                        .togetherWith(
                                            slideOutHorizontally(spring(dampingRatio = 0.86f, stiffness = 340f)) { -(it * 0.25f).toInt() * dir } +
                                                scaleOut(targetScale = 0.96f, animationSpec = tween(170)) +
                                                fadeOut(tween(150))
                                        )
                                },
                                label = "settingsSubmenuTransition"
                            ) { curPage ->
                            LazyColumn(
                                modifier = Modifier.fillMaxSize().statusBarsPadding().padding(horizontal = 16.dp),
                                verticalArrangement = Arrangement.spacedBy(10.dp),
                                contentPadding = PaddingValues(top = 12.dp, bottom = 100.dp)
                            ) {
                                if (curPage == SettingsPage.ROOT) {
                                    item {
                                        val transportLabel = if (client.isRelayActive()) "Через интернет" else "Bluetooth"
                                        Row(
                                            modifier = Modifier.fillMaxWidth().padding(bottom = 2.dp),
                                            horizontalArrangement = Arrangement.SpaceBetween,
                                            verticalAlignment = Alignment.CenterVertically
                                        ) {
                                            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                                                Text("Настройки", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 26.sp)
                                                Row(
                                                    verticalAlignment = Alignment.CenterVertically,
                                                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                                                ) {
                                                    Surface(
                                                        shape = RoundedCornerShape(99.dp),
                                                        color = palette.accent.copy(alpha = 0.16f)
                                                    ) {
                                                        Text(
                                                            transportLabel,
                                                            color = palette.accent,
                                                            fontSize = 11.sp,
                                                            fontWeight = FontWeight.SemiBold,
                                                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp)
                                                        )
                                                    }
                                                    Text(
                                                        "${status.model}${if (status.fwVersion.isNotBlank()) " · v${status.fwVersion}" else ""}",
                                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                                        fontSize = 12.sp
                                                    )
                                                }
                                            }
                                            IconButton(
                                                onClick = { client.disconnect(); autoConnectState = "manual" },
                                                modifier = Modifier.size(38.dp).clip(CircleShape).background(OledCard)
                                            ) {
                                                Icon(Icons.Rounded.LinkOff, contentDescription = "Отключить колонку", tint = Color.White, modifier = Modifier.size(19.dp))
                                            }
                                        }
                                    }
                                    val curAppVer = AppUpdater.currentVersionName(context)
                                    val sections = listOf(
                                        "Подключение и аккаунт" to listOf(
                                            SettingsPage.ACCOUNT to if (status.ymusic.loggedIn) status.ymusic.displayName.ifBlank { status.ymusic.login.ifBlank { "Подключён" } } else "Не выполнен вход",
                                            SettingsPage.WIFI to if (status.wifiConnected) "${status.wifiSsid.ifBlank { "Wi-Fi OK" }} (${status.wifiIp})" else "Wi-Fi не подключён",
                                            SettingsPage.BLUETOOTH to "${if (status.btSpeakerMode) "BT-колонка вкл." else "BT-колонка выкл."} · ${if (status.btHidden) "Скрыта" else "Видима"}"
                                        ),
                                        "Интерфейс и время" to listOf(
                                            SettingsPage.PALETTE to palette.label,
                                            SettingsPage.TIME to status.timezone.ifBlank { "UTC" }
                                        ),
                                        "Система и диагностика" to listOf(
                                            SettingsPage.ABOUT to "Прошивка v${status.fwVersion.ifBlank { "—" }} · App v$curAppVer",
                                            SettingsPage.DEBUG to "Логи (${DebugLogStore.errorCount.get()} ош.), VQE, E2EE, ИИ"
                                        )
                                    )
                                    sections.forEach { (sectionTitle, entries) ->
                                        item {
                                            Text(
                                                sectionTitle,
                                                color = palette.accent,
                                                fontWeight = FontWeight.SemiBold,
                                                fontSize = 12.sp,
                                                modifier = Modifier.padding(start = 4.dp, top = 4.dp)
                                            )
                                        }
                                        item {
                                            Surface(
                                                shape = RoundedCornerShape(TypeUI.RadiusMd),
                                                color = TypeUI.SurfaceElevated,
                                                modifier = Modifier
                                                    .fillMaxWidth()
                                                    .border(1.dp, TypeUI.Hairline, RoundedCornerShape(TypeUI.RadiusMd))
                                            ) {
                                                Column(modifier = Modifier.fillMaxWidth()) {
                                                    entries.forEachIndexed { idx, (page, dynamicSub) ->
                                                        if (idx > 0) {
                                                            HorizontalDivider(
                                                                color = TypeUI.Hairline,
                                                                modifier = Modifier.padding(start = 62.dp)
                                                            )
                                                        }
                                                        Row(
                                                            modifier = Modifier
                                                                .fillMaxWidth()
                                                                .clickable {
                                                                    settingsPage = page
                                                                    if (page == SettingsPage.ABOUT) client.otaStatus()
                                                                }
                                                                .padding(horizontal = 14.dp, vertical = 12.dp),
                                                            verticalAlignment = Alignment.CenterVertically,
                                                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                                                        ) {
                                                            Box(
                                                                modifier = Modifier.size(36.dp).clip(CircleShape).background(palette.accent.copy(alpha = 0.14f)),
                                                                contentAlignment = Alignment.Center
                                                            ) {
                                                                Icon(page.icon, contentDescription = null, tint = palette.accent, modifier = Modifier.size(20.dp))
                                                            }
                                                            Column(modifier = Modifier.weight(1f)) {
                                                                Text(page.title, color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                                                                Text(dynamicSub, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                                            }
                                                            Icon(Icons.Rounded.ChevronRight, contentDescription = null, tint = Color(0xFF6B6B75), modifier = Modifier.size(20.dp))
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    item {
                                        Row(
                                            verticalAlignment = Alignment.CenterVertically,
                                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                                            modifier = Modifier.fillMaxWidth()
                                        ) {
                                            IconButton(
                                                onClick = { settingsPage = SettingsPage.ROOT },
                                                modifier = Modifier.size(38.dp).clip(CircleShape).background(OledCard)
                                            ) {
                                                Icon(Icons.Rounded.ArrowBack, contentDescription = "Назад", tint = Color.White)
                                            }
                                            Text(curPage.title, color = Color.White, fontWeight = FontWeight.Bold, fontSize = 22.sp)
                                        }
                                    }

                                    when (curPage) {
                                        SettingsPage.ACCOUNT -> {
                                            item {
                                                Surface(shape = RoundedCornerShape(22.dp), color = OledCard) {
                                                    Column(
                                                        modifier = Modifier.fillMaxWidth().padding(18.dp),
                                                        verticalArrangement = Arrangement.spacedBy(12.dp)
                                                    ) {
                                                        Text(
                                                            "Управление аккаунтом AtriOS и интеграцией Яндекс Музыки (Ynison) перенесено во вкладку «Сервисы».",
                                                            color = Color(0xFFB0B0C0),
                                                            fontSize = 14.sp
                                                        )
                                                        Button(
                                                            onClick = {
                                                                settingsPage = SettingsPage.ROOT
                                                                selectedTab = AppTab.INTEGRATIONS
                                                            },
                                                            colors = ButtonDefaults.buttonColors(containerColor = palette.accent, contentColor = Color.Black)
                                                        ) {
                                                            Text("Открыть «Сервисы»", fontWeight = FontWeight.SemiBold)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        SettingsPage.WIFI -> {
                                            item {
                                                WifiCard(
                                                    wifiConnected = status.wifiConnected,
                                                    wifiSsid = status.wifiSsid,
                                                    wifiIp = status.wifiIp,
                                                    onConnectWifi = { ssid, psk -> client.wifiConnect(ssid, psk) }
                                                )
                                            }
                                            item {
                                                Surface(shape = RoundedCornerShape(22.dp), color = OledCard) {
                                                    Row(
                                                        modifier = Modifier.fillMaxWidth().padding(16.dp),
                                                        verticalAlignment = Alignment.CenterVertically,
                                                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                                                    ) {
                                                        Icon(
                                                            if (relayConfig.token.isNotBlank()) Icons.Rounded.CloudDone else Icons.Rounded.CloudOff,
                                                            contentDescription = null,
                                                            tint = if (relayConfig.token.isNotBlank()) palette.accent else Color(0xFF6B6B75)
                                                        )
                                                        Column {
                                                            Text(
                                                                if (relayConfig.token.isNotBlank()) "Интернет-пара активна" else "Интернет-пара не настроена",
                                                                color = Color.White,
                                                                fontWeight = FontWeight.SemiBold
                                                            )
                                                            Text(
                                                                if (relayConfig.token.isNotBlank()) "ID: ${relayConfig.deviceId} · автопереход при отключении BT"
                                                                else "Подключитесь 1 раз по Bluetooth для автопривязки",
                                                                style = MaterialTheme.typography.labelSmall,
                                                                color = MaterialTheme.colorScheme.onSurfaceVariant
                                                            )
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        SettingsPage.BLUETOOTH -> {
                                            item {
                                                BluetoothCard(
                                                    status = status,
                                                    onToggleSpeakerMode = { enabled -> client.setBtSpeakerMode(enabled) },
                                                    onPair = { secs -> client.btPair(secs) },
                                                    onHide = { client.btHide() },
                                                    onUnpair = { mac -> client.btUnpair(mac) }
                                                )
                                            }
                                        }
                                        SettingsPage.PALETTE -> {
                                            item {
                                                Surface(shape = RoundedCornerShape(22.dp), color = OledCard) {
                                                    Column(Modifier.fillMaxWidth().padding(18.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                                                        Text("Выберите палитру приложения", color = Color.White, fontWeight = FontWeight.SemiBold)
                                                        AppPalette.entries.forEach { p ->
                                                            val sel = p.id == palette.id
                                                            Row(
                                                                modifier = Modifier
                                                                    .fillMaxWidth()
                                                                    .clip(RoundedCornerShape(16.dp))
                                                                    .background(if (sel) p.accent.copy(alpha = 0.14f) else OledCardHigh)
                                                                    .border(1.dp, if (sel) p.accent else Color.Transparent, RoundedCornerShape(16.dp))
                                                                    .clickable { onPaletteChange(p) }
                                                                    .padding(horizontal = 14.dp, vertical = 12.dp),
                                                                verticalAlignment = Alignment.CenterVertically,
                                                                horizontalArrangement = Arrangement.spacedBy(12.dp)
                                                            ) {
                                                                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                                                    Box(Modifier.size(20.dp).clip(CircleShape).background(p.aura1))
                                                                    Box(Modifier.size(20.dp).clip(CircleShape).background(p.aura2))
                                                                    Box(Modifier.size(20.dp).clip(CircleShape).background(p.aura3))
                                                                }
                                                                Text(p.label, color = Color.White, fontWeight = if (sel) FontWeight.Bold else FontWeight.Medium, modifier = Modifier.weight(1f))
                                                                if (sel) Icon(Icons.Rounded.CheckCircle, contentDescription = null, tint = p.accent)
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            item {
                                                val savedCustom = remember { storage.loadCustomColors() }
                                                var customAccent by remember {
                                                    mutableStateOf(if (palette.id == "custom") palette.accent else Color(savedCustom.first))
                                                }
                                                var customSecondary by remember {
                                                    mutableStateOf(if (palette.id == "custom") palette.aura2 else Color(savedCustom.second))
                                                }
                                                var accentHue by remember {
                                                    mutableFloatStateOf(
                                                        FloatArray(3).also {
                                                            android.graphics.Color.colorToHSV(customAccent.toArgb(), it)
                                                        }[0]
                                                    )
                                                }
                                                var secondaryHue by remember {
                                                    mutableFloatStateOf(
                                                        FloatArray(3).also {
                                                            android.graphics.Color.colorToHSV(customSecondary.toArgb(), it)
                                                        }[0]
                                                    )
                                                }
                                                val swatches = listOf(
                                                    Color(0xFF00E5FF), Color(0xFFFF2E7A), Color(0xFFFFEE00),
                                                    Color(0xFF00E676), Color(0xFFB388FF), Color(0xFFFF6D00),
                                                    Color(0xFF448AFF), Color(0xFFFF5252)
                                                )
                                                val isCustomActive = palette.id == "custom"
                                                Surface(
                                                    shape = RoundedCornerShape(22.dp),
                                                    color = OledCard,
                                                    modifier = Modifier.border(
                                                        1.dp,
                                                        if (isCustomActive) palette.accent else Color.White.copy(alpha = 0.06f),
                                                        RoundedCornerShape(22.dp)
                                                    )
                                                ) {
                                                    Column(Modifier.fillMaxWidth().padding(18.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                                                        Row(
                                                            modifier = Modifier.fillMaxWidth(),
                                                            horizontalArrangement = Arrangement.SpaceBetween,
                                                            verticalAlignment = Alignment.CenterVertically
                                                        ) {
                                                            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                                                Icon(Icons.Rounded.ColorLens, contentDescription = null, tint = customAccent)
                                                                Text("Своя тема", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                                                            }
                                                            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                                                Box(Modifier.size(20.dp).clip(CircleShape).background(customAccent))
                                                                Box(Modifier.size(20.dp).clip(CircleShape).background(customSecondary))
                                                            }
                                                        }
                                                        Text("Акцентный цвет", color = Color(0xFFA0A0B0), fontSize = 12.sp)
                                                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                                            swatches.forEach { sw ->
                                                                Box(
                                                                    modifier = Modifier
                                                                        .size(28.dp)
                                                                        .clip(CircleShape)
                                                                        .background(sw)
                                                                        .border(
                                                                            if (sw.toArgb() == customAccent.toArgb()) 2.dp else 0.dp,
                                                                            Color.White,
                                                                            CircleShape
                                                                        )
                                                                        .clickable {
                                                                            customAccent = sw
                                                                            accentHue = FloatArray(3).also {
                                                                                android.graphics.Color.colorToHSV(sw.toArgb(), it)
                                                                            }[0]
                                                                            onPaletteChange(AppPalette.custom(sw.toArgb(), customSecondary.toArgb()))
                                                                        }
                                                                )
                                                            }
                                                        }
                                                        Slider(
                                                            value = accentHue,
                                                            onValueChange = { h ->
                                                                accentHue = h
                                                                val argb = android.graphics.Color.HSVToColor(floatArrayOf(h, 0.92f, 1.0f))
                                                                customAccent = Color(argb)
                                                                onPaletteChange(AppPalette.custom(argb, customSecondary.toArgb()))
                                                            },
                                                            valueRange = 0f..360f,
                                                            colors = SliderDefaults.colors(thumbColor = customAccent, activeTrackColor = customAccent)
                                                        )
                                                        Text("Цвет свечения сферы", color = Color(0xFFA0A0B0), fontSize = 12.sp)
                                                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                                            swatches.forEach { sw ->
                                                                Box(
                                                                    modifier = Modifier
                                                                        .size(28.dp)
                                                                        .clip(CircleShape)
                                                                        .background(sw)
                                                                        .border(
                                                                            if (sw.toArgb() == customSecondary.toArgb()) 2.dp else 0.dp,
                                                                            Color.White,
                                                                            CircleShape
                                                                        )
                                                                        .clickable {
                                                                            customSecondary = sw
                                                                            secondaryHue = FloatArray(3).also {
                                                                                android.graphics.Color.colorToHSV(sw.toArgb(), it)
                                                                            }[0]
                                                                            onPaletteChange(AppPalette.custom(customAccent.toArgb(), sw.toArgb()))
                                                                        }
                                                                )
                                                            }
                                                        }
                                                        Slider(
                                                            value = secondaryHue,
                                                            onValueChange = { h ->
                                                                secondaryHue = h
                                                                val argb = android.graphics.Color.HSVToColor(floatArrayOf(h, 0.92f, 1.0f))
                                                                customSecondary = Color(argb)
                                                                onPaletteChange(AppPalette.custom(customAccent.toArgb(), argb))
                                                            },
                                                            valueRange = 0f..360f,
                                                            colors = SliderDefaults.colors(thumbColor = customSecondary, activeTrackColor = customSecondary)
                                                        )
                                                    }
                                                }
                                            }
                                        }
                                        SettingsPage.TIME -> {
                                            item {
                                                TimeAndTimezoneCard(
                                                    localTime = status.localTime,
                                                    timezone = status.timezone,
                                                    onSyncNtp = { client.ntpSync() },
                                                    onSetTimezone = { tz -> client.setTimezone(tz) }
                                                )
                                            }
                                        }
                                        SettingsPage.ABOUT -> {
                                            item {
                                                Surface(shape = RoundedCornerShape(22.dp), color = OledCard) {
                                                    Column(Modifier.fillMaxWidth().padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                                                        Text("Информация об устройстве", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                                                        InfoRow("Модель", status.model)
                                                        InfoRow("Кодовое имя", status.codename.ifBlank { "—" })
                                                        InfoRow("Ревизия", status.rev.ifBlank { "—" })
                                                        InfoRow("Платформа (SoC)", status.soc.substringBefore("(").trim().ifBlank { "Amlogic S905X3" })
                                                        InfoRow("Прошивка", "v${status.fwVersion}")
                                                        if (relayConfig.deviceId.isNotBlank()) {
                                                            InfoRow("Device ID", relayConfig.deviceId)
                                                        }
                                                    }
                                                }
                                            }
                                            item {
                                                OtaUpdateCard(
                                                    fwVersion = status.fwVersion,
                                                    codename = status.codename,
                                                    ota = status.ota,
                                                    onCheckUpdates = { client.otaCheck() },
                                                    onRefreshStatus = { client.otaStatus() },
                                                    onApplyUpdate = { reboot -> client.otaApply(reboot) },
                                                    onRollback = { reboot -> client.otaRollback(reboot) },
                                                    onRebootNow = { showRebootDialog = true }
                                                )
                                            }
                                            item {
                                                Surface(shape = RoundedCornerShape(22.dp), color = OledCard) {
                                                    Column(Modifier.fillMaxWidth().padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                                                        val curAppVer = remember { AppUpdater.currentVersionName(context) }
                                                        Row(
                                                            modifier = Modifier.fillMaxWidth(),
                                                            horizontalArrangement = Arrangement.SpaceBetween,
                                                            verticalAlignment = Alignment.CenterVertically
                                                        ) {
                                                            Text("Приложение AtriOS Control", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                                                            Text("v$curAppVer", color = palette.accent, fontWeight = FontWeight.SemiBold, fontSize = 13.sp)
                                                        }
                                                        if (appUpdateStatusMsg != null) {
                                                            Text(appUpdateStatusMsg!!, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 13.sp)
                                                        }
                                                        val rel = availableAppUpdate
                                                        if (rel != null) {
                                                            val prog = appUpdateProgress
                                                            if (prog != null) {
                                                                LinearProgressIndicator(progress = { prog }, modifier = Modifier.fillMaxWidth(), color = palette.accent)
                                                                Text("Загрузка APK: ${(prog * 100).toInt()}%", style = MaterialTheme.typography.bodySmall)
                                                            } else {
                                                                Button(
                                                                    onClick = {
                                                                        scope.launch {
                                                                            appUpdateProgress = 0f
                                                                            val res = AppUpdater.downloadAndInstall(context, rel) { p -> appUpdateProgress = p }
                                                                            appUpdateProgress = null
                                                                            res.onFailure { e -> snackbarHostState.showSnackbar("Ошибка загрузки APK: ${e.message}") }
                                                                        }
                                                                    },
                                                                    shape = RoundedCornerShape(99.dp)
                                                                ) {
                                                                    Icon(Icons.Rounded.Download, contentDescription = null)
                                                                    Spacer(Modifier.width(6.dp))
                                                                    Text("Обновить до v${rel.version}", fontWeight = FontWeight.Bold)
                                                                }
                                                            }
                                                        } else {
                                                            FilledTonalButton(
                                                                enabled = !checkingAppUpdate,
                                                                onClick = {
                                                                    scope.launch {
                                                                        checkingAppUpdate = true
                                                                        appUpdateStatusMsg = "Проверка обновлений..."
                                                                        val found = AppUpdater.checkForUpdate(context)
                                                                        availableAppUpdate = found
                                                                        checkingAppUpdate = false
                                                                        appUpdateStatusMsg = if (found != null) {
                                                                            "Доступна новая версия v${found.version}"
                                                                        } else {
                                                                            "Установлена актуальная версия v$curAppVer"
                                                                        }
                                                                    }
                                                                },
                                                                shape = RoundedCornerShape(99.dp)
                                                            ) {
                                                                if (checkingAppUpdate) {
                                                                    CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                                                                } else {
                                                                    Icon(Icons.Rounded.Refresh, contentDescription = null)
                                                                }
                                                                Spacer(Modifier.width(6.dp))
                                                                Text("Проверить обновления приложения")
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        SettingsPage.DEBUG -> {
                                            item {
                                                var debugSection by remember { mutableStateOf("ALL") }
                                                var logFilter by remember { mutableStateOf("ALL") }
                                                val clipboard = androidx.compose.ui.platform.LocalClipboardManager.current
                                                var logRev by remember { mutableIntStateOf(DebugLogStore.version) }
                                                DisposableEffect(Unit) {
                                                    DebugLogStore.onChanged = { logRev = DebugLogStore.version }
                                                    onDispose { DebugLogStore.onChanged = null }
                                                }
                                                val filteredLogs = remember(logRev, logFilter) {
                                                    DebugLogStore.entries.filter { entry ->
                                                        when (logFilter) {
                                                            "ERR" -> entry.level == "ERR" || entry.level == "WARN"
                                                            "RELAY" -> entry.tag == "RELAY" || entry.tag == "OFFLOAD"
                                                            "BT" -> entry.tag == "BT"
                                                            "YM" -> entry.tag == "DIRECT_YM" || entry.message.contains("ymusic", ignoreCase = true)
                                                            "E2EE" -> entry.tag == "E2EE"
                                                            else -> true
                                                        }
                                                    }.take(80)
                                                }

                                                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                                                    // Section selector pills
                                                    LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                                        val secChips = listOf(
                                                            "ALL" to "Все",
                                                            "DIAG" to "Диагностика",
                                                            "LOGS" to "Логи (${DebugLogStore.entries.size})",
                                                            "E2EE" to "E2EE",
                                                            "HW" to "ИИ и LED"
                                                        )
                                                        items(secChips.size) { i ->
                                                            val (id, label) = secChips[i]
                                                            FilterChip(
                                                                selected = debugSection == id,
                                                                onClick = { debugSection = id },
                                                                label = { Text(label, fontSize = 12.sp) }
                                                            )
                                                        }
                                                    }

                                                    // 1. TOP: Live Connection & Hardware Diagnostics
                                                    if (debugSection == "ALL" || debugSection == "DIAG") {
                                                        Surface(
                                                            shape = RoundedCornerShape(TypeUI.RadiusLg),
                                                            color = TypeUI.SurfaceElevated,
                                                            modifier = Modifier.border(1.dp, TypeUI.Hairline, RoundedCornerShape(TypeUI.RadiusLg))
                                                        ) {
                                                            Column(
                                                                modifier = Modifier.fillMaxWidth().padding(16.dp),
                                                                verticalArrangement = Arrangement.spacedBy(7.dp)
                                                            ) {
                                                                Row(
                                                                    modifier = Modifier.fillMaxWidth(),
                                                                    horizontalArrangement = Arrangement.SpaceBetween,
                                                                    verticalAlignment = Alignment.CenterVertically
                                                                ) {
                                                                    Text("Соединение и телеметрия", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                                                                    Surface(
                                                                        shape = RoundedCornerShape(99.dp),
                                                                        color = palette.accent.copy(alpha = 0.16f)
                                                                    ) {
                                                                        Text(
                                                                            if (client.isRelayActive()) "TLS Relay" else "BT RFCOMM",
                                                                            color = palette.accent,
                                                                            fontSize = 11.sp,
                                                                            fontWeight = FontWeight.SemiBold,
                                                                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp)
                                                                        )
                                                                    }
                                                                }
                                                                InfoRow("Задержка (RTT)", "${DebugLogStore.lastLatencyMs} мс (ср. ${DebugLogStore.avgLatencyMs} мс)")
                                                                InfoRow("Запросы / Повторы / Ош.", "${DebugLogStore.totalRequests.get()} / ${DebugLogStore.retryCount.get()} / ${DebugLogStore.errorCount.get()}")
                                                                InfoRow("Кэш Яндекс Музыки", "Сфер: ${ymOrbs.size} · Коллекция: ${if (ymCollection.loaded) "${ymCollection.likesCount} л." else "—"}")
                                                                InfoRow("VQE / AEC", if (status.vqe.refActive) "ERLE %.1f дБ · SNR %.1f дБ · MIC #%d".format(status.vqe.erleDb, status.vqe.snrDb, status.vqe.bestMic) else "Ожидание loopback")
                                                                InfoRow("Задержка VQE / Barge-In", "${status.vqe.lagMs} мс · ${if (status.vqe.bargeIn) "Речь" else "Готово"}")
                                                            }
                                                        }
                                                        TelemetryCard(status = status)
                                                    }

                                                    // 2. Filterable Bounded Diagnostic Logs
                                                    if (debugSection == "ALL" || debugSection == "LOGS") {
                                                        Surface(
                                                            shape = RoundedCornerShape(TypeUI.RadiusLg),
                                                            color = TypeUI.SurfaceElevated,
                                                            modifier = Modifier.border(1.dp, TypeUI.Hairline, RoundedCornerShape(TypeUI.RadiusLg))
                                                        ) {
                                                            Column(
                                                                modifier = Modifier.fillMaxWidth().padding(16.dp),
                                                                verticalArrangement = Arrangement.spacedBy(10.dp)
                                                            ) {
                                                                Row(
                                                                    modifier = Modifier.fillMaxWidth(),
                                                                    horizontalArrangement = Arrangement.SpaceBetween,
                                                                    verticalAlignment = Alignment.CenterVertically
                                                                ) {
                                                                    Column {
                                                                        Text("Логи диагностики", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                                                                        Text(
                                                                            "Показано: ${filteredLogs.size} из ${DebugLogStore.entries.size}",
                                                                            color = Color(0xFF9A9AA6),
                                                                            fontSize = 11.sp
                                                                        )
                                                                    }
                                                                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                                                                        IconButton(
                                                                            onClick = {
                                                                                clipboard.setText(androidx.compose.ui.text.AnnotatedString(DebugLogStore.dumpText()))
                                                                                scope.launch { snackbarHostState.showSnackbar("Логи скопированы") }
                                                                            },
                                                                            modifier = Modifier.size(34.dp).clip(CircleShape).background(OledCard)
                                                                        ) {
                                                                            Icon(Icons.Rounded.ContentCopy, contentDescription = "Копировать", tint = Color.White, modifier = Modifier.size(16.dp))
                                                                        }
                                                                        IconButton(
                                                                            onClick = { DebugLogStore.clear() },
                                                                            modifier = Modifier.size(34.dp).clip(CircleShape).background(OledCard)
                                                                        ) {
                                                                            Icon(Icons.Rounded.DeleteOutline, contentDescription = "Очистить", tint = Color(0xFFFF6B81), modifier = Modifier.size(16.dp))
                                                                        }
                                                                    }
                                                                }
                                                                LazyRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                                                                    val filters = listOf("ALL" to "ALL", "ERR" to "ERR/WARN", "RELAY" to "RELAY", "BT" to "BT", "YM" to "YM", "E2EE" to "E2EE")
                                                                    items(filters.size) { i ->
                                                                        val (fid, flabel) = filters[i]
                                                                        FilterChip(
                                                                            selected = logFilter == fid,
                                                                            onClick = { logFilter = fid },
                                                                            label = { Text(flabel, fontSize = 11.sp) }
                                                                        )
                                                                    }
                                                                }
                                                                if (filteredLogs.isEmpty()) {
                                                                    Text(
                                                                        "Нет записей для фильтра «$logFilter».",
                                                                        color = Color(0xFF7A7A86),
                                                                        fontSize = 12.sp
                                                                    )
                                                                } else {
                                                                    Column(
                                                                        modifier = Modifier
                                                                            .fillMaxWidth()
                                                                            .heightIn(max = 240.dp)
                                                                            .clip(RoundedCornerShape(12.dp))
                                                                            .background(Color(0xFF0A0A0E))
                                                                            .verticalScroll(rememberScrollState())
                                                                            .padding(10.dp),
                                                                        verticalArrangement = Arrangement.spacedBy(4.dp)
                                                                    ) {
                                                                        filteredLogs.forEach { entry ->
                                                                            val col = when (entry.level) {
                                                                                "ERR" -> Color(0xFFFF5C77)
                                                                                "WARN" -> Color(0xFFFFC845)
                                                                                "OK" -> if (entry.tag == "OFFLOAD") Color(0xFF4DD0E1) else Color(0xFF69F0AE)
                                                                                else -> Color(0xFFB8B8C4)
                                                                            }
                                                                            Text(
                                                                                "${entry.time} [${entry.level}/${entry.tag}] ${entry.message}",
                                                                                color = col,
                                                                                fontSize = 11.sp,
                                                                                lineHeight = 14.sp,
                                                                                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                                                                            )
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }

                                                    // 3. E2EE Station Terminal
                                                    if (debugSection == "ALL" || debugSection == "E2EE") {
                                                        Surface(
                                                            shape = RoundedCornerShape(TypeUI.RadiusLg),
                                                            color = TypeUI.SurfaceElevated,
                                                            modifier = Modifier.border(1.dp, TypeUI.Hairline, RoundedCornerShape(TypeUI.RadiusLg))
                                                        ) {
                                                            Column(
                                                                modifier = Modifier.fillMaxWidth().padding(16.dp),
                                                                verticalArrangement = Arrangement.spacedBy(10.dp)
                                                            ) {
                                                                Row(
                                                                    modifier = Modifier.fillMaxWidth(),
                                                                    horizontalArrangement = Arrangement.SpaceBetween,
                                                                    verticalAlignment = Alignment.CenterVertically
                                                                ) {
                                                                    Column(modifier = Modifier.weight(1f)) {
                                                                        Text("Терминал станции (E2EE)", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                                                                        Text(
                                                                            termE2eeStatus,
                                                                            color = if (termE2eeReady) Color(0xFF69F0AE) else Color(0xFF9A9AA6),
                                                                            fontSize = 11.sp,
                                                                            maxLines = 1,
                                                                            overflow = TextOverflow.Ellipsis
                                                                        )
                                                                    }
                                                                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
                                                                        if (termOutputLog.isNotBlank()) {
                                                                            IconButton(
                                                                                onClick = { termOutputLog = "" },
                                                                                modifier = Modifier.size(32.dp).clip(CircleShape).background(OledCard)
                                                                            ) {
                                                                                Icon(Icons.Rounded.DeleteOutline, contentDescription = "Очистить вывод", tint = Color(0xFFFF6B81), modifier = Modifier.size(16.dp))
                                                                            }
                                                                        }
                                                                        FilledTonalButton(
                                                                            onClick = { client.termE2eeInit() },
                                                                            shape = RoundedCornerShape(99.dp),
                                                                            contentPadding = PaddingValues(horizontal = 12.dp, vertical = 6.dp)
                                                                        ) {
                                                                            Icon(Icons.Rounded.Lock, contentDescription = null, modifier = Modifier.size(15.dp))
                                                                            Spacer(Modifier.width(4.dp))
                                                                            Text(if (termE2eeReady) "Обновить" else "Подключить", fontSize = 12.sp)
                                                                        }
                                                                    }
                                                                }
                                                                Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.fillMaxWidth()) {
                                                                    listOf("uptime", "free -m", "uname -a", "ps | head -n 12").forEach { qc ->
                                                                        Surface(
                                                                            shape = RoundedCornerShape(99.dp),
                                                                            color = OledCard,
                                                                            modifier = Modifier
                                                                                .border(1.dp, Color(0xFF33333A), RoundedCornerShape(99.dp))
                                                                                .clickable {
                                                                                    termOutputLog = (termOutputLog + "\n$ $qc").trim()
                                                                                    client.termE2eeExec(qc)
                                                                                }
                                                                        ) {
                                                                            Text(qc.substringBefore(" "), color = Color(0xFFD0D0DC), fontSize = 11.sp, modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp))
                                                                        }
                                                                    }
                                                                }
                                                                Row(
                                                                    verticalAlignment = Alignment.CenterVertically,
                                                                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                                                                    modifier = Modifier.fillMaxWidth()
                                                                ) {
                                                                    OutlinedTextField(
                                                                        value = termCmdInput,
                                                                        onValueChange = { termCmdInput = it },
                                                                        placeholder = { Text("sh команда (шифруется E2EE)...", fontSize = 12.sp) },
                                                                        singleLine = true,
                                                                        modifier = Modifier.weight(1f),
                                                                        shape = RoundedCornerShape(12.dp)
                                                                    )
                                                                    IconButton(
                                                                        onClick = {
                                                                            val cmd = termCmdInput.trim()
                                                                            if (cmd.isNotEmpty()) {
                                                                                termOutputLog = (termOutputLog + "\n$ $cmd").trim()
                                                                                client.termE2eeExec(cmd)
                                                                                termCmdInput = ""
                                                                            }
                                                                        },
                                                                        modifier = Modifier.size(44.dp).clip(CircleShape).background(palette.accent)
                                                                    ) {
                                                                        Icon(Icons.Rounded.PlayArrow, contentDescription = "Выполнить", tint = palette.onAccent)
                                                                    }
                                                                }
                                                                if (termOutputLog.isNotBlank()) {
                                                                    Column(
                                                                        modifier = Modifier
                                                                            .fillMaxWidth()
                                                                            .heightIn(max = 200.dp)
                                                                            .clip(RoundedCornerShape(10.dp))
                                                                            .background(Color(0xFF070A08))
                                                                            .verticalScroll(rememberScrollState())
                                                                            .padding(10.dp)
                                                                    ) {
                                                                        Text(
                                                                            termOutputLog,
                                                                            color = Color(0xFF7CFF9E),
                                                                            fontSize = 11.sp,
                                                                            lineHeight = 14.sp,
                                                                            fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                                                                        )
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }

                                                    // 4. AI / Qwen3-TTS, LED Ring/Matrix & Audio Test
                                                    if (debugSection == "ALL" || debugSection == "HW") {
                                                        AiAssistantCard(
                                                            ai = status.ai,
                                                            onApplyAi = { enabled, mode, server, wake, voice, emotion ->
                                                                client.setAi(enabled = enabled, mode = mode, server = server, wakeWord = wake, ttsVoice = voice, ttsEmotion = emotion)
                                                            }
                                                        )
                                                        LedAndScreenCard(
                                                            capabilities = status.capabilities,
                                                            currentScreenMode = status.screenMode,
                                                            currentRingMode = status.ringMode,
                                                            onSetScreen = { mode, st, txt -> client.setScreen(mode, st, txt) },
                                                            onSetRing = { mode -> status = status.copy(ringMode = mode); client.setRing(mode) }
                                                        )
                                                        Surface(
                                                            shape = RoundedCornerShape(TypeUI.RadiusLg),
                                                            color = TypeUI.SurfaceElevated,
                                                            modifier = Modifier.border(1.dp, TypeUI.Hairline, RoundedCornerShape(TypeUI.RadiusLg))
                                                        ) {
                                                            Row(
                                                                modifier = Modifier.fillMaxWidth().padding(18.dp),
                                                                horizontalArrangement = Arrangement.SpaceBetween,
                                                                verticalAlignment = Alignment.CenterVertically
                                                            ) {
                                                                Column(modifier = Modifier.weight(1f)) {
                                                                    Text("Тест динамиков", color = Color.White, fontWeight = FontWeight.SemiBold)
                                                                    Text("Проверка аудиовыхода колонки", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                                                }
                                                                FilledTonalButton(onClick = { client.audioTest() }, shape = RoundedCornerShape(TypeUI.RadiusPill)) {
                                                                    Icon(Icons.Rounded.GraphicEq, contentDescription = null, modifier = Modifier.size(18.dp))
                                                                    Spacer(Modifier.width(6.dp))
                                                                    Text("Тест")
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        SettingsPage.ROOT -> Unit
                                    }
                                }
                            }
                            }
                        }
                    }
                }
            }

            // Volume quick-sheet overlay accessible right from the main Wave screen!
            if (showVolumeSheet) {
                ModalBottomSheet(
                    onDismissRequest = { showVolumeSheet = false },
                    containerColor = Color(0xFF141418)
                ) {
                    Box(modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp).padding(bottom = 24.dp)) {
                        AudioCard(
                            volume = sliderVol,
                            mute = status.mute,
                            speakersDesc = status.speakersDesc,
                            onVolumeChange = { dispatchVolumeLive(it, forceCommit = false) },
                            onVolumeCommit = { dispatchVolumeLive(latestSliderPctRef.get().toFloat(), forceCommit = true) },
                            onToggleMute = {
                                val nextMute = !status.mute
                                val nowMs = System.currentTimeMillis()
                                lastVolumeDragRef.set(nowMs)
                                pendingVolUntilRef.set(nowMs + 2500L)
                                if (nextMute) {
                                    pendingVolRef.set(0)
                                    latestSliderPctRef.set(0)
                                    sliderVol = 0f
                                    status = status.copy(volume = 0, mute = true)
                                } else {
                                    pendingVolRef.set(-1)
                                    status = status.copy(mute = false)
                                }
                                client.setMute(nextMute)
                            },
                            onAudioTest = { client.audioTest() },
                            showAudioTest = false
                        )
                    }
                }
            }

            // 5-Band Live Equalizer Bottom Sheet!
            if (showEqSheet) {
                val eqSheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
                ModalBottomSheet(
                    onDismissRequest = { showEqSheet = false },
                    sheetState = eqSheetState,
                    containerColor = Color(0xFF141418)
                ) {
                    EqualizerPanel(
                        eq = status.eq,
                        palette = palette,
                        onApplyEq = { en, pr, b ->
                            pendingEqUntilRef.set(System.currentTimeMillis() + 2500L)
                            status = status.copy(eq = status.eq.copy(enabled = en, preset = pr, bands = b))
                            client.setEq(en, pr, b)
                        }
                    )
                }
            }
        }

        if (showRebootDialog) {
            AlertDialog(
                onDismissRequest = { showRebootDialog = false },
                title = { Text("Перезагрузить колонку?") },
                text = { Text("Соединение будет временно прервано на время перезагрузки.") },
                confirmButton = {
                    Button(onClick = { showRebootDialog = false; client.reboot() }) { Text("Перезагрузить") }
                },
                dismissButton = {
                    TextButton(onClick = { showRebootDialog = false }) { Text("Отмена") }
                }
            )
        }
    }
}

@Composable
private fun MiniPlayerCard(
    title: String,
    artist: String,
    coverUrl: String,
    liked: Boolean,
    playing: Boolean,
    progressFrac: Float,
    palette: AppPalette,
    interactive: Boolean,
    onOpenBigPlayer: () -> Unit,
    onLike: () -> Unit,
    onTogglePlay: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .then(
                if (palette.isYandexOriginal) {
                    Modifier.border(1.dp, Color(0xFF333333), RoundedCornerShape(18.dp))
                } else Modifier
            )
            .then(if (interactive) Modifier.clickable { onOpenBigPlayer() } else Modifier),
        shape = RoundedCornerShape(if (palette.isYandexOriginal) 18.dp else 22.dp),
        color = if (palette.isYandexOriginal) Color(0xFF141414) else Color(0xFF1A1A1F)
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
            if (palette.isYandexOriginal) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(2.5.dp)
                        .background(Color(0xFF2B2B2B))
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth(progressFrac)
                            .fillMaxHeight()
                            .background(palette.accent)
                    )
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                AsyncCoverArt(url = coverUrl, shape = RoundedCornerShape(10.dp), modifier = Modifier.size(42.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(title.ifBlank { "Моя волна" }, color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    if (artist.isNotBlank()) {
                        Text(artist, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
                IconButton(onClick = { if (interactive) onLike() }, modifier = Modifier.size(32.dp)) {
                    Icon(if (liked) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder, contentDescription = null, tint = if (liked) Color(0xFFFF2E7A) else Color.White, modifier = Modifier.size(20.dp))
                }
                IconButton(
                    onClick = { if (interactive) onTogglePlay() },
                    modifier = Modifier.size(34.dp)
                ) {
                    Icon(
                        if (playing) Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                        contentDescription = null,
                        tint = if (palette.isYandexOriginal) palette.accent else Color.White,
                        modifier = Modifier.size(26.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun AtriIdOfflineLoginCard(
    palette: AppPalette,
    busy: Boolean,
    error: String?,
    onLogin: (String, String) -> Unit
) {
    var username by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    val passFocus = remember { FocusRequester() }
    val googlePicker = rememberLauncherForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { res ->
        if (res.resultCode == Activity.RESULT_OK) {
            val email = res.data?.getStringExtra(AccountManager.KEY_ACCOUNT_NAME)?.trim().orEmpty()
            if (email.isNotBlank()) {
                username = email
                runCatching { passFocus.requestFocus() }
            }
        }
    }

    Surface(shape = RoundedCornerShape(22.dp), color = OledCard) {
        Column(Modifier.fillMaxWidth().padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Icon(Icons.Rounded.VerifiedUser, contentDescription = null, tint = palette.accent)
                Text("Войти по AtriOS ID", fontWeight = FontWeight.Bold, color = Color.White, fontSize = 16.sp)
            }
            Text(
                "Восстановите колонку из защищённого облака без повторной настройки по Bluetooth.",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                fontSize = 12.sp
            )
            OutlinedButton(
                onClick = {
                    val intent = AccountManager.newChooseAccountIntent(
                        null, null, arrayOf("com.google"), null, null, null, null
                    )
                    googlePicker.launch(intent)
                },
                enabled = !busy,
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(14.dp)
            ) {
                Icon(Icons.Rounded.Email, contentDescription = null, tint = palette.accent, modifier = Modifier.size(18.dp))
                Spacer(Modifier.width(8.dp))
                Text("Выбрать почту Google", color = Color.White)
            }
            OutlinedTextField(
                value = username,
                onValueChange = { username = it },
                label = { Text("Почта Google или логин") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(14.dp)
            )
            OutlinedTextField(
                value = password,
                onValueChange = { password = it },
                label = { Text("Мастер-пароль") },
                visualTransformation = PasswordVisualTransformation(),
                singleLine = true,
                modifier = Modifier.fillMaxWidth().focusRequester(passFocus),
                shape = RoundedCornerShape(14.dp)
            )
            if (!error.isNullOrBlank()) {
                Text(error, color = Color(0xFFFF6B6B), fontSize = 12.sp)
            }
            Button(
                onClick = { onLogin(username, password) },
                enabled = !busy && username.trim().length >= 3 && password.length >= 6,
                modifier = Modifier.fillMaxWidth().height(48.dp),
                shape = RoundedCornerShape(99.dp),
                colors = ButtonDefaults.buttonColors(containerColor = palette.accent, contentColor = palette.onAccent)
            ) {
                if (busy) {
                    CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp, color = palette.onAccent)
                } else {
                    Text("Войти и подключиться", fontWeight = FontWeight.Bold)
                }
            }
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 14.sp)
        Text(value, color = Color.White, fontWeight = FontWeight.Medium, fontSize = 14.sp)
    }
}
