package com.leftymods.atrios.ui

import android.graphics.RuntimeShader
import android.os.Build
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.StrokeCap
import com.leftymods.atrios.ui.theme.AppPalette
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

private const val METABALL_GODRAYS_AGSL = """
uniform float2 uResolution;
uniform float uTime;
uniform float uBass;
uniform float uMid;
uniform float3 uCol1;
uniform float3 uCol2;
uniform float3 uCol3;

half4 main(float2 fragCoord) {
    float2 center = float2(uResolution.x * 0.5, uResolution.y * 0.44);
    float minDim = min(uResolution.x, uResolution.y);
    float2 rawUv = (fragCoord - center) / minDim;
    float pulse = 1.0 + uBass * 0.24 + 0.04 * sin(uTime * 2.0);
    float2 uv = rawUv / pulse;

    // 4 sub-nuclei fused into ONE cohesive organic liquid blob via smooth metaball field
    float2 p1 = float2(cos(uTime) * 0.11, sin(uTime * 2.0) * 0.08);
    float2 p2 = float2(-cos(uTime + 0.9) * 0.12, cos(uTime) * 0.09);
    float2 p3 = float2(sin(uTime * 2.0) * 0.09, -sin(uTime + 0.5) * 0.11);
    float2 p4 = float2(sin(uTime) * 0.05, cos(uTime * 2.0) * 0.05);

    float d1 = length(uv - p1);
    float d2 = length(uv - p2);
    float d3 = length(uv - p3);
    float d4 = length(uv - p4);

    float w1 = 0.052 / (d1 * d1 + 0.032);
    float w2 = 0.048 / (d2 * d2 + 0.032);
    float w3 = 0.045 / (d3 * d3 + 0.034);
    float w4 = 0.038 / (d4 * d4 + 0.028);
    float field = w1 + w2 + w3 + w4;

    // Procedural Volumetric God-Rays radiating outward in multi-harmonic directions
    float r = length(rawUv);
    float ang = atan(rawUv.y, rawUv.x);
    float ray1 = pow(max(0.0, sin(ang * 5.0 + uTime * 1.0)), 3.0);
    float ray2 = pow(max(0.0, cos(ang * 8.0 - uTime * 2.0 + 1.3)), 4.0);
    float ray3 = pow(max(0.0, sin(ang * 13.0 + uTime * 3.0)), 5.0);
    float rayMask = smoothstep(0.14, 0.28, r) * (1.0 - smoothstep(0.28, 0.72 + uBass * 0.18, r));
    float rays = (ray1 * 0.45 + ray2 * 0.35 + ray3 * 0.20) * rayMask * (0.48 + uBass * 0.85 + uMid * 0.35);

    // Crisp outer blob contour with soft glowing rim
    float body = smoothstep(1.12, 1.85, field);
    float core = smoothstep(2.15, 3.60, field);
    float halo = smoothstep(0.58, 1.25, field) * 0.40;

    // Internal multi-hue swirling inside the single blob + ray color blend
    float totalW = w1 + w2 + w3 + 0.001;
    float3 blobRgb = (uCol1 * w1 + uCol2 * w2 + uCol3 * w3) / totalW;
    float3 rayRgb = mix(uCol2, uCol1, ray1) + uCol3 * (ray2 * 0.45);

    float shimmer = (0.18 + uMid * 0.45) * core;
    blobRgb = mix(blobRgb, float3(1.0, 0.98, 0.92), clamp(shimmer, 0.0, 0.72));

    float3 finalRgb = blobRgb * (body + halo) + rayRgb * rays;
    float alpha = clamp(body + halo + rays * 0.88, 0.0, 0.97);
    return half4(half3(clamp(finalRgb, 0.0, 1.0)), half(alpha));
}
"""

@Composable
fun SingleLiquidBlobShaderAura(
    isPlaying: Boolean,
    palette: AppPalette,
    coverUrl: String = "",
    audioBass: Float = 0f,
    audioMid: Float = 0f,
    audioEnergy: Float = 0f,
    modifier: Modifier = Modifier
) {
    var extracted by remember(coverUrl) { mutableStateOf(CoverArtCache.getColors(coverUrl)) }
    LaunchedEffect(coverUrl) {
        if (coverUrl.isNotBlank()) {
            CoverArtCache.load(coverUrl)
            extracted = CoverArtCache.getColors(coverUrl)
        } else {
            extracted = null
        }
    }
    val col1 by animateColorAsState(extracted?.c1 ?: palette.aura1, tween(800), label = "bc1")
    val col2 by animateColorAsState(extracted?.c2 ?: palette.aura2, tween(800), label = "bc2")
    val col3 by animateColorAsState(extracted?.c3 ?: palette.aura3, tween(800), label = "bc3")
    val bassSmooth by animateFloatAsState(if (isPlaying) (audioBass + audioEnergy * 0.35f).coerceIn(0f, 1f) else 0f, spring(dampingRatio = 0.68f, stiffness = 260f), label = "bbs")
    val midSmooth by animateFloatAsState(if (isPlaying) audioMid else 0f, spring(dampingRatio = 0.68f, stiffness = 260f), label = "bms")

    val inf = rememberInfiniteTransition(label = "liquidBlob")
    val t by inf.animateFloat(
        initialValue = 0f,
        targetValue = (2f * PI).toFloat(),
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = if (isPlaying) 8000 else 14000, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "blobPhase"
    )

    val agslShader = remember {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            try { RuntimeShader(METABALL_GODRAYS_AGSL) } catch (_: Throwable) { null }
        } else null
    }

    Canvas(modifier = modifier) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && agslShader != null) {
            agslShader.setFloatUniform("uResolution", size.width, size.height)
            agslShader.setFloatUniform("uTime", t)
            agslShader.setFloatUniform("uBass", bassSmooth)
            agslShader.setFloatUniform("uMid", midSmooth)
            agslShader.setFloatUniform("uCol1", col1.red, col1.green, col1.blue)
            agslShader.setFloatUniform("uCol2", col2.red, col2.green, col2.blue)
            agslShader.setFloatUniform("uCol3", col3.red, col3.green, col3.blue)
            drawRect(brush = ShaderBrush(agslShader))
        } else {
            val cx = size.width * 0.5f
            val cy = size.height * 0.44f
            val r = size.minDimension * 0.44f * (1f + bassSmooth * 0.20f + 0.04f * sin(t * 2f))

            // Volumetric diagonal God-Rays fallback
            val rayCount = 12
            for (i in 0 until rayCount) {
                val baseAng = (i.toFloat() / rayCount.toFloat()) * (2f * PI.toFloat()) + t * (if (i % 2 == 0) 0.5f else -0.5f)
                val wave = 0.5f + 0.5f * sin(t * 2f + i * 1.7f)
                val rayLen = r * (1.25f + wave * 0.45f + bassSmooth * 0.35f)
                val end = Offset(cx + cos(baseAng) * rayLen, cy + sin(baseAng) * rayLen)
                val rCol = when (i % 3) { 0 -> col1; 1 -> col2; else -> col3 }
                drawLine(
                    brush = Brush.linearGradient(
                        listOf(rCol.copy(alpha = (0.28f + bassSmooth * 0.25f) * wave), Color.Transparent),
                        start = Offset(cx, cy),
                        end = end
                    ),
                    start = Offset(cx, cy),
                    end = end,
                    strokeWidth = r * (0.22f + wave * 0.14f),
                    cap = StrokeCap.Round
                )
            }

            val p1 = Offset(cx + cos(t) * r * 0.24f, cy + sin(t * 2f) * r * 0.18f)
            val p2 = Offset(cx - cos(t + 0.9f) * r * 0.25f, cy + cos(t) * r * 0.20f)
            val p3 = Offset(cx + sin(t * 2f) * r * 0.20f, cy - sin(t + 0.5f) * r * 0.22f)

            drawCircle(
                brush = Brush.radialGradient(
                    colorStops = arrayOf(
                        0.0f to col2.copy(alpha = 0.92f),
                        0.62f to col2.copy(alpha = 0.78f),
                        0.84f to col3.copy(alpha = 0.25f),
                        1.0f to Color.Transparent
                    ),
                    center = p2,
                    radius = r * 0.95f
                ),
                radius = r * 0.95f,
                center = p2
            )
            drawCircle(
                brush = Brush.radialGradient(
                    colorStops = arrayOf(
                        0.0f to col3.copy(alpha = 0.94f),
                        0.60f to col3.copy(alpha = 0.76f),
                        0.84f to col1.copy(alpha = 0.22f),
                        1.0f to Color.Transparent
                    ),
                    center = p3,
                    radius = r * 0.92f
                ),
                radius = r * 0.92f,
                center = p3
            )
            drawCircle(
                brush = Brush.radialGradient(
                    colorStops = arrayOf(
                        0.0f to Color.White.copy(alpha = (0.25f + midSmooth * 0.42f).coerceIn(0.2f, 0.75f)),
                        0.28f to col1.copy(alpha = 0.96f),
                        0.68f to col1.copy(alpha = 0.80f),
                        0.88f to col2.copy(alpha = 0.20f),
                        1.0f to Color.Transparent
                    ),
                    center = p1,
                    radius = r
                ),
                radius = r,
                center = p1
            )
        }
    }
}
