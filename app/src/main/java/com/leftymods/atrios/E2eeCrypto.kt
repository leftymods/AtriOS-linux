package com.leftymods.atrios

import java.security.MessageDigest
import java.security.SecureRandom
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/**
 * End-to-End Encryption (RFC 7748 X25519 + RFC 8439 ChaCha20 + HMAC-SHA256 Encrypt-then-MAC).
 * Matches speaker e2ee.rs byte-for-byte so the relay server never sees terminal commands or output.
 */
object E2eeCrypto {
    private val rng = SecureRandom()

    fun randomBytes(n: Int): ByteArray = ByteArray(n).also { rng.nextBytes(it) }

    fun hexEncode(bytes: ByteArray): String =
        bytes.joinToString("") { "%02x".format(it.toInt() and 0xFF) }

    fun hexDecode(hex: String): ByteArray {
        val s = hex.trim()
        require(s.length % 2 == 0) { "Invalid hex length" }
        return ByteArray(s.length / 2) { i ->
            s.substring(i * 2, i * 2 + 2).toInt(16).toByte()
        }
    }

    // --- X25519 mod 2^255 - 19 ---
    private fun feFromBytes(s: ByteArray): LongArray {
        val o = LongArray(16)
        for (i in 0 until 16) {
            o[i] = (s[2 * i].toInt() and 0xFF).toLong() + ((s[2 * i + 1].toInt() and 0xFF).toLong() shl 8)
        }
        o[15] = o[15] and 0x7FFFL
        return o
    }

    private fun feCarry(o: LongArray) {
        for (i in 0 until 16) {
            o[i] += 1L shl 16
            val c = o[i] shr 16
            if (i < 15) o[i + 1] += c - 1 else o[0] += (c - 1) * 38L
            o[i] -= c shl 16
        }
    }

    private fun feAdd(a: LongArray, b: LongArray) = LongArray(16) { a[it] + b[it] }
    private fun feSub(a: LongArray, b: LongArray) = LongArray(16) { a[it] - b[it] }

    private fun feMul(a: LongArray, b: LongArray): LongArray {
        val t = LongArray(31)
        for (i in 0 until 16) {
            for (j in 0 until 16) t[i + j] += a[i] * b[j]
        }
        for (i in 0 until 15) t[i] += 38L * t[i + 16]
        val o = LongArray(16) { t[it] }
        feCarry(o); feCarry(o)
        return o
    }

    private fun feSq(a: LongArray) = feMul(a, a)

    private fun feInv(i: LongArray): LongArray {
        var c = i.copyOf()
        for (a in 253 downTo 0) {
            c = feSq(c)
            if (a != 2 && a != 4) c = feMul(c, i)
        }
        return c
    }

    private fun feCswap(p: LongArray, q: LongArray, b: Long) {
        val c = (b - 1L).inv()
        for (i in 0 until 16) {
            val t = c and (p[i] xor q[i])
            p[i] = p[i] xor t
            q[i] = q[i] xor t
        }
    }

    private fun feToBytes(n: LongArray): ByteArray {
        val t = n.copyOf()
        feCarry(t); feCarry(t); feCarry(t)
        val m = LongArray(16)
        repeat(2) {
            m[0] = t[0] - 0xFFEDL
            for (i in 1 until 15) {
                m[i] = t[i] - 0xFFFFL - ((m[i - 1] shr 16) and 1L)
                m[i - 1] = m[i - 1] and 0xFFFFL
            }
            m[15] = t[15] - 0x7FFFL - ((m[14] shr 16) and 1L)
            val b = (m[15] shr 16) and 1L
            m[14] = m[14] and 0xFFFFL
            feCswap(t, m, 1L - b)
        }
        val o = ByteArray(32)
        for (i in 0 until 16) {
            o[2 * i] = (t[i] and 0xFFL).toByte()
            o[2 * i + 1] = ((t[i] shr 8) and 0xFFL).toByte()
        }
        return o
    }

    fun x25519(scalar: ByteArray, point: ByteArray): ByteArray {
        val z = scalar.copyOf(32)
        z[0] = (z[0].toInt() and 248).toByte()
        z[31] = ((z[31].toInt() and 127) or 64).toByte()
        val x = feFromBytes(point)
        var a = LongArray(16).apply { this[0] = 1L }
        var b = x.copyOf()
        var c = LongArray(16)
        var d = LongArray(16).apply { this[0] = 1L }
        val e121665 = LongArray(16).apply { this[0] = 0xDB41L; this[1] = 1L }
        for (i in 254 downTo 0) {
            val r = (((z[i shr 3].toInt() and 0xFF) shr (i and 7)) and 1).toLong()
            feCswap(a, b, r); feCswap(c, d, r)
            val e = feAdd(a, c)
            a = feSub(a, c)
            c = feAdd(b, d)
            b = feSub(b, d)
            d = feSq(e)
            val f = feSq(a)
            a = feMul(c, a)
            c = feMul(b, e)
            val e2 = feAdd(a, c)
            a = feSub(a, c)
            b = feSq(a)
            c = feSub(d, f)
            a = feMul(c, e121665)
            a = feAdd(a, d)
            c = feMul(c, a)
            a = feMul(d, f)
            d = feMul(b, x)
            b = feSq(e2)
            feCswap(a, b, r); feCswap(c, d, r)
        }
        return feToBytes(feMul(a, feInv(c)))
    }

    fun x25519PublicKey(secret: ByteArray): ByteArray {
        val base = ByteArray(32).apply { this[0] = 9 }
        return x25519(secret, base)
    }

    // --- RFC 8439 ChaCha20 ---
    private fun rotl(v: Int, n: Int): Int = (v shl n) or (v ushr (32 - n))

    private fun qr(s: IntArray, a: Int, b: Int, c: Int, d: Int) {
        s[a] += s[b]; s[d] = rotl(s[d] xor s[a], 16)
        s[c] += s[d]; s[b] = rotl(s[b] xor s[c], 12)
        s[a] += s[b]; s[d] = rotl(s[d] xor s[a], 8)
        s[c] += s[d]; s[b] = rotl(s[b] xor s[c], 7)
    }

    private fun le32(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or
            ((b[off + 1].toInt() and 0xFF) shl 8) or
            ((b[off + 2].toInt() and 0xFF) shl 16) or
            ((b[off + 3].toInt() and 0xFF) shl 24)

    private fun chacha20Block(key: ByteArray, counter: Int, nonce: ByteArray): ByteArray {
        val st = IntArray(16)
        st[0] = 0x61707865; st[1] = 0x3320646e; st[2] = 0x79622d32; st[3] = 0x6b206574
        for (i in 0 until 8) st[4 + i] = le32(key, 4 * i)
        st[12] = counter
        for (i in 0 until 3) st[13 + i] = le32(nonce, 4 * i)
        val w = st.copyOf()
        repeat(10) {
            qr(w, 0, 4, 8, 12); qr(w, 1, 5, 9, 13); qr(w, 2, 6, 10, 14); qr(w, 3, 7, 11, 15)
            qr(w, 0, 5, 10, 15); qr(w, 1, 6, 11, 12); qr(w, 2, 7, 8, 13); qr(w, 3, 4, 9, 14)
        }
        val out = ByteArray(64)
        for (i in 0 until 16) {
            val v = w[i] + st[i]
            out[4 * i] = (v and 0xFF).toByte()
            out[4 * i + 1] = ((v ushr 8) and 0xFF).toByte()
            out[4 * i + 2] = ((v ushr 16) and 0xFF).toByte()
            out[4 * i + 3] = ((v ushr 24) and 0xFF).toByte()
        }
        return out
    }

    private fun chacha20Xor(key: ByteArray, nonce: ByteArray, data: ByteArray): ByteArray {
        val out = ByteArray(data.size)
        var ctr = 1
        var off = 0
        while (off < data.size) {
            val ks = chacha20Block(key, ctr++, nonce)
            val len = minOf(64, data.size - off)
            for (i in 0 until len) out[off + i] = (data[off + i].toInt() xor ks[i].toInt()).toByte()
            off += len
        }
        return out
    }

    private fun sha256(vararg parts: ByteArray): ByteArray {
        val md = MessageDigest.getInstance("SHA-256")
        parts.forEach { md.update(it) }
        return md.digest()
    }

    private fun hmacSha256(key: ByteArray, data: ByteArray): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        return mac.doFinal(data)
    }

    private fun deriveKeys(shared: ByteArray): Pair<ByteArray, ByteArray> {
        val enc = sha256(shared, "atrios-e2ee-enc-v1".toByteArray(Charsets.UTF_8))
        val mac = sha256(shared, "atrios-e2ee-mac-v1".toByteArray(Charsets.UTF_8))
        return enc to mac
    }

    fun encrypt(shared: ByteArray, nonce: ByteArray, plaintext: ByteArray): Pair<ByteArray, ByteArray> {
        val (encKey, macKey) = deriveKeys(shared)
        val ct = chacha20Xor(encKey, nonce, plaintext)
        val mac = hmacSha256(macKey, nonce + ct)
        return ct to mac
    }

    fun decrypt(shared: ByteArray, nonce: ByteArray, ct: ByteArray, mac: ByteArray): ByteArray {
        val (encKey, macKey) = deriveKeys(shared)
        val expected = hmacSha256(macKey, nonce + ct)
        var diff = 0
        for (i in 0 until 32) diff = diff or (expected[i].toInt() xor mac[i].toInt())
        require(diff == 0) { "E2EE MAC verification failed" }
        return chacha20Xor(encKey, nonce, ct)
    }
}
