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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.leftymods.atrios.ui.theme.AtriOSTheme
import org.json.JSONObject

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

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@SuppressLint("MissingPermission")
@Composable
fun AtriOSApp() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val btManager = remember { context.getSystemService(BluetoothManager::class.java) }
    val adapter: BluetoothAdapter? = btManager?.adapter

    var hasPerms by remember {
        mutableStateOf(
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
            } else true
        )
    }
    val permLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { res -> hasPerms = res.values.all { it } }

    var connected by remember { mutableStateOf(false) }
    var connText by remember { mutableStateOf("Выберите колонку для подключения") }
    var status by remember { mutableStateOf(SpeakerStatus()) }
    var sliderVol by remember { mutableFloatStateOf(60f) }
    var tickerText by remember { mutableStateOf("") }
    var wifiSsid by remember { mutableStateOf("") }
    var wifiPass by remember { mutableStateOf("") }
    var showRebootDialog by remember { mutableStateOf(false) }

    val client = remember {
        BtControlClient(
            scope = scope,
            onStatus = { s ->
                status = s
                sliderVol = s.volume.toFloat()
            },
            onVolumePush = { v -> sliderVol = v.toFloat() },
            onButtonsPush = { action, mic ->
                status = status.copy(actionPressed = action, micMuted = mic)
            },
            onConnectionState = { ok, msg ->
                connected = ok
                connText = msg
            }
        )
    }

    DisposableEffect(Unit) { onDispose { client.disconnect() } }

    val bondedDevices: List<BluetoothDevice> = remember(hasPerms, adapter) {
        if (hasPerms && adapter != null) adapter.bondedDevices.toList() else emptyList()
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text(status.model, fontWeight = FontWeight.SemiBold)
                        Text(
                            connText,
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                },
                actions = {
                    if (connected) {
                        IconButton(onClick = { client.disconnect() }) {
                            Icon(Icons.Rounded.BluetoothDisabled, contentDescription = "Отключить")
                        }
                    }
                }
            )
        }
    ) { pad ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(pad).padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
            contentPadding = PaddingValues(vertical = 12.dp)
        ) {
            if (!hasPerms) {
                item {
                    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                            Text("Разрешение Bluetooth", style = MaterialTheme.typography.titleMedium)
                            Text("Для управления колонкой по RFCOMM SPP нужно разрешение Bluetooth.")
                            Button(onClick = {
                                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                                    permLauncher.launch(arrayOf(
                                        Manifest.permission.BLUETOOTH_CONNECT,
                                        Manifest.permission.BLUETOOTH_SCAN
                                    ))
                                }
                            }) { Text("Разрешить") }
                        }
                    }
                }
            }

            if (!connected && hasPerms) {
                item {
                    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                            Text("Сопряжённые устройства", style = MaterialTheme.typography.titleMedium)
                            if (bondedDevices.isEmpty()) {
                                Text("Нет сопряжённых колонок. Сначала выполните сопряжение в настройках Bluetooth телефона.")
                            } else {
                                bondedDevices.forEach { dev ->
                                    FilledTonalButton(
                                        onClick = { client.connect(dev) },
                                        modifier = Modifier.fillMaxWidth()
                                    ) {
                                        Icon(Icons.Rounded.Speaker, contentDescription = null)
                                        Spacer(Modifier.width(8.dp))
                                        Text("${dev.name ?: "AtriStation"} (${dev.address})")
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (connected) {
                item {
                    Card(
                        shape = RoundedCornerShape(24.dp),
                        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)
                    ) {
                        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("${status.os} · ${status.kernel}", style = MaterialTheme.typography.labelLarge)
                            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                                Text("CPU: ${status.cpuTemp} °C", fontWeight = FontWeight.Medium)
                                Text("RAM: ${status.memUsedMb}/${status.memTotalMb} МБ", fontWeight = FontWeight.Medium)
                            }
                            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                                Text("Свет: ${status.lux?.let { "$it lux" } ?: "—"}")
                                Text(if (status.wifiConnected) "${status.wifiSsid} (${status.wifiIp})" else "Wi-Fi откл.")
                            }
                        }
                    }
                }

                item {
                    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                            Text("Верхние кнопки", style = MaterialTheme.typography.titleMedium)
                            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                                Text("Кнопка 1 (Действие / Play-Pause)")
                                AssistChip(
                                    onClick = {},
                                    label = { Text(if (status.actionPressed) "Нажата" else "Отпущена") }
                                )
                            }
                            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                                Text("Кнопка 2 (Микрофон)")
                                AssistChip(
                                    onClick = {},
                                    label = { Text(if (status.micMuted) "Выключен (Mute)" else "Включён") }
                                )
                            }
                        }
                    }
                }

                item {
                    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                                Text("Громкость", style = MaterialTheme.typography.titleMedium)
                                Text("${sliderVol.toInt()}%", fontWeight = FontWeight.Bold)
                            }
                            Slider(
                                value = sliderVol,
                                onValueChange = { sliderVol = it },
                                onValueChangeFinished = {
                                    client.sendCommand(JSONObject().put("cmd", "set_volume").put("volume", sliderVol.toInt()))
                                },
                                valueRange = 0f..100f
                            )
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                FilledTonalButton(onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "set_mute").put("mute", !status.mute))
                                }) { Text(if (status.mute) "Вкл. звук" else "Без звука") }
                                FilledTonalButton(onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "audio_test"))
                                }) { Text("Тест звука") }
                            }
                        }
                    }
                }

                item {
                    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                            Text("LED-экран и кольцо", style = MaterialTheme.typography.titleMedium)
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                FilterChip(selected = status.screenMode == "clock", onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "set_screen").put("mode", "clock"))
                                }, label = { Text("Часы") })
                                FilterChip(selected = status.screenMode == "kisser", onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "set_screen").put("mode", "kisser"))
                                }, label = { Text("Kisser") })
                                FilterChip(selected = status.screenMode == "eyes", onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "set_screen").put("mode", "eyes").put("state", "happy"))
                                }, label = { Text("Глаза") })
                                FilterChip(selected = status.screenMode == "off", onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "set_screen").put("mode", "off"))
                                }, label = { Text("Выкл") })
                            }
                            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                OutlinedTextField(
                                    value = tickerText,
                                    onValueChange = { tickerText = it },
                                    label = { Text("Бегущая строка") },
                                    modifier = Modifier.weight(1f),
                                    singleLine = true
                                )
                                Button(onClick = {
                                    if (tickerText.isNotBlank()) {
                                        client.sendCommand(JSONObject().put("cmd", "set_screen").put("mode", "ticker").put("text", tickerText))
                                    }
                                }) { Text("OK") }
                            }
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                AssistChip(onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "set_ring").put("mode", "rainbow"))
                                }, label = { Text("Кольцо: Радуга") })
                                AssistChip(onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "set_ring").put("mode", "volume"))
                                }, label = { Text("Кольцо: Громкость") })
                                AssistChip(onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "set_ring").put("mode", "off"))
                                }, label = { Text("Кольцо: Выкл") })
                            }
                        }
                    }
                }

                item {
                    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                                Text("Bluetooth и приватность", style = MaterialTheme.typography.titleMedium)
                                AssistChip(
                                    onClick = {},
                                    label = {
                                        Text(
                                            when {
                                                status.btHidden -> "Скрыта (PSCAN)"
                                                status.btPairRemainingSec > 0 -> "Видима (${status.btPairRemainingSec} с)"
                                                else -> "Видима"
                                            }
                                        )
                                    }
                                )
                            }
                            Text(
                                if (status.btHidden)
                                    "Идентификатор колонки скрыт от поиска, чужие устройства не могут подключиться."
                                else
                                    "Колонка видима для сопряжения.",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            status.pairedDevices.forEach { mac ->
                                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                                    Text(mac)
                                    TextButton(onClick = {
                                        client.sendCommand(JSONObject().put("cmd", "bt_unpair").put("mac", mac))
                                    }) { Text("Удалить") }
                                }
                            }
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Button(onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "bt_pair").put("seconds", 60))
                                }) { Text("Сопряжение 60 с") }
                                FilledTonalButton(onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "bt_hide"))
                                }) { Text("Скрыть") }
                            }
                        }
                    }
                }

                item {
                    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                            Text("Wi-Fi", style = MaterialTheme.typography.titleMedium)
                            OutlinedTextField(value = wifiSsid, onValueChange = { wifiSsid = it }, label = { Text("SSID") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                            OutlinedTextField(value = wifiPass, onValueChange = { wifiPass = it }, label = { Text("Пароль") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                            Button(onClick = {
                                if (wifiSsid.isNotBlank()) {
                                    client.sendCommand(JSONObject().put("cmd", "wifi_connect").put("ssid", wifiSsid).put("psk", wifiPass))
                                }
                            }) { Text("Подключить Wi-Fi") }
                        }
                    }
                }

                item {
                    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
                        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("Об устройстве", style = MaterialTheme.typography.titleMedium)
                            InfoRow("Модель", "${status.model} (${status.codename})")
                            InfoRow("Ревизия", status.rev)
                            InfoRow("Прошивка", status.fwVersion)
                            InfoRow("ОС", status.os)
                            InfoRow("Ядро", "${status.kernel} (${status.arch})")
                            InfoRow("Платформа", status.soc)
                            if (status.speakers.isNotBlank()) {
                                InfoRow("Акустика", status.speakers)
                            }
                            if (status.localTime.isNotBlank()) {
                                InfoRow("Время", "${status.localTime} (${status.timezone})")
                            }
                            InfoRow("Аптайм", formatUptime(status.uptimeSec))
                            InfoRow("Bluetooth A2DP", if (status.isSpeakerMode) "Активен" else "Ожидание")
                            Spacer(Modifier.height(4.dp))
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                FilledTonalButton(onClick = {
                                    client.sendCommand(JSONObject().put("cmd", "sync_time"))
                                }) { Text("Синхр. время (NTP)") }
                                OutlinedButton(onClick = { showRebootDialog = true }) {
                                    Text("Перезагрузить")
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
            text = { Text("Соединение по Bluetooth будет временно прервано на время загрузки.") },
            confirmButton = {
                TextButton(onClick = {
                    showRebootDialog = false
                    client.sendCommand(JSONObject().put("cmd", "reboot"))
                }) { Text("Перезагрузить") }
            },
            dismissButton = {
                TextButton(onClick = { showRebootDialog = false }) { Text("Отмена") }
            }
        )
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.width(12.dp))
        Text(value, style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.Medium)
    }
}

private fun formatUptime(sec: Int): String {
    val h = sec / 3600
    val m = (sec % 3600) / 60
    val s = sec % 60
    return if (h > 0) "${h}ч ${m}м ${s}с" else "${m}м ${s}с"
}
