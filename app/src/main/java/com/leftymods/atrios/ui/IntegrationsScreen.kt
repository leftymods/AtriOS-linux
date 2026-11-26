package com.leftymods.atrios.ui

import android.accounts.AccountManager
import android.annotation.SuppressLint
import android.app.Activity
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.leftymods.atrios.AtriAccountState
import com.leftymods.atrios.model.YMusicStatus
import com.leftymods.atrios.ui.theme.AppPalette
import com.leftymods.atrios.ui.theme.OledBlack
import com.leftymods.atrios.ui.theme.OledCard
import com.leftymods.atrios.ui.theme.OledCardHigh

@Composable
fun IntegrationsScreen(
    accountState: AtriAccountState,
    accountBusy: Boolean,
    accountError: String?,
    currentDeviceId: String,
    currentSpeakerName: String,
    ymStatus: YMusicStatus,
    palette: AppPalette,
    onAccountRegister: (String, String) -> Unit,
    onAccountLogin: (String, String) -> Unit,
    onAccountSync: () -> Unit,
    onAccountLogout: () -> Unit,
    onBindCurrentSpeaker: () -> Unit,
    onUnbindSpeaker: (String) -> Unit,
    onYandexLoginToken: (String) -> Unit,
    onYandexLogout: () -> Unit,
    onToggleYnison: (Boolean) -> Unit = {}
) {
    var showYmWebView by remember { mutableStateOf(false) }
    var isRegisterMode by remember { mutableStateOf(false) }
    var username by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    val passwordFocusRequester = remember { FocusRequester() }
    val googleAccountLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { res ->
        if (res.resultCode == Activity.RESULT_OK) {
            val email = res.data?.getStringExtra(AccountManager.KEY_ACCOUNT_NAME)?.trim().orEmpty()
            if (email.isNotBlank()) {
                username = email
                runCatching { passwordFocusRequester.requestFocus() }
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(OledBlack)
            .statusBarsPadding()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp, vertical = 14.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Text(
            text = "Аккаунт и интеграции",
            color = Color.White,
            fontSize = 22.sp,
            fontWeight = FontWeight.Bold
        )

        // 1. Zero-Knowledge AtriOS Account Card
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(24.dp))
                .background(OledCard)
                .border(1.dp, Color.White.copy(alpha = 0.08f), RoundedCornerShape(24.dp))
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(40.dp)
                        .clip(CircleShape)
                        .background(palette.accent.copy(alpha = 0.18f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(Icons.Rounded.VerifiedUser, contentDescription = null, tint = palette.accent)
                }
                Column(modifier = Modifier.weight(1f)) {
                    val idLabel = if (accountState.username.contains("@")) accountState.username else "@${accountState.username}"
                    Text(
                        text = if (accountState.loggedIn) "AtriOS ID · $idLabel" else "Аккаунт AtriOS",
                        color = Color.White,
                        fontSize = 16.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = if (accountState.loggedIn) {
                            "Защищённое облако"
                        } else {
                            "Шифрование на устройстве · сервер не видит токены"
                        },
                        color = Color(0xFFA0A0B0),
                        fontSize = 12.sp
                    )
                }
            }

            if (!accountState.loggedIn) {
                OutlinedButton(
                    onClick = {
                        val intent = AccountManager.newChooseAccountIntent(
                            null,
                            null,
                            arrayOf("com.google"),
                            null,
                            null,
                            null,
                            null
                        )
                        googleAccountLauncher.launch(intent)
                    },
                    enabled = !accountBusy,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Rounded.Email, contentDescription = null, tint = palette.accent, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text("Войти через Google (выбрать почту)", color = Color.White, fontWeight = FontWeight.Medium)
                }
                OutlinedTextField(
                    value = username,
                    onValueChange = { username = it },
                    label = { Text("Почта Google или логин") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                OutlinedTextField(
                    value = password,
                    onValueChange = { password = it },
                    label = { Text("Мастер-пароль") },
                    visualTransformation = PasswordVisualTransformation(),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth().focusRequester(passwordFocusRequester)
                )
                if (!accountError.isNullOrBlank()) {
                    Text(accountError, color = Color(0xFFFF6B6B), fontSize = 12.sp)
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = {
                            if (isRegisterMode) onAccountRegister(username, password)
                            else onAccountLogin(username, password)
                        },
                        enabled = !accountBusy && username.trim().length >= 3 && password.length >= 6,
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = palette.accent, contentColor = Color.Black)
                    ) {
                        if (accountBusy) {
                            CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp, color = Color.Black)
                        } else {
                            Text(if (isRegisterMode) "Создать" else "Войти", fontWeight = FontWeight.SemiBold)
                        }
                    }
                    OutlinedButton(
                        onClick = { isRegisterMode = !isRegisterMode },
                        enabled = !accountBusy
                    ) {
                        Text(if (isRegisterMode) "Уже есть" else "Регистрация", color = Color.White)
                    }
                }
            } else {
                // Bound speakers section
                val isBound = currentDeviceId.isNotBlank() && accountState.devices.any { it.deviceId == currentDeviceId }
                if (currentDeviceId.isNotBlank()) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(14.dp))
                            .background(OledCardHigh)
                            .padding(horizontal = 12.dp, vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(currentSpeakerName.ifBlank { "AtriStation" }, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Medium)
                            Text(
                                if (isBound) "Привязана к аккаунту AtriOS" else "Не привязана к аккаунту",
                                color = if (isBound) Color(0xFF4ADE80) else Color(0xFFA0A0B0),
                                fontSize = 12.sp
                            )
                        }
                        TextButton(
                            onClick = { if (isBound) onUnbindSpeaker(currentDeviceId) else onBindCurrentSpeaker() },
                            enabled = !accountBusy
                        ) {
                            Text(
                                if (isBound) "Отвязать" else "Привязать",
                                color = if (isBound) Color(0xFFFF6B6B) else palette.accent
                            )
                        }
                    }
                }
                if (!accountError.isNullOrBlank()) {
                    Text(accountError, color = Color(0xFFFF6B6B), fontSize = 12.sp)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    val syncSpin = if (accountBusy) {
                        val inf = rememberInfiniteTransition(label = "syncSpin")
                        val rot by inf.animateFloat(
                            initialValue = 0f,
                            targetValue = 360f,
                            animationSpec = infiniteRepeatable(
                                animation = tween(900, easing = LinearEasing),
                                repeatMode = RepeatMode.Restart
                            ),
                            label = "syncRot"
                        )
                        rot
                    } else 0f
                    OutlinedButton(
                        onClick = onAccountSync,
                        enabled = !accountBusy,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(
                            Icons.Rounded.Sync,
                            contentDescription = null,
                            tint = Color.White,
                            modifier = Modifier
                                .size(16.dp)
                                .graphicsLayer { rotationZ = syncSpin }
                        )
                        Spacer(Modifier.width(6.dp))
                        Text(
                            if (accountBusy) "Синхронизация..." else "Синхронизировать",
                            color = Color.White,
                            fontSize = 13.sp
                        )
                    }
                    OutlinedButton(
                        onClick = onAccountLogout,
                        enabled = !accountBusy
                    ) {
                        Text("Выйти", color = Color(0xFFFF6B6B), fontSize = 13.sp)
                    }
                }
            }
        }

        // 2. Active Music Integration: Yandex Music (Ynison Connect)
        Text(
            text = "МУЗЫКА И СЕРВИСЫ",
            color = Color(0xFF8A8A9A),
            fontSize = 11.sp,
            fontWeight = FontWeight.SemiBold,
            letterSpacing = 1.sp
        )

        Column(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(24.dp))
                .background(OledCard)
                .border(1.dp, Color.White.copy(alpha = 0.08f), RoundedCornerShape(24.dp))
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(44.dp)
                        .clip(RoundedCornerShape(14.dp))
                        .background(Color(0xFFFFCC00).copy(alpha = 0.16f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(Icons.Rounded.MusicNote, contentDescription = null, tint = Color(0xFFFFCC00))
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text("Яндекс Музыка", color = Color.White, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
                    Text(
                        text = if (ymStatus.loggedIn) {
                            "Активна (${ymStatus.displayName.ifBlank { ymStatus.login }}) · AtriStation видна в ЯМ"
                        } else {
                            "После входа колонка сама появится в приложении ЯМ"
                        },
                        color = if (ymStatus.loggedIn) Color(0xFF4ADE80) else Color(0xFFA0A0B0),
                        fontSize = 12.sp
                    )
                }
            }

            if (ymStatus.loggedIn) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(14.dp))
                        .background(OledCardHigh)
                        .padding(horizontal = 12.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            "Управление из приложения Яндекс Музыка",
                            color = Color.White,
                            fontSize = 13.sp,
                            fontWeight = FontWeight.Medium
                        )
                        Text(
                            if (ymStatus.ynisonEnabled) "Колонка видна в списке устройств" else "Отключено — колонка скрыта в ЯМ",
                            color = Color(0xFFA0A0B0),
                            fontSize = 11.sp
                        )
                    }
                    Switch(
                        checked = ymStatus.ynisonEnabled,
                        onCheckedChange = { onToggleYnison(it) },
                        colors = SwitchDefaults.colors(
                            checkedThumbColor = palette.onAccent,
                            checkedTrackColor = palette.accent
                        )
                    )
                }
                OutlinedButton(
                    onClick = onYandexLogout,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = Color(0xFFFF6B6B))
                ) {
                    Icon(Icons.Rounded.LinkOff, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text("Отключить и убрать AtriStation из Яндекс Музыки")
                }
            } else {
                Button(
                    onClick = { showYmWebView = true },
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFFFCC00), contentColor = Color.Black)
                ) {
                    Icon(Icons.Rounded.Login, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text("Подключить Яндекс Музыку", fontWeight = FontWeight.SemiBold)
                }
            }
        }

        // 3. Smart Home Slot
        FutureIntegrationRow(Icons.Rounded.Home, "Home Assistant / Умный дом", "Скоро · голосовое управление устройствами")

        Spacer(Modifier.height(16.dp))
    }

    if (showYmWebView) {
        YandexOAuthDialog(
            onToken = { tok ->
                showYmWebView = false
                onYandexLoginToken(tok)
            },
            onDismiss = { showYmWebView = false }
        )
    }
}

@Composable
private fun FutureIntegrationRow(icon: ImageVector, title: String, subtitle: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(18.dp))
            .background(OledCard.copy(alpha = 0.7f))
            .border(1.dp, Color.White.copy(alpha = 0.05f), RoundedCornerShape(18.dp))
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Icon(icon, contentDescription = null, tint = Color(0xFF707085), modifier = Modifier.size(22.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(title, color = Color.White.copy(alpha = 0.85f), fontSize = 14.sp, fontWeight = FontWeight.Medium)
            Text(subtitle, color = Color(0xFF808095), fontSize = 12.sp)
        }
    }
}

@SuppressLint("SetJavaScriptEnabled")
@Composable
private fun YandexOAuthDialog(
    onToken: (String) -> Unit,
    onDismiss: () -> Unit
) {
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(OledBlack)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(OledCard)
                    .padding(horizontal = 12.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text("Вход в Яндекс Музыку", color = Color.White, fontWeight = FontWeight.SemiBold, fontSize = 16.sp)
                IconButton(onClick = onDismiss) {
                    Icon(Icons.Rounded.Close, contentDescription = "Закрыть", tint = Color.White)
                }
            }
            var webViewRef by remember { mutableStateOf<WebView?>(null) }
            DisposableEffect(Unit) {
                onDispose { webViewRef?.destroy() }
            }
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { ctx ->
                    WebView(ctx).apply {
                        webViewRef = this
                        settings.javaScriptEnabled = true
                        settings.domStorageEnabled = true
                        webViewClient = object : WebViewClient() {
                            private fun checkUrl(u: String?): Boolean {
                                val tok = u?.let { YMusicStatus.extractAccessToken(it) }
                                if (!tok.isNullOrBlank()) {
                                    onToken(tok)
                                    return true
                                }
                                return false
                            }
                            override fun shouldOverrideUrlLoading(view: WebView?, request: WebResourceRequest?): Boolean {
                                return checkUrl(request?.url?.toString())
                            }
                            override fun onPageFinished(view: WebView?, url: String?) {
                                checkUrl(url)
                            }
                        }
                        loadUrl(YMusicStatus.OAUTH_URL)
                    }
                }
            )
        }
    }
}
