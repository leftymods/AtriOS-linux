//! End-to-End Encryption (X25519 + ChaCha20 + HMAC-SHA256 Encrypt-then-MAC)
//! Pure Rust, zero external crates. Used for E2EE terminal between Android app and speaker
//! so the relay server never sees commands or terminal output.

use std::fs::File;
use std::io::Read;

use crate::sys::Sha256;

// --- Field arithmetic mod p = 2^255 - 19 (16 limbs of 16-bit signed i64) ---
type Fe = [i64; 16];

fn fe_zero() -> Fe { [0; 16] }
fn fe_one() -> Fe { let mut r = [0; 16]; r[0] = 1; r }

fn fe_from_bytes(s: &[u8; 32]) -> Fe {
    let mut o = [0i64; 16];
    for i in 0..16 {
        o[i] = (s[2 * i] as i64) + ((s[2 * i + 1] as i64) << 8);
    }
    o[15] &= 0x7fff;
    o
}

fn fe_carry(o: &mut Fe) {
    for i in 0..16 {
        o[i] += 1 << 16;
        let c = o[i] >> 16;
        if i < 15 {
            o[i + 1] += c - 1;
        } else {
            o[0] += (c - 1) * 38;
        }
        o[i] -= c << 16;
    }
}

fn fe_add(a: &Fe, b: &Fe) -> Fe {
    let mut o = [0i64; 16];
    for i in 0..16 { o[i] = a[i] + b[i]; }
    o
}

fn fe_sub(a: &Fe, b: &Fe) -> Fe {
    let mut o = [0i64; 16];
    for i in 0..16 { o[i] = a[i] - b[i]; }
    o
}

fn fe_mul(a: &Fe, b: &Fe) -> Fe {
    let mut t = [0i64; 31];
    for i in 0..16 {
        for j in 0..16 {
            t[i + j] += a[i] * b[j];
        }
    }
    for i in 0..15 {
        t[i] += 38 * t[i + 16];
    }
    let mut o = [0i64; 16];
    o.copy_from_slice(&t[0..16]);
    fe_carry(&mut o);
    fe_carry(&mut o);
    o
}

fn fe_sq(a: &Fe) -> Fe { fe_mul(a, a) }

fn fe_inv(i: &Fe) -> Fe {
    let mut c = *i;
    for a in (0..=253).rev() {
        c = fe_sq(&c);
        if a != 2 && a != 4 {
            c = fe_mul(&c, i);
        }
    }
    c
}

fn fe_cswap(p: &mut Fe, q: &mut Fe, b: i64) {
    let c = !(b - 1);
    for i in 0..16 {
        let t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

fn fe_to_bytes(n: &Fe) -> [u8; 32] {
    let mut t = *n;
    fe_carry(&mut t);
    fe_carry(&mut t);
    fe_carry(&mut t);
    let mut m = [0i64; 16];
    for _ in 0..2 {
        m[0] = t[0] - 0xffed;
        for i in 1..15 {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        let b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        fe_cswap(&mut t, &mut m, 1 - b);
    }
    let mut o = [0u8; 32];
    for i in 0..16 {
        o[2 * i] = (t[i] & 0xff) as u8;
        o[2 * i + 1] = ((t[i] >> 8) & 0xff) as u8;
    }
    o
}

pub fn x25519(scalar: &[u8; 32], point: &[u8; 32]) -> [u8; 32] {
    let mut z = *scalar;
    z[0] &= 248;
    z[31] = (z[31] & 127) | 64;
    let x = fe_from_bytes(point);
    let mut a = fe_one();
    let mut b = x;
    let mut c = fe_zero();
    let mut d = fe_one();
    let mut e121665 = fe_zero();
    e121665[0] = 0xdb41;
    e121665[1] = 1;

    for i in (0..255).rev() {
        let r = ((z[i >> 3] >> (i & 7)) & 1) as i64;
        fe_cswap(&mut a, &mut b, r);
        fe_cswap(&mut c, &mut d, r);
        let e = fe_add(&a, &c);
        a = fe_sub(&a, &c);
        c = fe_add(&b, &d);
        b = fe_sub(&b, &d);
        d = fe_sq(&e);
        let f = fe_sq(&a);
        a = fe_mul(&c, &a);
        c = fe_mul(&b, &e);
        let e2 = fe_add(&a, &c);
        a = fe_sub(&a, &c);
        b = fe_sq(&a);
        c = fe_sub(&d, &f);
        a = fe_mul(&c, &e121665);
        a = fe_add(&a, &d);
        c = fe_mul(&c, &a);
        a = fe_mul(&d, &f);
        d = fe_mul(&b, &x);
        b = fe_sq(&e2);
        fe_cswap(&mut a, &mut b, r);
        fe_cswap(&mut c, &mut d, r);
    }
    let ci = fe_inv(&c);
    let out = fe_mul(&a, &ci);
    fe_to_bytes(&out)
}

pub fn x25519_public_key(secret: &[u8; 32]) -> [u8; 32] {
    let mut base = [0u8; 32];
    base[0] = 9;
    x25519(secret, &base)
}

// --- RFC 8439 ChaCha20 ---
fn qr(s: &mut [u32; 16], a: usize, b: usize, c: usize, d: usize) {
    s[a] = s[a].wrapping_add(s[b]); s[d] ^= s[a]; s[d] = s[d].rotate_left(16);
    s[c] = s[c].wrapping_add(s[d]); s[b] ^= s[c]; s[b] = s[b].rotate_left(12);
    s[a] = s[a].wrapping_add(s[b]); s[d] ^= s[a]; s[d] = s[d].rotate_left(8);
    s[c] = s[c].wrapping_add(s[d]); s[b] ^= s[c]; s[b] = s[b].rotate_left(7);
}

fn chacha20_block(key: &[u8; 32], counter: u32, nonce: &[u8; 12]) -> [u8; 64] {
    let mut state = [0u32; 16];
    state[0] = 0x61707865;
    state[1] = 0x3320646e;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;
    for i in 0..8 {
        state[4 + i] = u32::from_le_bytes([key[4 * i], key[4 * i + 1], key[4 * i + 2], key[4 * i + 3]]);
    }
    state[12] = counter;
    for i in 0..3 {
        state[13 + i] = u32::from_le_bytes([nonce[4 * i], nonce[4 * i + 1], nonce[4 * i + 2], nonce[4 * i + 3]]);
    }
    let mut w = state;
    for _ in 0..10 {
        qr(&mut w, 0, 4, 8, 12); qr(&mut w, 1, 5, 9, 13); qr(&mut w, 2, 6, 10, 14); qr(&mut w, 3, 7, 11, 15);
        qr(&mut w, 0, 5, 10, 15); qr(&mut w, 1, 6, 11, 12); qr(&mut w, 2, 7, 8, 13); qr(&mut w, 3, 4, 9, 14);
    }
    let mut out = [0u8; 64];
    for i in 0..16 {
        let v = w[i].wrapping_add(state[i]).to_le_bytes();
        out[4 * i..4 * i + 4].copy_from_slice(&v);
    }
    out
}

pub fn chacha20_xor(key: &[u8; 32], nonce: &[u8; 12], data: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(data.len());
    let mut ctr = 1u32;
    for chunk in data.chunks(64) {
        let ks = chacha20_block(key, ctr, nonce);
        for (i, &b) in chunk.iter().enumerate() {
            out.push(b ^ ks[i]);
        }
        ctr = ctr.wrapping_add(1);
    }
    out
}

// --- HMAC-SHA256 ---
pub fn hmac_sha256(key: &[u8], msg: &[u8]) -> [u8; 32] {
    let mut k_block = [0u8; 64];
    if key.len() > 64 {
        let mut h = Sha256::new();
        h.update(key);
        k_block[..32].copy_from_slice(&h.finalize());
    } else {
        k_block[..key.len()].copy_from_slice(key);
    }
    let mut ipad = [0x36u8; 64];
    let mut opad = [0x5cu8; 64];
    for i in 0..64 {
        ipad[i] ^= k_block[i];
        opad[i] ^= k_block[i];
    }
    let mut inner = Sha256::new();
    inner.update(&ipad);
    inner.update(msg);
    let inner_hash = inner.finalize();

    let mut outer = Sha256::new();
    outer.update(&opad);
    outer.update(&inner_hash);
    outer.finalize()
}

pub fn derive_keys(shared: &[u8; 32]) -> ([u8; 32], [u8; 32]) {
    let mut he = Sha256::new();
    he.update(shared);
    he.update(b"atrios-e2ee-enc-v1");
    let enc_key = he.finalize();

    let mut hm = Sha256::new();
    hm.update(shared);
    hm.update(b"atrios-e2ee-mac-v1");
    let mac_key = hm.finalize();
    (enc_key, mac_key)
}

pub fn encrypt_e2ee(shared: &[u8; 32], nonce: &[u8; 12], plaintext: &[u8]) -> (Vec<u8>, [u8; 32]) {
    let (enc_key, mac_key) = derive_keys(shared);
    let ct = chacha20_xor(&enc_key, nonce, plaintext);
    let mut mac_in = Vec::with_capacity(12 + ct.len());
    mac_in.extend_from_slice(nonce);
    mac_in.extend_from_slice(&ct);
    let mac = hmac_sha256(&mac_key, &mac_in);
    (ct, mac)
}

pub fn decrypt_e2ee(shared: &[u8; 32], nonce: &[u8; 12], ct: &[u8], mac: &[u8; 32]) -> Result<Vec<u8>, String> {
    let (enc_key, mac_key) = derive_keys(shared);
    let mut mac_in = Vec::with_capacity(12 + ct.len());
    mac_in.extend_from_slice(nonce);
    mac_in.extend_from_slice(ct);
    let expected = hmac_sha256(&mac_key, &mac_in);
    let mut diff = 0u8;
    for i in 0..32 { diff |= expected[i] ^ mac[i]; }
    if diff != 0 {
        return Err("E2EE MAC verification failed".into());
    }
    Ok(chacha20_xor(&enc_key, nonce, ct))
}

pub fn random_bytes(buf: &mut [u8]) {
    if let Ok(mut f) = File::open("/dev/urandom") {
        if f.read_exact(buf).is_ok() {
            return;
        }
    }
    let mut seed = crate::sys::now_unix_i64() as u64 ^ (std::process::id() as u64).wrapping_mul(0x9e3779b97f4a7c15);
    for b in buf.iter_mut() {
        seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        *b = (seed >> 33) as u8;
    }
}

pub fn hex_encode(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        use std::fmt::Write;
        let _ = write!(s, "{:02x}", b);
    }
    s
}

pub fn hex_decode(s: &str) -> Result<Vec<u8>, String> {
    let s = s.trim();
    if s.len() % 2 != 0 {
        return Err("Invalid hex length".into());
    }
    let mut out = Vec::with_capacity(s.len() / 2);
    let b = s.as_bytes();
    for i in (0..b.len()).step_by(2) {
        let hi = (b[i] as char).to_digit(16).ok_or("Invalid hex char")?;
        let lo = (b[i + 1] as char).to_digit(16).ok_or("Invalid hex char")?;
        out.push(((hi << 4) | lo) as u8);
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn x25519_and_e2ee_roundtrip() {
        // RFC 7748 test vector
        let alice_sk = hex_decode("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a").unwrap();
        let bob_sk = hex_decode("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb").unwrap();
        let a: [u8; 32] = alice_sk.try_into().unwrap();
        let b: [u8; 32] = bob_sk.try_into().unwrap();
        let a_pub = x25519_public_key(&a);
        let b_pub = x25519_public_key(&b);
        assert_eq!(hex_encode(&a_pub), "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
        let s1 = x25519(&a, &b_pub);
        let s2 = x25519(&b, &a_pub);
        assert_eq!(s1, s2);
        assert_eq!(hex_encode(&s1), "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");

        let nonce = [7u8; 12];
        let (ct, mac) = encrypt_e2ee(&s1, &nonce, b"uptime && uname -a");
        let pt = decrypt_e2ee(&s2, &nonce, &ct, &mac).unwrap();
        assert_eq!(pt, b"uptime && uname -a");
    }
}
