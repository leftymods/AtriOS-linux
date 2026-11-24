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
import androidx.compose.ui.graphics.drawscope.Stroke
import com.leftymods.atrios.ui.theme.AppPalette
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

// Liquid Plasma + Full-Spectrum 4-Band Audio-Reactive AGSL Shader
// - uBass: expands plasma body & drives volumetric god-ray bursts
// - uMid (vocals): shifts internal plasma color swirls & luminous core glow
// - uHigh (hi-hats/treble): drives high-frequency contour ripples & sparkling plasma flecks
// - uBeat (drops/transients): accelerates internal vortex & radiates a soft shockwave halo
private const val LIQUID_PLASMA_4BAND_AGSL = """
uniform float2 uResolution;
uniform float uTime;
uniform float uBass;
uniform float uMid;
uniform float uHigh;
uniform float uBeat;
uniform float3 uCol1;
uniform float3 uCol2;
uniform float3 uCol3;

half4 main(float2 fragCoord) {
    float2 center = float2(uResolution.x * 0.5, uResolution.y * 0.43);
    float minDim = min(uResolution.x, uResolution.y);
    float2 rawUv = (fragCoord - center) / minDim;

    // Beat-accelerated angular swirl + bass expansion
    float pulse = 1.0 + uBass * 0.28 + uBeat * 0.14 + 0.035 * sin(uTime * 2.0);
    float2 uv = rawUv / pulse;

    float r = length(rawUv);
    float ang = atan(rawUv.y, rawUv.x);

    // High-frequency treble contour ripple (hi-hats / snares make the plasma skin shimmer!)
    float ripple = (sin(ang * 10.0 + uTime * 4.0) * 0.5 + cos(ang * 16.0 - uTime * 5.0) * 0.5) * uHigh * 0.026;
    float2 warpedUv = uv + float2(
        sin(uv.y * 7.0 + uTime * 2.0 + uMid * 2.4) * (0.024 + uMid * 0.022),
        cos(uv.x * 7.0 - uTime * 2.0 + uBeat * 2.2) * (0.024 + uBeat * 0.022)
    );
    warpedUv *= (1.0 + ripple);

    // 5 sub-nuclei fused into ONE rich liquid plasma core
    float swirl = uTime + uBeat * 0.85;
    float2 p1 = float2(cos(swirl) * 0.12, sin(swirl * 2.0) * 0.09);
    float2 p2 = float2(-cos(swirl + 1.1) * 0.13, cos(swirl) * 0.10);
    float2 p3 = float2(sin(swirl * 2.0) * 0.10, -sin(swirl + 0.6) * 0.12);
    float2 p4 = float2(sin(swirl) * 0.06, cos(swirl * 2.0) * 0.06);
    float2 p5 = float2(-sin(swirl * 3.0) * 0.08, cos(swirl * 3.0 + 0.8) * 0.08);

    float d1 = length(warpedUv - p1);
    float d2 = length(warpedUv - p2);
    float d3 = length(warpedUv - p3);
    float d4 = length(warpedUv - p4);
    float d5 = length(warpedUv - p5);

    float w1 = 0.056 / (d1 * d1 + 0.030);
    float w2 = 0.052 / (d2 * d2 + 0.030);
    float w3 = 0.048 / (d3 * d3 + 0.032);
    float w4 = 0.042 / (d4 * d4 + 0.026);
    float w5 = 0.032 / (d5 * d5 + 0.024);
    float field = w1 + w2 + w3 + w4 + w5;

    // Volumetric God-Rays flaring outward on Bass & Beat
    float ray1 = pow(max(0.0, sin(ang * 5.0 + uTime * 1.0 + uBeat * 0.6)), 3.0);
    float ray2 = pow(max(0.0, cos(ang * 8.0 - uTime * 2.0 + 1.3)), 4.0);
    float ray3 = pow(max(0.0, sin(ang * 13.0 + uTime * 3.0)), 5.0);
    float rayMask = smoothstep(0.14, 0.29, r) * (1.0 - smoothstep(0.29, 0.76 + uBass * 0.22 + uBeat * 0.12, r));
    float rays = (ray1 * 0.46 + ray2 * 0.34 + ray3 * 0.20) * rayMask * (0.45 + uBass * 0.95 + uBeat * 0.55);

    // Beat-driven expanding shockwave ring around the plasma
    float ringPos = 0.28 + uBeat * 0.26;
    float shockRing = exp(-pow((r - ringPos) * 18.0, 2.0)) * uBeat * 0.45;

    // High-frequency sparkling plasma flecks along outer rim
    float sparkWave = pow(max(0.0, sin(ang * 23.0 + uTime * 6.0) * cos(r * 42.0 - uTime * 5.0)), 8.0);
    float sparks = sparkWave * smoothstep(0.18, 0.32, r) * (1.0 - smoothstep(0.32, 0.58, r)) * uHigh * 1.35;

    // Crisp plasma body + incandescent rim + deep glowing halo
    float body = smoothstep(1.22, 1.98, field);
    float rim = smoothstep(1.15, 1.55, field) * (1.0 - smoothstep(1.65, 2.25, field));
    float core = smoothstep(2.35, 4.10, field);
    float halo = smoothstep(0.62, 1.32, field) * (0.38 + uBass * 0.22);

    // Internal plasma color swirling (vocals shift blend between uCol1/uCol2/uCol3)
    float vShift = 0.5 + 0.5 * sin(warpedUv.x * 8.0 - warpedUv.y * 6.0 + uTime * 2.0 + uMid * 3.14);
    float totalW = w1 + w2 + w3 + w5 + 0.001;
    float3 plasmaRgb = (uCol1 * (w1 + w5 * vShift) + uCol2 * w2 + uCol3 * (w3 + w5 * (1.0 - vShift))) / totalW;
    plasmaRgb = mix(plasmaRgb, uCol2 * 1.15, clamp(uMid * 0.38 * vShift, 0.0, 0.45));

    // Chromatic hot rim + vocal core illumination
    float3 rimRgb = mix(uCol3, float3(1.0, 0.96, 0.90), 0.45 + uHigh * 0.45);
    float3 rayRgb = mix(uCol2, uCol1, ray1) + uCol3 * (ray2 * 0.5);

    float coreGlow = clamp(0.18 + uMid * 0.55 + uBeat * 0.28, 0.0, 0.85) * core;
    plasmaRgb = mix(plasmaRgb, float3(1.0, 0.98, 0.94), coreGlow);

    float3 finalRgb =
        plasmaRgb * (body + halo) +
        rimRgb * rim * (0.45 + uHigh * 0.65) +
        rayRgb * rays +
        mix(uCol1, float3(1.0, 1.0, 1.0), 0.55) * (shockRing + sparks);

    float alpha = clamp(body + halo + rays * 0.90 + shockRing + sparks, 0.0, 0.98);
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
    audioHigh: Float = 0f,
    audioEnergy: Float = 0f,
    audioBeat: Float = 0f,
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
    val col1 by animateColorAsState(extracted?.c1 ?: palette.aura1, tween(700), label = "bc1")
    val col2 by animateColorAsState(extracted?.c2 ?: palette.aura2, tween(700), label = "bc2")
    val col3 by animateColorAsState(extracted?.c3 ?: palette.aura3, tween(700), label = "bc3")
    val bassSmooth by animateFloatAsState(
        if (isPlaying) (audioBass + audioEnergy * 0.30f).coerceIn(0f, 1f) else 0f,
        spring(dampingRatio = 0.62f, stiffness = 320f),
        label = "bbs"
    )
    val midSmooth by animateFloatAsState(
        if (isPlaying) audioMid.coerceIn(0f, 1f) else 0f,
        spring(dampingRatio = 0.65f, stiffness = 300f),
        label = "bms"
    )
    val highSmooth by animateFloatAsState(
        if (isPlaying) audioHigh.coerceIn(0f, 1f) else 0f,
        spring(dampingRatio = 0.58f, stiffness = 380f),
        label = "bhs"
    )
    val beatSmooth by animateFloatAsState(
        if (isPlaying) audioBeat.coerceIn(0f, 1f) else 0f,
        spring(dampingRatio = 0.52f, stiffness = 420f),
        label = "bbt"
    )

    val inf = rememberInfiniteTransition(label = "liquidPlasma")
    val t by inf.animateFloat(
        initialValue = 0f,
        targetValue = (2f * PI).toFloat(),
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = if (isPlaying) 7000 else 13000, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "blobPhase"
    )

    val agslShader = remember {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            try { RuntimeShader(LIQUID_PLASMA_4BAND_AGSL) } catch (_: Throwable) { null }
        } else null
    }

    Canvas(modifier = modifier) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && agslShader != null) {
            agslShader.setFloatUniform("uResolution", size.width, size.height)
            agslShader.setFloatUniform("uTime", t)
            agslShader.setFloatUniform("uBass", bassSmooth)
            agslShader.setFloatUniform("uMid", midSmooth)
            agslShader.setFloatUniform("uHigh", highSmooth)
            agslShader.setFloatUniform("uBeat", beatSmooth)
            agslShader.setFloatUniform("uCol1", col1.red, col1.green, col1.blue)
            agslShader.setFloatUniform("uCol2", col2.red, col2.green, col2.blue)
            agslShader.setFloatUniform("uCol3", col3.red, col3.green, col3.blue)
            drawRect(brush = ShaderBrush(agslShader))
        } else {
            val cx = size.width * 0.5f
            val cy = size.height * 0.43f
            val r = size.minDimension * 0.44f * (1f + bassSmooth * 0.24f + beatSmooth * 0.12f + 0.04f * sin(t * 2f))

            // Volumetric God-Rays reacting to bass & beat
            val rayCount = 14
            for (i in 0 until rayCount) {
                val baseAng = (i.toFloat() / rayCount.toFloat()) * (2f * PI.toFloat()) + t * (if (i % 2 == 0) 0.55f else -0.55f)
                val wave = 0.5f + 0.5f * sin(t * 2f + i * 1.7f)
                val rayLen = r * (1.25f + wave * 0.48f + bassSmooth * 0.42f + beatSmooth * 0.25f)
                val end = Offset(cx + cos(baseAng) * rayLen, cy + sin(baseAng) * rayLen)
                val rCol = when (i % 3) { 0 -> col1; 1 -> col2; else -> col3 }
                drawLine(
                    brush = Brush.linearGradient(
                        listOf(rCol.copy(alpha = (0.28f + bassSmooth * 0.30f + beatSmooth * 0.18f) * wave), Color.Transparent),
                        start = Offset(cx, cy),
                        end = end
                    ),
                    start = Offset(cx, cy),
                    end = end,
                    strokeWidth = r * (0.20f + wave * 0.15f),
                    cap = StrokeCap.Round
                )
            }

            // Beat shockwave ring
            if (beatSmooth > 0.08f) {
                drawCircle(
                    color = col1.copy(alpha = (beatSmooth * 0.42f).coerceIn(0f, 0.5f)),
                    radius = r * (0.85f + beatSmooth * 0.55f),
                    center = Offset(cx, cy),
                    style = Stroke(width = 4f + beatSmooth * 6f)
                )
            }

            // Liquid plasma sub-nuclei
            val swirl = t + beatSmooth * 0.8f
            val p1 = Offset(cx + cos(swirl) * r * 0.24f, cy + sin(swirl * 2f) * r * 0.18f)
            val p2 = Offset(cx - cos(swirl + 0.9f) * r * 0.25f, cy + cos(swirl) * r * 0.20f)
            val p3 = Offset(cx + sin(swirl * 2f) * r * 0.20f, cy - sin(swirl + 0.5f) * r * 0.22f)

            drawCircle(
                brush = Brush.radialGradient(
                    colorStops = arrayOf(
                        0.0f to col2.copy(alpha = 0.94f),
                        0.60f to col2.copy(alpha = 0.78f),
                        0.85f to col3.copy(alpha = 0.26f),
                        1.0f to Color.Transparent
                    ),
                    center = p2,
                    radius = r * 0.96f
                ),
                radius = r * 0.96f,
                center = p2
            )
            drawCircle(
                brush = Brush.radialGradient(
                    colorStops = arrayOf(
                        0.0f to col3.copy(alpha = 0.94f),
                        0.58f to col3.copy(alpha = 0.76f),
                        0.85f to col1.copy(alpha = 0.24f),
                        1.0f to Color.Transparent
                    ),
                    center = p3,
                    radius = r * 0.93f
                ),
                radius = r * 0.93f,
                center = p3
            )
            drawCircle(
                brush = Brush.radialGradient(
                    colorStops = arrayOf(
                        0.0f to Color.White.copy(alpha = (0.24f + midSmooth * 0.48f + beatSmooth * 0.20f).coerceIn(0.2f, 0.85f)),
                        0.28f to col1.copy(alpha = 0.96f),
                        0.68f to col1.copy(alpha = 0.80f),
                        0.88f to col2.copy(alpha = 0.22f),
                        1.0f to Color.Transparent
                    ),
                    center = p1,
                    radius = r
                ),
                radius = r,
                center = p1
            )

            // High-frequency sparkle flecks around contour
            if (highSmooth > 0.12f) {
                for (s in 0 until 10) {
                    val sa = s * (2f * PI.toFloat() / 10f) + t * 2.5f
                    val sr = r * (0.72f + 0.28f * sin(t * 4f + s))
                    drawCircle(
                        color = Color.White.copy(alpha = (highSmooth * 0.75f).coerceIn(0f, 0.85f)),
                        radius = 3.5f + highSmooth * 4.5f,
                        center = Offset(cx + cos(sa) * sr, cy + sin(sa) * sr)
                    )
                }
            }
        }
    }
}
