package com.leftymods.atrios

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL

data class AppReleaseInfo(
    val version: String,
    val tagName: String,
    val notes: String,
    val apkUrl: String,
    val apkName: String,
    val apkSize: Long = 0L
)

object AppUpdater {
    private const val RELEASES_API = "https://api.github.com/repos/leftymods/AtriOS-linux/releases?per_page=15"
    private const val RELEASES_HTML = "https://github.com/leftymods/AtriOS-linux/releases"

    fun currentVersionName(context: Context): String {
        return try {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: "1.0"
        } catch (_: Exception) {
            "1.0"
        }
    }

    private fun isValidApkFile(file: File, expectedSize: Long): Boolean {
        if (!file.exists() || !file.isFile) return false
        val len = file.length()
        if (len < 100_000L) return false
        if (expectedSize > 0L && len != expectedSize) return false
        return try {
            val hdr = ByteArray(4)
            FileInputStream(file).use { it.read(hdr) } == 4 &&
                hdr[0] == 0x50.toByte() && hdr[1] == 0x4B.toByte() &&
                hdr[2] == 0x03.toByte() && hdr[3] == 0x04.toByte()
        } catch (_: Exception) {
            false
        }
    }

    fun cachedApkFile(context: Context, release: AppReleaseInfo): File {
        val dir = File(context.cacheDir, "updates").apply { mkdirs() }
        val safeVer = release.version.replace(Regex("[^0-9A-Za-z._-]"), "_")
        return File(dir, "AtriOS-Control-v$safeVer.apk")
    }

    fun hasCachedUpdate(context: Context, release: AppReleaseInfo): Boolean =
        isValidApkFile(cachedApkFile(context, release), release.apkSize)

    suspend fun checkForUpdate(context: Context): AppReleaseInfo? = withContext(Dispatchers.IO) {
        val current = currentVersionName(context)
        // 1. Try GitHub API first
        try {
            val conn = (URL(RELEASES_API).openConnection() as HttpURLConnection).apply {
                requestMethod = "GET"
                connectTimeout = 8000
                readTimeout = 8000
                setRequestProperty("Accept", "application/vnd.github+json")
                setRequestProperty("User-Agent", "AtriOS-Control")
            }
            val code = conn.responseCode
            if (code == 200) {
                val body = conn.inputStream.bufferedReader().use { it.readText() }
                conn.disconnect()
                val arr = JSONArray(body)
                var best: AppReleaseInfo? = null
                for (i in 0 until arr.length()) {
                    val rel = arr.getJSONObject(i)
                    if (rel.optBoolean("draft", false) || rel.optBoolean("prerelease", false)) continue
                    val tag = rel.optString("tag_name", "")
                    if (!tag.startsWith("app-v")) continue
                    val ver = tag.removePrefix("app-v").trim()
                    if (compareVersions(ver, current) <= 0) continue

                    val assets = rel.optJSONArray("assets") ?: continue
                    var apkUrl = ""
                    var apkName = ""
                    var apkSize = 0L
                    for (j in 0 until assets.length()) {
                        val a = assets.getJSONObject(j)
                        val name = a.optString("name", "")
                        if (name.endsWith(".apk")) {
                            apkUrl = a.optString("browser_download_url", "")
                            apkName = name
                            apkSize = a.optLong("size", 0L)
                            break
                        }
                    }
                    if (apkUrl.isBlank()) continue

                    if (best == null || compareVersions(ver, best.version) > 0) {
                        best = AppReleaseInfo(
                            version = ver,
                            tagName = tag,
                            notes = rel.optString("body", ""),
                            apkUrl = apkUrl,
                            apkName = apkName,
                            apkSize = apkSize
                        )
                    }
                }
                return@withContext best
            } else {
                conn.disconnect()
            }
        } catch (_: Exception) {
            // Fall through to HTML fallback (avoids GitHub API 403 rate limits)
        }

        // 2. Fallback: scrape GitHub Releases HTML (zero API rate limit!)
        try {
            val conn = (URL(RELEASES_HTML).openConnection() as HttpURLConnection).apply {
                requestMethod = "GET"
                connectTimeout = 8000
                readTimeout = 8000
                setRequestProperty("User-Agent", "AtriOS-Control")
            }
            if (conn.responseCode != 200) {
                conn.disconnect()
                return@withContext null
            }
            val html = conn.inputStream.bufferedReader().use { it.readText() }
            conn.disconnect()
            val matches = Regex("""/releases/tag/app-v([0-9]+(?:\.[0-9]+)*)""").findAll(html)
            var bestVer: String? = null
            for (m in matches) {
                val ver = m.groupValues[1].trim()
                if (compareVersions(ver, current) > 0 && (bestVer == null || compareVersions(ver, bestVer) > 0)) {
                    bestVer = ver
                }
            }
            if (bestVer != null) {
                val tag = "app-v$bestVer"
                val apkName = "AtriOS-Control-v$bestVer.apk"
                return@withContext AppReleaseInfo(
                    version = bestVer,
                    tagName = tag,
                    notes = "Обновление AtriOS Control v$bestVer",
                    apkUrl = "https://github.com/leftymods/AtriOS-linux/releases/download/$tag/$apkName",
                    apkName = apkName,
                    apkSize = 0L
                )
            }
        } catch (_: Exception) {
        }
        null
    }

    suspend fun downloadOnly(
        context: Context,
        release: AppReleaseInfo,
        onProgress: (Float) -> Unit = {}
    ): Result<File> = withContext(Dispatchers.IO) {
        runCatching {
            val outFile = cachedApkFile(context, release)
            val dir = outFile.parentFile!!
            if (isValidApkFile(outFile, release.apkSize)) {
                withContext(Dispatchers.Main) { onProgress(1f) }
                return@runCatching outFile
            }
            dir.listFiles()?.forEach { f ->
                if ((f.name != outFile.name && f.name.endsWith(".apk")) || f.name.endsWith(".part")) {
                    f.delete()
                }
            }
            val partFile = File(dir, "${outFile.name}.part")
            val conn = (URL(release.apkUrl).openConnection() as HttpURLConnection).apply {
                instanceFollowRedirects = true
                connectTimeout = 15000
                readTimeout = 30000
                setRequestProperty("User-Agent", "AtriOS-Control")
            }
            if (conn.responseCode !in 200..299) {
                error("HTTP ${conn.responseCode}")
            }
            val total = conn.contentLengthLong.coerceAtLeast(release.apkSize).coerceAtLeast(1L)
            var downloaded = 0L
            conn.inputStream.use { input ->
                FileOutputStream(partFile).use { output ->
                    val buf = ByteArray(16384)
                    while (true) {
                        val n = input.read(buf)
                        if (n <= 0) break
                        output.write(buf, 0, n)
                        downloaded += n
                        val frac = (downloaded.toFloat() / total.toFloat()).coerceIn(0f, 0.99f)
                        withContext(Dispatchers.Main) { onProgress(frac) }
                    }
                }
            }
            conn.disconnect()
            if (!isValidApkFile(partFile, release.apkSize)) {
                partFile.delete()
                error("Downloaded APK corrupted")
            }
            if (outFile.exists()) outFile.delete()
            if (!partFile.renameTo(outFile)) {
                partFile.copyTo(outFile, overwrite = true)
                partFile.delete()
            }
            withContext(Dispatchers.Main) { onProgress(1f) }
            outFile
        }
    }

    suspend fun downloadAndInstall(
        context: Context,
        release: AppReleaseInfo,
        onProgress: (Float) -> Unit
    ): Result<Unit> = downloadOnly(context, release, onProgress).mapCatching { apkFile ->
        withContext(Dispatchers.Main) {
            installApk(context, apkFile)
        }
    }

    fun installApk(context: Context, apkFile: File) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && !context.packageManager.canRequestPackageInstalls()) {
            val permIntent = Intent(
                Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                Uri.parse("package:${context.packageName}")
            ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(permIntent)
            return
        }
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", apkFile)
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package-archive")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        context.startActivity(intent)
    }

    private fun compareVersions(a: String, b: String): Int {
        val pa = a.split(".").map { it.toIntOrNull() ?: 0 }
        val pb = b.split(".").map { it.toIntOrNull() ?: 0 }
        val n = maxOf(pa.size, pb.size)
        for (i in 0 until n) {
            val va = pa.getOrElse(i) { 0 }
            val vb = pb.getOrElse(i) { 0 }
            if (va != vb) return va.compareTo(vb)
        }
        return 0
    }
}
