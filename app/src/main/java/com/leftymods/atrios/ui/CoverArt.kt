package com.leftymods.atrios.ui

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.MusicNote
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.HttpURLConnection
import java.net.URL

data class ExtractedAuraColors(val c1: Color, val c2: Color, val c3: Color)

object CoverArtCache {
    private val cache = LruCache<String, ImageBitmap>(160)
    private val colorCache = LruCache<String, ExtractedAuraColors>(160)

    fun get(url: String): ImageBitmap? = if (url.isBlank()) null else cache.get(url)
    fun getColors(url: String): ExtractedAuraColors? = if (url.isBlank()) null else colorCache.get(url)

    private fun extractColors(bmp: Bitmap): ExtractedAuraColors {
        val w = bmp.width
        val h = bmp.height
        val stepX = (w / 10).coerceAtLeast(1)
        val stepY = (h / 10).coerceAtLeast(1)
        val hsv = FloatArray(3)
        val picks = mutableListOf<Pair<Float, Int>>()
        var y = stepY / 2
        while (y < h) {
            var x = stepX / 2
            while (x < w) {
                val px = bmp.getPixel(x, y)
                android.graphics.Color.colorToHSV(px, hsv)
                val sat = hsv[1]
                val v = hsv[2]
                if (v > 0.22f) {
                    val score = sat * 1.4f + v * 0.6f
                    picks.add(score to px)
                }
                x += stepX
            }
            y += stepY
        }
        picks.sortByDescending { it.first }
        fun boost(px: Int): Color {
            val arr = FloatArray(3)
            android.graphics.Color.colorToHSV(px, arr)
            arr[1] = arr[1].coerceAtLeast(0.65f)
            arr[2] = arr[2].coerceAtLeast(0.78f)
            return Color(android.graphics.Color.HSVToColor(arr))
        }
        if (picks.isEmpty()) {
            return ExtractedAuraColors(Color(0xFFFFD800), Color(0xFFFF3D00), Color(0xFFFF1C96))
        }
        val first = picks.first().second
        val h1 = FloatArray(3).also { android.graphics.Color.colorToHSV(first, it) }[0]
        val second = picks.firstOrNull {
            val h2 = FloatArray(3).also { a -> android.graphics.Color.colorToHSV(it.second, a) }[0]
            val d = kotlin.math.abs(h1 - h2)
            kotlin.math.min(d, 360f - d) > 38f
        }?.second ?: picks[picks.size / 3].second
        val h2 = FloatArray(3).also { android.graphics.Color.colorToHSV(second, it) }[0]
        val third = picks.firstOrNull {
            val h3 = FloatArray(3).also { a -> android.graphics.Color.colorToHSV(it.second, a) }[0]
            val d1 = kotlin.math.min(kotlin.math.abs(h1 - h3), 360f - kotlin.math.abs(h1 - h3))
            val d2 = kotlin.math.min(kotlin.math.abs(h2 - h3), 360f - kotlin.math.abs(h2 - h3))
            d1 > 32f && d2 > 32f
        }?.second ?: picks[(picks.size * 2) / 3].second
        return ExtractedAuraColors(boost(first), boost(second), boost(third))
    }

    suspend fun load(url: String): ImageBitmap? {
        if (url.isBlank()) return null
        cache.get(url)?.let { return it }
        return withContext(Dispatchers.IO) {
            try {
                val conn = (URL(url).openConnection() as HttpURLConnection).apply {
                    connectTimeout = 3500
                    readTimeout = 3500
                    instanceFollowRedirects = true
                    setRequestProperty("User-Agent", "AtriOS-Control/1.19")
                }
                val bmp = conn.inputStream.use { BitmapFactory.decodeStream(it) }
                conn.disconnect()
                if (bmp != null) {
                    colorCache.put(url, extractColors(bmp))
                    bmp.asImageBitmap().also { cache.put(url, it) }
                } else null
            } catch (_: Exception) {
                null
            }
        }
    }
}

@Composable
fun AsyncCoverArt(
    url: String,
    shape: Shape,
    modifier: Modifier = Modifier,
    fallbackIcon: ImageVector = Icons.Rounded.MusicNote,
    monochromeIcon: Boolean = false
) {
    var bitmap by remember(url) { mutableStateOf(CoverArtCache.get(url)) }
    LaunchedEffect(url) {
        if (url.isNotBlank() && bitmap == null) {
            bitmap = CoverArtCache.load(url)
        }
    }
    Box(
        modifier = modifier
            .clip(shape)
            .background(
                if (monochromeIcon) Brush.linearGradient(listOf(Color(0xFF22222A), Color(0xFF141418)))
                else Brush.linearGradient(listOf(Color(0xFF1D2D50), Color(0xFF2B1E4A)))
            ),
        contentAlignment = Alignment.Center
    ) {
        val img = bitmap
        if (img != null && !monochromeIcon) {
            Image(
                bitmap = img,
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = Modifier.fillMaxSize()
            )
        } else {
            Icon(
                fallbackIcon,
                contentDescription = null,
                tint = if (monochromeIcon) Color.White else MaterialTheme.colorScheme.primary.copy(alpha = 0.85f),
                modifier = Modifier.size(if (monochromeIcon) 28.dp else 22.dp)
            )
        }
    }
}
