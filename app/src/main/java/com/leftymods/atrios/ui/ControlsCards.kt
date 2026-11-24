package com.leftymods.atrios.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.VolumeOff
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.leftymods.atrios.model.AiStatus
import com.leftymods.atrios.model.AlarmItem
import com.leftymods.atrios.model.DeviceCapabilities
import com.leftymods.atrios.model.SpeakerStatus
import com.leftymods.atrios.model.TimerItem
import java.util.TimeZone

@Composable
fun TelemetryCard(status: SpeakerStatus) {
    Card(
        shape = RoundedCornerShape(24.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    buildString {
                        append(status.os)
                        if (status.fwVersion.isNotBlank()) append(" v${status.fwVersion}")
                    },
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
                if (status.codename.isNotBlank()) {
                    Text(
                        status.codename,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                }
            }
            if (status.kernel.isNotBlank()) {
                Text(
                    "Ядро: ${status.kernel}${if (status.arch.isNotBlank()) " (${status.arch})" else ""}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("CPU: ${status.cpuTemp} °C", fontWeight = FontWeight.Medium)
                Text("ОЗУ: ${status.memUsedMb}/${status.memTotalMb} МБ")
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                val wifiText = if (status.wifiConnected) {
                    "Wi-Fi: ${status.wifiSsid.ifBlank { "OK" }} (${status.wifiIp})"
                } else {
                    "Wi-Fi: не подключён"
                }
                Text(wifiText, style = MaterialTheme.typography.bodySmall)
                if (status.capabilities.hasLuxSensor && status.lux != null) {
                    Text("Свет: ${status.lux} лк", style = MaterialTheme.typography.bodySmall)
                }
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(
                    "Аптайм: ${formatDuration(status.uptimeSec)}",
                    style = MaterialTheme.typography.bodySmall
                )
                if (status.micMuted) {
                    Text(
                        "Микрофон выкл.",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.error
                    )
                }
            }
        }
    }
}

@Composable
fun AudioCard(
    volume: Float,
    mute: Boolean,
    speakersDesc: String,
    onVolumeChange: (Float) -> Unit,
    onVolumeCommit: (Int) -> Unit,
    onToggleMute: () -> Unit,
    onAudioTest: () -> Unit,
    showAudioTest: Boolean = false
) {
    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(
                        if (mute || volume.toInt() == 0) Icons.AutoMirrored.Rounded.VolumeOff else Icons.AutoMirrored.Rounded.VolumeUp,
                        contentDescription = null
                    )
                    Text("Аудио и громкость", style = MaterialTheme.typography.titleMedium)
                }
                Text("${volume.toInt()}%", fontWeight = FontWeight.SemiBold)
            }
            if (speakersDesc.isNotBlank()) {
                Text(
                    speakersDesc,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Slider(
                value = volume,
                onValueChange = onVolumeChange,
                onValueChangeFinished = { onVolumeCommit(volume.toInt()) },
                valueRange = 0f..100f
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilledTonalButton(onClick = onToggleMute) {
                    Text(if (mute) "Вкл. звук" else "Без звука")
                }
                if (showAudioTest) {
                    FilledTonalButton(onClick = onAudioTest) {
                        Text("Тест звука")
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LedAndScreenCard(
    capabilities: DeviceCapabilities,
    currentScreenMode: String,
    currentRingMode: String,
    onSetScreen: (String, String?, String?) -> Unit,
    onSetRing: (String) -> Unit
) {
    var tickerText by remember { mutableStateOf("") }
    var eyesState by remember { mutableStateOf("happy") }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Icon(Icons.Rounded.LightMode, contentDescription = null)
                Text("LED-экран и кольцо", style = MaterialTheme.typography.titleMedium)
            }

            if (capabilities.hasLedMatrix) {
                Text("Режим матрицы", style = MaterialTheme.typography.labelLarge)
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    val modes = listOf(
                        "clock" to "Часы",
                        "kisser" to "Kisser",
                        "eyes" to "Глаза",
                        "off" to "Выкл"
                    )
                    modes.forEach { (mode, label) ->
                        FilterChip(
                            selected = currentScreenMode == mode,
                            onClick = {
                                if (mode == "eyes") {
                                    onSetScreen("eyes", eyesState, null)
                                } else {
                                    onSetScreen(mode, null, null)
                                }
                            },
                            label = { Text(label) }
                        )
                    }
                }

                if (currentScreenMode == "eyes") {
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        listOf(
                            "happy" to "Весёлые",
                            "neutral" to "Спокойные",
                            "sleepy" to "Сонные",
                            "love" to "Любовь"
                        ).forEach { (st, lbl) ->
                            AssistChip(
                                onClick = {
                                    eyesState = st
                                    onSetScreen("eyes", st, null)
                                },
                                label = { Text(lbl) }
                            )
                        }
                    }
                }

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedTextField(
                        value = tickerText,
                        onValueChange = { tickerText = it },
                        label = { Text("Бегущая строка") },
                        modifier = Modifier.weight(1f),
                        singleLine = true
                    )
                    Button(
                        onClick = {
                            if (tickerText.isNotBlank()) {
                                onSetScreen("ticker", null, tickerText.trim())
                            }
                        }
                    ) { Text("OK") }
                }
            }

            if (capabilities.hasRgbRing) {
                Text("Верхнее RGB-кольцо", style = MaterialTheme.typography.labelLarge)
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    listOf(
                        "rainbow" to "Радуга",
                        "volume" to "Громкость",
                        "amber" to "Янтарный",
                        "blue" to "Синий",
                        "green" to "Зелёный",
                        "red" to "Красный",
                        "off" to "Выкл"
                    ).forEach { (mode, label) ->
                        FilterChip(
                            selected = currentRingMode == mode,
                            onClick = { onSetRing(mode) },
                            label = { Text(label) }
                        )
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun TimersAndAlarmsCard(
    timers: List<TimerItem>,
    alarms: List<AlarmItem>,
    onRefresh: () -> Unit,
    onAddTimer: (Int) -> Unit,
    onCancelTimer: (Int?) -> Unit,
    onAddAlarm: (Int, Int) -> Unit,
    onCancelAlarm: (Int?) -> Unit
) {
    var customMinutes by remember { mutableStateOf("5") }
    var alarmHour by remember { mutableStateOf("07") }
    var alarmMinute by remember { mutableStateOf("30") }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(Icons.Rounded.Timer, contentDescription = null)
                    Text("Таймеры и будильники", style = MaterialTheme.typography.titleMedium)
                }
                IconButton(onClick = onRefresh) {
                    Icon(Icons.Rounded.Refresh, contentDescription = "Обновить")
                }
            }

            // Timers section
            Text("Таймеры", style = MaterialTheme.typography.labelLarge)
            if (timers.isEmpty()) {
                Text(
                    "Нет активных таймеров",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                timers.forEach { t ->
                    Surface(
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.surfaceContainerHigh
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column {
                                Text("Таймер #${t.id}", fontWeight = FontWeight.Medium)
                                Text(
                                    "Осталось: ${formatDuration(t.remainingSecs)} из ${formatDuration(t.totalSecs)}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            TextButton(onClick = { onCancelTimer(t.id) }) { Text("Отменить") }
                        }
                    }
                }
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                listOf(60 to "1 мин", 300 to "5 мин", 600 to "10 мин", 900 to "15 мин", 1800 to "30 мин")
                    .forEach { (secs, label) ->
                        AssistChip(onClick = { onAddTimer(secs) }, label = { Text("+$label") })
                    }
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedTextField(
                    value = customMinutes,
                    onValueChange = { customMinutes = it.filter(Char::isDigit).take(4) },
                    label = { Text("Минуты") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f),
                    singleLine = true
                )
                Button(onClick = {
                    val mins = customMinutes.toIntOrNull() ?: 0
                    if (mins in 1..1440) onAddTimer(mins * 60)
                }) { Text("Запустить") }
                if (timers.isNotEmpty()) {
                    TextButton(onClick = { onCancelTimer(null) }) { Text("Сброс всех") }
                }
            }

            HorizontalDivider()

            // Alarms section
            Text("Будильники", style = MaterialTheme.typography.labelLarge)
            if (alarms.isEmpty()) {
                Text(
                    "Нет установленных будильников",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                alarms.forEach { a ->
                    Surface(
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.surfaceContainerHigh
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(10.dp)
                            ) {
                                Icon(Icons.Rounded.Alarm, contentDescription = null)
                                Text(a.formattedTime, style = MaterialTheme.typography.titleMedium)
                                Text(
                                    if (a.enabled) "Вкл" else "Выкл",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            TextButton(onClick = { onCancelAlarm(a.id) }) { Text("Удалить") }
                        }
                    }
                }
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedTextField(
                    value = alarmHour,
                    onValueChange = { alarmHour = it.filter(Char::isDigit).take(2) },
                    label = { Text("Час") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f),
                    singleLine = true
                )
                OutlinedTextField(
                    value = alarmMinute,
                    onValueChange = { alarmMinute = it.filter(Char::isDigit).take(2) },
                    label = { Text("Мин") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f),
                    singleLine = true
                )
                Button(onClick = {
                    val h = (alarmHour.toIntOrNull() ?: 7).coerceIn(0, 23)
                    val m = (alarmMinute.toIntOrNull() ?: 0).coerceIn(0, 59)
                    onAddAlarm(h, m)
                }) { Text("Добавить") }
            }
            if (alarms.isNotEmpty()) {
                TextButton(onClick = { onCancelAlarm(null) }) { Text("Удалить все будильники") }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun AiAssistantCard(
    ai: AiStatus,
    onApplyAi: (
        enabled: Boolean?,
        mode: String?,
        server: String?,
        wakeWord: String?,
        ttsVoice: String?,
        ttsEmotion: String?
    ) -> Unit
) {
    var serverText by remember(ai.server) { mutableStateOf(ai.server) }
    var wakeText by remember(ai.wakeWord) { mutableStateOf(ai.wakeWord) }
    var selectedMode by remember(ai.mode) { mutableStateOf(ai.mode) }
    var selectedVoice by remember(ai.ttsVoice) { mutableStateOf(ai.ttsVoice) }
    var selectedEmotion by remember(ai.ttsEmotion) { mutableStateOf(ai.ttsEmotion) }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(Icons.Rounded.AutoAwesome, contentDescription = null)
                    Column {
                        Text("ИИ-ассистент и Qwen3-TTS", style = MaterialTheme.typography.titleMedium)
                        Text(
                            "Состояние: ${ai.state}",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                Switch(
                    checked = ai.enabled,
                    onCheckedChange = { enabled ->
                        onApplyAi(enabled, null, null, null, null, null)
                    }
                )
            }

            if (ai.lastAsr.isNotBlank() || ai.lastAnswer.isNotBlank()) {
                Surface(
                    shape = RoundedCornerShape(14.dp),
                    color = MaterialTheme.colorScheme.surfaceContainerHigh
                ) {
                    Column(
                        modifier = Modifier.fillMaxWidth().padding(12.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        if (ai.lastAsr.isNotBlank()) {
                            Text("Вы: ${ai.lastAsr}", style = MaterialTheme.typography.bodySmall)
                        }
                        if (ai.lastAnswer.isNotBlank()) {
                            Text(
                                "Ассистент: ${ai.lastAnswer}",
                                style = MaterialTheme.typography.bodySmall,
                                fontWeight = FontWeight.Medium
                            )
                        }
                    }
                }
            }

            if (ai.error.isNotBlank()) {
                Text(
                    "Ошибка ИИ: ${ai.error}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error
                )
            }

            Text("Режим работы ИИ", style = MaterialTheme.typography.labelLarge)
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                AiStatus.MODES.forEach { (id, label) ->
                    FilterChip(
                        selected = selectedMode == id,
                        onClick = {
                            selectedMode = id
                            onApplyAi(null, id, null, null, null, null)
                        },
                        label = { Text(label) }
                    )
                }
            }

            if (selectedMode != "local") {
                OutlinedTextField(
                    value = serverText,
                    onValueChange = { serverText = it },
                    label = { Text("Адрес GPU-сервера AtriAI") },
                    placeholder = { Text("https://192.168.1.107:8090") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )
            }

            OutlinedTextField(
                value = wakeText,
                onValueChange = { wakeText = it },
                label = { Text("Слово активации (Wake Word)") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true
            )

            Text("Голос Qwen3-TTS", style = MaterialTheme.typography.labelLarge)
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                AiStatus.QWEN3_VOICES.forEach { voice ->
                    FilterChip(
                        selected = selectedVoice.equals(voice, ignoreCase = true),
                        onClick = {
                            selectedVoice = voice
                            onApplyAi(null, null, null, null, voice, null)
                        },
                        label = { Text(voice) }
                    )
                }
            }

            Text("Эмоция голоса (Qwen3-TTS)", style = MaterialTheme.typography.labelLarge)
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                AiStatus.QWEN3_EMOTIONS.forEach { (id, label) ->
                    FilterChip(
                        selected = selectedEmotion.equals(id, ignoreCase = true),
                        onClick = {
                            selectedEmotion = id
                            onApplyAi(null, null, null, null, null, id)
                        },
                        label = { Text(label) }
                    )
                }
            }

            Button(
                onClick = {
                    onApplyAi(
                        ai.enabled,
                        selectedMode,
                        if (selectedMode != "local") serverText else null,
                        wakeText,
                        selectedVoice,
                        selectedEmotion
                    )
                }
            ) {
                Icon(Icons.Rounded.Save, contentDescription = null)
                Spacer(Modifier.width(6.dp))
                Text("Сохранить настройки ИИ")
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun BluetoothCard(
    status: SpeakerStatus,
    onToggleSpeakerMode: (Boolean) -> Unit,
    onPair: (Int) -> Unit,
    onHide: () -> Unit,
    onUnpair: (String) -> Unit
) {
    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(Icons.Rounded.BluetoothAudio, contentDescription = null)
                    Text("Bluetooth и приватность", style = MaterialTheme.typography.titleMedium)
                }
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

            if (status.capabilities.hasBluetoothSpeaker) {
                Surface(
                    shape = RoundedCornerShape(16.dp),
                    color = MaterialTheme.colorScheme.surfaceContainerHigh
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 10.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text("Режим Bluetooth-колонки", fontWeight = FontWeight.Medium)
                            Text(
                                if (status.btSpeakerMode) "A2DP-приёмник активен" else "Режим колонки выключен",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Switch(
                            checked = status.btSpeakerMode,
                            onCheckedChange = onToggleSpeakerMode
                        )
                    }
                }
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
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(mac, style = MaterialTheme.typography.bodyMedium)
                    TextButton(onClick = { onUnpair(mac) }) { Text("Удалить") }
                }
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Button(onClick = { onPair(120) }) { Text("Сопряжение 120 с") }
                FilledTonalButton(onClick = onHide) { Text("Скрыть") }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun TimeAndTimezoneCard(
    localTime: String,
    timezone: String,
    onSyncNtp: () -> Unit,
    onSetTimezone: (String) -> Unit
) {
    val phoneTz = remember { TimeZone.getDefault().id }
    var customTz by remember(timezone) { mutableStateOf(timezone) }
    val presets = listOf(
        phoneTz,
        "Europe/Moscow",
        "Europe/Kaliningrad",
        "Asia/Yekaterinburg",
        "Asia/Novosibirsk",
        "Asia/Vladivostok",
        "UTC"
    ).distinct()

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Icon(Icons.Rounded.Schedule, contentDescription = null)
                Text("Время (NTP) и часовой пояс", style = MaterialTheme.typography.titleMedium)
            }
            if (localTime.isNotBlank()) {
                Text("Время на колонке: $localTime", fontWeight = FontWeight.Medium)
            }
            Text(
                "Часовой пояс: ${timezone.ifBlank { "UTC" }}",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Button(onClick = onSyncNtp) {
                    Icon(Icons.Rounded.Sync, contentDescription = null)
                    Spacer(Modifier.width(6.dp))
                    Text("Синхронизировать NTP")
                }
                if (phoneTz != timezone) {
                    FilledTonalButton(onClick = { onSetTimezone(phoneTz) }) {
                        Text("Как на телефоне ($phoneTz)")
                    }
                }
            }
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                presets.forEach { tz ->
                    FilterChip(
                        selected = timezone == tz,
                        onClick = { onSetTimezone(tz) },
                        label = { Text(tz) }
                    )
                }
            }
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedTextField(
                    value = customTz,
                    onValueChange = { customTz = it },
                    label = { Text("IANA зона (напр. Europe/Moscow)") },
                    modifier = Modifier.weight(1f),
                    singleLine = true
                )
                Button(onClick = {
                    if (customTz.isNotBlank()) onSetTimezone(customTz.trim())
                }) { Text("Задать") }
            }
        }
    }
}

@Composable
fun WifiCard(
    wifiConnected: Boolean,
    wifiSsid: String,
    wifiIp: String,
    onConnectWifi: (String, String) -> Unit
) {
    var ssid by remember(wifiSsid) { mutableStateOf(wifiSsid) }
    var pass by remember { mutableStateOf("") }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Icon(Icons.Rounded.Wifi, contentDescription = null)
                Text("Wi-Fi", style = MaterialTheme.typography.titleMedium)
            }
            Text(
                if (wifiConnected) "Подключено: $wifiSsid ($wifiIp)" else "Не подключено",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            OutlinedTextField(
                value = ssid,
                onValueChange = { ssid = it },
                label = { Text("SSID") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )
            OutlinedTextField(
                value = pass,
                onValueChange = { pass = it },
                label = { Text("Пароль") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )
            Button(onClick = {
                if (ssid.isNotBlank()) onConnectWifi(ssid.trim(), pass)
            }) { Text("Подключить Wi-Fi") }
        }
    }
}

private fun formatDuration(totalSecs: Int): String {
    val s = totalSecs.coerceAtLeast(0)
    val h = s / 3600
    val m = (s % 3600) / 60
    val sec = s % 60
    return when {
        h > 0 -> "${h}ч ${m}м"
        m > 0 -> "${m}м ${sec}с"
        else -> "${sec}с"
    }
}
