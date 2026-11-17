// ---- v3.8 Create a Character: outfit themes (owner 2026-10-25: voice, evade type and outfit theme for the created
// character, shown in the live preview). A C++ port of our own 13_Independent_Research_Tools/outfit_generator.py
// (apply_theme): the outfit colour texture (the first 512 x 512 DXT1 texture of the file) is recoloured through a colour
// ramp on its own light and shade, skin tiles kept, re-encoded with the full mip chain; every block that is transparent in
// the original stays transparent with the same pixels on every mip (owner's hard rule), checked after encoding. Same
// arithmetic and rounding as the Python (tests/cb_test checks the bytes against it).
#include <cmath>

static void th_c565(unsigned v, int *o) { o[0] = (v >> 11 & 31) * 255 / 31; o[1] = (v >> 5 & 63) * 255 / 63; o[2] = (v & 31) * 255 / 31; }
struct Px { int r, g, b, a; };

static void th_decode(const unsigned char *d, int w, int h, std::vector<Px> &px) {   // export_all_effects.dxt_rows, DXT1
    px.assign((size_t)w * h, {0, 0, 0, 0});
    size_t o = 0;
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4, o += 8) {
            unsigned c0 = rd16(d + o), c1 = rd16(d + o + 2), bits = rd32(d + o + 4);
            int a[3], b[3], pal[4][3];
            th_c565(c0, a); th_c565(c1, b);
            for (int k = 0; k < 3; k++) {
                pal[0][k] = a[k]; pal[1][k] = b[k];
                if (c0 > c1) { pal[2][k] = (2 * a[k] + b[k]) / 3; pal[3][k] = (a[k] + 2 * b[k]) / 3; }
                else { pal[2][k] = (a[k] + b[k]) / 2; pal[3][k] = 0; }
            }
            for (int i = 0; i < 16; i++) {
                int y = by + i / 4, x = bx + i % 4, q = bits >> 2 * i & 3;
                if (y < h && x < w) px[(size_t)y * w + x] = {pal[q][0], pal[q][1], pal[q][2], (c0 > c1 || q != 3) ? 255 : 0};
            }
        }
}

static double th_lum(int r, int g, int b) { return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0; }
static void th_hsv(int ri, int gi, int bi, double &h, double &s, double &v) {
    double r = ri / 255.0, g = gi / 255.0, b = bi / 255.0, mx = (std::max)(r, (std::max)(g, b)), mn = (std::min)(r, (std::min)(g, b)), d = mx - mn;
    if (d == 0) h = 0.0;
    else if (mx == r) { h = fmod(60 * ((g - b) / d), 360.0); if (h < 0) h += 360.0; }
    else if (mx == g) h = 60 * ((b - r) / d) + 120;
    else h = 60 * ((r - g) / d) + 240;
    s = mx ? d / mx : 0.0;
    v = mx;
}
static int th_round(double x) { return (int)nearbyint(x); }       // Python round(): ties to even
static double th_hash(long long x, long long y, long long seed) {
    unsigned long long n = (unsigned long long)(x * 374761393LL + y * 668265263LL + seed * 2147483647LL) & 0xFFFFFFFFull;
    n = ((n ^ (n >> 13)) * 1274126177ull) & 0xFFFFFFFFull;
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0;
}
static double th_noise(int x, int y, int scale, int seed) {
    double gx = (double)x / scale, gy = (double)y / scale;
    int x0 = (int)gx, y0 = (int)gy;
    double fx = gx - x0, fy = gy - y0;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    double a = th_hash(x0, y0, seed), b = th_hash(x0 + 1, y0, seed), c = th_hash(x0, y0 + 1, seed), d = th_hash(x0 + 1, y0 + 1, seed);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}
struct Stop { double t; int c[3]; };
static void th_ramp(const Stop *s, int n, double t, int *out) {
    t = t < 0 ? 0.0 : t > 1 ? 1.0 : t;
    for (int i = 0; i + 1 < n; i++)
        if (t <= s[i + 1].t) {
            double f = s[i + 1].t > s[i].t ? (t - s[i].t) / (s[i + 1].t - s[i].t) : 0;
            for (int k = 0; k < 3; k++) out[k] = th_round(s[i].c[k] + (s[i + 1].c[k] - s[i].c[k]) * f);
            return;
        }
    for (int k = 0; k < 3; k++) out[k] = s[n - 1].c[k];
}
static void th_mix(int *o, const double *b, double f) { for (int k = 0; k < 3; k++) o[k] = th_round(o[k] + (b[k] - o[k]) * f); }

static const Stop TH_FIRE[] = {{0, {10, 4, 2}}, {0.3, {70, 10, 0}}, {0.55, {190, 50, 0}}, {0.78, {250, 140, 20}}, {1, {255, 236, 170}}};
static const Stop TH_ICE[] = {{0, {6, 16, 42}}, {0.38, {38, 96, 168}}, {0.7, {140, 208, 240}}, {1, {246, 252, 255}}};
static const Stop TH_SHADOW[] = {{0, {5, 0, 10}}, {0.5, {48, 14, 70}}, {0.8, {118, 60, 160}}, {1, {210, 172, 255}}};
static const Stop TH_GOLD[] = {{0, {30, 18, 0}}, {0.4, {128, 84, 10}}, {0.7, {220, 170, 52}}, {1, {255, 246, 192}}};
static const Stop TH_TOXIC[] = {{0, {4, 20, 0}}, {0.5, {58, 140, 10}}, {0.8, {160, 240, 40}}, {1, {236, 255, 172}}};
static const Stop TH_CRIMSON[] = {{0, {14, 0, 2}}, {0.5, {118, 6, 22}}, {0.8, {200, 40, 52}}, {1, {255, 192, 192}}};
static const Stop TH_OCEAN[] = {{0, {0, 14, 26}}, {0.5, {0, 88, 110}}, {0.8, {40, 178, 190}}, {1, {202, 250, 246}}};
static const char *TH_NAMES[11] = {"none", "Fire", "Ice", "Shadow", "Royal gold", "Toxic", "Crimson", "Ocean", "Urban camo", "Neon circuit", "Spectral ghost"};

static void th_pixel(int theme, int x, int y, double L, double edge, int *out) {   // theme 1-10 = THEME_ORDER
    const Stop *st = NULL; int n = 0;
    switch (theme) {
    case 1: st = TH_FIRE; n = 5; break;
    case 2: st = TH_ICE; n = 4; break;
    case 3: st = TH_SHADOW; n = 4; break;
    case 4: st = TH_GOLD; n = 4; break;
    case 5: st = TH_TOXIC; n = 4; break;
    case 6: st = TH_CRIMSON; n = 4; break;
    case 7: st = TH_OCEAN; n = 4; break;
    }
    if (st) {
        double t = 0.04 + 0.96 * pow(L, 1.15);
        th_ramp(st, n, t, out);
        if (theme == 1 && edge > 0.2) { const double g[3] = {255, 190, 60}; th_mix(out, g, (std::min)(0.6, edge)); }
        if (theme == 4 && edge > 0.18) { const double g[3] = {255, 250, 210}; th_mix(out, g, (std::min)(1.0, edge * 1.4)); }
        return;
    }
    if (theme == 8) {                                       // camo
        double nz = th_noise(x, y, 26, 3) * 0.65 + th_noise(x, y, 9, 5) * 0.35;
        int base[3];
        if (nz < 0.38) { base[0] = 52; base[1] = 56; base[2] = 44; }
        else if (nz < 0.55) { base[0] = 96; base[1] = 100; base[2] = 82; }
        else if (nz < 0.7) { base[0] = 150; base[1] = 150; base[2] = 132; }
        else { base[0] = 24; base[1] = 26; base[2] = 22; }
        double k = 0.45 + 0.9 * L;
        for (int i = 0; i < 3; i++) out[i] = (std::min)(255, (int)(base[i] * k));
        return;
    }
    if (theme == 9) {                                       // neon
        out[0] = (int)(40 * 0.22); out[1] = (int)(30 * 0.22); out[2] = (int)(60 * 0.22);
        double glow[3];
        if (th_noise(x, y, 64, 9) > 0.5) { glow[0] = 255; glow[1] = 40; glow[2] = 200; } else { glow[0] = 30; glow[1] = 230; glow[2] = 255; }
        th_mix(out, glow, (std::min)(1.0, edge * 3.2));
        const double dim[3] = {L * 90, L * 70, L * 120};
        th_mix(out, dim, 0.35);
        return;
    }
    // 10 spectral
    int g = 120 + (int)(135 * L);
    out[0] = (int)(g * 0.80); out[1] = (int)(g * 0.92); out[2] = (std::min)(255, (int)(g * 1.08));
    const double pale[3] = {180, 230, 255};
    th_mix(out, pale, 0.25 * th_noise(x, y, 48, 13));
}

static void th_565(unsigned v, int *o) { int r = v >> 11 & 31, g = v >> 5 & 63, b = v & 31; o[0] = r << 3 | r >> 2; o[1] = g << 2 | g >> 4; o[2] = b << 3 | b >> 2; }
static unsigned th_to565(const int *c) { return (unsigned)((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | (c[2] >> 3)); }
static void th_block(const int blk[16][3], const bool *clear, unsigned char *o8) {
    bool any = false;
    for (int i = 0; i < 16; i++) any |= clear[i];
    unsigned c0, c1, bits = 0;
    if (any) {
        int lo = -1, hi = -1; long long lmin = 1LL << 60, lmax = -1;
        for (int i = 0; i < 16; i++) {
            if (clear[i]) continue;
            long long l = blk[i][0] * 299LL + blk[i][1] * 587LL + blk[i][2] * 114LL;
            if (l < lmin) { lmin = l; lo = i; }
            if (l > lmax) { lmax = l; hi = i; }
        }
        static const int black[3] = {0, 0, 0};
        unsigned a1 = th_to565(lo >= 0 ? blk[lo] : black), b1 = th_to565(hi >= 0 ? blk[hi] : black);
        c0 = (std::min)(a1, b1); c1 = (std::max)(a1, b1);
        int A[3], B[3], pal[3][3];
        th_565(c0, A); th_565(c1, B);
        for (int k = 0; k < 3; k++) { pal[0][k] = A[k]; pal[1][k] = B[k]; pal[2][k] = (A[k] + B[k]) / 2; }
        for (int i = 0; i < 16; i++) {
            int q = 3;
            if (!clear[i]) {
                int best = 1 << 30; q = 0;
                for (int p = 0; p < 3; p++) {
                    int d = (blk[i][0] - pal[p][0]) * (blk[i][0] - pal[p][0]) + (blk[i][1] - pal[p][1]) * (blk[i][1] - pal[p][1]) + (blk[i][2] - pal[p][2]) * (blk[i][2] - pal[p][2]);
                    if (d < best) { best = d; q = p; }
                }
            }
            bits |= (unsigned)q << (2 * i);
        }
    } else {
        int lo = 0, hi = 0; long long lmin = 1LL << 60, lmax = -1;
        for (int i = 0; i < 16; i++) {
            long long l = blk[i][0] * 299LL + blk[i][1] * 587LL + blk[i][2] * 114LL;
            if (l < lmin) { lmin = l; lo = i; }
            if (l > lmax) { lmax = l; hi = i; }
        }
        c0 = th_to565(blk[hi]); c1 = th_to565(blk[lo]);
        if (c0 != c1) {
            if (c0 < c1) std::swap(c0, c1);
            int A[3], B[3], pal[4][3];
            th_565(c0, A); th_565(c1, B);
            for (int k = 0; k < 3; k++) { pal[0][k] = A[k]; pal[1][k] = B[k]; pal[2][k] = (2 * A[k] + B[k]) / 3; pal[3][k] = (A[k] + 2 * B[k]) / 3; }
            for (int i = 0; i < 16; i++) {
                int best = 1 << 30, q = 0;
                for (int p = 0; p < 4; p++) {
                    int d = (blk[i][0] - pal[p][0]) * (blk[i][0] - pal[p][0]) + (blk[i][1] - pal[p][1]) * (blk[i][1] - pal[p][1]) + (blk[i][2] - pal[p][2]) * (blk[i][2] - pal[p][2]);
                    if (d < best) { best = d; q = p; }
                }
                bits |= (unsigned)q << (2 * i);
            }
        }
    }
    o8[0] = (unsigned char)c0; o8[1] = (unsigned char)(c0 >> 8); o8[2] = (unsigned char)c1; o8[3] = (unsigned char)(c1 >> 8);
    o8[4] = (unsigned char)bits; o8[5] = (unsigned char)(bits >> 8); o8[6] = (unsigned char)(bits >> 16); o8[7] = (unsigned char)(bits >> 24);
}
static void th_clear(const unsigned char *b8, bool *clear) {
    unsigned c0 = rd16(b8), c1 = rd16(b8 + 2), bits = rd32(b8 + 4);
    for (int i = 0; i < 16; i++) clear[i] = c0 <= c1 && ((bits >> (2 * i)) & 3) == 3;
}

// the theme on a built character file; false (file unchanged) when it has no 512 x 512 DXT1 texture or the check fails
static bool th_apply(Bytes &file, int theme) {
    if (theme < 1 || theme > 10) return true;
    Alr m;
    if (!alr_parse(file, m) || m.tex.size() < 12) return false;
    int ntex = (short)rd16(&m.tex[8]);
    Bytes res = m.resource;
    res.insert(res.end(), m.trailing.begin(), m.trailing.end());
    for (int ti = 0; ti < ntex; ti++) {
        const unsigned char *t = &m.tex[12 + 28 * ti];
        unsigned toff = rd32(t + 4), packed = rd32(t + 12);
        int w = 1 << ((packed >> 20) & 15), h = 1 << ((packed >> 24) & 15), fmt = (packed >> 8) & 255, mips = (packed >> 16) & 15;
        if (fmt != 0x0C || w != 512 || h != 512) continue;
        size_t size = 0;                                    // alr_transplant.texture_bytes: the chain, rounded up to 0x80
        for (int k = 0; k < (std::max)(mips, 1); k++) { int mw = (std::max)(w >> k, 1), mh = (std::max)(h >> k, 1); size += (size_t)(std::max)(mw / 4, 1) * (std::max)(mh / 4, 1) * 8; }
        size = (size + 0x7F) / 0x80 * 0x80;
        if ((packed & 0xFF) & 4) size *= 6;
        if (toff + size > res.size()) return false;
        const unsigned char *orig = &res[toff];
        std::vector<Px> px;
        th_decode(orig, w, h, px);
        // keep: skin pixels, mostly-skin 16 x 16 tiles, and their skin-bearing neighbours (keep_mask)
        std::vector<char> skin((size_t)w * h);
        for (size_t i = 0; i < px.size(); i++) {
            double hu, s, v;
            th_hsv(px[i].r, px[i].g, px[i].b, hu, s, v);
            skin[i] = 8 <= hu && hu <= 36 && 0.22 <= s && s <= 0.72 && 0.22 <= v && v <= 0.98;
        }
        const int T = 16, th_ = h / T, tw = w / T;
        std::vector<double> frac((size_t)th_ * tw);
        for (int ty = 0; ty < th_; ty++)
            for (int tx = 0; tx < tw; tx++) {
                int c = 0;
                for (int i = 0; i < T; i++) for (int j = 0; j < T; j++) c += skin[(size_t)(ty * T + i) * w + tx * T + j];
                frac[(size_t)ty * tw + tx] = c / (double)(T * T);
            }
        auto hot = [&](int ty, int tx) { ty = (std::min)((std::max)(ty, 0), th_ - 1); tx = (std::min)((std::max)(tx, 0), tw - 1); return frac[(size_t)ty * tw + tx] >= 0.35; };
        std::vector<char> tk((size_t)th_ * tw);
        int nkeep = 0;
        for (int ty = 0; ty < th_; ty++)
            for (int tx = 0; tx < tw; tx++) {
                int near_ = 0;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) near_ += hot(ty + dy, tx + dx);
                tk[(size_t)ty * tw + tx] = hot(ty, tx) || (frac[(size_t)ty * tw + tx] >= 0.08 && near_ >= 3);
                nkeep += tk[(size_t)ty * tw + tx];
            }
        // v5.36 (owner): on a body whose outfit is skin-coloured the skin rule keeps the outfit itself (the Spokesman's brown coat:
        // 633 of 1024 tiles read as skin; Edgar 55, the Player 305 - measured on the live files). When more than 45 % of the
        // tiles read as skin, only small skin patches (face, hands: up to 64 joined tiles) are kept; the big regions are the
        // outfit and take the theme, skin-hued pixels inside them too
        bool big = nkeep * 100 > th_ * tw * 45;
        if (big) {
            std::vector<int> seen((size_t)th_ * tw, 0), stack;
            for (int s = 0; s < th_ * tw; s++) {
                if (!tk[s] || seen[s]) continue;
                std::vector<int> comp;
                stack.assign(1, s); seen[s] = 1;
                while (!stack.empty()) {
                    int c = stack.back(); stack.pop_back();
                    comp.push_back(c);
                    int cy = c / tw, cx = c % tw;
                    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                        int ny = cy + dy, nx = cx + dx;
                        if (ny < 0 || ny >= th_ || nx < 0 || nx >= tw) continue;
                        int n = ny * tw + nx;
                        if (tk[n] && !seen[n]) { seen[n] = 1; stack.push_back(n); }
                    }
                }
                if (comp.size() > 64) for (int c : comp) tk[c] = 0;
            }
        }
        std::vector<char> keep((size_t)w * h);
        for (int ty = 0; ty < th_; ty++)
            for (int tx = 0; tx < tw; tx++) {
                bool k = tk[(size_t)ty * tw + tx] != 0;
                for (int i = 0; i < T; i++) for (int j = 0; j < T; j++) { size_t p = (size_t)(ty * T + i) * w + tx * T + j; keep[p] = k || (!big && skin[p]); }
            }
        std::vector<double> L((size_t)w * h), E((size_t)w * h, 0.0);
        for (size_t i = 0; i < px.size(); i++) L[i] = th_lum(px[i].r, px[i].g, px[i].b);
        for (int y = 1; y < h - 1; y++)
            for (int x = 1; x < w - 1; x++) {
                double gx = L[(size_t)y * w + x + 1] - L[(size_t)y * w + x - 1], gy = L[(size_t)(y + 1) * w + x] - L[(size_t)(y - 1) * w + x];
                E[(size_t)y * w + x] = sqrt(gx * gx + gy * gy);
            }
        std::vector<int> img((size_t)w * h * 3);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                size_t i = (size_t)y * w + x;
                int c[3] = {px[i].r, px[i].g, px[i].b};
                if (!keep[i]) { th_pixel(theme, x, y, L[i], E[i], c); for (int k = 0; k < 3; k++) c[k] = (std::max)(0, (std::min)(255, c[k])); }
                img[i * 3] = c[0]; img[i * 3 + 1] = c[1]; img[i * 3 + 2] = c[2];
            }
        // the mip chain (encode_chain): each level from the previous by 2 x 2 averages, transparency from the original
        Bytes data;
        int lw = w, lh = h;
        size_t o = 0;
        std::vector<int> lvl = img;
        for (int k = 0; k < (std::max)(mips, 1); k++) {
            int mw = (std::max)(w >> k, 1), mh = (std::max)(h >> k, 1);
            size_t sz = (size_t)(std::max)(mw / 4, 1) * (std::max)(mh / 4, 1) * 8, nb = 0;
            for (int by = 0; by < (std::max)(lh, 4); by += 4)
                for (int bx = 0; bx < (std::max)(lw, 4); bx += 4, nb++) {
                    int blk[16][3];
                    for (int i = 0; i < 16; i++) {
                        int yy = (std::min)(by + i / 4, lh - 1), xx = (std::min)(bx + i % 4, lw - 1);
                        for (int c = 0; c < 3; c++) blk[i][c] = lvl[((size_t)yy * lw + xx) * 3 + c];
                    }
                    bool clear[16], got[16];
                    th_clear(orig + o + nb * 8, clear);
                    unsigned char b8[8];
                    th_block(blk, clear, b8);
                    th_clear(b8, got);
                    if (memcmp(clear, got, sizeof(clear)) != 0) return false;   // the transparency rule
                    data.insert(data.end(), b8, b8 + 8);
                }
            if (nb * 8 != sz) return false;
            o += sz;
            if (!(lw == 1 && lh == 1)) {                    // half()
                int nh = (std::max)(lh / 2, 1), nw = (std::max)(lw / 2, 1);
                std::vector<int> nx((size_t)nw * nh * 3);
                for (int y = 0; y < nh; y++)
                    for (int x = 0; x < nw; x++)
                        for (int c = 0; c < 3; c++) {
                            int sum = 0;
                            for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++)
                                sum += lvl[((size_t)(std::min)(2 * y + dy, lh - 1) * lw + (std::min)(2 * x + dx, lw - 1)) * 3 + c];
                            nx[((size_t)y * nw + x) * 3 + c] = sum / 4;
                        }
                lvl.swap(nx); lw = nw; lh = nh;
            }
        }
        if (data.size() > size) return false;
        data.insert(data.end(), orig + data.size(), orig + size);
        // write it back in the file (the resource buffer, then the trailing bytes, as they lie in the file)
        size_t ro = rd32(&file[0x0C]);
        memcpy(&file[ro + toff], data.data(), size);
        return true;
    }
    return false;
}
