package com.leftymods.atrios.data

import android.content.Context
import com.leftymods.atrios.model.SavedSpeaker
import com.leftymods.atrios.security.KeystoreSecretStore
import com.leftymods.atrios.security.SecretReadResult
import com.leftymods.atrios.security.SharedPrefsBacking
import org.json.JSONArray


data class RelayConfig(
    val gatewayUrl: String = "https://ai.leftymods.tech",
    val deviceId: String = "",
    val token: String = ""
)

class SpeakerStorage(
    context: Context,
    private val secretStore: KeystoreSecretStore = KeystoreSecretStore.create(context)
) {
    private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    init {
        // Fail-safe migration of legacy plaintext tokens into Android Keystore AEAD AES-256-GCM.
        // Legacy keys are removed ONLY after verified encrypted read-back succeeds.
        secretStore.migrateFromLegacy(
            legacyBacking = SharedPrefsBacking(prefs),
            keys = listOf(KEY_RELAY_TOKEN, KEY_YMUSIC_TOKEN)
        )
    }

    fun loadSpeakers(): List<SavedSpeaker> {
        val raw = prefs.getString(KEY_SPEAKERS, null) ?: return emptyList()
        return try {
            val arr = JSONArray(raw)
            buildList {
                for (i in 0 until arr.length()) {
                    val obj = arr.optJSONObject(i) ?: continue
                    SavedSpeaker.fromJson(obj)?.let { add(it) }
                }
            }.sortedByDescending { it.lastSeenTs }
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun upsertSpeaker(speaker: SavedSpeaker): List<SavedSpeaker> {
        val current = loadSpeakers().filterNot { it.mac.equals(speaker.mac, ignoreCase = true) }
        val updated = (listOf(speaker) + current).sortedByDescending { it.lastSeenTs }
        saveAll(updated)
        return updated
    }

    fun removeSpeaker(mac: String): List<SavedSpeaker> {
        val updated = loadSpeakers().filterNot { it.mac.equals(mac, ignoreCase = true) }
        saveAll(updated)
        return updated
    }

    private fun saveAll(list: List<SavedSpeaker>) {
        val arr = JSONArray()
        list.forEach { arr.put(it.toJson()) }
        prefs.edit().putString(KEY_SPEAKERS, arr.toString()).apply()
    }

    private fun readSecretFailClosed(key: String): String {
        return when (val r = secretStore.getSecret(key)) {
            is SecretReadResult.Found -> r.value.trim()
            is SecretReadResult.Missing -> ""
            is SecretReadResult.Corrupted -> "" // Fail closed: corrupted entry already purged, never fall back to plaintext
        }
    }

    fun loadRelayConfig(): RelayConfig = RelayConfig(
        gatewayUrl = prefs.getString(KEY_RELAY_URL, "https://ai.leftymods.tech")
            ?.trim()?.ifBlank { "https://ai.leftymods.tech" } ?: "https://ai.leftymods.tech",
        deviceId = prefs.getString(KEY_RELAY_DID, "")?.trim().orEmpty(),
        token = readSecretFailClosed(KEY_RELAY_TOKEN)
    )

    fun saveRelayConfig(cfg: RelayConfig) {
        prefs.edit()
            .putString(KEY_RELAY_URL, cfg.gatewayUrl.trim())
            .putString(KEY_RELAY_DID, cfg.deviceId.trim())
            .remove(KEY_RELAY_TOKEN) // Ensure legacy plaintext token is never written back
            .apply()
        val tok = cfg.token.trim()
        if (tok.isEmpty()) {
            secretStore.removeSecret(KEY_RELAY_TOKEN)
        } else {
            secretStore.putSecret(KEY_RELAY_TOKEN, tok)
        }
    }

    companion object {
        private const val PREFS_NAME = "atrios_speakers"
        private const val KEY_SPEAKERS = "saved_speakers_v1"
        private const val KEY_RELAY_URL = "relay_gateway_url_v1"
        private const val KEY_RELAY_DID = "relay_device_id_v1"
        private const val KEY_RELAY_TOKEN = "relay_token_v1"
        private const val KEY_YMUSIC_TOKEN = "ymusic_token_v1"
    }

    fun loadPaletteId(): String = prefs.getString("app_palette", "yandex") ?: "yandex"

    fun savePaletteId(id: String) {
        prefs.edit().putString("app_palette", id).apply()
    }

    fun loadCustomColors(): Pair<Int, Int> {
        val accent = prefs.getInt("custom_accent_argb", 0xFF00E5FF.toInt())
        val secondary = prefs.getInt("custom_secondary_argb", 0xFFFF2E7A.toInt())
        return accent to secondary
    }

    fun saveCustomColors(accentArgb: Int, secondaryArgb: Int) {
        prefs.edit()
            .putInt("custom_accent_argb", accentArgb)
            .putInt("custom_secondary_argb", secondaryArgb)
            .apply()
    }

    fun loadYMusicToken(): String = readSecretFailClosed(KEY_YMUSIC_TOKEN)

    fun saveYMusicToken(token: String) {
        prefs.edit().remove(KEY_YMUSIC_TOKEN).apply()
        val clean = token.trim()
        if (clean.isEmpty()) {
            secretStore.removeSecret(KEY_YMUSIC_TOKEN)
        } else {
            secretStore.putSecret(KEY_YMUSIC_TOKEN, clean)
        }
    }

    /** Clears locally cached service/relay bearer tokens (used on account logout or switch). */
    fun clearSensitiveTokens() {
        prefs.edit().remove(KEY_RELAY_TOKEN).remove(KEY_YMUSIC_TOKEN).apply()
        secretStore.removeSecret(KEY_RELAY_TOKEN)
        secretStore.removeSecret(KEY_YMUSIC_TOKEN)
    }

    fun loadPinnedStationKey(deviceId: String): String {
        val did = deviceId.trim()
        if (did.isEmpty()) return ""
        return prefs.getString("e2ee_pin_$did", "")?.trim().orEmpty()
    }

    fun savePinnedStationKey(deviceId: String, identityPubB64: String) {
        val did = deviceId.trim()
        val pub = identityPubB64.trim()
        if (did.isEmpty() || pub.isEmpty()) return
        prefs.edit().putString("e2ee_pin_$did", pub).apply()
    }
}
