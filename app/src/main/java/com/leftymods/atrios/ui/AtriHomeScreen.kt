package com.leftymods.atrios.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.leftymods.atrios.AtriAccountState
import com.leftymods.atrios.model.SpeakerStatus
import com.leftymods.atrios.ui.theme.AppPalette
import com.leftymods.atrios.ui.theme.OledBlack
import com.leftymods.atrios.ui.theme.OledCard
import com.leftymods.atrios.ui.theme.OledCardHigh

@Composable
fun AtriHomeScreen(
    status: SpeakerStatus,
    speakerName: String,
    transportLabel: String,
    accountState: AtriAccountState,
    palette: AppPalette,
    volumePct: Int,
    progressFrac: Float = 0f,
    onBlobTap: () -> Unit,
    onPrev: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onOpenVolume: () -> Unit,
    onOpenEq: () -> Unit,
    onOpenIntegrations: () -> Unit,
    onOpenTimers: () -> Unit
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val ym = status.ymusic
    val isPlaying = ym.state == "playing"
    val isLoading = ym.state == "loading"
    val hasTrack = ym.title.isNotBlank()
    val accountDisplay = if (accountState.username.contains("@")) accountState.username else "@${accountState.username}"

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(OledBlack)
            .statusBarsPadding()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp, vertical = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(11.dp)
    ) {
        // Top status bar (no duplicate EQ/Volume buttons — those live in the 2x2 Quick Action grid below)
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = speakerName.ifBlank { "AtriStation" },
                    color = Color.White,
                    fontSize = 22.sp,
                    fontWeight = FontWeight.Bold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(Color(0xFF4ADE80))
                    )
                    Text(
                        text = buildString {
                            append(transportLabel)
                            append(" · Подключена")
                            if (accountState.loggedIn) append(" · $accountDisplay")
                        },
                        color = Color(0xFFA0A0B0),
                        fontSize = 12.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
        }

        // Hero GPU Blob — Living Atri Core & Audio Visualizer
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(220.dp)
                .clip(RoundedCornerShape(30.dp))
                .clickable { onBlobTap() },
            contentAlignment = Alignment.Center
        ) {
            SingleLiquidBlobShaderAura(
                isPlaying = isPlaying || isLoading,
                palette = palette,
                coverUrl = ym.coverUrl,
                audioBass = ym.audioBass,
                audioMid = ym.audioMid,
                audioHigh = ym.audioHigh,
                audioEnergy = ym.audioEnergy,
                audioBeat = ym.audioBeat,
                modifier = Modifier.fillMaxSize()
            )
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                if (isLoading) {
                    CircularProgressIndicator(
                        color = Color.White,
                        strokeWidth = 3.dp,
                        modifier = Modifier.size(34.dp)
                    )
                } else {
                    Icon(
                        imageVector = if (isPlaying) Icons.Rounded.GraphicEq else Icons.Rounded.Mic,
                        contentDescription = null,
                        tint = Color.White,
                        modifier = Modifier.size(36.dp)
                    )
                }
                Text(
                    text = when {
                        isLoading -> "Загрузка..."
                        isPlaying -> "Играет"
                        ym.loggedIn -> "Нажмите для Моей волны"
                        else -> "Атри готова"
                    },
                    color = Color.White,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = if (isPlaying) "Тап — пауза" else "Управление музыкой на колонке",
                    color = Color.White.copy(alpha = 0.72f),
                    fontSize = 11.sp
                )
            }
        }

        // Compact Now Playing status card with live progress bar + 1-tap launch official Yandex Music
        if (hasTrack) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(22.dp))
                    .background(OledCard)
                    .border(1.dp, Color.White.copy(alpha = 0.06f), RoundedCornerShape(22.dp))
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            runCatching {
                                context.packageManager.getLaunchIntentForPackage("ru.yandex.music")?.let {
                                    context.startActivity(it)
                                }
                            }
                        }
                        .padding(horizontal = 12.dp, vertical = 10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    AsyncCoverArt(
                        url = ym.coverUrl,
                        shape = RoundedCornerShape(12.dp),
                        modifier = Modifier.size(46.dp)
                    )
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = ym.title,
                            color = Color.White,
                            fontSize = 15.sp,
                            fontWeight = FontWeight.SemiBold,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            text = ym.artist.ifBlank { if (ym.loggedIn) "Яндекс Музыка" else "AtriStation" },
                            color = Color(0xFFA0A0B0),
                            fontSize = 12.sp,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        IconButton(
                            onClick = onPrev,
                            enabled = ym.canPrev,
                            modifier = Modifier.size(36.dp)
                        ) {
                            Icon(
                                Icons.Rounded.SkipPrevious,
                                contentDescription = "Назад",
                                tint = if (ym.canPrev) Color.White else Color.White.copy(alpha = 0.25f)
                            )
                        }
                        IconButton(
                            onClick = onPlayPause,
                            modifier = Modifier
                                .size(40.dp)
                                .clip(CircleShape)
                                .background(palette.accent)
                        ) {
                            Icon(
                                if (isPlaying) Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                                contentDescription = "Пуск/Пауза",
                                tint = palette.onAccent
                            )
                        }
                        IconButton(
                            onClick = onNext,
                            enabled = ym.canNext && !isLoading,
                            modifier = Modifier.size(36.dp)
                        ) {
                            Icon(
                                Icons.Rounded.SkipNext,
                                contentDescription = "Вперёд",
                                tint = if (ym.canNext && !isLoading) Color.White else Color.White.copy(alpha = 0.25f)
                            )
                        }
                    }
                }
                if (ym.durationMs > 0L) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(2.5.dp)
                            .background(Color.White.copy(alpha = 0.08f))
                    ) {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth(progressFrac.coerceIn(0f, 1f))
                                .fillMaxHeight()
                                .background(palette.accent)
                        )
                    }
                }
            }
        }

        // Quick Action Tiles
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            QuickTile(
                icon = Icons.Rounded.VolumeUp,
                title = "Громкость",
                subtitle = "$volumePct%",
                accent = palette.accent,
                modifier = Modifier.weight(1f),
                onClick = onOpenVolume
            )
            QuickTile(
                icon = Icons.Rounded.Equalizer,
                title = "Эквалайзер",
                subtitle = "5 полос",
                accent = palette.accent,
                modifier = Modifier.weight(1f),
                onClick = onOpenEq
            )
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            QuickTile(
                icon = Icons.Rounded.MusicNote,
                title = "Яндекс Музыка",
                subtitle = if (ym.loggedIn) "Подключена" else "Настроить",
                accent = if (ym.loggedIn) Color(0xFF4ADE80) else palette.accent,
                modifier = Modifier.weight(1f),
                onClick = onOpenIntegrations
            )
            QuickTile(
                icon = Icons.Rounded.Timer,
                title = "Таймеры",
                subtitle = "Будильники и таймеры",
                accent = palette.accent,
                modifier = Modifier.weight(1f),
                onClick = onOpenTimers
            )
        }

        Spacer(Modifier.height(16.dp))
    }
}

@Composable
private fun QuickTile(
    icon: ImageVector,
    title: String,
    subtitle: String,
    accent: Color,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(20.dp))
            .background(OledCard)
            .border(1.dp, Color.White.copy(alpha = 0.06f), RoundedCornerShape(20.dp))
            .clickable { onClick() }
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        Box(
            modifier = Modifier
                .size(32.dp)
                .clip(RoundedCornerShape(10.dp))
                .background(OledCardHigh),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, contentDescription = null, tint = accent, modifier = Modifier.size(18.dp))
        }
        Text(title, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
        Text(subtitle, color = Color(0xFFA0A0B0), fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
