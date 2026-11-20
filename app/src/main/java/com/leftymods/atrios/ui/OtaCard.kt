package com.leftymods.atrios.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.leftymods.atrios.model.OtaStatus
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun OtaUpdateCard(
    fwVersion: String,
    codename: String,
    ota: OtaStatus,
    onCheckUpdates: () -> Unit,
    onRefreshStatus: () -> Unit,
    onApplyUpdate: (Boolean) -> Unit,
    onRollback: (Boolean) -> Unit,
    onRebootNow: () -> Unit
) {
    var showApplyDialog by remember { mutableStateOf(false) }
    var showRollbackDialog by remember { mutableStateOf(false) }
    var autoReboot by remember { mutableStateOf(true) }
    var showChangelog by remember { mutableStateOf(false) }

    val displayCurrent = ota.currentVersion.ifBlank { fwVersion.ifBlank { "—" } }
    val stateLabel = when (ota.state) {
        "checking" -> "Проверка…"
        "up_to_date" -> "Актуально"
        "available" -> "Доступно обновление"
        "downloading" -> "Загрузка (${ota.progress}%)"
        "verifying" -> "Проверка целостности"
        "installing" -> "Установка (${ota.progress}%)"
        "installed" -> "Установлено"
        "rolling_back" -> "Откат прошивки…"
        "rolled_back" -> "Выполнен откат"
        "error" -> "Ошибка OTA"
        else -> "Ожидание"
    }

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
                    Icon(Icons.Rounded.SystemUpdate, contentDescription = null)
                    Text("Обновление прошивки (OTA)", style = MaterialTheme.typography.titleMedium)
                }
                AssistChip(
                    onClick = onRefreshStatus,
                    label = { Text(stateLabel) }
                )
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column {
                    Text(
                        "Текущая версия",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(displayCurrent, fontWeight = FontWeight.SemiBold)
                }
                if (codename.isNotBlank()) {
                    Column(horizontalAlignment = Alignment.End) {
                        Text(
                            "Платформа",
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(codename, fontWeight = FontWeight.Medium)
                    }
                }
            }

            if (ota.lastCheckTs > 0L) {
                val formatted = remember(ota.lastCheckTs) {
                    SimpleDateFormat("dd.MM.yyyy HH:mm", Locale.getDefault())
                        .format(Date(ota.lastCheckTs * 1000L))
                }
                Text(
                    "Последняя проверка: $formatted",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            if (ota.isBusy) {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    if (ota.progress > 0) {
                        LinearProgressIndicator(
                            progress = { ota.progress / 100f },
                            modifier = Modifier.fillMaxWidth()
                        )
                    } else {
                        LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                    }
                    val sizeText = if (ota.assetSize > 0L) {
                        " · ${formatMb(ota.downloadedBytes)} / ${formatMb(ota.assetSize)}"
                    } else ""
                    Text(
                        "${ota.message.ifBlank { ota.stage }}$sizeText",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            } else if (ota.message.isNotBlank()) {
                Text(
                    ota.message,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            if (!ota.error.isNullOrBlank()) {
                Surface(
                    shape = RoundedCornerShape(14.dp),
                    color = MaterialTheme.colorScheme.errorContainer
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(
                            Icons.Rounded.ErrorOutline,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onErrorContainer
                        )
                        Text(
                            ota.error,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onErrorContainer
                        )
                    }
                }
            }

            if (ota.state == "rolled_back") {
                Surface(
                    shape = RoundedCornerShape(14.dp),
                    color = MaterialTheme.colorScheme.tertiaryContainer
                ) {
                    Text(
                        "Обновление было автоматически отменено и восстановлена предыдущая рабочая прошивка.",
                        modifier = Modifier.fillMaxWidth().padding(12.dp),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onTertiaryContainer
                    )
                }
            }

            if (ota.updateAvailable && !ota.latestVersion.isNullOrBlank()) {
                Surface(
                    shape = RoundedCornerShape(16.dp),
                    color = MaterialTheme.colorScheme.secondaryContainer
                ) {
                    Column(
                        modifier = Modifier.fillMaxWidth().padding(14.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                "Новая версия: ${ota.latestVersion}",
                                style = MaterialTheme.typography.titleSmall,
                                color = MaterialTheme.colorScheme.onSecondaryContainer
                            )
                            if (ota.assetSize > 0L) {
                                Text(
                                    formatMb(ota.assetSize),
                                    style = MaterialTheme.typography.labelMedium,
                                    color = MaterialTheme.colorScheme.onSecondaryContainer
                                )
                            }
                        }
                        if (ota.changelog.isNotBlank()) {
                            TextButton(
                                onClick = { showChangelog = !showChangelog },
                                contentPadding = PaddingValues(0.dp)
                            ) {
                                Text(if (showChangelog) "Скрыть список изменений" else "Показать список изменений")
                            }
                            if (showChangelog) {
                                Text(
                                    ota.changelog,
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSecondaryContainer
                                )
                            }
                        }
                    }
                }
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    onClick = onCheckUpdates,
                    enabled = !ota.isBusy
                ) {
                    Icon(Icons.Rounded.Refresh, contentDescription = null)
                    Spacer(Modifier.width(6.dp))
                    Text("Проверить обновления")
                }

                if (ota.updateAvailable && !ota.isBusy) {
                    FilledTonalButton(onClick = { showApplyDialog = true }) {
                        Icon(Icons.Rounded.Download, contentDescription = null)
                        Spacer(Modifier.width(6.dp))
                        Text("Установить ${ota.latestVersion ?: ""}".trim())
                    }
                }

                if (ota.rebootRequired) {
                    Button(onClick = onRebootNow) {
                        Icon(Icons.Rounded.RestartAlt, contentDescription = null)
                        Spacer(Modifier.width(6.dp))
                        Text("Перезагрузить")
                    }
                }

                if (ota.rollbackAvailable && !ota.isBusy) {
                    OutlinedButton(onClick = { showRollbackDialog = true }) {
                        Icon(Icons.Rounded.History, contentDescription = null)
                        Spacer(Modifier.width(6.dp))
                        Text("Откат")
                    }
                }
            }
        }
    }

    if (showApplyDialog) {
        AlertDialog(
            onDismissRequest = { showApplyDialog = false },
            title = { Text("Установить обновление ${ota.latestVersion ?: ""}?") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(
                        "Колонка загрузит образ, проверит контрольную сумму и создаст резервную копию текущей прошивки для автоматического отката при сбое."
                    )
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = autoReboot, onCheckedChange = { autoReboot = it })
                        Text("Перезагрузить автоматически после установки")
                    }
                }
            },
            confirmButton = {
                Button(onClick = {
                    showApplyDialog = false
                    onApplyUpdate(autoReboot)
                }) { Text("Установить") }
            },
            dismissButton = {
                TextButton(onClick = { showApplyDialog = false }) { Text("Отмена") }
            }
        )
    }

    if (showRollbackDialog) {
        AlertDialog(
            onDismissRequest = { showRollbackDialog = false },
            title = { Text("Откатить прошивку?") },
            text = {
                Text("Будут восстановлены резервные копии предыдущей версии прошивки. После отката рекомендуется перезагрузить колонку.")
            },
            confirmButton = {
                Button(onClick = {
                    showRollbackDialog = false
                    onRollback(true)
                }) { Text("Откатить и перезагрузить") }
            },
            dismissButton = {
                TextButton(onClick = { showRollbackDialog = false }) { Text("Отмена") }
            }
        )
    }
}

private fun formatMb(bytes: Long): String {
    if (bytes <= 0L) return "0 МБ"
    val mb = bytes / (1024.0 * 1024.0)
    return String.format(Locale.US, "%.1f МБ", mb)
}
