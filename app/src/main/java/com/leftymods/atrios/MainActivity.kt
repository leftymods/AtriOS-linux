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
import androidx.compose.animation.AnimatedVisibility
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
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
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
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            AtriOSTheme {
                AtriOSApp()
            }
        }
    }
}

private enum class AppTab(val label: String, val icon: ImageVector) {
    HOME("Главная", Icons.Rounded.Home),
    MUSIC("Музыка", Icons.Rounded.MusicNote),
    ASSISTANT("ИИ", Icons.Rounded.AutoAwesome),
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
    var connText by remember { mutableStateOf("Выберите колонку для подключения") }
    var status by remember { mutableStateOf(SpeakerStatus()) }
    var sliderVol by remember { mutableFloatStateOf(60f) }
    var timers by remember { mutableStateOf<List<TimerItem>>(emptyList()) }
    var alarms by remember { mutableStateOf<List<AlarmItem>>(emptyList()) }
    var ymSearchResults by remember { mutableStateOf<List<YMusicTrack>>(emptyList()) }
    var ymAlbumResults by remember { mutableStateOf<List<YMusicAlbum>>(emptyList()) }
    var savedSpeakers by remember { mutableStateOf(storage.loadSpeakers()) }
    var relayConfig by remember { mutableStateOf(storage.loadRelayConfig()) }
    var selectedTab by remember { mutableStateOf(AppTab.HOME) }
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
            onOtaUpdate = { ota ->
                status = status.copy(ota = ota)
            },
            onYMusicSearchResults = { tracks, albums ->
                ymSearchResults = tracks
                ymAlbumResults = albums
            },
            onYMusicUpdate = { ym ->
                status = status.copy(ymusic = ym)
            },
            onTimersUpdate = { list -> timers = list },
            onAlarmsUpdate = { list -> alarms = list },
            onRelayInfo = { did, gw ->
                val updated = relayConfig.copy(
                    deviceId = did,
                    gatewayUrl = gw.ifBlank { relayConfig.gatewayUrl }
                )
                relayConfig = updated
                storage.saveRelayConfig(updated)
            },
            onBtSpeakerPush = { enabled ->
                status = status.copy(btSpeakerMode = enabled)
            },
            onMessage = { msg, isError ->
                // Silence routine action confirmations; only surface actual errors
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
                    client.ymusicStatus()
                    client.getTimers()
                    client.getAlarms()
                } else {
                    timers = emptyList()
                    alarms = emptyList()
                    ymSearchResults = emptyList()
                    ymAlbumResults = emptyList()
                }
            }
        )
    }

    // Keep Now-Playing and mini-player synchronized while music is active
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
            .background(
                Brush.verticalGradient(
                    listOf(Color(0xFF090E1C), Color(0xFF070B14), Color(0xFF0B1122))
                )
            )
    ) {
        Scaffold(
            containerColor = Color.Transparent,
            snackbarHost = {
                SnackbarHost(snackbarHostState) { data ->
                    Snackbar(
                        snackbarData = data,
                        containerColor = Color(0xFF2B1420),
                        contentColor = Color(0xFFFFD6E0),
                        shape = RoundedCornerShape(14.dp)
                    )
                }
            },
            topBar = {
                TopAppBar(
                    colors = TopAppBarDefaults.topAppBarColors(
                        containerColor = Color(0xFF090E1B).copy(alpha = 0.95f)
                    ),
                    title = {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Box(
                                modifier = Modifier
                                    .size(10.dp)
                                    .clip(CircleShape)
                                    .background(if (connected) Color(0xFF34D399) else Color(0xFF64748B))
                            )
                            Column {
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                                ) {
                                    Text(
                                        if (connected) status.model else "AtriOS Control",
                                        fontWeight = FontWeight.Bold,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                    if (connected && status.fwVersion.isNotBlank()) {
                                        Surface(
                                            shape = RoundedCornerShape(99.dp),
                                            color = MaterialTheme.colorScheme.primaryContainer
                                        ) {
                                            Text(
                                                "v${status.fwVersion}",
                                                modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                                                style = MaterialTheme.typography.labelSmall,
                                                color = MaterialTheme.colorScheme.primary,
                                                fontWeight = FontWeight.SemiBold
                                            )
                                        }
                                    }
                                }
                                Text(
                                    connText,
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        }
                    },
                    actions = {
                        if (connected) {
                            FilledTonalIconButton(
                                onClick = { client.requestStatus(); client.ymusicStatus() },
                                modifier = Modifier.size(36.dp)
                            ) {
                                Icon(Icons.Rounded.Refresh, contentDescription = "Обновить", modifier = Modifier.size(18.dp))
                            }
                            Spacer(Modifier.width(6.dp))
                            FilledTonalIconButton(
                                onClick = { client.disconnect() },
                                modifier = Modifier.size(36.dp)
                            ) {
                                Icon(Icons.Rounded.BluetoothDisabled, contentDescription = "Сменить колонку", modifier = Modifier.size(18.dp))
                            }
                            Spacer(Modifier.width(8.dp))
                        }
                    }
                )
            },
            bottomBar = {
                if (connected) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(Color(0xFF0A101E))
                    ) {
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
                                color = Color.Transparent
                            ) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .background(
                                            Brush.horizontalGradient(
                                                listOf(Color(0xFF172642), Color(0xFF251A45))
                                            ),
                                            RoundedCornerShape(18.dp)
                                        )
                                        .padding(horizontal = 10.dp, vertical = 8.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                                ) {
                                    AsyncCoverArt(
                                        url = ym.coverUrl,
                                        shape = RoundedCornerShape(10.dp),
                                        modifier = Modifier.size(40.dp),
                                        fallbackIcon = Icons.Rounded.MusicNote
                                    )
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text(
                                            ym.title.ifBlank { "Яндекс Музыка" },
                                            style = MaterialTheme.typography.labelLarge,
                                            fontWeight = FontWeight.SemiBold,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                        if (ym.artist.isNotBlank()) {
                                            Text(
                                                ym.artist,
                                                style = MaterialTheme.typography.labelSmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                                maxLines = 1,
                                                overflow = TextOverflow.Ellipsis
                                            )
                                        }
                                    }
                                    IconButton(
                                        onClick = { client.ymusicControl("prev") },
                                        modifier = Modifier.size(34.dp)
                                    ) {
                                        Icon(Icons.Rounded.SkipPrevious, contentDescription = "Предыдущий", modifier = Modifier.size(20.dp))
                                    }
                                    FilledIconButton(
                                        onClick = {
                                            val nextState = if (ym.state == "playing") "paused" else "playing"
                                            status = status.copy(ymusic = ym.copy(state = nextState))
                                            client.ymusicControl(if (ym.state == "playing") "pause" else "play")
                                        },
                                        modifier = Modifier.size(36.dp),
                                        shape = CircleShape
                                    ) {
                                        Icon(
                                            if (ym.state == "playing") Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                                            contentDescription = "Пауза / Плей",
                                            modifier = Modifier.size(20.dp)
                                        )
                                    }
                                    IconButton(
                                        onClick = { client.ymusicControl("next") },
                                        modifier = Modifier.size(34.dp)
                                    ) {
                                        Icon(Icons.Rounded.SkipNext, contentDescription = "Следующий", modifier = Modifier.size(20.dp))
                                    }
                                }
                            }
                        }
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                        NavigationBar(
                            containerColor = Color(0xFF0A101E),
                            tonalElevation = 0.dp
                        ) {
                            AppTab.entries.forEach { tab ->
                                NavigationBarItem(
                                    selected = selectedTab == tab,
                                    onClick = {
                                        selectedTab = tab
                                        when (tab) {
                                            AppTab.HOME -> {
                                                client.getTimers()
                                                client.getAlarms()
                                            }
                                            AppTab.MUSIC -> client.ymusicStatus()
                                            AppTab.SETTINGS -> client.otaStatus()
                                            else -> Unit
                                        }
                                    },
                                    icon = { Icon(tab.icon, contentDescription = tab.label) },
                                    label = {
                                        Text(
                                            tab.label,
                                            fontWeight = if (selectedTab == tab) FontWeight.SemiBold else FontWeight.Normal
                                        )
                                    }
                                )
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
                    .padding(horizontal = 16.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp),
                contentPadding = PaddingValues(vertical = 12.dp)
            ) {
                if (!hasPerms) {
                    item {
                        ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                            Column(
                                Modifier.padding(20.dp),
                                verticalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                Text("Разрешение Bluetooth", style = MaterialTheme.typography.titleMedium)
                                Text("Для управления колонками AtriOS по Bluetooth RFCOMM SPP требуется разрешение Bluetooth.")
                                Button(onClick = {
                                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                                        permLauncher.launch(
                                            arrayOf(
                                                Manifest.permission.BLUETOOTH_CONNECT,
                                                Manifest.permission.BLUETOOTH_SCAN
                                            )
                                        )
                                    }
                                }) { Text("Разрешить") }
                            }
                        }
                    }
                }

                if (!connected && hasPerms) {
                    item {
                        DevicePickerSection(
                            savedSpeakers = savedSpeakers,
                            bondedDevices = bondedDevices,
                            onConnect = { dev -> client.connect(dev) },
                            onForgetSaved = { mac -> savedSpeakers = storage.removeSpeaker(mac) }
                        )
                    }
                }

                if (!connected) {
                    item {
                        var gwInput by remember(relayConfig.gatewayUrl) { mutableStateOf(relayConfig.gatewayUrl) }
                        var didInput by remember(relayConfig.deviceId) { mutableStateOf(relayConfig.deviceId) }
                        var tokInput by remember(relayConfig.token) { mutableStateOf(relayConfig.token) }
                        ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                            Column(
                                modifier = Modifier.fillMaxWidth().padding(20.dp),
                                verticalArrangement = Arrangement.spacedBy(10.dp)
                            ) {
                                Text("Управление по Wi-Fi (TLS)", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                                Text(
                                    "После первичной настройки по Bluetooth колонка автоматически подключается к шлюзу ai.leftymods.tech.",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                OutlinedTextField(
                                    value = gwInput,
                                    onValueChange = { gwInput = it },
                                    label = { Text("HTTPS шлюз") },
                                    singleLine = true,
                                    shape = RoundedCornerShape(14.dp),
                                    modifier = Modifier.fillMaxWidth()
                                )
                                OutlinedTextField(
                                    value = didInput,
                                    onValueChange = { didInput = it },
                                    label = { Text("Device ID") },
                                    singleLine = true,
                                    shape = RoundedCornerShape(14.dp),
                                    modifier = Modifier.fillMaxWidth()
                                )
                                OutlinedTextField(
                                    value = tokInput,
                                    onValueChange = { tokInput = it },
                                    label = { Text("Bearer токен шлюза") },
                                    singleLine = true,
                                    shape = RoundedCornerShape(14.dp),
                                    modifier = Modifier.fillMaxWidth()
                                )
                                Button(
                                    onClick = {
                                        val cfg = RelayConfig(gwInput.trim(), didInput.trim(), tokInput.trim())
                                        relayConfig = cfg
                                        storage.saveRelayConfig(cfg)
                                        client.connectRelay(cfg.gatewayUrl, cfg.deviceId, cfg.token)
                                    },
                                    enabled = gwInput.trim().startsWith("https://") && didInput.isNotBlank() && tokInput.isNotBlank(),
                                    modifier = Modifier.fillMaxWidth().height(48.dp),
                                    shape = RoundedCornerShape(14.dp)
                                ) {
                                    Text("Подключиться по Wi-Fi (HTTPS)")
                                }
                            }
                        }
                    }
                }

                if (connected) {
                    when (selectedTab) {
                        AppTab.HOME -> {
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
                                            delay(1200)
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

                        AppTab.ASSISTANT -> {
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
                        }

                        AppTab.SETTINGS -> {
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
                                ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                                    Column(
                                        modifier = Modifier.fillMaxWidth().padding(20.dp),
                                        verticalArrangement = Arrangement.spacedBy(10.dp)
                                    ) {
                                        Text("Об устройстве и приложении", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                                        Text("Модель: ${status.model} · v${status.fwVersion}", style = MaterialTheme.typography.bodyMedium)
                                        if (status.soc.isNotBlank()) {
                                            Text("SoC: ${status.soc}", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                        }
                                        val rel = availableAppUpdate
                                        if (rel != null) {
                                            val prog = appUpdateProgress
                                            if (prog != null) {
                                                LinearProgressIndicator(progress = { prog }, modifier = Modifier.fillMaxWidth())
                                                Text("Загрузка APK: ${(prog * 100).toInt()}%", style = MaterialTheme.typography.bodySmall)
                                            } else {
                                                Button(
                                                    onClick = {
                                                        scope.launch {
                                                            appUpdateProgress = 0f
                                                            val res = AppUpdater.downloadAndInstall(context, rel) { p ->
                                                                appUpdateProgress = p
                                                            }
                                                            appUpdateProgress = null
                                                            res.onFailure { e ->
                                                                snackbarHostState.showSnackbar("Ошибка загрузки APK: ${e.message}")
                                                            }
                                                        }
                                                    },
                                                    shape = RoundedCornerShape(14.dp)
                                                ) {
                                                    Icon(Icons.Rounded.Download, contentDescription = null)
                                                    Spacer(Modifier.width(6.dp))
                                                    Text("Обновить приложение (v${rel.version})")
                                                }
                                            }
                                        } else {
                                            FilledTonalButton(
                                                onClick = {
                                                    scope.launch {
                                                        availableAppUpdate = AppUpdater.checkForUpdate(context)
                                                    }
                                                },
                                                shape = RoundedCornerShape(14.dp)
                                            ) {
                                                Icon(Icons.Rounded.Refresh, contentDescription = null)
                                                Spacer(Modifier.width(6.dp))
                                                Text("Проверить обновления приложения")
                                            }
                                        }
                                    }
                                }
                            }
                            // Collapsible Debug / Engineering Menu (Screen animations, RGB ring, Time & Telemetry)
                            item {
                                ElevatedCard(
                                    shape = RoundedCornerShape(24.dp),
                                    colors = CardDefaults.elevatedCardColors(
                                        containerColor = MaterialTheme.colorScheme.surfaceContainerLow
                                    )
                                ) {
                                    Row(
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .clickable { showDebugMenu = !showDebugMenu }
                                            .padding(horizontal = 20.dp, vertical = 16.dp),
                                        horizontalArrangement = Arrangement.SpaceBetween,
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        Row(
                                            verticalAlignment = Alignment.CenterVertically,
                                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                                        ) {
                                            Icon(
                                                Icons.Rounded.BugReport,
                                                contentDescription = null,
                                                tint = MaterialTheme.colorScheme.secondary
                                            )
                                            Column {
                                                Text("Debug-меню", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                                                Text(
                                                    "Анимации экрана, RGB-кольцо, время, телеметрия",
                                                    style = MaterialTheme.typography.labelSmall,
                                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                                )
                                            }
                                        }
                                        Icon(
                                            if (showDebugMenu) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                                            contentDescription = null
                                        )
                                    }
                                }
                            }
                            if (showDebugMenu) {
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
                                item {
                                    TimeAndTimezoneCard(
                                        localTime = status.localTime,
                                        timezone = status.timezone,
                                        onSyncNtp = { client.ntpSync() },
                                        onSetTimezone = { tz -> client.setTimezone(tz) }
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
