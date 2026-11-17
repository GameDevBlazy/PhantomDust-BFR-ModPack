// ---- v5.32 every skill's colour, any hue (owner 2026-10-27; "the color hue degrees is
// changing by like 30% instead of it letting us select every single color").
// Why it was so: the live preview only turned the particles' start / end colours, and turning a hue does nothing to white or
// grey; textures and vertex colours only changed through colour copies built at install, one every 30 degrees.
// Game facts (Ghidra full pass 065 via pd_fn.py + capstone on PDUWP 9ba7411b, 2026-10-27):
//  - the effect index object *0x1405bb2e8 (MSEffectEI, 0x3090 bytes, made by FUN_14018b2f0): Effect\e.i at +0x88: u16 word0,
//    u16 count at +0x8a, then 12-byte entries {u16 request, u16 pack, u32 offset, u32 span} from +0x8c, sorted by request
//    (binary search FUN_14018c4e0); the object has room for (0x3090 - 0x8c) / 12 = 1023 entries.
//  - FUN_14018d0b0 reads a request's span from Effect\a<pack>.e into its effect-loader entry (*0x1405bb228: entries of 0x88
//    at +0xC0, count +0xC8; entry +8 request, +4 loaded, +0x18 the span's bytes, +0x20 = +0x18 + span - 0x800 its tail);
//    when the read is done FUN_14018c5e0 calls FUN_1402934e0(data, 1) at exe 14018c6ce, which builds the effect's textures
//    and vertex buffers from those bytes (FUN_1402934e0(data, 0) frees them again).
//  - the colours in a span (ALR layout Batch 012 / 020, export_all_effects.py, skill_colour.py; recoloured copies in game
//    since IGT-060): DXT1/3/5 colour blocks and ARGB8888 textures (texture table chunk 0x15 at the header's end: entry +4
//    offset from the resource base, +12 packed size / format / mips), vertex colours (D3DCOLOR at the vertex format's colour
//    offset; chunk 0x16 lists the buffers), particle start / end colours (chunk 0x13 +0x38 / +0x3C, a second pair at +0x40 /
//    +0x44 when they read as colours; FUN_1401a9ae0 reads them every frame).
// So a coloured new skill gets an effect number of its own (8000 and up: an e.i entry with the same pack, offset and span as
// the effect it shows) and, when that number is loaded, the loader recolours the bytes just before FUN_1402934e0 builds them:
// textures, vertex colours and particles, at any hue. A stock number is never recoloured.
// Modes: Auto (coloured parts turn by the hue, white / grey parts take it), Turn (every colour turns; greys stay), Paint
// (every colour takes the hue). The texture transparency rule holds: DXT1 blocks keep their 4-colour / 3-colour mode and
// index 3 stays transparent in 3-colour blocks; alpha is never changed.
#include "cwo_fxload_hook.h"
static ULONG_PTR sixf2_near_alloc(SIZE_T n, DWORD prot);         // cwo_sixf2.inl
static const ULONG_PTR RVA_FX_EI = 0x5bb2e8, RVA_FX_LOADER = 0x5bb228, FN_FX_BUILD = 0x2934e0;
static const int FX_EI_MAX = 1023, FX_REQ0 = 8000, FX_MAXMAP = 400;
struct FxColour { int hue, sat, val, mode; };                 // hue 0-359, sat / val in %, mode 0 Auto 1 Turn 2 Paint
struct FxMapEnt { int req, src, owner; FxColour c; };
static FxMapEnt g_fxm[FX_MAXMAP];
static int g_fxm_n, g_fx_state;                               // g_fx_state: 1 hook on, -1 refused
static volatile LONG g_fx_done, g_fx_faults;
static unsigned char *g_fx_test_ei;                           // self-test stand-in index (0x3090 bytes)

static void fx_hsv(float r, float g, float b, float &h, float &s, float &v) {
    float mx = max(r, max(g, b)), mn = min(r, min(g, b)), d = mx - mn;
    h = 0;
    if (d > 0) {
        if (mx == r) h = fmodf((g - b) / d, 6.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h *= 60.0f;
        if (h < 0) h += 360.0f;
    }
    s = mx > 0 ? d / mx : 0;
    v = mx;
}
static void fx_rgb(float h, float s, float v, float &r, float &g, float &b) {
    float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1)), m = v - c;
    r = g = b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    r += m; g += m; b += m;
}
// one colour through the chosen hue / saturation / brightness / mode (8-bit channels)
static void fx_colour(int &R, int &G, int &B, const FxColour &c) {
    float h, s, v;
    fx_hsv(R / 255.0f, G / 255.0f, B / 255.0f, h, s, v);
    float sp = c.sat / 100.0f, vp = c.val / 100.0f;
    bool grey = s < 0.2f;
    if (c.mode == 2 || (c.mode == 0 && grey && c.hue)) {      // Paint (or Auto on a white / grey part): the hue itself
        h = (float)(c.hue % 360);
        s = (grey ? 0.8f : (s > 0.5f ? s : 0.5f)) * sp;
    } else {                                                  // Turn (or Auto on a coloured part)
        h = fmodf(h + (float)c.hue, 360.0f);
        s *= sp;
    }
    if (s > 1) s = 1;
    v *= vp; if (v > 1) v = 1;
    float r, g, b;
    fx_rgb(h, s, v, r, g, b);
    R = (int)(r * 255 + 0.5f); G = (int)(g * 255 + 0.5f); B = (int)(b * 255 + 0.5f);
}
static unsigned short fx_565(unsigned short w, const FxColour &c) {
    int r = (w >> 11 & 31) * 255 / 31, g = (w >> 5 & 63) * 255 / 63, b = (w & 31) * 255 / 31;
    fx_colour(r, g, b, c);
    return (unsigned short)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
}
static unsigned fx_swap_idx(unsigned bits, const int t[4]) {
    unsigned out = 0;
    for (int i = 0; i < 16; i++) out |= (unsigned)t[bits >> 2 * i & 3] << 2 * i;
    return out;
}
static void fx_dxt(unsigned char *d, size_t n, int fmt, const FxColour &c) {   // skill_colour.recolour_dxt, in place
    size_t bs = fmt == 0x0C ? 8 : 16, co = fmt == 0x0C ? 0 : 8;
    static const int SW4[4] = {1, 0, 3, 2}, SW3[4] = {1, 0, 2, 3};
    for (size_t o = 0; o + bs <= n; o += bs) {
        unsigned short c0 = *(unsigned short *)(d + o + co), c1 = *(unsigned short *)(d + o + co + 2);
        unsigned bits = *(unsigned *)(d + o + co + 4);
        unsigned short n0 = fx_565(c0, c), n1 = fx_565(c1, c);
        if (fmt == 0x0C) {
            if (c0 > c1) {                                    // a 4-colour block stays one (n0 > n1)
                if (n0 == n1) { if (n0 < 0xFFFF) n0++; else n1--; }
                if (n0 < n1) { unsigned short t = n0; n0 = n1; n1 = t; bits = fx_swap_idx(bits, SW4); }
            } else if (n0 > n1) {                             // a 3-colour block stays one: index 3 keeps transparent
                unsigned short t = n0; n0 = n1; n1 = t; bits = fx_swap_idx(bits, SW3);
            }
        }
        *(unsigned short *)(d + o + co) = n0;
        *(unsigned short *)(d + o + co + 2) = n1;
        *(unsigned *)(d + o + co + 4) = bits;
    }
}
static size_t fx_tex_bytes(unsigned packed, int *fmt_out) {  // alr_transplant.texture_bytes (without the 0x80 rounding)
    int w = 1 << ((packed >> 24) & 15), h = 1 << ((packed >> 20) & 15), mips = (packed >> 16) & 15, fmt = (packed >> 8) & 0xFF;
    *fmt_out = fmt;
    size_t total = 0;
    for (int k = 0; k < (mips > 0 ? mips : 1); k++) {
        int mw = max(w >> k, 1), mh = max(h >> k, 1);
        if (fmt == 0x0C) total += (size_t)max(mw / 4, 1) * max(mh / 4, 1) * 8;
        else if (fmt == 0x0E || fmt == 0x0F) total += (size_t)max(mw / 4, 1) * max(mh / 4, 1) * 16;
        else if (fmt == 0x86) total += (size_t)mw * mh * 4;
        else return 0;                                        // other formats are left as they are
    }
    return (packed & 4) ? total * 6 : total;
}
static int fx_colour_at(int fmt) {                            // the vertex format's colour offset (ALR_STAGE_VERTEX_FORMATS)
    switch (fmt) { case 1: case 3: case 5: case 0x1D: case 0x21: return 20; case 8: case 9: case 0x15: case 0x16: case 0x17: return 16; }
    return -1;
}

// the whole span (ALR part: [data, data + size)) in place; returns how many colours were changed
static int fx_recolour(unsigned char *d, size_t size, const FxColour &c, int *ntex = NULL, int *nvert = NULL, int *npart = NULL) {
    if (size < 0x40 || *(unsigned *)d != 0x11) return 0;
    unsigned hs = *(unsigned *)(d + 4), ro = *(unsigned *)(d + 0xC), n = *(unsigned *)(d + 0x10);
    if (hs < 0x20 || hs + 12 > size || n > 4096 || 0x20 + 4ull * n > size || ro >= size) return 0;
    unsigned char *res = d + ro;
    size_t res_n = size - ro;
    int t = 0, v = 0, p = 0;
    if (*(unsigned *)(d + hs) == 0x15) {                     // textures
        int cnt = *(short *)(d + hs + 8);
        for (int k = 0; k < cnt && hs + 12 + 28ull * (k + 1) <= size; k++) {
            unsigned char *e = d + hs + 12 + 28 * k;
            unsigned off = *(unsigned *)(e + 4), packed = *(unsigned *)(e + 12);
            int fmt;
            size_t bytes = fx_tex_bytes(packed, &fmt);
            if (!bytes || !ro || off + bytes > res_n) continue;
            if (fmt == 0x86) {
                for (size_t o = 0; o + 4 <= bytes; o += 4) {
                    unsigned char *px = res + off + o;
                    int R = px[2], G = px[1], B = px[0];
                    fx_colour(R, G, B, c);
                    px[2] = (unsigned char)R; px[1] = (unsigned char)G; px[0] = (unsigned char)B;
                }
            } else fx_dxt(res + off, bytes, fmt, c);
            t++;
        }
    }
    const int *offs = (const int *)(d + 0x20);
    for (unsigned gi = 0; gi < n; gi++) {                   // groups: vertex colours and particle colours
        if (offs[gi] <= 0 || (size_t)offs[gi] + 8 > size) continue;
        size_t i = (size_t)offs[gi];
        for (int guard = 0; guard < 4096 && i + 8 <= size; guard++) {
            unsigned cid = *(unsigned *)(d + i), sp = *(unsigned *)(d + i + 4);
            if (cid == 0 || sp < 8 || i + sp > size) break;
            if (cid == 0x16 && ro && sp >= 12) {
                unsigned nb = *(unsigned *)(d + i + 8);
                for (unsigned k = 0; k < nb && 12 + 28ull * (k + 1) <= sp; k++) {
                    unsigned char *dd = d + i + 12 + 28 * k;
                    int fmt = dd[0], stride = dd[1], at = fx_colour_at(fmt);
                    unsigned cnt = *(unsigned *)(dd + 8), voff = *(unsigned *)(dd + 0x14);
                    if (at < 0 || !stride || voff + (size_t)stride * cnt > res_n) continue;
                    for (unsigned q = 0; q < cnt; q++) {
                        unsigned char *px = res + voff + (size_t)q * stride + at;
                        int R = px[2], G = px[1], B = px[0];
                        fx_colour(R, G, B, c);
                        px[2] = (unsigned char)R; px[1] = (unsigned char)G; px[0] = (unsigned char)B;
                        v++;
                    }
                }
            } else if (cid == 0x13 && sp >= 0x48) {
                for (int off = 0x38; off <= 0x44; off += 4) {
                    unsigned *w = (unsigned *)(d + i + off);
                    if (off >= 0x40 && ((*w >> 23) & 0xFF) != 0xFF) continue;
                    if ((*w & 0xFFFFFF) == 0) continue;
                    int R = (*w >> 16) & 255, G = (*w >> 8) & 255, B = *w & 255;
                    fx_colour(R, G, B, c);
                    *w = (*w & 0xFF000000u) | (unsigned)R << 16 | (unsigned)G << 8 | (unsigned)B;
                    p++;
                }
            }
            i += sp;
        }
    }
    if (ntex) *ntex = t;
    if (nvert) *nvert = v;
    if (npart) *npart = p;
    return t + v + p;
}

// ---- the effect index (e.i) in the game ----
static unsigned char *fx_ei() {
    if (g_fx_test_ei) return g_fx_test_ei;
    __try { return *(unsigned char **)(g_base + RVA_FX_EI); } __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}
static bool fx_ei_ready() {
    unsigned char *ei = fx_ei();
    __try { return ei && *(unsigned short *)(ei + 0x8a) > 100 && *(unsigned short *)(ei + 0x8c) > 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static unsigned char *fx_ei_find(int req) {                  // FUN_14018c4e0's search
    unsigned char *ei = fx_ei();
    if (!ei) return NULL;
    int n = *(unsigned short *)(ei + 0x8a);
    for (int i = 0; i < n; i++) if (*(unsigned short *)(ei + 0x8c + 12 * i) == (unsigned)req) return ei + 0x8c + 12 * i;
    return NULL;
}
static bool fx_loaded(int req) {                             // is this number in the effect loader now?
    if (g_fx_test_ei) return false;
    __try {
        ULONG_PTR ld = *(ULONG_PTR *)(g_base + RVA_FX_LOADER);
        char *ent = ld ? *(char **)(ld + 0xC0) : NULL;
        unsigned cnt = ld ? *(unsigned *)(ld + 0xC8) : 0;
        for (unsigned e = 0; ent && e < cnt && e < 2048; e++) if (*(int *)(ent + 0x88 * e + 8) == req) return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
static int fx_ei_append(int src) {                           // a new number (above every other) showing src's span
    unsigned char *ei = fx_ei(), *se = fx_ei_find(src);
    if (!ei || !se) return -1;
    int n = *(unsigned short *)(ei + 0x8a);
    if (n >= FX_EI_MAX) return -1;
    int last = *(unsigned short *)(ei + 0x8c + 12 * (n - 1)), req = last < FX_REQ0 ? FX_REQ0 : last + 1;
    if (req > 65000) return -1;
    unsigned char *e = ei + 0x8c + 12 * n;
    memcpy(e, se, 12);
    *(unsigned short *)e = (unsigned short)req;
    *(unsigned short *)(ei + 0x8a) = (unsigned short)(n + 1);   // the count last: the search never sees half an entry
    return req;
}

// every second (game thread): the loader's numbers are still in the index (were it ever read again, they are put back in
// their order - they are all above the game's own - before anything can ask for them)
static void fx_ei_keep() {
    if (!g_fxm_n || !fx_ei_ready()) return;
    __try {
        for (int i = 0; i < g_fxm_n; i++) {
            if (fx_ei_find(g_fxm[i].req)) continue;
            unsigned char *ei = fx_ei(), *se = fx_ei_find(g_fxm[i].src);
            int n = *(unsigned short *)(ei + 0x8a);
            if (!se || n >= FX_EI_MAX || *(unsigned short *)(ei + 0x8c + 12 * (n - 1)) >= (unsigned)g_fxm[i].req) continue;
            unsigned char *e = ei + 0x8c + 12 * n;
            memcpy(e, se, 12);
            *(unsigned short *)e = (unsigned short)g_fxm[i].req;
            *(unsigned short *)(ei + 0x8a) = (unsigned short)(n + 1);
            logf("skill color: effect %d put back in the effect index", g_fxm[i].req);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// the number a coloured skill's effect uses: the one it has while that one is not loaded yet (its colour can still change),
// else a new one; owner = the new skill's id
static int fx_colour_req(int owner, int src, const FxColour &c) {
    if (src <= 0 || src >= FX_REQ0 || !fx_ei_ready()) return -1;
    int old = -1;
    for (int i = 0; i < g_fxm_n; i++) {
        FxMapEnt &m = g_fxm[i];
        if (m.owner != owner || m.src != src) continue;
        if (!memcmp(&m.c, &c, sizeof(c))) return m.req;     // the same colour
        if (!fx_loaded(m.req)) { m.c = c; return m.req; }    // not loaded yet: its colour simply changes
        old = i;                                             // loaded with the old colour: a fresh number below
    }
    if (old >= 0) g_fxm[old].owner = -1;                     // it can serve again once the game has let it go
    for (int i = 0; i < g_fxm_n; i++) {                       // a number nobody uses now and nothing has loaded
        FxMapEnt &m = g_fxm[i];
        if (m.owner == -1 && m.src == src && !fx_loaded(m.req)) { m.owner = owner; m.c = c; return m.req; }
    }
    if (g_fxm_n >= FX_MAXMAP) return -1;
    int req = fx_ei_append(src);
    if (req < 0) return -1;
    g_fxm[g_fxm_n++] = {req, src, owner, c};
    logf("skill color: effect %d = effect %d's look, recolored (hue %d, sat %d, bright %d, mode %d) for skill %d", req, src,
         c.hue, c.sat, c.val, c.mode, owner);
    return req;
}
static const FxMapEnt *fx_map_of(int req) {
    for (int i = 0; i < g_fxm_n; i++) if (g_fxm[i].req == req) return &g_fxm[i];
    return NULL;
}

// ---- the hook: FUN_1402934e0(data, 1) at exe 14018c6ce ----
typedef void(__fastcall *PFN_FX_BUILD)(void *, int);
static void __fastcall fx_build_hook(void *data, int flag) {
    // v5.179.5 (owner 2026-11-13, match-end crash after freed-texture faults): an effect loaded while the fight is on can push
    // out a resident one whose particles still fly (FUN_14018cf80) - log which request it was, so it can be loaded before the
    // fight instead (match state *0x1404bc950 == 6 = the fight)
    if (flag == 1 && data) {
        static int mid_logged;
        __try {
            if (*(int *)(g_base + 0x4bc950) == 6 && mid_logged < 60) {
                ULONG_PTR ld = *(ULONG_PTR *)(g_base + RVA_FX_LOADER);
                char *ent = ld ? *(char **)(ld + 0xC0) : NULL;
                unsigned cnt = ld ? *(unsigned *)(ld + 0xC8) : 0;
                for (unsigned e = 0; ent && e < cnt && e < 2048; e++)
                    if (*(void **)(ent + 0x88 * e + 0x18) == data) {
                        mid_logged++;
                        logf("effect load during the fight: request %d (loader slot %u of %u) - it can push out an effect in use", *(int *)(ent + 0x88 * e + 8), e, cnt);
                        break;
                    }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (flag == 1 && data && g_fxm_n > 0) {
        __try {
            ULONG_PTR ld = *(ULONG_PTR *)(g_base + RVA_FX_LOADER);
            char *ent = ld ? *(char **)(ld + 0xC0) : NULL;
            unsigned cnt = ld ? *(unsigned *)(ld + 0xC8) : 0;
            for (unsigned e = 0; ent && e < cnt && e < 2048; e++) {
                char *en = ent + 0x88 * e;
                if (*(void **)(en + 0x18) != data) continue;
                int req = *(int *)(en + 8);
                const FxMapEnt *m = req >= FX_REQ0 ? fx_map_of(req) : NULL;
                unsigned char *tail = *(unsigned char **)(en + 0x20);
                // v5.33 (owner; log: "0 textures"): the size is the
                // e.i span of this number (FUN_14018c5e0 reads it the same way: FUN_14018c4e0(e.i) + 8); the entry's +0x20 is
                // only set for some loader slots, and a short one left out the textures, which lie behind the vertices
                unsigned char *ee = m ? fx_ei_find(req) : NULL;
                size_t span = ee ? *(unsigned *)(ee + 8) : 0;
                if (!span && tail > (unsigned char *)data) span = (size_t)(tail - (unsigned char *)data);
                if (m && span > 0x40 && span < (64u << 20)) {
                    int t = 0, v = 0, p = 0;
                    fx_recolour((unsigned char *)data, span, m->c, &t, &v, &p);
                    if (InterlockedIncrement(&g_fx_done) <= 200)
                        logf("skill color: effect %d loaded - %d textures, %d vertex colors, %d particle colors recolored", req, t, v, p);
                }
                break;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { InterlockedIncrement(&g_fx_faults); }
    }
    ((PFN_FX_BUILD)(g_base + FN_FX_BUILD))(data, flag);
}

static void fx_install() {                                    // DllMain: before any game code runs
    if (memcmp((const void *)(g_base + FXL_SITE_RVA), FXL_SITE, sizeof(FXL_SITE))) {
        g_fx_state = -1;
        logf("skill color: the effect load is not the expected code - colors stay as the game draws them");
        return;
    }
    ULONG_PTR stub = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    long long d = (long long)stub - (long long)(g_base + FXL_SITE_RVA + 5);
    if (!stub || d < INT_MIN || d > INT_MAX) { g_fx_state = -1; logf("skill color: no memory within reach for the call"); return; }
    memcpy((void *)stub, FXL_STUB, sizeof(FXL_STUB));
    ULONG_PTR fn = (ULONG_PTR)&fx_build_hook;
    memcpy((void *)(stub + FXL_FN_AT), &fn, 8);
    unsigned char c[5] = {0xE8};
    int rel = (int)d;
    memcpy(c + 1, &rel, 4);
    DWORD old;
    if (!VirtualProtect((void *)(g_base + FXL_SITE_RVA), 5, PAGE_EXECUTE_READWRITE, &old)) { g_fx_state = -1; return; }
    memcpy((void *)(g_base + FXL_SITE_RVA), c, 5);
    VirtualProtect((void *)(g_base + FXL_SITE_RVA), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    g_fx_state = 1;
    logf("skill color: the effect load calls the loader first (stub %p): new skills' own effect numbers are recolored as they load", (void *)stub);
}

static int fx_self_test() {
    // colours: Turn keeps greys, Auto and Paint colour them; a pure red turned 120 degrees is green
    FxColour turn = {120, 100, 100, 1}, aut = {120, 100, 100, 0}, paint = {240, 100, 100, 2}, keep = {0, 100, 100, 0};
    int r = 255, g = 0, b = 0;
    fx_colour(r, g, b, turn);
    bool col = r < 5 && g > 250 && b < 5;
    r = g = b = 200; fx_colour(r, g, b, turn); col &= r == 200 && g == 200 && b == 200;
    r = g = b = 200; fx_colour(r, g, b, aut); col &= g > r + 60 && g > b + 60;
    r = g = b = 200; fx_colour(r, g, b, keep); col &= r == 200 && g == 200 && b == 200;
    r = 255; g = 128; b = 0; fx_colour(r, g, b, paint); col &= b > r && b > g;
    // DXT1: a 4-colour block stays 4-colour, a 3-colour block keeps its transparent index
    unsigned char blk[16];
    *(unsigned short *)blk = 0xF800; *(unsigned short *)(blk + 2) = 0x07E0; *(unsigned *)(blk + 4) = 0xE4E4E4E4u;   // red > green
    *(unsigned short *)(blk + 8) = 0x001F; *(unsigned short *)(blk + 10) = 0xF800; *(unsigned *)(blk + 12) = 0xFFFFFFFFu;   // 3-colour, all transparent
    fx_dxt(blk, 16, 0x0C, turn);
    bool dxt = *(unsigned short *)blk > *(unsigned short *)(blk + 2) && *(unsigned short *)(blk + 8) <= *(unsigned short *)(blk + 10) &&
               *(unsigned *)(blk + 12) == 0xFFFFFFFFu;
    // a small span: one DXT1 texture, one vertex buffer (format 9), one particle node
    static unsigned char sp[0x400];
    memset(sp, 0, sizeof(sp));
    *(unsigned *)sp = 0x11; *(unsigned *)(sp + 4) = 0x24; *(unsigned *)(sp + 0xC) = 0x200; *(unsigned *)(sp + 0x10) = 1;
    *(int *)(sp + 0x20) = 0x24 + 12 + 28;                     // the group after the texture table
    unsigned char *tt = sp + 0x24;
    *(unsigned *)tt = 0x15; *(unsigned *)(tt + 4) = 12 + 28; *(short *)(tt + 8) = 1;
    *(unsigned *)(tt + 12 + 4) = 0; *(unsigned *)(tt + 12 + 12) = (2u << 24) | (2u << 20) | (1u << 16) | (0x0Cu << 8);   // 4 x 4, 1 mip
    unsigned char *gr = sp + 0x24 + 12 + 28;
    *(unsigned *)gr = 0x16; *(unsigned *)(gr + 4) = 12 + 28; *(unsigned *)(gr + 8) = 1;
    gr[12] = 9; gr[13] = 20; *(unsigned *)(gr + 12 + 8) = 1; *(unsigned *)(gr + 12 + 0x14) = 0x10;
    unsigned char *nd = gr + 12 + 28;
    *(unsigned *)nd = 0x13; *(unsigned *)(nd + 4) = 0x48; *(unsigned *)(nd + 0x38) = 0xFFC8C8C8u;   // grey particle
    unsigned char *res = sp + 0x200;
    *(unsigned short *)res = 0xF800; *(unsigned short *)(res + 2) = 0x0000; *(unsigned *)(res + 4) = 0;
    res[0x10 + 16] = 200; res[0x10 + 17] = 200; res[0x10 + 18] = 200; res[0x10 + 19] = 255;   // grey vertex (B, G, R, A)
    int nt = 0, nv = 0, np = 0;
    fx_recolour(sp, sizeof(sp), aut, &nt, &nv, &np);
    unsigned pw = *(unsigned *)(nd + 0x38);
    bool span = nt == 1 && nv == 1 && np == 1 && *(unsigned short *)res != 0xF800 && ((pw >> 8) & 255) > (pw & 255) + 40 &&
                (pw >> 24) == 0xFF && res[0x10 + 19] == 255 && res[0x10 + 17] > res[0x10 + 16] + 40;
    // the index: a new number is appended after the last, copying the source's place; the same colour keeps it
    static unsigned char ei[0x3090];
    memset(ei, 0, sizeof(ei));
    for (int i = 0; i < 120; i++) { unsigned char *e = ei + 0x8c + 12 * i; *(unsigned short *)e = (unsigned short)(i + 1); *(unsigned short *)(e + 2) = 7; *(unsigned *)(e + 4) = 0x1000 * i; *(unsigned *)(e + 8) = 0x800; }
    *(unsigned short *)(ei + 0x8a) = 120;
    int keep_n = g_fxm_n;
    g_fx_test_ei = ei;
    int a = fx_colour_req(-5, 61, aut), b2 = fx_colour_req(-5, 61, aut), c2 = fx_colour_req(-5, 61, turn);
    unsigned char *ne = fx_ei_find(a);
    bool idx = a == FX_REQ0 && b2 == a && c2 == a && ne && *(unsigned *)(ne + 4) == 0x1000 * 60 && *(unsigned short *)(ei + 0x8a) == 121 &&
               fx_map_of(a) && fx_map_of(a)->c.mode == 1;
    g_fx_test_ei = NULL;
    g_fxm_n = keep_n;
    logf("self-test: skill color - colors %s, DXT1 modes %s, span %s, effect numbers %s, load hook %s", col ? "ok" : "FAILED",
         dxt ? "ok" : "FAILED", span ? "ok" : "FAILED", idx ? "ok" : "FAILED", g_fx_state == 1 ? "on" : "off");
    return col && dxt && span && idx ? 1 : -76;
}
