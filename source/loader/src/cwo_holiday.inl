// ---- v5.74 holiday themes (owner 2026-11-02; "that start screen will have the theme applied as well").
// Game facts (PD Ghidra full pass 065, MODLOG 2026-11-02): the menus are drawn with light set 1 (0x1403e8a40 + 0x58: three
// BGRA colours at +0 / +4 / +8; FUN_140199510 turns set n into the light constants 8n + 0x40 / + 0x41 at 0x1405a6a40, 0x70
// each); in the menus set 1's first colour is pure green (00 ff 00) - the menus' green look. The scan lines and grid are
// textures, so they stay.
// What this does: outside matches (and outside the creator previews), while a holiday is on, set 1's first colour and the
// two light constants take the holiday's colour every frame; in a match nothing is touched (the stage keeps its own
// lights). At the start screen a greeting panel shows for 10 s ("Merry Christmas ... the Christmas theme is on until
// Dec 26"), then a What's New panel for 10 s (CwoWhatsNew.txt lines, or the built-in list). CwoTheme.txt: "auto" (the
// date), "off", or a holiday's name to try it out of season.
struct Holiday { const char *name, *greet; int m0, d0, m1, d1; unsigned char bgr[3], bgr2[3]; };
static const Holiday HOLIDAYS[] = {
    {"New Year", "Happy New Year! Here's to a great year of fights.", 12, 31, 1, 2, {40, 200, 255}, {255, 255, 255}},
    {"Valentine's Day", "Happy Valentine's Day! Spread the love - then spread the aura.", 2, 10, 2, 15, {150, 60, 255}, {200, 160, 255}},
    {"St. Patrick's Day", "Happy St. Patrick's Day! May the luck of the capsules be with you.", 3, 15, 3, 18, {60, 255, 60}, {120, 220, 255}},
    {"Easter", "Happy Easter! Hope your day is full of good things.", 0, 0, 0, 0, {255, 200, 200}, {200, 255, 230}},
    {"Independence Day", "Happy 4th of July! Hope you have a wonderful day.", 7, 1, 7, 5, {255, 80, 40}, {40, 40, 255}},
    {"Halloween", "Happy Halloween! The dust is spooky tonight.", 10, 24, 11, 1, {0, 110, 255}, {200, 40, 150}},
    {"Thanksgiving", "Happy Thanksgiving! Thankful for every fight with you.", 0, 0, 0, 0, {20, 120, 220}, {40, 70, 150}},
    {"Christmas", "Merry Christmas! Hope you have a wonderful day.", 12, 18, 12, 26, {40, 40, 230}, {255, 255, 255}},
};
static const int HOL_N = (int)(sizeof(HOLIDAYS) / sizeof(HOLIDAYS[0]));
static const ULONG_PTR RVA_TH_SETS = 0x3e8a40, RVA_TH_LIGHTS = 0x5a6a40;
static int g_th_on = -1;                                         // the holiday in use (-1 none)
// v5.202.0 (owner 2026-11-23): the holidays keep their
// words (greeting panel, title banner, What's New) and change no colour - no menu light tint, no post-pass palette, the
// panels' accent stays the normal one, the banner is white
static const bool TH_COLOURS = false;
static int g_th_until_m, g_th_until_d;
static bool g_th_forced;
static unsigned char g_th_keep[12], g_th_lights_keep[2 * 0x70];
static unsigned char g_th_wrote_s1[12], g_th_wrote_l1[2 * 0x70];   // v5.201.1 what the holiday wrote last frame
static bool g_th_applied;
static ULONGLONG g_th_cfg_t, g_th_popup_t;
// g_th_popup (cwo_loader.cpp): 0 not yet, 1 greeting, 4 waiting for the main menu, 2 what's new, 3 done
static int g_th_scroll;
static const int TH_MAIN_MENU_ID = 8,   TH_ROWS = 8;           // the main menu = menu 8 (title 7; logged 14:15)

static void th_easter(int y, int *m, int *d) {                   // Gregorian Easter Sunday (anonymous algorithm)
    int a = y % 19, b = y / 100, c = y % 100, dd = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3,
        h = (19 * a + b - dd - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7, mm = (a + 11 * h + 22 * l) / 451;
    *m = (h + l - 7 * mm + 114) / 31; *d = (h + l - 7 * mm + 114) % 31 + 1;
}
static int th_doy(int y, int m, int d) {                         // day of the year (1-based)
    static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return cum[m - 1] + d + (leap && m > 2 ? 1 : 0);
}
static void th_from_doy(int y, int doy, int *m, int *d) {
    for (int mm = 12; mm >= 1; mm--) { int s = th_doy(y, mm, 1); if (doy >= s) { *m = mm; *d = doy - s + 1; return; } }
    *m = 1; *d = 1;
}
static int th_pick(int y, int m, int d, int *um, int *ud) {      // the holiday on this date, and its last day
    int today = th_doy(y, m, d);
    for (int i = 0; i < HOL_N; i++) {
        const Holiday &h = HOLIDAYS[i];
        int a, b;
        if (!strcmp(h.name, "Easter")) { int em, ed; th_easter(y, &em, &ed); int e = th_doy(y, em, ed); a = e - 2; b = e + 1; }
        else if (!strcmp(h.name, "Thanksgiving")) {               // the 4th Thursday of November: its Monday .. Sunday
            int n1 = th_doy(y, 11, 1);
            SYSTEMTIME st = {}; st.wYear = (WORD)y; st.wMonth = 11; st.wDay = 1;
            FILETIME ft; SystemTimeToFileTime(&st, &ft); FileTimeToSystemTime(&ft, &st);
            int first_thu = 1 + (4 - st.wDayOfWeek + 7) % 7, thu = n1 + first_thu - 1 + 21;
            a = thu - 3; b = thu + 3;
        } else if (h.m0 > h.m1) {                                 // across the new year (New Year: Dec 31 - Jan 2)
            if (m == h.m0 && d >= h.d0) { *um = h.m1; *ud = h.d1; return i; }
            if (m == h.m1 && d <= h.d1) { *um = h.m1; *ud = h.d1; return i; }
            continue;
        } else { a = th_doy(y, h.m0, h.d0); b = th_doy(y, h.m1, h.d1); }
        if (today >= a && today <= b) { th_from_doy(y, b, um, ud); return i; }
    }
    return -1;
}
static void th_cfg() {                                           // CwoTheme.txt + the date, once every 5 s
    ULONGLONG now = GetTickCount64();
    if (now < g_th_cfg_t) return;
    g_th_cfg_t = now + 5000;
    char line[64] = "auto";
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoTheme.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(line, sizeof(line), f)) strcpy_s(line, "auto"); fclose(f); }
    for (char *c = line; *c; c++) if (*c == '\r' || *c == '\n') *c = 0;
    SYSTEMTIME st; GetLocalTime(&st);
    int pick = -1, um = 0, ud = 0;
    g_th_forced = false;
    if (!_stricmp(line, "off")) pick = -1;
    else if (_stricmp(line, "auto") && line[0]) {
        for (int i = 0; i < HOL_N; i++) if (!_stricmp(line, HOLIDAYS[i].name)) { pick = i; g_th_forced = true; }
        um = st.wMonth; ud = st.wDay;
    } else pick = th_pick(st.wYear, st.wMonth, st.wDay, &um, &ud);
    if (pick != g_th_on) logf("holiday theme: %s%s", pick >= 0 ? HOLIDAYS[pick].name : "none", g_th_forced ? " (CwoTheme.txt)" : "");
    g_th_on = pick; g_th_until_m = um; g_th_until_d = ud;
}
static void th_lights(bool on) {
    __try {
        unsigned char *s1 = (unsigned char *)(g_base + RVA_TH_SETS) + 0x58;
        unsigned char *l1 = (unsigned char *)(g_base + RVA_TH_LIGHTS) + 0x48 * 0x70;
        if (on) {
            // v5.201.1 (owner 2026-11-23, Thanksgiving week: every fighter BLACK on every stage): set 1 is also the fighters'
            // light. A stage's light script writes it while the stage loads - still "menus" here - and the match start used to
            // put back the menu values kept before, over the stage's own set 1. Now: whenever set 1 / its light constants are not
            // what the holiday wrote last frame, the GAME wrote them (a light script) - they become the values to put back.
            if (!g_th_applied || memcmp(s1, g_th_wrote_s1, 12) || memcmp(l1, g_th_wrote_l1, sizeof(g_th_wrote_l1))) {
                static ULONGLONG s_said;
                if (g_th_applied && GetTickCount64() - s_said > 10000) { s_said = GetTickCount64(); logf("holiday theme: the game set light set 1 again (a stage light) - kept for the match"); }
                memcpy(g_th_keep, s1, 12); memcpy(g_th_lights_keep, l1, sizeof(g_th_lights_keep)); g_th_applied = true;
            }
            const Holiday &h = HOLIDAYS[g_th_on];
            s1[0] = h.bgr[0]; s1[1] = h.bgr[1]; s1[2] = h.bgr[2];
            s1[8] = h.bgr2[0] / 3; s1[9] = h.bgr2[1] / 3; s1[10] = h.bgr2[2] / 3;
            float k = *(float *)(l1 + 0x00) + *(float *)(l1 + 0x04) + *(float *)(l1 + 0x08);   // the light's own strength
            float sum = (float)(h.bgr[0] + h.bgr[1] + h.bgr[2]);
            if (k > 0.0f && sum > 0.0f) {                        // r g b at +0 / +4 / +8 (and again at +0x10): same strength, holiday hue
                float r = h.bgr[2] * k / sum, g = h.bgr[1] * k / sum, b = h.bgr[0] * k / sum;
                for (int o = 0; o <= 0x10; o += 0x10) { *(float *)(l1 + o) = r; *(float *)(l1 + o + 4) = g; *(float *)(l1 + o + 8) = b; }
            }
            memcpy(g_th_wrote_s1, s1, 12); memcpy(g_th_wrote_l1, l1, sizeof(g_th_wrote_l1));
        } else if (g_th_applied) {
            memcpy(s1, g_th_keep, 12); memcpy(l1, g_th_lights_keep, sizeof(g_th_lights_keep)); g_th_applied = false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static volatile LONG g_pp_mode_want;                             // v5.78 -> cwo_post.inl g_pp_mode
// v5.134 menu themes (owner 2026-11-10 + screenshots): CwoUiTheme.txt "white" / "off"
static volatile LONG g_pp_white, g_pp_white_ui;                 // v5.135 g_pp_white_ui: only the game's teal UI (not the main menu)
static void ut_cfg() {                                           // every 5 s
    static ULONGLONG t; ULONGLONG now = GetTickCount64();
    if (now < t) return;
    t = now + 5000;
    char line[32] = "off"; wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoUiTheme.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(line, sizeof(line), f)) strcpy_s(line, "off"); fclose(f); }
    else if (cwo_wfopen(&f, p, L"w") == 0 && f) { fprintf(f, "off\n# menu theme: white (minimalistic white) or off\n"); fclose(f); }
    LONG w = !_strnicmp(line, "white", 5) ? 1 : 0;
    if (w != g_pp_white) logf("menu theme: %s", w ? "minimalistic white" : "off");
    InterlockedExchange(&g_pp_white, w);
}
static void th_step() {                                          // CwoOverlayFrame
    th_cfg();
    bool menus = !g_in_match && !g_co_prev && !g_cc_prev;
    th_lights(TH_COLOURS && g_th_on >= 0 && menus);
    // v5.74.4 (owner): the greeting stays on the start screen; What's New waits
    // for the main menu (menu id, *(*0x140533c58 + 0x105f4); logged once per change) and holds the pad until OK
    int mid = -1;
    __try { ULONG_PTR mm = *(ULONG_PTR *)(g_base + 0x533c58); mid = mm ? *(short *)(mm + 0x105f4) : -1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    static int last_mid = -2;
    if (mid != last_mid) { logf("holiday theme: menu id %d", mid); last_mid = mid; }
    // v5.78 the post pass (cwo_post.inl); v5.78.5 the title and main menu only (in game 15:47 it also turned the select
    // portraits' greens - character art - to the holiday colours)
    g_pp_mode_want = TH_COLOURS && g_th_on >= 0 && menus && (mid == 7 || mid == TH_MAIN_MENU_ID) ? 1 + g_th_on + (mid == 7 ? 64 : 0) : 0;
    ut_cfg();                                                     // v5.134 the white theme: title + main menu, no holiday palette
    // v5.134.1 (in game 06:49: the title's background movie came out inverted) - the main menu in full;
    // v5.135 (owner): every other menu (not the title, not a match) - only the
    // game's teal UI is turned white, so character art, portraits and the stages behind the lists keep their colours
    InterlockedExchange(&g_pp_white_ui, mid != TH_MAIN_MENU_ID ? 1 : 0);
    if (g_pp_white && menus && mid != 7 && mid >= 0) g_pp_mode_want = 63;
    if (g_th_popup == 1 && GetTickCount64() - g_th_popup_t > 60000u) g_th_popup = 4;   // v5.86.1 a minute (it was missed at 12 s)   // v5.86 also without a holiday (the MOD LIST line)   // greeting over, wait for the main menu
    // v5.86: What's New lives in the MOD LIST now; the start screen's panel points to it (th_draw)
    if (g_th_popup == 4) g_th_popup = 3;
    if (g_in_match && g_th_popup != 3) g_th_popup = 3;
}
static const char *TH_NEWS[] = {"WHAT'S NEW (BFR, 14 Nov)",          // v5.185.2 (it still said BFR 1.47); v5.186.0 + 2 rows
                                "- Single Play: the CPUs move and fight",
                                "- Main menu: CREATE, Single Play, FEEDBACK,",
                                "  TO-DO LIST - all 8 rows drawn",
                                "- Pause menu: Teammate Lock-on row",
                                "- OPTIONS: Records, Visions, Stage Music",
                                "- COMING SOON can no longer be highlighted",
                                "- HUD faces for fighters 5 and 6 (3 vs 3)",
                                "- Stage and character lists stay right after",
                                "  a slow start",
                                "- Freia and Gyne: a fuller figure",
                                "- Skills you make: Aura Particles online",
                                "- FEEDBACK: share a skill you made",
                                "- No crash at the end of a match after an",
                                "  effect was pushed out of memory", NULL};
// v5.170 (2026-11-13 quality task: the panel said "BFR 1.47" in MODS too): MODS has its own What's New; BFR keeps the list above
static const char *TH_NEWS_MODS[] = {"WHAT'S NEW (MODS, 13 Nov)",
                                "- Illusion school: 75 skills (753-827) with its",
                                "  own mark, silver effects and aura (offline)",
                                "- Edit Arsenal: Illusion counts as a school, has",
                                "  its icon in the picker and in MASK",
                                "- Single School rule lists the Illusion school",
                                "- COLORS list scrolls, clear of the help line",
                                "- A rare crash at game start is fixed",
                                "- Crash shield: an effect whose texture was",
                                "  freed no longer closes the game",
                                "- Create a Skill lists your own arsenals",
                                "- FEEDBACK finds your gamertag",
                                "- Edgar's Dream help lines fit on the screen",
                                "- The Bunker loads again (its crash is fixed)", NULL};
static const char **th_news() { return g_bfr_edition ? TH_NEWS : TH_NEWS_MODS; }
static int th_news_n() { const char **t = th_news(); int n = 0; while (t[n]) n++; return n; }
static void th_input(WORD pressed) {                              // pad thread, while What's New shows
    int maxs = th_news_n() - TH_ROWS; if (maxs < 0) maxs = 0;
    if (pressed & BTN_UP) g_th_scroll = g_th_scroll > 0 ? g_th_scroll - 1 : maxs;      // v5.120 loops
    if (pressed & BTN_DOWN) g_th_scroll = g_th_scroll < maxs ? g_th_scroll + 1 : 0;
    if ((pressed & BTN_A) && g_th_scroll >= maxs) { g_th_popup = 3; InterlockedExchange(&g_ed_game_hold, 1); rumble(1); }   // v5.86 its A stays here
}
static ULONGLONG g_th_start;
static DWORD th_accent();                                       // cwo_post.inl
static void th_draw() {                                          // the start-screen panels (font route as dbtl_draw)
    if (!g_th_start) g_th_start = GetTickCount64();
    if (g_th_popup == 0) {                                        // the first frame it can draw, once the start screen is up
        if (g_in_match || GetTickCount64() - g_th_start < 26000) return;
        g_th_popup = 1; g_th_popup_t = GetTickCount64();
    }
    if (g_th_popup != 1 && g_th_popup != 2) return;
    if (g_th_popup == 1) {                                        // v5.86.3 the start-screen panel only on the title (menu 7)
        int mid = -1;
        __try { ULONG_PTR m = *(ULONG_PTR *)(g_base + 0x533c58); mid = m ? *(short *)(m + 0x105f4) : -1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (mid != 7) return;
        // v5.87 (owner): the title shares
        // menu 7 with LOAD PROFILE - any menu window with rows (window record +0x16) means the title is gone: hide it for good
        __try {
            ULONG_PTR m = *(ULONG_PTR *)(g_base + 0x533c58);
            for (int k = 0; m && k < 16; k++) {
                short rows = *(short *)(m + 0x40 + (ULONG_PTR)k * 0x103c + 0x16);
                if (rows > 0 && rows < 64) { g_th_popup = 4; return; }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 500) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    DWORD accent = 0xFF80E0D0;
    if (g_th_on >= 0) accent = th_accent();                       // v5.79.2 (4th of July: blue on the navy panel)
    auto box = [&](int px, int py, DWORD col, int w, int h) { *space = 0; *gap = 0; *fh = h; *fw = w; set_rgba(rgba, col); text(px, py, "\x81\xa1"); };
    auto put = [&](int px, int py, const char *s, DWORD col) { *space = 0; *gap = 2; *fh = 14; *fw = 7; set_rgba(rgba, 0xFF000000); text(px + 1, py + 1, s); set_rgba(rgba, col); text(px, py, s); };
    const int LH = 20;
    if (g_th_popup == 1) {
        // v5.174 (owner 2026-11-13; in game the panel covered the game's own PRESS ENTER TO START at
        // y ~330 and left an empty line without a holiday): below the copyright line, as tall as its lines
        const int X = 110, Y = 410, NL = g_th_on >= 0 ? 3 : 2, BOT = Y + LH * NL + 2;   // 410: clear of the copyright line (y ~383)
        for (int by = Y - 8; by < BOT; by += 48)
            for (int bx = X - 12; bx < X + 420; bx += 48) box(bx, by, 0xE8061614, 48, by + 64 > BOT ? BOT - by : 64);
        if (g_th_on >= 0) {
            const Holiday &h = HOLIDAYS[g_th_on];
            static const char *MON[13] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            char l2[96];
            put(X, Y, h.greet, accent);
            if (g_th_forced) sprintf_s(l2, "The %s theme is on (CwoTheme.txt).", h.name);
            else sprintf_s(l2, "The %s theme is on until %s %d.", h.name, MON[g_th_until_m >= 1 && g_th_until_m <= 12 ? g_th_until_m : 0], g_th_until_d);
            put(X, Y + LH, l2, 0xFFFFFFFF);
        } else put(X, Y, g_bfr_edition ? "Battle for Reality" : "Phantom Dust MODS", accent);   // v5.174 the edition it is
        put(X, Y + (NL - 1) * LH, "What's new: see MOD LIST on the main menu", 0xFFC8C8C8);   // v5.86
    } else {                                                      // centred window: 8 rows, a scroll bar, OK at the bottom
        const int W = 440, X = (640 - W) / 2, Y = 110, H = 30 + LH * TH_ROWS + 34;
        for (int by = Y - 10; by < Y + H; by += 48)
            for (int bx = X - 12; bx < X + W; bx += 48) box(bx, by, 0xF0061614, 48, 64);
        int n = th_news_n(), maxs = n - TH_ROWS; if (maxs < 0) maxs = 0;
        put(X, Y, "WHAT'S NEW IN THE MODS", accent);
        for (int r = 0; r < TH_ROWS && g_th_scroll + r < n; r++) {
            const char *t = th_news()[g_th_scroll + r];
            put(X, Y + 26 + LH * r, t, t[0] == '-' || t[0] == ' ' ? 0xFFFFFFFF : accent);
        }
        int bar_h = LH * TH_ROWS, knob = maxs ? bar_h * TH_ROWS / n : bar_h, ky = maxs ? (bar_h - knob) * g_th_scroll / maxs : 0;
        box(X + W - 14, Y + 26, 0xFF2A3A3A, 6, bar_h);
        box(X + W - 14, Y + 26 + ky, accent, 6, knob);
        put(X, Y + 30 + LH * TH_ROWS, g_th_scroll >= maxs ? "A: OK" : "Down: scroll to the end", g_th_scroll >= maxs ? accent : 0xFFC8C8C8);
    }
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}
static int th_self_test() {                                      // the date windows
    int um, ud;
    bool xmas = th_pick(2026, 12, 25, &um, &ud) == 7 && um == 12 && ud == 26;
    bool hallo = th_pick(2026, 10, 31, &um, &ud) == 5 && um == 11 && ud == 1;
    bool ny = th_pick(2027, 1, 1, &um, &ud) == 0 && um == 1 && ud == 2;
    bool none = th_pick(2026, 6, 10, &um, &ud) == -1;
    int em, ed; th_easter(2027, &em, &ed);
    bool easter = em == 3 && ed == 28;                           // Easter 2027: 28 March
    bool tg = th_pick(2026, 11, 26, &um, &ud) == 6;              // Thanksgiving 2026: 26 November
    logf("self-test: holiday themes Christmas %s, Halloween %s, New Year %s, none %s, Easter %s, Thanksgiving %s", xmas ? "ok" : "FAILED",
         hallo ? "ok" : "FAILED", ny ? "ok" : "FAILED", none ? "ok" : "FAILED", easter ? "ok" : "FAILED", tg ? "ok" : "FAILED");
    return xmas && hallo && ny && none && easter && tg ? 1 : -80;
}

// v5.74.1 the menus' cloud background (owner):
// it is the first 512 x 512 DXT1 texture of Menu\StMenu.alr (teal clouds; the grid and scan lines are other textures).
// While a holiday is on, the game's reads of Menu\StMenu.alr are served a copy with that texture in the holiday's colour
// (each texel: the colour with the texel's own shading, co_tint), built once per holiday into <data>\holiday\<name>\.
static bool th_menu_path(LPCWSTR f, wchar_t *out) {
    if (!TH_COLOURS || g_th_on < 0 || !f) return false;          // v5.202.0 the menu clouds keep their own colour
    size_t n = wcslen(f);
    static const wchar_t tail[] = L"menu\\stmenu.alr";
    const size_t t = sizeof(tail) / sizeof(wchar_t) - 1;
    if (n < t) return false;
    wchar_t low[32]; size_t k;
    for (k = 0; k < t; k++) { wchar_t c = towlower(f[n - t + k]); low[k] = c == L'/' ? L'\\' : c; }
    low[k] = 0;
    if (wcscmp(low, tail)) return false;
    wchar_t dir[MAX_PATH], name[48];
    MultiByteToWideChar(CP_UTF8, 0, HOLIDAYS[g_th_on].name, -1, name, 48);
    for (wchar_t *c = name; *c; c++) if (wcschr(L"\\/:*?\"<>|' .", *c)) *c = L'_';
    swprintf_s(dir, L"%sholiday", g_dir); CreateDirectoryW(dir, NULL);
    swprintf_s(dir, L"%sholiday\\%s", g_dir, name); CreateDirectoryW(dir, NULL);
    swprintf_s(out, MAX_PATH, L"%s\\StMenu.alr", dir);
    if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return true;
    wchar_t src[MAX_PATH];
    if (!sm_redirect(f, src, MAX_PATH, false)) wcscpy_s(src, f);
    Bytes b;
    if (!read_all(src, b)) { logf("holiday theme: StMenu.alr not readable"); return false; }
    Alr m;
    int ti, w, h, mips; unsigned toff; size_t size;
    if (!alr_parse(b, m) || !co_tex(m, ti, toff, w, h, mips, size)) { logf("holiday theme: StMenu.alr has no 512 x 512 DXT1 texture"); return false; }
    Bytes res = m.resource;
    res.insert(res.end(), m.trailing.begin(), m.trailing.end());
    if (toff + size > res.size()) return false;
    std::vector<Px> px;
    th_decode(&res[toff], w, h, px);
    const Holiday &hd = HOLIDAYS[g_th_on];
    int rgb[3] = {hd.bgr[2], hd.bgr[1], hd.bgr[0]};
    Bytes data;
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4) {
            int blk[16][3];
            for (int i = 0; i < 16; i++) {
                const Px &p = px[(size_t)(by + i / 4) * w + bx + i % 4];
                int c[3] = {p.r, p.g, p.b};
                co_tint(rgb, th_lum(c[0], c[1], c[2]), c);
                for (int q = 0; q < 3; q++) blk[i][q] = c[q];
            }
            bool clear[16];
            th_clear(&res[toff + data.size()], clear);
            unsigned char b8[8];
            th_block(blk, clear, b8);
            data.insert(data.end(), b8, b8 + 8);
        }
    size_t ro = rd32(&b[0x0C]);
    memcpy(&b[ro + toff], data.data(), (std::min)(data.size(), size));
    bool ok = pf_write(out, b.data(), (DWORD)b.size());
    logf("holiday theme: %s menu background %s", hd.name, ok ? "built" : "NOT written");
    return ok;
}

// v5.76 (owner 2026-11-02): the split-screen grid skips its
// locked created-character cell (slot 3, top right); while the grid is on screen (op 77 asked this frame) the loader
// writes COMING SOON there. Cell from the screenshots: 640 x 480 units x 423-523, y 80-168.
static ULONGLONG g_sel_seen;
static bool g_sel_mon;                                          // v5.96 the split-screen grid shows the monster page                                     // the last frame the split-screen grid asked op 77
static void sel_draw() {
    if (GetTickCount64() - g_sel_seen > 150) return;
    if (GetTickCount64() - g_so_seen < 200) return;   // v5.105 (owner 2026-11-04): not while a COLORS list is open
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 200) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    *space = 4; *gap = 4; *fh = 18; *fw = 10;                    // (in game 14:50: x 440 drew ~46 units left of the cell centre)
    auto mono = [&](int x, int y, const char *m) { char one[2] = {0, 0}; for (int i = 0; m[i]; i++) { one[0] = m[i]; text(x, y, one); x += m[i] == 'I' ? 7 : 14; } };
    if (g_npc_page && !g_sel_mon) {                               // v5.96 the players page: the NPC cell
        if (g_bfr_edition) { set_rgba(rgba, 0xFF000000); mono(514, 119, "NPC"); set_rgba(rgba, 0xFFE6D296); mono(513, 118, "NPC"); }   // v5.185.1 BFR keeps NPC (EXTRAS is MODS-only)
        else { set_rgba(rgba, 0xFF000000); mono(493, 119, "EXTRAS"); set_rgba(rgba, 0xFFE6D296); mono(492, 118, "EXTRAS"); }   // v5.182 the NPC cell is EXTRAS (owner 2026-11-14)
        memcpy(rgba, s_rgba, sizeof(s_rgba)); *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
        return;
    }
    if (g_bfr_edition) {                                          // v5.185.1 BFR keeps COMING SOON (EXTRAS is MODS-only)
        set_rgba(rgba, 0xFF000000); mono(486, 107, "COMING"); mono(497, 131, "SOON");
        set_rgba(rgba, 0xFFE6D296); mono(485, 106, "COMING"); mono(496, 130, "SOON");
    } else {
        set_rgba(rgba, 0xFF000000); mono(493, 119, "EXTRAS");   // v5.182 the monster page's cell is EXTRAS (owner 2026-11-14; its list comes with the first new enemy)
        set_rgba(rgba, 0xFFE6D296); mono(492, 118, "EXTRAS");
    }
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}
