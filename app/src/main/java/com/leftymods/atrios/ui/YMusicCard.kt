package com.leftymods.atrios.ui

import android.annotation.SuppressLint
import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.webkit.CookieManager
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
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
import androidx.compose.ui.graphics.vector.ImageVector
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
import com.leftymods.atrios.model.YMusicStatus
import com.leftymods.atrios.model.YMusicTrack
import com.leftymods.atrios.ui.theme.OledBlack
import com.leftymods.atrios.ui.theme.OledCard
import com.leftymods.atrios.ui.theme.OledCardHigh
import com.leftymods.atrios.ui.theme.YandexYellow
import kotlinx.coroutines.delay
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

private data class WavePreset(
    val title: String,
    val mode: String,
    val query: String?,
    val icon: ImageVector,
    val colorTop: Color,
    val colorBottom: Color
)

private val WAVE_PRESETS = listOf(
    WavePreset("Моя волна", "wave", null, Icons.Rounded.AutoAwesome, Color(0xFFFFD600), Color(0xFF4A2800)),
    WavePreset("Мне нравится", "likes", null, Icons.Rounded.Favorite, Color(0xFFFF2E7A), Color(0xFF4A0020)),
    WavePreset("Тренды", "chart", null, Icons.Rounded.Bolt, Color(0xFFFF4081), Color(0xFF3E0028)),
    WavePreset("Вечериниться", "station", "mood:party", Icons.Rounded.LocalBar, Color(0xFF00E5B0), Color(0xFF003B30)),
    WavePreset("Бегаю и не устаю", "station", "activity:workout", Icons.Rounded.DirectionsRun, Color(0xFF6B8CFF), Color(0xFF141E4A)),
    WavePreset("Бодрое", "station", "mood:energetic", Icons.Rounded.Whatshot, Color(0xFFFF6D00), Color(0xFF421800)),
    WavePreset("Спокойное", "station", "mood:calm", Icons.Rounded.SelfImprovement, Color(0xFF9B6BFF), Color(0xFF24104A))
)

@Composable
fun MyWaveAnimatedAura(
    isPlaying: Boolean,
    modifier: Modifier = Modifier
) {
    val inf = rememberInfiniteTransition(label = "waveAura")
    val phase1 by inf.animateFloat(
        initialValue = 0f,
        targetValue = (2f * PI).toFloat(),
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = if (isPlaying) 5200 else 9000, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "p1"
    )
    val phase2 by inf.animateFloat(
        initialValue = 0f,
        targetValue = (2f * PI).toFloat(),
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = if (isPlaying) 7400 else 12500, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "p2"
    )
    val pulse by inf.animateFloat(
        initialValue = 0.88f,
        targetValue = 1.14f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = if (isPlaying) 1900 else 3600, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "pulse"
    )

    Canvas(modifier = modifier) {
        val cx = size.width * 0.5f
        val cy = size.height * 0.46f
        val baseR = size.minDimension * 0.56f * pulse

        // Deep fiery red-orange outer glow
        val cRed = Offset(
            cx + cos(phase2 * 0.8f) * size.width * 0.14f,
            cy + sin(phase1 * 0.7f) * size.height * 0.10f
        )
        drawCircle(
            brush = Brush.radialGradient(
                colors = listOf(Color(0xDDFD3B00), Color(0x66C71558), Color.Transparent),
                center = cRed,
                radius = baseR * 1.25f
            ),
            radius = baseR * 1.25f,
            center = cRed
        )

        // Hot magenta-pink lobe (like Yandex Music wave right side)
        val cPink = Offset(
            cx + cos(phase1 + 0.6f) * size.width * 0.22f,
            cy + sin(phase2 + 1.1f) * size.height * 0.16f
        )
        drawCircle(
            brush = Brush.radialGradient(
                colors = listOf(Color(0xEEFF1C96), Color(0x88FF0066), Color.Transparent),
                center = cPink,
                radius = baseR * 0.96f
            ),
            radius = baseR * 0.96f,
            center = cPink
        )

        // Bright golden yellow-amber plasma core (left/center swirling lobe)
        val cYellow = Offset(
            cx - cos(phase1 * 1.1f) * size.width * 0.18f,
            cy - sin(phase2 * 0.9f) * size.height * 0.14f
        )
        drawCircle(
            brush = Brush.radialGradient(
                colors = listOf(Color(0xFFFFD800), Color(0xCCFF8C00), Color.Transparent),
                center = cYellow,
                radius = baseR * 0.92f
            ),
            radius = baseR * 0.92f,
            center = cYellow
        )

        // Secondary warm solar highlight
        val cGold = Offset(
            cx + sin(phase2 * 1.3f) * size.width * 0.11f,
            cy - cos(phase1 * 1.2f) * size.height * 0.11f
        )
        drawCircle(
            brush = Brush.radialGradient(
                colors = listOf(Color(0xDDFFEE33), Color(0x44FF5500), Color.Transparent),
                center = cGold,
                radius = baseR * 0.62f
            ),
            radius = baseR * 0.62f,
            center = cGold
        )
    }
}

@Composable
fun YMusicCard(
    ymusic: YMusicStatus,
    searchResults: List<YMusicTrack> = emptyList(),
    albumResults: List<YMusicAlbum> = emptyList(),
    onRefresh: () -> Unit,
    onLogin: (String) -> Unit,
    onLogout: () -> Unit,
    onPlay: (String, String?) -> Unit,
    onSearch: (String) -> Unit = {},
    onControl: (String) -> Unit
) {
    val context = LocalContext.current
    val clipboard = LocalClipboardManager.current
    var tokenInput by remember { mutableStateOf("") }
    var searchQuery by remember { mutableStateOf("") }
    var searchOpen by remember { mutableStateOf(false) }
    var searching by remember { mutableStateOf(false) }
    var showOAuthDialog by remember { mutableStateOf(false) }
    var showManualToken by remember { mutableStateOf(false) }
    var activePreset by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(searchQuery, ymusic.loggedIn) {
        if (!ymusic.loggedIn) return@LaunchedEffect
        val q = searchQuery.trim()
        if (q.length < 2) {
            searching = false
            if (q.isEmpty()) onSearch("")
            return@LaunchedEffect
        }
        searching = true
        delay(300)
        onSearch(q)
    }

    LaunchedEffect(searchResults, albumResults) {
        searching = false
    }

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Yandex Music top header row (Avatar | Яндекс * Музыка | Search)
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 4.dp, vertical = 2.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                if (ymusic.loggedIn) {
                    AsyncCoverArt(
                        url = ymusic.avatarUrl,
                        shape = CircleShape,
                        modifier = Modifier
                            .size(34.dp)
                            .clickable { onRefresh() },
                        fallbackIcon = Icons.Rounded.Person
                    )
                } else {
                    Box(
                        modifier = Modifier
                            .size(34.dp)
                            .clip(CircleShape)
                            .background(Color(0xFF1F1F24)),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(Icons.Rounded.Add, contentDescription = null, tint = Color.White, modifier = Modifier.size(20.dp))
                    }
                }
            }

            // Center title pill (or active mood pill)
            if (activePreset != null && ymusic.loggedIn) {
                Surface(
                    shape = RoundedCornerShape(99.dp),
                    color = Color(0xFF262010),
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
                        Text(activePreset!!, color = YandexYellow, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                        Icon(Icons.Rounded.Cancel, contentDescription = "Сбросить", tint = YandexYellow, modifier = Modifier.size(18.dp))
                    }
                }
            } else {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text("Яндекс", color = YandexYellow, fontWeight = FontWeight.SemiBold, fontSize = 17.sp)
                    Icon(Icons.Rounded.AutoAwesome, contentDescription = null, tint = YandexYellow, modifier = Modifier.size(18.dp))
                    Text("Музыка", color = YandexYellow, fontWeight = FontWeight.ExtraBold, fontSize = 17.sp)
                }
            }

            Row(verticalAlignment = Alignment.CenterVertically) {
                if (ymusic.loggedIn) {
                    IconButton(onClick = {
                        searchOpen = !searchOpen
                        if (!searchOpen) {
                            searchQuery = ""
                            onSearch("")
                        }
                    }) {
                        Icon(
                            if (searchOpen) Icons.Rounded.Close else Icons.Rounded.Search,
                            contentDescription = "Поиск",
                            tint = Color.White
                        )
                    }
                } else {
                    Spacer(Modifier.size(40.dp))
                }
            }
        }

        if (!ymusic.loggedIn) {
            // Login card in OLED style
            Surface(
                shape = RoundedCornerShape(26.dp),
                color = OledCard
            ) {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(22.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Box(
                        modifier = Modifier.fillMaxWidth().height(160.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        MyWaveAnimatedAura(isPlaying = false, modifier = Modifier.fillMaxSize())
                        Text(
                            "Моя волна",
                            color = Color.White,
                            fontSize = 32.sp,
                            fontWeight = FontWeight.Black
                        )
                    }
                    Text(
                        "Войдите в Яндекс Плюс — музыка будет работать и с телефона, и голосом напрямую на колонке.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        textAlign = TextAlign.Center
                    )
                    Button(
                        onClick = { showOAuthDialog = true },
                        modifier = Modifier.fillMaxWidth().height(52.dp),
                        shape = RoundedCornerShape(99.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = YandexYellow,
                            contentColor = Color.Black
                        )
                    ) {
                        Icon(Icons.AutoMirrored.Rounded.Login, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text("Войти через Яндекс", fontWeight = FontWeight.Bold, fontSize = 15.sp)
                    }
                    TextButton(onClick = { showManualToken = !showManualToken }) {
                        Text(
                            if (showManualToken) "Скрыть ручной ввод" else "Ввести OAuth-токен вручную",
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
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
                            Text("Сохранить токен", fontWeight = FontWeight.Bold)
                        }
                    }
                }
            }
        } else {
            // Expandable live search bar + results (Tracks + Albums with covers)
            AnimatedVisibility(visible = searchOpen) {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = { searchQuery = it },
                        placeholder = { Text("Трек, альбом или исполнитель...") },
                        singleLine = true,
                        shape = RoundedCornerShape(99.dp),
                        modifier = Modifier.fillMaxWidth(),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedContainerColor = OledCardHigh,
                            unfocusedContainerColor = OledCard,
                            focusedBorderColor = YandexYellow,
                            unfocusedBorderColor = Color(0xFF28282E)
                        ),
                        leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null, tint = YandexYellow) },
                        trailingIcon = {
                            if (searching) {
                                CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp, color = YandexYellow)
                            } else if (searchQuery.isNotEmpty()) {
                                IconButton(onClick = { searchQuery = ""; onSearch("") }) {
                                    Icon(Icons.Rounded.Clear, contentDescription = "Очистить")
                                }
                            }
                        }
                    )

                    if (searchQuery.trim().length >= 2 && albumResults.isNotEmpty()) {
                        Text("Альбомы", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
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
                                        }
                                ) {
                                    Column(
                                        modifier = Modifier.padding(8.dp),
                                        verticalArrangement = Arrangement.spacedBy(6.dp)
                                    ) {
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
                                                if (album.year > 0) {
                                                    if (isNotEmpty()) append(" · ")
                                                    append(album.year)
                                                }
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

                    if (searchQuery.trim().length >= 2 && searchResults.isNotEmpty()) {
                        Text("Треки", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                        Surface(shape = RoundedCornerShape(20.dp), color = OledCard) {
                            Column(modifier = Modifier.fillMaxWidth()) {
                                searchResults.forEachIndexed { idx, track ->
                                    if (idx > 0) {
                                        HorizontalDivider(
                                            modifier = Modifier.padding(horizontal = 14.dp),
                                            color = Color(0xFF222228)
                                        )
                                    }
                                    Row(
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .clickable {
                                                activePreset = null
                                                onPlay("track", track.id)
                                            }
                                            .padding(horizontal = 12.dp, vertical = 10.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                                    ) {
                                        AsyncCoverArt(
                                            url = track.coverUrl,
                                            shape = RoundedCornerShape(10.dp),
                                            modifier = Modifier.size(46.dp),
                                            fallbackIcon = Icons.Rounded.MusicNote
                                        )
                                        Column(modifier = Modifier.weight(1f)) {
                                            Text(track.title, color = Color.White, fontWeight = FontWeight.Medium, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                            Text(
                                                buildString {
                                                    append(track.artist)
                                                    if (track.album.isNotBlank()) {
                                                        if (isNotEmpty()) append(" · ")
                                                        append(track.album)
                                                    }
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
                                        Icon(Icons.Rounded.PlayArrow, contentDescription = "Играть", tint = YandexYellow, modifier = Modifier.size(22.dp))
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // HERO: Animated Yellow-Orange-Magenta Fluid Wave + "Моя волна" + Big Yellow Play Button
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(320.dp),
                contentAlignment = Alignment.Center
            ) {
                MyWaveAnimatedAura(
                    isPlaying = ymusic.state == "playing",
                    modifier = Modifier.fillMaxSize()
                )

                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                    modifier = Modifier.padding(horizontal = 12.dp)
                ) {
                    Text(
                        "Моя волна",
                        color = Color.White,
                        fontSize = 40.sp,
                        fontWeight = FontWeight.Black,
                        letterSpacing = (-1.0).sp
                    )

                    // Big yellow circular Play/Pause button + Prev/Next flanking
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(22.dp)
                    ) {
                        IconButton(
                            onClick = { onControl("prev") },
                            modifier = Modifier
                                .size(44.dp)
                                .clip(CircleShape)
                                .background(Color.Black.copy(alpha = 0.35f))
                        ) {
                            Icon(Icons.Rounded.SkipPrevious, contentDescription = "Предыдущий", tint = Color.White, modifier = Modifier.size(26.dp))
                        }

                        Box(
                            modifier = Modifier
                                .size(78.dp)
                                .clip(CircleShape)
                                .background(YandexYellow)
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
                                CircularProgressIndicator(
                                    modifier = Modifier.size(30.dp),
                                    color = Color.Black,
                                    strokeWidth = 3.dp
                                )
                            } else {
                                Icon(
                                    if (ymusic.state == "playing") Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                                    contentDescription = "Воспроизвести / Пауза",
                                    tint = Color.Black,
                                    modifier = Modifier.size(40.dp)
                                )
                            }
                        }

                        IconButton(
                            onClick = { onControl("next") },
                            modifier = Modifier
                                .size(44.dp)
                                .clip(CircleShape)
                                .background(Color.Black.copy(alpha = 0.35f))
                        ) {
                            Icon(Icons.Rounded.SkipNext, contentDescription = "Следующий", tint = Color.White, modifier = Modifier.size(26.dp))
                        }
                    }

                    // Translucent track pill (just like Yandex Music screenshot!)
                    Surface(
                        shape = RoundedCornerShape(99.dp),
                        color = Color(0xAA1C1C20),
                        modifier = Modifier.fillMaxWidth(0.92f)
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 10.dp, vertical = 8.dp),
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
                            Column(
                                modifier = Modifier.weight(1f),
                                horizontalAlignment = if (ymusic.coverUrl.isBlank()) Alignment.CenterHorizontally else Alignment.Start
                            ) {
                                Text(
                                    if (ymusic.title.isNotBlank()) {
                                        if (ymusic.artist.isNotBlank()) "${ymusic.artist} — ${ymusic.title}" else ymusic.title
                                    } else "Нажмите ▶ для запуска на колонке",
                                    color = Color.White,
                                    fontWeight = FontWeight.SemiBold,
                                    fontSize = 13.sp,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                if (ymusic.album.isNotBlank()) {
                                    Text(
                                        ymusic.album,
                                        color = Color.White.copy(alpha = 0.65f),
                                        fontSize = 11.sp,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                            }
                            IconButton(
                                onClick = { onControl("like") },
                                modifier = Modifier.size(32.dp)
                            ) {
                                Icon(
                                    if (ymusic.liked) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                                    contentDescription = "Нравится",
                                    tint = if (ymusic.liked) Color(0xFFFF3366) else Color.White,
                                    modifier = Modifier.size(20.dp)
                                )
                            }
                        }
                    }
                }
            }

            // Glowing mood & station bubbles carousel (like bottom of Yandex Music screenshots!)
            LazyRow(
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                contentPadding = PaddingValues(horizontal = 2.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                items(WAVE_PRESETS) { preset ->
                    Box(
                        modifier = Modifier
                            .width(122.dp)
                            .height(126.dp)
                            .clip(RoundedCornerShape(28.dp))
                            .background(
                                Brush.verticalGradient(
                                    listOf(preset.colorTop.copy(alpha = 0.42f), preset.colorBottom.copy(alpha = 0.9f), OledBlack)
                                )
                            )
                            .clickable {
                                activePreset = if (preset.mode == "wave") null else preset.title
                                onPlay(preset.mode, preset.query)
                            }
                            .padding(12.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Column(
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Box(
                                modifier = Modifier
                                    .size(44.dp)
                                    .clip(CircleShape)
                                    .background(preset.colorTop.copy(alpha = 0.28f)),
                                contentAlignment = Alignment.Center
                            ) {
                                Icon(preset.icon, contentDescription = null, tint = preset.colorTop, modifier = Modifier.size(24.dp))
                            }
                            Text(
                                preset.title,
                                color = Color.White,
                                fontWeight = FontWeight.SemiBold,
                                fontSize = 12.sp,
                                textAlign = TextAlign.Center,
                                maxLines = 2,
                                lineHeight = 15.sp,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                }
            }

            // Compact account footer (account name + logout)
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 6.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "Аккаунт: ${ymusic.displayName.ifBlank { ymusic.login }}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    TextButton(onClick = { onControl("stop") }) {
                        Text("Стоп", color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 12.sp)
                    }
                    TextButton(onClick = onLogout) {
                        Icon(Icons.AutoMirrored.Rounded.Logout, contentDescription = null, modifier = Modifier.size(16.dp), tint = MaterialTheme.colorScheme.error)
                        Spacer(Modifier.width(4.dp))
                        Text("Выйти", color = MaterialTheme.colorScheme.error, fontSize = 12.sp)
                    }
                }
            }
        }

        if (!ymusic.error.isNullOrBlank()) {
            Surface(
                shape = RoundedCornerShape(14.dp),
                color = Color(0xFF2B121A)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = Color(0xFFFF8A9B))
                    Text(ymusic.error, style = MaterialTheme.typography.bodySmall, color = Color(0xFFFFD6DC))
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
                    LinearProgressIndicator(modifier = Modifier.fillMaxWidth(), color = YandexYellow)
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
