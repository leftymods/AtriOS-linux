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
import org.atrios.companion.viewmodel.StationUiState
import org.atrios.companion.viewmodel.StationViewModel

@Composable
fun SystemScreen(
    uiState: StationUiState,
    viewModel: StationViewModel,
    modifier: Modifier = Modifier
) {
    val scrollState = rememberScrollState()
    var showRebootConfirm by remember { mutableStateOf(false) }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Power Actions Card
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "System Maintenance",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary
                )
                Spacer(modifier = Modifier.height(12.dp))

                Button(
                    onClick = { showRebootConfirm = true },
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Default.RestartAlt, contentDescription = null)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Reboot Station")
                }
            }
        }

        // Hardware Specifications Card
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Hardware Profile",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary
                )
                Spacer(modifier = Modifier.height(12.dp))

                SpecRow("Target Board", uiState.status?.device?.model ?: "Yandex Station Max")
                SpecRow("SoC", uiState.status?.device?.soc ?: "Amlogic S905X3 64-bit Quad-Core")
                SpecRow("Linux Kernel", uiState.status?.device?.kernel ?: "4.9.113-atrios")
                SpecRow("Operating System", "AtriOS (Buildroot Embedded)")
                SpecRow("Sound Card", uiState.status?.audio?.card ?: "AML-AUGESOUND")
                SpecRow("Audio Amplifiers", "Silergy SY6045S (Woofer 30W + Tweeters 2x10W)")
                SpecRow("Headphone DAC", "Everest ES8156 (3.5mm Jack)")
                SpecRow("Mic Array", "Everest ES7210 8-ch ADC Array")
                SpecRow("Wireless", "Realtek RTL8822CS (Wi-Fi 802.11ac + BT 5.0)")
                SpecRow("Zigbee", "Silicon Labs EFR32MG21 Coordinator")
            }
        }
    }

    if (showRebootConfirm) {
        AlertDialog(
            onDismissRequest = { showRebootConfirm = false },
            title = { Text("Reboot Station?") },
            text = { Text("Are you sure you want to reboot Yandex Station Max? The connection will temporarily drop.") },
            confirmButton = {
                Button(
                    onClick = {
                        viewModel.reboot()
                        showRebootConfirm = false
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
                ) {
                    Text("Reboot Now")
                }
            },
            dismissButton = {
                TextButton(onClick = { showRebootConfirm = false }) {
                    Text("Cancel")
                }
            }
        )
    }
}

@Composable
fun SpecRow(key: String, value: String) {
    Column(modifier = Modifier.padding(vertical = 4.dp)) {
        Text(key, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.SemiBold)
    }
}
