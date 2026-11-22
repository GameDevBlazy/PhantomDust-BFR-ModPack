// ---- v5.61 Create an Outfit (owner 2026-11-02)
// Edgar's Dream > Create an Outfit (entry 12, page EDP_OUTFIT): a character (players 0-10), the outfit (1-4) it starts from
// and replaces, a skin tone, a head accessory (another character's hair / hat / glasses, the Create a Character route),
// a jacket colour and a pants colour. "Preview live" shows it turning in the game's Character viewer; "Wear it" writes it
// over that outfit in the loader's game-file folder (this machine only: online, others see their own file for that
// outfit); "Take it off" puts the outfit back.
// Facts used (our own reading of the stock models, checked on Edgar pc01a0.alr 2026-11-02 with a picture of the result;
// the model format facts of cwo_charbuild2.inl / BODY_PARTS_RESEARCH.md): skinned vertices (code 0x11, 24 bytes) carry the
// texture position at +12 as two 16-bit values over 32,768 (0 .. 32,767 = 0 .. 1, not flipped); a piece's section
// (upper = neck subtree, lower = pelvis + legs, middle = the rest of the spine) is the one that carries most of its weight
// (build_section_swap rule, through the reference skeleton pc00a0); the colour texture is the first 512 x 512 DXT1 one.
// Every texel takes the section of the nearest piece vertex (breadth-first growth from the vertices); skin pixels (the
// outfit theme's skin test) take the skin tone; the rest of a middle / lower texel takes the chosen colour with its own
// shading. The DXT1 chain is encoded as the outfit themes do (every mip, transparency kept and checked).
static const char *CO_COLOURS[] = {"the outfit's own", "Black", "White", "Gray", "Red", "Crimson", "Orange", "Yellow", "Gold",
                                   "Green", "Teal", "Blue", "Navy", "Purple", "Pink", "Brown"};
static const int CO_RGB[16][3] = {{0, 0, 0}, {28, 28, 32}, {236, 236, 232}, {128, 128, 132}, {200, 30, 30}, {120, 12, 24},
                                  {230, 110, 20}, {236, 210, 40}, {200, 160, 50}, {40, 150, 50}, {20, 140, 140}, {40, 90, 210},
                                  {24, 34, 96}, {120, 50, 170}, {230, 100, 170}, {110, 70, 40}};
static const char *CO_SKINS[] = {"the character's own", "Fair", "Light", "Medium", "Olive", "Tan", "Brown", "Deep"};
static const int CO_SKIN_RGB[8][3] = {{0, 0, 0}, {244, 214, 196}, {232, 190, 160}, {206, 156, 118}, {184, 140, 96},
                                      {170, 112, 72}, {120, 74, 44}, {74, 44, 28}};
static const int CO_NCOL = 16, CO_NSKIN = 8, CO_NCHAR = 11;
// v5.70 sliders (owner 2026-11-02):
// color value 0 = the outfit's own, 1-5 = black .. white, 6-41 = 36 hues 10 degrees apart; skin 0 = the character's own,
// 1-15 = a ramp from fair to deep through the 7 tones
// CO_SLIDE_MAX / CO_SKIN_MAX: cwo_ccprev.inl (included first; its slider bar uses them)
static void co_rgb(int v, int *o) {
    if (v >= 1 && v <= 5) { int g = 20 + (v - 1) * 56; o[0] = o[1] = o[2] = g; return; }
    double h = ((v - 6) % 36) * 10.0, s = 0.78, val = 0.86, c = val * s, x = c * (1 - fabs(fmod(h / 60.0, 2) - 1)), m = val - c, r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    o[0] = (int)((r + m) * 255); o[1] = (int)((g + m) * 255); o[2] = (int)((b + m) * 255);
}
static void co_skin_rgb(int v, int *o) {
    double t = (v - 1) / (double)(CO_SKIN_MAX - 1) * 6.0;      // across the 7 tones (CO_SKIN_RGB 1-7)
    int a = (int)t; if (a > 5) a = 5;
    double f = t - a;
    for (int k = 0; k < 3; k++) o[k] = (int)(CO_SKIN_RGB[1 + a][k] * (1 - f) + CO_SKIN_RGB[2 + a][k] * f);
}

static wchar_t g_co_src[MAX_PATH], g_co_prev_file[MAX_PATH];
static char g_co_target[24];                                   // "pc02a" while the preview runs: its outfit files read ours
static volatile LONG g_co_redirect, g_co_dirty;                 // g_co_prev: cwo_edgar.inl
static ULONGLONG g_co_t, g_co_change_t, g_co_load_t;
static int g_co_old_char = -1;
static void co_mark_dirty() { g_co_change_t = GetTickCount64(); InterlockedExchange(&g_co_dirty, 1); }

// the clean copies the builder reads (players' outfits and the reference pc00a0): <data>\charsrc\pcNNa<k>.alr, copied from
// the game's own file the first time one is needed
static bool co_src(int ch, int k) {
    wchar_t dst[MAX_PATH], src[MAX_PATH];
    swprintf_s(g_co_src, L"%scharsrc\\", g_dir);
    CreateDirectoryW(g_co_src, NULL);
    swprintf_s(dst, L"%spc%02da%d.alr", g_co_src, ch, k);
    if (GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES) return true;
    swprintf_s(src, L"%sAssets\\Data\\Player\\pc%02da\\pc%02da%d.alr", g_install, ch, ch, k);
    DWORD n = 0;
    BYTE *b = pf_read(src, &n);                                 // the game's own file (not an override)
    bool ok = b && n && pf_write(dst, b, n);
    free(b);
    if (!ok) logf("create an outfit: pc%02da%d.alr not readable from the game folder", ch, k);
    return ok;
}
static bool of_build(const char *stem, int k, Bytes &out, char *why, size_t nwhy);   // cwo_outfits15.inl
static bool co_base_path(int ch, int k, wchar_t *out) {          // the file the outfit starts from
    swprintf_s(out, MAX_PATH, L"%soutfit_base\\pc%02da%d.alr", g_dir, ch, k);   // the one before we wore ours
    if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return true;
    // v5.84 (owner 2026-11-02): outfits 5-15 are not game files -
    // the loader builds them into its own game folder the first time one is picked; look there, and build it if not yet
    if (g_over[0]) {
        swprintf_s(out, MAX_PATH, L"%sAssets\\Data\\Player\\pc%02da\\pc%02da%d.alr", g_over, ch, ch, k);
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    swprintf_s(out, MAX_PATH, L"%sAssets\\Data\\Player\\pc%02da\\pc%02da%d.alr", g_install, ch, ch, k);   // the game's (or an override)
    if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES || !g_over[0] || k < 4) return true;
    char stem[8], why[128] = "";
    sprintf_s(stem, "pc%02da", ch);
    Bytes b;
    wchar_t ov[MAX_PATH], dir[MAX_PATH];
    swprintf_s(ov, MAX_PATH, L"%sAssets\\Data\\Player\\pc%02da\\pc%02da%d.alr", g_over, ch, ch, k);
    swprintf_s(dir, MAX_PATH, L"%sAssets\\Data\\Player\\pc%02da", g_over, ch);
    for (wchar_t *c = dir + wcslen(g_over); *c; c++) if (*c == L'\\') { *c = 0; CreateDirectoryW(dir, NULL); *c = L'\\'; }
    CreateDirectoryW(dir, NULL);
    if (of_build(stem, k, b, why, sizeof(why)) && pf_write(ov, b.data(), (DWORD)b.size())) {
        logf("create an outfit: %s%d built first (%zu bytes)", stem, k, b.size());
        wcscpy_s(out, MAX_PATH, ov);
    } else logf("create an outfit: %s%d could not be built (%s)", stem, k, why);
    return true;
}

// the colour texture of a parsed model: index in the texture table, offset in the resource, size and mips; false = none
static bool co_tex(const Alr &m, int &ti, unsigned &toff, int &w, int &h, int &mips, size_t &size) {
    if (m.tex.size() < 12) return false;
    int ntex = (short)rd16(&m.tex[8]);
    for (ti = 0; ti < ntex; ti++) {
        const unsigned char *t = &m.tex[12 + 28 * ti];
        unsigned packed = rd32(t + 12);
        toff = rd32(t + 4);
        w = 1 << ((packed >> 20) & 15); h = 1 << ((packed >> 24) & 15);
        int fmt = (packed >> 8) & 255;
        mips = (packed >> 16) & 15;
        if (fmt != 0x0C || w != 512 || h != 512) continue;
        size = 0;
        for (int k = 0; k < (std::max)(mips, 1); k++) { int mw = (std::max)(w >> k, 1), mh = (std::max)(h >> k, 1); size += (size_t)(std::max)(mw / 4, 1) * (std::max)(mh / 4, 1) * 8; }
        size = (size + 0x7F) / 0x80 * 0x80;
        if ((packed & 0xFF) & 4) size *= 6;
        return true;
    }
    return false;
}

// each texel's section (RG_UPPER / RG_MIDDLE / RG_LOWER, 0 = none) from the nearest piece vertex
static const int RG_FOOT_CO = 6;                                 // v5.188.60 boots (outfit labels only)
static const int RG_HAND_CO = 7;                                 // v5.188.61 gloves (outfit labels only)
static bool co_labels(const Alr &T, const Alr &R, int W, std::vector<char> &lab, bool arms = false, bool feet = false,
                      bool hands = false) {
    JMap jT;
    if (!ref_map(R, T, false, &T, jT)) return false;
    std::vector<int> tpar;
    if (!skeleton(T, tpar, NULL) && tpar.empty()) skel_any(T, 0, &tpar, NULL);
    if (tpar.empty()) return false;
    auto root = [&](int k) { return jT.has(k) ? jT.get(k) : -1; };
    std::set<int> up = root(70) >= 0 ? subtree(tpar, root(70)) : std::set<int>(), lo, mid;
    if (root(22) >= 0) lo.insert(root(22));
    if (root(82) >= 0) { auto s = subtree(tpar, root(82)); lo.insert(s.begin(), s.end()); }
    if (root(113) >= 0) { auto s = subtree(tpar, root(113)); lo.insert(s.begin(), s.end()); }
    if (root(23) >= 0) { auto s = subtree(tpar, root(23)); for (int j : s) if (!up.count(j)) mid.insert(j); }
    // v5.188.59 arms (reference pc00a0: the clavicle chains 25 and 47) - only when the Sleeves color is set
    std::set<int> armj;
    if (arms) for (int r0 : {25, 47}) if (root(r0) >= 0) { auto s = subtree(tpar, root(r0)); armj.insert(s.begin(), s.end()); }
    std::set<int> footj;                                          // v5.188.60 feet (reference pc00a0: the ankles 87 and 101)
    if (feet) for (int r0 : {87, 101}) if (root(r0) >= 0) { auto s = subtree(tpar, root(r0)); footj.insert(s.begin(), s.end()); }
    std::set<int> handj;                                          // v5.188.61 hands (reference pc00a0: the wrists 31 and 52)
    if (hands) for (int r0 : {31, 52}) if (root(r0) >= 0) { auto s = subtree(tpar, root(r0)); handj.insert(s.begin(), s.end()); }
    std::vector<int> treg(tpar.size(), RG_NONE);
    for (int j = 0; j < (int)tpar.size(); j++) treg[j] = up.count(j) ? RG_UPPER : lo.count(j) ? RG_LOWER : mid.count(j) ? RG_MIDDLE : RG_NONE;
    std::vector<std::pair<Piece, Shares>> sh;
    piece_shares(T, [&](int j) { return j >= 0 && j < (int)treg.size() ? treg[j] : RG_NONE; }, sh);
    int bsg = body_slot(T);
    if (bsg < 0 || sh.empty()) return false;
    const Bytes &g = T.groups[T.slot[bsg]];
    std::vector<Piece> pcs; std::vector<int> codes; std::vector<size_t> descs;
    pieces(g, pcs, codes, descs);
    size_t d = 0;
    for (size_t i = 0; i < codes.size(); i++) if (codes[i] == 0x11) { d = descs[i]; break; }
    unsigned off = rd32(&g[d + 0x14]);
    const Bytes &vb = T.resource;
    lab.assign((size_t)W * W, 0);
    std::vector<int> q;
    q.reserve((size_t)W * W);
    for (auto &pr : sh) {
        int sec = section_of(pr.second);
        if (sec != RG_UPPER && sec != RG_MIDDLE && sec != RG_LOWER) sec = RG_MIDDLE;
        for (unsigned v = pr.first.base; v < pr.first.base + pr.first.used; v++) {
            size_t o = off + 24 * (size_t)v;
            if (o + 24 > vb.size()) break;
            int u = (unsigned short)rd16(&vb[o + 12]) & 0x7FFF, t = (unsigned short)rd16(&vb[o + 14]) & 0x7FFF;
            int x = (int)((long long)u * W / 32768), y = (int)((long long)t * W / 32768);
            size_t p = (size_t)y * W + x;
            int vsec = sec;
            if (!armj.empty() && sec == RG_MIDDLE) {               // v5.188.59 this vertex's strongest joint is an arm joint
                int ca = vb[o + 20], wa = vb[o + 18], cbb = vb[o + 22], wb = vb[o + 17];
                int slot = pyfloordiv((wa >= wb ? ca : cbb) - 120, 3);
                if (slot >= 0 && slot < (int)pr.first.pal.size() && armj.count(pr.first.pal[slot])) vsec = RG_ARM;
            }
            if (!handj.empty() && sec == RG_MIDDLE) {              // v5.188.61 this vertex's strongest joint is a hand joint
                int ca = vb[o + 20], wa = vb[o + 18], cbb = vb[o + 22], wb = vb[o + 17];
                int slot = pyfloordiv((wa >= wb ? ca : cbb) - 120, 3);
                if (slot >= 0 && slot < (int)pr.first.pal.size() && handj.count(pr.first.pal[slot])) vsec = RG_HAND_CO;
            }
            if (!footj.empty() && sec == RG_LOWER) {               // v5.188.60 this vertex's strongest joint is a foot joint
                int ca = vb[o + 20], wa = vb[o + 18], cbb = vb[o + 22], wb = vb[o + 17];
                int slot = pyfloordiv((wa >= wb ? ca : cbb) - 120, 3);
                if (slot >= 0 && slot < (int)pr.first.pal.size() && footj.count(pr.first.pal[slot])) vsec = RG_FOOT_CO;
            }
            if (!lab[p]) { lab[p] = (char)vsec; q.push_back((int)p); }
        }
    }
    for (size_t h = 0; h < q.size(); h++) {                      // breadth-first: each texel the nearest seed's section
        int p = q[h], x = p % W, y = p / W;
        const int nx[4] = {x + 1, x - 1, x, x}, ny[4] = {y, y, y + 1, y - 1};
        for (int k = 0; k < 4; k++) {
            if (nx[k] < 0 || nx[k] >= W || ny[k] < 0 || ny[k] >= W) continue;
            size_t n = (size_t)ny[k] * W + nx[k];
            if (!lab[n]) { lab[n] = lab[p]; q.push_back((int)n); }
        }
    }
    return true;
}

static void co_tint(const int *rgb, double L, int *c) {          // the chosen colour with the pixel's own shading
    for (int k = 0; k < 3; k++) {
        double v = rgb[k] * (0.25 + 1.5 * L);
        c[k] = (std::max)(0, (std::min)(255, (int)(v + 0.5)));
    }
}

// recolours the colour texture of `file` (a whole .alr) by sections; false = no colour texture or the transparency rule
static bool co_recolour(Bytes &file, const std::vector<char> &lab, int skin, int top, int bottom, int arms = 0, int boots = 0,
                        int gloves = 0) {
    Alr m;
    if (!alr_parse(file, m)) return false;
    int ti, w, h, mips; unsigned toff; size_t size;
    if (!co_tex(m, ti, toff, w, h, mips, size)) return false;
    Bytes res = m.resource;
    res.insert(res.end(), m.trailing.begin(), m.trailing.end());
    if (toff + size > res.size() || (int)lab.size() != w * h) return false;
    const unsigned char *orig = &res[toff];
    std::vector<Px> px;
    th_decode(orig, w, h, px);
    std::vector<int> img((size_t)w * h * 3);
    for (size_t i = 0; i < px.size(); i++) {
        int c[3] = {px[i].r, px[i].g, px[i].b};
        double hu, s, v;
        th_hsv(c[0], c[1], c[2], hu, s, v);
        bool is_skin = 8 <= hu && hu <= 36 && 0.22 <= s && s <= 0.72 && 0.22 <= v && v <= 0.98;
        double L = th_lum(c[0], c[1], c[2]);
        int rgb[3];
        if (is_skin && skin > 0 && lab[i] != RG_LOWER && lab[i] != RG_FOOT_CO) { co_skin_rgb(skin, rgb); co_tint(rgb, L, c); }
        else if (!is_skin && lab[i] == RG_FOOT_CO && boots > 0) { co_rgb(boots, rgb); co_tint(rgb, L, c); }   // v5.188.60 boots
        else if (!is_skin && lab[i] == RG_HAND_CO && gloves > 0) { co_rgb(gloves, rgb); co_tint(rgb, L, c); }   // v5.188.61 gloves
        else if (!is_skin && (lab[i] == RG_ARM || lab[i] == RG_HAND_CO) && arms > 0) { co_rgb(arms, rgb); co_tint(rgb, L, c); }   // v5.188.59 sleeves
        else if (!is_skin && (lab[i] == RG_MIDDLE || lab[i] == RG_ARM || lab[i] == RG_HAND_CO) && top > 0) { co_rgb(top, rgb); co_tint(rgb, L, c); }
        else if (!is_skin && (lab[i] == RG_LOWER || lab[i] == RG_FOOT_CO) && bottom > 0) { co_rgb(bottom, rgb); co_tint(rgb, L, c); }
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
        if (!(lw == 1 && lh == 1)) {
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
    size_t ro = rd32(&file[0x0C]);
    memcpy(&file[ro + toff], data.data(), size);
    return true;
}

// the whole outfit: v[0] character, v[1] outfit 1-4, v[2] skin, v[3] head accessory (-1 none), v[4] jacket, v[5] pants,
// v[17] sleeves (v5.188.59), v[18] boots (v5.188.60), v[19] gloves (v5.188.61)
static bool co_build(const int *v, Bytes &out, char *why, size_t nwhy) {
    int ch = v[0], k = v[1] - 1;
    if (ch < 0 || ch >= CO_NCHAR || k < 0 || k > 14) { sprintf_s(why, nwhy, "no such character / outfit"); return false; }   // v5.69 15 slots
    wchar_t base[MAX_PATH];
    co_base_path(ch, k, base);
    if (!read_all(base, out)) { sprintf_s(why, nwhy, "the outfit file is not readable"); return false; }
    Alr T, R;
    if (!alr_parse(out, T)) { sprintf_s(why, nwhy, "the outfit file is not a model"); return false; }
    if (!co_src(0, 0)) { sprintf_s(why, nwhy, "the reference model is not readable"); return false; }
    g_cb_src = g_co_src;
    if (!cb_load(0, 0, R)) { sprintf_s(why, nwhy, "the reference model is not readable"); return false; }
    int hx = v[3];
    if (hx >= 0 && hx != ch) {
        if (hx > 10) { sprintf_s(why, nwhy, "head accessories come from the 11 players"); return false; }
        if (!cb_part_allowed(ch, hx)) { sprintf_s(why, nwhy, "that accessory is not allowed on this body (male / female rule)"); return false; }
        Alr D, o;
        if (!co_src(hx, 0) || !cb_load_any(hx, 0, D)) { sprintf_s(why, nwhy, "the accessory's model is not readable"); return false; }
        if (!section_swap_any(T, D, R, ch, hx, T, RG_UPPER, true, false, o, why, nwhy, true)) return false;
        out = alr_build(o);
        if (!alr_parse(out, T)) { sprintf_s(why, nwhy, "rebuild failed"); return false; }
    }
    if (v[2] > 0 || v[4] > 0 || v[5] > 0 || v[17] > 0 || v[18] > 0 || v[19] > 0) {   // v5.188.61 v[19] Gloves   // v5.188.59 v[17] Sleeves, v5.188.60 v[18] Boots
        std::vector<char> lab;
        if (!co_labels(T, R, 512, lab, v[17] > 0, v[18] > 0, v[19] > 0)) { sprintf_s(why, nwhy, "the outfit's pieces could not be sorted into jacket / pants"); return false; }
        if (!co_recolour(out, lab, v[2], v[4], v[5], v[17], v[18], v[19])) { sprintf_s(why, nwhy, "the colors could not be written (texture or transparency rule)"); return false; }
    }
    return true;
}

// ---- wear / take off (this machine's game-file folder)
static void co_game_path(int ch, int k, wchar_t *out) { swprintf_s(out, MAX_PATH, L"%sAssets\\Data\\Player\\pc%02da\\pc%02da%d.alr", g_install, ch, ch, k); }
static bool co_put(int ch, int k, const Bytes &b, char *msg, size_t n);
static void ofx_set(const char *stem, int outfit, int preset);   // v5.75 cwo_outfitfx.inl
static void ofx_set_full(const char *stem, int outfit, const int *fx);   // v5.79 preset + placement, offsets, size, speed
static void ofx_stock_write(const wchar_t *alr, const int *fx);
static bool ofx_stock_read(const wchar_t *alr, int *fx);
static bool co_wear(const int *v, char *msg, size_t n) {            // v5.69 Save and apply: into the Save to slot row's slot
    int ch = v[0], k = v[6] > 0 ? v[6] - 1 : v[1] - 1;
    Bytes b;
    char why[128] = "";
    if (!co_build(v, b, why, sizeof(why))) { sprintf_s(msg, n, "Not built: %s", why); return false; }
    bool ok = co_put(ch, k, b, msg, n);
    if (ok) { char st[8]; sprintf_s(st, "pc%02da", ch); ofx_set_full(st, k + 1, v + 8); }   // v5.75 the slot's effect (v5.79 all of it)
    return ok;
}
// a built outfit into slot k (0-14) of character ch: the file before ours is kept once (Take it off puts it back)
static bool co_put(int ch, int k, const Bytes &b, char *msg, size_t n) {
    if (ch < 0 || ch >= CO_NCHAR || k < 0 || k > 14) { sprintf_s(msg, n, "Not saved: no such slot"); return false; }
    wchar_t gp[MAX_PATH], bp[MAX_PATH], cur[MAX_PATH];
    co_game_path(ch, k, gp);
    swprintf_s(bp, L"%soutfit_base\\pc%02da%d.alr", g_dir, ch, k);
    if (GetFileAttributesW(bp) == INVALID_FILE_ATTRIBUTES) {     // first time: the file before ours is kept
        Bytes before;
        wcscpy_s(cur, gp);
        if (!read_all(cur, before)) { sprintf_s(msg, n, "Not saved: slot %d has no outfit file to keep", k + 1); return false; }
        if (!pf_write(bp, before.data(), (DWORD)before.size())) { sprintf_s(msg, n, "Not worn: could not keep the original"); return false; }
    }
    FILE *f = NULL;
    if (cwo_wfopen(&f, gp, L"wb") != 0 || !f) { sprintf_s(msg, n, "Not worn: the game-file folder is not writable"); return false; }
    bool ok = fwrite(b.data(), 1, b.size(), f) == b.size();
    fclose(f);
    sprintf_s(msg, n, ok ? "Saved and applied: %s slot %d (this machine)" : "Not saved: write failed", ED_CHARAS[ch], k + 1);
    logf("create an outfit: %s slot %d %s (%zu bytes)", ED_CHARAS[ch], k + 1, ok ? "applied" : "NOT written", b.size());
    return ok;
}
static bool co_unwear(const int *v, char *msg, size_t n) {
    int ch = v[0], k = v[6] > 0 ? v[6] - 1 : v[1] - 1;
    if (ch < 0 || ch >= CO_NCHAR || k < 0 || k > 14) return false;
    wchar_t gp[MAX_PATH], bp[MAX_PATH];
    co_game_path(ch, k, gp);
    swprintf_s(bp, L"%soutfit_base\\pc%02da%d.alr", g_dir, ch, k);
    Bytes before;
    if (!read_all(bp, before)) { sprintf_s(msg, n, "%s outfit %d is the original already", ED_CHARAS[ch], k + 1); return false; }
    FILE *f = NULL;
    if (cwo_wfopen(&f, gp, L"wb") != 0 || !f) { sprintf_s(msg, n, "Not changed: the game-file folder is not writable"); return false; }
    bool ok = fwrite(before.data(), 1, before.size(), f) == before.size();
    fclose(f);
    if (ok) { wchar_t kept[MAX_PATH]; swprintf_s(kept, L"%s.taken_off", bp); MoveFileExW(bp, kept, MOVEFILE_REPLACE_EXISTING); }
    sprintf_s(msg, n, ok ? "%s outfit %d is the original again" : "Not changed: write failed", ED_CHARAS[ch], k + 1);
    logf("create an outfit: %s outfit %d taken off", ED_CHARAS[ch], k + 1);
    return ok;
}

// ---- v5.69 the outfit stock (owner 2026-11-02). Each character's stock is
// <data>\OutfitStock\pcNNa\ (up to 50 .alr files, named "NN name.alr"); any .alr put there by hand (an Unreal export, a
// friend's outfit) is listed too. Apply copies the chosen one into the Save to slot, as Save and apply does.
static const int CO_STOCK_MAX = 50;
static void co_stock_dir(int ch, wchar_t *out) {
    swprintf_s(out, MAX_PATH, L"%sOutfitStock", g_dir);
    CreateDirectoryW(out, NULL);
    swprintf_s(out, MAX_PATH, L"%sOutfitStock\\pc%02da\\", g_dir, ch);
    CreateDirectoryW(out, NULL);
}
static int co_stock_list(int ch, wchar_t names[][MAX_PATH], int cap) {   // the stock's files, sorted by name
    wchar_t dir[MAX_PATH], pat[MAX_PATH];
    co_stock_dir(ch, dir);
    swprintf_s(pat, L"%s*.alr", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    int n = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n < cap) wcscpy_s(names[n++], fd.cFileName); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (int i = 1; i < n; i++)                                  // insertion sort: few files
        for (int j = i; j > 0 && _wcsicmp(names[j - 1], names[j]) > 0; j--) { wchar_t t[MAX_PATH]; wcscpy_s(t, names[j]); wcscpy_s(names[j], names[j - 1]); wcscpy_s(names[j - 1], t); }
    return n;
}
static const char *co_stock_label(int ch, int idx) {             // "3 Fire Edgar" for the Outfit stock row (idx from 1)
    static char t[48];
    static wchar_t names[CO_STOCK_MAX][MAX_PATH];
    if (ch < 0 || ch >= CO_NCHAR) return "-";
    int n = co_stock_list(ch, names, CO_STOCK_MAX);
    if (n == 0) return "empty - save one first";                   // v5.175 fits beside the model
    if (idx < 1 || idx > n) { sprintf_s(t, "%d of %d (none here)", idx, n); return t; }
    char nm[40];
    WideCharToMultiByte(CP_UTF8, 0, names[idx - 1], -1, nm, sizeof(nm), NULL, NULL);
    size_t l = strlen(nm);
    if (l > 4) nm[l - 4] = 0;                                    // without ".alr"
    sprintf_s(t, "%d of %d: %.28s", idx, n, nm);
    return t;
}
static bool co_stock_save(const EdItem &it, char *msg, size_t n) {
    int ch = it.v[0];
    Bytes b;
    char why[128] = "";
    if (!co_build(it.v, b, why, sizeof(why))) { sprintf_s(msg, n, "Not saved: %s", why); return false; }
    static wchar_t names[CO_STOCK_MAX][MAX_PATH];
    int have = co_stock_list(ch, names, CO_STOCK_MAX);
    if (have >= CO_STOCK_MAX) { sprintf_s(msg, n, "The stock is full (%d) - delete one from OutfitStock\\pc%02da", CO_STOCK_MAX, ch); return false; }
    wchar_t dir[MAX_PATH], path[MAX_PATH], wn[32];
    co_stock_dir(ch, dir);
    MultiByteToWideChar(CP_UTF8, 0, it.name[0] ? it.name : "Outfit", -1, wn, 32);
    for (wchar_t *c = wn; *c; c++) if (wcschr(L"\\/:*?\"<>|", *c)) *c = L'_';
    int num = have + 1;
    for (;; num++) { swprintf_s(path, L"%s%02d %s.alr", dir, num, wn); if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) break; }
    bool ok = pf_write(path, b.data(), (DWORD)b.size());
    if (ok) ofx_stock_write(path, it.v + 8);                      // v5.79 its effect beside it (.fx)
    sprintf_s(msg, n, ok ? "Saved to %s's outfit stock (%d of %d)" : "Not saved: the stock folder is not writable", ED_CHARAS[ch], have + 1, CO_STOCK_MAX);
    logf("outfit stock: %s %s (%zu bytes)", ED_CHARAS[ch], ok ? "saved" : "NOT saved", b.size());
    return ok;
}
static bool co_stock_apply(const int *v, char *msg, size_t n) {
    int ch = v[0], idx = v[7], k = v[6] > 0 ? v[6] - 1 : v[1] - 1;
    static wchar_t names[CO_STOCK_MAX][MAX_PATH];
    int have = co_stock_list(ch, names, CO_STOCK_MAX);
    if (idx < 1 || idx > have) { sprintf_s(msg, n, have ? "Choose a stock outfit (1-%d) first" : "The stock is empty: Save to the outfit stock first", have); return false; }
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    co_stock_dir(ch, dir);
    swprintf_s(path, L"%s%s", dir, names[idx - 1]);
    Bytes b;
    Alr m;
    if (!read_all(path, b) || !alr_parse(b, m)) { sprintf_s(msg, n, "Not applied: stock outfit %d is not a model file", idx); return false; }
    bool ok = co_put(ch, k, b, msg, n);
    int fx[9] = {0};                                              // v5.93 + the aura colour
    if (ok && ofx_stock_read(path, fx)) { char st[8]; sprintf_s(st, "pc%02da", ch); ofx_set_full(st, k + 1, fx); }   // v5.79 its effect too
    return ok;
}
// v5.69 (owner 2026-11-02): the
// viewer draws with the menu's lights; light set n = 0x1403e8a40 + 0x58 n (FUN_140199510: three BGRA colours at +0 / +4 / +8
// turned into the light constants). In the menus set 1's first colour is pure green (00 ff 00, read 2026-11-02) - the
// preview gets set 0's neutral colours in set 1 while it runs, and the menu's own back after.
static const ULONG_PTR RVA_LIGHT_SETS = 0x3e8a40, RVA_LIGHTS = 0x5a6a40;   // FUN_140199510: set n -> lights 8n + 0x40 / + 0x41 (0x70 each)
static unsigned char g_co_light_keep[12], g_co_lights_keep[2 * 0x70];
static bool g_co_light_on;
static void co_light_neutral(bool on) {                          // every frame while a preview shows (on), once after (off)
    __try {
        unsigned char *s0 = (unsigned char *)(g_base + RVA_LIGHT_SETS), *s1 = s0 + 0x58;
        unsigned char *l0 = (unsigned char *)(g_base + RVA_LIGHTS) + 0x40 * 0x70, *l1 = (unsigned char *)(g_base + RVA_LIGHTS) + 0x48 * 0x70;
        if (on) {
            if (!g_co_light_on) {
                memcpy(g_co_light_keep, s1, 12); memcpy(g_co_lights_keep, l1, sizeof(g_co_lights_keep)); g_co_light_on = true;
                logf("preview light: set 1 neutral (was %02x %02x %02x)", g_co_light_keep[0], g_co_light_keep[1], g_co_light_keep[2]);
            }
            memcpy(s1, s0, 12);
            for (int k = 0; k < 2; k++) memcpy(l1 + 0x70 * k, l0 + 0x70 * k, 0x20);   // the colours (+0, +0x10), not the direction
        } else if (g_co_light_on) {
            memcpy(s1, g_co_light_keep, 12); memcpy(l1, g_co_lights_keep, sizeof(g_co_lights_keep)); g_co_light_on = false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// v5.71 "Add the theme outfits to the stock": every theme design (cwo_design.inl, 11+) painted on the character's outfit 1
// and saved into its stock, in the background (each takes a moment); names already there are skipped
static volatile LONG g_co_theme_busy;
static bool ds_build(const char *stem, Bytes &out, int d, char *why, size_t nwhy);
static const char *ds_name(int d);
static int ds_count();
static const int DESIGN_THEME0_FWD = 11;
static DWORD WINAPI co_theme_thread(LPVOID p) {
    int ch = (int)(INT_PTR)p, made = 0, skipped = 0, failed = 0;
    char stem[8]; sprintf_s(stem, "pc%02da", ch);
    wchar_t dir[MAX_PATH], base[MAX_PATH];
    co_stock_dir(ch, dir);
    co_base_path(ch, 0, base);
    static wchar_t names[CO_STOCK_MAX][MAX_PATH];
    for (int d = DESIGN_THEME0_FWD; d < ds_count(); d++) {
        int have = co_stock_list(ch, names, CO_STOCK_MAX);
        wchar_t wn[40], path[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, ds_name(d), -1, wn, 40);
        bool dup = false;
        for (int i = 0; i < have; i++) if (wcsstr(names[i], wn)) dup = true;
        if (dup) { skipped++; continue; }
        if (have >= CO_STOCK_MAX) { failed++; break; }
        Bytes b; char why[128] = "";
        if (!read_all(base, b) || !ds_build(stem, b, d, why, sizeof(why))) { failed++; logf("outfit stock: theme %s for %s not built (%s)", ds_name(d), ED_CHARAS[ch], why); continue; }
        int num = have + 1;
        for (;; num++) { swprintf_s(path, L"%s%02d %s.alr", dir, num, wn); if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) break; }
        if (pf_write(path, b.data(), (DWORD)b.size())) made++; else failed++;
    }
    char msg[96];
    sprintf_s(msg, "%s's stock: %d theme outfits added%s", ED_CHARAS[ch], made, failed ? " (some failed: see the log)" : skipped ? " (the rest were there)" : "");
    set_status(msg);
    logf("outfit stock: %s themes - %d added, %d already there, %d failed", ED_CHARAS[ch], made, skipped, failed);
    InterlockedExchange(&g_co_theme_busy, 0);
    return 0;
}
static void co_themes_to_stock(const int *v, char *msg, size_t n) {
    if (InterlockedCompareExchange(&g_co_theme_busy, 1, 0) != 0) { sprintf_s(msg, n, "Still adding the themes - wait a moment"); return; }
    HANDLE t = CreateThread(NULL, 0, co_theme_thread, (LPVOID)(INT_PTR)v[0], 0, NULL);
    if (t) { CloseHandle(t); sprintf_s(msg, n, "Adding %d theme outfits to %s's stock...", ds_count() - DESIGN_THEME0_FWD, ED_CHARAS[v[0]]); }
    else { InterlockedExchange(&g_co_theme_busy, 0); sprintf_s(msg, n, "Could not start"); }
}
// the right stick turns the previewed model (the viewer's fighter, +0x110; its facing as the game's actors)
static float g_co_turn;
static void co_stick_turn(ULONG_PTR viewer) {
    float rx = g_co_rx;
    if (rx > -0.2f && rx < 0.2f) return;
    ULONG_PTR f = *(ULONG_PTR *)(viewer + 0x110);
    if (!f) return;
    g_co_turn += rx * 0.06f;
    *(int *)(f + ACTOR_FACING) = (int)(g_co_turn * ANGLE_UNITS);
    *(int *)(viewer + 0x44) = 1;                                 // off the auto-turning page while the stick holds it
}

// ---- the preview: the game's Character viewer (cwo_ccprev.inl facts), the character's outfit files read from ours
static bool co_is_target(LPCWSTR f) {                            // ...\pcNNa<digit>.alr of the previewed character
    if (!g_co_target[0] || !f) return false;
    size_t n = wcslen(f), t = strlen(g_co_target);
    if (n < t + 5) return false;
    const wchar_t *e = f + n - (t + 5);
    if (e > f && e[-1] != L'\\' && e[-1] != L'/') return false;
    for (size_t i = 0; i < t; i++) if (towlower(e[i]) != (wchar_t)tolower(g_co_target[i])) return false;
    return iswdigit(e[t]) && !_wcsicmp(e + t + 1, L".alr");
}
static int co_prev_build() {
    const EdItem &it = g_edc[EDP_OUTFIT].item[g_edc[EDP_OUTFIT].cur];
    Bytes b;
    char why[128] = "";
    if (!co_build(it.v, b, why, sizeof(why))) { char m[160]; sprintf_s(m, "Preview: %s", why); set_status(m); return 0; }
    wchar_t tmp[MAX_PATH];
    swprintf_s(tmp, L"%s.new", g_co_prev_file);
    if (!pf_write(tmp, b.data(), (DWORD)b.size()) || !MoveFileExW(tmp, g_co_prev_file, MOVEFILE_REPLACE_EXISTING)) return 0;
    return 1;
}
static void co_prev_request() {                                  // A on "Preview live" (controller thread)
    if (ed_online()) { set_status("Online: no preview"); return; }
    swprintf_s(g_co_prev_file, L"%spreview_outfit.alr", g_dir);
    InterlockedExchange(&g_co_dirty, 0);
    InterlockedExchange(&g_co_prev, 1);
}
// v5.112 (owner 2026-11-07): leaving the preview ends the game's viewer, which returns to the
// main menu (menu 8, *(*0x140533c58 + 0x105f4)); there the loader queues the EDGAR'S DREAM item's event as the menu's own
// input would: native 2.73 = FUN_1402071d0 stores event -> menu + 0x10414, window -> + 0x10418; the MAIN item 160 is event
// 260 in window 1 (as MULTIPLAYER, item 110 = event 210, 5.3.2), its handler opens the EDGAR'S DREAM list.
static ULONGLONG g_co_back_t, g_co_main_since;
static void co_back_step() {
    if (!g_co_back_t) return;
    ULONGLONG now = GetTickCount64();
    if (now - g_co_back_t > 20000) { g_co_back_t = 0; logf("create an outfit: no main menu within 20 s - stays where it is"); return; }
    __try {
        ULONG_PTR menu = *(ULONG_PTR *)(g_base + 0x533c58);
        int id = menu ? *(short *)(menu + 0x105f4) : -1;
        if (id != 8 || g_in_match) { g_co_main_since = 0; return; }
        if (!g_co_main_since) { g_co_main_since = now; return; }
        if (now - g_co_main_since < 700) return;                 // the main menu built and its fade done
        *(int *)(menu + 0x10414) = 260;
        *(int *)(menu + 0x10418) = 1;
        g_co_back_t = 0; g_co_main_since = 0;
        logf("create an outfit: back at the main menu - EDGAR'S DREAM opened (event 260, window 1)");
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_co_back_t = 0; }
}
static void co_prev_step() {                                     // game thread (CwoOverlayFrame)
    LONG st = g_co_prev;
    if (!st) { co_back_step(); return; }
    __try {
        ULONGLONG now = GetTickCount64();
        const EdItem &it = g_edc[EDP_OUTFIT].item[g_edc[EDP_OUTFIT].cur];
        int word = ((it.v[1] - 1) << 16) | it.v[0];
        if (st == 1) {
            if (!co_prev_build()) { InterlockedExchange(&g_co_prev, 0); return; }
            sprintf_s(g_co_target, "pc%02da", it.v[0]);
            InterlockedExchange(&g_co_redirect, 1);
            g_co_old_char = *(int *)(g_base + RVA_CHAR_WORD);
            *(int *)(g_base + RVA_CHAR_WORD) = word;
            ((PFN_MODE)(g_base + RVA_MODE_SWITCH))(*(ULONG_PTR *)(g_base + RVA_MODE_OBJ), 0x103);
            logf("create an outfit: preview of %s outfit %d", ED_CHARAS[it.v[0]], it.v[1]);
            g_co_t = now; g_co_load_t = 0;
            InterlockedExchange(&g_co_prev, 2);
            set_status("PREVIEW: the model turns; change any row and it follows. B: leave");
            return;
        }
        ULONG_PTR v = cc_viewer();
        pv_watch(g_co_pw, "Create an Outfit", st, g_co_t);
        if (st == 2) {
            if (!v) {
                if (now - g_co_t > 10000) { logf("create an outfit: the viewer did not start"); InterlockedExchange(&g_co_prev, 3); }
                return;
            }
            if (*(int *)(v + 0x44) != 0) *(int *)(v + 0x44) = 0;    // the turning page
            InterlockedExchange(&g_co_stick, 1);
            co_light_neutral(true);
            co_stick_turn(v);                                    // v5.69 the right stick turns the model
            *(int *)(v + 0x54) = 0;                              // the viewer's own list not drawn
            bool ready = *(int *)(v + 0x48) != 3 && *(int *)(v + 0x48) != 4 && *(int *)(v + 0x48) != 5;
            if (!g_co_load_t && ready && now - g_co_t > 600) {
                *(int *)(v + 0x84) = word; *(int *)(v + 0x48) = 3; *(int *)(v + 0x4c) = 10;
                g_co_load_t = now;
            }
            if (g_co_dirty && now - g_co_change_t >= 300 && *(int *)(v + 0x48) == 2) {
                InterlockedExchange(&g_co_dirty, 0);
                char t2[24]; sprintf_s(t2, "pc%02da", it.v[0]);
                strcpy_s(g_co_target, t2);
                if (co_prev_build()) { *(int *)(v + 0x84) = word; *(int *)(v + 0x48) = 3; *(int *)(v + 0x4c) = 10; }
            }
            return;
        }
        if (st == 3) {
            InterlockedExchange(&g_co_stick, 0);
            if (g_co_old_char >= 0) *(int *)(g_base + RVA_CHAR_WORD) = g_co_old_char;
            // v5.117.3 (seen live 2026-11-08 10:53: B out of the preview landed on the title - the 150 ms press reached the main
            // menu too): long enough for the viewer only
            if (v) { *(int *)(v + 0x44) = 0; f_press(0x8, 60); }
            g_co_t = now;
            InterlockedExchange(&g_co_prev, 4);
            return;
        }
        if (st == 4 && (!v || now - g_co_t > 4000)) {
            co_light_neutral(false);
            InterlockedExchange(&g_co_redirect, 0);
            g_co_target[0] = 0;
            InterlockedExchange(&g_co_prev, 0);
            g_co_back_t = now;                                   // v5.112 then the EDGAR'S DREAM list on the main menu
            ed_close();                                          // v5.115 the loader's page closes too: only the game's menu
            InterlockedExchange(&g_ed_page, EDP_CHOOSER);
            set_status("Preview closed");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("create an outfit: preview fault - closed");
        InterlockedExchange(&g_co_redirect, 0);
        g_co_target[0] = 0;
        InterlockedExchange(&g_co_prev, 0);
    }
}

// offline test (tests/co_test.cpp): one outfit built from a folder of clean files into out_path
extern "C" __declspec(dllexport) int CwoOutfitTest(const wchar_t *install, const wchar_t *data_dir, int ch, int outfit, int skin,
                                                   int headx, int top, int bottom, const wchar_t *out_path) {
    wcscpy_s(g_install, install);
    wcscpy_s(g_dir, data_dir);
    int v[40] = {ch, outfit, skin, headx, top, bottom};          // v5.188.59: 40 like an EdItem (co_build reads v[17])
    Bytes b;
    char why[128] = "";
    if (!co_build(v, b, why, sizeof(why))) { logf("outfit test: %s", why); return -1; }
    return pf_write(out_path, b.data(), (DWORD)b.size()) ? 1 : -2;
}
// v5.188.59 the same with a Sleeves color (offline check of the arm labels)
extern "C" __declspec(dllexport) int CwoOutfitSleevesTest(const wchar_t *install, const wchar_t *data_dir, int ch, int outfit,
                                                          int top, int bottom, int sleeves, const wchar_t *out_path) {
    wcscpy_s(g_install, install);
    wcscpy_s(g_dir, data_dir);
    int v[40] = {ch, outfit, 0, -1, top, bottom};
    v[17] = sleeves % 100; v[18] = sleeves / 100 % 100; v[19] = sleeves / 10000;   // v5.188.61 test: sleeves + 100 x boots + 10000 x gloves
    Bytes b;
    char why[128] = "";
    if (!co_build(v, b, why, sizeof(why))) { logf("outfit test: %s", why); return -1; }
    return pf_write(out_path, b.data(), (DWORD)b.size()) ? 1 : -2;
}

static int co_self_test() {                                      // the tint and the target match
    int c[3];
    co_tint(CO_RGB[4], 0.5, c);                                  // mid grey -> the colour itself
    bool tint = c[0] == 200 && c[1] == 30 && c[2] == 30;
    co_tint(CO_RGB[2], 1.0, c);
    tint &= c[0] == 255 && c[1] == 255 && c[2] == 255;
    char keep[24]; strcpy_s(keep, g_co_target);
    strcpy_s(g_co_target, "pc02a");
    bool tgt = co_is_target(L"Assets/Data/Player\\pc02a\\pc02a1.alr") && co_is_target(L"x/PC02A3.ALR") && !co_is_target(L"x\\pc02a.alr") &&
               !co_is_target(L"x\\pc12a1.alr") && !co_is_target(L"x\\xpc02a1.alr");
    strcpy_s(g_co_target, keep);
    logf("self-test: create an outfit tint %s, preview files %s", tint ? "ok" : "FAILED", tgt ? "ok" : "FAILED");
    return tint && tgt ? 1 : -61;
}
