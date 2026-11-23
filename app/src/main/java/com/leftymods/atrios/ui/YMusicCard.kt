package com.leftymods.atrios.ui

import android.annotation.SuppressLint
import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.webkit.CookieManager
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import java.util.concurrent.atomic.AtomicBoolean
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Login
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.leftymods.atrios.model.YMusicStatus

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun YMusicCard(
    ymusic: YMusicStatus,
    onRefresh: () -> Unit,
    onLogin: (String) -> Unit,
    onLogout: () -> Unit,
    onPlay: (String, String?) -> Unit,
    onControl: (String) -> Unit
) {
    val context = LocalContext.current
    val clipboard = LocalClipboardManager.current
    var tokenInput by remember { mutableStateOf("") }
    var searchQuery by remember { mutableStateOf("") }
    var showOAuthDialog by remember { mutableStateOf(false) }
    var showManualToken by remember { mutableStateOf(false) }

    val stateLabel = when {
        !ymusic.loggedIn -> "Не авторизован"
        ymusic.state == "playing" -> "Играет"
        ymusic.state == "paused" -> "Пауза"
        ymusic.state == "loading" -> "Загрузка…"
        ymusic.state == "error" -> "Ошибка"
        else -> "Готово"
    }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(Icons.Rounded.MusicNote, contentDescription = null)
                    Text("Яндекс Музыка", style = MaterialTheme.typography.titleMedium)
                }
                AssistChip(
                    onClick = onRefresh,
                    label = { Text(stateLabel) }
                )
            }

            if (!ymusic.loggedIn) {
                Text(
                    "Войдите в Яндекс ID прямо в приложении — токен автоматически перехватится и передастся на колонку (хранится локально 0600).",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )

                Button(
                    onClick = { showOAuthDialog = true },
                    modifier = Modifier.fillMaxWidth()
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
                            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(YMusicStatus.OAUTH_URL))
                            context.startActivity(intent)
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
                            if (clip.isNotBlank()) {
                                tokenInput = YMusicStatus.extractAccessToken(clip)
                            }
                        }) {
                            Icon(Icons.Rounded.ContentPaste, contentDescription = null)
                            Spacer(Modifier.width(6.dp))
                            Text("Вставить из буфера")
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
                        ) {
                            Icon(Icons.AutoMirrored.Rounded.Login, contentDescription = null)
                            Spacer(Modifier.width(6.dp))
                            Text("Войти на колонке")
                        }
                    }
                }
            } else {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    val accountLabel = ymusic.displayName.ifBlank { ymusic.login.ifBlank { "Аккаунт Яндекс" } }
                    Text(
                        "Аккаунт: $accountLabel",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    TextButton(onClick = onLogout) {
                        Text("Выйти")
                    }
                }

                run { // Always keep Now-Playing player visible when logged in
                    Surface(
                        shape = RoundedCornerShape(16.dp),
                        color = MaterialTheme.colorScheme.secondaryContainer
                    ) {
                        Column(
                            modifier = Modifier.fillMaxWidth().padding(14.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            val modeLabel = when (ymusic.mode) {
                                "wave" -> "Моя волна"
                                "likes" -> "Любимые треки"
                                "search" -> "Поиск"
                                else -> "Сейчас играет"
                            }
                            Text(
                                modeLabel,
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSecondaryContainer
                            )
                            Text(
                                ymusic.title.ifBlank { if (ymusic.state == "loading") "Загрузка трека…" else "Выберите волну или трек" },
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.onSecondaryContainer
                            )
                            if (ymusic.artist.isNotBlank()) {
                                Text(
                                    buildString {
                                        append(ymusic.artist)
                                        if (ymusic.album.isNotBlank()) append(" · ${ymusic.album}")
                                    },
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSecondaryContainer
                                )
                            }
                            if (ymusic.state == "loading") {
                                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                            }

                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceEvenly,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                IconButton(onClick = { onControl("prev") }) {
                                    Icon(Icons.Rounded.SkipPrevious, contentDescription = "Предыдущий")
                                }
                                FilledIconButton(
                                    onClick = {
                                        onControl(if (ymusic.state == "playing") "pause" else "play")
                                    }
                                ) {
                                    Icon(
                                        if (ymusic.state == "playing") Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                                        contentDescription = if (ymusic.state == "playing") "Пауза" else "Играть"
                                    )
                                }
                                IconButton(onClick = { onControl("next") }) {
                                    Icon(Icons.Rounded.SkipNext, contentDescription = "Следующий")
                                }
                                IconButton(onClick = { onControl("stop") }) {
                                    Icon(Icons.Rounded.Stop, contentDescription = "Стоп")
                                }
                            }

                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                if (ymusic.liked) {
                                    Button(
                                        onClick = { onControl("like") },
                                        modifier = Modifier.weight(1f)
                                    ) {
                                        Icon(Icons.Rounded.Favorite, contentDescription = null)
                                        Spacer(Modifier.width(6.dp))
                                        Text("Нравится")
                                    }
                                } else {
                                    FilledTonalButton(
                                        onClick = { onControl("like") },
                                        modifier = Modifier.weight(1f)
                                    ) {
                                        Icon(Icons.Rounded.FavoriteBorder, contentDescription = null)
                                        Spacer(Modifier.width(6.dp))
                                        Text("Лайк")
                                    }
                                }
                                OutlinedButton(
                                    onClick = { onControl("dislike") },
                                    modifier = Modifier.weight(1f)
                                ) {
                                    Icon(Icons.Rounded.ThumbDown, contentDescription = null)
                                    Spacer(Modifier.width(6.dp))
                                    Text("Дизлайк")
                                }
                            }
                        }
                    }
                }

                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(onClick = { onPlay("wave", null) }) {
                        Icon(Icons.Rounded.Waves, contentDescription = null)
                        Spacer(Modifier.width(6.dp))
                        Text("Моя волна")
                    }
                    FilledTonalButton(onClick = { onPlay("likes", null) }) {
                        Icon(Icons.Rounded.Favorite, contentDescription = null)
                        Spacer(Modifier.width(6.dp))
                        Text("Любимые")
                    }
                    FilledTonalButton(onClick = { onPlay("chart", null) }) {
                        Icon(Icons.Rounded.TrendingUp, contentDescription = null)
                        Spacer(Modifier.width(6.dp))
                        Text("Чарт")
                    }
                    OutlinedButton(onClick = { onPlay("station", "genre:rock") }) {
                        Text("Рок")
                    }
                    OutlinedButton(onClick = { onPlay("station", "mood:calm") }) {
                        Text("Спокойная")
                    }
                }

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = { searchQuery = it },
                        label = { Text("Найти трек или исполнителя") },
                        singleLine = true,
                        modifier = Modifier.weight(1f)
                    )
                    Button(
                        onClick = {
                            if (searchQuery.isNotBlank()) {
                                onPlay("search", searchQuery.trim())
                            }
                        },
                        enabled = searchQuery.isNotBlank()
                    ) {
                        Icon(Icons.Rounded.PlayArrow, contentDescription = "Найти и включить")
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
                        Icon(
                            Icons.Rounded.ErrorOutline,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onErrorContainer
                        )
                        Text(
                            ymusic.error,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onErrorContainer
                        )
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

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
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
                    IconButton(onClick = onDismiss) {
                        Icon(Icons.Rounded.Close, contentDescription = "Закрыть")
                    }
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
                            settings.databaseEnabled = true
                            CookieManager.getInstance().setAcceptCookie(true)
                            CookieManager.getInstance().setAcceptThirdPartyCookies(this, true)
                            webViewClient = object : WebViewClient() {
                                override fun shouldOverrideUrlLoading(
                                    view: WebView?,
                                    request: WebResourceRequest?
                                ): Boolean {
                                    val url = request?.url?.toString()
                                    if (tryExtract(url)) return true
                                    return false
                                }

                                override fun onPageStarted(view: WebView?, url: String?, favicon: Bitmap?) {
                                    loading = true
                                    tryExtract(url)
                                }

                                override fun doUpdateVisitedHistory(view: WebView?, url: String?, isReload: Boolean) {
                                    tryExtract(url)
                                }

                                override fun onPageFinished(view: WebView?, url: String?) {
                                    loading = false
                                    if (!tryExtract(url)) {
                                        view?.evaluateJavascript("window.location.href") { jsHref ->
                                            val cleaned = jsHref?.trim()?.trim('"')
                                            tryExtract(cleaned)
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
