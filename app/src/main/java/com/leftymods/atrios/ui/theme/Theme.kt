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

val OledBlack = Color(0xFF000000)
val OledCard = Color(0xFF141416)
val OledCardHigh = Color(0xFF1E1E22)

enum class AppPalette(
    val id: String,
    val label: String,
    val accent: Color,
    val onAccent: Color,
    val aura1: Color,
    val aura2: Color,
    val aura3: Color
) {
    YANDEX(
        "yandex", "Яндекс Жёлтый",
        Color(0xFFFFEE00), Color(0xFF0D0D0D),
        Color(0xFFFFD800), Color(0xFFFF3D00), Color(0xFFFF1C96)
    ),
    CYAN(
        "cyan", "Неон Циан",
        Color(0xFF00E5FF), Color(0xFF001F26),
        Color(0xFF00E5FF), Color(0xFF2979FF), Color(0xFFB388FF)
    ),
    VIOLET(
        "violet", "Кибер Фиолет",
        Color(0xFFB388FF), Color(0xFF1A0047),
        Color(0xFF9C4DFF), Color(0xFFFF1F8E), Color(0xFF448AFF)
    ),
    EMERALD(
        "emerald", "Изумруд",
        Color(0xFF00E676), Color(0xFF002611),
        Color(0xFF00E676), Color(0xFF00BFA5), Color(0xFF64FFDA)
    ),
    CRIMSON(
        "crimson", "Малиновый",
        Color(0xFFFF2E7A), Color(0xFFFFFFFF),
        Color(0xFFFF2E7A), Color(0xFFFF6D00), Color(0xFFFFD600)
    );

    companion object {
        fun fromId(id: String?): AppPalette = entries.firstOrNull { it.id == id } ?: YANDEX
    }
}

@Composable
fun AtriOSTheme(
    palette: AppPalette = AppPalette.YANDEX,
    content: @Composable () -> Unit
) {
    val colors = darkColorScheme(
        primary = palette.accent,
        onPrimary = palette.onAccent,
        primaryContainer = palette.accent.copy(alpha = 0.18f),
        onPrimaryContainer = palette.accent,
        secondary = palette.aura3,
        onSecondary = Color.White,
        secondaryContainer = palette.aura3.copy(alpha = 0.18f),
        onSecondaryContainer = Color.White,
        tertiary = palette.aura2,
        onTertiary = Color.White,
        background = OledBlack,
        onBackground = Color.White,
        surface = Color(0xFF0B0B0D),
        onSurface = Color.White,
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
        colorScheme = colors,
        content = content
    )
}
