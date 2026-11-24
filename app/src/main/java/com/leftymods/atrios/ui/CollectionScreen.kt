package com.leftymods.atrios.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.automirrored.rounded.PlaylistPlay
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.leftymods.atrios.R
import com.leftymods.atrios.model.YMusicAlbum
import com.leftymods.atrios.model.YMusicArtist
import com.leftymods.atrios.model.YMusicCollection
import com.leftymods.atrios.model.YMusicPlaylist
import com.leftymods.atrios.model.YMusicStatus
import com.leftymods.atrios.model.YMusicTrack
import com.leftymods.atrios.ui.theme.AppPalette
import com.leftymods.atrios.ui.theme.OledBlack
import com.leftymods.atrios.ui.theme.OledCard
import com.leftymods.atrios.ui.theme.OledCardHigh
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

private enum class CollectionSubPage {
    MAIN, LIKES, PLAYLISTS, ALBUMS, ARTISTS
}

private fun pluralTracks(count: Int): String {
    val n = count % 100
    val last = count % 10
    val word = when {
        n in 11..19 -> "треков"
        last == 1 -> "трек"
        last in 2..4 -> "трека"
        else -> "треков"
    }
    return "$count $word"
}

@Composable
private fun CollectionHeartAura(
    accent: Color,
    isPlaying: Boolean,
    onClick: () -> Unit
) {
    val inf = rememberInfiniteTransition(label = "collectionHeart")
    val pulse by inf.animateFloat(
        initialValue = 0.94f,
        targetValue = 1.07f,
        animationSpec = infiniteRepeatable(
            animation = tween(if (isPlaying) 1400 else 2800, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "pulse"
    )
    val glowAlpha by inf.animateFloat(
        initialValue = 0.48f,
        targetValue = 0.85f,
        animationSpec = infiniteRepeatable(
            animation = tween(if (isPlaying) 1600 else 3200, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "glow"
    )

    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() }
            .padding(vertical = 8.dp)
    ) {
        Box(
            modifier = Modifier.size(168.dp),
            contentAlignment = Alignment.Center
        ) {
            Canvas(modifier = Modifier.fillMaxSize()) {
                val w = size.width
                val h = size.height
                val cx = w / 2f
                val cy = h / 2f
                val r = (w.coerceAtMost(h) * 0.46f) * pulse

                // Soft volumetric glow behind the heart (like Yandex Music "У вашей музыки есть цвет")
                drawCircle(
                    brush = Brush.radialGradient(
                        colors = listOf(
                            accent.copy(alpha = glowAlpha * 0.55f),
                            accent.copy(alpha = glowAlpha * 0.18f),
                            Color.Transparent
                        ),
                        center = Offset(cx, cy),
                        radius = r * 1.45f
                    ),
                    radius = r * 1.45f,
                    center = Offset(cx, cy)
                )

                // Sculpted Yandex Music style heart silhouette with center slit
                val heart = Path().apply {
                    val topY = cy - r * 0.35f
                    val botY = cy + r * 0.82f
                    moveTo(cx, botY)
                    cubicTo(
                        cx - r * 0.12f, cy + r * 0.42f,
                        cx - r * 0.98f, cy + r * 0.16f,
                        cx - r * 0.92f, topY - r * 0.08f
                    )
                    cubicTo(
                        cx - r * 0.88f, cy - r * 0.88f,
                        cx - r * 0.14f, cy - r * 0.88f,
                        cx, cy - r * 0.28f
                    )
                    cubicTo(
                        cx + r * 0.14f, cy - r * 0.88f,
                        cx + r * 0.88f, cy - r * 0.88f,
                        cx + r * 0.92f, topY - r * 0.08f
                    )
                    cubicTo(
                        cx + r * 0.98f, cy + r * 0.16f,
                        cx + r * 0.12f, cy + r * 0.42f,
                        cx, botY
                    )
                    close()
                }
                drawPath(
                    path = heart,
                    brush = Brush.radialGradient(
                        colors = listOf(
                            accent.copy(alpha = 0.95f),
                            accent.copy(alpha = 0.72f),
                            accent.copy(alpha = 0.28f)
                        ),
                        center = Offset(cx, cy - r * 0.12f),
                        radius = r * 1.15f
                    )
                )
            }
        }
        Spacer(Modifier.height(6.dp))
        Text(
            "Моя волна по коллекции",
            color = Color.White,
            fontSize = 15.sp,
            fontWeight = FontWeight.SemiBold
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CollectionScreen(
    ymusic: YMusicStatus,
    collection: YMusicCollection,
    palette: AppPalette,
    onRefresh: () -> Unit,
    onPlay: (String, String?) -> Unit,
    onOpenAccountSettings: () -> Unit
) {
    var subPage by remember { mutableStateOf(CollectionSubPage.MAIN) }
    var showColorInfoSheet by remember { mutableStateOf(false) }

    // Dynamic accent color per account/theme (Yandex yellow or active palette accent)
    val musicAccent = if (palette.isYandexOriginal) Color(0xFFFFE53B) else palette.accent

    BackHandler(enabled = subPage != CollectionSubPage.MAIN) {
        subPage = CollectionSubPage.MAIN
    }

    if (showColorInfoSheet) {
        ModalBottomSheet(
            onDismissRequest = { showColorInfoSheet = false },
            containerColor = Color(0xFF141414),
            dragHandle = { BottomSheetDefaults.DragHandle(color = Color(0xFF44444C)) }
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 24.dp, vertical = 12.dp)
                    .padding(bottom = 28.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(84.dp)
                        .clip(CircleShape)
                        .background(musicAccent),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        Icons.Rounded.Favorite,
                        contentDescription = null,
                        tint = Color.Black,
                        modifier = Modifier.size(40.dp)
                    )
                }
                Text(
                    "Меняется вместе с вами",
                    color = Color.White,
                    fontSize = 22.sp,
                    fontWeight = FontWeight.ExtraBold,
                    textAlign = TextAlign.Center
                )
                Text(
                    "Коллекция автоматически подстраивается под аккаунт Яндекс Музыки, который подключён к вашей колонке",
                    color = Color(0xFFB4B4BC),
                    fontSize = 14.sp,
                    textAlign = TextAlign.Center
                )
                Spacer(Modifier.height(4.dp))
                Button(
                    onClick = { showColorInfoSheet = false },
                    shape = RoundedCornerShape(99.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = musicAccent, contentColor = Color.Black),
                    modifier = Modifier.height(50.dp).widthIn(min = 180.dp)
                ) {
                    Text("Буду знать", fontWeight = FontWeight.Bold, fontSize = 15.sp)
                }
            }
        }
    }

    if (!ymusic.loggedIn) {
        Box(
            modifier = Modifier.fillMaxSize().background(OledBlack).statusBarsPadding().padding(24.dp),
            contentAlignment = Alignment.Center
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(14.dp)) {
                Icon(Icons.Rounded.FavoriteBorder, contentDescription = null, tint = musicAccent, modifier = Modifier.size(56.dp))
                Text("Коллекция", color = Color.White, fontSize = 26.sp, fontWeight = FontWeight.ExtraBold)
                Text(
                    "Войдите в аккаунт Яндекс Музыки, чтобы увидеть ваши любимые треки, исполнителей, плейлисты и альбомы",
                    color = Color(0xFF9E9EA8),
                    fontSize = 14.sp,
                    textAlign = TextAlign.Center
                )
                Button(
                    onClick = onOpenAccountSettings,
                    shape = RoundedCornerShape(99.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = musicAccent, contentColor = Color.Black)
                ) {
                    Text("Войти в аккаунт", fontWeight = FontWeight.Bold)
                }
            }
        }
        return
    }

    if (subPage != CollectionSubPage.MAIN) {
        CollectionDetailSubPage(
            subPage = subPage,
            ymusic = ymusic,
            collection = collection,
            accent = musicAccent,
            onBack = { subPage = CollectionSubPage.MAIN },
            onPlay = onPlay
        )
        return
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(OledBlack)
            .statusBarsPadding(),
        contentPadding = PaddingValues(bottom = 140.dp),
        verticalArrangement = Arrangement.spacedBy(20.dp)
    ) {
        // Top Header: "Коллекция" + "У вашей музыки есть цвет" + Profile & Refresh
        item {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 18.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column(modifier = Modifier.clickable { showColorInfoSheet = true }) {
                    Text(
                        "Коллекция",
                        color = Color.White,
                        fontSize = 24.sp,
                        fontWeight = FontWeight.ExtraBold
                    )
                    Text(
                        buildAnnotatedString {
                            withStyle(SpanStyle(color = Color(0xFF8E8E98))) {
                                append("У вашей музыки есть ")
                            }
                            withStyle(SpanStyle(color = musicAccent, fontWeight = FontWeight.SemiBold)) {
                                append("цвет")
                            }
                        },
                        fontSize = 13.sp
                    )
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = onOpenAccountSettings) {
                        Icon(Icons.Rounded.PersonOutline, contentDescription = "Аккаунт", tint = Color.White, modifier = Modifier.size(25.dp))
                    }
                    IconButton(onClick = onRefresh) {
                        Icon(Icons.Rounded.Refresh, contentDescription = "Обновить коллекцию", tint = Color.White, modifier = Modifier.size(23.dp))
                    }
                }
            }
        }

        // Hero: Glowing Heart Aura ("Моя волна по коллекции")
        item {
            CollectionHeartAura(
                accent = musicAccent,
                isPlaying = ymusic.state == "playing" && ymusic.mode == "likes",
                onClick = { onPlay("likes", null) }
            )
        }

        // Section 1: "Мне нравится >" + dynamic count + top tracks preview
        item {
            Column(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 18.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            if (collection.likedTracks.isNotEmpty()) subPage = CollectionSubPage.LIKES
                            else onPlay("likes", null)
                        },
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    // 3D coral-red heart tile matching Yandex Music reference
                    Box(
                        modifier = Modifier
                            .size(56.dp)
                            .clip(RoundedCornerShape(16.dp))
                            .background(
                                Brush.linearGradient(
                                    listOf(Color(0xFFFF7A59), Color(0xFFE62E4D), Color(0xFF8F1128))
                                )
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(Icons.Rounded.Favorite, contentDescription = null, tint = Color.White, modifier = Modifier.size(30.dp))
                    }
                    Column(modifier = Modifier.weight(1f)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("Мне нравится", color = Color.White, fontSize = 22.sp, fontWeight = FontWeight.ExtraBold)
                            Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, contentDescription = null, tint = Color.White, modifier = Modifier.size(24.dp))
                        }
                        Text(
                            if (collection.loaded) pluralTracks(collection.likesCount) else "Загрузка коллекции...",
                            color = Color(0xFF8E8E98),
                            fontSize = 13.sp
                        )
                    }
                    IconButton(
                        onClick = { onPlay("likes", null) },
                        modifier = Modifier
                            .size(40.dp)
                            .clip(CircleShape)
                            .background(musicAccent)
                    ) {
                        Icon(
                            painterResource(R.drawable.ic_ym_play),
                            contentDescription = "Слушать Мне нравится",
                            tint = Color.Black,
                            modifier = Modifier.size(20.dp)
                        )
                    }
                }

                if (collection.likedTracks.isNotEmpty()) {
                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        collection.likedTracks.take(4).forEach { track ->
                            CollectionTrackRow(
                                track = track,
                                isPlaying = ymusic.trackId == track.id && ymusic.state == "playing",
                                accent = musicAccent,
                                onClick = { onPlay("track", track.id) }
                            )
                        }
                    }
                } else if (!collection.loaded) {
                    Box(Modifier.fillMaxWidth().padding(vertical = 12.dp), contentAlignment = Alignment.Center) {
                        CircularProgressIndicator(color = musicAccent, strokeWidth = 2.5.dp, modifier = Modifier.size(24.dp))
                    }
                }
            }
        }

        // Section 2: "Любимые исполнители >" (dynamic per account!)
        if (collection.favoriteArtists.isNotEmpty()) {
            item {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { subPage = CollectionSubPage.ARTISTS }
                            .padding(horizontal = 18.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Любимые исполнители", color = Color.White, fontSize = 21.sp, fontWeight = FontWeight.ExtraBold)
                        Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, contentDescription = null, tint = Color.White, modifier = Modifier.size(24.dp))
                    }
                    LazyRow(
                        contentPadding = PaddingValues(horizontal = 18.dp),
                        horizontalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        itemsIndexed(collection.favoriteArtists.take(10), key = { idx, a -> "${a.id}_$idx" }) { index, artist ->
                            Column(
                                horizontalAlignment = Alignment.CenterHorizontally,
                                modifier = Modifier
                                    .width(134.dp)
                                    .clickable { onPlay("search", artist.name) }
                            ) {
                                AsyncCoverArt(
                                    url = artist.coverUrl,
                                    shape = CircleShape,
                                    modifier = Modifier
                                        .size(126.dp)
                                        .border(1.dp, Color.White.copy(alpha = 0.08f), CircleShape),
                                    fallbackIcon = Icons.Rounded.Person
                                )
                                Spacer(Modifier.height(8.dp))
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                                ) {
                                    Text(
                                        "${index + 1}",
                                        color = Color.White,
                                        fontWeight = FontWeight.Bold,
                                        fontSize = 13.sp
                                    )
                                    if (index == 0) {
                                        Icon(Icons.Rounded.WorkspacePremium, contentDescription = null, tint = musicAccent, modifier = Modifier.size(15.dp))
                                    } else {
                                        Text("—", color = Color(0xFF7A7A86), fontSize = 12.sp)
                                    }
                                }
                                Text(
                                    artist.name,
                                    color = Color.White,
                                    fontWeight = FontWeight.SemiBold,
                                    fontSize = 14.sp,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                    textAlign = TextAlign.Center
                                )
                                if (artist.trackCount > 0) {
                                    Text(
                                        pluralTracks(artist.trackCount),
                                        color = Color(0xFF8E8E98),
                                        fontSize = 12.sp
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }

        // Section 3: "Ещё у вас в Коллекции" (2-column grid matching screenshot!)
        item {
            Column(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 18.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                Text(
                    "Ещё у вас в Коллекции",
                    color = Color.White,
                    fontSize = 21.sp,
                    fontWeight = FontWeight.ExtraBold
                )

                val playlistPreview = buildString {
                    append("Мне нравится")
                    collection.playlists.take(2).forEach { pl ->
                        if (pl.title.isNotBlank() && !pl.title.equals("Мне нравится", ignoreCase = true)) {
                            append(", ").append(pl.title)
                        }
                    }
                }
                val albumPreview = collection.likedAlbums.firstOrNull()?.title ?: "Ваши альбомы"
                val albumCover = collection.likedAlbums.firstOrNull()?.coverUrl.orEmpty()

                Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
                    CollectionGridTile(
                        title = "Плейлисты",
                        subtitle = playlistPreview,
                        coverUrl = collection.playlists.firstOrNull()?.coverUrl.orEmpty(),
                        fallbackIcon = Icons.Rounded.Favorite,
                        iconGradient = listOf(Color(0xFFFF7A59), Color(0xFFD92647)),
                        modifier = Modifier.weight(1f),
                        onClick = { subPage = CollectionSubPage.PLAYLISTS }
                    )
                    CollectionGridTile(
                        title = "Альбомы",
                        subtitle = albumPreview,
                        coverUrl = albumCover,
                        fallbackIcon = Icons.Rounded.Album,
                        iconGradient = null,
                        modifier = Modifier.weight(1f),
                        onClick = { subPage = CollectionSubPage.ALBUMS }
                    )
                }

                Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
                    CollectionGridTile(
                        title = "Чарт",
                        subtitle = "Популярные треки",
                        coverUrl = "",
                        fallbackIcon = Icons.Rounded.TrendingUp,
                        iconGradient = null,
                        modifier = Modifier.weight(1f),
                        onClick = { onPlay("chart", null) }
                    )
                    CollectionGridTile(
                        title = "Моя волна",
                        subtitle = "Бесконечный поток",
                        coverUrl = "",
                        fallbackIcon = Icons.Rounded.AutoAwesome,
                        iconGradient = listOf(musicAccent.copy(alpha = 0.85f), Color(0xFFFF6D00)),
                        modifier = Modifier.weight(1f),
                        onClick = { onPlay("wave", null) }
                    )
                }
            }
        }

        // Section 4: "Ваши дизлайки >"
        item {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 18.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        "Ваши дизлайки",
                        color = Color.White,
                        fontSize = 20.sp,
                        fontWeight = FontWeight.ExtraBold
                    )
                }
                if (collection.loaded) {
                    Text(
                        pluralTracks(collection.dislikesCount),
                        color = Color(0xFF8E8E98),
                        fontSize = 13.sp
                    )
                }
            }
        }
    }
}

@Composable
private fun CollectionGridTile(
    title: String,
    subtitle: String,
    coverUrl: String,
    fallbackIcon: ImageVector,
    iconGradient: List<Color>?,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    Row(
        modifier = modifier
            .clip(RoundedCornerShape(14.dp))
            .clickable { onClick() }
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        if (coverUrl.isNotBlank()) {
            AsyncCoverArt(
                url = coverUrl,
                shape = RoundedCornerShape(12.dp),
                modifier = Modifier.size(54.dp),
                fallbackIcon = fallbackIcon
            )
        } else {
            Box(
                modifier = Modifier
                    .size(54.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(
                        if (iconGradient != null) Brush.linearGradient(iconGradient)
                        else Brush.linearGradient(listOf(Color(0xFF1D1D22), Color(0xFF141418)))
                    ),
                contentAlignment = Alignment.Center
            ) {
                Icon(fallbackIcon, contentDescription = null, tint = Color.White, modifier = Modifier.size(26.dp))
            }
        }
        Column(modifier = Modifier.weight(1f)) {
            Text(title, color = Color.White, fontWeight = FontWeight.Bold, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(subtitle, color = Color(0xFF8E8E98), fontSize = 12.sp, maxLines = 2, overflow = TextOverflow.Ellipsis, lineHeight = 15.sp)
        }
    }
}

@Composable
private fun CollectionTrackRow(
    track: YMusicTrack,
    isPlaying: Boolean,
    accent: Color,
    onClick: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .clickable { onClick() }
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        AsyncCoverArt(
            url = track.coverUrl,
            shape = RoundedCornerShape(8.dp),
            modifier = Modifier.size(48.dp),
            fallbackIcon = Icons.Rounded.MusicNote
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                track.title,
                color = if (isPlaying) accent else Color.White,
                fontWeight = FontWeight.SemiBold,
                fontSize = 15.sp,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            if (track.artist.isNotBlank()) {
                Text(
                    track.artist,
                    color = Color(0xFF8E8E98),
                    fontSize = 13.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
        Icon(
            if (isPlaying) Icons.Rounded.GraphicEq else Icons.Rounded.PlayArrow,
            contentDescription = null,
            tint = if (isPlaying) accent else Color(0xFF6E6E78),
            modifier = Modifier.size(20.dp)
        )
    }
}

@Composable
private fun CollectionDetailSubPage(
    subPage: CollectionSubPage,
    ymusic: YMusicStatus,
    collection: YMusicCollection,
    accent: Color,
    onBack: () -> Unit,
    onPlay: (String, String?) -> Unit
) {
    val pageTitle = when (subPage) {
        CollectionSubPage.LIKES -> "Мне нравится"
        CollectionSubPage.PLAYLISTS -> "Плейлисты"
        CollectionSubPage.ALBUMS -> "Альбомы"
        CollectionSubPage.ARTISTS -> "Любимые исполнители"
        CollectionSubPage.MAIN -> "Коллекция"
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(OledBlack)
            .statusBarsPadding()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = "Назад", tint = Color.White)
            }
            Text(pageTitle, color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.ExtraBold, modifier = Modifier.weight(1f))
            if (subPage == CollectionSubPage.LIKES) {
                Button(
                    onClick = { onPlay("likes", null) },
                    shape = RoundedCornerShape(99.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = accent, contentColor = Color.Black),
                    contentPadding = PaddingValues(horizontal = 16.dp, vertical = 6.dp)
                ) {
                    Text("Слушать все", fontWeight = FontWeight.Bold, fontSize = 13.sp)
                }
            }
        }

        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(horizontal = 18.dp),
            contentPadding = PaddingValues(bottom = 140.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            when (subPage) {
                CollectionSubPage.LIKES -> {
                    if (collection.likedTracks.isEmpty()) {
                        item { Text("Список «Мне нравится» пока пуст", color = Color(0xFF8E8E98)) }
                    } else {
                        itemsIndexed(collection.likedTracks, key = { idx, tr -> "${tr.id}_$idx" }) { _, tr ->
                            CollectionTrackRow(
                                track = tr,
                                isPlaying = ymusic.trackId == tr.id && ymusic.state == "playing",
                                accent = accent,
                                onClick = { onPlay("track", tr.id) }
                            )
                        }
                    }
                }
                CollectionSubPage.PLAYLISTS -> {
                    item {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(14.dp))
                                .background(OledCard)
                                .clickable { onPlay("likes", null) }
                                .padding(12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            Box(
                                modifier = Modifier
                                    .size(52.dp)
                                    .clip(RoundedCornerShape(12.dp))
                                    .background(Brush.linearGradient(listOf(Color(0xFFFF7A59), Color(0xFFD92647)))),
                                contentAlignment = Alignment.Center
                            ) {
                                Icon(Icons.Rounded.Favorite, contentDescription = null, tint = Color.White)
                            }
                            Column(Modifier.weight(1f)) {
                                Text("Мне нравится", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
                                Text(pluralTracks(collection.likesCount), color = Color(0xFF8E8E98), fontSize = 12.sp)
                            }
                            Icon(Icons.Rounded.PlayArrow, contentDescription = null, tint = accent)
                        }
                    }
                    items(collection.playlists, key = { "${it.kind}_${it.title}" }) { pl ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(14.dp))
                                .background(OledCard)
                                .clickable { onPlay("playlist", pl.title) }
                                .padding(12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            AsyncCoverArt(
                                url = pl.coverUrl,
                                shape = RoundedCornerShape(12.dp),
                                modifier = Modifier.size(52.dp),
                                fallbackIcon = Icons.AutoMirrored.Rounded.PlaylistPlay
                            )
                            Column(Modifier.weight(1f)) {
                                Text(pl.title, color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                Text(pluralTracks(pl.trackCount), color = Color(0xFF8E8E98), fontSize = 12.sp)
                            }
                            Icon(Icons.Rounded.PlayArrow, contentDescription = null, tint = accent)
                        }
                    }
                }
                CollectionSubPage.ALBUMS -> {
                    if (collection.likedAlbums.isEmpty()) {
                        item { Text("Нет сохранённых альбомов", color = Color(0xFF8E8E98)) }
                    } else {
                        itemsIndexed(collection.likedAlbums, key = { idx, al -> "${al.id}_$idx" }) { _, al ->
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(14.dp))
                                    .background(OledCard)
                                    .clickable { onPlay("album", al.id) }
                                    .padding(12.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                AsyncCoverArt(
                                    url = al.coverUrl,
                                    shape = RoundedCornerShape(12.dp),
                                    modifier = Modifier.size(52.dp),
                                    fallbackIcon = Icons.Rounded.Album
                                )
                                Column(Modifier.weight(1f)) {
                                    Text(al.title, color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    Text(
                                        buildString {
                                            append(al.artist)
                                            if (al.year > 0) { if (isNotEmpty()) append(" · "); append(al.year) }
                                        },
                                        color = Color(0xFF8E8E98),
                                        fontSize = 12.sp,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                                Icon(Icons.Rounded.PlayArrow, contentDescription = null, tint = accent)
                            }
                        }
                    }
                }
                CollectionSubPage.ARTISTS -> {
                    if (collection.favoriteArtists.isEmpty()) {
                        item { Text("Нет любимых исполнителей", color = Color(0xFF8E8E98)) }
                    } else {
                        itemsIndexed(collection.favoriteArtists, key = { idx, a -> "${a.id}_$idx" }) { idx, artist ->
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(14.dp))
                                    .background(OledCard)
                                    .clickable { onPlay("search", artist.name) }
                                    .padding(12.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                AsyncCoverArt(
                                    url = artist.coverUrl,
                                    shape = CircleShape,
                                    modifier = Modifier.size(52.dp),
                                    fallbackIcon = Icons.Rounded.Person
                                )
                                Column(Modifier.weight(1f)) {
                                    Text("${idx + 1}. ${artist.name}", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    if (artist.trackCount > 0) {
                                        Text(pluralTracks(artist.trackCount), color = Color(0xFF8E8E98), fontSize = 12.sp)
                                    }
                                }
                                Icon(Icons.Rounded.PlayArrow, contentDescription = null, tint = accent)
                            }
                        }
                    }
                }
                CollectionSubPage.MAIN -> {}
            }
        }
    }
}
