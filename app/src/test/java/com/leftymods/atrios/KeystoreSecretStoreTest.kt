package com.leftymods.atrios

import com.leftymods.atrios.security.AeadCipherBackend
import com.leftymods.atrios.security.KeyValueBacking
import com.leftymods.atrios.security.KeystoreSecretStore
import com.leftymods.atrios.security.SecretReadResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.security.KeyPairGenerator
import java.security.SecureRandom
import java.security.Signature
import java.security.spec.ECGenParameterSpec
import java.util.Base64
import javax.crypto.AEADBadTagException
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

class KeystoreSecretStoreTest {

    private class InMemoryKeyValueBacking(
        private val failPut: Boolean = false
    ) : KeyValueBacking {
        val map = linkedMapOf<String, String>()
        override fun getString(key: String): String? = map[key]
        override fun putString(key: String, value: String): Boolean {
            if (failPut) return false
            map[key] = value
            return true
        }
        override fun remove(key: String): Boolean {
            map.remove(key)
            return true
        }
        override fun clear(): Boolean {
            map.clear()
            return true
        }
        override fun allKeys(): Set<String> = map.keys.toSet()
    }

    /**
     * Real AES-256-GCM AEAD backend matching AndroidKeyStoreAeadBackend wire format
     * (version byte 1 || 12-byte random IV || ciphertext+128-bit GCM tag, with AAD).
     */
    private class SoftwareAesGcmBackend : AeadCipherBackend {
        private val rng = SecureRandom()
        private var key: SecretKey? = generateAes256Key()
        var failEncrypt: Boolean = false

        private fun generateAes256Key(): SecretKey =
            KeyGenerator.getInstance("AES").apply { init(256, rng) }.generateKey()

        override fun encrypt(plaintext: ByteArray, aad: ByteArray): ByteArray {
            if (failEncrypt) throw IllegalStateException("simulated_keystore_encrypt_error")
            val activeKey = key ?: generateAes256Key().also { key = it }
            val iv = ByteArray(12).also { rng.nextBytes(it) }
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.ENCRYPT_MODE, activeKey, GCMParameterSpec(128, iv))
            cipher.updateAAD(aad)
            val ct = cipher.doFinal(plaintext)
            return ByteArray(1 + iv.size + ct.size).apply {
                this[0] = 1
                System.arraycopy(iv, 0, this, 1, iv.size)
                System.arraycopy(ct, 0, this, 1 + iv.size, ct.size)
            }
        }

        override fun decrypt(blob: ByteArray, aad: ByteArray): ByteArray {
            require(blob.size > 1 + 12 + 16) { "Ciphertext blob too short" }
            require(blob[0] == 1.toByte()) { "Unsupported version" }
            val activeKey = key ?: throw IllegalStateException("Keystore key missing or reset")
            val iv = blob.copyOfRange(1, 13)
            val ct = blob.copyOfRange(13, blob.size)
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, activeKey, GCMParameterSpec(128, iv))
            cipher.updateAAD(aad)
            return cipher.doFinal(ct)
        }

        override fun resetKey() {
            // Simulate Keystore reset or backup restore onto a new device without the old hardware key
            key = generateAes256Key()
        }
    }

    @Test
    fun migrationEncryptsVerifiesAndDeletesLegacyPlaintext() {
        val meta = InMemoryKeyValueBacking()
        val encBacking = InMemoryKeyValueBacking()
        val backend = SoftwareAesGcmBackend()

        val legacyVault = AtriVaultData(
            yandexMusicToken = "y0_secret_ym_token_12345",
            yandexMusicLogin = "user@example.com",
            relayDeviceId = "atri-dev-01",
            relayToken = "relay_tok_secret_999"
        )
        val vaultKey = ByteArray(32) { (it + 1).toByte() }
        val vaultKeyB64 = Base64.getEncoder().encodeToString(vaultKey)

        // Seed legacy plaintext SharedPreferences
        meta.putString(AtriAccountClient.KEY_USERNAME, "alice")
        meta.putString(AtriAccountClient.KEY_VAULT_VERSION, "3")
        meta.putString(AtriAccountClient.KEY_SESSION_TOKEN, "sess_legacy_token_abc")
        meta.putString(AtriAccountClient.KEY_VAULT_KEY_B64, vaultKeyB64)
        meta.putString(AtriAccountClient.KEY_VAULT_CACHE, legacyVault.toJson())

        val store = KeystoreSecretStore(encBacking, backend)
        val client = AtriAccountClient(meta, store)

        // Legacy plaintext keys must be removed after verified migration
        assertNull(meta.getString(AtriAccountClient.KEY_SESSION_TOKEN))
        assertNull(meta.getString(AtriAccountClient.KEY_VAULT_KEY_B64))
        assertNull(meta.getString(AtriAccountClient.KEY_VAULT_CACHE))
        // Non-secret metadata remains
        assertEquals("alice", meta.getString(AtriAccountClient.KEY_USERNAME))

        // Encrypted backing must NOT contain plaintext secret substrings
        val allEncValues = encBacking.map.values.joinToString("|")
        assertFalse(allEncValues.contains("sess_legacy_token_abc"))
        assertFalse(allEncValues.contains("y0_secret_ym_token_12345"))
        assertFalse(allEncValues.contains(vaultKeyB64))

        // Cached state loads seamlessly from encrypted Keystore store
        val state = client.loadCachedState()
        assertTrue(state.loggedIn)
        assertEquals("alice", state.username)
        assertEquals(3, state.vaultVersion)
        assertEquals("y0_secret_ym_token_12345", state.vault.yandexMusicToken)
        assertEquals("relay_tok_secret_999", state.vault.relayToken)
    }

    @Test
    fun migrationFailurePreservesLegacyAndNeverWritesPlaintext() {
        val meta = InMemoryKeyValueBacking()
        val encBacking = InMemoryKeyValueBacking()
        val backend = SoftwareAesGcmBackend().apply { failEncrypt = true }

        meta.putString("session_token", "sess_do_not_lose")
        val store = KeystoreSecretStore(encBacking, backend)

        val report = store.migrateFromLegacy(meta, listOf("session_token"))
        assertFalse(report.allSucceeded)
        assertEquals(listOf("session_token"), report.failedKeys)
        // Legacy key MUST remain intact when encrypted write+verify fails
        assertEquals("sess_do_not_lose", meta.getString("session_token"))
        // Encrypted store MUST remain empty (zero plaintext fallback)
        assertTrue(encBacking.map.isEmpty())

        // Direct saveLocalSession also fails closed without writing plaintext to meta
        val client = AtriAccountClient(meta, store)
        var threw = false
        try {
            client.saveLocalSession(
                username = "bob",
                sessionTok = "sess_bob_123",
                vaultKey = ByteArray(32) { 7 },
                vaultVer = 1,
                vault = AtriVaultData(yandexMusicToken = "ym_bob")
            )
        } catch (_: IllegalStateException) {
            threw = true
        }
        assertTrue(threw)
        assertNull(meta.getString(AtriAccountClient.KEY_SESSION_TOKEN))
        assertNull(meta.getString(AtriAccountClient.KEY_VAULT_KEY_B64))
        assertNull(meta.getString(AtriAccountClient.KEY_VAULT_CACHE))
    }

    @Test
    fun corruptedCiphertextFailsClosedAndPurgesEntry() {
        val meta = InMemoryKeyValueBacking()
        val encBacking = InMemoryKeyValueBacking()
        val backend = SoftwareAesGcmBackend()
        val store = KeystoreSecretStore(encBacking, backend)
        val client = AtriAccountClient(meta, store)

        client.saveLocalSession(
            username = "alice",
            sessionTok = "sess_valid_token",
            vaultKey = ByteArray(32) { 42 },
            vaultVer = 2,
            vault = AtriVaultData(yandexMusicToken = "ym_tok_valid")
        )
        assertTrue(client.loadCachedState().loggedIn)

        // Tamper 1 bit in the stored ciphertext/GCM tag
        val keyName = "enc_v1_${AtriAccountClient.KEY_SESSION_TOKEN}"
        val raw = Base64.getDecoder().decode(encBacking.getString(keyName)!!)
        raw[raw.lastIndex] = (raw[raw.lastIndex].toInt() xor 0x01).toByte()
        encBacking.putString(keyName, Base64.getEncoder().encodeToString(raw))

        // Reading corrupted secret must fail closed and purge the corrupted entry
        val state = client.loadCachedState()
        assertFalse(state.loggedIn)
        assertEquals("", state.username)
        assertNull(encBacking.getString(keyName))
        assertTrue(meta.map.isEmpty())
    }

    @Test
    fun aadMismatchPreventsKeySwapAttack() {
        val encBacking = InMemoryKeyValueBacking()
        val backend = SoftwareAesGcmBackend()
        val store = KeystoreSecretStore(encBacking, backend)

        assertTrue(store.putSecret("session_token", "token_value"))
        assertTrue(store.putSecret("vault_key_b64", "key_value"))

        // Copy valid ciphertext from session_token slot into vault_key_b64 slot
        val swappedBlob = encBacking.getString("enc_v1_session_token")!!
        encBacking.putString("enc_v1_vault_key_b64", swappedBlob)

        // AAD is bound to logicalKey, so swapping ciphertexts between keys MUST fail
        val read = store.getSecret("vault_key_b64")
        assertTrue(read is SecretReadResult.Corrupted)
        assertNull(encBacking.getString("enc_v1_vault_key_b64"))
    }

    @Test
    fun keystoreResetOrBackupRestoreFailsClosed() {
        val meta = InMemoryKeyValueBacking()
        val encBacking = InMemoryKeyValueBacking()
        val backend = SoftwareAesGcmBackend()
        val store = KeystoreSecretStore(encBacking, backend)
        val client = AtriAccountClient(meta, store)

        client.saveLocalSession(
            username = "alice",
            sessionTok = "sess_alice_1",
            vaultKey = ByteArray(32) { 9 },
            vaultVer = 5,
            vault = AtriVaultData(yandexMusicToken = "ym_secret_alice")
        )
        assertTrue(client.loadCachedState().loggedIn)

        // Simulate Keystore reset or restored backup XML on a device with a different hardware key
        backend.resetKey()

        val stateAfterReset = client.loadCachedState()
        assertFalse(stateAfterReset.loggedIn)
        assertEquals("", stateAfterReset.username)
        assertEquals("", stateAfterReset.vault.yandexMusicToken)
        assertTrue(encBacking.map.isEmpty())
        assertTrue(meta.map.isEmpty())
    }

    @Test
    fun logoutAndAccountSwitchWipePreviousSecrets() {
        val meta = InMemoryKeyValueBacking()
        val encBacking = InMemoryKeyValueBacking()
        val backend = SoftwareAesGcmBackend()
        val store = KeystoreSecretStore(encBacking, backend)
        val client = AtriAccountClient(meta, store)

        client.saveLocalSession(
            username = "alice",
            sessionTok = "sess_alice",
            vaultKey = ByteArray(32) { 1 },
            vaultVer = 1,
            vault = AtriVaultData(yandexMusicToken = "ym_alice", relayToken = "relay_alice")
        )
        val aliceEncSession = encBacking.getString("enc_v1_session_token")

        // Switch directly to bob -> alice's secrets must be wiped and replaced with bob's
        client.saveLocalSession(
            username = "bob",
            sessionTok = "sess_bob",
            vaultKey = ByteArray(32) { 2 },
            vaultVer = 4,
            vault = AtriVaultData(yandexMusicToken = "ym_bob", relayToken = "relay_bob")
        )
        val stateBob = client.loadCachedState()
        assertTrue(stateBob.loggedIn)
        assertEquals("bob", stateBob.username)
        assertEquals("ym_bob", stateBob.vault.yandexMusicToken)
        assertEquals("relay_bob", stateBob.vault.relayToken)
        assertNotEquals(aliceEncSession, encBacking.getString("enc_v1_session_token"))

        // Logout / clearLocalSessionState wipes everything
        client.clearLocalSessionState()
        assertFalse(client.loadCachedState().loggedIn)
        assertTrue(encBacking.map.isEmpty())
        assertTrue(meta.map.isEmpty())
    }

    @Test
    fun vaultEncryptionAndE2eeCryptoRejectTampering() {
        val client = AtriAccountClient(InMemoryKeyValueBacking(), KeystoreSecretStore(InMemoryKeyValueBacking(), SoftwareAesGcmBackend()))
        val vaultKey = ByteArray(32) { (it * 3 + 5).toByte() }
        val orig = AtriVaultData(
            yandexMusicToken = "y0_token_abc",
            yandexMusicLogin = "dev@atrios",
            boundDeviceIds = listOf("dev-1", "dev-2"),
            relayDeviceId = "dev-1",
            relayToken = "rtok_xyz"
        )
        val blob = client.encryptVault(orig, vaultKey)
        val dec = client.decryptVault(blob, vaultKey)
        assertEquals(orig, dec)

        // Tamper vault blob -> AES-GCM must reject
        val bytes = Base64.getDecoder().decode(blob)
        bytes[bytes.lastIndex] = (bytes[bytes.lastIndex].toInt() xor 0xFF).toByte()
        var badTag = false
        try {
            client.decryptVault(Base64.getEncoder().encodeToString(bytes), vaultKey)
        } catch (_: AEADBadTagException) {
            badTag = true
        }
        assertTrue(badTag)

        // E2eeCrypto all-zero check, ChaCha20+HMAC tamper check, and ECDSA P-256 verification
        assertTrue(E2eeCrypto.isAllZero(ByteArray(32)))
        assertFalse(E2eeCrypto.isAllZero(ByteArray(32).apply { this[31] = 1 }))

        val shared = ByteArray(32) { (it + 11).toByte() }
        val nonce = ByteArray(12) { (it + 1).toByte() }
        val pt = "ls -la /tmp".toByteArray(Charsets.UTF_8)
        val (ct, mac) = E2eeCrypto.encrypt(shared, nonce, pt)
        assertEquals("ls -la /tmp", String(E2eeCrypto.decrypt(shared, nonce, ct, mac), Charsets.UTF_8))

        val badMac = mac.copyOf().apply { this[0] = (this[0].toInt() xor 0x80).toByte() }
        var macFailed = false
        try {
            E2eeCrypto.decrypt(shared, nonce, ct, badMac)
        } catch (_: IllegalArgumentException) {
            macFailed = true
        }
        assertTrue(macFailed)

        val kpg = KeyPairGenerator.getInstance("EC").apply { initialize(ECGenParameterSpec("secp256r1")) }
        val kp = kpg.generateKeyPair()
        val spkiB64 = Base64.getEncoder().encodeToString(kp.public.encoded)
        val transcript = "atrios-e2ee-v1|dev-1|sid1|cpub|cnonce|spub".toByteArray(Charsets.UTF_8)
        val sig = Signature.getInstance("SHA256withECDSA").apply {
            initSign(kp.private)
            update(transcript)
        }.sign()
        val sigB64 = Base64.getEncoder().encodeToString(sig)
        assertTrue(E2eeCrypto.verifyEcdsaP256(spkiB64, transcript, sigB64))
        assertFalse(E2eeCrypto.verifyEcdsaP256(spkiB64, "tampered".toByteArray(Charsets.UTF_8), sigB64))
    }

    @Test
    fun legacyTypedIntVaultVersionConvertsSafelyOnUpgrade() {
        val rawMap = linkedMapOf<String, Any?>(
            AtriAccountClient.KEY_USERNAME to "alice",
            AtriAccountClient.KEY_VAULT_VERSION to 7, // v2.6 stored vault_version as Int
            AtriAccountClient.KEY_SESSION_TOKEN to "sess_v26_upgrade",
            AtriAccountClient.KEY_VAULT_KEY_B64 to Base64.getEncoder().encodeToString(ByteArray(32) { 5 }),
            AtriAccountClient.KEY_VAULT_CACHE to AtriVaultData(yandexMusicToken = "ym_v26").toJson()
        )
        val legacyTypedBacking = object : KeyValueBacking {
            override fun getString(key: String): String? = rawMap[key]?.toString()
            override fun putString(key: String, value: String): Boolean {
                rawMap[key] = value
                return true
            }
            override fun remove(key: String): Boolean {
                rawMap.remove(key)
                return true
            }
            override fun clear(): Boolean {
                rawMap.clear()
                return true
            }
            override fun allKeys(): Set<String> = rawMap.keys.toSet()
        }
        val encBacking = InMemoryKeyValueBacking()
        val client = AtriAccountClient(legacyTypedBacking, KeystoreSecretStore(encBacking, SoftwareAesGcmBackend()))
        val state = client.loadCachedState()
        assertTrue(state.loggedIn)
        assertEquals("alice", state.username)
        assertEquals(7, state.vaultVersion)
        assertEquals("ym_v26", state.vault.yandexMusicToken)
    }
}
