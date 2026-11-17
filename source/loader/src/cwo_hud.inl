// ---- v5.65 HUD presets (owner 2026-11-02). Independently authored from the game's own code and data (Ghidra full pass 065 + a runtime
// probe in an offline split-screen match, 2026-11-02, MODLOG):
//  - the battle HUD is CBattle2DMenuClass; its draw FUN_140150c30 takes every element's x / y from small layout tables
//    in .rdata, one 8-byte row {int x, int y} per layout: row 0 the full screen (Xbox Live / 4 players / Single Play),
//    rows 1 / 2 the two split-screen halves (HUD +0x2a0 != 0 = split, the half = the player):
//      0x1402f1250  the player's own panel (name, life bar)        rows (-59,57) (-59,57) (462,57)
//      0x1402f14e8  the capsule badge ("26" + the capsules out)    rows (603,36) (84,57) (605,57)
//      0x1402f1500  the skill wheel (the hand on the face buttons)  rows (507,344) (79,344) (505,344)
//      0x1402f12d0  the other players' side panels (full screen)    rows (463,92) (-39,92) (446,92)
//      0x1402f11e0  the side panels' second column                  rows (465,198) (-39,210) (446,210)
//    (the probe: +100 on a row-1 value moved that element by 100 the same frame; rows 1 / 2 of the side panels have no
//    effect in split screen);
//  - the black bars are plain quads: the clock / text-box drawer FUN_140154cb0 (one call, 0x140151383, before the
//    elements) draws FUN_14029b890(0, 0, 640, 84, black) and (0, 312, 640, 168, black) only in split screen; in the full
//    screen it draws a 640 x 52 strip at y 428 (the text box) instead; black = the game's own colour word 0x1402f1268.
// (5.65.2, from the scoreboard-match shots: the other players' panels stack under your own panel - the side tables do
// not move them in the full screen - so presets 1 / 2 lift the stack into a 96-high top bar; preset 4 sits above the
// message box.)
// A preset = the full-screen row (row 0) of the five tables + whether the bars are drawn in the full screen. Split screen
// keeps the game's own rows. The choice is CwoHud.txt in the data folder ("preset N"), the lobby's BFR OPTIONS row "HUD
// Preset" (A cycles) and, offline, the file alone. Display only, this machine only.
static const ULONG_PTR RVA_HUD_PANEL = 0x2f1250, RVA_HUD_BADGE = 0x2f14e8, RVA_HUD_WHEEL = 0x2f1500, RVA_HUD_SIDE = 0x2f12d0,
                       RVA_HUD_SIDE2 = 0x2f11e0, RVA_HUD_CLOCK_CALL = 0x151383, RVA_HUD_CLOCK_FN = 0x154cb0,
                       RVA_HUD_QUAD = 0x29b890, RVA_HUD_BLACK = 0x2f1268, RVA_HUD_STATE = 0x20c40;
static const bool HUD_ONLINE = true;                             // owner 2026-11-02 (AGENTS.md "HUD presets online"): display only
struct HudPreset { const char *name; int panel[2], badge[2], wheel[2], side[2], side2[2]; int top_bar, bot_bar; };
static const HudPreset HUD_PRESETS[] = {
    {"Game's own",   {-59, 57}, {603, 36}, {507, 344}, {463, 92}, {465, 198}, 0, 0},
    {"Split Look",   {-59, 20}, {84, 20},  {240, 344}, {463, 92}, {465, 198}, 96, 168},   // preset 1: the owner's
    {"Wide Bars",    {-59, 20}, {603, 20}, {79, 344},  {463, 92}, {465, 198}, 96, 168},
    {"Centre Stage", {-59, 57}, {280, 36}, {240, 344}, {463, 92}, {465, 198}, 0, 0},
    {"Low Profile",  {-59, 316}, {84, 316}, {507, 344}, {463, 30}, {465, 136}, 0, 0},
    {"Mirror",       {462, 57}, {84, 57},  {79, 344},  {10, 92},  {12, 198},  0, 0},
    {"Minimal",      {-59, 57}, {900, 36}, {507, 344}, {900, 92}, {900, 198}, 0, 0},
};
static const int HUD_N = (int)(sizeof(HUD_PRESETS) / sizeof(HUD_PRESETS[0]));
static int g_hud_preset, g_hud_applied = -1, g_hud_hook_state;
static int g_hud_stock[5][2];
static bool g_hud_stock_saved;
static int *g_hud_t_tables;                                      // self-test stand-in (5 x 2 ints)
typedef void(__fastcall *PFN_HUD_CLOCK)(INT64);
typedef void(__fastcall *PFN_HUD_QUAD)(int, short, int, short, const unsigned *);
typedef void(__fastcall *PFN_HUD_STATE)(int, int, int);
static PFN_HUD_CLOCK g_hud_clock_orig;
static bool ed_online();                                         // cwo_edgar.inl

static int *hud_row0(int k) {                                    // table k's full-screen row
    static const ULONG_PTR rva[5] = {RVA_HUD_PANEL, RVA_HUD_BADGE, RVA_HUD_WHEEL, RVA_HUD_SIDE, RVA_HUD_SIDE2};
    return g_hud_t_tables ? g_hud_t_tables + 2 * k : (int *)(g_base + rva[k]);
}
static void hud_path(wchar_t *p) { swprintf_s(p, MAX_PATH, L"%sCwoHud.txt", g_dir); }
static int hud_load() {
    wchar_t p[MAX_PATH];
    hud_path(p);
    FILE *f = NULL;
    int v = 0;
    if (_wfopen_s(&f, p, L"r") == 0 && f) {
        char line[64] = "";
        if (fgets(line, sizeof(line), f)) sscanf_s(line, "preset %d", &v);
        fclose(f);
    }
    return v >= 0 && v < HUD_N ? v : 0;
}
static void hud_save() {
    wchar_t p[MAX_PATH];
    hud_path(p);
    FILE *f = NULL;
    if (_wfopen_s(&f, p, L"w") == 0 && f) { fprintf(f, "preset %d\n", g_hud_preset); fclose(f); }
}
static bool hud_write(int *row, const int *xy) {
    if (row[0] == xy[0] && row[1] == xy[1]) return true;
    DWORD old;
    if (!VirtualProtect(row, 8, PAGE_READWRITE, &old)) return false;
    row[0] = xy[0]; row[1] = xy[1];
    VirtualProtect(row, 8, old, &old);
    return true;
}
static void hud_apply(int p) {                                   // row 0 of every table := the preset (0 = the stock rows)
    if (p < 0 || p >= HUD_N) p = 0;
    if (!g_hud_stock_saved) {
        for (int k = 0; k < 5; k++) { g_hud_stock[k][0] = hud_row0(k)[0]; g_hud_stock[k][1] = hud_row0(k)[1]; }
        g_hud_stock_saved = true;
    }
    const HudPreset &h = HUD_PRESETS[p];
    const int *xy[5] = {h.panel, h.badge, h.wheel, h.side, h.side2};
    bool ok = true;
    for (int k = 0; k < 5; k++) ok = hud_write(hud_row0(k), p ? xy[k] : g_hud_stock[k]) && ok;
    g_hud_applied = p;
    if (!g_hud_t_tables) logf("HUD preset %d '%s' %s (bars %d / %d)", p, h.name, ok ? "applied" : "PARTLY applied", h.top_bar, h.bot_bar);
}
static volatile LONG g_maptest_on;                               // v5.146 set by gt_maptest_step (cwo_gametype.inl) in a Map Test match
static void __fastcall hud_clock_hook(INT64 hud) {                // in place of the clock / text-box call: the bars first
    // v5.146 (owner 2026-11-10): the split-screen bars are FUN_140154cb0's own quads, drawn only while HUD +0x2a0 (split) is set; in a Map
    // Test match that one call runs with +0x2a0 = 0 (its full-screen look) and the flag goes back at once
    if (g_maptest_on) {
        int sp = 0;
        __try { sp = *(int *)(hud + 0x2a0); *(int *)(hud + 0x2a0) = 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_hud_clock_orig(hud);
        __try { *(int *)(hud + 0x2a0) = sp; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return;
    }
    __try {
        const HudPreset &h = HUD_PRESETS[g_hud_applied >= 0 && g_hud_applied < HUD_N ? g_hud_applied : 0];
        if ((h.top_bar || h.bot_bar) && *(int *)(hud + 0x2a0) == 0 && *(int *)(hud + 0x50) != 0) {
            PFN_HUD_STATE st = (PFN_HUD_STATE)(g_base + RVA_HUD_STATE);
            PFN_HUD_QUAD quad = (PFN_HUD_QUAD)(g_base + RVA_HUD_QUAD);
            const unsigned *black = (const unsigned *)(g_base + RVA_HUD_BLACK);
            st(0, 3, 2); st(0, 4, 2);
            if (h.top_bar) quad(0, 0, 0x280, (short)h.top_bar, black);
            if (h.bot_bar) quad(0, (short)(480 - h.bot_bar), 0x280, (short)h.bot_bar, black);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_hud_clock_orig(hud);
}
static void hud_install() {                                      // worker start: the clock call -> the hook
    unsigned char *site = (unsigned char *)(g_base + RVA_HUD_CLOCK_CALL);
    if (site[0] != 0xE8 || (ULONG_PTR)(site + 5 + *(int *)(site + 1)) != g_base + RVA_HUD_CLOCK_FN) {
        logf("HUD presets: the clock call at %p is not the game's - bars off", (void *)site);
        g_hud_hook_state = -1;
        return;
    }
    ULONG_PTR stub = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!stub) { g_hud_hook_state = -1; return; }
    unsigned char *c = (unsigned char *)stub;
    c[0] = 0x48; c[1] = 0xB8; *(ULONGLONG *)(c + 2) = (ULONGLONG)(ULONG_PTR)hud_clock_hook; c[10] = 0xFF; c[11] = 0xE0;
    FlushInstructionCache(GetCurrentProcess(), c, 12);
    g_hud_clock_orig = (PFN_HUD_CLOCK)(g_base + RVA_HUD_CLOCK_FN);
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) { g_hud_hook_state = -1; return; }
    *(int *)(site + 1) = (int)(stub - (ULONG_PTR)(site + 5));
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    g_hud_hook_state = 1;
    g_hud_preset = hud_load();
    logf("HUD presets: %d presets, the clock call hooked (stub %p), preset %d from CwoHud.txt", HUD_N, (void *)stub, g_hud_preset);
}
static void hud_step() {                                         // game thread, once a second: the file, the online gate
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (now < next || g_hud_hook_state != 1) return;
    next = now + 1000;
    __try {
        int want = g_hud_preset;
        static int file_last = -1;
        int fv = hud_load();
        if (fv != file_last) { file_last = fv; want = g_hud_preset = fv; }
        if (!HUD_ONLINE && ed_online()) want = 0;
        if (want != g_hud_applied) hud_apply(want);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static const char *hud_row_text() {                              // the lobby's BFR OPTIONS row
    static char t[48];
    sprintf_s(t, "HUD Preset: %d %s", g_hud_preset, HUD_PRESETS[g_hud_preset].name);
    return t;
}
static void hud_cycle() {                                        // A on the row
    g_hud_preset = (g_hud_preset + 1) % HUD_N;
    hud_save();
    char m[96];
    sprintf_s(m, "HUD preset %d: %s%s", g_hud_preset, HUD_PRESETS[g_hud_preset].name,
              !HUD_ONLINE && ed_online() ? " (offline matches only for now)" : "");
    set_status(m);
    logf("HUD presets: row -> %d", g_hud_preset);
}

// v5.86.2 OPTIONS > HUD Preset in the game's own menu (owner 2026-11-03; "You must retain the theme of Phantom Dust ... use the font text and box
// and everything that's in the options"): tools/build_hud_option.py v2 makes the game's window 1 list the presets (A = op
// 103(k)) and its window 9 "PREVIEW", whose row-draw routine passes its place (op 104); the loader draws, inside that window,
// the highlighted row's layout: a 640 x 480 battle screen at 42 % with each element where the preset puts it.
static ULONGLONG g_hp_seen;
static int g_hp_x, g_hp_y;
static INT64 hp_native(int op, INT64 *args) {
    if (op == 103) {
        int k = (int)args[1];
        if (k < 0 || k >= HUD_N) return 0;
        g_hud_preset = k; hud_save();
        char m[80]; sprintf_s(m, "HUD preset %d: %s - in use", k, HUD_PRESETS[k].name); set_status(m);
        logf("HUD presets: OPTIONS -> %d", k);
        return 1;
    }
    if (op == 104) {
        static bool logged;
        g_hp_x = (int)args[1]; g_hp_y = (int)args[2]; g_hp_seen = GetTickCount64();
        if (!logged) { logged = true; logf("HUD presets: PREVIEW window at %d, %d", g_hp_x, g_hp_y); }
    }
    return 0;
}
static int hp_cursor() {                                          // window 1's cursor row (the window record, cwo_selout.inl)
    __try {
        ULONG_PTR menu = *(ULONG_PTR *)(g_base + 0x533c58);
        if (!menu) return -1;
        const unsigned char *w = (const unsigned char *)(menu + 0x40 + 1 * (ULONG_PTR)0x103c);
        int cur = *(const short *)(w + 0x12), rows = *(const short *)(w + 0x16);
        return rows == HUD_N && cur >= 0 && cur < rows ? cur : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static void hp_draw() {
    if (g_in_match || GetTickCount64() - g_hp_seen > 200) return;
    int sel = hp_cursor();
    if (sel < 0) return;
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 400 || *(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4]; memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    auto box = [&](int px, int py, DWORD col, int w, int h) { if (w <= 0 || h <= 0) return; *space = 0; *gap = 0; *fh = h; *fw = w; set_rgba(rgba, col); text(px, py, "\x81\xa1"); };
    auto put = [&](int px, int py, const char *s, DWORD col) { *space = 0; *gap = 1; *fh = 10; *fw = 5; set_rgba(rgba, 0xFF000000); text(px + 1, py + 1, s); set_rgba(rgba, col); text(px, py, s); };
    const int PX = g_hp_x + 14, PY = g_hp_y + 6; const float SC = 0.42f;
    auto r = [&](int x, int y, int w, int h, DWORD col) {            // a rectangle in battle-screen units, clipped, in tiles
        int x1 = x + w, y1 = y + h; if (x < 0) x = 0; if (y < 0) y = 0; if (x1 > 640) x1 = 640; if (y1 > 480) y1 = 480;
        if (x1 <= x || y1 <= y) return;
        int sx0 = PX + (int)(x * SC), sy0 = PY + (int)(y * SC), sx1 = PX + (int)(x1 * SC) + 1, sy1 = PY + (int)(y1 * SC) + 1;
        for (int ty = sy0; ty < sy1; ty += 24)                       // the box glyph fills ~3/4 of its height (MOD LIST overlaps too)
            for (int tx = sx0; tx < sx1; tx += 24) box(tx, ty, col, min(24, sx1 - tx), min(32, (sy1 - ty) * 4 / 3));
    };
    auto lab = [&](int x, int y, const char *s) { if (x >= 0 && x < 600 && y >= 0 && y < 470) put(PX + (int)(x * SC) + 2, PY + (int)(y * SC) + 1, s, 0xFFFFFFFF); };
    r(0, 0, 640, 480, 0xFF24343A);                                    // the stage (the menus' own blue-grey)
    r(0, 300, 640, 180, 0xFF33463F);                                  // ground
    r(300, 190, 36, 110, 0xFF6E6458);                                 // your fighter
    const HudPreset &h = HUD_PRESETS[sel];
    if (h.top_bar) r(0, 0, 640, h.top_bar, 0xFF000000);
    if (h.bot_bar) r(0, 480 - h.bot_bar, 640, h.bot_bar, 0xFF000000);
    // element extents in battle-screen units (estimates from the 2026-11-02 preset shots)
    r(h.panel[0] + 59, h.panel[1] - 24, 230, 44, 0xFF3C8C5A); lab(h.panel[0] + 64, h.panel[1] - 20, "LIFE");
    r(h.badge[0] - 8, h.badge[1] - 16, 48, 30, 0xFFB8862E); lab(h.badge[0] - 4, h.badge[1] - 12, "CAP");
    r(h.wheel[0] - 10, h.wheel[1] - 34, 120, 110, 0xFF3E5C9E); lab(h.wheel[0] - 4, h.wheel[1] - 30, "SKILLS");
    r(h.side[0], h.side[1] - 15, 170, 30, 0xFF8E3A3A); lab(h.side[0] + 4, h.side[1] - 12, "OTHERS");
    r(h.side2[0], h.side2[1] - 15, 170, 30, 0xFF8E3A3A);
    char t[48]; sprintf_s(t, "%s%s", HUD_PRESETS[sel].name, sel == g_hud_preset ? "  (in use)" : "");
    *space = 0; *gap = 2; *fh = 14; *fw = 7; set_rgba(rgba, 0xFF000000); text(PX + 1, PY + (int)(480 * SC) + 9, t);
    set_rgba(rgba, 0xFFFFD040); text(PX, PY + (int)(480 * SC) + 8, t);
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}
static int hud_self_test() {
    static int t[10];
    for (int k = 0; k < 10; k++) t[k] = 1000 + k;
    g_hud_t_tables = t;
    g_hud_stock_saved = false;
    hud_apply(1);
    bool a = t[2] == 84 && t[3] == 20 && t[4] == 240 && t[5] == 344 && t[0] == -59 && t[1] == 20;
    hud_apply(0);
    bool b = t[0] == 1000 && t[5] == 1005 && t[9] == 1009;
    g_hud_t_tables = NULL; g_hud_stock_saved = false; g_hud_applied = -1;
    logf("self-test: HUD presets apply %s, stock back %s (%d presets)", a ? "ok" : "FAILED", b ? "ok" : "FAILED", HUD_N);
    return a && b ? 1 : -65;
}
