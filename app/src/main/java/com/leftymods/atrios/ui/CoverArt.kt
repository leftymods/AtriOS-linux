package com.leftymods.atrios.ui

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

object CoverArtCache {
    private val cache = LruCache<String, ImageBitmap>(160)

    fun get(url: String): ImageBitmap? = if (url.isBlank()) null else cache.get(url)

    suspend fun load(url: String): ImageBitmap? {
        if (url.isBlank()) return null
        cache.get(url)?.let { return it }
        return withContext(Dispatchers.IO) {
            try {
                val conn = (URL(url).openConnection() as HttpURLConnection).apply {
                    connectTimeout = 3500
                    readTimeout = 3500
                    instanceFollowRedirects = true
                    setRequestProperty("User-Agent", "AtriOS-Control/1.13")
                }
                val bmp = conn.inputStream.use { BitmapFactory.decodeStream(it) }
                conn.disconnect()
                bmp?.asImageBitmap()?.also { cache.put(url, it) }
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
    fallbackIcon: ImageVector = Icons.Rounded.MusicNote
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
                Brush.linearGradient(listOf(Color(0xFF1D2D50), Color(0xFF2B1E4A)))
            ),
        contentAlignment = Alignment.Center
    ) {
        val img = bitmap
        if (img != null) {
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
                tint = MaterialTheme.colorScheme.primary.copy(alpha = 0.85f),
                modifier = Modifier.size(22.dp)
            )
        }
    }
}
