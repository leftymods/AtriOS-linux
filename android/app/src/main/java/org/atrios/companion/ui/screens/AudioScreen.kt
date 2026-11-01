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
import kotlin.math.roundToInt
import kotlin.math.sqrt

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AudioScreen(
    uiState: StationUiState,
    viewModel: StationViewModel,
    modifier: Modifier = Modifier
) {
    val scrollState = rememberScrollState()
    val volume = uiState.status?.audio?.volume ?: 0
    val isMute = uiState.status?.audio?.mute ?: false
    var selectedEqPreset by remember { mutableStateOf("Flat") }
    val eqPresets = listOf("Flat", "Bass Boost", "Acoustic", "Vocal", "Night")

    // Approximate dB attenuation from wide dynamic range curve
    val calculatedDb = if (volume <= 0) -127.5 else {
        val maxDb = (255 * 0.5) - 12.0
        val db = -maxDb * (1.0 - sqrt(volume / 100.0))
        (db * 10).roundToInt() / 10.0
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Master Volume Card with M3 Slider
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(20.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = "Acoustic Volume Control",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary
                )
                Spacer(modifier = Modifier.height(16.dp))

                // Big percentage and dB readout
                Text(
                    text = if (isMute) "MUTED" else "$volume%",
                    style = MaterialTheme.typography.displayMedium,
                    fontWeight = FontWeight.Bold,
                    color = if (isMute) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
                )
                Text(
                    text = if (isMute) "Hardware output muted" else "$calculatedDb dB attenuation",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )

                Spacer(modifier = Modifier.height(16.dp))

                Slider(
                    value = volume.toFloat(),
                    onValueChange = { viewModel.setVolume(it.toInt()) },
                    valueRange = 0f..100f,
                    modifier = Modifier.fillMaxWidth()
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("0% (Mute)", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text("50% (-34 dB)", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text("100% (0 dB)", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }

                Spacer(modifier = Modifier.height(16.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceEvenly
                ) {
                    FilledTonalButton(onClick = { viewModel.toggleMute() }) {
                        Icon(if (isMute) Icons.Default.VolumeUp else Icons.Default.VolumeOff, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(if (isMute) "Unmute" else "Mute")
                    }
                    Button(onClick = { viewModel.testAudio() }) {
                        Icon(Icons.Default.MusicNote, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Play Test Sound")
                    }
                }
            }
        }

        // Equalizer & Acoustic Profile
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Acoustic Tuning & EQ",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary
                )
                Text(
                    text = "Hardware DSP profiles for SY6045S Silergy Class-D DAC",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(12.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    eqPresets.take(3).forEach { preset ->
                        FilterChip(
                            selected = (selectedEqPreset == preset),
                            onClick = { selectedEqPreset = preset },
                            label = { Text(preset) }
                        )
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    eqPresets.drop(3).forEach { preset ->
                        FilterChip(
                            selected = (selectedEqPreset == preset),
                            onClick = { selectedEqPreset = preset },
                            label = { Text(preset) }
                        )
                    }
                }
            }
        }

        // Hardware Amplifier Diagnostics
        ElevatedCard(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Hardware Output Stages",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary
                )
                Spacer(modifier = Modifier.height(12.dp))

                AudioStageRow(
                    name = "Woofer Stage (Silergy SY6045S @ 0x2b)",
                    specs = "30W RMS • TDM-B Lane 0 • 20V PVDD Boosted",
                    active = true
                )
                HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))
                AudioStageRow(
                    name = "Tweeters Stage (Silergy SY6045S @ 0x2a)",
                    specs = "2x10W RMS Stereo • 20V PVDD Boosted",
                    active = true
                )
                HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))
                AudioStageRow(
                    name = "Headphone DAC (Everest ES8156 @ 0x08)",
                    specs = "3.5mm Stereo Output • Auto-detection ready",
                    active = true
                )
                HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))
                AudioStageRow(
                    name = "Microphone Array (Everest ES7210 @ 0x40/0x43)",
                    specs = "8-channel TDM Beamforming Array",
                    active = true
                )
            }
        }
    }
}

@Composable
fun AudioStageRow(name: String, specs: String, active: Boolean) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(
            imageVector = if (active) Icons.Default.CheckCircle else Icons.Default.Cancel,
            contentDescription = null,
            tint = if (active) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
            modifier = Modifier.size(20.dp)
        )
        Spacer(modifier = Modifier.width(10.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(name, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.SemiBold)
            Text(specs, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}
