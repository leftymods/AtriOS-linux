package com.leftymods.atrios.ui

import android.annotation.SuppressLint
import androidx.activity.compose.BackHandler
import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.webkit.CookieManager
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import kotlin.math.absoluteValue
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.pager.PageSize
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Login
import androidx.compose.material.icons.automirrored.rounded.Logout
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.leftymods.atrios.model.YMusicAlbum
import com.leftymods.atrios.model.YMusicOrb
import com.leftymods.atrios.model.YMusicStatus
import com.leftymods.atrios.model.YMusicTrack
import com.leftymods.atrios.ui.theme.AppPalette
import com.leftymods.atrios.ui.theme.OledBlack
import com.leftymods.atrios.ui.theme.OledCard
import com.leftymods.atrios.ui.theme.OledCardHigh
import kotlinx.coroutines.delay
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

private fun parseHexColor(hex: String, fallback: Color): Color {
    return try {
        Color(android.graphics.Color.parseColor(hex))
    } catch (_: Exception) {
        fallback
    }
}

@Composable
fun MyWaveAnimatedAura(
    isPlaying: Boolean,
    palette: AppPalette,
    modifier: Modifier = Modifier
) {
    val inf = rememberInfiniteTransition(label = "waveAura")
    // Single master phase 0..2*PI with integer harmonics -> mathematically 100% seamless loop!
    val t by inf.animateFloat(
        initialValue = 0f,
        targetValue = (2f * PI).toFloat(),
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = if (isPlaying) 9000 else 15000, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "masterPhase"
    )

    Canvas(modifier = modifier) {
        val cx = size.width * 0.5f
        val cy = size.height * 0.43f
        val breathe = 1f + 0.08f * sin(t * 2f)
        val r = size.minDimension * 0.64f * breathe

        // Layer 1: Wide soft ambient nebula (slow orbit, 1x harmonic)
        val c1 = Offset(cx + cos(t) * size.width * 0.14f, cy + sin(t) * size.height * 0.08f)
        drawCircle(
            brush = Brush.radialGradient(
                colorStops = arrayOf(
                    0.0f to palette.aura2.copy(alpha = 0.72f),
                    0.45f to palette.aura3.copy(alpha = 0.36f),
                    0.82f to palette.aura2.copy(alpha = 0.10f),
                    1.0f to Color.Transparent
                ),
                center = c1,
                radius = r * 1.48f
            ),
            radius = r * 1.48f,
            center = c1
        )

        // Layer 2: Organic figure-8 magenta lobe (1x / 2x Lissajous -> smooth & seamless)
        val c2 = Offset(cx + sin(t) * size.width * 0.22f, cy + sin(t * 2f) * size.height * 0.11f)
        drawCircle(
            brush = Brush.radialGradient(
                colorStops = arrayOf(
                    0.0f to palette.aura3.copy(alpha = 0.90f),
                    0.42f to palette.aura3.copy(alpha = 0.48f),
                    0.78f to palette.aura3.copy(alpha = 0.12f),
                    1.0f to Color.Transparent
                ),
                center = c2,
                radius = r * 1.12f
            ),
            radius = r * 1.12f,
            center = c2
        )

        // Layer 3: Swirling golden-yellow solar plasma (counter-phase 1x / 2x)
        val c3 = Offset(cx - cos(t) * size.width * 0.19f, cy - sin(t * 2f + 0.8f) * size.height * 0.10f)
        drawCircle(
            brush = Brush.radialGradient(
                colorStops = arrayOf(
                    0.0f to palette.aura1.copy(alpha = 0.96f),
                    0.40f to palette.aura2.copy(alpha = 0.62f),
                    0.76f to palette.aura1.copy(alpha = 0.14f),
                    1.0f to Color.Transparent
                ),
                center = c3,
                radius = r * 1.05f
            ),
            radius = r * 1.05f,
            center = c3
        )

        // Layer 4: Luminous inner core (2x / 1x gentle shimmer)
        val c4 = Offset(cx + cos(t * 2f) * size.width * 0.09f, cy - sin(t) * size.height * 0.08f)
        drawCircle(
            brush = Brush.radialGradient(
                colorStops = arrayOf(
                    0.0f to Color.White.copy(alpha = 0.30f),
                    0.35f to palette.aura1.copy(alpha = 0.68f),
                    0.75f to palette.aura2.copy(alpha = 0.15f),
                    1.0f to Color.Transparent
                ),
                center = c4,
                radius = r * 0.68f
            ),
            radius = r * 0.68f,
            center = c4
        )
    }
}

@Composable
fun YandexWaveFullScreen(
    ymusic: YMusicStatus,
    palette: AppPalette,
    orbs: List<YMusicOrb>,
    searchResults: List<YMusicTrack>,
    albumResults: List<YMusicAlbum>,
    onRefresh: () -> Unit,
    onLogin: (String) -> Unit,
    onPlay: (String, String?) -> Unit,
    onSearch: (String) -> Unit,
    onControl: (String) -> Unit,
    onOpenAccountSettings: () -> Unit,
    onOpenBigPlayer: () -> Unit = {},
    onOpenVolumeSheet: () -> Unit = {}
) {
    var searchOpen by remember { mutableStateOf(false) }
    var searchQuery by remember { mutableStateOf("") }
    var searching by remember { mutableStateOf(false) }
    var activePreset by remember { mutableStateOf<String?>(null) }
    var showOAuthDialog by remember { mutableStateOf(false) }

    LaunchedEffect(searchQuery, ymusic.loggedIn) {
        if (!ymusic.loggedIn) return@LaunchedEffect
        val q = searchQuery.trim()
        if (q.length < 2) {
            searching = false
            if (q.isEmpty()) onSearch("")
            return@LaunchedEffect
        }
        searching = true
        delay(280)
        onSearch(q)
    }

    LaunchedEffect(searchResults, albumResults) {
        searching = false
    }

    val displayOrbs = remember(orbs) {
        if (orbs.isNotEmpty()) orbs else listOf(
            YMusicOrb("likes", "Мне нравится", "Коллекция", "likes", "", "", "#FF1F8E"),
            YMusicOrb("chart", "Тренды", "Чарт", "chart", "", "", "#FF4D00"),
            YMusicOrb("party", "Вечериниться", "Настроение", "station", "mood:party", "", "#00C896"),
            YMusicOrb("run", "Бегаю и не устаю", "Занятие", "station", "activity:workout", "", "#4C7CFF"),
            YMusicOrb("calm", "Спокойное", "Настроение", "station", "mood:calm", "", "#9B6BFF")
        )
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(OledBlack)
    ) {
        // Full-bleed animated Yandex Music aura spanning top 70% of the screen!
        MyWaveAnimatedAura(
            isPlaying = ymusic.state == "playing",
            palette = palette,
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.74f)
                .align(Alignment.TopCenter)
        )

        if (searchOpen && ymusic.loggedIn) {
            BackHandler { searchOpen = false; searchQuery = ""; onSearch("") }
            // Clean full-screen Search Overlay (Tracks + Albums with cover art)
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .background(OledBlack.copy(alpha = 0.96f))
                    .statusBarsPadding()
                    .padding(horizontal = 16.dp, vertical = 10.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    IconButton(onClick = { searchOpen = false; searchQuery = ""; onSearch("") }) {
                        Icon(Icons.Rounded.ArrowBack, contentDescription = "Назад", tint = Color.White)
                    }
                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = { searchQuery = it },
                        placeholder = { Text("Трек, альбом или исполнитель...") },
                        singleLine = true,
                        shape = RoundedCornerShape(99.dp),
                        modifier = Modifier.weight(1f),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedContainerColor = OledCardHigh,
                            unfocusedContainerColor = OledCard,
                            focusedBorderColor = palette.accent,
                            unfocusedBorderColor = Color(0xFF28282E)
                        ),
                        trailingIcon = {
                            if (searching) {
                                CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp, color = palette.accent)
                            } else if (searchQuery.isNotEmpty()) {
                                IconButton(onClick = { searchQuery = ""; onSearch("") }) {
                                    Icon(Icons.Rounded.Clear, contentDescription = "Очистить")
                                }
                            }
                        }
                    )
                }

                LazyColumn(
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                    modifier = Modifier.fillMaxSize()
                ) {
                    if (searchQuery.trim().length >= 2 && albumResults.isNotEmpty()) {
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
                                            .width(124.dp)
                                            .clickable {
                                                activePreset = album.title
                                                onPlay("album", album.id)
                                                searchOpen = false
                                            }
                                    ) {
                                        Column(Modifier.padding(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                                            AsyncCoverArt(
                                                url = album.coverUrl,
                                                shape = RoundedCornerShape(12.dp),
                                                modifier = Modifier.fillMaxWidth().aspectRatio(1f),
                                                fallbackIcon = Icons.Rounded.Album
                                            )
                                            Text(album.title, color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                            Text(
                                                buildString {
                                                    append(album.artist)
                                                    if (album.year > 0) { if (isNotEmpty()) append(" · "); append(album.year) }
                                                },
                                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                                fontSize = 11.sp,
                                                maxLines = 1,
                                                overflow = TextOverflow.Ellipsis
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (searchQuery.trim().length >= 2 && searchResults.isNotEmpty()) {
                        item {
                            Text("Треки", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                        }
                        items(searchResults, key = { it.id }) { track ->
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(14.dp))
                                    .background(OledCard)
                                    .clickable {
                                        activePreset = null
                                        onPlay("track", track.id)
                                        searchOpen = false
                                    }
                                    .padding(horizontal = 12.dp, vertical = 9.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                AsyncCoverArt(
                                    url = track.coverUrl,
                                    shape = RoundedCornerShape(10.dp),
                                    modifier = Modifier.size(48.dp),
                                    fallbackIcon = Icons.Rounded.MusicNote
                                )
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(track.title, color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    Text(
                                        buildString {
                                            append(track.artist)
                                            if (track.album.isNotBlank()) { if (isNotEmpty()) append(" · "); append(track.album) }
                                        },
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        fontSize = 12.sp,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                                if (track.durationMs > 0) {
                                    Text(formatDurationMs(track.durationMs), color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 12.sp)
                                }
                                Icon(Icons.Rounded.PlayArrow, contentDescription = null, tint = palette.accent, modifier = Modifier.size(22.dp))
                            }
                        }
                    }
                }
            }
        } else {
            // Main full-screen Yandex Music Wave layout (fills 100% height cleanly!)
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .statusBarsPadding()
                    .padding(top = 6.dp, bottom = 6.dp),
                verticalArrangement = Arrangement.SpaceBetween,
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                // Top Header: Avatar | Яндекс * Музыка | Search
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    if (ymusic.loggedIn) {
                        AsyncCoverArt(
                            url = ymusic.avatarUrl,
                            shape = CircleShape,
                            modifier = Modifier
                                .size(36.dp)
                                .clickable { onOpenAccountSettings() },
                            fallbackIcon = Icons.Rounded.Person
                        )
                    } else {
                        Box(
                            modifier = Modifier
                                .size(36.dp)
                                .clip(CircleShape)
                                .background(Color.White.copy(alpha = 0.14f))
                                .clickable { showOAuthDialog = true },
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(Icons.Rounded.Person, contentDescription = null, tint = Color.White, modifier = Modifier.size(20.dp))
                        }
                    }

                    if (activePreset != null && ymusic.loggedIn) {
                        Surface(
                            shape = RoundedCornerShape(99.dp),
                            color = Color.Black.copy(alpha = 0.55f),
                            modifier = Modifier.clickable {
                                activePreset = null
                                onPlay("wave", null)
                            }
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 14.dp, vertical = 6.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp)
                            ) {
                                Text(activePreset!!, color = palette.accent, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                                Icon(Icons.Rounded.Cancel, contentDescription = "Сбросить", tint = palette.accent, modifier = Modifier.size(18.dp))
                            }
                        }
                    } else {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            modifier = Modifier.clickable { onRefresh() }
                        ) {
                            Text("Яндекс", color = palette.accent, fontWeight = FontWeight.SemiBold, fontSize = 18.sp)
                            Icon(Icons.Rounded.AutoAwesome, contentDescription = null, tint = palette.accent, modifier = Modifier.size(19.dp))
                            Text("Музыка", color = palette.accent, fontWeight = FontWeight.ExtraBold, fontSize = 18.sp)
                        }
                    }

                    Row(verticalAlignment = Alignment.CenterVertically) {
                        IconButton(onClick = onOpenVolumeSheet) {
                            Icon(Icons.Rounded.VolumeUp, contentDescription = "Громкость колонки", tint = Color.White, modifier = Modifier.size(24.dp))
                        }
                        IconButton(
                            onClick = { if (ymusic.loggedIn) searchOpen = true else showOAuthDialog = true }
                        ) {
                            Icon(Icons.Rounded.Search, contentDescription = "Поиск", tint = Color.White, modifier = Modifier.size(26.dp))
                        }
                    }
                }

                // Center Hero: "Моя волна" + Big Accent Play Circle + Frosted Track Pill + AI Subtitle
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 18.dp)
                ) {
                    Text(
                        "Моя волна",
                        color = Color.White,
                        fontSize = 44.sp,
                        fontWeight = FontWeight.Black,
                        letterSpacing = (-1.2).sp
                    )

                    if (!ymusic.loggedIn) {
                        Button(
                            onClick = { showOAuthDialog = true },
                            shape = RoundedCornerShape(99.dp),
                            colors = ButtonDefaults.buttonColors(containerColor = palette.accent, contentColor = palette.onAccent),
                            modifier = Modifier.height(54.dp).padding(horizontal = 16.dp)
                        ) {
                            Icon(Icons.AutoMirrored.Rounded.Login, contentDescription = null)
                            Spacer(Modifier.width(8.dp))
                            Text("Войти в Яндекс Музыку", fontWeight = FontWeight.Bold, fontSize = 15.sp)
                        }
                    } else {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(26.dp)
                        ) {
                            IconButton(
                                onClick = { onControl("prev") },
                                modifier = Modifier
                                    .size(46.dp)
                                    .clip(CircleShape)
                                    .background(Color.Black.copy(alpha = 0.34f))
                            ) {
                                Icon(Icons.Rounded.SkipPrevious, contentDescription = "Назад", tint = Color.White, modifier = Modifier.size(28.dp))
                            }

                            Box(
                                modifier = Modifier
                                    .size(86.dp)
                                    .clip(CircleShape)
                                    .background(palette.accent)
                                    .clickable {
                                        when (ymusic.state) {
                                            "playing" -> onControl("pause")
                                            "paused" -> onControl("play")
                                            else -> onPlay("wave", null)
                                        }
                                    },
                                contentAlignment = Alignment.Center
                            ) {
                                if (ymusic.state == "loading") {
                                    CircularProgressIndicator(modifier = Modifier.size(32.dp), color = palette.onAccent, strokeWidth = 3.dp)
                                } else {
                                    Icon(
                                        if (ymusic.state == "playing") Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                                        contentDescription = "Играть / Пауза",
                                        tint = palette.onAccent,
                                        modifier = Modifier.size(44.dp)
                                    )
                                }
                            }

                            IconButton(
                                onClick = { onControl("next") },
                                modifier = Modifier
                                    .size(46.dp)
                                    .clip(CircleShape)
                                    .background(Color.Black.copy(alpha = 0.34f))
                            ) {
                                Icon(Icons.Rounded.SkipNext, contentDescription = "Вперёд", tint = Color.White, modifier = Modifier.size(28.dp))
                            }
                        }

                        // Frosted track pill (like Yandex Music screenshot!)
                        Surface(
                            shape = RoundedCornerShape(99.dp),
                            color = Color(0x991C1C22),
                            modifier = Modifier
                                .fillMaxWidth(0.92f)
                                .clickable { onOpenBigPlayer() }
                        ) {
                            Row(
                                modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 10.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(10.dp)
                            ) {
                                if (ymusic.coverUrl.isNotBlank()) {
                                    AsyncCoverArt(
                                        url = ymusic.coverUrl,
                                        shape = CircleShape,
                                        modifier = Modifier.size(34.dp),
                                        fallbackIcon = Icons.Rounded.MusicNote
                                    )
                                }
                                Text(
                                    if (ymusic.title.isNotBlank()) {
                                        if (ymusic.artist.isNotBlank()) "${ymusic.artist} — ${ymusic.title}" else ymusic.title
                                    } else "Нажмите ▶ для запуска на колонке",
                                    color = Color.White,
                                    fontWeight = FontWeight.SemiBold,
                                    fontSize = 14.sp,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                    textAlign = if (ymusic.coverUrl.isBlank()) TextAlign.Center else TextAlign.Start,
                                    modifier = Modifier.weight(1f)
                                )
                                IconButton(
                                    onClick = { onControl("like") },
                                    modifier = Modifier.size(30.dp)
                                ) {
                                    Icon(
                                        if (ymusic.liked) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                                        contentDescription = "Нравится",
                                        tint = if (ymusic.liked) Color(0xFFFF2E7A) else Color.White,
                                        modifier = Modifier.size(22.dp)
                                    )
                                }
                            }
                        }

                        // AI Caption line (like screenshot: "✨ AI  ...")
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            modifier = Modifier.padding(horizontal = 16.dp)
                        ) {
                            Icon(Icons.Rounded.AutoAwesome, contentDescription = null, tint = Color.White.copy(alpha = 0.85f), modifier = Modifier.size(18.dp))
                            Text(
                                when {
                                    ymusic.album.isNotBlank() -> "Альбом «${ymusic.album}» · играет на колонке AtriOS"
                                    ymusic.state == "playing" -> "Персональный поток подобран по вашим лайкам"
                                    else -> "Доступно и с телефона, и голосом на колонке"
                                },
                                color = Color.White.copy(alpha = 0.82f),
                                fontSize = 13.sp,
                                textAlign = TextAlign.Center,
                                maxLines = 2,
                                lineHeight = 17.sp
                            )
                        }
                    }
                }

                // Infinite Circular Carousel of Artist & Genre Orbs (loops endlessly with curved arc!)
                val orbCount = displayOrbs.size.coerceAtLeast(1)
                val startPage = remember(orbCount) { (Int.MAX_VALUE / 2) - ((Int.MAX_VALUE / 2) % orbCount) }
                val orbPagerState = rememberPagerState(initialPage = startPage, pageCount = { Int.MAX_VALUE })
                HorizontalPager(
                    state = orbPagerState,
                    pageSize = PageSize.Fixed(128.dp),
                    pageSpacing = 12.dp,
                    contentPadding = PaddingValues(horizontal = 112.dp),
                    modifier = Modifier.fillMaxWidth().height(156.dp)
                ) { page ->
                    val orb = displayOrbs[((page % orbCount) + orbCount) % orbCount]
                    val pageOffset = ((orbPagerState.currentPage - page) + orbPagerState.currentPageOffsetFraction).absoluteValue.coerceIn(0f, 1.5f)
                    val scale = 1.06f - 0.14f * pageOffset.coerceIn(0f, 1f)
                    val arcDropPx = 22f * pageOffset * pageOffset
                    val alphaVal = (1f - 0.28f * pageOffset).coerceIn(0.60f, 1f)
                    val orbColor = parseHexColor(orb.colorHex, palette.aura2)

                    Box(
                        modifier = Modifier
                            .width(128.dp)
                            .height(148.dp)
                            .graphicsLayer {
                                scaleX = scale
                                scaleY = scale
                                translationY = arcDropPx
                                alpha = alphaVal
                            }
                            .clickable {
                                activePreset = orb.title
                                onPlay(orb.mode, orb.query.ifBlank { null })
                            },
                        contentAlignment = Alignment.BottomCenter
                    ) {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(114.dp)
                                .clip(RoundedCornerShape(topStart = 60.dp, topEnd = 60.dp, bottomStart = 20.dp, bottomEnd = 20.dp))
                                .background(
                                    Brush.verticalGradient(
                                        listOf(
                                            orbColor.copy(alpha = 0.80f),
                                            orbColor.copy(alpha = 0.28f),
                                            Color.Transparent
                                        )
                                    )
                                )
                        )
                        Column(
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(4.dp),
                            modifier = Modifier.fillMaxSize().padding(horizontal = 8.dp, vertical = 4.dp)
                        ) {
                            AsyncCoverArt(
                                url = orb.coverUrl,
                                shape = CircleShape,
                                modifier = Modifier.size(64.dp),
                                fallbackIcon = when (orb.mode) {
                                    "likes" -> Icons.Rounded.Favorite
                                    "chart" -> Icons.Rounded.Bolt
                                    else -> Icons.Rounded.GraphicEq
                                }
                            )
                            if (orb.subtitle.isNotBlank()) {
                                Text(
                                    orb.subtitle,
                                    color = Color.White.copy(alpha = 0.72f),
                                    fontSize = 11.sp,
                                    maxLines = 1
                                )
                            }
                            Text(
                                orb.title,
                                color = Color.White,
                                fontWeight = FontWeight.Bold,
                                fontSize = 13.sp,
                                textAlign = TextAlign.Center,
                                maxLines = 2,
                                lineHeight = 15.sp,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                }
            }
        }
    }

    if (showOAuthDialog) {
        YandexOAuthWebViewDialog(
            onTokenCaptured = { tok ->
                showOAuthDialog = false
                onLogin(tok)
            },
            onDismiss = { showOAuthDialog = false }
        )
    }
}

@Composable
fun YandexAccountCard(
    ymusic: YMusicStatus,
    onLogin: (String) -> Unit,
    onLogout: () -> Unit,
    onRefresh: () -> Unit
) {
    val context = LocalContext.current
    val clipboard = LocalClipboardManager.current
    var tokenInput by remember { mutableStateOf("") }
    var showOAuthDialog by remember { mutableStateOf(false) }
    var showManualToken by remember { mutableStateOf(false) }

    Surface(shape = RoundedCornerShape(24.dp), color = OledCard) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(14.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                AsyncCoverArt(
                    url = ymusic.avatarUrl,
                    shape = CircleShape,
                    modifier = Modifier.size(56.dp),
                    fallbackIcon = Icons.Rounded.Person
                )
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        if (ymusic.loggedIn) ymusic.displayName.ifBlank { ymusic.login.ifBlank { "Яндекс Аккаунт" } }
                        else "Не выполнен вход",
                        color = Color.White,
                        fontWeight = FontWeight.Bold,
                        fontSize = 17.sp
                    )
                    Text(
                        if (ymusic.loggedIn) "Яндекс Плюс подключён · UID ${ymusic.uid}" else "Подключите Яндекс Музыку",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                IconButton(onClick = onRefresh) {
                    Icon(Icons.Rounded.Refresh, contentDescription = "Обновить")
                }
            }

            if (ymusic.loggedIn) {
                OutlinedButton(
                    onClick = onLogout,
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(99.dp)
                ) {
                    Icon(Icons.AutoMirrored.Rounded.Logout, contentDescription = null, tint = MaterialTheme.colorScheme.error)
                    Spacer(Modifier.width(8.dp))
                    Text("Выйти из аккаунта", color = MaterialTheme.colorScheme.error)
                }
            } else {
                Button(
                    onClick = { showOAuthDialog = true },
                    modifier = Modifier.fillMaxWidth().height(50.dp),
                    shape = RoundedCornerShape(99.dp)
                ) {
                    Icon(Icons.AutoMirrored.Rounded.Login, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Войти через Яндекс", fontWeight = FontWeight.Bold)
                }
                TextButton(onClick = { showManualToken = !showManualToken }) {
                    Text(if (showManualToken) "Скрыть ручной ввод" else "Ввести OAuth-токен вручную")
                }
                if (showManualToken) {
                    OutlinedButton(
                        onClick = { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(YMUSIC_OAUTH_URL))) },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(14.dp)
                    ) {
                        Icon(Icons.Rounded.OpenInBrowser, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text("Открыть OAuth в браузере")
                    }
                    OutlinedTextField(
                        value = tokenInput,
                        onValueChange = { tokenInput = it },
                        label = { Text("OAuth-токен (y0_...)") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(14.dp),
                        trailingIcon = {
                            IconButton(onClick = {
                                val txt = clipboard.getText()?.text.orEmpty()
                                tokenInput = extractYMusicToken(txt) ?: txt.trim()
                            }) {
                                Icon(Icons.Rounded.ContentPaste, contentDescription = "Вставить")
                            }
                        }
                    )
                    Button(
                        onClick = {
                            val tok = extractYMusicToken(tokenInput) ?: tokenInput.trim()
                            if (tok.isNotBlank()) {
                                onLogin(tok)
                                tokenInput = ""
                            }
                        },
                        enabled = tokenInput.trim().length >= 10,
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(99.dp)
                    ) {
                        Text("Сохранить токен")
                    }
                }
            }
        }
    }

    if (showOAuthDialog) {
        YandexOAuthWebViewDialog(
            onTokenCaptured = { tok ->
                showOAuthDialog = false
                onLogin(tok)
            },
            onDismiss = { showOAuthDialog = false }
        )
    }
}

private fun formatDurationMs(ms: Long): String {
    val totalSec = (ms / 1000).coerceAtLeast(0)
    return "%d:%02d".format(totalSec / 60, totalSec % 60)
}

private const val YMUSIC_OAUTH_URL =
    "https://oauth.yandex.ru/authorize?response_type=token&client_id=23cabbbdc6cd418abb4b39c32c41195d"

private fun extractYMusicToken(raw: String): String? {
    val trimmed = raw.trim()
    if (trimmed.isEmpty()) return null
    Regex("""access_token=([^&\s]+)""").find(trimmed)?.groupValues?.getOrNull(1)?.let {
        if (it.length >= 10) return it
    }
    val cleaned = trimmed.removePrefix("OAuth ").trim()
    return if (cleaned.length >= 10 && !cleaned.contains(' ') && !cleaned.startsWith("http")) cleaned else null
}

@SuppressLint("SetJavaScriptEnabled")
@Composable
private fun YandexOAuthWebViewDialog(
    onTokenCaptured: (String) -> Unit,
    onDismiss: () -> Unit
) {
    val delivered = remember { AtomicBoolean(false) }
    var loading by remember { mutableStateOf(true) }

    fun maybeCapture(url: String?): Boolean {
        if (url.isNullOrBlank()) return false
        val tok = extractYMusicToken(url) ?: return false
        if (delivered.compareAndSet(false, true)) {
            onTokenCaptured(tok)
        }
        return true
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier.fillMaxSize().padding(12.dp),
            shape = RoundedCornerShape(24.dp),
            color = OledCard
        ) {
            Column(Modifier.fillMaxSize()) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 10.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Вход в Яндекс Музыку", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                    IconButton(onClick = onDismiss) { Icon(Icons.Rounded.Close, contentDescription = "Закрыть") }
                }
                if (loading) {
                    LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                }
                AndroidView(
                    modifier = Modifier.fillMaxSize(),
                    factory = { ctx ->
                        WebView(ctx).apply {
                            settings.javaScriptEnabled = true
                            settings.domStorageEnabled = true
                            CookieManager.getInstance().setAcceptCookie(true)
                            CookieManager.getInstance().setAcceptThirdPartyCookies(this, true)
                            webViewClient = object : WebViewClient() {
                                override fun shouldOverrideUrlLoading(view: WebView?, request: WebResourceRequest?): Boolean {
                                    return maybeCapture(request?.url?.toString())
                                }
                                override fun onPageStarted(view: WebView?, url: String?, favicon: Bitmap?) {
                                    loading = true
                                    maybeCapture(url)
                                }
                                override fun onPageFinished(view: WebView?, url: String?) {
                                    loading = false
                                    maybeCapture(url)
                                }
                            }
                            loadUrl(YMUSIC_OAUTH_URL)
                        }
                    }
                )
            }
        }
    }
}
