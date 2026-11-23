package com.leftymods.atrios.ui

import android.annotation.SuppressLint
import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.webkit.CookieManager
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.foundation.BorderStroke
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
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.leftymods.atrios.model.YMusicAlbum
import com.leftymods.atrios.model.YMusicStatus
import com.leftymods.atrios.model.YMusicTrack
import kotlinx.coroutines.delay
import java.util.concurrent.atomic.AtomicBoolean

@OptIn(ExperimentalLayoutApi::class)
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
    var searching by remember { mutableStateOf(false) }
    var showOAuthDialog by remember { mutableStateOf(false) }
    var showManualToken by remember { mutableStateOf(false) }

    LaunchedEffect(searchQuery, ymusic.loggedIn) {
        if (!ymusic.loggedIn) return@LaunchedEffect
        val q = searchQuery.trim()
        if (q.length < 2) {
            searching = false
            if (q.isEmpty()) onSearch("")
            return@LaunchedEffect
        }
        searching = true
        delay(320)
        onSearch(q)
    }

    LaunchedEffect(searchResults, albumResults) {
        searching = false
    }

    val stateLabel = when {
        !ymusic.loggedIn -> "Не подключено"
        ymusic.state == "playing" -> "Играет"
        ymusic.state == "paused" -> "Пауза"
        ymusic.state == "loading" -> "Загрузка..."
        ymusic.state == "error" -> "Ошибка"
        else -> "Готово"
    }

    ElevatedCard(
        shape = RoundedCornerShape(26.dp),
        colors = CardDefaults.elevatedCardColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainerLow
        )
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            // Header with Yandex Avatar
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    modifier = Modifier.weight(1f)
                ) {
                    if (ymusic.loggedIn) {
                        AsyncCoverArt(
                            url = ymusic.avatarUrl,
                            shape = CircleShape,
                            modifier = Modifier.size(44.dp),
                            fallbackIcon = Icons.Rounded.Person
                        )
                    } else {
                        Box(
                            modifier = Modifier
                                .size(44.dp)
                                .clip(CircleShape)
                                .background(
                                    Brush.linearGradient(
                                        listOf(Color(0xFFFFCC00), Color(0xFFFF6B00))
                                    )
                                ),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                Icons.Rounded.MusicNote,
                                contentDescription = null,
                                tint = Color(0xFF1A1200),
                                modifier = Modifier.size(24.dp)
                            )
                        }
                    }
                    Column {
                        Text(
                            if (ymusic.loggedIn) ymusic.displayName.ifBlank { ymusic.login.ifBlank { "Яндекс Музыка" } }
                            else "Яндекс Музыка",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            if (ymusic.loggedIn) "Яндекс Музыка · $stateLabel" else stateLabel,
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    IconButton(onClick = onRefresh) {
                        Icon(Icons.Rounded.Refresh, contentDescription = "Обновить")
                    }
                    if (ymusic.loggedIn) {
                        IconButton(onClick = onLogout) {
                            Icon(
                                Icons.AutoMirrored.Rounded.Logout,
                                contentDescription = "Выйти",
                                tint = MaterialTheme.colorScheme.error
                            )
                        }
                    }
                }
            }

            if (!ymusic.loggedIn) {
                Text(
                    "Войдите в аккаунт Яндекс Плюс — колонка сможет играть «Мою волну», лайки, чарты, альбомы и любые треки по голосу и из приложения.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Button(
                    onClick = { showOAuthDialog = true },
                    modifier = Modifier.fillMaxWidth().height(50.dp),
                    shape = RoundedCornerShape(16.dp)
                ) {
                    Icon(Icons.AutoMirrored.Rounded.Login, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Войти через Яндекс", fontWeight = FontWeight.SemiBold)
                }
                TextButton(
                    onClick = { showManualToken = !showManualToken },
                    modifier = Modifier.align(Alignment.End)
                ) {
                    Text(if (showManualToken) "Скрыть ручной ввод" else "Ввести OAuth-токен вручную")
                }
                if (showManualToken) {
                    OutlinedButton(
                        onClick = {
                            context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(YMUSIC_OAUTH_URL)))
                        },
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
                                val extracted = extractYMusicToken(txt)
                                tokenInput = extracted ?: txt.trim()
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
                        shape = RoundedCornerShape(14.dp)
                    ) {
                        Text("Сохранить токен")
                    }
                }
            } else {
                // Now-Playing Hero Card with Cover Art
                Surface(
                    shape = RoundedCornerShape(22.dp),
                    color = Color.Transparent,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(
                                Brush.linearGradient(
                                    listOf(
                                        Color(0xFF152544),
                                        Color(0xFF211A42),
                                        Color(0xFF111B33)
                                    )
                                )
                            )
                            .padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(14.dp)
                        ) {
                            AsyncCoverArt(
                                url = ymusic.coverUrl,
                                shape = RoundedCornerShape(16.dp),
                                modifier = Modifier.size(72.dp),
                                fallbackIcon = if (ymusic.state == "playing") Icons.Rounded.GraphicEq else Icons.Rounded.Album
                            )
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    ymusic.title.ifBlank { "Выберите трек или «Мою волну»" },
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.Bold,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                if (ymusic.artist.isNotBlank()) {
                                    Text(
                                        ymusic.artist,
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                                if (ymusic.album.isNotBlank()) {
                                    Text(
                                        ymusic.album,
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.72f),
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                            }
                            if (ymusic.isActive || ymusic.title.isNotBlank()) {
                                IconButton(onClick = { onControl("like") }) {
                                    Icon(
                                        if (ymusic.liked) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                                        contentDescription = "Нравится",
                                        tint = if (ymusic.liked) Color(0xFFFF5C8A) else MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        }

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceEvenly,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            IconButton(onClick = { onControl("dislike") }) {
                                Icon(Icons.Rounded.ThumbDown, contentDescription = "Дизлайк")
                            }
                            IconButton(onClick = { onControl("prev") }) {
                                Icon(Icons.Rounded.SkipPrevious, contentDescription = "Предыдущий", modifier = Modifier.size(30.dp))
                            }
                            FilledIconButton(
                                onClick = {
                                    if (ymusic.state == "playing") onControl("pause")
                                    else if (ymusic.state == "paused") onControl("play")
                                    else onPlay("wave", null)
                                },
                                modifier = Modifier.size(56.dp),
                                shape = CircleShape
                            ) {
                                Icon(
                                    if (ymusic.state == "playing") Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                                    contentDescription = "Пауза / Воспроизвести",
                                    modifier = Modifier.size(30.dp)
                                )
                            }
                            IconButton(onClick = { onControl("next") }) {
                                Icon(Icons.Rounded.SkipNext, contentDescription = "Следующий", modifier = Modifier.size(30.dp))
                            }
                            IconButton(onClick = { onControl("stop") }) {
                                Icon(Icons.Rounded.Stop, contentDescription = "Стоп")
                            }
                        }
                    }
                }

                // Quick modes
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    FilledTonalButton(onClick = { onPlay("wave", null) }, shape = RoundedCornerShape(14.dp)) {
                        Icon(Icons.Rounded.Waves, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("Моя волна")
                    }
                    FilledTonalButton(onClick = { onPlay("likes", null) }, shape = RoundedCornerShape(14.dp)) {
                        Icon(Icons.Rounded.Favorite, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("Мне нравится")
                    }
                    FilledTonalButton(onClick = { onPlay("chart", null) }, shape = RoundedCornerShape(14.dp)) {
                        Icon(Icons.Rounded.TrendingUp, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("Чарт")
                    }
                }

                // Live interactive search (Tracks + Albums)
                OutlinedTextField(
                    value = searchQuery,
                    onValueChange = { searchQuery = it },
                    label = { Text("Поиск треков и альбомов") },
                    placeholder = { Text("Введите название или исполнителя...") },
                    singleLine = true,
                    shape = RoundedCornerShape(16.dp),
                    modifier = Modifier.fillMaxWidth(),
                    leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
                    trailingIcon = {
                        if (searching) {
                            CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
                        } else if (searchQuery.isNotEmpty()) {
                            IconButton(onClick = { searchQuery = ""; onSearch("") }) {
                                Icon(Icons.Rounded.Clear, contentDescription = "Очистить")
                            }
                        }
                    }
                )

                // Albums carousel
                if (searchQuery.trim().length >= 2 && albumResults.isNotEmpty()) {
                    Text(
                        "Альбомы",
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.primary
                    )
                    LazyRow(
                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        items(albumResults, key = { it.id }) { album ->
                            Surface(
                                shape = RoundedCornerShape(16.dp),
                                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                                modifier = Modifier
                                    .width(126.dp)
                                    .clickable { onPlay("album", album.id) }
                            ) {
                                Column(
                                    modifier = Modifier.padding(8.dp),
                                    verticalArrangement = Arrangement.spacedBy(6.dp)
                                ) {
                                    AsyncCoverArt(
                                        url = album.coverUrl,
                                        shape = RoundedCornerShape(12.dp),
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .aspectRatio(1f),
                                        fallbackIcon = Icons.Rounded.Album
                                    )
                                    Text(
                                        album.title,
                                        style = MaterialTheme.typography.labelMedium,
                                        fontWeight = FontWeight.SemiBold,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                    Text(
                                        buildString {
                                            append(album.artist)
                                            if (album.year > 0) {
                                                if (isNotEmpty()) append(" · ")
                                                append(album.year)
                                            }
                                        },
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                            }
                        }
                    }
                }

                // Tracks list with cover art
                if (searchQuery.trim().length >= 2 && searchResults.isNotEmpty()) {
                    Text(
                        "Треки",
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.primary
                    )
                    Surface(
                        shape = RoundedCornerShape(18.dp),
                        color = MaterialTheme.colorScheme.surfaceContainerHigh
                    ) {
                        Column(modifier = Modifier.fillMaxWidth()) {
                            searchResults.forEachIndexed { idx, track ->
                                if (idx > 0) {
                                    HorizontalDivider(
                                        modifier = Modifier.padding(horizontal = 12.dp),
                                        color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
                                    )
                                }
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .clickable { onPlay("track", track.id) }
                                        .padding(horizontal = 12.dp, vertical = 9.dp),
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
                                        Text(
                                            track.title,
                                            style = MaterialTheme.typography.bodyMedium,
                                            fontWeight = FontWeight.Medium,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                        Text(
                                            buildString {
                                                append(track.artist)
                                                if (track.album.isNotBlank()) {
                                                    if (isNotEmpty()) append(" · ")
                                                    append(track.album)
                                                }
                                            },
                                            style = MaterialTheme.typography.labelSmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                    }
                                    if (track.durationMs > 0) {
                                        Text(
                                            formatDurationMs(track.durationMs),
                                            style = MaterialTheme.typography.labelSmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant
                                        )
                                    }
                                    Icon(
                                        Icons.Rounded.PlayArrow,
                                        contentDescription = "Играть",
                                        tint = MaterialTheme.colorScheme.primary,
                                        modifier = Modifier.size(20.dp)
                                    )
                                }
                            }
                        }
                    }
                }
            }

            if (!ymusic.error.isNullOrBlank()) {
                Surface(
                    shape = RoundedCornerShape(12.dp),
                    color = MaterialTheme.colorScheme.errorContainer
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = MaterialTheme.colorScheme.onErrorContainer)
                        Text(ymusic.error, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onErrorContainer)
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
            tonalElevation = 6.dp
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
