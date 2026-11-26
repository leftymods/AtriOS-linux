package com.leftymods.atrios.security

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.KeyStore
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

sealed class SecretReadResult {
    data class Found(val value: String) : SecretReadResult()
    data object Missing : SecretReadResult()
    data class Corrupted(val reason: String) : SecretReadResult()
}

data class MigrationReport(
    val migratedKeys: List<String>,
    val failedKeys: List<String>
) {
    val allSucceeded: Boolean get() = failedKeys.isEmpty()
}

/**
 * Abstract AEAD AES-256-GCM backend so production uses hardware-backed AndroidKeyStore
 * while JVM unit tests can exercise real AES-GCM encryption, tampering, and key resets.
 */
interface AeadCipherBackend {
    fun encrypt(plaintext: ByteArray, aad: ByteArray): ByteArray
    fun decrypt(blob: ByteArray, aad: ByteArray): ByteArray
    fun resetKey()
}

interface KeyValueBacking {
    fun getString(key: String): String?
    fun putString(key: String, value: String): Boolean
    fun remove(key: String): Boolean
    fun clear(): Boolean
    fun allKeys(): Set<String>
}

class SharedPrefsBacking(private val prefs: SharedPreferences) : KeyValueBacking {
    override fun getString(key: String): String? = try {
        prefs.getString(key, null)
    } catch (_: ClassCastException) {
        // Legacy keys (e.g. v2.6 Int vault_version) may be stored as non-String primitives
        prefs.all[key]?.toString()
    }
    override fun putString(key: String, value: String): Boolean =
        prefs.edit().putString(key, value).commit()
    override fun remove(key: String): Boolean =
        prefs.edit().remove(key).commit()
    override fun clear(): Boolean =
        prefs.edit().clear().commit()
    override fun allKeys(): Set<String> = prefs.all.keys.toSet()
}

/**
 * Hardware-backed Android Keystore AES-256-GCM AEAD backend.
 * Uses randomized 12-byte IVs and binds each ciphertext to its logical key name via AAD.
 */
class AndroidKeyStoreAeadBackend(
    private val keyAlias: String = DEFAULT_KEY_ALIAS
) : AeadCipherBackend {

    companion object {
        private const val ANDROID_KEYSTORE = "AndroidKeyStore"
        private const val TRANSFORMATION = "AES/GCM/NoPadding"
        private const val GCM_TAG_BITS = 128
        private const val IV_LEN = 12
        const val DEFAULT_KEY_ALIAS = "atrios_secret_store_aes256_gcm_v1"
    }

    private fun getOrCreateKey(): SecretKey {
        val ks = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        val existing = ks.getKey(keyAlias, null) as? SecretKey
        if (existing != null) return existing
        val kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
        val spec = KeyGenParameterSpec.Builder(
            keyAlias,
            KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT
        )
            .setKeySize(256)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .setRandomizedEncryptionRequired(true)
            .build()
        kg.init(spec)
        return kg.generateKey()
    }

    override fun encrypt(plaintext: ByteArray, aad: ByteArray): ByteArray {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey())
        cipher.updateAAD(aad)
        val iv = cipher.iv
        require(iv.size == IV_LEN) { "Unexpected GCM IV length: ${iv.size}" }
        val ct = cipher.doFinal(plaintext)
        return ByteArray(1 + IV_LEN + ct.size).apply {
            this[0] = 1 // format version 1
            System.arraycopy(iv, 0, this, 1, IV_LEN)
            System.arraycopy(ct, 0, this, 1 + IV_LEN, ct.size)
        }
    }

    override fun decrypt(blob: ByteArray, aad: ByteArray): ByteArray {
        require(blob.size > 1 + IV_LEN + 16) { "Ciphertext blob too short" }
        require(blob[0] == 1.toByte()) { "Unsupported ciphertext format version" }
        val ks = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        val key = ks.getKey(keyAlias, null) as? SecretKey
            ?: throw IllegalStateException("Keystore key missing or reset")
        val iv = blob.copyOfRange(1, 1 + IV_LEN)
        val ct = blob.copyOfRange(1 + IV_LEN, blob.size)
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(GCM_TAG_BITS, iv))
        cipher.updateAAD(aad)
        return cipher.doFinal(ct)
    }

    override fun resetKey() {
        runCatching {
            val ks = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
            if (ks.containsAlias(keyAlias)) {
                ks.deleteEntry(keyAlias)
            }
        }
    }
}

/**
 * Fail-closed AEAD secret store backed by Android Keystore AES-256-GCM.
 *
 * Security invariants:
 * - Never writes plaintext secrets into SharedPreferences.
 * - Never silently falls back to plaintext if Keystore fails or ciphertext is corrupted.
 * - Every write verifies decrypt read-back before reporting success.
 * - Legacy plaintext migration deletes old keys ONLY after verified encrypted read-back.
 * - Corrupted / unauthenticated / restored-backup entries are purged and reported as Corrupted.
 */
class KeystoreSecretStore(
    private val encryptedBacking: KeyValueBacking,
    private val backend: AeadCipherBackend
) {
    companion object {
        const val ENCRYPTED_PREFS_NAME = "atrios_keystore_secrets_v1"
        private const val KEY_PREFIX = "enc_v1_"

        fun create(context: Context): KeystoreSecretStore {
            val prefs = context.applicationContext.getSharedPreferences(
                ENCRYPTED_PREFS_NAME,
                Context.MODE_PRIVATE
            )
            return KeystoreSecretStore(
                encryptedBacking = SharedPrefsBacking(prefs),
                backend = AndroidKeyStoreAeadBackend()
            )
        }
    }

    private fun storageKey(logicalKey: String): String = "$KEY_PREFIX$logicalKey"
    private fun aadFor(logicalKey: String): ByteArray =
        "atrios-ks-v1:$logicalKey".toByteArray(Charsets.UTF_8)

    /**
     * Encrypts [value] with AES-256-GCM, commits to backing store, and performs an immediate
     * decrypt read-back verification. Returns false on any failure without writing plaintext.
     */
    fun putSecret(logicalKey: String, value: String): Boolean {
        if (logicalKey.isBlank()) return false
        if (value.isEmpty()) {
            return removeSecret(logicalKey)
        }
        return try {
            val pt = value.toByteArray(Charsets.UTF_8)
            val blob = backend.encrypt(pt, aadFor(logicalKey))
            val b64 = Base64.getEncoder().encodeToString(blob)
            val sk = storageKey(logicalKey)
            if (!encryptedBacking.putString(sk, b64)) return false
            // Mandatory read-back verification before trusting write
            when (val verify = getSecret(logicalKey)) {
                is SecretReadResult.Found -> verify.value == value
                else -> false
            }
        } catch (_: Exception) {
            false
        }
    }

    /**
     * Decrypts [logicalKey] with AEAD authentication. On corruption, tampering, AAD mismatch,
     * or Keystore reset/restore, purges the unreadable entry and returns [SecretReadResult.Corrupted].
     */
    fun getSecret(logicalKey: String): SecretReadResult {
        if (logicalKey.isBlank()) return SecretReadResult.Missing
        val sk = storageKey(logicalKey)
        val raw = encryptedBacking.getString(sk) ?: return SecretReadResult.Missing
        if (raw.isBlank()) return SecretReadResult.Missing
        return try {
            val blob = Base64.getDecoder().decode(raw)
            val pt = backend.decrypt(blob, aadFor(logicalKey))
            SecretReadResult.Found(String(pt, Charsets.UTF_8))
        } catch (e: Exception) {
            // Fail closed: purge corrupted/unreadable ciphertext; NEVER fall back to plaintext
            encryptedBacking.remove(sk)
            SecretReadResult.Corrupted(e.javaClass.simpleName + ": " + (e.message ?: "decrypt_failed"))
        }
    }

    fun removeSecret(logicalKey: String): Boolean {
        if (logicalKey.isBlank()) return true
        return encryptedBacking.remove(storageKey(logicalKey))
    }

    fun clearAllSecrets(): Boolean = encryptedBacking.clear()

    /**
     * Fail-safe migration from legacy plaintext SharedPreferences:
     * 1. For each key in [keys] present in [legacyBacking]:
     *    - Encrypts and verifies read-back in [encryptedBacking].
     *    - Deletes the legacy plaintext key ONLY if encrypted write + verify succeeded.
     * 2. If encryption/verification fails, leaves legacy untouched and reports the key in failedKeys.
     */
    fun migrateFromLegacy(legacyBacking: KeyValueBacking, keys: List<String>): MigrationReport {
        val migrated = mutableListOf<String>()
        val failed = mutableListOf<String>()
        for (k in keys) {
            val legacyVal = legacyBacking.getString(k) ?: continue
            if (legacyVal.isEmpty()) {
                legacyBacking.remove(k)
                continue
            }
            if (putSecret(k, legacyVal)) {
                if (legacyBacking.remove(k)) {
                    migrated.add(k)
                } else {
                    failed.add(k)
                }
            } else {
                failed.add(k)
            }
        }
        return MigrationReport(migratedKeys = migrated, failedKeys = failed)
    }
}
