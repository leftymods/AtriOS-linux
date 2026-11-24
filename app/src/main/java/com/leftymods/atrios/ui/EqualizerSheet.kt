package com.leftymods.atrios.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Equalizer
import androidx.compose.material.icons.rounded.RestartAlt
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.leftymods.atrios.model.EqStatus
import com.leftymods.atrios.ui.theme.AppPalette
import com.leftymods.atrios.ui.theme.OledCard

private data class EqPresetDef(val id: String, val label: String, val bands: List<Float>)

private val EQ_PRESETS = listOf(
    EqPresetDef("flat", "Плоский", listOf(0f, 0f, 0f, 0f, 0f)),
    EqPresetDef("bass", "Бас+", listOf(6.5f, 3.5f, 0f, 1.0f, 2.0f)),
    EqPresetDef("vocal", "Вокал", listOf(-2.0f, 1.5f, 4.5f, 3.0f, 0.5f)),
    EqPresetDef("rock", "Рок", listOf(4.5f, 1.5f, -1.0f, 3.0f, 4.5f)),
    EqPresetDef("electronic", "Электро", listOf(5.5f, 2.0f, -0.5f, 2.5f, 5.0f)),
    EqPresetDef("night", "Ночной", listOf(-4.5f, -1.5f, 2.0f, 1.5f, -3.0f))
)

private val BAND_LABELS = listOf("60 Гц", "230 Гц", "910 Гц", "3.6 кГц", "14 кГц")

@Composable
fun EqualizerPanel(
    eq: EqStatus,
    palette: AppPalette,
    onApplyEq: (enabled: Boolean, preset: String, bands: List<Float>) -> Unit
) {
    var enabled by remember { mutableStateOf(eq.enabled) }
    var preset by remember { mutableStateOf(eq.preset) }
    var bands by remember { mutableStateOf(if (eq.bands.size == 5) eq.bands else listOf(0f, 0f, 0f, 0f, 0f)) }
    val lastLiveSent = remember { java.util.concurrent.atomic.AtomicLong(0L) }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 18.dp, vertical = 8.dp)
            .padding(bottom = 26.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Icon(Icons.Rounded.Equalizer, contentDescription = null, tint = palette.accent, modifier = Modifier.size(26.dp))
                Column {
                    Text("Эквалайзер колонки", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 18.sp)
                    Text("5-полосный живой DSP (-12 .. +12 дБ)", color = Color(0xFF9A9AA6), fontSize = 12.sp)
                }
            }
            Switch(
                checked = enabled,
                onCheckedChange = {
                    enabled = it
                    onApplyEq(enabled, preset, bands)
                },
                colors = SwitchDefaults.colors(checkedThumbColor = palette.onAccent, checkedTrackColor = palette.accent)
            )
        }

        // Quick Presets Row
        LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(EQ_PRESETS, key = { it.id }) { p ->
                val active = preset == p.id && enabled
                Surface(
                    shape = RoundedCornerShape(99.dp),
                    color = if (active) palette.accent else OledCard,
                    modifier = Modifier
                        .border(1.dp, if (active) palette.accent else Color(0xFF33333A), RoundedCornerShape(99.dp))
                        .clickable {
                            enabled = true
                            preset = p.id
                            bands = p.bands
                            onApplyEq(true, p.id, p.bands)
                        }
                ) {
                    Text(
                        p.label,
                        color = if (active) palette.onAccent else Color.White,
                        fontWeight = FontWeight.SemiBold,
                        fontSize = 13.sp,
                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp)
                    )
                }
            }
        }

        // 5 Full-Control Frequency Band Sliders
        Surface(
            shape = RoundedCornerShape(20.dp),
            color = OledCard,
            modifier = Modifier.fillMaxWidth().border(1.dp, Color(0xFF282830), RoundedCornerShape(20.dp))
        ) {
            Column(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                BAND_LABELS.forEachIndexed { idx, label ->
                    val db = bands[idx]
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        Text(
                            label,
                            color = Color(0xFFD0D0DA),
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Medium,
                            modifier = Modifier.width(52.dp)
                        )
                        Slider(
                            value = db,
                            onValueChange = { v ->
                                val rounded = (v * 2f).toInt() / 2f
                                val updated = bands.toMutableList().apply { this[idx] = rounded }
                                bands = updated
                                enabled = true
                                preset = "custom"
                                val now = System.currentTimeMillis()
                                if (now - lastLiveSent.get() >= 180L) {
                                    lastLiveSent.set(now)
                                    onApplyEq(true, "custom", updated)
                                }
                            },
                            onValueChangeFinished = {
                                lastLiveSent.set(System.currentTimeMillis())
                                onApplyEq(enabled, preset, bands)
                            },
                            valueRange = -12f..12f,
                            modifier = Modifier.weight(1f),
                            colors = SliderDefaults.colors(
                                thumbColor = if (enabled) palette.accent else Color(0xFF666670),
                                activeTrackColor = if (enabled) palette.accent else Color(0xFF555560),
                                inactiveTrackColor = Color(0xFF2A2A32)
                            )
                        )
                        Text(
                            "%+.1f".format(db),
                            color = if (enabled && db != 0f) palette.accent else Color(0xFF8A8A96),
                            fontSize = 12.sp,
                            fontWeight = FontWeight.SemiBold,
                            modifier = Modifier.width(40.dp)
                        )
                    }
                }
            }
        }

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
            TextButton(
                onClick = {
                    val flat = listOf(0f, 0f, 0f, 0f, 0f)
                    bands = flat
                    preset = "flat"
                    onApplyEq(enabled, "flat", flat)
                }
            ) {
                Icon(Icons.Rounded.RestartAlt, contentDescription = null, modifier = Modifier.size(18.dp))
                Spacer(Modifier.width(4.dp))
                Text("Сбросить в 0 дБ")
            }
        }
    }
}
