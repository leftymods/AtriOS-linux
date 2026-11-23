package com.leftymods.atrios.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.leftymods.atrios.model.YMusicStatus
import com.leftymods.atrios.ui.theme.AppPalette
import kotlinx.coroutines.delay

@Composable
fun BigPlayerOverlay(
    ymusic: YMusicStatus,
    palette: AppPalette,
    volume: Float,
    mute: Boolean,
    onVolumeChange: (Float) -> Unit,
    onVolumeCommit: (Int) -> Unit,
    onControl: (String) -> Unit,
    onPlayWave: () -> Unit,
    onDismiss: () -> Unit
) {
    BackHandler(onBack = onDismiss)

    var showVolumeRow by remember { mutableStateOf(false) }
    var elapsedSec by remember(ymusic.trackId) { mutableIntStateOf(0) }
    var coverDir by remember { mutableIntStateOf(1) }
    var coverDragX by remember { mutableFloatStateOf(0f) }
    val animCoverX by animateFloatAsState(
        targetValue = coverDragX,
        animationSpec = spring(dampingRatio = 0.82f, stiffness = 360f),
        label = "bigCoverDrag"
    )
    val totalSec = remember(ymusic.durationMs) { (ymusic.durationMs / 1000L).toInt().coerceAtLeast(1) }

    LaunchedEffect(ymusic.trackId, ymusic.state) {
        while (ymusic.state == "playing" && elapsedSec < totalSec) {
            delay(1000)
            elapsedSec += 1
        }
    }

    val coverColors = CoverArtCache.getColors(ymusic.coverUrl)
    val bgTop = (coverColors?.c2 ?: palette.aura2).copy(alpha = 0.24f)
    val bgMid = Color(0xFF14110E)
    val bgBot = Color(0xFF050506)

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Brush.verticalGradient(listOf(bgTop, bgMid, bgBot)))
            .background(Color.Black.copy(alpha = 0.72f))
            .pointerInput(Unit) {
                var totalDrag = 0f
                detectVerticalDragGestures(
                    onDragEnd = {
                        if (totalDrag > 130f) onDismiss()
                        totalDrag = 0f
                    },
                    onVerticalDrag = { _, dragAmount ->
                        if (dragAmount > 0) totalDrag += dragAmount
                    }
                )
            }
            .statusBarsPadding()
            .padding(horizontal = 22.dp, vertical = 12.dp)
    ) {
        Column(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.SpaceBetween,
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            // Top bar
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(onClick = onDismiss) {
                    Icon(Icons.Rounded.KeyboardArrowDown, contentDescription = "Свернуть", tint = Color.White, modifier = Modifier.size(30.dp))
                }
                Text(
                    when (ymusic.mode) {
                        "likes" -> "Мне нравится"
                        "chart" -> "Чарт"
                        "album" -> ymusic.album.ifBlank { "Альбом" }
                        else -> "Моя волна"
                    },
                    color = Color.White,
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 15.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                IconButton(onClick = { showVolumeRow = !showVolumeRow }) {
                    Icon(
                        if (mute || volume.toInt() == 0) Icons.Rounded.VolumeOff else Icons.Rounded.VolumeUp,
                        contentDescription = "Громкость",
                        tint = if (showVolumeRow) palette.accent else Color.White
                    )
                }
            }

            // Swipeable Large Cover Art (swipes all the way off, then next/prev slides cleanly in!)
            Box(
                modifier = Modifier
                    .fillMaxWidth(0.88f)
                    .aspectRatio(1f)
                    .pointerInput(ymusic.trackId) {
                        detectHorizontalDragGestures(
                            onDragEnd = {
                                if (coverDragX < -95f) {
                                    coverDir = 1
                                    elapsedSec = 0
                                    onControl("next")
                                } else if (coverDragX > 95f) {
                                    coverDir = -1
                                    elapsedSec = 0
                                    onControl("prev")
                                }
                                coverDragX = 0f
                            },
                            onDragCancel = { coverDragX = 0f },
                            onHorizontalDrag = { _, amt ->
                                coverDragX = (coverDragX + amt).coerceIn(-360f, 360f)
                            }
                        )
                    }
            ) {
                AnimatedContent(
                    targetState = ymusic.trackId to ymusic.coverUrl,
                    transitionSpec = {
                        (slideInHorizontally(spring(dampingRatio = 0.86f, stiffness = 300f)) { it * coverDir } + fadeIn(tween(220)))
                            .togetherWith(
                                slideOutHorizontally(spring(dampingRatio = 0.86f, stiffness = 300f)) { -it * coverDir } + fadeOut(tween(180))
                            )
                    },
                    label = "bigCoverCarousel"
                ) { (_, covUrl) ->
                    AsyncCoverArt(
                        url = covUrl,
                        shape = RoundedCornerShape(22.dp),
                        modifier = Modifier
                            .fillMaxSize()
                            .graphicsLayer {
                                translationX = animCoverX
                                alpha = (1f - kotlin.math.abs(animCoverX) / 520f).coerceIn(0.35f, 1f)
                            },
                        fallbackIcon = Icons.Rounded.Album
                    )
                }
            }

            // Track info + Progress + Controls
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                AnimatedContent(
                    targetState = Triple(ymusic.trackId, ymusic.title, ymusic.artist to ymusic.coverUrl),
                    transitionSpec = {
                        (slideInHorizontally(tween(240)) { it / 2 * coverDir } + fadeIn(tween(220)))
                            .togetherWith(slideOutHorizontally(tween(180)) { -it / 2 * coverDir } + fadeOut(tween(160)))
                    },
                    label = "bigTrackMeta"
                ) { (_, title, artistCov) ->
                    val (artist, covUrl) = artistCov
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        AsyncCoverArt(
                            url = covUrl,
                            shape = CircleShape,
                            modifier = Modifier.size(44.dp),
                            fallbackIcon = Icons.Rounded.MusicNote
                        )
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                title.ifBlank { "Трек не выбран" },
                                color = Color.White,
                                fontWeight = FontWeight.Bold,
                                fontSize = 20.sp,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                            Text(
                                artist.ifBlank { "Яндекс Музыка" },
                                color = Color.White.copy(alpha = 0.68f),
                                fontSize = 14.sp,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                }

                Column(modifier = Modifier.fillMaxWidth()) {
                    LinearProgressIndicator(
                        progress = { (elapsedSec.toFloat() / totalSec.toFloat()).coerceIn(0f, 1f) },
                        modifier = Modifier.fillMaxWidth().height(4.dp).clip(RoundedCornerShape(99.dp)),
                        color = Color.White,
                        trackColor = Color.White.copy(alpha = 0.22f)
                    )
                    Spacer(Modifier.height(6.dp))
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("%d:%02d".format(elapsedSec / 60, elapsedSec % 60), color = Color.White.copy(alpha = 0.68f), fontSize = 12.sp)
                        Text("%d:%02d".format(totalSec / 60, totalSec % 60), color = Color.White.copy(alpha = 0.68f), fontSize = 12.sp)
                    }
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    IconButton(onClick = { onControl("dislike") }) {
                        Icon(Icons.Rounded.HeartBroken, contentDescription = "Не нравится", tint = Color.White.copy(alpha = 0.78f), modifier = Modifier.size(26.dp))
                    }
                    IconButton(onClick = { coverDir = -1; elapsedSec = 0; onControl("prev") }) {
                        Icon(Icons.Rounded.SkipPrevious, contentDescription = "Предыдущий", tint = Color.White, modifier = Modifier.size(36.dp))
                    }
                    Box(
                        modifier = Modifier
                            .size(76.dp)
                            .clip(CircleShape)
                            .background(palette.accent)
                            .clickable {
                                when (ymusic.state) {
                                    "playing" -> onControl("pause")
                                    "paused" -> onControl("play")
                                    else -> onPlayWave()
                                }
                            },
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            if (ymusic.state == "playing") Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                            contentDescription = "Плей / Пауза",
                            tint = palette.onAccent,
                            modifier = Modifier.size(40.dp)
                        )
                    }
                    IconButton(onClick = { coverDir = 1; elapsedSec = 0; onControl("next") }) {
                        Icon(Icons.Rounded.SkipNext, contentDescription = "Следующий", tint = Color.White, modifier = Modifier.size(36.dp))
                    }
                    IconButton(onClick = { onControl("like") }) {
                        Icon(
                            if (ymusic.liked) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                            contentDescription = "Нравится",
                            tint = if (ymusic.liked) Color(0xFFFF2E7A) else Color.White,
                            modifier = Modifier.size(26.dp)
                        )
                    }
                }

                if (showVolumeRow) {
                    Surface(shape = RoundedCornerShape(18.dp), color = Color.White.copy(alpha = 0.10f)) {
                        Row(
                            modifier = Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 6.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Icon(if (mute || volume.toInt() == 0) Icons.Rounded.VolumeOff else Icons.Rounded.VolumeDown, contentDescription = null, tint = Color.White, modifier = Modifier.size(20.dp))
                            Slider(
                                value = volume,
                                onValueChange = onVolumeChange,
                                onValueChangeFinished = { onVolumeCommit(volume.toInt()) },
                                valueRange = 0f..100f,
                                modifier = Modifier.weight(1f),
                                colors = SliderDefaults.colors(thumbColor = palette.accent, activeTrackColor = palette.accent)
                            )
                            Text("${volume.toInt()}%", color = Color.White, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                        }
                    }
                }

                Row(
                    modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Surface(shape = RoundedCornerShape(99.dp), color = Color.White.copy(alpha = 0.10f), modifier = Modifier.width(96.dp).height(44.dp).clickable { onPlayWave() }) {
                        Box(contentAlignment = Alignment.Center) { Icon(Icons.Rounded.Repeat, contentDescription = "Волна", tint = Color.White.copy(alpha = 0.85f)) }
                    }
                    Surface(shape = RoundedCornerShape(99.dp), color = Color.White.copy(alpha = 0.10f), modifier = Modifier.width(96.dp).height(44.dp).clickable { showVolumeRow = !showVolumeRow }) {
                        Box(contentAlignment = Alignment.Center) { Icon(if (mute || volume.toInt() == 0) Icons.Rounded.VolumeOff else Icons.Rounded.VolumeUp, contentDescription = "Звук", tint = if (showVolumeRow) palette.accent else Color.White.copy(alpha = 0.85f)) }
                    }
                    Surface(shape = RoundedCornerShape(99.dp), color = Color.White.copy(alpha = 0.10f), modifier = Modifier.width(96.dp).height(44.dp).clickable { onControl("stop"); onDismiss() }) {
                        Box(contentAlignment = Alignment.Center) { Icon(Icons.Rounded.Stop, contentDescription = "Стоп", tint = Color.White.copy(alpha = 0.85f)) }
                    }
                }
            }
        }
    }
}
