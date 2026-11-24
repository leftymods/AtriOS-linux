package com.leftymods.atrios.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
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
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.painterResource
import com.leftymods.atrios.R
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.leftymods.atrios.model.YMusicAlbum
import com.leftymods.atrios.model.YMusicStatus
import com.leftymods.atrios.model.YMusicTrack
import com.leftymods.atrios.ui.theme.AppPalette
import com.leftymods.atrios.ui.theme.OledCard
import kotlinx.coroutines.delay

@Composable
fun BigPlayerOverlay(
    ymusic: YMusicStatus,
    palette: AppPalette,
    volume: Float,
    mute: Boolean,
    searchResults: List<YMusicTrack> = emptyList(),
    albumResults: List<YMusicAlbum> = emptyList(),
    onSearch: (String) -> Unit = {},
    onPlay: (String, String?) -> Unit = { _, _ -> },
    onVolumeChange: (Float) -> Unit,
    onVolumeCommit: (Int) -> Unit,
    onControl: (String) -> Unit,
    onPlayWave: () -> Unit,
    onDismiss: () -> Unit,
    onOpenEqualizer: () -> Unit = {},
    canSwipeNext: Boolean = true,
    canSwipePrev: Boolean = true
) {
    var artistSheetName by remember { mutableStateOf<String?>(null) }
    var artistLoading by remember { mutableStateOf(false) }

    BackHandler {
        if (artistSheetName != null) {
            artistSheetName = null
        } else {
            onDismiss()
        }
    }

    LaunchedEffect(searchResults, albumResults) {
        artistLoading = false
    }

    var showVolumeRow by remember { mutableStateOf(false) }
    var elapsedSec by remember(ymusic.trackId) { mutableIntStateOf((ymusic.positionMs / 1000L).toInt().coerceAtLeast(0)) }
    LaunchedEffect(ymusic.positionMs) {
        val srvSec = (ymusic.positionMs / 1000L).toInt().coerceAtLeast(0)
        if (kotlin.math.abs(srvSec - elapsedSec) >= 2) {
            elapsedSec = srvSec
        }
    }
    var coverDir by remember { mutableIntStateOf(1) }
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
                        if (totalDrag > 130f && artistSheetName == null) onDismiss()
                        totalDrag = 0f
                    },
                    onVerticalDrag = { _, dragAmount ->
                        if (dragAmount > 0 && artistSheetName == null) totalDrag += dragAmount
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

            // Synchronous 2-Card Ribbon Cover Art (drags freely under finger, ONLY switches when released!)
            SwipeableTwoCardRibbon(
                trackKey = ymusic.trackId,
                onSwipeNext = { coverDir = 1; elapsedSec = 0; onControl("next") },
                onSwipePrev = { coverDir = -1; elapsedSec = 0; onControl("prev") },
                canSwipeNext = canSwipeNext,
                canSwipePrev = canSwipePrev,
                modifier = Modifier
                    .fillMaxWidth(0.88f)
                    .aspectRatio(1f),
                currentCard = {
                    AsyncCoverArt(
                        url = ymusic.coverUrl,
                        shape = RoundedCornerShape(22.dp),
                        modifier = Modifier.fillMaxSize(),
                        fallbackIcon = Icons.Rounded.Album
                    )
                },
                nextCard = {
                    AsyncCoverArt(
                        url = ymusic.nextCoverUrl.ifBlank { ymusic.coverUrl },
                        shape = RoundedCornerShape(22.dp),
                        modifier = Modifier.fillMaxSize(),
                        fallbackIcon = Icons.Rounded.Album
                    )
                },
                prevCard = {
                    AsyncCoverArt(
                        url = ymusic.prevCoverUrl.ifBlank { ymusic.coverUrl },
                        shape = RoundedCornerShape(22.dp),
                        modifier = Modifier.fillMaxSize(),
                        fallbackIcon = Icons.Rounded.Album
                    )
                }
            )

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
                            val primaryArtist = artist.split(",").firstOrNull()?.trim().orEmpty()
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(2.dp),
                                modifier = Modifier
                                    .clip(RoundedCornerShape(8.dp))
                                    .clickable(enabled = primaryArtist.isNotBlank()) {
                                        artistSheetName = primaryArtist
                                        artistLoading = true
                                        onSearch(primaryArtist)
                                    }
                                    .padding(vertical = 2.dp)
                            ) {
                                Text(
                                    artist.ifBlank { "Яндекс Музыка" },
                                    color = if (primaryArtist.isNotBlank()) palette.accent.copy(alpha = 0.92f) else Color.White.copy(alpha = 0.68f),
                                    fontSize = 14.sp,
                                    fontWeight = FontWeight.Medium,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                if (primaryArtist.isNotBlank()) {
                                    Icon(
                                        Icons.Rounded.ChevronRight,
                                        contentDescription = "Исполнитель",
                                        tint = palette.accent.copy(alpha = 0.85f),
                                        modifier = Modifier.size(16.dp)
                                    )
                                }
                            }
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
                        Icon(painterResource(R.drawable.ic_ym_dislike), contentDescription = "Не нравится", tint = Color.White.copy(alpha = 0.78f), modifier = Modifier.size(24.dp))
                    }
                    IconButton(onClick = { coverDir = -1; elapsedSec = 0; onControl("prev") }) {
                        Icon(painterResource(R.drawable.ic_ym_prev), contentDescription = "Предыдущий", tint = Color.White, modifier = Modifier.size(30.dp))
                    }
                    Box(
                        modifier = Modifier
                            .size(76.dp)
                            .clip(CircleShape)
                            .background(palette.accent)
                            .clickable {
                                when (ymusic.state) {
                                    "playing", "loading" -> onControl("pause")
                                    "paused" -> onControl("play")
                                    else -> onPlayWave()
                                }
                            },
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            painterResource(if (ymusic.state == "playing") R.drawable.ic_ym_pause else R.drawable.ic_ym_play),
                            contentDescription = "Плей / Пауза",
                            tint = palette.onAccent,
                            modifier = Modifier.size(34.dp)
                        )
                    }
                    IconButton(onClick = { coverDir = 1; elapsedSec = 0; onControl("next") }) {
                        Icon(painterResource(R.drawable.ic_ym_next), contentDescription = "Следующий", tint = Color.White, modifier = Modifier.size(30.dp))
                    }
                    IconButton(onClick = { onControl("like") }) {
                        Icon(
                            painterResource(if (ymusic.liked) R.drawable.ic_ym_liked else R.drawable.ic_ym_like),
                            contentDescription = "Нравится",
                            tint = if (ymusic.liked) Color(0xFFFF2E7A) else Color.White,
                            modifier = Modifier.size(24.dp)
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
                    Surface(
                        shape = RoundedCornerShape(99.dp),
                        color = if (ymusic.repeat) palette.accent.copy(alpha = 0.24f) else Color.White.copy(alpha = 0.10f),
                        modifier = Modifier.width(96.dp).height(44.dp).clickable { onControl("repeat") }
                    ) {
                        Box(contentAlignment = Alignment.Center) {
                            Icon(
                                if (ymusic.repeat) Icons.Rounded.RepeatOne else Icons.Rounded.Repeat,
                                contentDescription = "Повтор трека",
                                tint = if (ymusic.repeat) palette.accent else Color.White.copy(alpha = 0.85f)
                            )
                        }
                    }
                    Surface(shape = RoundedCornerShape(99.dp), color = Color.White.copy(alpha = 0.10f), modifier = Modifier.width(74.dp).height(44.dp).clickable { onOpenEqualizer() }) {
                        Box(contentAlignment = Alignment.Center) { Icon(Icons.Rounded.Equalizer, contentDescription = "Эквалайзер", tint = Color.White.copy(alpha = 0.85f)) }
                    }
                    Surface(shape = RoundedCornerShape(99.dp), color = Color.White.copy(alpha = 0.10f), modifier = Modifier.width(74.dp).height(44.dp).clickable { showVolumeRow = !showVolumeRow }) {
                        Box(contentAlignment = Alignment.Center) { Icon(if (mute || volume.toInt() == 0) Icons.Rounded.VolumeOff else Icons.Rounded.VolumeUp, contentDescription = "Звук", tint = if (showVolumeRow) palette.accent else Color.White.copy(alpha = 0.85f)) }
                    }
                    Surface(shape = RoundedCornerShape(99.dp), color = Color.White.copy(alpha = 0.10f), modifier = Modifier.width(74.dp).height(44.dp).clickable { onControl("stop"); onDismiss() }) {
                        Box(contentAlignment = Alignment.Center) { Icon(Icons.Rounded.Stop, contentDescription = "Стоп", tint = Color.White.copy(alpha = 0.85f)) }
                    }
                }
            }
        }

        // Artist Card Overlay right on top of Big Player!
        AnimatedVisibility(
            visible = artistSheetName != null,
            enter = slideInVertically(initialOffsetY = { it }) + fadeIn(tween(220)),
            exit = slideOutVertically(targetOffsetY = { it }) + fadeOut(tween(180)),
            modifier = Modifier.align(Alignment.BottomCenter)
        ) {
            val aName = artistSheetName.orEmpty()
            val heroCover = searchResults.firstOrNull()?.coverUrl?.ifBlank { ymusic.coverUrl } ?: ymusic.coverUrl
            Surface(
                shape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp),
                color = Color(0xF5111116),
                modifier = Modifier
                    .fillMaxWidth()
                    .fillMaxHeight(0.84f)
                    .border(1.dp, Color.White.copy(alpha = 0.08f), RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp))
            ) {
                Column(
                    modifier = Modifier.fillMaxSize().padding(horizontal = 18.dp, vertical = 14.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    // Header with artist photo, name, Play All, Close
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        AsyncCoverArt(
                            url = heroCover,
                            shape = CircleShape,
                            modifier = Modifier.size(56.dp),
                            fallbackIcon = Icons.Rounded.Person
                        )
                        Column(modifier = Modifier.weight(1f)) {
                            Text("Исполнитель", color = palette.accent, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
                            Text(aName, color = Color.White, fontWeight = FontWeight.Bold, fontSize = 20.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        }
                        FilledTonalButton(
                            onClick = {
                                onPlay("search", aName)
                                artistSheetName = null
                            },
                            shape = RoundedCornerShape(99.dp),
                            contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp)
                        ) {
                            Icon(Icons.Rounded.PlayArrow, contentDescription = null, modifier = Modifier.size(18.dp))
                            Spacer(Modifier.width(4.dp))
                            Text("Микс", fontSize = 13.sp, fontWeight = FontWeight.Bold)
                        }
                        IconButton(
                            onClick = { artistSheetName = null },
                            modifier = Modifier.size(36.dp).clip(CircleShape).background(Color.White.copy(alpha = 0.10f))
                        ) {
                            Icon(Icons.Rounded.Close, contentDescription = "Закрыть", tint = Color.White, modifier = Modifier.size(18.dp))
                        }
                    }

                    if (artistLoading && searchResults.isEmpty() && albumResults.isEmpty()) {
                        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                            CircularProgressIndicator(color = palette.accent)
                        }
                    } else {
                        LazyColumn(
                            verticalArrangement = Arrangement.spacedBy(10.dp),
                            contentPadding = PaddingValues(bottom = 18.dp),
                            modifier = Modifier.fillMaxSize()
                        ) {
                            if (albumResults.isNotEmpty()) {
                                item {
                                    Text("Альбомы", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                                }
                                item {
                                    LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                                        items(albumResults, key = { it.id }) { album ->
                                            Surface(
                                                shape = RoundedCornerShape(16.dp),
                                                color = OledCard,
                                                modifier = Modifier
                                                    .width(126.dp)
                                                    .border(1.dp, Color.White.copy(alpha = 0.07f), RoundedCornerShape(16.dp))
                                                    .clickable {
                                                        onPlay("album", album.id)
                                                        artistSheetName = null
                                                    }
                                            ) {
                                                Column(Modifier.padding(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                                                    AsyncCoverArt(
                                                        url = album.coverUrl,
                                                        shape = RoundedCornerShape(12.dp),
                                                        modifier = Modifier.fillMaxWidth().aspectRatio(1f),
                                                        fallbackIcon = Icons.Rounded.Album
                                                    )
                                                    Text(album.title, color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                                    if (album.year > 0) {
                                                        Text("${album.year}", color = Color.White.copy(alpha = 0.6f), fontSize = 11.sp)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            if (searchResults.isNotEmpty()) {
                                item {
                                    Text("Треки исполнителя", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                                }
                                items(searchResults, key = { it.id }) { track ->
                                    val isCur = track.id == ymusic.trackId || (track.title == ymusic.title && ymusic.title.isNotBlank())
                                    Row(
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .clip(RoundedCornerShape(14.dp))
                                            .background(if (isCur) palette.accent.copy(alpha = 0.14f) else OledCard)
                                            .border(1.dp, if (isCur) palette.accent else Color.White.copy(alpha = 0.06f), RoundedCornerShape(14.dp))
                                            .clickable {
                                                onPlay("track", track.id)
                                                artistSheetName = null
                                            }
                                            .padding(horizontal = 10.dp, vertical = 8.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                                    ) {
                                        AsyncCoverArt(
                                            url = track.coverUrl,
                                            shape = RoundedCornerShape(10.dp),
                                            modifier = Modifier.size(44.dp),
                                            fallbackIcon = Icons.Rounded.MusicNote
                                        )
                                        Column(modifier = Modifier.weight(1f)) {
                                            Text(track.title, color = if (isCur) palette.accent else Color.White, fontWeight = FontWeight.SemiBold, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                            Text(track.artist, color = Color.White.copy(alpha = 0.65f), fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                        }
                                        Icon(
                                            if (isCur && ymusic.state == "playing") Icons.Rounded.GraphicEq else Icons.Rounded.PlayArrow,
                                            contentDescription = null,
                                            tint = palette.accent,
                                            modifier = Modifier.size(20.dp)
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
