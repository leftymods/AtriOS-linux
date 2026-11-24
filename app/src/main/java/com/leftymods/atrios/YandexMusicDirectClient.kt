package com.leftymods.atrios

import com.leftymods.atrios.model.YMusicAlbum
import com.leftymods.atrios.model.YMusicArtist
import com.leftymods.atrios.model.YMusicCollection
import com.leftymods.atrios.model.YMusicGeneratedPlaylist
import com.leftymods.atrios.model.YMusicOrb
import com.leftymods.atrios.model.YMusicPlaylist
import com.leftymods.atrios.model.YMusicRemoteQueue
import com.leftymods.atrios.model.YMusicTrack
import org.json.JSONArray
import org.json.JSONObject
import java.net.URL
import java.net.URLEncoder
import javax.net.ssl.HttpsURLConnection

/**
 * Runs routine Yandex Music catalog/collection/search/orbs requests DIRECTLY on the Android phone
 * against https://api.music.yandex.net so neither the speaker CPU nor the relay server is loaded
 * with UI browsing!
 */
object YandexMusicDirectClient {
    private const val API_BASE = "https://api.music.yandex.net"
    private const val CLIENT_HDR = "YandexMusicAndroid/24023621"

    @Volatile private var cachedUid: Long = 0L
    @Volatile private var cachedUidToken: String = ""
    @Volatile private var cachedCollectionTs: Long = 0L
    @Volatile private var cachedCollectionToken: String = ""
    @Volatile private var cachedCollection: YMusicCollection? = null
    @Volatile private var cachedOrbsTs: Long = 0L
    @Volatile private var cachedOrbsToken: String = ""
    @Volatile private var cachedOrbs: List<YMusicOrb>? = null
    private val collectionLock = Any()
    private val orbsLock = Any()

    fun clearCache() {
        cachedUid = 0L
        cachedUidToken = ""
        cachedCollectionTs = 0L
        cachedCollectionToken = ""
        cachedCollection = null
        cachedOrbsTs = 0L
        cachedOrbsToken = ""
        cachedOrbs = null
    }

    private fun coverUrl(uri: String?): String {
        val u = uri?.trim().orEmpty()
        if (u.isEmpty()) return ""
        val sized = u.replace("%%", "200x200")
        return if (sized.startsWith("http")) sized else "https://$sized"
    }

    private fun apiGet(token: String, path: String): JSONObject {
        val conn = (URL("$API_BASE$path").openConnection() as HttpsURLConnection).apply {
            requestMethod = "GET"
            connectTimeout = 8000
            readTimeout = 12000
            setRequestProperty("Authorization", "OAuth $token")
            setRequestProperty("X-Yandex-Music-Client", CLIENT_HDR)
            setRequestProperty("Accept-Language", "ru")
            setRequestProperty("Accept", "application/json")
        }
        try {
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val body = stream?.bufferedReader(Charsets.UTF_8)?.readText().orEmpty()
            if (code !in 200..299) throw IllegalStateException("YM HTTP $code")
            return JSONObject(body)
        } finally {
            conn.disconnect()
        }
    }

    private fun apiPostForm(token: String, path: String, form: String): JSONObject {
        val conn = (URL("$API_BASE$path").openConnection() as HttpsURLConnection).apply {
            requestMethod = "POST"
            doOutput = true
            connectTimeout = 8000
            readTimeout = 12000
            setRequestProperty("Authorization", "OAuth $token")
            setRequestProperty("X-Yandex-Music-Client", CLIENT_HDR)
            setRequestProperty("Accept-Language", "ru")
            setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
            setRequestProperty("Accept", "application/json")
        }
        try {
            conn.outputStream.use { it.write(form.toByteArray(Charsets.UTF_8)) }
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val body = stream?.bufferedReader(Charsets.UTF_8)?.readText().orEmpty()
            if (code !in 200..299) throw IllegalStateException("YM HTTP $code")
            return JSONObject(body)
        } finally {
            conn.disconnect()
        }
    }

    private fun resolveUid(token: String, hintUid: Long): Long {
        if (hintUid > 0L) return hintUid
        if (cachedUid > 0L && cachedUidToken == token) return cachedUid
        val acc = apiGet(token, "/account/status").optJSONObject("result")?.optJSONObject("account")
        val uid = acc?.optLong("uid", 0L) ?: 0L
        if (uid > 0L) {
            cachedUid = uid
            cachedUidToken = token
        }
        return uid
    }

    private fun parseTrack(t: JSONObject): YMusicTrack? {
        val id = t.optString("id", "").trim()
        if (id.isEmpty()) return null
        val artistsArr = t.optJSONArray("artists") ?: JSONArray()
        val artist = buildList {
            for (i in 0 until artistsArr.length()) {
                val n = artistsArr.optJSONObject(i)?.optString("name", "")?.trim().orEmpty()
                if (n.isNotEmpty()) add(n)
            }
        }.joinToString(", ")
        val firstAl = t.optJSONArray("albums")?.optJSONObject(0)
        return YMusicTrack(
            id = id,
            title = t.optString("title", "Unknown Track"),
            artist = artist,
            album = firstAl?.optString("title", "").orEmpty(),
            coverUrl = coverUrl(t.optString("coverUri", "").ifBlank { firstAl?.optString("coverUri", "").orEmpty() }),
            durationMs = t.optLong("durationMs", 0L)
        )
    }

    private fun parseAlbum(a: JSONObject): YMusicAlbum? {
        val id = a.optString("id", "").trim()
        if (id.isEmpty()) return null
        val artistsArr = a.optJSONArray("artists") ?: JSONArray()
        val artist = buildList {
            for (i in 0 until artistsArr.length()) {
                val n = artistsArr.optJSONObject(i)?.optString("name", "")?.trim().orEmpty()
                if (n.isNotEmpty()) add(n)
            }
        }.joinToString(", ")
        return YMusicAlbum(
            id = id,
            title = a.optString("title", "Unknown Album"),
            artist = artist,
            year = a.optInt("year", 0),
            trackCount = a.optInt("trackCount", 0),
            coverUrl = coverUrl(a.optString("coverUri", ""))
        )
    }

    fun searchCatalog(token: String, query: String): Pair<List<YMusicTrack>, List<YMusicAlbum>> {
        val q = query.trim()
        if (q.isEmpty()) return emptyList<YMusicTrack>() to emptyList()
        val t0 = System.currentTimeMillis()
        val enc = URLEncoder.encode(q, "UTF-8")
        val res = apiGet(token, "/search?type=all&page=0&nocorrect=false&text=$enc").optJSONObject("result")
            ?: return emptyList<YMusicTrack>() to emptyList()
        val tArr = res.optJSONObject("tracks")?.optJSONArray("results") ?: JSONArray()
        val tracks = buildList {
            for (i in 0 until minOf(tArr.length(), 15)) {
                tArr.optJSONObject(i)?.let { parseTrack(it)?.let(::add) }
            }
        }
        val aArr = res.optJSONObject("albums")?.optJSONArray("results") ?: JSONArray()
        val albums = buildList {
            for (i in 0 until minOf(aArr.length(), 10)) {
                aArr.optJSONObject(i)?.let { parseAlbum(it)?.let(::add) }
            }
        }
        DebugLogStore.log("OK", "DIRECT_YM", "search('$q') -> ${tracks.size}t/${albums.size}a (${System.currentTimeMillis() - t0}ms)")
        return tracks to albums
    }

    fun fetchCollection(token: String, hintUid: Long, force: Boolean = false): YMusicCollection = synchronized(collectionLock) {
        val now = System.currentTimeMillis()
        if (!force && cachedCollection != null && cachedCollectionToken == token && now - cachedCollectionTs < 300_000L) {
            return cachedCollection!!
        }
        val t0 = System.currentTimeMillis()
        val uid = resolveUid(token, hintUid)
        if (uid <= 0L) throw IllegalStateException("YM uid not resolved")

        var likesCount = 0
        val likedTracks = mutableListOf<YMusicTrack>()
        runCatching {
            val arr = apiGet(token, "/users/$uid/likes/tracks")
                .optJSONObject("result")?.optJSONObject("library")?.optJSONArray("tracks") ?: JSONArray()
            likesCount = arr.length()
            val ids = buildList {
                for (i in 0 until minOf(arr.length(), 300)) {
                    val id = arr.optJSONObject(i)?.optString("id", "")?.trim().orEmpty()
                    if (id.isNotEmpty()) add(id)
                }
            }
            for (chunk in ids.chunked(100)) {
                val det = apiPostForm(token, "/tracks", "track-ids=${URLEncoder.encode(chunk.joinToString(","), "UTF-8")}")
                val list = det.optJSONArray("result") ?: JSONArray()
                for (i in 0 until list.length()) {
                    list.optJSONObject(i)?.let { parseTrack(it)?.let(likedTracks::add) }
                }
            }
        }

        val favArtists = mutableListOf<YMusicArtist>()
        val seenArtists = hashSetOf<String>()
        val countsByArtist = linkedMapOf<String, Pair<Int, String>>()
        for (tr in likedTracks) {
            val main = tr.artist.substringBefore(',').trim()
            if (main.isNotEmpty()) {
                val cur = countsByArtist[main]
                countsByArtist[main] = ((cur?.first ?: 0) + 1) to (cur?.second?.ifBlank { tr.coverUrl } ?: tr.coverUrl)
            }
        }
        runCatching {
            val arr = apiGet(token, "/users/$uid/likes/artists").optJSONArray("result") ?: JSONArray()
            for (i in 0 until minOf(arr.length(), 10)) {
                val item = arr.optJSONObject(i) ?: continue
                val a = item.optJSONObject("artist") ?: item
                val name = a.optString("name", "").trim()
                if (name.isEmpty() || !seenArtists.add(name.lowercase())) continue
                val cov = coverUrl(a.optJSONObject("cover")?.optString("uri", "")?.ifBlank { a.optString("ogImage", "") })
                favArtists.add(
                    YMusicArtist(
                        id = a.optString("id", name),
                        name = name,
                        coverUrl = cov,
                        trackCount = countsByArtist[name]?.first ?: 0
                    )
                )
            }
        }
        if (favArtists.size < 8) {
            countsByArtist.entries.sortedByDescending { it.value.first }.forEach { (name, pair) ->
                if (favArtists.size < 10 && seenArtists.add(name.lowercase())) {
                    favArtists.add(YMusicArtist(id = name, name = name, coverUrl = pair.second, trackCount = pair.first))
                }
            }
        }

        val playlists = mutableListOf<YMusicPlaylist>()
        runCatching {
            val arr = apiGet(token, "/users/$uid/playlists/list").optJSONArray("result") ?: JSONArray()
            for (i in 0 until minOf(arr.length(), 12)) {
                val pl = arr.optJSONObject(i) ?: continue
                val title = pl.optString("title", "").trim()
                if (title.isEmpty()) continue
                val covObj = pl.optJSONObject("cover")
                val uri = covObj?.optString("uri", "")?.ifBlank {
                    covObj.optJSONArray("itemsUri")?.optString(0, "").orEmpty()
                }?.ifBlank { pl.optString("ogImage", "") }.orEmpty()
                playlists.add(
                    YMusicPlaylist(
                        kind = pl.optInt("kind", i),
                        title = title,
                        trackCount = pl.optInt("trackCount", 0),
                        coverUrl = coverUrl(uri)
                    )
                )
            }
        }

        val likedAlbums = mutableListOf<YMusicAlbum>()
        runCatching {
            val arr = apiGet(token, "/users/$uid/likes/albums").optJSONArray("result") ?: JSONArray()
            val aids = buildList {
                for (i in 0 until minOf(arr.length(), 12)) {
                    val o = arr.optJSONObject(i) ?: continue
                    val id = o.optString("id", "").ifBlank { o.optJSONObject("album")?.optString("id", "").orEmpty() }.trim()
                    if (id.isNotEmpty()) add(id)
                }
            }
            if (aids.isNotEmpty()) {
                val det = apiPostForm(token, "/albums", "album-ids=${URLEncoder.encode(aids.joinToString(","), "UTF-8")}")
                val list = det.optJSONArray("result") ?: JSONArray()
                for (i in 0 until list.length()) {
                    list.optJSONObject(i)?.let { parseAlbum(it)?.let(likedAlbums::add) }
                }
            }
        }

        var dislikesCount = 0
        runCatching {
            val arr = apiGet(token, "/users/$uid/dislikes/tracks")
                .optJSONObject("result")?.optJSONObject("library")?.optJSONArray("tracks")
            dislikesCount = arr?.length() ?: 0
        }

        val generatedPlaylists = mutableListOf<YMusicGeneratedPlaylist>()
        runCatching {
            val arr = apiGet(token, "/feed")
                .optJSONObject("result")?.optJSONArray("generatedPlaylists") ?: JSONArray()
            for (i in 0 until arr.length()) {
                val item = arr.optJSONObject(i) ?: continue
                val gType = item.optString("type", "").trim()
                val data = item.optJSONObject("data") ?: continue
                val title = data.optString("title", "").trim()
                val plUid = data.optJSONObject("owner")?.optLong("uid", uid) ?: uid
                val kind = data.optInt("kind", 0)
                if (title.isEmpty() || kind <= 0) continue
                val covObj = data.optJSONObject("cover")
                val covUri = covObj?.optString("uri", "")?.ifBlank {
                    covObj.optJSONArray("itemsUri")?.optString(0, "").orEmpty()
                }?.ifBlank { data.optString("ogImage", "") }.orEmpty()
                generatedPlaylists.add(
                    YMusicGeneratedPlaylist(
                        type = gType,
                        uid = plUid,
                        kind = kind,
                        title = title,
                        subtitle = data.optString("description", "").trim(),
                        trackCount = data.optInt("trackCount", 0),
                        coverUrl = coverUrl(covUri)
                    )
                )
            }
        }

        val remoteQueues = mutableListOf<YMusicRemoteQueue>()
        runCatching {
            val arr = apiGet(token, "/queues")
                .optJSONObject("result")?.optJSONArray("queues") ?: JSONArray()
            for (i in 0 until arr.length()) {
                if (remoteQueues.size >= 4) break
                val q = arr.optJSONObject(i) ?: continue
                val qId = q.optString("id", "").trim()
                if (qId.isEmpty()) continue
                val ctx = q.optJSONObject("context")
                val ctxDesc = ctx?.optString("description", "")?.trim().orEmpty()
                val ctxType = ctx?.optString("type", "")?.trim().orEmpty()
                val qTitle = ctxDesc.ifBlank {
                    when (ctxType.lowercase()) {
                        "my_music" -> "Моя музыка"
                        "radio" -> "Моя волна"
                        "playlist" -> "Плейлист"
                        "album" -> "Альбом"
                        "artist" -> "Исполнитель"
                        "search" -> "Поиск"
                        else -> ctxType.ifBlank { "Очередь воспроизведения" }
                    }
                }
                val devName = q.optJSONObject("device")?.optString("name", "")?.trim()
                    ?.ifBlank { q.optString("from", "").trim() }
                    ?: q.optString("from", "").trim()
                remoteQueues.add(
                    YMusicRemoteQueue(
                        id = qId,
                        title = qTitle,
                        deviceName = devName,
                        modified = q.optString("modified", "").trim()
                    )
                )
            }
        }

        val col = YMusicCollection(
            loaded = true,
            likesCount = likesCount,
            dislikesCount = dislikesCount,
            likedTracks = likedTracks,
            favoriteArtists = favArtists,
            playlists = playlists,
            likedAlbums = likedAlbums,
            generatedPlaylists = generatedPlaylists,
            remoteQueues = remoteQueues
        )
        cachedCollection = col
        cachedCollectionToken = token
        cachedCollectionTs = System.currentTimeMillis()
        DebugLogStore.log(
            "OK",
            "DIRECT_YM",
            "collection -> $likesCount likes, ${favArtists.size} artists, ${playlists.size} pl, ${generatedPlaylists.size} feed, ${remoteQueues.size} queues (${System.currentTimeMillis() - t0}ms)"
        )
        return col
    }

    fun fetchOrbs(token: String, hintUid: Long): List<YMusicOrb> = synchronized(orbsLock) {
        val now = System.currentTimeMillis()
        if (cachedOrbs != null && cachedOrbsToken == token && now - cachedOrbsTs < 600_000L) return cachedOrbs!!
        val t0 = System.currentTimeMillis()
        val palette = listOf("#6D4AFF", "#E65C00", "#FF1F8E", "#00C896", "#3D7EFF", "#FF6D00", "#B24BF3")
        val orbs = mutableListOf<YMusicOrb>()
        var cidx = 0
        runCatching {
            val col = fetchCollection(token, hintUid)
            col.favoriteArtists.take(4).forEach { a ->
                orbs.add(YMusicOrb("artist:${a.name}", a.name, "Артист", "search", a.name, a.coverUrl, palette[cidx++ % palette.size]))
            }
        }
        runCatching {
            val arr = apiGet(token, "/rotor/stations/dashboard").optJSONObject("result")?.optJSONArray("stations") ?: JSONArray()
            for (i in 0 until minOf(arr.length(), 5)) {
                val st = arr.optJSONObject(i)?.optJSONObject("station") ?: continue
                val idObj = st.optJSONObject("id") ?: continue
                val type = idObj.optString("type", "")
                val tag = idObj.optString("tag", "")
                val name = st.optString("name", "").trim()
                if (type.isEmpty() || tag.isEmpty() || name.isEmpty() || tag == "onyourwave") continue
                val sid = "$type:$tag"
                val bg = st.optJSONObject("icon")?.optString("backgroundColor", "")?.takeIf { it.startsWith("#") }
                    ?: palette[cidx++ % palette.size]
                val cov = coverUrl(st.optString("fullImageUrl", "").ifBlank { st.optJSONObject("icon")?.optString("imageUrl", "").orEmpty() })
                orbs.add(YMusicOrb("station:$sid", name, "Волна", "station", sid, cov, bg))
            }
        }
        if (orbs.isNotEmpty()) {
            cachedOrbs = orbs
            cachedOrbsToken = token
            cachedOrbsTs = System.currentTimeMillis()
            DebugLogStore.log("OK", "DIRECT_YM", "orbs -> ${orbs.size} items (${System.currentTimeMillis() - t0}ms)")
        }
        return orbs
    }
}
