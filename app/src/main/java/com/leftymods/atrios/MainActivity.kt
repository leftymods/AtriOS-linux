package com.leftymods.atrios

import android.Manifest
import android.annotation.SuppressLint
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
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
import com.leftymods.atrios.model.YMusicOrb
import com.leftymods.atrios.model.YMusicTrack
import com.leftymods.atrios.ui.AiAssistantCard
import com.leftymods.atrios.ui.AsyncCoverArt
import com.leftymods.atrios.ui.AudioCard
import com.leftymods.atrios.ui.BigPlayerOverlay
import com.leftymods.atrios.ui.EqualizerPanel
import com.leftymods.atrios.ui.SwipeableTwoCardRibbon
import com.leftymods.atrios.ui.BluetoothCard
import com.leftymods.atrios.ui.DevicePickerSection
import com.leftymods.atrios.ui.LedAndScreenCard
import com.leftymods.atrios.ui.OtaUpdateCard
import com.leftymods.atrios.ui.TelemetryCard
import com.leftymods.atrios.ui.TimeAndTimezoneCard
import com.leftymods.atrios.ui.TimersAndAlarmsCard
import com.leftymods.atrios.ui.WifiCard
import com.leftymods.atrios.ui.YandexAccountCard
import com.leftymods.atrios.ui.YandexWaveFullScreen
import com.leftymods.atrios.ui.theme.AppPalette
import com.leftymods.atrios.ui.theme.AtriOSTheme
import com.leftymods.atrios.ui.theme.OledBlack
import com.leftymods.atrios.ui.theme.OledCard
import com.leftymods.atrios.ui.theme.OledCardHigh
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

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
    WAVE("Волна", Icons.Rounded.AutoAwesome),
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
    var palette by remember { mutableStateOf(AppPalette.fromId(storage.loadPaletteId())) }

    AtriOSTheme(palette = palette) {
        AtriOSApp(
            storage = storage,
            palette = palette,
            onPaletteChange = { p ->
                palette = p
                storage.savePaletteId(p.id)
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
    var savedSpeakers by remember { mutableStateOf(storage.loadSpeakers()) }
    var relayConfig by remember { mutableStateOf(storage.loadRelayConfig()) }

    var selectedTab by remember { mutableStateOf(AppTab.WAVE) }
    var settingsPage by remember { mutableStateOf(SettingsPage.ROOT) }
    var showBigPlayer by remember { mutableStateOf(false) }
    val lastUserActionRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val rejectedOldTitleRef = remember { java.util.concurrent.atomic.AtomicReference<String?>(null) }
    val rejectedUntilRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val lastVolumeDragRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val lastVolumeSentRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    val lastVolumeSentPctRef = remember { java.util.concurrent.atomic.AtomicInteger(-1) }
    val lastNextSkipMsRef = remember { java.util.concurrent.atomic.AtomicLong(0L) }
    var isTrackTransitioning by remember { mutableStateOf(false) }
    var showVolumeSheet by remember { mutableStateOf(false) }
    var showEqSheet by remember { mutableStateOf(false) }
    var liveElapsedMs by remember(status.ymusic.trackId) { mutableLongStateOf(status.ymusic.positionMs.coerceAtLeast(0L)) }
    LaunchedEffect(status.ymusic.positionMs) {
        val srv = status.ymusic.positionMs.coerceAtLeast(0L)
        if (kotlin.math.abs(srv - liveElapsedMs) >= 2000L) liveElapsedMs = srv
    }
    LaunchedEffect(status.ymusic.trackId, status.ymusic.state) {
        while (status.ymusic.state == "playing") {
            delay(500)
            val dur = status.ymusic.durationMs
            liveElapsedMs = if (dur > 0L) (liveElapsedMs + 500L).coerceAtMost(dur) else (liveElapsedMs + 500L)
        }
    }
    var checkingAppUpdate by remember { mutableStateOf(false) }
    var appUpdateStatusMsg by remember { mutableStateOf<String?>(null) }
    var showRebootDialog by remember { mutableStateOf(false) }
    var availableAppUpdate by remember { mutableStateOf<AppReleaseInfo?>(null) }
    var appUpdateProgress by remember { mutableStateOf<Float?>(null) }

    LaunchedEffect(Unit) {
        availableAppUpdate = AppUpdater.checkForUpdate(context)
    }

    val client = remember {
        BtControlClient(
            scope = scope,
            onStatus = { s ->
                val nowMs = System.currentTimeMillis()
                val lockedYm = nowMs - lastUserActionRef.get() < 2200L
                val rejectedOld = nowMs < rejectedUntilRef.get() &&
                    s.ymusic.title.isNotBlank() && s.ymusic.title == rejectedOldTitleRef.get()
                val lockedVol = nowMs - lastVolumeDragRef.get() < 900L
                val nextYm = if (lockedYm || rejectedOld) status.ymusic else s.ymusic
                status = if (lockedVol) {
                    s.copy(ymusic = nextYm, volume = sliderVol.toInt(), mute = sliderVol.toInt() == 0)
                } else {
                    sliderVol = s.volume.toFloat()
                    s.copy(ymusic = nextYm)
                }
                activeDevice?.let { dev ->
                    savedSpeakers = storage.upsertSpeaker(
                        SavedSpeaker(
                            mac = dev.address,
                            name = dev.name ?: s.model,
                            model = s.model,
                            codename = s.codename,
                            fwVersion = s.fwVersion.ifBlank { s.ota.currentVersion },
                            lastSeenTs = System.currentTimeMillis()
                        )
                    )
                }
            },
            onVolumePush = { v, m ->
                if (System.currentTimeMillis() - lastVolumeDragRef.get() >= 900L) {
                    sliderVol = v.toFloat()
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
            onYMusicUpdate = { ym ->
                val nowMs = System.currentTimeMillis()
                val rejectedOld = nowMs < rejectedUntilRef.get() &&
                    ym.title.isNotBlank() && ym.title == rejectedOldTitleRef.get()
                if (rejectedOld) return@BtControlClient
                val locked = nowMs - lastUserActionRef.get() < 2200L
                if (!locked || (ym.title.isNotBlank() && ym.title != status.ymusic.title) || isTrackTransitioning) {
                    status = status.copy(ymusic = ym)
                    if (isTrackTransitioning && ym.trackId.isNotBlank() && !ym.trackId.startsWith("opt_")) {
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
                }
            }
        ).also { c ->
            c.setSavedRelayConfig(relayConfig.gatewayUrl, relayConfig.deviceId, relayConfig.token)
        }
    }

    fun dispatchVolumeLive(vol: Float, forceCommit: Boolean = false) {
        val nowMs = System.currentTimeMillis()
        lastVolumeDragRef.set(nowMs)
        val clamped = vol.coerceIn(0f, 100f)
        val pct = clamped.toInt()
        sliderVol = clamped
        status = status.copy(volume = pct, mute = pct == 0)
        val changed = pct != lastVolumeSentPctRef.get()
        if (changed && (forceCommit || nowMs - lastVolumeSentRef.get() >= 160L)) {
            lastVolumeSentRef.set(nowMs)
            lastVolumeSentPctRef.set(pct)
            client.setVolume(pct)
        }
    }

    fun dispatchYMusicControl(action: String) {
        val nowMs = System.currentTimeMillis()
        // Strict 1-skip-per-burst on forward skip (1150ms cooldown + lock until speaker confirms new track); NEVER block prev!
        if (action == "next") {
            if (isTrackTransitioning || nowMs - lastNextSkipMsRef.get() < 1150L) return
            lastNextSkipMsRef.set(nowMs)
        }
        lastUserActionRef.set(nowMs)
        val cur = status.ymusic
        when (action) {
            "pause" -> status = status.copy(ymusic = cur.copy(state = "paused"))
            "play" -> status = status.copy(ymusic = cur.copy(state = "playing"))
            "repeat" -> status = status.copy(ymusic = cur.copy(repeat = !cur.repeat))
            "like" -> status = status.copy(ymusic = cur.copy(liked = !cur.liked))
            "next" -> {
                isTrackTransitioning = true
                liveElapsedMs = 0L
                if (cur.title.isNotBlank()) {
                    rejectedOldTitleRef.set(cur.title)
                    rejectedUntilRef.set(nowMs + 2800L)
                }
                if (cur.nextTitle.isNotBlank()) {
                    status = status.copy(
                        ymusic = cur.copy(
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
                            positionMs = 0L
                        )
                    )
                }
            }
            "prev" -> {
                liveElapsedMs = 0L
                if (cur.title.isNotBlank()) {
                    rejectedOldTitleRef.set(cur.title)
                    rejectedUntilRef.set(nowMs + 2800L)
                }
                if (cur.prevTitle.isNotBlank()) {
                    status = status.copy(
                        ymusic = cur.copy(
                            trackId = "opt_prev_${nowMs}",
                            title = cur.prevTitle,
                            artist = cur.prevArtist,
                            coverUrl = cur.prevCoverUrl.ifBlank { cur.coverUrl },
                            nextTitle = cur.title,
                            nextArtist = cur.artist,
                            nextCoverUrl = cur.coverUrl,
                            positionMs = 0L
                        )
                    )
                }
            }
        }
        client.ymusicControl(action)
        if (action == "next" || action == "prev") {
            scope.launch {
                delay(750)
                client.ymusicStatus()
                delay(1800)
                isTrackTransitioning = false
            }
        }
    }

    val bondedDevices: List<BluetoothDevice> = remember(hasPerms, adapter, connected) {
        if (hasPerms && adapter != null) adapter.bondedDevices.toList() else emptyList()
    }

    // System Back gesture navigates cleanly inside the app instead of closing it!
    BackHandler(enabled = connected && (showBigPlayer || showVolumeSheet || settingsPage != SettingsPage.ROOT || selectedTab != AppTab.WAVE)) {
        when {
            showBigPlayer -> showBigPlayer = false
            showVolumeSheet -> showVolumeSheet = false
            selectedTab == AppTab.SETTINGS && settingsPage != SettingsPage.ROOT -> settingsPage = SettingsPage.ROOT
            selectedTab != AppTab.WAVE -> selectedTab = AppTab.WAVE
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
            client.ymusicStatus()
            client.ymusicOrbs()
            client.getTimers()
            client.getAlarms()
        }
    }

    LaunchedEffect(connected, status.ymusic.isActive) {
        while (connected && status.ymusic.isActive) {
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
                                    Text("Через интернет (HTTPS Relay)", fontWeight = FontWeight.Bold, color = Color.White)
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
                        // Synchronous 2-card ribbon bottom mini-player (entire card + cover + text + heart + pause drags, with next/prev card right behind!)
                        val ym = status.ymusic
                        if (selectedTab != AppTab.WAVE && (ym.isActive || ym.title.isNotBlank())) {
                            @Composable
                            fun MiniPlayerCard(title: String, artist: String, coverUrl: String, liked: Boolean, playing: Boolean, interactive: Boolean) {
                                Surface(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .then(
                                            if (palette.isYandexOriginal) {
                                                Modifier.border(1.dp, Color(0xFF333333), RoundedCornerShape(18.dp))
                                            } else Modifier
                                        )
                                        .then(if (interactive) Modifier.clickable { showBigPlayer = true } else Modifier),
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
                                                val frac = if (ym.durationMs > 0L) {
                                                    (liveElapsedMs.toFloat() / ym.durationMs.toFloat()).coerceIn(0f, 1f)
                                                } else 0f
                                                Box(
                                                    modifier = Modifier
                                                        .fillMaxWidth(frac)
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
                                            IconButton(onClick = { if (interactive) dispatchYMusicControl("like") }, modifier = Modifier.size(32.dp)) {
                                                Icon(if (liked) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder, contentDescription = null, tint = if (liked) Color(0xFFFF2E7A) else Color.White, modifier = Modifier.size(20.dp))
                                            }
                                            IconButton(
                                                onClick = {
                                                    if (interactive) {
                                                        dispatchYMusicControl(if (playing) "pause" else "play")
                                                    }
                                                },
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

                            SwipeableTwoCardRibbon(
                                trackKey = ym.trackId,
                                onSwipeNext = { dispatchYMusicControl("next") },
                                onSwipePrev = { dispatchYMusicControl("prev") },
                                canSwipeNext = !isTrackTransitioning,
                                canSwipePrev = true,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 6.dp),
                                currentCard = {
                                    MiniPlayerCard(ym.title, ym.artist, ym.coverUrl, ym.liked, ym.state == "playing", interactive = true)
                                },
                                nextCard = {
                                    MiniPlayerCard(
                                        ym.nextTitle.ifBlank { "Следующий трек" },
                                        ym.nextArtist,
                                        ym.nextCoverUrl,
                                        false,
                                        ym.state == "playing",
                                        interactive = false
                                    )
                                },
                                prevCard = {
                                    MiniPlayerCard(
                                        ym.prevTitle.ifBlank { "Предыдущий трек" },
                                        ym.prevArtist,
                                        ym.prevCoverUrl,
                                        false,
                                        ym.state == "playing",
                                        interactive = false
                                    )
                                }
                            )
                        }

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
                                                    AppTab.WAVE -> { client.ymusicStatus(); if (ymOrbs.isEmpty()) client.ymusicOrbs() }
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
                    when (currentTab) {
                        AppTab.WAVE -> {
                            YandexWaveFullScreen(
                                ymusic = status.ymusic,
                                palette = palette,
                                orbs = ymOrbs,
                                searchResults = ymSearchResults,
                                albumResults = ymAlbumResults,
                                onRefresh = { client.ymusicStatus(); client.ymusicOrbs() },
                                onLogin = { tok -> client.ymusicLogin(tok) },
                                onPlay = { mode, q ->
                                    client.ymusicPlay(mode, q)
                                    scope.launch { delay(900); client.ymusicStatus() }
                                },
                                onSearch = { q -> client.ymusicSearch(q) },
                                onControl = { action -> dispatchYMusicControl(action) },
                                onOpenAccountSettings = {
                                    selectedTab = AppTab.SETTINGS
                                    settingsPage = SettingsPage.ACCOUNT
                                },
                                onOpenBigPlayer = { showBigPlayer = true },
                                onOpenVolumeSheet = { showVolumeSheet = true },
                                onOpenEqualizer = { showEqSheet = true },
                                volume = sliderVol,
                                mute = status.mute,
                                canSwipeNext = !isTrackTransitioning,
                                canSwipePrev = true
                            )
                        }

                        AppTab.TIMERS -> {
                            LazyColumn(
                                modifier = Modifier.fillMaxSize().statusBarsPadding().padding(horizontal = 16.dp),
                                verticalArrangement = Arrangement.spacedBy(14.dp),
                                contentPadding = PaddingValues(vertical = 14.dp)
                            ) {
                                item {
                                    Text("Таймеры и будильники", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 24.sp)
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
                                        onCancelAlarm = { id -> client.cancelAlarms(id) }
                                    )
                                }
                            }
                        }

                        AppTab.SETTINGS -> {
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
                                verticalArrangement = Arrangement.spacedBy(12.dp),
                                contentPadding = PaddingValues(vertical = 14.dp)
                            ) {
                                if (curPage == SettingsPage.ROOT) {
                                    item {
                                        Row(
                                            modifier = Modifier.fillMaxWidth(),
                                            horizontalArrangement = Arrangement.SpaceBetween,
                                            verticalAlignment = Alignment.CenterVertically
                                        ) {
                                            Column {
                                                Text("Настройки", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 26.sp)
                                                Text("${status.model} · $connText", color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 12.sp)
                                            }
                                            IconButton(
                                                onClick = { client.disconnect(); autoConnectState = "manual" },
                                                modifier = Modifier.size(38.dp).clip(CircleShape).background(OledCard)
                                            ) {
                                                Icon(Icons.Rounded.PowerSettingsNew, contentDescription = "Отключить", tint = Color.White, modifier = Modifier.size(19.dp))
                                            }
                                        }
                                    }
                                    val menuPages = listOf(
                                        SettingsPage.ACCOUNT,
                                        SettingsPage.WIFI,
                                        SettingsPage.BLUETOOTH,
                                        SettingsPage.PALETTE,
                                        SettingsPage.TIME,
                                        SettingsPage.ABOUT,
                                        SettingsPage.DEBUG
                                    )
                                    items(menuPages.size) { idx ->
                                        val page = menuPages[idx]
                                        Surface(
                                            shape = RoundedCornerShape(TypeUI.RadiusMd),
                                            color = TypeUI.SurfaceElevated,
                                            modifier = Modifier
                                                .fillMaxWidth()
                                                .border(1.dp, TypeUI.Hairline, RoundedCornerShape(TypeUI.RadiusMd))
                                                .clickable {
                                                    settingsPage = page
                                                    if (page == SettingsPage.ABOUT) client.otaStatus()
                                                }
                                        ) {
                                            Row(
                                                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 15.dp),
                                                verticalAlignment = Alignment.CenterVertically,
                                                horizontalArrangement = Arrangement.spacedBy(14.dp)
                                            ) {
                                                Box(
                                                    modifier = Modifier.size(40.dp).clip(CircleShape).background(palette.accent.copy(alpha = 0.14f)),
                                                    contentAlignment = Alignment.Center
                                                ) {
                                                    Icon(page.icon, contentDescription = null, tint = palette.accent, modifier = Modifier.size(22.dp))
                                                }
                                                Column(modifier = Modifier.weight(1f)) {
                                                    Text(page.title, color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
                                                    Text(page.subtitle, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                                }
                                                Icon(Icons.Rounded.ChevronRight, contentDescription = null, tint = Color(0xFF6B6B75))
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
                                                YandexAccountCard(
                                                    ymusic = status.ymusic,
                                                    onLogin = { tok -> client.ymusicLogin(tok) },
                                                    onLogout = { client.ymusicLogout() },
                                                    onRefresh = { client.ymusicStatus(); client.ymusicOrbs() }
                                                )
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
                                                            val sel = p == palette
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
                                                AiAssistantCard(
                                                    ai = status.ai,
                                                    onApplyAi = { enabled, mode, server, wake, voice, emotion ->
                                                        client.setAi(enabled = enabled, mode = mode, server = server, wakeWord = wake, ttsVoice = voice, ttsEmotion = emotion)
                                                    }
                                                )
                                            }
                                            item {
                                                LedAndScreenCard(
                                                    capabilities = status.capabilities,
                                                    currentScreenMode = status.screenMode,
                                                    currentRingMode = status.ringMode,
                                                    onSetScreen = { mode, st, txt -> client.setScreen(mode, st, txt) },
                                                    onSetRing = { mode -> status = status.copy(ringMode = mode); client.setRing(mode) }
                                                )
                                            }
                                            item {
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
                                            item {
                                                // Live VQE / STFT-AEC & Barge-In Telemetry Card (ERLE, SNR, Echo Lag, Active Mic)
                                                Surface(
                                                    shape = RoundedCornerShape(TypeUI.RadiusLg),
                                                    color = TypeUI.SurfaceElevated,
                                                    modifier = Modifier.border(1.dp, TypeUI.Hairline, RoundedCornerShape(TypeUI.RadiusLg))
                                                ) {
                                                    Column(
                                                        modifier = Modifier.fillMaxWidth().padding(18.dp),
                                                        verticalArrangement = Arrangement.spacedBy(8.dp)
                                                    ) {
                                                        Row(
                                                            modifier = Modifier.fillMaxWidth(),
                                                            horizontalArrangement = Arrangement.SpaceBetween,
                                                            verticalAlignment = Alignment.CenterVertically
                                                        ) {
                                                            Text("VQE · STFT-AEC & Barge-In", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                                                            Text(
                                                                if (status.vqe.refActive) "Loopback OK" else "Ожидание",
                                                                color = if (status.vqe.refActive) palette.accent else Color(0xFF7A7A86),
                                                                fontSize = 12.sp,
                                                                fontWeight = FontWeight.SemiBold
                                                            )
                                                        }
                                                        InfoRow("Подавление эха (ERLE)", "%.1f дБ".format(status.vqe.erleDb))
                                                        InfoRow("Речевой SNR (300–3400 Гц)", "%.1f дБ".format(status.vqe.snrDb))
                                                        InfoRow("Задержка тракта", "${status.vqe.lagMs} мс")
                                                        InfoRow("Активный микрофон", "MIC #${status.vqe.bestMic} (из 7)")
                                                        InfoRow("Перебивание (Barge-In)", if (status.vqe.bargeIn) "Активно (речь)" else "Готово")
                                                    }
                                                }
                                            }
                                            item { TelemetryCard(status = status) }
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
                            onVolumeCommit = { dispatchVolumeLive(it.toFloat(), forceCommit = true) },
                            onToggleMute = { client.setMute(!status.mute) },
                            onAudioTest = { client.audioTest() },
                            showAudioTest = false
                        )
                    }
                }
            }

            // 5-Band Live Equalizer Bottom Sheet!
            if (showEqSheet) {
                ModalBottomSheet(
                    onDismissRequest = { showEqSheet = false },
                    containerColor = Color(0xFF141418)
                ) {
                    EqualizerPanel(
                        eq = status.eq,
                        palette = palette,
                        onApplyEq = { en, pr, b ->
                            status = status.copy(eq = status.eq.copy(enabled = en, preset = pr, bands = b))
                            client.setEq(en, pr, b)
                        }
                    )
                }
            }

            // Full-screen Big Player overlay!
            AnimatedVisibility(
                visible = showBigPlayer,
                enter = slideInVertically(initialOffsetY = { it }) + fadeIn(),
                exit = slideOutVertically(targetOffsetY = { it }) + fadeOut()
            ) {
                BigPlayerOverlay(
                    ymusic = status.ymusic,
                    palette = palette,
                    volume = sliderVol,
                    mute = status.mute,
                    searchResults = ymSearchResults,
                    albumResults = ymAlbumResults,
                    onSearch = { q -> client.ymusicSearch(q) },
                    onPlay = { mode, q ->
                        client.ymusicPlay(mode, q)
                        scope.launch { delay(900); client.ymusicStatus() }
                    },
                    onVolumeChange = { dispatchVolumeLive(it, forceCommit = false) },
                    onVolumeCommit = { dispatchVolumeLive(it.toFloat(), forceCommit = true) },
                    onControl = { action -> dispatchYMusicControl(action) },
                    onPlayWave = { client.ymusicPlay("wave", null) },
                    onDismiss = { showBigPlayer = false },
                    onOpenEqualizer = { showEqSheet = true },
                    canSwipeNext = !isTrackTransitioning,
                    canSwipePrev = true
                )
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
