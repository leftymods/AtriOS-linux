package com.leftymods.atrios

import android.content.Context
import com.leftymods.atrios.security.KeyValueBacking
import com.leftymods.atrios.security.KeystoreSecretStore
import com.leftymods.atrios.security.SecretReadResult
import com.leftymods.atrios.security.SharedPrefsBacking
import org.json.JSONArray
import org.json.JSONObject
import java.net.URL
import java.net.URLEncoder
import java.security.SecureRandom
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.PBEKeySpec
import javax.crypto.spec.SecretKeySpec
import javax.net.ssl.HttpsURLConnection

data class BoundSpeakerInfo(
    val deviceId: String,
    val name: String,
    val pubkeyB64: String = "",
    val boundAt: Long = 0L
)

data class AtriVaultData(
    val yandexMusicToken: String = "",
    val yandexMusicLogin: String = "",
    val boundDeviceIds: List<String> = emptyList(),
    val relayDeviceId: String = "",
    val relayToken: String = "",
    val relayUrl: String = "",
    val speakerName: String = ""
) {
    fun toJson(): String = JSONObject().apply {
        put("ym_token", yandexMusicToken)
        put("ym_login", yandexMusicLogin)
        put("bound_devices", JSONArray(boundDeviceIds))
        put("relay_device_id", relayDeviceId)
        put("relay_token", relayToken)
        put("relay_url", relayUrl)
        put("speaker_name", speakerName)
    }.toString()

    companion object {
        fun fromJson(raw: String): AtriVaultData {
            if (raw.isBlank()) return AtriVaultData()
            val j = JSONObject(raw)
            val arr = j.optJSONArray("bound_devices") ?: JSONArray()
            val devs = buildList {
                for (i in 0 until arr.length()) {
                    val s = arr.optString(i, "").trim()
                    if (s.isNotEmpty()) add(s)
                }
            }
            return AtriVaultData(
                yandexMusicToken = j.optString("ym_token", ""),
                yandexMusicLogin = j.optString("ym_login", ""),
                boundDeviceIds = devs,
                relayDeviceId = j.optString("relay_device_id", ""),
                relayToken = j.optString("relay_token", ""),
                relayUrl = j.optString("relay_url", ""),
                speakerName = j.optString("speaker_name", "")
            )
        }
    }
}

data class AtriAccountState(
    val loggedIn: Boolean = false,
    val username: String = "",
    val vaultVersion: Int = 0,
    val vault: AtriVaultData = AtriVaultData(),
    val devices: List<BoundSpeakerInfo> = emptyList()
)

/**
 * Zero-Knowledge AtriOS Account & Encrypted Vault Client.
 * - Password NEVER leaves the phone.
 * - MasterKey = PBKDF2-HMAC-SHA256(password, salt, 210_000, 64 bytes)
 *   - bytes[0..31]  = AuthKey (sent only on register/login; scrypt-hashed on server)
 *   - bytes[32..63] = VaultKey (NEVER leaves phone; AES-256-GCM encrypts the vault blob)
 * - Local session_token, vault_key_b64, and vault_cache are stored ONLY in Android Keystore
 *   AEAD AES-256-GCM (KeystoreSecretStore) — never in plaintext SharedPreferences.
 */
class AtriAccountClient(
    private val metaBacking: KeyValueBacking,
    private val secretStore: KeystoreSecretStore
) {
    constructor(context: Context) : this(
        metaBacking = SharedPrefsBacking(
            context.applicationContext.getSharedPreferences(META_PREFS_NAME, Context.MODE_PRIVATE)
        ),
        secretStore = KeystoreSecretStore.create(context)
    )

    companion object {
        const val META_PREFS_NAME = "atrios_zk_account_v1"
        const val KEY_USERNAME = "username"
        const val KEY_VAULT_VERSION = "vault_version"
        const val KEY_SESSION_TOKEN = "session_token"
        const val KEY_VAULT_KEY_B64 = "vault_key_b64"
        const val KEY_VAULT_CACHE = "vault_cache"
        private val LEGACY_SECRET_KEYS = listOf(KEY_SESSION_TOKEN, KEY_VAULT_KEY_B64, KEY_VAULT_CACHE)
    }

    private val baseUrl = "https://ai.leftymods.tech/relay/v1/account"

    /**
     * Public client gateway identifier required by the reverse-proxy nginx layer.
     * NOT an account secret and grants ZERO account privileges (account endpoints strictly require
     * a user-bound X-Atri-Account-Session token; requests with only this header return HTTP 401).
     */
    private val gatewayClientId = "024ef8b87fe7313f981287e81326ef10b39421ee84a121cc"

    init {
        // Fail-safe migration: encrypts legacy plaintext secrets in Android Keystore, verifies
        // decrypt read-back, and removes old plaintext keys ONLY on verified success.
        secretStore.migrateFromLegacy(metaBacking, LEGACY_SECRET_KEYS)
    }

    private fun toHex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it) }
    private fun fromHex(hex: String): ByteArray {
        val clean = hex.trim()
        val out = ByteArray(clean.length / 2)
        for (i in out.indices) {
            out[i] = clean.substring(i * 2, i * 2 + 2).toInt(16).toByte()
        }
        return out
    }

    private fun deriveKeys(password: String, saltHex: String, iterations: Int): Pair<String, ByteArray> {
        val spec = PBEKeySpec(password.toCharArray(), fromHex(saltHex), iterations.coerceIn(100_000, 600_000), 512)
        val skf = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
        val derived = skf.generateSecret(spec).encoded
        val authKeyHex = toHex(derived.copyOfRange(0, 32))
        val vaultKey = derived.copyOfRange(32, 64)
        return authKeyHex to vaultKey
    }

    internal fun encryptVault(vault: AtriVaultData, vaultKey: ByteArray): String {
        val iv = ByteArray(12).also { SecureRandom().nextBytes(it) }
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, SecretKeySpec(vaultKey, "AES"), GCMParameterSpec(128, iv))
        val ct = cipher.doFinal(vault.toJson().toByteArray(Charsets.UTF_8))
        val packed = ByteArray(iv.size + ct.size)
        System.arraycopy(iv, 0, packed, 0, iv.size)
        System.arraycopy(ct, 0, packed, iv.size, ct.size)
        return Base64.getEncoder().encodeToString(packed)
    }

    internal fun decryptVault(blobB64: String, vaultKey: ByteArray): AtriVaultData {
        if (blobB64.isBlank()) return AtriVaultData()
        val packed = Base64.getMimeDecoder().decode(blobB64)
        if (packed.size < 28) return AtriVaultData()
        val iv = packed.copyOfRange(0, 12)
        val ct = packed.copyOfRange(12, packed.size)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, SecretKeySpec(vaultKey, "AES"), GCMParameterSpec(128, iv))
        val pt = cipher.doFinal(ct).toString(Charsets.UTF_8)
        return AtriVaultData.fromJson(pt)
    }

    private fun httpJson(method: String, path: String, body: JSONObject? = null, sessionToken: String = ""): Pair<Int, JSONObject> {
        val conn = (URL("$baseUrl$path").openConnection() as HttpsURLConnection).apply {
            requestMethod = method
            connectTimeout = 10_000
            readTimeout = 15_000
            setRequestProperty("Authorization", "Bearer $gatewayClientId")
            setRequestProperty("Accept", "application/json")
            if (sessionToken.isNotBlank()) {
                setRequestProperty("X-Atri-Account-Session", sessionToken)
            }
            if (body != null) {
                doOutput = true
                setRequestProperty("Content-Type", "application/json")
            }
        }
        try {
            if (body != null) {
                conn.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
            }
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val text = stream?.bufferedReader(Charsets.UTF_8)?.readText().orEmpty()
            val json = runCatching { JSONObject(text) }.getOrElse { JSONObject().put("error", "http_$code") }
            return code to json
        } finally {
            conn.disconnect()
        }
    }

    private fun parseDevices(arr: JSONArray?): List<BoundSpeakerInfo> {
        if (arr == null) return emptyList()
        return buildList {
            for (i in 0 until arr.length()) {
                val o = arr.optJSONObject(i) ?: continue
                add(
                    BoundSpeakerInfo(
                        deviceId = o.optString("device_id", ""),
                        name = o.optString("name", "AtriStation"),
                        pubkeyB64 = o.optString("pubkey_b64", ""),
                        boundAt = o.optLong("bound_at", 0L)
                    )
                )
            }
        }
    }

    /**
     * Clears all local account secrets (Keystore) and metadata (SharedPreferences).
     * Called on logout, account switch, or Keystore corruption/reset (fail-closed).
     */
    fun clearLocalSessionState() {
        secretStore.removeSecret(KEY_SESSION_TOKEN)
        secretStore.removeSecret(KEY_VAULT_KEY_B64)
        secretStore.removeSecret(KEY_VAULT_CACHE)
        metaBacking.clear()
    }

    internal fun saveLocalSession(username: String, sessionTok: String, vaultKey: ByteArray, vaultVer: Int, vault: AtriVaultData) {
        val prevUser = metaBacking.getString(KEY_USERNAME).orEmpty()
        if (prevUser.isNotBlank() && !prevUser.equals(username, ignoreCase = true)) {
            // Switching accounts: wipe all previous account secrets before writing new ones
            clearLocalSessionState()
        }
        val keyB64 = java.util.Base64.getEncoder().encodeToString(vaultKey)
        val okTok = secretStore.putSecret(KEY_SESSION_TOKEN, sessionTok)
        val okKey = secretStore.putSecret(KEY_VAULT_KEY_B64, keyB64)
        val okCache = secretStore.putSecret(KEY_VAULT_CACHE, vault.toJson())
        if (!okTok || !okKey || !okCache) {
            // Fail closed: never fall back to plaintext SharedPreferences!
            clearLocalSessionState()
            throw IllegalStateException("keystore_secret_write_failed")
        }
        // Only non-secret metadata goes to regular prefs (and ensure legacy plaintext keys stay absent)
        metaBacking.remove(KEY_SESSION_TOKEN)
        metaBacking.remove(KEY_VAULT_KEY_B64)
        metaBacking.remove(KEY_VAULT_CACHE)
        metaBacking.putString(KEY_USERNAME, username)
        metaBacking.putString(KEY_VAULT_VERSION, vaultVer.toString())
    }

    private fun readRequiredSecret(key: String): String? {
        return when (val r = secretStore.getSecret(key)) {
            is SecretReadResult.Found -> r.value
            is SecretReadResult.Missing -> null
            is SecretReadResult.Corrupted -> {
                // Fail closed on Keystore reset, tampered ciphertext, or restored backup without hardware key
                clearLocalSessionState()
                null
            }
        }
    }

    fun loadCachedState(): AtriAccountState {
        val u = metaBacking.getString(KEY_USERNAME).orEmpty()
        if (u.isBlank()) return AtriAccountState()
        val tok = readRequiredSecret(KEY_SESSION_TOKEN)
        val keyB64 = readRequiredSecret(KEY_VAULT_KEY_B64)
        if (tok.isNullOrBlank() || keyB64.isNullOrBlank()) {
            return AtriAccountState()
        }
        val v = when (val c = secretStore.getSecret(KEY_VAULT_CACHE)) {
            is SecretReadResult.Found -> runCatching { AtriVaultData.fromJson(c.value) }.getOrDefault(AtriVaultData())
            is SecretReadResult.Missing -> AtriVaultData()
            is SecretReadResult.Corrupted -> {
                clearLocalSessionState()
                return AtriAccountState()
            }
        }
        val ver = metaBacking.getString(KEY_VAULT_VERSION)?.toIntOrNull() ?: 1
        return AtriAccountState(
            loggedIn = true,
            username = u,
            vaultVersion = ver,
            vault = v
        )
    }

    fun register(username: String, password: String, initialVault: AtriVaultData = AtriVaultData()): Result<AtriAccountState> = runCatching {
        val u = username.trim().lowercase()
        val saltBytes = ByteArray(16).also { SecureRandom().nextBytes(it) }
        val saltHex = toHex(saltBytes)
        val iter = 210_000
        val (authKey, vaultKey) = deriveKeys(password, saltHex, iter)
        val blob = encryptVault(initialVault, vaultKey)
        val req = JSONObject().apply {
            put("username", u)
            put("kdf_salt", saltHex)
            put("kdf_iter", iter)
            put("auth_key", authKey)
            put("vault_blob", blob)
        }
        val (code, res) = httpJson("POST", "/register", req)
        if (code != 200 || !res.optBoolean("ok", false)) {
            throw IllegalStateException(res.optString("error", "register_failed_$code"))
        }
        val sessionTok = res.getString("session_token")
        val ver = res.optInt("vault_version", 1)
        val devs = parseDevices(res.optJSONArray("devices"))
        saveLocalSession(u, sessionTok, vaultKey, ver, initialVault)
        DebugLogStore.log("OK", "ACCOUNT", "Registered Zero-Knowledge AtriOS account (vault v$ver)")
        AtriAccountState(true, u, ver, initialVault, devs)
    }

    fun login(username: String, password: String): Result<AtriAccountState> = runCatching {
        val u = username.trim().lowercase()
        val enc = URLEncoder.encode(u, "UTF-8")
        val (sCode, sRes) = httpJson("GET", "/salt?username=$enc")
        if (sCode != 200 || !sRes.optBoolean("ok", false)) {
            throw IllegalStateException(sRes.optString("error", "salt_failed_$sCode"))
        }
        val saltHex = sRes.getString("kdf_salt")
        val iter = sRes.optInt("kdf_iter", 210_000)
        val (authKey, vaultKey) = deriveKeys(password, saltHex, iter)
        val req = JSONObject().apply {
            put("username", u)
            put("auth_key", authKey)
        }
        val (code, res) = httpJson("POST", "/login", req)
        if (code != 200 || !res.optBoolean("ok", false)) {
            throw IllegalStateException(res.optString("error", "login_failed_$code"))
        }
        val sessionTok = res.getString("session_token")
        val ver = res.optInt("vault_version", 1)
        val vault = decryptVault(res.optString("vault_blob", ""), vaultKey)
        val devs = parseDevices(res.optJSONArray("devices"))
        saveLocalSession(u, sessionTok, vaultKey, ver, vault)
        DebugLogStore.log("OK", "ACCOUNT", "Signed in (vault v$ver, ${devs.size} devices)")
        AtriAccountState(true, u, ver, vault, devs)
    }

    fun syncVault(): Result<AtriAccountState> = runCatching {
        val tok = readRequiredSecret(KEY_SESSION_TOKEN)
        val keyB64 = readRequiredSecret(KEY_VAULT_KEY_B64)
        if (tok.isNullOrBlank() || keyB64.isNullOrBlank()) throw IllegalStateException("not_logged_in")
        val vaultKey = java.util.Base64.getDecoder().decode(keyB64)
        val (code, res) = httpJson("GET", "/vault", null, tok)
        if (code != 200 || !res.optBoolean("ok", false)) {
            throw IllegalStateException(res.optString("error", "vault_get_$code"))
        }
        val u = res.optString("username", metaBacking.getString(KEY_USERNAME).orEmpty())
        val ver = res.optInt("vault_version", 1)
        val vault = decryptVault(res.optString("vault_blob", ""), vaultKey)
        val devs = parseDevices(res.optJSONArray("devices"))
        saveLocalSession(u, tok, vaultKey, ver, vault)
        AtriAccountState(true, u, ver, vault, devs)
    }

    fun saveVault(vault: AtriVaultData): Result<AtriAccountState> = runCatching {
        val tok = readRequiredSecret(KEY_SESSION_TOKEN)
        val keyB64 = readRequiredSecret(KEY_VAULT_KEY_B64)
        val u = metaBacking.getString(KEY_USERNAME).orEmpty()
        if (tok.isNullOrBlank() || keyB64.isNullOrBlank()) throw IllegalStateException("not_logged_in")
        val vaultKey = java.util.Base64.getDecoder().decode(keyB64)
        val blob = encryptVault(vault, vaultKey)
        val req = JSONObject().apply {
            put("vault_blob", blob)
        }
        val (code, res) = httpJson("POST", "/vault", req, tok)
        if (code != 200 || !res.optBoolean("ok", false)) {
            throw IllegalStateException(res.optString("error", "vault_put_$code"))
        }
        val prevVer = metaBacking.getString(KEY_VAULT_VERSION)?.toIntOrNull() ?: 1
        val ver = res.optInt("vault_version", prevVer + 1)
        saveLocalSession(u, tok, vaultKey, ver, vault)
        DebugLogStore.log("OK", "ACCOUNT", "Encrypted Vault synced (v$ver)")
        syncVault().getOrElse { AtriAccountState(true, u, ver, vault) }
    }

    fun bindDevice(deviceId: String, name: String, pubkeyB64: String = ""): Result<List<BoundSpeakerInfo>> = runCatching {
        val tok = readRequiredSecret(KEY_SESSION_TOKEN)
        if (tok.isNullOrBlank()) throw IllegalStateException("not_logged_in")
        val req = JSONObject().apply {
            put("device_id", deviceId)
            put("name", name)
            put("pubkey_b64", pubkeyB64)
        }
        val (code, res) = httpJson("POST", "/devices/bind", req, tok)
        if (code != 200 || !res.optBoolean("ok", false)) {
            throw IllegalStateException(res.optString("error", "bind_failed_$code"))
        }
        DebugLogStore.log("OK", "ACCOUNT", "Bound speaker ($deviceId) to AtriOS account")
        parseDevices(res.optJSONArray("devices"))
    }

    fun unbindDevice(deviceId: String): Result<List<BoundSpeakerInfo>> = runCatching {
        val tok = readRequiredSecret(KEY_SESSION_TOKEN)
        if (tok.isNullOrBlank()) throw IllegalStateException("not_logged_in")
        val req = JSONObject().apply { put("device_id", deviceId) }
        val (code, res) = httpJson("POST", "/devices/unbind", req, tok)
        if (code != 200 || !res.optBoolean("ok", false)) {
            throw IllegalStateException(res.optString("error", "unbind_failed_$code"))
        }
        parseDevices(res.optJSONArray("devices"))
    }

    fun logout() {
        val tok = when (val r = secretStore.getSecret(KEY_SESSION_TOKEN)) {
            is SecretReadResult.Found -> r.value
            else -> ""
        }
        if (tok.isNotBlank()) {
            runCatching { httpJson("POST", "/logout", JSONObject(), tok) }
        }
        clearLocalSessionState()
        DebugLogStore.log("INFO", "ACCOUNT", "Signed out of AtriOS account")
    }
}
