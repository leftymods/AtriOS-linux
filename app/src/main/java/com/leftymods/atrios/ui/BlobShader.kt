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
import com.leftymods.atrios.ui.theme.AppPalette
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

// Authentic port of the official Yandex Music "Моя волна" WebGL Vibe shader
// (3D Simplex Noise snoise3 + 3D Triangle Noise triNoise3D + 3 organic light-contour layers
//  with radial audio-reactive sparks driven by Bass / Mid / High / Beat).
private const val YANDEX_OFFICIAL_VIBE_AGSL = """
uniform float2 vScreenSize;
uniform float vTime;
uniform float vScale;

uniform float3 vCol0;
uniform float3 vCol1;
uniform float3 vCol2;
uniform float3 vCol3;
uniform float3 vCol4;
uniform float3 vCol5;

uniform float vAudio0;
uniform float vAudio1;
uniform float vAudio2;
uniform float vBeat;

float4 permute(float4 x) { return mod(((x * 34.0) + 1.0) * x, 289.0); }
float4 taylorInvSqrt(float4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise3(float3 v) {
    const float2 C = float2(0.1666667, 0.3333333);
    const float4 D = float4(0.0, 0.5, 1.0, 2.0);

    float3 i = floor(v + dot(v, C.yyy));
    float3 x0 = v - i + dot(i, C.xxx);

    float3 g = step(x0.yzx, x0.xyz);
    float3 l = 1.0 - g;
    float3 i1 = min(g.xyz, l.zxy);
    float3 i2 = max(g.xyz, l.zxy);

    float3 x1 = x0 - i1 + 1.0 * C.xxx;
    float3 x2 = x0 - i2 + 2.0 * C.xxx;
    float3 x3 = x0 - 1.0 + 3.0 * C.xxx;

    i = mod(i, 289.0);
    float4 p = permute(permute(permute(
        i.z + float4(0.0, i1.z, i2.z, 1.0))
        + i.y + float4(0.0, i1.y, i2.y, 1.0))
        + i.x + float4(0.0, i1.x, i2.x, 1.0));

    float n_ = 0.142857142857;
    float3 ns = n_ * D.wyz - D.xzx;

    float4 j = p - 49.0 * floor(p * ns.z * ns.z);
    float4 x_ = floor(j * ns.z);
    float4 y_ = floor(j - 7.0 * x_);

    float4 x = x_ * ns.x + ns.yyyy;
    float4 y = y_ * ns.x + ns.yyyy;
    float4 h = 1.0 - abs(x) - abs(y);

    float4 b0 = float4(x.xy, y.xy);
    float4 b1 = float4(x.zw, y.zw);

    float4 s0 = floor(b0) * 2.0 + 1.0;
    float4 s1 = floor(b1) * 2.0 + 1.0;
    float4 sh = -step(h, float4(0.0));

    float4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    float4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    float3 p0 = float3(a0.xy, h.x);
    float3 p1 = float3(a0.zw, h.y);
    float3 p2 = float3(a1.xy, h.z);
    float3 p3 = float3(a1.zw, h.w);

    float4 norm = taylorInvSqrt(float4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    float4 m = max(0.6 - float4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, float4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

float tri(float x) { return abs(fract(x) - 0.5); }
float3 tri3(float3 p) {
    return float3(tri(p.z + tri(p.y * 20.0)), tri(p.z + tri(p.x)), tri(p.y + tri(p.x)));
}

float triNoise3D(float3 p, float spd) {
    float z = 0.4;
    float rz = 0.1;
    float3 bp = p;
    for (int i = 0; i <= 4; i++) {
        float3 dg = tri3(bp * 0.01);
        p += (dg + vTime * 0.1 * spd);
        bp *= 4.0;
        z *= 0.9;
        p *= 1.6;
        rz += (tri(p.z + tri(0.6 * p.x + 0.1 * tri(p.y)))) / z;
    }
    return smoothstep(0.0, 8.0, rz + sin(rz + sin(z) * 2.8) * 2.2);
}

float2 rot2(float2 p, float a) {
    float s = sin(a);
    float c = cos(a);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

float light(float intensity, float attenuation, float dist) {
    return intensity / (1.0 + dist + dist * attenuation);
}

float4 makeNoiseBlob2(float2 uv, float3 color1, float3 color2, float strength, float offset) {
    float len = max(length(uv), 0.0001);
    float n0 = snoise3(float3(uv * 1.2 + offset, vTime * 0.5 + offset)) * 0.5 + 0.5;
    float r0 = mix(0.0, 1.0, n0);
    float d0 = abs(len - r0);
    float v0 = smoothstep(r0 + 0.1 + (sin(vTime + offset) + 1.0), r0, len);
    float v1 = light(0.15 * (1.0 + 1.5 * (-sin(vTime * 2.0 + offset * 0.5) * 0.5)) + 0.32 * strength, 10.0, d0);
    float3 col = clamp(mix(color1, color2, uv.y * 2.0) + v1, 0.0, 1.0);
    return float4(col, v0);
}

float4 makeBlob(
    float2 uv,
    float blob,
    float3 color1,
    float3 color2,
    float width,
    float baseReaction,
    float beatReaction,
    float audioStrength,
    float offset,
    float2 noiseOffset
) {
    float len = length(uv);
    float outerRadius = blob + width * 0.5 + baseReaction * (1.0 + max(beatReaction * 0.45, audioStrength * 0.65) * 46.0 * baseReaction);
    float strength = max(beatReaction * 0.55, audioStrength);
    float4 noise = makeNoiseBlob2(uv * (1.0 - beatReaction * 0.16) + noiseOffset, color1, color2, strength, offset);
    noise.a = mix(0.0, noise.a, smoothstep(outerRadius, 0.5, len));
    noise.rgb += 0.35 * beatReaction * (1.0 - smoothstep(0.2, outerRadius * 0.8, len));
    return noise;
}

half4 main(float2 fragCoord) {
    float2 uv = fragCoord / max(vScreenSize, float2(1.0, 1.0));
    uv = uv * 2.0 - 1.0;
    float minDim = max(min(vScreenSize.x, vScreenSize.y), 1.0);
    uv.y = (uv.y + 0.10) * (vScreenSize.y / minDim) / vScale;
    uv.x = uv.x * (vScreenSize.x / minDim) / vScale;

    float2 ruv = uv * 2.0;
    float pa = atan(ruv.y, ruv.x);
    float idx = (pa / 3.14159) * 0.5;

    float2 ruv1 = rot2(uv * 2.0, 3.14159);
    float pa1 = atan(ruv1.y, ruv1.x);
    float idx1 = (pa1 / 3.14159) * 0.5;
    float idx21 = (pa1 / 3.14159 + 1.0) * 0.5 * 3.14159;

    float spark = triNoise3D(float3(idx, 0.0, 0.0), 0.15);
    spark = mix(spark, triNoise3D(float3(idx1, 0.0, idx1), 0.15), smoothstep(0.9, 1.0, sin(idx21)));
    spark = spark * 0.2 + pow(spark, 10.0);
    float gate = smoothstep(0.0, max(spark, 0.0001), 0.3);
    spark = gate * spark;

    float n0 = snoise3(float3(uv * 1.2, vTime * 0.5));
    float3 color = float3(0.0);
    float totalAlpha = 0.0;

    // Layer 0 (outermost — Bass + Beat)
    float4 b0 = makeBlob(
        uv, mix(0.95, 1.25, n0), vCol0, vCol3, 0.80,
        1.00 * spark, vBeat, vAudio0, 0.0, rot2(float2(0.18, 0.12), vTime * 0.35)
    );
    color = mix(color, b0.rgb, b0.a);
    totalAlpha = max(totalAlpha, b0.a);

    // Layer 1 (middle — Vocals / Mid + Beat)
    float4 b1 = makeBlob(
        uv, mix(0.80, 1.10, n0), vCol1, vCol4, 0.60,
        0.70 * spark, vBeat * 0.85, vAudio1, 1.57, rot2(float2(-0.15, 0.16), -vTime * 0.45)
    );
    color = mix(color, b1.rgb, b1.a);
    totalAlpha = max(totalAlpha, b1.a);

    // Layer 2 (innermost — Highs / Hi-hats + Beat)
    float4 b2 = makeBlob(
        uv, mix(0.65, 0.95, n0), vCol2, vCol5, 0.40,
        0.40 * spark, vBeat * 0.70, vAudio2, 3.14, rot2(float2(0.12, -0.18), vTime * 0.55)
    );
    color = mix(color, b2.rgb, b2.a);
    totalAlpha = max(totalAlpha, b2.a);

    return half4(half3(clamp(color, 0.0, 1.0)), half(clamp(totalAlpha, 0.0, 1.0)));
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
    val col1 by animateColorAsState(extracted?.c1 ?: palette.aura1, tween(800), label = "bc1")
    val col2 by animateColorAsState(extracted?.c2 ?: palette.aura2, tween(800), label = "bc2")
    val col3 by animateColorAsState(extracted?.c3 ?: palette.aura3, tween(800), label = "bc3")

    val bassSmooth by animateFloatAsState(
        if (isPlaying) (audioBass + audioEnergy * 0.25f).coerceIn(0f, 1f) else 0.08f,
        spring(dampingRatio = 0.72f, stiffness = 240f),
        label = "bbs"
    )
    val midSmooth by animateFloatAsState(
        if (isPlaying) audioMid.coerceIn(0f, 1f) else 0.06f,
        spring(dampingRatio = 0.72f, stiffness = 240f),
        label = "bms"
    )
    val highSmooth by animateFloatAsState(
        if (isPlaying) audioHigh.coerceIn(0f, 1f) else 0.04f,
        spring(dampingRatio = 0.68f, stiffness = 280f),
        label = "bhs"
    )
    val beatSmooth by animateFloatAsState(
        if (isPlaying) audioBeat.coerceIn(0f, 1f) else 0f,
        spring(dampingRatio = 0.60f, stiffness = 340f),
        label = "bbt"
    )

    // Continuous smooth time counter (no jump at 2*PI so snoise3 & triNoise3D flow seamlessly forever)
    var shaderTime by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(isPlaying) {
        var lastNs = withFrameNanos { it }
        while (true) {
            val nowNs = withFrameNanos { it }
            val dt = ((nowNs - lastNs) / 1_000_000_000f).coerceIn(0f, 0.05f)
            lastNs = nowNs
            val speed = if (isPlaying) 0.95f + beatSmooth * 0.65f else 0.38f
            shaderTime += dt * speed
        }
    }

    val agslShader = remember {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            try { RuntimeShader(YANDEX_OFFICIAL_VIBE_AGSL) } catch (_: Throwable) { null }
        } else null
    }

    Canvas(modifier = modifier) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && agslShader != null) {
            agslShader.setFloatUniform("vScreenSize", size.width, size.height)
            agslShader.setFloatUniform("vTime", shaderTime)
            agslShader.setFloatUniform("vScale", 0.86f + bassSmooth * 0.14f)
            agslShader.setFloatUniform("vCol0", col1.red, col1.green, col1.blue)
            agslShader.setFloatUniform("vCol1", col2.red, col2.green, col2.blue)
            agslShader.setFloatUniform("vCol2", col3.red, col3.green, col3.blue)
            agslShader.setFloatUniform("vCol3", col2.red, col2.green, col2.blue)
            agslShader.setFloatUniform("vCol4", col3.red, col3.green, col3.blue)
            agslShader.setFloatUniform("vCol5", col1.red, col1.green, col1.blue)
            agslShader.setFloatUniform("vAudio0", bassSmooth)
            agslShader.setFloatUniform("vAudio1", midSmooth)
            agslShader.setFloatUniform("vAudio2", highSmooth)
            agslShader.setFloatUniform("vBeat", beatSmooth)
            drawRect(brush = ShaderBrush(agslShader))
        } else {
            // Smooth 3-layer Yandex Vibe fallback for Android <13
            val cx = size.width * 0.5f
            val cy = size.height * 0.44f
            val baseR = size.minDimension * 0.72f * (1f + bassSmooth * 0.18f + beatSmooth * 0.10f)
            val t = shaderTime

            val p0 = Offset(cx + cos(t * 0.7f) * baseR * 0.16f, cy + sin(t * 0.9f) * baseR * 0.14f)
            val p1 = Offset(cx - cos(t * 0.8f + 1.4f) * baseR * 0.18f, cy + cos(t * 0.6f) * baseR * 0.16f)
            val p2 = Offset(cx + sin(t * 1.0f + 0.6f) * baseR * 0.15f, cy - sin(t * 0.8f) * baseR * 0.15f)

            drawCircle(
                brush = Brush.radialGradient(
                    colorStops = arrayOf(
                        0.0f to col1.copy(alpha = 0.85f),
                        0.45f to col2.copy(alpha = 0.55f),
                        0.82f to col2.copy(alpha = 0.12f),
                        1.0f to Color.Transparent
                    ),
                    center = p0,
                    radius = baseR * 1.15f
                ),
                radius = baseR * 1.15f,
                center = p0
            )
            drawCircle(
                brush = Brush.radialGradient(
                    colorStops = arrayOf(
                        0.0f to col2.copy(alpha = 0.82f),
                        0.48f to col3.copy(alpha = 0.52f),
                        0.85f to col3.copy(alpha = 0.10f),
                        1.0f to Color.Transparent
                    ),
                    center = p1,
                    radius = baseR * 0.98f
                ),
                radius = baseR * 0.98f,
                center = p1
            )
            drawCircle(
                brush = Brush.radialGradient(
                    colorStops = arrayOf(
                        0.0f to Color.White.copy(alpha = (0.20f + midSmooth * 0.35f + beatSmooth * 0.25f).coerceIn(0.15f, 0.75f)),
                        0.28f to col3.copy(alpha = 0.86f),
                        0.68f to col1.copy(alpha = 0.35f),
                        1.0f to Color.Transparent
                    ),
                    center = p2,
                    radius = baseR * 0.82f
                ),
                radius = baseR * 0.82f,
                center = p2
            )
        }
    }
}
