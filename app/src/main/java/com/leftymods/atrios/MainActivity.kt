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
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
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
import com.leftymods.atrios.model.YMusicTrack
import com.leftymods.atrios.ui.AiAssistantCard
import com.leftymods.atrios.ui.AsyncCoverArt
import com.leftymods.atrios.ui.AudioCard
import com.leftymods.atrios.ui.BluetoothCard
import com.leftymods.atrios.ui.DevicePickerSection
import com.leftymods.atrios.ui.LedAndScreenCard
import com.leftymods.atrios.ui.OtaUpdateCard
import com.leftymods.atrios.ui.TelemetryCard
import com.leftymods.atrios.ui.TimeAndTimezoneCard
import com.leftymods.atrios.ui.TimersAndAlarmsCard
import com.leftymods.atrios.ui.WifiCard
import com.leftymods.atrios.ui.YMusicCard
import com.leftymods.atrios.ui.theme.AtriOSTheme
import com.leftymods.atrios.ui.theme.OledBlack
import com.leftymods.atrios.ui.theme.OledCard
import com.leftymods.atrios.ui.theme.OledCardHigh
import com.leftymods.atrios.ui.theme.YandexYellow
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        applyFullScreenImmersive()
        setContent {
            AtriOSTheme {
                AtriOSApp()
            }
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
        // Auto-hide gesture swipe navigation bar so the app fills 100% of the screen
        ctrl.hide(WindowInsetsCompat.Type.navigationBars())
    }
}

private enum class AppTab(val label: String, val icon: ImageVector) {
    MUSIC("Музыка", Icons.Rounded.AutoAwesome),
    SPEAKER("Колонка", Icons.Rounded.Speaker),
    SETTINGS("Настройки", Icons.Rounded.Settings)
}

@OptIn(ExperimentalMaterial3Api::class)
@SuppressLint("MissingPermission")
@Composable
fun AtriOSApp() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    val storage = remember { SpeakerStorage(context) }
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
    var connText by remember { mutableStateOf("Выберите колонку") }
    var status by remember { mutableStateOf(SpeakerStatus()) }
    var sliderVol by remember { mutableFloatStateOf(60f) }
    var timers by remember { mutableStateOf<List<TimerItem>>(emptyList()) }
    var alarms by remember { mutableStateOf<List<AlarmItem>>(emptyList()) }
    var ymSearchResults by remember { mutableStateOf<List<YMusicTrack>>(emptyList()) }
    var ymAlbumResults by remember { mutableStateOf<List<YMusicAlbum>>(emptyList()) }
    var savedSpeakers by remember { mutableStateOf(storage.loadSpeakers()) }
    var relayConfig by remember { mutableStateOf(storage.loadRelayConfig()) }
    var selectedTab by remember { mutableStateOf(AppTab.MUSIC) }
    var showRebootDialog by remember { mutableStateOf(false) }
    var showDebugMenu by remember { mutableStateOf(false) }
    var availableAppUpdate by remember { mutableStateOf<AppReleaseInfo?>(null) }
    var appUpdateProgress by remember { mutableStateOf<Float?>(null) }

    LaunchedEffect(Unit) {
        availableAppUpdate = AppUpdater.checkForUpdate(context)
    }

    val client = remember {
        BtControlClient(
            scope = scope,
            onStatus = { s ->
                status = s
                sliderVol = s.volume.toFloat()
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
                sliderVol = v.toFloat()
                status = status.copy(volume = v, mute = m)
            },
            onButtonsPush = { ap, mm ->
                status = status.copy(actionPressed = ap, micMuted = mm)
            },
            onOtaUpdate = { ota -> status = status.copy(ota = ota) },
            onYMusicSearchResults = { tracks, albums ->
                ymSearchResults = tracks
                ymAlbumResults = albums
            },
            onYMusicUpdate = { ym -> status = status.copy(ymusic = ym) },
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
                if (!ok) {
                    timers = emptyList()
                    alarms = emptyList()
                    ymSearchResults = emptyList()
                    ymAlbumResults = emptyList()
                }
            }
        ).also { c ->
            // Seed saved internet-pair credentials immediately for seamless BT -> Wi-Fi failover
            c.setSavedRelayConfig(relayConfig.gatewayUrl, relayConfig.deviceId, relayConfig.token)
        }
    }

    LaunchedEffect(connected) {
        if (connected) {
            client.ymusicStatus()
            client.getTimers()
            client.getAlarms()
        }
    }

    // Periodic Now-Playing sync while music is active
    LaunchedEffect(connected, status.ymusic.isActive) {
        while (connected && status.ymusic.isActive) {
            delay(4000)
            client.ymusicStatus()
        }
    }

    DisposableEffect(Unit) { onDispose { client.disconnect(silent = true) } }

    val bondedDevices: List<BluetoothDevice> = remember(hasPerms, adapter, connected) {
        if (hasPerms && adapter != null) adapter.bondedDevices.toList() else emptyList()
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(OledBlack)
    ) {
        Scaffold(
            containerColor = OledBlack,
            contentWindowInsets = WindowInsets(0, 0, 0, 0),
            snackbarHost = {
                SnackbarHost(snackbarHostState) { data ->
                    Snackbar(
                        snackbarData = data,
                        containerColor = Color(0xFF2B121A),
                        contentColor = Color(0xFFFFD6DC),
                        shape = RoundedCornerShape(14.dp)
                    )
                }
            },
            topBar = {
                // On Music tab when connected, YMusicCard renders its own clean Yandex Music header
                if (!connected || selectedTab != AppTab.MUSIC) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(OledBlack)
                            .statusBarsPadding()
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                            modifier = Modifier.weight(1f)
                        ) {
                            Box(
                                modifier = Modifier
                                    .size(9.dp)
                                    .clip(CircleShape)
                                    .background(if (connected) YandexYellow else Color(0xFF555560))
                            )
                            Column {
                                Text(
                                    if (connected) status.model else "AtriOS Control",
                                    color = Color.White,
                                    fontWeight = FontWeight.Bold,
                                    fontSize = 18.sp,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                Text(
                                    connText,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    fontSize = 11.sp,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        }
                        if (connected) {
                            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                                IconButton(
                                    onClick = { client.requestStatus(); client.ymusicStatus() },
                                    modifier = Modifier.size(36.dp).clip(CircleShape).background(OledCard)
                                ) {
                                    Icon(Icons.Rounded.Refresh, contentDescription = "Обновить", tint = Color.White, modifier = Modifier.size(18.dp))
                                }
                                IconButton(
                                    onClick = { client.disconnect() },
                                    modifier = Modifier.size(36.dp).clip(CircleShape).background(OledCard)
                                ) {
                                    Icon(Icons.Rounded.PowerSettingsNew, contentDescription = "Отключить", tint = Color.White, modifier = Modifier.size(18.dp))
                                }
                            }
                        }
                    }
                }
            },
            bottomBar = {
                if (connected) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(OledBlack)
                    ) {
                        // Yandex-Music-style dark floating mini-player on non-Music tabs
                        val ym = status.ymusic
                        if (selectedTab != AppTab.MUSIC && (ym.isActive || ym.title.isNotBlank())) {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 12.dp, vertical = 6.dp)
                                    .clickable {
                                        selectedTab = AppTab.MUSIC
                                        client.ymusicStatus()
                                    },
                                shape = RoundedCornerShape(18.dp),
                                color = Color(0xFF1A1A1E)
                            ) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 10.dp, vertical = 8.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                                ) {
                                    AsyncCoverArt(
                                        url = ym.coverUrl,
                                        shape = RoundedCornerShape(10.dp),
                                        modifier = Modifier.size(42.dp),
                                        fallbackIcon = Icons.Rounded.MusicNote
                                    )
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text(
                                            ym.title.ifBlank { "Моя волна" },
                                            color = Color.White,
                                            fontWeight = FontWeight.SemiBold,
                                            fontSize = 14.sp,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                        if (ym.artist.isNotBlank()) {
                                            Text(
                                                ym.artist,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                                fontSize = 12.sp,
                                                maxLines = 1,
                                                overflow = TextOverflow.Ellipsis
                                            )
                                        }
                                    }
                                    IconButton(
                                        onClick = { client.ymusicControl("like") },
                                        modifier = Modifier.size(34.dp)
                                    ) {
                                        Icon(
                                            if (ym.liked) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                                            contentDescription = "Нравится",
                                            tint = if (ym.liked) Color(0xFFFF3366) else Color.White,
                                            modifier = Modifier.size(20.dp)
                                        )
                                    }
                                    IconButton(
                                        onClick = {
                                            val nextState = if (ym.state == "playing") "paused" else "playing"
                                            status = status.copy(ymusic = ym.copy(state = nextState))
                                            client.ymusicControl(if (ym.state == "playing") "pause" else "play")
                                        },
                                        modifier = Modifier.size(36.dp)
                                    ) {
                                        Icon(
                                            if (ym.state == "playing") Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                                            contentDescription = "Пауза / Плей",
                                            tint = Color.White,
                                            modifier = Modifier.size(26.dp)
                                        )
                                    }
                                    IconButton(
                                        onClick = { client.ymusicControl("next") },
                                        modifier = Modifier.size(34.dp)
                                    ) {
                                        Icon(Icons.Rounded.SkipNext, contentDescription = "Следующий", tint = Color.White, modifier = Modifier.size(22.dp))
                                    }
                                }
                            }
                        }

                        // Minimalist Yandex Music style bottom icon bar
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(OledBlack)
                                .padding(horizontal = 20.dp, vertical = 10.dp),
                            horizontalArrangement = Arrangement.SpaceEvenly,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            AppTab.entries.forEach { tab ->
                                val sel = selectedTab == tab
                                Column(
                                    horizontalAlignment = Alignment.CenterHorizontally,
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(14.dp))
                                        .clickable {
                                            selectedTab = tab
                                            when (tab) {
                                                AppTab.MUSIC -> client.ymusicStatus()
                                                AppTab.SPEAKER -> {
                                                    client.getTimers()
                                                    client.getAlarms()
                                                }
                                                AppTab.SETTINGS -> client.otaStatus()
                                            }
                                        }
                                        .padding(horizontal = 18.dp, vertical = 6.dp)
                                ) {
                                    Icon(
                                        tab.icon,
                                        contentDescription = tab.label,
                                        tint = if (sel) YandexYellow else Color(0xFF6E6E78),
                                        modifier = Modifier.size(26.dp)
                                    )
                                    Spacer(Modifier.height(2.dp))
                                    Text(
                                        tab.label,
                                        color = if (sel) YandexYellow else Color(0xFF6E6E78),
                                        fontSize = 11.sp,
                                        fontWeight = if (sel) FontWeight.SemiBold else FontWeight.Normal
                                    )
                                }
                            }
                        }
                    }
                }
            }
        ) { pad ->
            LazyColumn(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(pad)
                    .then(if (connected && selectedTab == AppTab.MUSIC) Modifier.statusBarsPadding() else Modifier)
                    .padding(horizontal = 14.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp),
                contentPadding = PaddingValues(top = 6.dp, bottom = 18.dp)
            ) {
                if (!hasPerms) {
                    item {
                        Surface(shape = RoundedCornerShape(24.dp), color = OledCard) {
                            Column(
                                Modifier.padding(20.dp),
                                verticalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                Text("Разрешение Bluetooth", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
                                Text("Для первичной связки с колонкой AtriOS требуется разрешение Bluetooth.")
                                Button(
                                    onClick = {
                                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                                            permLauncher.launch(
                                                arrayOf(
                                                    Manifest.permission.BLUETOOTH_CONNECT,
                                                    Manifest.permission.BLUETOOTH_SCAN
                                                )
                                            )
                                        }
                                    },
                                    shape = RoundedCornerShape(99.dp)
                                ) { Text("Разрешить", fontWeight = FontWeight.Bold) }
                            }
                        }
                    }
                }

                if (!connected && hasPerms) {
                    // Quick 1-tap connect over saved Internet pair if already paired once via BT!
                    if (relayConfig.gatewayUrl.startsWith("https://") && relayConfig.deviceId.isNotBlank() && relayConfig.token.isNotBlank()) {
                        item {
                            Surface(shape = RoundedCornerShape(24.dp), color = OledCard) {
                                Column(
                                    modifier = Modifier.fillMaxWidth().padding(18.dp),
                                    verticalArrangement = Arrangement.spacedBy(10.dp)
                                ) {
                                    Row(
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                                    ) {
                                        Icon(Icons.Rounded.CloudDone, contentDescription = null, tint = YandexYellow)
                                        Column(modifier = Modifier.weight(1f)) {
                                            Text("Интернет-пара готова", fontWeight = FontWeight.Bold, color = Color.White)
                                            Text(
                                                "${relayConfig.deviceId} · ${relayConfig.gatewayUrl.removePrefix("https://")}",
                                                style = MaterialTheme.typography.labelSmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                    }
                                    Button(
                                        onClick = {
                                            client.connectRelay(relayConfig.gatewayUrl, relayConfig.deviceId, relayConfig.token)
                                        },
                                        modifier = Modifier.fillMaxWidth().height(48.dp),
                                        shape = RoundedCornerShape(99.dp),
                                        colors = ButtonDefaults.buttonColors(containerColor = YandexYellow, contentColor = Color.Black)
                                    ) {
                                        Icon(Icons.Rounded.Wifi, contentDescription = null)
                                        Spacer(Modifier.width(8.dp))
                                        Text("Подключиться через интернет", fontWeight = FontWeight.Bold)
                                    }
                                }
                            }
                        }
                    }

                    item {
                        DevicePickerSection(
                            savedSpeakers = savedSpeakers,
                            bondedDevices = bondedDevices,
                            onConnect = { dev -> client.connect(dev) },
                            onForgetSaved = { mac -> savedSpeakers = storage.removeSpeaker(mac) }
                        )
                    }
                }

                if (connected) {
                    when (selectedTab) {
                        AppTab.MUSIC -> {
                            item {
                                YMusicCard(
                                    ymusic = status.ymusic,
                                    searchResults = ymSearchResults,
                                    albumResults = ymAlbumResults,
                                    onRefresh = { client.ymusicStatus() },
                                    onLogin = { token -> client.ymusicLogin(token) },
                                    onLogout = { client.ymusicLogout() },
                                    onPlay = { mode, query ->
                                        client.ymusicPlay(mode, query)
                                        scope.launch {
                                            delay(1100)
                                            client.ymusicStatus()
                                        }
                                    },
                                    onSearch = { q -> client.ymusicSearch(q) },
                                    onControl = { action ->
                                        if (action == "pause" || action == "play") {
                                            status = status.copy(
                                                ymusic = status.ymusic.copy(
                                                    state = if (action == "pause") "paused" else "playing"
                                                )
                                            )
                                        }
                                        client.ymusicControl(action)
                                    }
                                )
                            }
                        }

                        AppTab.SPEAKER -> {
                            item {
                                AudioCard(
                                    volume = sliderVol,
                                    mute = status.mute,
                                    speakersDesc = status.speakersDesc,
                                    onVolumeChange = { sliderVol = it },
                                    onVolumeCommit = { client.setVolume(it) },
                                    onToggleMute = { client.setMute(!status.mute) },
                                    onAudioTest = { client.audioTest() }
                                )
                            }
                            item {
                                TimersAndAlarmsCard(
                                    timers = timers,
                                    alarms = alarms,
                                    onRefresh = {
                                        client.getTimers()
                                        client.getAlarms()
                                    },
                                    onAddTimer = { secs -> client.setTimer(secs) },
                                    onCancelTimer = { id -> client.cancelTimers(id) },
                                    onAddAlarm = { h, m -> client.setAlarm(h, m) },
                                    onCancelAlarm = { id -> client.cancelAlarms(id) }
                                )
                            }
                        }

                        AppTab.SETTINGS -> {
                            // Auto-paired Internet link status badge
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
                                            tint = if (relayConfig.token.isNotBlank()) YandexYellow else Color(0xFF6E6E78)
                                        )
                                        Column(modifier = Modifier.weight(1f)) {
                                            Text(
                                                if (relayConfig.token.isNotBlank()) "Интернет-пара активна (автопереход)" else "Интернет-пара не создана",
                                                fontWeight = FontWeight.SemiBold,
                                                color = Color.White,
                                                fontSize = 14.sp
                                            )
                                            Text(
                                                if (relayConfig.token.isNotBlank())
                                                    "ID: ${relayConfig.deviceId} · при отключении BT перейдёт на Wi-Fi автоматически"
                                                else "Подключитесь один раз по Bluetooth для автоматической привязки",
                                                style = MaterialTheme.typography.labelSmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                    }
                                }
                            }
                            item {
                                WifiCard(
                                    wifiConnected = status.wifiConnected,
                                    wifiSsid = status.wifiSsid,
                                    wifiIp = status.wifiIp,
                                    onConnectWifi = { ssid, psk -> client.wifiConnect(ssid, psk) }
                                )
                            }
                            item {
                                BluetoothCard(
                                    status = status,
                                    onToggleSpeakerMode = { enabled -> client.setBtSpeakerMode(enabled) },
                                    onPair = { secs -> client.btPair(secs) },
                                    onHide = { client.btHide() },
                                    onUnpair = { mac -> client.btUnpair(mac) }
                                )
                            }
                            item {
                                TimeAndTimezoneCard(
                                    localTime = status.localTime,
                                    timezone = status.timezone,
                                    onSyncNtp = { client.ntpSync() },
                                    onSetTimezone = { tz -> client.setTimezone(tz) }
                                )
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
                                    Column(
                                        modifier = Modifier.fillMaxWidth().padding(18.dp),
                                        verticalArrangement = Arrangement.spacedBy(10.dp)
                                    ) {
                                        Text("Об устройстве и приложении", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                                        Text("${status.model} · прошивка v${status.fwVersion}", style = MaterialTheme.typography.bodyMedium)
                                        val rel = availableAppUpdate
                                        if (rel != null) {
                                            val prog = appUpdateProgress
                                            if (prog != null) {
                                                LinearProgressIndicator(progress = { prog }, modifier = Modifier.fillMaxWidth(), color = YandexYellow)
                                                Text("Загрузка APK: ${(prog * 100).toInt()}%", style = MaterialTheme.typography.bodySmall)
                                            } else {
                                                Button(
                                                    onClick = {
                                                        scope.launch {
                                                            appUpdateProgress = 0f
                                                            val res = AppUpdater.downloadAndInstall(context, rel) { p -> appUpdateProgress = p }
                                                            appUpdateProgress = null
                                                            res.onFailure { e ->
                                                                snackbarHostState.showSnackbar("Ошибка загрузки APK: ${e.message}")
                                                            }
                                                        }
                                                    },
                                                    shape = RoundedCornerShape(99.dp)
                                                ) {
                                                    Icon(Icons.Rounded.Download, contentDescription = null)
                                                    Spacer(Modifier.width(6.dp))
                                                    Text("Обновить приложение (v${rel.version})", fontWeight = FontWeight.Bold)
                                                }
                                            }
                                        } else {
                                            FilledTonalButton(
                                                onClick = { scope.launch { availableAppUpdate = AppUpdater.checkForUpdate(context) } },
                                                shape = RoundedCornerShape(99.dp)
                                            ) {
                                                Icon(Icons.Rounded.Refresh, contentDescription = null)
                                                Spacer(Modifier.width(6.dp))
                                                Text("Проверить обновления приложения")
                                            }
                                        }
                                    }
                                }
                            }

                            // Collapsible Debug / Engineering Menu: AI server, Qwen3-TTS, Screen animations, RGB ring, Telemetry
                            item {
                                Surface(
                                    shape = RoundedCornerShape(22.dp),
                                    color = OledCardHigh,
                                    modifier = Modifier.clickable { showDebugMenu = !showDebugMenu }
                                ) {
                                    Row(
                                        modifier = Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 16.dp),
                                        horizontalArrangement = Arrangement.SpaceBetween,
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        Row(
                                            verticalAlignment = Alignment.CenterVertically,
                                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                                        ) {
                                            Icon(Icons.Rounded.BugReport, contentDescription = null, tint = YandexYellow)
                                            Column {
                                                Text("Debug-меню", fontWeight = FontWeight.Bold, color = Color.White)
                                                Text(
                                                    "ИИ-сервер, Qwen3-TTS, анимации экрана, RGB-кольцо, телеметрия",
                                                    style = MaterialTheme.typography.labelSmall,
                                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                                )
                                            }
                                        }
                                        Icon(
                                            if (showDebugMenu) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                                            contentDescription = null,
                                            tint = Color.White
                                        )
                                    }
                                }
                            }
                            if (showDebugMenu) {
                                item {
                                    AiAssistantCard(
                                        ai = status.ai,
                                        onApplyAi = { enabled, mode, server, wake, voice, emotion ->
                                            client.setAi(
                                                enabled = enabled,
                                                mode = mode,
                                                server = server,
                                                wakeWord = wake,
                                                ttsVoice = voice,
                                                ttsEmotion = emotion
                                            )
                                        }
                                    )
                                }
                                item {
                                    LedAndScreenCard(
                                        capabilities = status.capabilities,
                                        currentScreenMode = status.screenMode,
                                        currentRingMode = status.ringMode,
                                        onSetScreen = { mode, st, txt -> client.setScreen(mode, st, txt) },
                                        onSetRing = { mode ->
                                            status = status.copy(ringMode = mode)
                                            client.setRing(mode)
                                        }
                                    )
                                }
                                item { TelemetryCard(status = status) }
                            }
                        }
                    }
                }
            }
        }

        if (showRebootDialog) {
            AlertDialog(
                onDismissRequest = { showRebootDialog = false },
                title = { Text("Перезагрузить колонку?") },
                text = { Text("Соединение будет временно прервано на время перезагрузки.") },
                confirmButton = {
                    Button(onClick = {
                        showRebootDialog = false
                        client.reboot()
                    }) { Text("Перезагрузить") }
                },
                dismissButton = {
                    TextButton(onClick = { showRebootDialog = false }) { Text("Отмена") }
                }
            )
        }
    }
}
