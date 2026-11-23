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
import com.leftymods.atrios.model.YMusicStatus
import com.leftymods.atrios.model.YMusicTrack
import kotlinx.coroutines.delay
import java.util.concurrent.atomic.AtomicBoolean

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun YMusicCard(
    ymusic: YMusicStatus,
    searchResults: List<YMusicTrack> = emptyList(),
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
        delay(450)
        onSearch(q)
        delay(1200)
        searching = false
    }

    LaunchedEffect(searchResults) {
        searching = false
    }

    val stateLabel = when {
        !ymusic.loggedIn -> "Не авторизован"
        ymusic.state == "playing" -> "Играет"
        ymusic.state == "paused" -> "Пауза"
        ymusic.state == "loading" -> "Загрузка…"
        ymusic.state == "error" -> "Ошибка"
        else -> "Готово"
    }

    ElevatedCard(
        shape = RoundedCornerShape(28.dp),
        colors = CardDefaults.elevatedCardColors(
            containerColor = MaterialTheme.colorScheme.surface
        )
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(38.dp)
                            .clip(RoundedCornerShape(12.dp))
                            .background(
                                Brush.linearGradient(
                                    listOf(Color(0xFFFFCC00), Color(0xFFFF5E3A))
                                )
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            Icons.Rounded.MusicNote,
                            contentDescription = null,
                            tint = Color(0xFF141414)
                        )
                    }
                    Column {
                        Text(
                            "Яндекс Музыка",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold
                        )
                        if (ymusic.loggedIn) {
                            val acc = ymusic.displayName.ifBlank { ymusic.login.ifBlank { "Аккаунт подключён" } }
                            Text(
                                acc,
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    AssistChip(onClick = onRefresh, label = { Text(stateLabel) })
                    if (ymusic.loggedIn) {
                        IconButton(onClick = onLogout) {
                            Icon(
                                Icons.AutoMirrored.Rounded.Logout,
                                contentDescription = "Выйти",
                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }

            if (!ymusic.loggedIn) {
                Text(
                    "Войдите в Яндекс ID прямо в приложении — токен автоматически перехватится и передастся на колонку.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Button(
                    onClick = { showOAuthDialog = true },
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(16.dp)
                ) {
                    Icon(Icons.AutoMirrored.Rounded.Login, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Войти в Яндекс Музыку")
                }
                TextButton(
                    onClick = { showManualToken = !showManualToken },
                    modifier = Modifier.align(Alignment.End)
                ) {
                    Text(if (showManualToken) "Скрыть ручной ввод" else "Ввести токен вручную")
                }
                if (showManualToken) {
                    OutlinedButton(
                        onClick = {
                            context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(YMusicStatus.OAUTH_URL)))
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.OpenInBrowser, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text("Открыть в браузере")
                    }
                    OutlinedTextField(
                        value = tokenInput,
                        onValueChange = { tokenInput = it },
                        label = { Text("URL с #access_token=... или токен") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth()
                    )
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        FilledTonalButton(onClick = {
                            val clip = clipboard.getText()?.text?.toString().orEmpty()
                            if (clip.isNotBlank()) tokenInput = YMusicStatus.extractAccessToken(clip)
                        }) {
                            Icon(Icons.Rounded.ContentPaste, contentDescription = null)
                            Spacer(Modifier.width(6.dp))
                            Text("Вставить")
                        }
                        Button(
                            onClick = {
                                val extracted = YMusicStatus.extractAccessToken(tokenInput)
                                if (extracted.isNotBlank()) {
                                    onLogin(extracted)
                                    tokenInput = ""
                                }
                            },
                            enabled = tokenInput.isNotBlank()
                        ) { Text("Войти") }
                    }
                }
            } else {
                Surface(
                    shape = RoundedCornerShape(22.dp),
                    border = BorderStroke(1.dp, Color.White.copy(alpha = 0.08f)),
                    color = Color.Transparent,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(22.dp))
                        .background(
                            Brush.linearGradient(
                                listOf(
                                    MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.7f),
                                    MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.65f)
                                )
                            )
                        )
                ) {
                    Column(
                        modifier = Modifier.fillMaxWidth().padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            Box(
                                modifier = Modifier
                                    .size(52.dp)
                                    .clip(RoundedCornerShape(16.dp))
                                    .background(
                                        Brush.linearGradient(
                                            listOf(Color(0xFF38E8FF), Color(0xFF6C63FF), Color(0xFFFF4FD8))
                                        )
                                    ),
                                contentAlignment = Alignment.Center
                            ) {
                                Icon(
                                    if (ymusic.state == "playing") Icons.Rounded.GraphicEq else Icons.Rounded.Album,
                                    contentDescription = null,
                                    tint = Color.White,
                                    modifier = Modifier.size(28.dp)
                                )
                            }
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    ymusic.title.ifBlank {
                                        if (ymusic.state == "loading") "Загрузка трека…"
                                        else "Выберите волну или трек"
                                    },
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.Bold,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                Text(
                                    ymusic.artist.ifBlank {
                                        when (ymusic.mode) {
                                            "wave" -> "Моя волна"
                                            "likes" -> "Любимые треки"
                                            "chart" -> "Топ-чарт"
                                            else -> "Готово к воспроизведению"
                                        }
                                    },
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                            IconButton(onClick = { onControl("like") }) {
                                Icon(
                                    if (ymusic.liked) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                                    contentDescription = "Лайк",
                                    tint = if (ymusic.liked) Color(0xFFFF4D8D) else MaterialTheme.colorScheme.onSurfaceVariant
                                )
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
                                Icon(Icons.Rounded.SkipPrevious, contentDescription = "Предыдущий", modifier = Modifier.size(28.dp))
                            }
                            FilledIconButton(
                                onClick = { onControl(if (ymusic.state == "playing") "pause" else "play") },
                                modifier = Modifier.size(54.dp),
                                shape = CircleShape
                            ) {
                                Icon(
                                    if (ymusic.state == "playing") Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                                    contentDescription = "Пауза / Плей",
                                    modifier = Modifier.size(30.dp)
                                )
                            }
                            IconButton(onClick = { onControl("next") }) {
                                Icon(Icons.Rounded.SkipNext, contentDescription = "Следующий", modifier = Modifier.size(28.dp))
                            }
                            IconButton(onClick = { onControl("stop") }) {
                                Icon(Icons.Rounded.Stop, contentDescription = "Стоп")
                            }
                        }
                    }
                }

                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(onClick = { onPlay("wave", null) }, shape = RoundedCornerShape(14.dp)) {
                        Icon(Icons.Rounded.Waves, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("Моя волна")
                    }
                    FilledTonalButton(onClick = { onPlay("likes", null) }, shape = RoundedCornerShape(14.dp)) {
                        Icon(Icons.Rounded.Favorite, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("Любимые")
                    }
                    FilledTonalButton(onClick = { onPlay("chart", null) }, shape = RoundedCornerShape(14.dp)) {
                        Icon(Icons.Rounded.TrendingUp, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("Чарт")
                    }
                    OutlinedButton(onClick = { onPlay("station", "genre:rock") }, shape = RoundedCornerShape(14.dp)) { Text("Рок") }
                    OutlinedButton(onClick = { onPlay("station", "mood:calm") }, shape = RoundedCornerShape(14.dp)) { Text("Спокойная") }
                }

                OutlinedTextField(
                    value = searchQuery,
                    onValueChange = { searchQuery = it },
                    label = { Text("Поиск треков и исполнителей") },
                    leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
                    trailingIcon = {
                        if (searching) {
                            CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                        } else if (searchQuery.isNotBlank()) {
                            IconButton(onClick = { searchQuery = ""; onSearch("") }) {
                                Icon(Icons.Rounded.Clear, contentDescription = "Очистить")
                            }
                        }
                    },
                    singleLine = true,
                    shape = RoundedCornerShape(16.dp),
                    modifier = Modifier.fillMaxWidth()
                )

                if (searchQuery.trim().length >= 2 && searchResults.isNotEmpty()) {
                    Surface(
                        shape = RoundedCornerShape(18.dp),
                        tonalElevation = 2.dp,
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.25f)),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Column(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                            searchResults.take(8).forEachIndexed { idx, track ->
                                if (idx > 0) {
                                    HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.12f))
                                }
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .clickable { onPlay("track", track.id) }
                                        .padding(horizontal = 14.dp, vertical = 10.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                                ) {
                                    Icon(
                                        Icons.Rounded.PlayCircleFilled,
                                        contentDescription = null,
                                        tint = MaterialTheme.colorScheme.primary,
                                        modifier = Modifier.size(26.dp)
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
                                            track.artist,
                                            style = MaterialTheme.typography.labelSmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                    }
                                    if (track.durationMs > 0L) {
                                        Text(
                                            track.durationFormatted(),
                                            style = MaterialTheme.typography.labelSmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (!ymusic.error.isNullOrBlank()) {
                Surface(
                    shape = RoundedCornerShape(14.dp),
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
        YMusicOAuthDialog(
            onDismiss = { showOAuthDialog = false },
            onTokenCaptured = { token ->
                showOAuthDialog = false
                onLogin(token)
            }
        )
    }
}

@SuppressLint("SetJavaScriptEnabled")
@Composable
private fun YMusicOAuthDialog(
    onDismiss: () -> Unit,
    onTokenCaptured: (String) -> Unit
) {
    val captured = remember { AtomicBoolean(false) }
    var loading by remember { mutableStateOf(true) }

    fun tryExtract(rawUrl: String?): Boolean {
        if (rawUrl.isNullOrBlank() || !rawUrl.contains("access_token=")) return false
        val tok = YMusicStatus.extractAccessToken(rawUrl)
        if (tok.isNotBlank() && tok != rawUrl.trim() && captured.compareAndSet(false, true)) {
            onTokenCaptured(tok)
            return true
        }
        return false
    }

    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(
            modifier = Modifier.fillMaxSize().padding(12.dp),
            shape = RoundedCornerShape(24.dp),
            tonalElevation = 6.dp
        ) {
            Column(modifier = Modifier.fillMaxSize()) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 10.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text("Вход в Яндекс Музыку", style = MaterialTheme.typography.titleMedium)
                    IconButton(onClick = onDismiss) { Icon(Icons.Rounded.Close, contentDescription = "Закрыть") }
                }
                if (loading) LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                AndroidView(
                    modifier = Modifier.fillMaxSize(),
                    factory = { ctx ->
                        WebView(ctx).apply {
                            settings.javaScriptEnabled = true
                            settings.domStorageEnabled = true
                            settings.databaseEnabled = true
                            CookieManager.getInstance().setAcceptCookie(true)
                            CookieManager.getInstance().setAcceptThirdPartyCookies(this, true)
                            webViewClient = object : WebViewClient() {
                                override fun shouldOverrideUrlLoading(view: WebView?, request: WebResourceRequest?): Boolean =
                                    tryExtract(request?.url?.toString())
                                override fun onPageStarted(view: WebView?, url: String?, favicon: Bitmap?) {
                                    loading = true; tryExtract(url)
                                }
                                override fun doUpdateVisitedHistory(view: WebView?, url: String?, isReload: Boolean) {
                                    tryExtract(url)
                                }
                                override fun onPageFinished(view: WebView?, url: String?) {
                                    loading = false
                                    if (!tryExtract(url)) {
                                        view?.evaluateJavascript("window.location.href") { jsHref ->
                                            tryExtract(jsHref?.trim()?.trim('"'))
                                        }
                                    }
                                }
                            }
                            loadUrl(YMusicStatus.OAUTH_URL)
                        }
                    }
                )
            }
        }
    }
}
