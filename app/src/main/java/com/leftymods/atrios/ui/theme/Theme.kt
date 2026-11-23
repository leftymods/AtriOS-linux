package com.leftymods.atrios.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

private val AtriDarkColors = darkColorScheme(
    primary = Color(0xFF5EE7FF),
    onPrimary = Color(0xFF002A34),
    primaryContainer = Color(0xFF1A2942),
    onPrimaryContainer = Color(0xFFC2F3FF),
    secondary = Color(0xFFB388FF),
    onSecondary = Color(0xFF21005D),
    secondaryContainer = Color(0xFF2A2146),
    onSecondaryContainer = Color(0xFFEADDFF),
    tertiary = Color(0xFFFF7AD9),
    background = Color(0xFF0B0F1A),
    onBackground = Color(0xFFE9EEF8),
    surface = Color(0xFF111726),
    onSurface = Color(0xFFE9EEF8),
    surfaceVariant = Color(0xFF1B2338),
    onSurfaceVariant = Color(0xFFA9B6D0),
    outline = Color(0xFF33415E)
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
    background = Color(0xFFF6F8FC),
    onBackground = Color(0xFF141B29),
    surface = Color(0xFFFFFFFF),
    onSurface = Color(0xFF141B29),
    surfaceVariant = Color(0xFFE8EEF8),
    onSurfaceVariant = Color(0xFF44516A)
)

@Composable
fun AtriOSTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
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
    MaterialTheme(
        colorScheme = colors,
        content = content
    )
}
