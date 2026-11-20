package com.leftymods.atrios.ui

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.leftymods.atrios.model.SavedSpeaker

@SuppressLint("MissingPermission")
@Composable
fun DevicePickerSection(
    savedSpeakers: List<SavedSpeaker>,
    bondedDevices: List<BluetoothDevice>,
    onConnect: (BluetoothDevice) -> Unit,
    onForgetSaved: (String) -> Unit
) {
    val bondedByMac = bondedDevices.associateBy { it.address.uppercase() }

    if (savedSpeakers.isNotEmpty()) {
        ElevatedCard(shape = RoundedCornerShape(24.dp)) {
            Column(
                modifier = Modifier.fillMaxWidth().padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(Icons.Rounded.DevicesOther, contentDescription = null)
                    Text("Сохранённые колонки AtriOS", style = MaterialTheme.typography.titleMedium)
                }
                savedSpeakers.forEach { saved ->
                    val bonded = bondedByMac[saved.mac.uppercase()]
                    Surface(
                        shape = RoundedCornerShape(16.dp),
                        color = MaterialTheme.colorScheme.surfaceContainerHigh
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth().padding(12.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(saved.model.ifBlank { saved.name }, fontWeight = FontWeight.SemiBold)
                                val meta = buildList {
                                    if (saved.codename.isNotBlank()) add(saved.codename)
                                    if (saved.fwVersion.isNotBlank()) add("v${saved.fwVersion}")
                                    add(saved.mac)
                                }.joinToString(" · ")
                                Text(
                                    meta,
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                if (bonded != null) {
                                    FilledTonalButton(onClick = { onConnect(bonded) }) {
                                        Text("Подкл.")
                                    }
                                } else {
                                    Text(
                                        "Нет пары",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.error
                                    )
                                }
                                IconButton(onClick = { onForgetSaved(saved.mac) }) {
                                    Icon(Icons.Rounded.DeleteOutline, contentDescription = "Забыть")
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Icon(Icons.Rounded.Bluetooth, contentDescription = null)
                Text("Сопряжённые Bluetooth-устройства", style = MaterialTheme.typography.titleMedium)
            }
            if (bondedDevices.isEmpty()) {
                Text(
                    "Нет сопряжённых колонок. Сначала выполните сопряжение в настройках Bluetooth телефона.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                bondedDevices.forEach { dev ->
                    FilledTonalButton(
                        onClick = { onConnect(dev) },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.Speaker, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text("${dev.name ?: "AtriOS Speaker"} (${dev.address})")
                    }
                }
            }
        }
    }
}
