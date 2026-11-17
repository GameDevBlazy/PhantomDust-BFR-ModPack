// ---- v5.66 designer outfits (owner 2026-11-02). The owner's own outfits (the MODS build) are
// painted designs: a dark base with neon circuit lines, gold ornament on black, glowing white, new trims - several colours
// per outfit, each piece its own. A design here does that from the stock outfit's own texture:
//  - the pieces: jacket (middle section) and pants (lower section) take different base colours (co_labels: the section of
//    the nearest piece vertex); skin stays (the theme skin test); the head / hair (upper section) keeps its own colours
//    unless the design colours it;
//  - the detail: every texel's local contrast (its brightness against a 5 x 5 blur) marks seams, trims, straps and folds;
//    a design paints that detail in an accent colour, as plain trim or as a glow;
//  - a pattern over the base: stripes, digital camo, a circuit grid, lava cracks, frost crystals, stars - drawn in the
//    texture's own space, shaded by the stock texture's brightness so the folds and creases stay.
struct Design {
    const char *name;
    int jacket[3], pants[3], accent[3], accent2[3];
    int pattern;        // 0 none, 1 circuit, 2 camo, 3 stripes, 4 lava, 5 frost, 6 stars, 7 split, 8 carbon
    double accent_amt;  // how much the detail takes the accent
    bool glow;          // the accent adds light (neon) instead of replacing
    double dark;        // base brightness scale (0.35 = a dark suit)
};
static const Design DESIGNS[] = {
    {"Neon Circuit",  {22, 24, 30},   {16, 16, 22},   {40, 230, 255}, {255, 40, 200}, 1, 1.0, true, 0.45},
    {"Royal Gold",    {20, 18, 16},   {24, 26, 44},   {236, 186, 60}, {250, 230, 150}, 8, 0.9, false, 0.5},
    {"Angelic",       {240, 238, 232},{210, 210, 216},{232, 196, 90}, {150, 220, 255}, 6, 0.6, true, 1.0},
    {"Street",        {196, 34, 40},  {60, 92, 150},  {240, 240, 236},{30, 30, 30},    3, 0.7, false, 0.9},
    {"Field Ops",     {92, 104, 58},  {160, 140, 100},{24, 26, 20},   {200, 190, 150}, 2, 0.6, false, 0.85},
    {"Inferno",       {26, 18, 16},   {40, 16, 10},   {255, 120, 20}, {255, 220, 60},  4, 1.0, true, 0.45},
    {"Frostbite",     {214, 236, 250},{120, 170, 220},{30, 50, 110},  {255, 255, 255}, 5, 0.6, false, 1.0},
    {"Hazard",        {232, 196, 28}, {32, 32, 34},   {20, 20, 20},   {20, 20, 20},    3, 0.8, false, 0.9},
    {"Galaxy",        {40, 26, 90},   {18, 14, 48},   {255, 120, 220},{140, 220, 255}, 6, 0.9, true, 0.6},
    {"Two-Tone Tide", {20, 150, 160}, {24, 26, 34},   {240, 240, 236},{20, 150, 160},  7, 0.7, false, 0.9},
    {"Crimson Shadow",{26, 24, 28},   {130, 14, 28},  {255, 40, 50},  {120, 10, 20},   8, 0.9, true, 0.5},
    // v5.71 the owner's themes (2026-11-02): our own palettes in the spirit of each theme - cyberpunk, dystopian, galaxy,
    // angelic, demonic, psychokinesis, the elements - for the outfit stock (Galaxy and Angelic above)
    {"Cyberpunk",     {250, 230, 40}, {30, 30, 40},   {0, 240, 255},  {255, 40, 160},  1, 1.0, true, 0.7},
    {"Dystopian",     {92, 86, 72},   {58, 54, 48},   {170, 60, 30},  {130, 120, 100}, 2, 0.7, false, 0.6},
    {"Digital Rain",  {14, 18, 14},   {10, 12, 10},   {60, 255, 90},  {180, 255, 190}, 3, 1.0, true, 0.4},
    {"Edgerunner",    {250, 220, 30}, {20, 22, 30},   {255, 60, 140}, {60, 220, 255},  7, 0.9, true, 0.75},
    {"Edgerunner II", {30, 34, 60},   {240, 60, 120}, {80, 255, 230}, {250, 220, 40},  1, 1.0, true, 0.6},
    {"Wild Beast",    {150, 100, 60}, {70, 50, 40},   {30, 20, 16},   {230, 200, 150}, 2, 0.8, false, 0.8},
    {"Samurai",       {150, 20, 26},  {24, 22, 26},   {220, 180, 80}, {240, 240, 230}, 7, 0.8, false, 0.75},
    {"Cosmic",        {16, 12, 40},   {50, 20, 80},   {140, 200, 255},{255, 255, 255}, 6, 1.0, true, 0.45},
    {"Seraph",        {250, 248, 240},{230, 228, 220},{250, 210, 110},{255, 255, 255}, 5, 0.7, true, 1.0},
    {"Demonic",       {24, 10, 12},   {60, 8, 12},    {255, 40, 20},  {255, 140, 30},  4, 1.0, true, 0.4},
    {"Psychokinesis", {70, 30, 120},  {24, 18, 40},   {200, 120, 255},{120, 220, 255}, 6, 1.0, true, 0.55},
    {"Fire Element",  {200, 50, 20},  {60, 20, 12},   {255, 160, 30}, {255, 240, 120}, 4, 1.0, true, 0.6},
    {"Ice Element",   {200, 230, 250},{110, 160, 210},{40, 110, 200}, {255, 255, 255}, 5, 0.7, false, 0.95},
    {"Thunder Element",{36, 34, 60},  {20, 20, 30},   {255, 240, 70}, {160, 220, 255}, 1, 1.0, true, 0.5},
    {"Water Element", {20, 90, 160},  {14, 50, 100},  {120, 220, 255},{230, 250, 255}, 3, 0.8, true, 0.7},
    {"Earth Element", {110, 80, 50},  {70, 90, 50},   {40, 30, 20},   {200, 170, 110}, 2, 0.8, false, 0.75},
    {"Air Element",   {220, 240, 236},{160, 200, 200},{90, 200, 180}, {255, 255, 255}, 7, 0.6, false, 0.95},
};
static const int DESIGN_N = (int)(sizeof(DESIGNS) / sizeof(DESIGNS[0]));
static const int DESIGN_SLOTS = 11, DESIGN_THEME0 = 11;    // 0-10 the 15-slot outfits (unchanged), 11+ the stock themes
static const char *ds_name(int d) { return d >= 0 && d < DESIGN_N ? DESIGNS[d].name : "?"; }
static int ds_count() { return DESIGN_N; }

static double ds_hash(int x, int y, int seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0;
}
static double ds_noise(double x, double y, int seed) {           // value noise, smooth
    int ix = (int)floor(x), iy = (int)floor(y);
    double fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    double a = ds_hash(ix, iy, seed), b = ds_hash(ix + 1, iy, seed), c = ds_hash(ix, iy + 1, seed), d = ds_hash(ix + 1, iy + 1, seed);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}
static double ds_fbm(double x, double y, int seed) {
    return 0.55 * ds_noise(x, y, seed) + 0.3 * ds_noise(2 * x, 2 * y, seed + 1) + 0.15 * ds_noise(4 * x, 4 * y, seed + 2);
}
// pattern value 0..1 at texel (x, y) of a 512 texture: how much the second colour / the pattern shows
static double ds_pattern(int pat, int x, int y, int &mode) {
    mode = 0;                                                    // 0 mix base -> accent2, 1 add light, 2 darken
    switch (pat) {
    case 1: {                                                    // circuit: grid lines with breaks and nodes
        int gx = x % 32, gy = y % 32;
        bool lx = gx < 2 && ds_hash(x / 32, y / 64, 7) > 0.35, ly = gy < 2 && ds_hash(x / 64, y / 32, 9) > 0.45;
        bool node = (gx - 16) * (gx - 16) + (gy - 16) * (gy - 16) < 9 && ds_hash(x / 32, y / 32, 3) > 0.7;
        mode = 1;
        return lx || ly ? 0.55 : node ? 0.9 : 0.0;
    }
    case 2: {                                                    // digital camo: blocky noise in 3 tones
        double n = ds_fbm((x / 8) * 0.09, (y / 8) * 0.09, 21);
        return n < 0.42 ? 0.0 : n < 0.55 ? 0.5 : 1.0;
    }
    case 3: return ((x + y) / 24) % 2 ? 1.0 : 0.0;               // diagonal stripes
    case 4: {                                                    // lava cracks: thin bright veins
        double n = ds_fbm(x * 0.02, y * 0.02, 41);
        double v = 1.0 - fabs(n - 0.5) * 22.0;
        mode = 1;
        return v > 0 ? v * 0.7 : 0.0;
    }
    case 5: {                                                    // frost: vertical fade + crystals
        double n = ds_noise(x * 0.08, y * 0.08, 55);
        return (y / 512.0) * 0.7 + (n > 0.82 ? 0.3 : 0.0);
    }
    case 6: {                                                    // stars + nebula
        double s = ds_hash(x, y, 77) > 0.9965 ? 1.0 : 0.0;
        if (s > 0) { mode = 1; return 1.0; }
        return ds_fbm(x * 0.012, y * 0.012, 66) * 0.6;
    }
    case 7: return ((x / 64) % 2) ? 1.0 : 0.0;                   // two-tone panels
    case 8: {                                                    // carbon weave
        int cx = (x / 4) % 2, cy = (y / 4) % 2;
        mode = 2;
        return cx ^ cy ? 0.25 : 0.0;
    }
    }
    return 0.0;
}

// paints design `d` over the colour texture of `file`, by sections (lab: per texel, 0 none / RG_*); false = no texture or
// the transparency rule
// v5.71 (owner 2026-11-02): each character gets
// its own take on a design - the colors turned around the hue wheel by up to +-25 degrees, accent / accent 2 swapped for
// half of them, and the pattern laid out from its own place - from a number made from the model's name (ds_build)
static unsigned g_ds_variant;
static void ds_hue(const int *in, double deg, int *out) {         // rotate a color's hue, keeping its brightness (YIQ)
    double y = 0.299 * in[0] + 0.587 * in[1] + 0.114 * in[2], i = 0.596 * in[0] - 0.274 * in[1] - 0.322 * in[2],
           q = 0.211 * in[0] - 0.523 * in[1] + 0.312 * in[2], a = deg * 3.14159265 / 180.0, c = cos(a), sn = sin(a);
    double i2 = i * c - q * sn, q2 = i * sn + q * c;
    double r = y + 0.956 * i2 + 0.621 * q2, g = y - 0.272 * i2 - 0.647 * q2, b = y - 1.106 * i2 + 1.703 * q2;
    out[0] = r < 0 ? 0 : r > 255 ? 255 : (int)r; out[1] = g < 0 ? 0 : g > 255 ? 255 : (int)g; out[2] = b < 0 ? 0 : b > 255 ? 255 : (int)b;
}
static bool ds_paint(Bytes &file, const std::vector<char> &lab, int d) {
    if (d < 0 || d >= DESIGN_N) return false;
    Design D = DESIGNS[d];
    if (g_ds_variant) {
        double deg = (int)(g_ds_variant % 51) - 25;              // +-25: a theme keeps its character (fire stays fiery)
        ds_hue(DESIGNS[d].jacket, deg, D.jacket); ds_hue(DESIGNS[d].pants, deg * 0.7, D.pants);
        ds_hue(DESIGNS[d].accent, deg * 0.5, D.accent); ds_hue(DESIGNS[d].accent2, deg * 0.5, D.accent2);
        if ((g_ds_variant >> 8) & 1) for (int k = 0; k < 3; k++) { int t = D.accent[k]; D.accent[k] = D.accent2[k]; D.accent2[k] = t; }
    }
    Alr m;
    if (!alr_parse(file, m)) return false;
    int ti, w, h, mips; unsigned toff; size_t size;
    if (!co_tex(m, ti, toff, w, h, mips, size)) return false;
    Bytes res = m.resource;
    res.insert(res.end(), m.trailing.begin(), m.trailing.end());
    if (toff + size > res.size() || (lab.size() && (int)lab.size() != w * h)) return false;
    const unsigned char *orig = &res[toff];
    std::vector<Px> px;
    th_decode(orig, w, h, px);
    std::vector<double> L(px.size()), B(px.size());
    for (size_t i = 0; i < px.size(); i++) L[i] = th_lum(px[i].r, px[i].g, px[i].b);
    {                                                            // a 5 x 5 box blur of the brightness (two passes)
        std::vector<double> t(px.size());
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                double s = 0; int n = 0;
                for (int k = -2; k <= 2; k++) { int xx = x + k; if (xx >= 0 && xx < w) { s += L[(size_t)y * w + xx]; n++; } }
                t[(size_t)y * w + x] = s / n;
            }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                double s = 0; int n = 0;
                for (int k = -2; k <= 2; k++) { int yy = y + k; if (yy >= 0 && yy < h) { s += t[(size_t)yy * w + x]; n++; } }
                B[(size_t)y * w + x] = s / n;
            }
    }
    std::vector<int> img((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t i = (size_t)y * w + x;
            int c[3] = {px[i].r, px[i].g, px[i].b};
            double hu, s, v;
            th_hsv(c[0], c[1], c[2], hu, s, v);
            bool is_skin = 8 <= hu && hu <= 36 && 0.22 <= s && s <= 0.72 && 0.22 <= v && v <= 0.98;
            int sec = lab.empty() ? RG_MIDDLE : lab[i];
            if (!(is_skin || sec == RG_UPPER || sec == 0)) {
                const int *base = sec == RG_LOWER ? D.pants : D.jacket;
                double shade = (0.30 + 1.45 * L[i]) * (sec == RG_LOWER && D.dark < 0.6 ? D.dark + 0.1 : D.dark);
                double col[3];
                for (int k = 0; k < 3; k++) col[k] = base[k] * shade;
                int mode;
                double p = ds_pattern(D.pattern, x + (int)(g_ds_variant >> 12 & 127), y + (int)(g_ds_variant >> 19 & 127), mode);
                if (p > 0) {
                    for (int k = 0; k < 3; k++) {
                        if (mode == 1) col[k] += D.accent2[k] * p * (0.6 + 0.6 * L[i]);
                        else if (mode == 2) col[k] *= (1.0 - p);
                        else col[k] = col[k] * (1 - p) + D.accent2[k] * shade * p;
                    }
                }
                double det = (fabs(L[i] - B[i]) - 0.06) * 7.0;     // seams, trims, straps (not the fabric's own grain)
                if (det < 0) det = 0;
                if (det > 1) det = 1;
                det *= D.accent_amt;
                for (int k = 0; k < 3; k++) {
                    if (D.glow) col[k] += D.accent[k] * det * 1.2;
                    else col[k] = col[k] * (1 - det) + D.accent[k] * (0.4 + 0.9 * L[i]) * det;
                }
                for (int k = 0; k < 3; k++) c[k] = (std::max)(0, (std::min)(255, (int)(col[k] + 0.5)));
            }
            img[i * 3] = c[0]; img[i * 3 + 1] = c[1]; img[i * 3 + 2] = c[2];
        }
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
                    for (int cc = 0; cc < 3; cc++) blk[i][cc] = lvl[((size_t)yy * lw + xx) * 3 + cc];
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
        if (!(lw == 1 && lh == 1)) {
            int nh = (std::max)(lh / 2, 1), nw = (std::max)(lw / 2, 1);
            std::vector<int> nx((size_t)nw * nh * 3);
            for (int yy = 0; yy < nh; yy++)
                for (int xx = 0; xx < nw; xx++)
                    for (int cc = 0; cc < 3; cc++) {
                        int sum = 0;
                        for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++)
                            sum += lvl[((size_t)(std::min)(2 * yy + dy, lh - 1) * lw + (std::min)(2 * xx + dx, lw - 1)) * 3 + cc];
                        nx[((size_t)yy * nw + xx) * 3 + cc] = sum / 4;
                    }
            lvl.swap(nx); lw = nw; lh = nh;
        }
    }
    if (data.size() > size) return false;
    data.insert(data.end(), orig + data.size(), orig + size);
    size_t ro = rd32(&file[0x0C]);
    memcpy(&file[ro + toff], data.data(), size);
    return true;
}

// a designer outfit for any fighter: players 0-10 by sections (jacket / pants), others on the whole texture
static bool ds_build(const char *stem, Bytes &out, int d, char *why, size_t nwhy) {
    g_ds_variant = 2166136261u;                                   // FNV-1a of the model's name: its own take on the design
    for (const char *c = stem; *c; c++) g_ds_variant = (g_ds_variant ^ (unsigned char)*c) * 16777619u;
    if (d < DESIGN_THEME0) g_ds_variant = 0;                       // the 15-slot outfits stay as they are
    std::vector<char> lab;
    bool player = !strncmp(stem, "pc", 2) && atoi(stem + 2) <= 10;
    if (player) {
        Alr T, R;
        if (alr_parse(out, T) && co_src(0, 0)) {
            g_cb_src = g_co_src;
            if (!(cb_load(0, 0, R) && co_labels(T, R, 512, lab))) lab.clear();
        }
    }
    if (!ds_paint(out, lab, d)) { sprintf_s(why, nwhy, "design %d not painted (texture / transparency rule)", d); return false; }
    return true;
}

// offline test (tests/of_test.cpp design mode): stem's outfit 1 with design d -> out_path
extern "C" __declspec(dllexport) int CwoDesignTest(const wchar_t *install, const wchar_t *data_dir, const char *stem, int d, const wchar_t *out_path) {
    wcscpy_s(g_install, install);
    wcscpy_s(g_dir, data_dir);
    wchar_t base[MAX_PATH];
    swprintf_s(base, L"%sAssets\\Data\\Player\\%S\\%S0.alr", g_install, stem, stem);
    DWORD n = 0;
    BYTE *b = pf_read(base, &n);
    if (!b || !n) { free(b); return -1; }
    Bytes out(b, b + n);
    free(b);
    char why[128] = "";
    if (!ds_build(stem, out, d, why, sizeof(why))) { logf("design test: %s", why); return -2; }
    return pf_write(out_path, out.data(), (DWORD)out.size()) ? 1 : -3;
}
