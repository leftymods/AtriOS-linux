package com.leftymods.atrios.ui.theme

import android.app.Activity
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LocalRippleConfiguration
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

val OledBlack = Color(0xFF000000)
val OledCard = Color(0xFF141416)
val OledCardHigh = Color(0xFF1E1E22)

// Official Yandex Music dark theme tokens extracted from desktop client CSS:
// --ym-background-color-primary-enabled-basic: #000000
// --ym-background-color-primary-enabled-content: #121212
// --ym-background-color-primary-enabled-popover: #1a1a1a
// --ym-controls-color-primary-default-enabled: #ffff00
val YmSurfaceContent = Color(0xFF121212)
val YmSurfacePopover = Color(0xFF1A1A1A)
val YmOutline = Color(0xFF333333)

data class AppPalette(
    val id: String,
    val label: String,
    val accent: Color,
    val onAccent: Color,
    val aura1: Color,
    val aura2: Color,
    val aura3: Color,
    val surfaceBg: Color = Color(0xFF0B0B0D),
    val cardBg: Color = OledCard,
    val cardHighBg: Color = OledCardHigh,
    val borderCol: Color = Color(0xFF222228),
    val isYandexOriginal: Boolean = false
) {
    companion object {
        val YANDEX_ORIGINAL = AppPalette(
            id = "yandex_original",
            label = "Яндекс Музыка (Оригинал)",
            accent = Color(0xFFFFFF00),
            onAccent = Color(0xFF000000),
            aura1 = Color(0xFFFFD400),
            aura2 = Color(0xFFFF5500),
            aura3 = Color(0xFFFF2478),
            surfaceBg = YmSurfaceContent,
            cardBg = YmSurfacePopover,
            cardHighBg = Color(0xFF242424),
            borderCol = YmOutline,
            isYandexOriginal = true
        )
        val YANDEX = AppPalette(
            "yandex", "Яндекс Жёлтый",
            Color(0xFFFFEE00), Color(0xFF0D0D0D),
            Color(0xFFFFD800), Color(0xFFFF3D00), Color(0xFFFF1C96)
        )
        val CYAN = AppPalette(
            "cyan", "Неон Циан",
            Color(0xFF00E5FF), Color(0xFF001F26),
            Color(0xFF00E5FF), Color(0xFF2979FF), Color(0xFFB388FF)
        )
        val VIOLET = AppPalette(
            "violet", "Кибер Фиолет",
            Color(0xFFB388FF), Color(0xFF1A0047),
            Color(0xFF9C4DFF), Color(0xFFFF1F8E), Color(0xFF448AFF)
        )
        val EMERALD = AppPalette(
            "emerald", "Изумруд",
            Color(0xFF00E676), Color(0xFF002611),
            Color(0xFF00E676), Color(0xFF00BFA5), Color(0xFF64FFDA)
        )
        val CRIMSON = AppPalette(
            "crimson", "Малиновый",
            Color(0xFFFF2E7A), Color(0xFFFFFFFF),
            Color(0xFFFF2E7A), Color(0xFFFF6D00), Color(0xFFFFD600)
        )

        val entries: List<AppPalette> = listOf(YANDEX_ORIGINAL, YANDEX, CYAN, VIOLET, EMERALD, CRIMSON)

        fun custom(accentArgb: Int, secondaryArgb: Int): AppPalette {
            val acc = Color(accentArgb)
            val sec = Color(secondaryArgb)
            val onAcc = if (acc.luminance() > 0.45f) Color(0xFF000000) else Color(0xFFFFFFFF)
            val mid = Color(
                red = (acc.red * 0.55f + sec.red * 0.45f).coerceIn(0f, 1f),
                green = (acc.green * 0.55f + sec.green * 0.45f).coerceIn(0f, 1f),
                blue = (acc.blue * 0.55f + sec.blue * 0.45f).coerceIn(0f, 1f),
                alpha = 1f
            )
            return AppPalette(
                id = "custom",
                label = "Своя тема",
                accent = acc,
                onAccent = onAcc,
                aura1 = acc,
                aura2 = sec,
                aura3 = mid
            )
        }

        fun fromId(id: String?, customAccentArgb: Int = 0xFF00E5FF.toInt(), customSecondaryArgb: Int = 0xFFFF2E7A.toInt()): AppPalette {
            if (id == "custom") return custom(customAccentArgb, customSecondaryArgb)
            return entries.firstOrNull { it.id == id } ?: YANDEX_ORIGINAL
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AtriOSTheme(
    palette: AppPalette = AppPalette.YANDEX_ORIGINAL,
    content: @Composable () -> Unit
) {
    val colors = darkColorScheme(
        primary = palette.accent,
        onPrimary = palette.onAccent,
        primaryContainer = if (palette.isYandexOriginal) Color(0xFF262610) else palette.accent.copy(alpha = 0.18f),
        onPrimaryContainer = palette.accent,
        secondary = palette.aura3,
        onSecondary = Color.White,
        secondaryContainer = palette.aura3.copy(alpha = 0.18f),
        onSecondaryContainer = Color.White,
        tertiary = palette.aura2,
        onTertiary = Color.White,
        background = OledBlack,
        onBackground = if (palette.isYandexOriginal) Color(0xFFE6E6E6) else Color.White,
        surface = palette.surfaceBg,
        onSurface = if (palette.isYandexOriginal) Color(0xFFE6E6E6) else Color.White,
        surfaceVariant = palette.cardBg,
        onSurfaceVariant = if (palette.isYandexOriginal) Color(0x80FFFFFF) else Color(0xFFA0A0A8),
        surfaceContainerLowest = OledBlack,
        surfaceContainerLow = palette.cardBg,
        surfaceContainer = palette.surfaceBg,
        surfaceContainerHigh = palette.cardHighBg,
        surfaceContainerHighest = Color(0xFF2A2A2E),
        outline = palette.borderCol,
        outlineVariant = palette.borderCol
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
        colorScheme = colors
    ) {
        CompositionLocalProvider(
            LocalRippleConfiguration provides null,
            content = content
        )
    }
}
