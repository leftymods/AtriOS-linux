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

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DisplayScreen(
    uiState: StationUiState,
    viewModel: StationViewModel,
    modifier: Modifier = Modifier
) {
    val scrollState = rememberScrollState()

    var ringBrightness by remember { mutableFloatStateOf(100f) }
    var selectedRingEffect by remember { mutableStateOf("Volume Tracking") }
    val ringEffects = listOf("Volume Tracking", "Clock Pulse", "Rainbow Loop", "Breathing", "Off")

    var matrixBrightness by remember { mutableFloatStateOf(80f) }
    var selectedMatrixMode by remember { mutableStateOf("24h Clock") }
    val matrixModes = listOf("24h Clock", "Weather", "Audio Spectrum", "Off")

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // LED Top Ring Card
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Highlight, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Top LED Ring (24 RGB LEDs)",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold
                    )
                }

                Spacer(modifier = Modifier.height(12.dp))

                Text("Display Effect:", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
                Spacer(modifier = Modifier.height(6.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    ringEffects.take(3).forEach { effect ->
                        FilterChip(
                            selected = (selectedRingEffect == effect),
                            onClick = {
                                selectedRingEffect = effect
                                if (uiState.isConnected) {
                                    viewModel.setLedMode(effect, ringBrightness.toInt())
                                }
                            },
                            label = { Text(effect) }
                        )
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    ringEffects.drop(3).forEach { effect ->
                        FilterChip(
                            selected = (selectedRingEffect == effect),
                            onClick = {
                                selectedRingEffect = effect
                                if (uiState.isConnected) {
                                    viewModel.setLedMode(effect, ringBrightness.toInt())
                                }
                            },
                            label = { Text(effect) }
                        )
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                Text("Ring Brightness: ${ringBrightness.toInt()}%", style = MaterialTheme.typography.bodyMedium)
                Slider(
                    value = ringBrightness,
                    onValueChange = {
                        ringBrightness = it
                        if (uiState.isConnected) {
                            viewModel.setLedMode(selectedRingEffect, it.toInt())
                        }
                    },
                    valueRange = 0f..100f,
                    enabled = uiState.isConnected
                )

                Text(
                    text = "Controlled over Bluetooth via /dev/fb5 (rotaryd / bt-control)",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }

        // Front LED Matrix Display Card
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Tv, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Front LED Matrix Display",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold
                    )
                }

                Spacer(modifier = Modifier.height(12.dp))

                Text("Matrix Content Mode:", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
                Spacer(modifier = Modifier.height(6.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    matrixModes.forEach { mode ->
                        FilterChip(
                            selected = (selectedMatrixMode == mode),
                            onClick = {
                                selectedMatrixMode = mode
                                if (uiState.isConnected) {
                                    viewModel.setScreenMode(mode, matrixBrightness.toInt())
                                }
                            },
                            label = { Text(mode) }
                        )
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                Text("Matrix Brightness: ${matrixBrightness.toInt()}%", style = MaterialTheme.typography.bodyMedium)
                Slider(
                    value = matrixBrightness,
                    onValueChange = {
                        matrixBrightness = it
                        if (uiState.isConnected) {
                            viewModel.setScreenMode(selectedMatrixMode, it.toInt())
                        }
                    },
                    valueRange = 0f..100f,
                    enabled = uiState.isConnected
                )

                Text(
                    text = "Controlled over Bluetooth via /dev/fb0 (screend)",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}
