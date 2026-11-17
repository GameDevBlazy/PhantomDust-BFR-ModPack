// ---- v3.9 Create a School, round 2 (owner 2026-10-25; "Based on doesn't have
// to be mandatory"; "we should also be able to change the size ... move it around on the character's body ... select the part
// of the body ... set the speed ... toggle [the parts] on and off ... add it as the actual aura").
// Game facts (Ghidra full-pass batch_065 via pcode.py; AURA_COLOUR_BATCH036_037.md; AURA_FX_2026-10-24.md):
//   aura colour: every frame the aura script auradefault.ssb (loaded into the aura manager *0x14052ad00 + 0x558) copies
//     table B[slot +0xCC] (script +0x3C) to slot +0xC8, table A (+0x24) is the burst colour; index 0 = "no school" (an
//     aura-particle-only arsenal). The loader's old write to +0xC8 came after the frame's aura draw, so it never showed.
//     Now: the player's slot +0xCC = 0 and entry 0 of both tables = the school's colour (black when it replaces the aura:
//     the aura is drawn added on top).
//   effects: per-owner joint tables at fx manager + 0xF070 + owner * 0x800 ({u32 hash, u32 pad, ptr matrix} x n, count at
//     + 0x11070 + owner * 4, FUN_14018a7d0 / lookup FUN_14018aa50); entry k's matrix = entry 0's pointer + k * 0x40, bone b
//     = entry 0 + (b + 1) * 0x40. A node with a joint hash takes that joint's matrix (FUN_140189420), others the instance's.
//     A spare owner (no fighter there) whose table points every hash at the loader's matrix puts the whole effect where the
//     school says. Instance time = slot +0xC (float), the frame step = manager +0xF068 (FUN_1401899b0).
//   effect parts: EFT record = *(entry + 0x20) of the request table *0x1405bb228 + 0xC0 (0x88 per entry, request at +8, in
//     use at +4, count +0xC8); part blocks at EFT + 0xF4 + part * 0x1C {flags (bit 0 startable), i16 groups ending -1}
//     (FUN_14018a040, FUN_14018aee0). Custom pieces go into an unused part block of the same effect.
struct SfxSpec {
    bool on, always, replace_aura;
    int skill, part, based_on, hue, sat, bri;
    int bodypart, up, side, fwd, tilt, turn, size, width, height, speed;
    int pieces_custom, piece[4];
    int fx_hue, fx_str;                                         // v5.9 the effect's own colour (-1: its own colours)
};
static const int SFX_PARTS[19][8] = {   // ground, hips, chest, head, right hand, left hand, right foot, left foot (build_character maps)
    {20, 22, 24, 71, 31, 52, 87, 101}, {51, 53, 55, 102, 62, 83, 116, 128}, {25, 27, 29, 76, 36, 57, 87, 99}, {18, 20, 22, 69, 29, 50, 86, 100},
    {19, 21, 23, 70, 30, 51, 87, 99}, {20, 22, 24, 71, 31, 52, 88, 100}, {19, 21, 23, 70, 30, 51, 87, 99}, {11, 13, 15, 62, 22, 43, 74, 86},
    {10, 12, 14, 61, 21, 42, 77, 89}, {22, 24, 26, 73, 33, 54, 85, 97}, {0, 2, 4, 51, 11, 32, 63, 75}, {15, 17, 19, 66, 26, 47, 82, 95},
    {19, 21, 23, 70, 30, 51, 86, 98}, {21, 23, 26, 73, 33, 54, 83, 95}, {19, 21, 23, 70, 30, 51, 82, 94}, {15, 17, 19, 65, 26, 47, 78, 90},
    {30, 32, 34, 81, 41, 62, 96, 110}, {25, 27, 29, 76, 36, 57, 88, 100}, {31, 33, 35, 82, 42, 63, 93, 105}};
static const ULONG_PTR RVA_EFT_TABLE = 0x5bb228;
static int g_sfx_body = 0;                                    // set by the creator code: the body of charaset row 28

// ---- colour: entry 0 of the aura script's tables
static DWORD *g_sfx_tab;                                       // table A entry 0 (B entry 0 = +6 words)
static DWORD g_sfx_tab_orig[2], g_sfx_tab_last[2];
static bool g_sfx_tab_saved;
static ULONG_PTR g_sfx_tab_mgr;
// v5.9 (owner 21:32: "the hue doesn't change the aura"; the log never found the tables): the script is not at +0x558 (that
// is the aura's 0x2000-byte work heap, FUN_14019aa30 -> FUN_140292280) but in the file buffer *(aura + 0x1518)
// (= *0x1403e8808 = *0x14052dee8 + 0x3F0000, FUN_14019b520 once the file is in: descriptor 0x1403e8bb0 bit 2);
// auradefault.ssb holds u32 0 at +0x20, table A at +0x24 and table B at +0x3C (1,576 bytes)
static bool sfx_tables_at(DWORD *w) {
    bool stock = w[1] == 0xFF404040 && w[7] == 0xFF787878;
    bool ours = g_sfx_tab_last[0] && w[1] == g_sfx_tab_last[0] && w[7] == g_sfx_tab_last[1];
    if (w[0] != 0 || !(stock || ours) || (w[2] >> 24) != 0xFF || (w[8] >> 24) != 0xFF) return false;
    g_sfx_tab = w + 1;
    if (stock) { g_sfx_tab_orig[0] = w[1]; g_sfx_tab_orig[1] = w[7]; } else { g_sfx_tab_orig[0] = 0xFF404040; g_sfx_tab_orig[1] = 0xFF787878; }
    logf("school: aura color tables found at %p (%s)", (void *)w, stock ? "stock" : "the school's from before");
    return true;
}
static ULONGLONG g_sfx_tab_tried;
static DWORD *sfx_find_tables(ULONG_PTR aura) {
    if (g_sfx_tab && g_sfx_tab_mgr == aura) return g_sfx_tab;
    ULONGLONG now = GetTickCount64();
    if (g_sfx_tab_mgr == aura && now - g_sfx_tab_tried < 1000) return NULL;   // the file may still be loading: once a second
    g_sfx_tab_tried = now;
    g_sfx_tab = NULL; g_sfx_tab_saved = false; g_sfx_tab_mgr = aura;
    ULONG_PTR script = *(ULONG_PTR *)(aura + 0x1518);
    if (script && sfx_tables_at((DWORD *)(script + 0x20))) return g_sfx_tab;
    if (script)
        for (ULONG_PTR p = script; p + 4 * 14 <= script + 0x628; p += 4) if (sfx_tables_at((DWORD *)p)) return g_sfx_tab;
    static bool told;
    if (!told) { told = true; logf("school: aura color tables not found yet (script %p)", (void *)script); }
    return NULL;
}
static void sfx_colour_off() {
    __try {
        if (g_sfx_tab && g_sfx_tab_saved) { g_sfx_tab[0] = g_sfx_tab_orig[0]; g_sfx_tab[6] = g_sfx_tab_orig[1]; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_sfx_tab_saved = false; g_sfx_tab_last[0] = g_sfx_tab_last[1] = 0;
}
static void sfx_colour(const SfxSpec &s, ULONG_PTR aura, int pidx) {
    ULONG_PTR slot = aura + 0x120 * (ULONG_PTR)pidx;
    bool custom = s.hue >= 0 || s.replace_aura;
    if (!custom) {                                             // the game's colour of the school it is based on
        if (g_sfx_tab_saved) sfx_colour_off();
        *(int *)(slot + 0xCC) = s.based_on >= 0 && s.based_on <= 5 ? s.based_on : 0;
        return;
    }
    DWORD *t = sfx_find_tables(aura);
    if (!t) return;
    if (!g_sfx_tab_saved) { g_sfx_tab_saved = true; }
    DWORD main = s.replace_aura ? 0xFF000000 : ed_aura_rgb(s.hue, s.sat, s.bri, 176.0f);
    DWORD burst = s.replace_aura ? 0xFF000000 : ed_aura_rgb(s.hue, s.sat, s.bri, 120.0f);
    t[0] = burst; t[6] = main;                                 // table A[0], table B[0]
    g_sfx_tab_last[0] = burst; g_sfx_tab_last[1] = main;
    *(int *)(slot + 0xCC) = 0;
}

// ---- the effect: spare owner table, placement matrix, speed, custom pieces
static unsigned g_sfx_handle = FX_NONE;
static int g_sfx_req, g_sfx_fail, g_sfx_owner = -1, g_sfx_track = -1, g_sfx_key = 0;
static __declspec(align(16)) float g_sfx_mat[16];
static ULONG_PTR g_sfx_eft;                                     // EFT whose part block holds custom pieces
static int g_sfx_eft_part = -1;
static unsigned char g_sfx_eft_orig[0x1C], g_sfx_eft_mine[0x1C];

static void m_mul(const float *a, const float *b, float *o) {   // row-major 4 x 4, o = a * b (row vectors)
    float r[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    memcpy(o, r, sizeof(r));
}
static void m_rot(float *m, int axis, float deg) {
    float c = cosf(deg * 0.0174532925f), s = sinf(deg * 0.0174532925f);
    memset(m, 0, 64); m[15] = 1;
    if (axis == 0) { m[0] = 1; m[5] = c; m[6] = s; m[9] = -s; m[10] = c; }
    else { m[5] = 1; m[0] = c; m[2] = -s; m[8] = s; m[10] = c; }
}
// local (size, width, height, tilt, turn) * the character's facing; position = the body part + the offsets in the facing frame
static void sfx_matrix(const SfxSpec &s, const FxGame &g, const float *part_pos, unsigned facing, float *out) {
    float sc = s.size / 100.0f, S[16] = {0}, Rx[16], Ry[16], F[16], t[16];
    S[0] = sc * s.width / 100.0f; S[5] = sc * s.height / 100.0f; S[10] = sc; S[15] = 1;
    m_rot(Rx, 0, (float)s.tilt); m_rot(Ry, 1, (float)s.turn);
    memcpy(F, g.identity, 64);
    g.turn(F, facing);
    m_mul(S, Rx, t); m_mul(t, Ry, t); m_mul(t, F, out);
    float off[3] = {s.side * 0.5f, s.up * 0.5f, s.fwd * 0.5f};
    for (int j = 0; j < 3; j++) out[12 + j] = part_pos[j] + off[0] * F[j] + off[1] * F[4 + j] + off[2] * F[8 + j];
    out[15] = 1;
}

static int sfx_body_of(ULONG_PTR actor) {                      // charaset row of player 1 -> body index (-1: unknown)
    int row = *(int *)(actor + 0x12C70) & 0xFFFF;
    if (row >= 0 && row <= 10) return row;
    if (row == 28) return g_sfx_body;
    static const int npc_rows[8] = {29, 32, 33, 36, 37, 38, 40, 41};
    for (int k = 0; k < 8; k++) if (row == npc_rows[k]) return 11 + k;
    return -1;
}

static void sfx_eft_restore() {
    __try {
        if (g_sfx_eft && g_sfx_eft_part >= 0) {
            unsigned char *blk = (unsigned char *)(g_sfx_eft + 0xF4 + 0x1C * (ULONG_PTR)g_sfx_eft_part);
            if (!memcmp(blk, g_sfx_eft_mine, 0x1C)) memcpy(blk, g_sfx_eft_orig, 0x1C);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_sfx_eft = 0; g_sfx_eft_part = -1;
}
static ULONG_PTR sfx_eft_of(int req) {
    ULONG_PTR t = *(ULONG_PTR *)(g_base + RVA_EFT_TABLE);
    if (!t) return 0;
    unsigned n = *(unsigned *)(t + 0xC8);
    ULONG_PTR e = *(ULONG_PTR *)(t + 0xC0);
    for (unsigned i = 0; e && i < n && i < 4096; i++, e += 0x88)
        if (*(int *)(e + 8) == req && *(int *)(e + 4) != 0) return *(ULONG_PTR *)(e + 0x20);
    return 0;
}
// the part to start: the chosen one, or an unused block filled with the chosen pieces (-1 while the effect loads)
static int sfx_track(const SfxSpec &s, int req) {
    if (!s.pieces_custom) return s.part;
    ULONG_PTR eft = sfx_eft_of(req);
    if (!eft) return -1;
    if (g_sfx_eft == eft && g_sfx_eft_part >= 0) return g_sfx_eft_part;
    sfx_eft_restore();
    for (int p = 5; p >= 0; p--) {
        unsigned char *blk = (unsigned char *)(eft + 0xF4 + 0x1C * (ULONG_PTR)p);
        if (blk[0] & 1) continue;
        memcpy(g_sfx_eft_orig, blk, 0x1C);
        unsigned char nb[0x1C];
        memcpy(nb, blk, 0x1C);
        nb[0] = 1; nb[1] = nb[2] = nb[3] = 0;
        short *list = (short *)(nb + 4);
        int k = 0;
        for (int i = 0; i < 4; i++) if (s.piece[i] >= 0) list[k++] = (short)s.piece[i];
        list[k] = -1;
        memcpy(blk, nb, 0x1C);
        memcpy(g_sfx_eft_mine, nb, 0x1C);
        g_sfx_eft = eft; g_sfx_eft_part = p;
        logf("school: custom pieces in part block %d of effect %d (%d pieces)", p, req, k);
        return p;
    }
    set_status("This effect has no free part for custom pieces");
    return s.part;
}

// v5.9 (owner 21:32: Succubus / Damper Fang "keeps the skill on the floor in one spot ... I can walk away from it"; move,
// tilt, turn, size do nothing on them): FUN_14018a040 gives a track-0 instance of a skill effect (request < 1000) slot
// +0xDC = 1, and FUN_140188fd0 then draws it with FUN_14018a8b0 - the rows of *(slot + 0x40) (the owner's skeleton base)
// at slot +0xD0 (the position at the start) - instead of slot +0x50, the matrix FUN_14018d440 moves. Particle emitters
// (FUN_1401a5940) hang on *(slot + 0x40) (modes 3 / 4), the start copy slot +0x90 (mode 5) or FUN_14018a8b0 (mode 7).
// Every frame all of them get the school's matrix: +0x40 points at it, +0x90 and +0xD0 hold it, +0xDC = 0.
// Nodes on a joint (a hash, FUN_140189420 -> FUN_14018aa50) keep the owner's joint ("the skill's own place").
static void sfx_pin(const FxGame &g, unsigned h, float *m) {
    unsigned char *slot = (unsigned char *)(g.mgr + 0x60 + (ULONG_PTR)(h & 0xFFF) * 0xF0);
    if (*(unsigned *)slot != h) return;
    *(float **)(slot + 0x40) = m;
    memcpy(slot + 0x90, m, 64);
    memcpy(slot + 0xD0, m + 12, 12);
    *(int *)(slot + 0xDC) = 0;
}
// ---- v5.9 (owner 21:32: "The hue doesn't affect it though"): the effect's colours. The node colours of an effect are in
// its loaded span (0x13 chunks +0x38 / +0x3C, second pair +0x40 / +0x44, read every frame by FUN_1401a9ae0; the Skill
// Creator's colour preview), shared by every fighter using that skill. An instance reads its span through slot +0x30
// (FUN_14018a040 copies the loader entry's +0x18 there; FUN_140188fd0 / FUN_140189420 find the groups by offsets from it),
// so the school's instance gets a copy of the span of its own, coloured, and the stock skill keeps its colours for
// everyone else. Copies live until the next match (particles already flying still point into them).
struct SfxCopy { int req; char *src, *copy; size_t size; int hue, str; };
static SfxCopy g_sfx_cp[64];
static int g_sfx_cp_n;
static char *g_sfx_data0;                                       // the instance's own span (slot +0x30 at its start)
static char *g_sfx_draw;                                        // what slot +0x30 points at now
static float sfx_hue_of(unsigned w) {
    float r = ((w >> 16) & 255) / 255.0f, g = ((w >> 8) & 255) / 255.0f, b = (w & 255) / 255.0f;
    float mx = max(r, max(g, b)), mn = min(r, min(g, b)), d = mx - mn, h = 0;
    if (d <= 0) return 0;
    if (mx == r) h = fmodf((g - b) / d, 6.0f); else if (mx == g) h = (b - r) / d + 2.0f; else h = (r - g) / d + 4.0f;
    h *= 60.0f;
    return h < 0 ? h + 360.0f : h;
}
static unsigned sfx_tint_abs(unsigned w, int hue, int str) {    // the hue replaced, strength = how much of it
    unsigned t = sc_tint(w, ((int)(hue - sfx_hue_of(w) + 720.5f)) % 360, 100, 100);   // turned so its hue is the school's
    float k = str / 100.0f;
    float r0 = (float)((w >> 16) & 255), g0 = (float)((w >> 8) & 255), b0 = (float)(w & 255);
    float r1 = (float)((t >> 16) & 255), g1 = (float)((t >> 8) & 255), b1 = (float)(t & 255);
    unsigned R = (unsigned)(r0 + (r1 - r0) * k + 0.5f), G = (unsigned)(g0 + (g1 - g0) * k + 0.5f), B = (unsigned)(b0 + (b1 - b0) * k + 0.5f);
    return (w & 0xFF000000u) | (R << 16) | (G << 8) | B;
}
static bool sfx_span_ok(const char *data, size_t size) {        // the groups lie inside [data, data + size)
    if (size < 0x40 || size > (64u << 20) || *(const int *)data != 0x11) return false;
    unsigned n = *(const unsigned short *)(data + 0x10);
    if (0x20 + 4 * (size_t)n > size) return false;
    for (unsigned k = 0; k < n; k++) if (*(const unsigned *)(data + 0x20 + 4 * k) >= size) return false;
    return true;
}
static int sfx_tint_span(const char *src, char *dst, size_t size, int hue, int str) {
    int n = 0;
    for (size_t o = 0; o + 0x48 <= size; o += 4) {
        if (*(const unsigned *)(src + o) != 0x13 || *(const unsigned *)(src + o + 4) != 0x88) continue;
        for (int off = 0x38; off <= 0x44; off += 4) {
            unsigned w = *(const unsigned *)(src + o + off);
            if ((off >= 0x40 && ((w >> 23) & 0xFF) != 0xFF) || (w & 0xFFFFFF) == 0) continue;
            *(unsigned *)(dst + o + off) = hue < 0 ? w : sfx_tint_abs(w, hue, str);
            n++;
        }
    }
    return n;
}
static ULONG_PTR g_sfx_eft_test;                                // self-test: stand-in effect loader table
static char *sfx_coloured(int req, char *data, int hue, int str) {   // the copy to draw from (NULL: none possible)
    ULONG_PTR t = g_sfx_eft_test ? g_sfx_eft_test : *(ULONG_PTR *)(g_base + RVA_EFT_TABLE);
    if (!t || !data) return NULL;
    char *tail = NULL;
    unsigned n = *(unsigned *)(t + 0xC8);
    ULONG_PTR e = *(ULONG_PTR *)(t + 0xC0);
    for (unsigned i = 0; e && i < n && i < 4096; i++, e += 0x88)
        if (*(int *)(e + 8) == req && *(char **)(e + 0x18) == data) { tail = *(char **)(e + 0x20); break; }
    if (!tail || tail <= data) return NULL;
    size_t size = (size_t)(tail - data);
    char msg[96];
    for (int k = 0; k < g_sfx_cp_n; k++) {
        SfxCopy &c = g_sfx_cp[k];
        if (c.req != req || c.src != data || c.size != size) continue;
        if (c.hue != hue || c.str != str) {
            int m = sfx_tint_span(c.src, c.copy, c.size, hue, str);
            c.hue = hue; c.str = str;
            sprintf_s(msg, "Effect color: %d colors turned (its textures keep theirs)", m);
            set_status(msg);
        }
        return c.copy;
    }
    if (!sfx_span_ok(data, size)) { set_status("This effect's colors cannot be changed"); return NULL; }
    if (g_sfx_cp_n >= 64) { set_status("Effect color: too many effects tried this match (next match again)"); return NULL; }
    char *copy = (char *)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!copy) return NULL;
    memcpy(copy, data, size);
    int m = sfx_tint_span(data, copy, size, hue, str);
    SfxCopy nc = {req, data, copy, size, hue, str};
    g_sfx_cp[g_sfx_cp_n++] = nc;
    logf("school effect: color copy of effect %d (%zu bytes, %d colors, hue %d, strength %d)", req, size, m, hue, str);
    sprintf_s(msg, "Effect color: %d colors turned (its textures keep theirs)", m);
    set_status(msg);
    return copy;
}
static void sfx_copies_free() {                                 // a new match: last match's copies are no longer used
    for (int k = 0; k < g_sfx_cp_n; k++) if (g_sfx_cp[k].copy) VirtualFree(g_sfx_cp[k].copy, 0, MEM_RELEASE);
    if (g_sfx_cp_n) logf("school effect: %d color copies of the last match freed", g_sfx_cp_n);
    g_sfx_cp_n = 0;
}
static void sfx_colour_fx(const FxGame &g, unsigned h, const SfxSpec &s) {   // game thread, our instance alive
    unsigned char *slot = (unsigned char *)(g.mgr + 0x60 + (ULONG_PTR)(h & 0xFFF) * 0xF0);
    if (*(unsigned *)slot != h || !g_sfx_data0) return;
    char *want = s.fx_hue >= 0 ? sfx_coloured(g_sfx_req, g_sfx_data0, s.fx_hue, s.fx_str) : NULL;
    if (!want) want = g_sfx_data0;
    if (want != g_sfx_draw) { *(char **)(slot + 0x30) = want; g_sfx_draw = want; }
}
static void sfx_off(const FxGame &g, const char *why) {
    if (fx_alive(g, g_sfx_handle)) { g.stop(g_sfx_handle); logf("school effect: stopped (%s)", why); }
    g_sfx_handle = FX_NONE;
}

static volatile LONG g_sfx_restart;                           // v5.9 R3 in the school test: start the effect again
static float g_sfx_tlast = -1;                                  // the instance time after our last speed step
static int g_sfx_shown = -1;                                    // "with aura": the last aura state (logged when it changes)
static int sfx_spec_key(const SfxSpec &s) {                     // what needs a restart when it changes
    return s.skill * 131 + s.part * 7 + s.pieces_custom * 3 + s.piece[0] * 17 + s.piece[1] * 19 + s.piece[2] * 23 + s.piece[3] * 29 +
           (s.bodypart > 0) * 1000003;
}

static void sfx_step(const SfxSpec &s) {                        // game thread
    FxGame g;
    static bool was_in;                                         // v5.9 colour copies: freed when the next match starts
    if (g_in_match && !was_in && !g_fx_test) sfx_copies_free();
    was_in = g_in_match != 0;
    if (!g_in_match || !fx_game(&g)) {
        if (!g_in_match) { g_sfx_handle = FX_NONE; g_sfx_owner = -1; g_sfx_tab = NULL; g_sfx_tab_saved = false; g_sfx_eft = 0; g_sfx_eft_part = -1; g_sfx_data0 = g_sfx_draw = NULL; }
        return;
    }
    __try {
        ULONG_PTR actor = g.actor;
        if (!actor) return;
        int pidx = *(int *)(actor + 0x14);
        if (!s.on) {
            if (g_sfx_handle != FX_NONE) sfx_off(g, "school off");
            if (g_sfx_tab_saved) sfx_colour_off();
            sfx_eft_restore();
            return;
        }
        if (g.aura) sfx_colour(s, g.aura, pidx);
        if (InterlockedExchange(&g_sfx_restart, 0)) { sfx_off(g, "restart (R3)"); g_sfx_fail = 0; }
        // the effect
        int key = sfx_spec_key(s);
        if (key != g_sfx_key) { sfx_off(g, "changed"); g_sfx_key = key; g_sfx_fail = 0; sfx_eft_restore();
            short prof = s.skill >= 0 && s.skill < 753 ? *(short *)(g.skills + 0x14 + (ULONG_PTR)s.skill * 0x90) : 0;
            g_sfx_req = s.skill >= 0 && s.skill < 753 ? g.effect_of(prof, s.skill) : 0; }
        bool want = g_sfx_req > 0 && *(int *)(actor + ACTOR_ACTIVE) != 0;
        if (want && !s.always) {                                // v5.9: the aura slot's "aura up" (0x20, FUN_14024f2f0) or "shown"
            unsigned fl = g.aura ? *(unsigned *)(g.aura + 0x120 * (ULONG_PTR)pidx + 0xC0) : 0;   // (0x10, FUN_14019b520)
            want = (fl & 0x30) != 0;
            if ((int)want != g_sfx_shown) {
                g_sfx_shown = want;
                logf("school effect: with aura - aura flags %02x (%s)", fl & 0xFF, want ? "aura up: shown" : "aura down: waits");
                if (!want) set_status("Aura effect: waits for your aura ('Aura effect shows: always' shows it now)");
            }
        }
        if (!want) { if (g_sfx_handle != FX_NONE) sfx_off(g, "aura hidden"); return; }
        // where: a body part (spare owner pointing every joint at the matrix) or the effect's own places
        ULONG_PTR ptab = g.mgr + 0xF070 + 0x800 * (ULONG_PTR)pidx;
        float part_pos[3];
        memcpy(part_pos, (const float *)(actor + ACTOR_POS), 12);
        int body = sfx_body_of(actor), bone = -1;
        if (s.bodypart > 0 && body >= 0 && body < 19) bone = SFX_PARTS[body][s.bodypart - 1];
        int owner = pidx;
        if (s.bodypart > 0 && bone < 0) bone = 0;               // an unknown body: every part at the feet
        if (bone >= 0) {
            ULONG_PTR base = *(ULONG_PTR *)(ptab + 8);
            // v5.9 (owner): ground = the fighter's own position (the feet);
            // a joint further than 60 from it (the character is about 34 tall) is not this body's: the feet then too
            if (base && s.bodypart > 1) {
                const float *q = (const float *)(base + (ULONG_PTR)(bone + 1) * 0x40 + 48);
                float dx = q[0] - part_pos[0], dy = q[1] - part_pos[1], dz = q[2] - part_pos[2];
                if (dx * dx + dy * dy + dz * dz < 3600.0f) memcpy(part_pos, q, 12);
                else { static ULONGLONG told; ULONGLONG t = GetTickCount64();
                       if (t - told > 5000) { told = t; logf("school effect: joint %d is %.0f away from the fighter - at the feet instead", bone, sqrtf(dx * dx + dy * dy + dz * dz)); } }
            }
            int spare = -1;
            for (int o = 3; o >= 1 && spare < 0; o--) if (o != pidx && !cwo_actors()[o]) spare = o;
            if (spare >= 0) {                                   // every joint of the spare owner = the school's matrix
                ULONG_PTR st = g.mgr + 0xF070 + 0x800 * (ULONG_PTR)spare;
                int n = *(int *)(g.mgr + 0x11070 + 4 * (ULONG_PTR)pidx);
                if (n > 0x7F) n = 0x7F;
                memcpy((void *)st, (const void *)ptab, 0x10 * (size_t)n);
                for (int k = 1; k < n; k++) *(ULONG_PTR *)(st + 0x10 * (ULONG_PTR)k + 8) = (ULONG_PTR)g_sfx_mat;
                *(int *)(g.mgr + 0x11070 + 4 * (ULONG_PTR)spare) = n;
                owner = spare;
            }
        }
        sfx_matrix(s, g, part_pos, (unsigned)(*(int *)(actor + ACTOR_FACING) + 0x4000), g_sfx_mat);
        if (owner != g_sfx_owner && g_sfx_handle != FX_NONE) sfx_off(g, "owner changed");
        g_sfx_owner = owner;
        if (fx_alive(g, g_sfx_handle)) {
            g.move(g_sfx_handle, g_sfx_mat);
            sfx_pin(g, g_sfx_handle, g_sfx_mat);
            sfx_colour_fx(g, g_sfx_handle, s);
            // speed: v5.9 (owner): FUN_1401899b0 adds
            // the frame step (+0xF068) once per update, and this hook can run more often than that, so a fixed step per call
            // ran it backwards. Now the game's own advance since the last call is scaled (a loop back to 0 starts over).
            float *time = (float *)(g.mgr + 0x60 + (ULONG_PTR)(g_sfx_handle & 0xFFF) * 0xF0 + 0xC);
            float cur = *time, k = s.speed / 100.0f;
            if (s.speed != 100) {
                if (g_sfx_tlast >= 0 && cur >= g_sfx_tlast) *time = g_sfx_tlast + (cur - g_sfx_tlast) * k;
                else if (g_sfx_tlast >= 0) *time = cur * k;
            }
            g_sfx_tlast = *time;
            return;
        }
        if (g_sfx_fail > 180) return;
        int track = sfx_track(s, g_sfx_req);
        if (track < 0) { g_sfx_fail++; return; }
        unsigned h = g.start(g.mgr, owner, g_sfx_req, track, g_sfx_mat, 0);
        if (h == FX_NONE) {
            if (++g_sfx_fail > 180) { logf("school effect: effect %d part %d refused (empty?)", g_sfx_req, track); set_status("This part of the effect is empty"); }
            return;
        }
        g.loop(h);
        sfx_pin(g, h, g_sfx_mat);
        g_sfx_data0 = g_sfx_draw = *(char **)(g.mgr + 0x60 + (ULONG_PTR)(h & 0xFFF) * 0xF0 + 0x30);
        g_sfx_handle = h; g_sfx_fail = 0; g_sfx_tlast = -1;
        logf("school effect: effect %d part %d started (owner %d, body part %d, bone %d)", g_sfx_req, track, owner, s.bodypart, bone);
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_sfx_handle = FX_NONE; }
}

static int sfx_self_test() {                                   // the matrix and the custom-piece block on stand-ins
    SfxSpec s; memset(&s, 0, sizeof(s));
    s.size = 200; s.width = 100; s.height = 50; s.up = 4; s.side = 0; s.fwd = 0;
    struct Id { static void __fastcall turn(float *, unsigned) {} };
    static const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    FxGame g; memset(&g, 0, sizeof(g)); g.identity = I; g.turn = Id::turn;
    float pos[3] = {1, 2, 3}, m[16];
    sfx_matrix(s, g, pos, 0, m);
    bool mat = fabsf(m[0] - 2) < 1e-5f && fabsf(m[5] - 1) < 1e-5f && fabsf(m[10] - 2) < 1e-5f && fabsf(m[12] - 1) < 1e-5f &&
               fabsf(m[13] - 4) < 1e-5f && fabsf(m[14] - 3) < 1e-5f;
    // body table: Player head 71, Kei right hand 42
    bool parts = SFX_PARTS[0][3] == 71 && SFX_PARTS[18][4] == 42 && SFX_PARTS[10][0] == 0;
    // v5.9 the pin on a stand-in instance slot: +0x40 -> the matrix, +0x90 its copy, +0xD0 its position, +0xDC = 0
    static unsigned char mgr[0x60 + 3 * 0xF0];
    memset(mgr, 0, sizeof(mgr));
    FxGame gm; memset(&gm, 0, sizeof(gm)); gm.mgr = (ULONG_PTR)mgr;
    unsigned h = 0x00031002;                                     // serial 3, track 1, slot 2
    unsigned char *sl = mgr + 0x60 + 2 * 0xF0;
    *(unsigned *)sl = h; *(int *)(sl + 0xDC) = 1;
    sfx_pin(gm, h, m);
    bool pin = *(float **)(sl + 0x40) == m && !memcmp(sl + 0x90, m, 64) && !memcmp(sl + 0xD0, m + 12, 12) && *(int *)(sl + 0xDC) == 0;
    *(int *)(sl + 0xDC) = 1;
    sfx_pin(gm, h + 0x10000, m);                                 // a stale handle: untouched
    pin = pin && *(int *)(sl + 0xDC) == 1;
    static __declspec(align(16)) unsigned span[64];
    memset(span, 0, sizeof(span));
    span[0] = 0x11; *(unsigned short *)((char *)span + 0x10) = 1; span[8] = 0x40;   // 1 group at +0x40
    span[16] = 0x13; span[17] = 0x88; span[16 + 14] = 0xFFFF0000u;                  // its node colour: red
    static unsigned char ent[0x88]; static unsigned char tab[0xD0];
    memset(ent, 0, sizeof(ent)); memset(tab, 0, sizeof(tab));
    *(int *)(ent + 4) = 1; *(int *)(ent + 8) = 7001; *(char **)(ent + 0x18) = (char *)span; *(char **)(ent + 0x20) = (char *)span + sizeof(span);
    *(unsigned char **)(tab + 0xC0) = ent; *(unsigned *)(tab + 0xC8) = 1;
    g_sfx_eft_test = (ULONG_PTR)tab;
    int keep_n = g_sfx_cp_n;
    char *cp = sfx_coloured(7001, (char *)span, 120, 100);
    bool col = cp && cp != (char *)span && *(unsigned *)(cp + 0x78) == 0xFF00FF00u && span[16 + 14] == 0xFFFF0000u;
    char *cp2 = sfx_coloured(7001, (char *)span, 240, 50);                          // the same copy, coloured again
    col = col && cp2 == cp && *(unsigned *)(cp + 0x78) == sfx_tint_abs(0xFFFF0000u, 240, 50) && g_sfx_cp_n == keep_n + 1;
    sfx_copies_free();
    g_sfx_eft_test = 0;
    logf("self-test: school matrix %s, body parts %s, pin %s, effect color %s", mat ? "ok" : "FAILED", parts ? "ok" : "FAILED",
         pin ? "ok" : "FAILED", col ? "ok" : "FAILED");
    return mat && parts && pin && col ? 1 : -32;
}
