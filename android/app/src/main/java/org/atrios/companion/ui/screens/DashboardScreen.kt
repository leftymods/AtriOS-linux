package org.atrios.companion.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import org.atrios.companion.data.bluetooth.BtConnectionState
import org.atrios.companion.viewmodel.StationUiState
import org.atrios.companion.viewmodel.StationViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DashboardScreen(
    uiState: StationUiState,
    viewModel: StationViewModel,
    modifier: Modifier = Modifier
) {
    val scrollState = rememberScrollState()
    var showDeviceSheet by remember { mutableStateOf(false) }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Bluetooth Connection Banner
        ElevatedCard(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.elevatedCardColors(
                containerColor = when (uiState.btState) {
                    is BtConnectionState.Connected -> MaterialTheme.colorScheme.primaryContainer
                    is BtConnectionState.Connecting -> MaterialTheme.colorScheme.secondaryContainer
                    else -> MaterialTheme.colorScheme.surfaceVariant
                }
            )
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.weight(1f)) {
                    Icon(
                        imageVector = when (uiState.btState) {
                            is BtConnectionState.Connected -> Icons.Default.BluetoothConnected
                            is BtConnectionState.Connecting -> Icons.Default.BluetoothSearching
                            else -> Icons.Default.Bluetooth
                        },
                        contentDescription = null,
                        tint = when (uiState.btState) {
                            is BtConnectionState.Connected -> MaterialTheme.colorScheme.onPrimaryContainer
                            is BtConnectionState.Connecting -> MaterialTheme.colorScheme.onSecondaryContainer
                            else -> MaterialTheme.colorScheme.onSurfaceVariant
                        },
                        modifier = Modifier.size(28.dp)
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                    Column {
                        Text(
                            text = when (uiState.btState) {
                                is BtConnectionState.Connected -> "Bluetooth: Connected"
                                is BtConnectionState.Connecting -> "Connecting to ${uiState.btState.deviceName}..."
                                is BtConnectionState.Error -> "Connection Failed"
                                else -> "Bluetooth: Disconnected"
                            },
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                            color = when (uiState.btState) {
                                is BtConnectionState.Connected -> MaterialTheme.colorScheme.onPrimaryContainer
                                is BtConnectionState.Connecting -> MaterialTheme.colorScheme.onSecondaryContainer
                                else -> MaterialTheme.colorScheme.onSurfaceVariant
                            }
                        )
                        Text(
                            text = if (uiState.btState is BtConnectionState.Connected)
                                "${uiState.btState.deviceName} (${uiState.btState.address})"
                            else
                                "Peer-to-peer RFCOMM Serial Port Profile",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }

                if (uiState.btState is BtConnectionState.Connected) {
                    OutlinedButton(onClick = { viewModel.disconnect() }) {
                        Text("Disconnect")
                    }
                } else {
                    Button(onClick = {
                        viewModel.refreshDevices()
                        showDeviceSheet = true
                    }) {
                        Text("Connect")
                    }
                }
            }
        }

        // Live Volume Control (Operates over Bluetooth RFCOMM)
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = if (uiState.status?.audio?.mute == true) Icons.Default.VolumeOff else Icons.Default.VolumeUp,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Acoustic Volume",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                    Text(
                        text = "${uiState.status?.audio?.volume ?: 0}%",
                        style = MaterialTheme.typography.headlineSmall,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.primary
                    )
                }

                Spacer(modifier = Modifier.height(8.dp))

                Slider(
                    value = (uiState.status?.audio?.volume ?: 0).toFloat(),
                    onValueChange = { viewModel.setVolume(it.toInt()) },
                    valueRange = 0f..100f,
                    enabled = uiState.isConnected,
                    modifier = Modifier.fillMaxWidth()
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    OutlinedButton(
                        onClick = { viewModel.toggleMute() },
                        enabled = uiState.isConnected
                    ) {
                        Icon(
                            imageVector = if (uiState.status?.audio?.mute == true) Icons.Default.VolumeUp else Icons.Default.VolumeOff,
                            contentDescription = null
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(if (uiState.status?.audio?.mute == true) "Unmute" else "Mute")
                    }
                    Button(
                        onClick = { viewModel.testAudio() },
                        enabled = uiState.isConnected
                    ) {
                        Icon(Icons.Default.PlayArrow, contentDescription = null)
                        Spacer(modifier = Modifier.width(6.dp))
                        Text("Test Chime")
                    }
                }
            }
        }

        // Real-Time Telemetry over Bluetooth
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "Hardware Telemetry (via Bluetooth)",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.primary
                    )
                    IconButton(
                        onClick = { viewModel.requestStatus() },
                        enabled = uiState.isConnected
                    ) {
                        Icon(Icons.Default.Refresh, contentDescription = "Refresh telemetry", modifier = Modifier.size(20.dp))
                    }
                }
                Spacer(modifier = Modifier.height(8.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    MetricItem(
                        icon = Icons.Default.Thermostat,
                        label = "S905X3 Temp",
                        value = if (uiState.isConnected) "${uiState.status?.device?.cpuTemperature ?: 0.0} °C" else "—"
                    )
                    MetricItem(
                        icon = Icons.Default.Memory,
                        label = "RAM Usage",
                        value = if (uiState.isConnected) "${uiState.status?.device?.memory?.usedMb ?: 0} MB" else "—"
                    )
                    MetricItem(
                        icon = Icons.Default.Timer,
                        label = "Uptime",
                        value = if (uiState.isConnected) formatUptime(uiState.status?.device?.uptimeSeconds ?: 0) else "—"
                    )
                }
            }
        }

        // Station Sound Hardware & Amplifier Profile
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Acoustic Subsystem",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary
                )
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = "Sound Card: ${uiState.status?.audio?.card ?: "AML-AUGESOUND"}",
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = "Amplifiers: Silergy SY6045S Woofer (30W) + Tweeters (2x10W)",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = "Power Booster: 20V PVDD Active • Headphone Jack: ES8156",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }

    // Modal Bottom Sheet for Bluetooth Device Pairing & Selection
    if (showDeviceSheet) {
        ModalBottomSheet(onDismissRequest = { showDeviceSheet = false }) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Text(
                    text = "Select Paired Station",
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Choose Yandex Station Max to connect over Bluetooth:",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )

                if (uiState.pairedDevices.isEmpty()) {
                    Text(
                        text = "No paired Bluetooth devices found. Please pair with 'Yandex Station Max' in Android Settings first.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.error,
                        modifier = Modifier.padding(vertical = 12.dp)
                    )
                } else {
                    uiState.pairedDevices.forEach { dev ->
                        Card(
                            modifier = Modifier.fillMaxWidth(),
                            onClick = {
                                viewModel.connectToDevice(dev.address)
                                showDeviceSheet = false
                            },
                            colors = CardDefaults.cardColors(
                                containerColor = if (dev.isStation)
                                    MaterialTheme.colorScheme.primaryContainer
                                else
                                    MaterialTheme.colorScheme.surfaceContainerHigh
                            )
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(14.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    imageVector = if (dev.isStation) Icons.Default.Speaker else Icons.Default.Bluetooth,
                                    contentDescription = null,
                                    tint = if (dev.isStation) MaterialTheme.colorScheme.onPrimaryContainer else MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Column {
                                    Text(
                                        text = dev.name,
                                        style = MaterialTheme.typography.bodyLarge,
                                        fontWeight = FontWeight.Bold
                                    )
                                    Text(
                                        text = dev.address,
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        }
                    }
                }
                Spacer(modifier = Modifier.height(16.dp))
            }
        }
    }
}
