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
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.leftymods.atrios.data.SpeakerStorage
import com.leftymods.atrios.model.AlarmItem
import com.leftymods.atrios.model.SavedSpeaker
import com.leftymods.atrios.model.SpeakerStatus
import com.leftymods.atrios.model.TimerItem
import com.leftymods.atrios.ui.AiAssistantCard
import com.leftymods.atrios.ui.AudioCard
import com.leftymods.atrios.ui.BluetoothCard
import com.leftymods.atrios.ui.DevicePickerSection
import com.leftymods.atrios.ui.LedAndScreenCard
import com.leftymods.atrios.ui.OtaUpdateCard
import com.leftymods.atrios.ui.TelemetryCard
import com.leftymods.atrios.ui.TimeAndTimezoneCard
import com.leftymods.atrios.ui.TimersAndAlarmsCard
import com.leftymods.atrios.ui.WifiCard
import com.leftymods.atrios.ui.theme.AtriOSTheme
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
    OVERVIEW("Обзор", Icons.Rounded.Dashboard),
    ASSISTANT("Ассистент", Icons.Rounded.AutoAwesome),
    NETWORK("Сеть и BT", Icons.Rounded.Bluetooth),
    SYSTEM("Система", Icons.Rounded.SystemUpdate)
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
    var savedSpeakers by remember { mutableStateOf(storage.loadSpeakers()) }
    var selectedTab by remember { mutableStateOf(AppTab.OVERVIEW) }
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
            onTimersUpdate = { list ->
                timers = list
            },
            onAlarmsUpdate = { list ->
                alarms = list
            },
            onBtSpeakerPush = { enabled ->
                status = status.copy(btSpeakerMode = enabled)
            },
            onMessage = { msg, _ ->
                scope.launch {
                    snackbarHostState.showSnackbar(msg, duration = SnackbarDuration.Short)
                }
            },
            onConnectionState = { ok, msg, dev ->
                connected = ok
                connText = msg
                activeDevice = dev
                if (!ok) {
                    timers = emptyList()
                    alarms = emptyList()
                }
            }
        )
    }

    DisposableEffect(Unit) { onDispose { client.disconnect(silent = true) } }

    val bondedDevices: List<BluetoothDevice> = remember(hasPerms, adapter, connected) {
        if (hasPerms && adapter != null) adapter.bondedDevices.toList() else emptyList()
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbarHostState) },
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text(
                            if (connected) status.model else "AtriOS Control",
                            fontWeight = FontWeight.SemiBold
                        )
                        Text(
                            connText,
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                },
                actions = {
                    if (connected) {
                        IconButton(onClick = { client.requestStatus() }) {
                            Icon(Icons.Rounded.Refresh, contentDescription = "Обновить статус")
                        }
                        IconButton(onClick = { client.disconnect() }) {
                            Icon(Icons.Rounded.BluetoothDisabled, contentDescription = "Сменить колонку")
                        }
                    }
                }
            )
        },
        bottomBar = {
            if (connected) {
                NavigationBar {
                    AppTab.entries.forEach { tab ->
                        NavigationBarItem(
                            selected = selectedTab == tab,
                            onClick = {
                                selectedTab = tab
                                when (tab) {
                                    AppTab.ASSISTANT -> {
                                        client.getTimers()
                                        client.getAlarms()
                                    }
                                    AppTab.SYSTEM -> client.otaStatus()
                                    else -> Unit
                                }
                            },
                            icon = { Icon(tab.icon, contentDescription = tab.label) },
                            label = { Text(tab.label) }
                        )
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

            if (connected) {
                when (selectedTab) {
                    AppTab.OVERVIEW -> {
                        item { TelemetryCard(status = status) }
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
                    }

                    AppTab.ASSISTANT -> {
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

                    AppTab.NETWORK -> {
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
                            WifiCard(
                                wifiConnected = status.wifiConnected,
                                wifiSsid = status.wifiSsid,
                                wifiIp = status.wifiIp,
                                onConnectWifi = { ssid, psk -> client.wifiConnect(ssid, psk) }
                            )
                        }
                    }

                    AppTab.SYSTEM -> {
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
                                    verticalArrangement = Arrangement.spacedBy(8.dp)
                                ) {
                                    Text("Об устройстве", style = MaterialTheme.typography.titleMedium)
                                    Text("Модель: ${status.model}", style = MaterialTheme.typography.bodyMedium)
                                    if (status.codename.isNotBlank()) {
                                        Text(
                                            "Кодовое имя: ${status.codename} ${status.rev}".trim(),
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant
                                        )
                                    }
                                    if (status.soc.isNotBlank()) {
                                        Text(
                                            "SoC: ${status.soc}",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant
                                        )
                                    }
                                    val capsText = buildList {
                                        if (status.capabilities.hasLedMatrix) add("LED-матрица")
                                        if (status.capabilities.hasRgbRing) add("RGB-кольцо")
                                        if (status.capabilities.hasLuxSensor) add("Датчик света")
                                        if (status.capabilities.hasBluetoothSpeaker) add("BT-колонка")
                                        if (status.capabilities.hasLocalAi) add("Локальный ИИ")
                                    }.joinToString(", ")
                                    Text(
                                        "Возможности: $capsText",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                    Spacer(Modifier.height(4.dp))
                                    OutlinedButton(onClick = { showRebootDialog = true }) {
                                        Icon(Icons.Rounded.RestartAlt, contentDescription = null)
                                        Spacer(Modifier.width(6.dp))
                                        Text("Перезагрузить колонку")
                                    }
                                }
                            }
                        }
                        item {
                            ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                                Column(
                                    modifier = Modifier.fillMaxWidth().padding(20.dp),
                                    verticalArrangement = Arrangement.spacedBy(10.dp)
                                ) {
                                    Text(
                                        "Приложение AtriOS Control v${AppUpdater.currentVersionName(context)}",
                                        style = MaterialTheme.typography.titleMedium
                                    )
                                    val rel = availableAppUpdate
                                    if (rel != null) {
                                        Text(
                                            "Доступна новая версия приложения: v${rel.version}",
                                            style = MaterialTheme.typography.bodyMedium,
                                            color = MaterialTheme.colorScheme.primary
                                        )
                                        val prog = appUpdateProgress
                                        if (prog != null) {
                                            LinearProgressIndicator(
                                                progress = { prog },
                                                modifier = Modifier.fillMaxWidth()
                                            )
                                        } else {
                                            Button(onClick = {
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
                                            }) {
                                                Icon(Icons.Rounded.Download, contentDescription = null)
                                                Spacer(Modifier.width(6.dp))
                                                Text("Обновить приложение (v${rel.version})")
                                            }
                                        }
                                    } else {
                                        FilledTonalButton(onClick = {
                                            scope.launch {
                                                val found = AppUpdater.checkForUpdate(context)
                                                availableAppUpdate = found
                                                if (found == null) {
                                                    snackbarHostState.showSnackbar("Установлена последняя версия приложения")
                                                }
                                            }
                                        }) {
                                            Icon(Icons.Rounded.Refresh, contentDescription = null)
                                            Spacer(Modifier.width(6.dp))
                                            Text("Проверить обновления приложения")
                                        }
                                    }
                                }
                            }
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
            text = { Text("Соединение по Bluetooth будет временно прервано на время перезагрузки.") },
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
