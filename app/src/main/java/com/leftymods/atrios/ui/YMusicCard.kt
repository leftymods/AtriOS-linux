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
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.rememberLazyListState
import kotlin.math.absoluteValue
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.pager.PageSize
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.foundation.pager.PagerSnapDistance
import androidx.compose.foundation.pager.PagerDefaults
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.animation.togetherWith
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.fadeOut
import androidx.compose.animation.fadeIn
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.animateColorAsState
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


private fun orbFallbackIcon(orb: YMusicOrb): ImageVector {
    val t = (orb.title + " " + orb.query + " " + orb.id).lowercase()
    return when {
        orb.mode == "likes" || "нравит" in t || "влюблен" in t || "влюблён" in t || "романт" in t || "love" in t -> Icons.Rounded.Favorite
        "радост" in t || "весёл" in t || "весел" in t || "happy" in t -> Icons.Rounded.WbSunny
        "электрон" in t || "techno" in t || "house" in t || "edm" in t || "брейк" in t || "dnb" in t -> Icons.Rounded.Bolt
        "вечерин" in t || "party" in t || "танц" in t -> Icons.Rounded.Nightlife
        "бег" in t || "спорт" in t || "тренир" in t || "workout" in t || "run" in t -> Icons.Rounded.DirectionsRun
        "спокой" in t || "сон" in t || "чил" in t || "calm" in t || "chill" in t || "relax" in t -> Icons.Rounded.Spa
        "рок" in t || "метал" in t || "rock" in t || "metal" in t || "панк" in t -> Icons.Rounded.LocalFireDepartment
        "груст" in t || "печал" in t || "sad" in t -> Icons.Rounded.Cloud
        "бодр" in t || "энерг" in t || "energetic" in t -> Icons.Rounded.FlashOn
        "работ" in t || "фокус" in t || "work" in t || "focus" in t -> Icons.Rounded.Headphones
        "поп" in t || "pop" in t || "хит" in t -> Icons.Rounded.AutoAwesome
        "рэп" in t || "хип" in t || "rap" in t || "hip" in t || "фонк" in t -> Icons.Rounded.GraphicEq
        orb.mode == "chart" || "тренд" in t || "чарт" in t -> Icons.Rounded.TrendingUp
        orb.subtitle == "Артист" -> Icons.Rounded.Person
        else -> Icons.Rounded.Album
    }
}

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
    coverUrl: String = "",
    audioBass: Float = 0f,
    audioMid: Float = 0f,
    audioHigh: Float = 0f,
    audioEnergy: Float = 0f,
    audioBeat: Float = 0f,
    modifier: Modifier = Modifier
) {
    SingleLiquidBlobShaderAura(
        isPlaying = isPlaying,
        palette = palette,
        coverUrl = coverUrl,
        audioBass = audioBass,
        audioMid = audioMid,
        audioHigh = audioHigh,
        audioEnergy = audioEnergy,
        audioBeat = audioBeat,
        modifier = modifier
    )
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
    onOpenVolumeSheet: () -> Unit = {},
    onOpenEqualizer: () -> Unit = {},
    volume: Float = 50f,
    mute: Boolean = false,
    canSwipeNext: Boolean = true,
    canSwipePrev: Boolean = true
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
            YMusicOrb("party", "Вечериниться", "Настроение", "station", "activity:party", "", "#00C896"),
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
            coverUrl = ymusic.coverUrl,
            audioBass = ymusic.audioBass,
            audioMid = ymusic.audioMid,
            audioHigh = ymusic.audioHigh,
            audioEnergy = ymusic.audioEnergy,
            audioBeat = ymusic.audioBeat,
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.92f)
                .align(Alignment.TopCenter)
        )

        if (searchOpen && ymusic.loggedIn) {
            BackHandler { searchOpen = false; searchQuery = ""; onSearch("") }
            val focusRequester = remember { FocusRequester() }
            LaunchedEffect(Unit) {
                delay(80)
                try { focusRequester.requestFocus() } catch (_: Exception) {}
            }
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .background(OledBlack.copy(alpha = 0.97f))
                    .statusBarsPadding()
                    .padding(horizontal = 16.dp, vertical = 10.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                // Sleek Search Bar Header
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    IconButton(
                        onClick = { searchOpen = false; searchQuery = ""; onSearch("") },
                        modifier = Modifier.size(42.dp).clip(CircleShape).background(OledCard)
                    ) {
                        Icon(Icons.Rounded.ArrowBack, contentDescription = "Назад", tint = Color.White)
                    }
                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = { searchQuery = it },
                        placeholder = {
                            Text(
                                "Трек, альбом, артист",
                                color = Color(0xFF7A7A85),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                fontSize = 14.sp
                            )
                        },
                        singleLine = true,
                        shape = RoundedCornerShape(99.dp),
                        modifier = Modifier.weight(1f).focusRequester(focusRequester),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedContainerColor = OledCardHigh,
                            unfocusedContainerColor = OledCard,
                            focusedBorderColor = palette.accent,
                            unfocusedBorderColor = Color(0xFF282830)
                        ),
                        leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null, tint = palette.accent) },
                        trailingIcon = {
                            if (searching) {
                                CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp, color = palette.accent)
                            } else if (searchQuery.isNotEmpty()) {
                                IconButton(onClick = { searchQuery = ""; onSearch("") }) {
                                    Icon(Icons.Rounded.Clear, contentDescription = "Очистить", tint = Color.White)
                                }
                            }
                        }
                    )
                }

                LazyColumn(
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                    contentPadding = PaddingValues(bottom = 24.dp),
                    modifier = Modifier.fillMaxSize()
                ) {
                    if (searchQuery.trim().length < 2) {
                        item {
                            Text("Быстрые подборки и жанры", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                        }
                        item {
                            val quickChips = listOf(
                                "Фонк" to "phonk",
                                "Электроника" to "электронная музыка",
                                "Вечеринка" to "танцевальная вечеринка",
                                "Рок-хиты" to "рок хиты",
                                "Чиллаут" to "chillout lofi",
                                "Для бега" to "музыка для бега",
                                "Поп-чарт" to "популярные хиты",
                                "Брейккор" to "breakcore"
                            )
                            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                quickChips.chunked(2).forEach { pair ->
                                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
                                        pair.forEach { (label, q) ->
                                            Surface(
                                                shape = RoundedCornerShape(16.dp),
                                                color = OledCard,
                                                modifier = Modifier
                                                    .weight(1f)
                                                    .border(1.dp, Color.White.copy(alpha = 0.07f), RoundedCornerShape(16.dp))
                                                    .clickable { searchQuery = q }
                                            ) {
                                                Row(
                                                    modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
                                                    verticalAlignment = Alignment.CenterVertically,
                                                    horizontalArrangement = Arrangement.SpaceBetween
                                                ) {
                                                    Text(label, color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                                                    Icon(Icons.Rounded.NorthEast, contentDescription = null, tint = palette.accent, modifier = Modifier.size(16.dp))
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (searchQuery.trim().length >= 2 && albumResults.isNotEmpty()) {
                        item {
                            Text("Альбомы", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                        }
                        item {
                            LazyRow(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                                items(albumResults, key = { it.id }) { album ->
                                    Surface(
                                        shape = RoundedCornerShape(18.dp),
                                        color = OledCard,
                                        modifier = Modifier
                                            .width(138.dp)
                                            .border(1.dp, Color.White.copy(alpha = 0.07f), RoundedCornerShape(18.dp))
                                            .clickable {
                                                activePreset = album.title
                                                onPlay("album", album.id)
                                                searchOpen = false
                                            }
                                    ) {
                                        Column(Modifier.padding(10.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                            AsyncCoverArt(
                                                url = album.coverUrl,
                                                shape = RoundedCornerShape(14.dp),
                                                modifier = Modifier.fillMaxWidth().aspectRatio(1f),
                                                fallbackIcon = Icons.Rounded.Album
                                            )
                                            Text(album.title, color = Color.White, fontWeight = FontWeight.Bold, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
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
                            Text("Треки", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                        }
                        items(searchResults, key = { it.id }) { track ->
                            val isCurrent = track.id == ymusic.trackId || (track.title == ymusic.title && ymusic.title.isNotBlank())
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(16.dp))
                                    .background(if (isCurrent) palette.accent.copy(alpha = 0.14f) else OledCard)
                                    .border(
                                        1.dp,
                                        if (isCurrent) palette.accent else Color.White.copy(alpha = 0.06f),
                                        RoundedCornerShape(16.dp)
                                    )
                                    .clickable {
                                        activePreset = null
                                        onPlay("track", track.id)
                                        searchOpen = false
                                    }
                                    .padding(horizontal = 12.dp, vertical = 10.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                AsyncCoverArt(
                                    url = track.coverUrl,
                                    shape = RoundedCornerShape(12.dp),
                                    modifier = Modifier.size(50.dp),
                                    fallbackIcon = Icons.Rounded.MusicNote
                                )
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        track.title,
                                        color = if (isCurrent) palette.accent else Color.White,
                                        fontWeight = FontWeight.SemiBold,
                                        fontSize = 14.sp,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
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
                                Box(
                                    modifier = Modifier.size(34.dp).clip(CircleShape).background(palette.accent.copy(alpha = 0.18f)),
                                    contentAlignment = Alignment.Center
                                ) {
                                    Icon(
                                        if (isCurrent && ymusic.state == "playing") Icons.Rounded.GraphicEq else Icons.Rounded.PlayArrow,
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
                        IconButton(onClick = onOpenEqualizer) {
                            Icon(Icons.Rounded.Equalizer, contentDescription = "Эквалайзер", tint = Color.White, modifier = Modifier.size(24.dp))
                        }
                        IconButton(onClick = onOpenVolumeSheet) {
                            Icon(if (mute || volume.toInt() == 0) Icons.Rounded.VolumeOff else Icons.Rounded.VolumeUp, contentDescription = "Громкость колонки", tint = if (mute || volume.toInt() == 0) Color(0xFFFF5C8A) else Color.White, modifier = Modifier.size(24.dp))
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
                            horizontalArrangement = Arrangement.spacedBy(if (palette.isYandexOriginal) 16.dp else 26.dp)
                        ) {
                            IconButton(
                                onClick = { onControl("prev") },
                                modifier = Modifier
                                    .size(46.dp)
                                    .clip(CircleShape)
                                    .background(if (palette.isYandexOriginal) Color(0xCC1A1A1A) else Color.Black.copy(alpha = 0.34f))
                                    .then(if (palette.isYandexOriginal) Modifier.border(1.dp, Color(0xFF333333), CircleShape) else Modifier)
                            ) {
                                Icon(Icons.Rounded.SkipPrevious, contentDescription = "Назад", tint = Color.White, modifier = Modifier.size(26.dp))
                            }

                            if (palette.isYandexOriginal) {
                                // Official Yandex Music VibeBlock pill button (#FFFF00 with black icon/text)
                                Surface(
                                    shape = RoundedCornerShape(99.dp),
                                    color = palette.accent,
                                    modifier = Modifier
                                        .height(58.dp)
                                        .clickable {
                                            when (ymusic.state) {
                                                "playing" -> onControl("pause")
                                                "paused" -> onControl("play")
                                                else -> onPlay("wave", null)
                                            }
                                        }
                                ) {
                                    Row(
                                        modifier = Modifier.padding(horizontal = 28.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                                    ) {
                                        if (ymusic.state == "loading") {
                                            CircularProgressIndicator(modifier = Modifier.size(22.dp), color = palette.onAccent, strokeWidth = 2.5.dp)
                                        } else {
                                            Icon(
                                                if (ymusic.state == "playing") Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                                                contentDescription = "Играть / Пауза",
                                                tint = palette.onAccent,
                                                modifier = Modifier.size(28.dp)
                                            )
                                        }
                                        Text(
                                            if (ymusic.state == "playing") "Пауза" else "Слушать",
                                            color = palette.onAccent,
                                            fontWeight = FontWeight.ExtraBold,
                                            fontSize = 16.sp
                                        )
                                    }
                                }
                            } else {
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
                            }

                            IconButton(
                                onClick = { onControl("next") },
                                modifier = Modifier
                                    .size(46.dp)
                                    .clip(CircleShape)
                                    .background(if (palette.isYandexOriginal) Color(0xCC1A1A1A) else Color.Black.copy(alpha = 0.34f))
                                    .then(if (palette.isYandexOriginal) Modifier.border(1.dp, Color(0xFF333333), CircleShape) else Modifier)
                            ) {
                                Icon(Icons.Rounded.SkipNext, contentDescription = "Вперёд", tint = Color.White, modifier = Modifier.size(26.dp))
                            }
                        }

                        // Synchronous 2-card ribbon track pill (entire pill + cover + text + heart drags, with next/prev pill right behind it!)
                        @Composable
                        fun WavePillCard(title: String, artist: String, coverUrl: String, liked: Boolean, interactive: Boolean) {
                            Surface(
                                shape = RoundedCornerShape(99.dp),
                                color = Color(0xAA1C1C22),
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .then(if (interactive) Modifier.clickable { onOpenBigPlayer() } else Modifier)
                            ) {
                                Row(
                                    modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 10.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                                ) {
                                    if (coverUrl.isNotBlank()) {
                                        AsyncCoverArt(
                                            url = coverUrl,
                                            shape = CircleShape,
                                            modifier = Modifier.size(34.dp),
                                            fallbackIcon = Icons.Rounded.MusicNote
                                        )
                                    }
                                    Text(
                                        if (title.isNotBlank()) {
                                            if (artist.isNotBlank()) "$artist — $title" else title
                                        } else "Нажмите ▶ или свайпните для выбора",
                                        color = Color.White,
                                        fontWeight = FontWeight.SemiBold,
                                        fontSize = 14.sp,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis,
                                        textAlign = if (coverUrl.isBlank()) TextAlign.Center else TextAlign.Start,
                                        modifier = Modifier.weight(1f)
                                    )
                                    IconButton(
                                        onClick = { if (interactive) onControl("like") },
                                        modifier = Modifier.size(30.dp)
                                    ) {
                                        Icon(
                                            if (liked) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                                            contentDescription = "Нравится",
                                            tint = if (liked) Color(0xFFFF2E7A) else Color.White,
                                            modifier = Modifier.size(22.dp)
                                        )
                                    }
                                }
                            }
                        }

                        SwipeableTwoCardRibbon(
                            trackKey = ymusic.trackId,
                            onSwipeNext = { onControl("next") },
                            onSwipePrev = { onControl("prev") },
                            canSwipeNext = canSwipeNext,
                            canSwipePrev = canSwipePrev,
                            modifier = Modifier.fillMaxWidth(0.92f),
                            currentCard = {
                                WavePillCard(ymusic.title, ymusic.artist, ymusic.coverUrl, ymusic.liked, interactive = true)
                            },
                            nextCard = {
                                WavePillCard(
                                    ymusic.nextTitle.ifBlank { "Следующий трек" },
                                    ymusic.nextArtist,
                                    ymusic.nextCoverUrl,
                                    false,
                                    interactive = false
                                )
                            },
                            prevCard = {
                                WavePillCard(
                                    ymusic.prevTitle.ifBlank { "Предыдущий трек" },
                                    ymusic.prevArtist,
                                    ymusic.prevCoverUrl,
                                    false,
                                    interactive = false
                                )
                            }
                        )

                        // Quick Yandex Music Actions: Liked Tracks, My Wave, Chart, Equalizer
                        LazyRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            contentPadding = PaddingValues(horizontal = 4.dp)
                        ) {
                            item {
                                val isLikes = ymusic.mode == "likes"
                                Surface(
                                    shape = RoundedCornerShape(99.dp),
                                    color = if (isLikes) Color(0xFFFF2E7A) else Color(0xB81A1A1F),
                                    modifier = Modifier
                                        .border(1.dp, if (isLikes) Color(0xFFFF2E7A) else Color.White.copy(alpha = 0.14f), RoundedCornerShape(99.dp))
                                        .clickable {
                                            activePreset = "Мне нравится"
                                            onPlay("likes", null)
                                        }
                                ) {
                                    Row(
                                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                                    ) {
                                        Icon(Icons.Rounded.Favorite, contentDescription = null, tint = if (isLikes) Color.White else Color(0xFFFF2E7A), modifier = Modifier.size(16.dp))
                                        Text("Мне нравится", color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 13.sp)
                                    }
                                }
                            }
                            item {
                                val isChart = ymusic.mode == "chart"
                                Surface(
                                    shape = RoundedCornerShape(99.dp),
                                    color = if (isChart) palette.accent else Color(0xB81A1A1F),
                                    modifier = Modifier
                                        .border(1.dp, if (isChart) palette.accent else Color.White.copy(alpha = 0.14f), RoundedCornerShape(99.dp))
                                        .clickable {
                                            activePreset = "Чарт"
                                            onPlay("chart", null)
                                        }
                                ) {
                                    Row(
                                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                                    ) {
                                        Icon(Icons.Rounded.TrendingUp, contentDescription = null, tint = if (isChart) palette.onAccent else palette.accent, modifier = Modifier.size(16.dp))
                                        Text("Чарт", color = if (isChart) palette.onAccent else Color.White, fontWeight = FontWeight.SemiBold, fontSize = 13.sp)
                                    }
                                }
                            }
                            item {
                                Surface(
                                    shape = RoundedCornerShape(99.dp),
                                    color = Color(0xB81A1A1F),
                                    modifier = Modifier
                                        .border(1.dp, Color.White.copy(alpha = 0.14f), RoundedCornerShape(99.dp))
                                        .clickable {
                                            activePreset = "Бодрое"
                                            onPlay("station", "mood:energetic")
                                        }
                                ) {
                                    Row(
                                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                                    ) {
                                        Icon(Icons.Rounded.FlashOn, contentDescription = null, tint = palette.accent, modifier = Modifier.size(16.dp))
                                        Text("Бодрое", color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 13.sp)
                                    }
                                }
                            }
                            item {
                                Surface(
                                    shape = RoundedCornerShape(99.dp),
                                    color = Color(0xB81A1A1F),
                                    modifier = Modifier
                                        .border(1.dp, Color.White.copy(alpha = 0.14f), RoundedCornerShape(99.dp))
                                        .clickable { onOpenEqualizer() }
                                ) {
                                    Row(
                                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                                    ) {
                                        Icon(Icons.Rounded.Equalizer, contentDescription = null, tint = palette.accent, modifier = Modifier.size(16.dp))
                                        Text("EQ", color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 13.sp)
                                    }
                                }
                            }
                        }
                    }
                }

                // Infinite Circular Carousel of Artist & Genre Orbs with strong curved arc & native fling inertia!
                val orbCount = displayOrbs.size.coerceAtLeast(1)
                val startIdx = remember(orbCount) { 5000 - (5000 % orbCount) }
                val orbListState = rememberLazyListState(initialFirstVisibleItemIndex = startIdx)
                LazyRow(
                    state = orbListState,
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    contentPadding = PaddingValues(horizontal = 36.dp),
                    modifier = Modifier.fillMaxWidth().height(172.dp)
                ) {
                    items(count = 10000, key = { it }) { index ->
                        val orb = displayOrbs[((index % orbCount) + orbCount) % orbCount]
                        val layoutInfo = orbListState.layoutInfo
                        val itemInfo = layoutInfo.visibleItemsInfo.firstOrNull { it.index == index }
                        val distFromCenter = if (itemInfo != null && layoutInfo.viewportEndOffset > 0) {
                            val vpCenter = (layoutInfo.viewportStartOffset + layoutInfo.viewportEndOffset) * 0.5f
                            val itemCenter = itemInfo.offset + itemInfo.size * 0.5f
                            ((itemCenter - vpCenter) / (itemInfo.size.coerceAtLeast(1) * 1.1f)).coerceIn(-1.6f, 1.6f)
                        } else 0f
                        val absDist = distFromCenter.absoluteValue
                        val isSelectedOrb = activePreset == orb.title
                        val baseScale = 1.20f - 0.28f * absDist.coerceIn(0f, 1.15f)
                        val scale = if (isSelectedOrb) baseScale + 0.06f else baseScale
                        // Strong circular curvature: pronounced arc drop + tangent wheel tilt + center pop-up
                        val arcDropPx = (74f * absDist * absDist) - (if (isSelectedOrb || absDist < 0.35f) 24f * (1f - absDist.coerceIn(0f, 1f)) else 0f)
                        val wheelTiltDeg = distFromCenter * 13f
                        val alphaVal = (1f - 0.34f * absDist).coerceIn(0.48f, 1f)
                        val orbColor = parseHexColor(orb.colorHex, palette.aura2)

                        Box(
                            modifier = Modifier
                                .width(126.dp)
                                .height(166.dp)
                                .graphicsLayer {
                                    scaleX = scale
                                    scaleY = scale
                                    translationY = arcDropPx
                                    rotationZ = wheelTiltDeg
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
                                    .height(122.dp)
                                    .clip(RoundedCornerShape(topStart = 64.dp, topEnd = 64.dp, bottomStart = 44.dp, bottomEnd = 44.dp))
                                    .then(
                                        if (isSelectedOrb) Modifier.border(
                                            1.5.dp,
                                            palette.accent,
                                            RoundedCornerShape(topStart = 64.dp, topEnd = 64.dp, bottomStart = 44.dp, bottomEnd = 44.dp)
                                        ) else Modifier
                                    )
                                    .background(
                                        Brush.verticalGradient(
                                            listOf(
                                                orbColor.copy(alpha = 0.84f),
                                                orbColor.copy(alpha = 0.30f),
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
                                    url = if (orb.subtitle == "Артист") orb.coverUrl else "",
                                    shape = CircleShape,
                                    modifier = Modifier.size(64.dp),
                                    fallbackIcon = orbFallbackIcon(orb),
                                    monochromeIcon = orb.subtitle != "Артист"
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
