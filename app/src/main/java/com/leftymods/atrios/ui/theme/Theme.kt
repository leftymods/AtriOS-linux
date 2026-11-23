package com.leftymods.atrios.ui.theme

import android.app.Activity
import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

private val AtriDarkColors = darkColorScheme(
    primary = Color(0xFF4CEEFF),
    onPrimary = Color(0xFF00242E),
    primaryContainer = Color(0xFF152844),
    onPrimaryContainer = Color(0xFFCFF7FF),
    secondary = Color(0xFFA78BFA),
    onSecondary = Color(0xFF1F0B52),
    secondaryContainer = Color(0xFF261E47),
    onSecondaryContainer = Color(0xFFEBE4FF),
    tertiary = Color(0xFFF472B6),
    onTertiary = Color(0xFF3B0724),
    background = Color(0xFF070B14),
    onBackground = Color(0xFFEDF2FA),
    surface = Color(0xFF0E1525),
    onSurface = Color(0xFFEDF2FA),
    surfaceVariant = Color(0xFF18223A),
    onSurfaceVariant = Color(0xFF9FB0CE),
    surfaceContainerLowest = Color(0xFF060911),
    surfaceContainerLow = Color(0xFF0C1220),
    surfaceContainer = Color(0xFF11192B),
    surfaceContainerHigh = Color(0xFF172136),
    surfaceContainerHighest = Color(0xFF1F2B45),
    outline = Color(0xFF2D3C5E),
    outlineVariant = Color(0xFF1E2942)
)

private val AtriLightColors = lightColorScheme(
    primary = Color(0xFF006780),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFB9EAFF),
    onPrimaryContainer = Color(0xFF001F29),
    secondary = Color(0xFF5E42B8),
    onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFE7DEFF),
    onSecondaryContainer = Color(0xFF1B0062),
    background = Color(0xFFF4F7FC),
    onBackground = Color(0xFF101828),
    surface = Color(0xFFFFFFFF),
    onSurface = Color(0xFF101828),
    surfaceVariant = Color(0xFFE6EDF8),
    onSurfaceVariant = Color(0xFF44516A)
)

@Composable
fun AtriOSTheme(
    darkTheme: Boolean = true,
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit
) {
    val context = LocalContext.current
    val colors = when {
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
            if (darkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        }
        darkTheme -> AtriDarkColors
        else -> AtriLightColors
    }
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as? Activity)?.window
            if (window != null) {
                window.statusBarColor = colors.background.toArgb()
                window.navigationBarColor = colors.surface.toArgb()
                WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = !darkTheme
            }
        }
    }
    MaterialTheme(
        colorScheme = colors,
        content = content
    )
}
