// v5.58 SHA-256, written from the published standard (NIST FIPS 180-4, sections 4.1.2, 4.2.2, 5.3.3, 6.2). Used for the
// BFR mod pack's checks: the clean exe's bytes are checked by their hash (no game bytes in the DLL) and every game file
// the loader builds from a patch is checked before and after. No Windows crypto call: it also runs inside DllMain.
#pragma once

struct CwoSha256 {
    unsigned h[8];
    unsigned char buf[64];
    unsigned long long len;
    unsigned n;
};

static inline unsigned cwo_ror(unsigned x, int r) { return (x >> r) | (x << (32 - r)); }

static const unsigned CWO_SHA_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static void cwo_sha_block(CwoSha256 *s, const unsigned char *p) {
    unsigned w[64];
    for (int t = 0; t < 16; t++) w[t] = (unsigned)p[4 * t] << 24 | (unsigned)p[4 * t + 1] << 16 | (unsigned)p[4 * t + 2] << 8 | p[4 * t + 3];
    for (int t = 16; t < 64; t++) {
        unsigned s0 = cwo_ror(w[t - 15], 7) ^ cwo_ror(w[t - 15], 18) ^ (w[t - 15] >> 3);
        unsigned s1 = cwo_ror(w[t - 2], 17) ^ cwo_ror(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    unsigned a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int t = 0; t < 64; t++) {
        unsigned t1 = h + (cwo_ror(e, 6) ^ cwo_ror(e, 11) ^ cwo_ror(e, 25)) + ((e & f) ^ (~e & g)) + CWO_SHA_K[t] + w[t];
        unsigned t2 = (cwo_ror(a, 2) ^ cwo_ror(a, 13) ^ cwo_ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void cwo_sha_init(CwoSha256 *s) {
    static const unsigned H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (int i = 0; i < 8; i++) s->h[i] = H0[i];
    s->len = 0; s->n = 0;
}

static void cwo_sha_add(CwoSha256 *s, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    s->len += n;
    while (n) {
        if (s->n == 0 && n >= 64) { cwo_sha_block(s, p); p += 64; n -= 64; continue; }
        size_t k = 64 - s->n < n ? 64 - s->n : n;
        for (size_t i = 0; i < k; i++) s->buf[s->n + i] = p[i];
        s->n += (unsigned)k; p += k; n -= k;
        if (s->n == 64) { cwo_sha_block(s, s->buf); s->n = 0; }
    }
}

static void cwo_sha_end(CwoSha256 *s, unsigned char out[32]) {
    unsigned long long bits = s->len * 8;
    unsigned char pad = 0x80, z = 0;
    cwo_sha_add(s, &pad, 1);
    while (s->n != 56) cwo_sha_add(s, &z, 1);
    unsigned char lb[8];
    for (int i = 0; i < 8; i++) lb[i] = (unsigned char)(bits >> (56 - 8 * i));
    cwo_sha_add(s, lb, 8);
    for (int i = 0; i < 8; i++) { out[4 * i] = (unsigned char)(s->h[i] >> 24); out[4 * i + 1] = (unsigned char)(s->h[i] >> 16); out[4 * i + 2] = (unsigned char)(s->h[i] >> 8); out[4 * i + 3] = (unsigned char)s->h[i]; }
}

static void cwo_sha256(const void *data, size_t n, unsigned char out[32]) {
    CwoSha256 s; cwo_sha_init(&s); cwo_sha_add(&s, data, n); cwo_sha_end(&s, out);
}

// the standard's own example ("abc", FIPS 180-4 appendix / NIST examples) and the two-block message
static bool cwo_sha256_self_test() {
    static const unsigned char ABC[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                                          0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    static const unsigned char TWO[32] = {0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
                                          0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1};
    unsigned char o[32];
    cwo_sha256("abc", 3, o);
    for (int i = 0; i < 32; i++) if (o[i] != ABC[i]) return false;
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    cwo_sha256(m, 56, o);
    for (int i = 0; i < 32; i++) if (o[i] != TWO[i]) return false;
    return true;
}
