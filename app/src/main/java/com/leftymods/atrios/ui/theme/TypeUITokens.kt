package com.leftymods.atrios.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/** TypeUI-compliant semantic foundation tokens for AtriOS Control. */
object TypeUI {
    val SpaceXs = 4.dp
    val SpaceSm = 8.dp
    val SpaceMd = 16.dp
    val SpaceLg = 24.dp

    val RadiusSm = 12.dp
    val RadiusMd = 18.dp
    val RadiusLg = 24.dp
    val RadiusPill = 99.dp

    val Hairline = Color.White.copy(alpha = 0.09f)
    val SurfaceElevated = Color(0xFF141418)
    val SurfaceInteractive = Color(0xFF1E1E24)

    const val MotionFastMs = 180
    const val MotionMediumMs = 260
}
