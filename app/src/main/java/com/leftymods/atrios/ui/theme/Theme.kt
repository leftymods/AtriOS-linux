package com.leftymods.atrios.ui.theme

import android.app.Activity
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

val YandexYellow = Color(0xFFFFEE00)
val YandexAmber = Color(0xFFFFB800)
val YandexOrangeRed = Color(0xFFFF3D00)
val YandexMagenta = Color(0xFFFF1F8E)
val OledBlack = Color(0xFF000000)
val OledCard = Color(0xFF141416)
val OledCardHigh = Color(0xFF1E1E22)

private val AtriYandexDarkColors = darkColorScheme(
    primary = YandexYellow,
    onPrimary = Color(0xFF0D0D0D),
    primaryContainer = Color(0xFF2B2700),
    onPrimaryContainer = Color(0xFFFFF59D),
    secondary = Color(0xFFFF5C8A),
    onSecondary = Color(0xFF1A000C),
    secondaryContainer = Color(0xFF2B1220),
    onSecondaryContainer = Color(0xFFFFD8E4),
    tertiary = Color(0xFFFF8A00),
    onTertiary = Color(0xFF1F0D00),
    background = OledBlack,
    onBackground = Color(0xFFFFFFFF),
    surface = Color(0xFF0B0B0D),
    onSurface = Color(0xFFFFFFFF),
    surfaceVariant = Color(0xFF1B1B1F),
    onSurfaceVariant = Color(0xFFA0A0A8),
    surfaceContainerLowest = OledBlack,
    surfaceContainerLow = OledCard,
    surfaceContainer = Color(0xFF18181B),
    surfaceContainerHigh = OledCardHigh,
    surfaceContainerHighest = Color(0xFF28282E),
    outline = Color(0xFF34343C),
    outlineVariant = Color(0xFF222228)
)

@Composable
fun AtriOSTheme(
    darkTheme: Boolean = true,
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit
) {
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as? Activity)?.window
            if (window != null) {
                window.statusBarColor = Color.Transparent.toArgb()
                window.navigationBarColor = Color.Transparent.toArgb()
                WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = false
            }
        }
    }
    MaterialTheme(
        colorScheme = AtriYandexDarkColors,
        content = content
    )
}
