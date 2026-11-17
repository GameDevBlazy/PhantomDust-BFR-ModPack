// ---- v4.1 the select screen's created-character cell (owner 2026-10-25): up to 6 created
// characters (charaset rows 28, 30, 31, 34, 35, 39, written by bake_edgar.py with CwoSlots.txt); on the Multiplayer
// character select the top-right cell switches between them with LB / RB, and their name is shown under the grid.
// Facts (our own reading of the patched Menu/vsmenu_en.ssb b6784d3f, build_select_slot.py; Ghidra full-pass batch_065):
//   the menu object *0x140533c58: page name at +0x10611 ("CSEL" / "CSEL2", native 2.76 -> FUN_140245e80); the running
//     script image = *(*(menu + 0x10608) + 0x10) (FUN_140248230 "InitMenu"); the script's own variables live in the image
//     (pushaddr 0xb4: variable 0 = the grid cursor, both pages).
//   in the image: 0x6bac portrait sprite per cell, 0x6c9c character per cell, 0x6ccc monster per cell (cell 3 = the top
//     right), 0x6d3c the drawing helper's answer for created characters (push16 0 = the game's dark "?" look, 1 = shown);
//     header +4 = 0x6db4. Sprites 21 + character are the players' faces.
// v4.3 (owner 2026-10-25, lobby photos): vsmenu b'74dcd4e6 (build_select_slot.py v4.3) adds 0x6db4 the lobby face per created
//   row (body + 64 = drawn faded), 0x6ef0 the rule table (12 stock + 8 created-mode rows of 48 bytes: +3 shown, +8 name),
//   'Change Rule' in the lobby options, and plays a created-mode rule (menu var 0 >= 12) on Standard Battle. The menu's own
//   variables: *0x140533c58 + 0x10430 + 8 * n (natives 2.153 / 2.154 = 0x140207690 / 0x140207670); var 0 = the rule.
//   A on the cell opens the loader's list of created characters; the choice is applied and A handed to the game, which
//   opens its COLORS window (charaset +0x3A = the outfits built for it).
static const ULONG_PTR RVA_MENU_OBJ = 0x533c58;
static const unsigned VS_TABLE_L3 = 0x87d8;                   // v5.3 vsmenu with the 3 VS 3 LOBBY (build_lobby3.py)
static const unsigned VS_TABLE_L3L = 0x87f8;                  // v5.3.1 the same in the Xbox Live lobby's layout (STYLE live)
static const unsigned VS_TABLE = 0x7bb8, VS_PORT = 0x6bac, VS_CHR = 0x6c9c, VS_MON = 0x6ccc, VS_DRAWV = 0x6d3c, VS_VAR0 = 0xb4,
                      VS_FACE = 0x6db4, VS_RULES = 0x6ef0, VS_MENU_VARS = 0x10430;
// v4.8 (vsmenu 4.8): per created rule row its game rule (VS_MBASE, 8 ints) and its description (VS_DESC, 8 x 256 bytes)
static const unsigned VS_MBASE = 0x7330, VS_DESC = 0x7350;   // vsmenu 77ca5416
static volatile LONG g_slot_drop_sel, g_slot_pick;           // the list: the row under the cursor; a choice (index + 1)
static char g_slot_hold[12];                                 // the page where A was handed to the game (its COLORS window)
static ULONG_PTR g_slot_last_sc;
static LONGLONG g_slot_last_rule = -1;
struct SlotChar { int row, body, face; char name[24]; };
static SlotChar g_slots[6];
static int g_slot_n = -1, g_slot_idx;
static volatile LONG g_slot_req;                               // LB / RB asked (-1 / +1); g_slot_on: cwo_loader.cpp
static ULONGLONG g_slot_read_t;

static void slot_load() {                                      // CwoSlots.txt (the install writes it)
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%sCwoSlots.txt", g_dir);
    FILE *f = NULL;
    g_slot_n = 0;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[128];
    while (g_slot_n < 6 && fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        SlotChar s = {};
        char *ctx = NULL, *t = strtok_s(line, "\t\r\n", &ctx);
        if (!t) continue;
        s.row = atoi(t);
        if ((t = strtok_s(NULL, "\t\r\n", &ctx)) != NULL) s.body = atoi(t);
        if ((t = strtok_s(NULL, "\t\r\n", &ctx)) != NULL) s.face = atoi(t);
        if ((t = strtok_s(NULL, "\t\r\n", &ctx)) != NULL) strncpy_s(s.name, t, _TRUNCATE);
        if (s.row >= 28 && s.row <= 39) g_slots[g_slot_n++] = s;
    }
    fclose(f);
}

static ULONG_PTR g_slot_test_menu;                            // self-test: a stand-in menu object
static int g_vs_d;                                            // v5.64.2 where our part of the script starts (0 or -36)
static ULONG_PTR vs_at(ULONG_PTR sc, unsigned off) { return sc + off + g_vs_d; }   // a table in our part
static ULONG_PTR slot_script(ULONG_PTR *menu_out) {             // the patched select script, or 0
    ULONG_PTR menu = g_slot_test_menu ? g_slot_test_menu : *(ULONG_PTR *)(g_base + RVA_MENU_OBJ);
    if (!menu) return 0;
    if (menu_out) *menu_out = menu;
    ULONG_PTR blk = *(ULONG_PTR *)(menu + 0x10608);
    if (!blk) return 0;
    ULONG_PTR sc = *(ULONG_PTR *)(blk + 0x10);
    if (!sc || *(unsigned *)sc != 0x20) return 0;
    // v5.64.2: the tables are found by their content. Later tools appended more code (the export table moved past the
    // three positions once listed here, so 5.18-5.64.1 never filled them), and the BFR pack's script is built on the clean
    // game file (tools/rebase_ssb.py), where our part starts 36 bytes earlier: g_vs_d = that shift (0 or -36).
    unsigned table = *(unsigned *)(sc + 4);
    for (int d = 0; d >= -36; d -= 36) {
        if (table < VS_DESC + 8 * 256 + d) continue;
        if (memcmp((const void *)(sc + VS_RULES + 8 + d), "Standard Battle", 16)) continue;
        const int *chr = (const int *)(sc + VS_CHR + d);
        if (chr[0] != 2 || chr[1] != 0 || chr[2] != 1 || chr[4] != 3 || chr[11] != 10 || chr[3] < 28 || chr[3] > 39) continue;
        g_vs_d = d;
        return sc;
    }
    return 0;
}

static void slot_faces(ULONG_PTR sc) {                       // v4.3 the lobby faces of the created rows
    int *face = (int *)(vs_at(sc, VS_FACE));
    for (int i = 0; i < 12; i++) face[i] = 64;               // faded Player face
    for (int k = 0; k < g_slot_n; k++) {
        const SlotChar &s = g_slots[k];
        if (s.row < 28 || s.row > 39) continue;
        int body = s.body >= 0 && s.body <= 10 ? s.body : 0;
        face[s.row - 28] = body + (s.face && s.body <= 10 ? 0 : 64);
    }
}

static void slot_rules(ULONG_PTR sc, ULONG_PTR menu) {        // v4.3 the created modes as rules 12.. of the rule page
    int n = g_edc[EDP_MODE].n < 8 ? g_edc[EDP_MODE].n : 8;
    for (int i = 0; i < 8; i++) {
        char *e = (char *)(vs_at(sc, VS_RULES) + 48 * (12 + i));
        char want[40] = {};
        if (i < n) sprintf_s(want, "%.38s", g_edc[EDP_MODE].item[i].name);
        else sprintf_s(want, "Created Mode %d", i + 1);
        if (memcmp(e + 8, want, 40)) memcpy(e + 8, want, 40);
        char shown = i < n && !ed_online() ? 1 : 0;
        if (e[3] != shown) e[3] = shown;
        if (i < n && VS_MBASE && VS_DESC) {                     // v4.8 its game rule, its arsenal limit and its description
            const EdItem &m = g_edc[EDP_MODE].item[i];
            int rule = ed_mode_rule(m);
            ((int *)(vs_at(sc, VS_MBASE)))[i] = rule;
            const char *base = (const char *)(vs_at(sc, VS_RULES) + 48 * rule);
            if (memcmp(e, base, 3) || e[4] != base[4] || e[5] != base[5] || e[6] != base[6] || e[7] != base[7]) {
                memcpy(e, base, 3); memcpy(e + 4, base + 4, 4);
            }
            char l1[160], l2[160], l3[160], d[256];
            ed_mode_desc(m, l1, l2, l3, sizeof(l1));
            sprintf_s(d, "%.80s\n%.80s\n%.80s", l1, l2, l3);
            char *dst = (char *)(vs_at(sc, VS_DESC) + 256 * i);
            if (strcmp(dst, d)) strcpy_s(dst, 256, d);
        }
    }
    LONGLONG *var0 = (LONGLONG *)(menu + VS_MENU_VARS);
    if (sc != g_slot_last_sc) {                              // a fresh select script: the mode in use is its rule
        g_slot_last_sc = sc;
        LONG on = g_ed_mode_on;
        if (on >= 0 && on < n && *var0 != 12 + on) { *var0 = 12 + on; logf("lobby: rule shown as mode '%s'", g_edc[EDP_MODE].item[on].name); }
        else if ((on < 0 || on >= n) && *var0 >= 12) { *var0 = 0; logf("lobby: no mode in use - rule back to Standard Battle"); }   // v4.9.1 (owner tests 57 / 61)
        g_slot_last_rule = *var0;
        return;
    }
    LONGLONG r = *var0;
    if (r == g_slot_last_rule) return;
    g_slot_last_rule = r;
    if (r >= 12 && r < 12 + n) { if (g_ed_mode_on != r - 12) ed_mode_arm((int)(r - 12)); }
    else if (r >= 0 && r < 12 && g_ed_mode_on >= 0) ed_mode_arm(-1);   // a game rule chosen: the game's own rules
    logf("lobby: rule %lld chosen", r);
}

static void slot_apply(ULONG_PTR sc) {
    const SlotChar &s = g_slots[g_slot_idx];
    bool face = s.face && s.body >= 0 && s.body <= 10;           // an NPC body has no face of its own here
    ((int *)(vs_at(sc, VS_CHR)))[3] = s.row;
    ((int *)(vs_at(sc, VS_MON)))[3] = s.row;
    ((int *)(vs_at(sc, VS_PORT)))[3] = face ? 21 + s.body : 21;
    *(unsigned *)(vs_at(sc, VS_DRAWV)) = 0x0E | (face ? 1u : 0u) << 16;   // push16 1 / push16 0
}

static void slot_step() {                                      // game thread, every frame
    if (g_in_match || g_forge_session) { g_slot_on = 0; return; }
    __try {
        ULONG_PTR menu = 0, sc = slot_script(&menu);
        ULONGLONG now = GetTickCount64();
        if (sc && !g_slot_test_menu && (g_slot_n < 0 || now - g_slot_read_t > 5000)) { slot_load(); g_slot_read_t = now; }
        if (sc) { slot_faces(sc); if (!g_slot_test_menu) slot_rules(sc, menu); }   // v4.3 every page of the script
        const char *pg = sc ? (const char *)(menu + 0x10611) : "";
        if (g_slot_hold[0] && strncmp(pg, g_slot_hold, sizeof(g_slot_hold))) g_slot_hold[0] = 0;   // past its COLORS window
        bool page = sc && !strncmp(pg, "CSEL", 4);
        if (!page) { InterlockedExchange(&g_slot_on, 0); InterlockedExchange(&g_slot_drop, 0); return; }
        if (g_slot_n <= 0) { InterlockedExchange(&g_slot_on, 0); return; }
        if (g_slot_idx >= g_slot_n) g_slot_idx = 0;
        int cur = ((int *)(vs_at(sc, VS_CHR)))[3];
        if (cur != g_slots[g_slot_idx].row) {                   // a fresh script load (cell 3 = 28 again) or another start
            g_slot_idx = 0;
            for (int i = 0; i < g_slot_n; i++) if (g_slots[i].row == cur) g_slot_idx = i;
            slot_apply(sc);
        }
        int cursor = *(int *)(sc + VS_VAR0);
        InterlockedExchange(&g_slot_on, cursor == 3 && !g_slot_hold[0] ? 1 : 0);
        if (!g_slot_on) InterlockedExchange(&g_slot_drop, 0);
        LONG pick = InterlockedExchange(&g_slot_pick, 0);   // v4.3 a character chosen in the list: on the cell, then A
        if (pick > 0 && pick <= g_slot_n && cursor == 3) {
            g_slot_idx = pick - 1;
            slot_apply(sc);
            strncpy_s(g_slot_hold, pg, _TRUNCATE);
            InterlockedExchange(&g_slot_on, 0);
            InterlockedExchange64(&g_slot_pass_until, (LONGLONG)(now + 500));
            if (!g_slot_test_menu) f_press(0x4, 150);              // A for the game (WinRT GamepadButtons.A)
            logf("select slot: %s chosen from the list (row %d)", g_slots[g_slot_idx].name, g_slots[g_slot_idx].row);
        }
        LONG r = InterlockedExchange(&g_slot_req, 0);
        if (r && cursor == 3 && g_slot_n > 1) {
            g_slot_idx = (g_slot_idx + (r > 0 ? 1 : g_slot_n - 1)) % g_slot_n;
            slot_apply(sc);
            logf("select slot: %d/%d %s (row %d)", g_slot_idx + 1, g_slot_n, g_slots[g_slot_idx].name, g_slots[g_slot_idx].row);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { InterlockedExchange(&g_slot_on, 0); }
}

static void slot_draw() {                                      // the name under the grid while the cursor is on the cell
    if ((!g_slot_on && !g_slot_drop) || g_slot_n <= 0) return;
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 120) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    *space = 0; *gap = 2; *fh = 14; *fw = 7;
    char line[96];
    if (g_slot_drop) {                                       // v4.3 the list of created characters
        int x = 400, y = 150;
        set_rgba(rgba, 0xFF000000); text(x + 1, y + 1, "CREATED CHARACTERS");
        set_rgba(rgba, 0xFFFFD040); text(x, y, "CREATED CHARACTERS");
        for (int k = 0; k < g_slot_n; k++) {
            sprintf_s(line, "%s %d  %s", k == g_slot_drop_sel ? ">" : " ", k + 1, g_slots[k].name);
            set_rgba(rgba, 0xFF000000); text(x + 1, y + 21 + 18 * k, line);
            set_rgba(rgba, k == g_slot_drop_sel ? 0xFFFFD040 : 0xFFFFFFFF); text(x, y + 20 + 18 * k, line);
        }
        set_rgba(rgba, 0xFF000000); text(x + 1, y + 29 + 18 * g_slot_n, "A choose   B back");
        set_rgba(rgba, 0xFFA0E0FF); text(x, y + 28 + 18 * g_slot_n, "A choose   B back");
    } else {
        if (g_slot_n > 1) sprintf_s(line, "CREATED CHARACTER %d/%d: %s     A: list   LB / RB: switch", g_slot_idx + 1, g_slot_n,
                                    g_slots[g_slot_idx].name);
        else sprintf_s(line, "CREATED CHARACTER: %s     A: choose", g_slots[g_slot_idx].name);
        set_rgba(rgba, 0xFF000000); text(181, 405, line);
        set_rgba(rgba, 0xFFFFD040); text(180, 404, line);
    }
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}

static int slot_self_test() {                                  // a stand-in menu + script image: page, cursor, LB / RB
    static unsigned char menu[0x10700], blk[0x20], sc[0x8000];
    memset(menu, 0, sizeof(menu)); memset(blk, 0, sizeof(blk)); memset(sc, 0, sizeof(sc));
    const int td = -36;                                        // v5.64.2: the clean-based layout (found by content)
    *(ULONG_PTR *)(menu + 0x10608) = (ULONG_PTR)blk;
    *(ULONG_PTR *)(blk + 0x10) = (ULONG_PTR)sc;
    *(unsigned *)sc = 0x20; *(unsigned *)(sc + 4) = VS_TABLE;
    memcpy((sc + VS_RULES + td) + 8, "Standard Battle", 16);
    static const int chr[12] = {2, 0, 1, 28, 3, 4, 5, 6, 7, 8, 9, 10}, port[12] = {23, 21, 22, 21, 24, 25, 26, 27, 28, 29, 30, 31};
    memcpy((sc + VS_CHR + td), chr, sizeof(chr)); memcpy((sc + VS_MON + td), chr, sizeof(chr)); memcpy((sc + VS_PORT + td), port, sizeof(port));
    *(unsigned *)((sc + VS_DRAWV + td)) = 0x0E;
    strcpy_s((char *)(menu + 0x10611), 11, "CSEL");
    *(int *)(sc + VS_VAR0) = 3;
    g_slot_test_menu = (ULONG_PTR)menu;
    g_slot_n = 3; g_slot_idx = 0;
    g_slots[0] = {28, 3, 0, "BLAZY"}; g_slots[1] = {30, 1, 1, "EDGAR SHADOW"}; g_slots[2] = {31, 14, 1, "MAC RUNNER"};
    LONG s_match = g_in_match; g_in_match = 0;
    slot_step();
    bool on = g_slot_on == 1;
    g_slot_req = 1; slot_step();
    const int *c = (const int *)((sc + VS_CHR + td));
    bool next = c[3] == 30 && ((int *)((sc + VS_MON + td)))[3] == 30 && ((int *)((sc + VS_PORT + td)))[3] == 22 && *(unsigned *)((sc + VS_DRAWV + td)) == 0x1000E;
    const int *fc = (const int *)((sc + VS_FACE + td));
    next = next && fc[0] == 3 + 64 && fc[2] == 1 && fc[3] == 64 + 0 && fc[1] == 64;   // v4.3 lobby faces: faded / shown / NPC
    g_slot_pick = 3; slot_step();                              // v4.3 the list: the third one, then A is held back
    next = next && c[3] == 31 && g_slot_hold[0] && g_slot_on == 0;
    strcpy_s((char *)(menu + 0x10611), 11, "ASEL"); slot_step(); strcpy_s((char *)(menu + 0x10611), 11, "CSEL"); slot_step();
    next = next && !g_slot_hold[0] && g_slot_on == 1;
    g_slot_req = -1; slot_step();                              // back to the second for the checks below
    g_slot_req = 1; slot_step();
    bool npc = c[3] == 31 && ((int *)((sc + VS_PORT + td)))[3] == 21 && *(unsigned *)((sc + VS_DRAWV + td)) == 0x0E;   // NPC body: no face
    g_slot_req = -1; slot_step(); g_slot_req = -1; slot_step();
    bool back = c[3] == 28 && g_slot_idx == 0;
    *(int *)(sc + VS_VAR0) = 5; slot_step();
    bool off = g_slot_on == 0;
    strcpy_s((char *)(menu + 0x10611), 11, "ASEL"); *(int *)(sc + VS_VAR0) = 3; slot_step();
    bool page = g_slot_on == 0;
    g_slot_test_menu = 0; g_slot_n = -1; g_slot_idx = 0; g_in_match = s_match; g_slot_on = 0;
    logf("self-test: select slots on %s, next %s, NPC face %s, back %s, cursor off %s, other page %s", on ? "ok" : "FAILED",
         next ? "ok" : "FAILED", npc ? "ok" : "FAILED", back ? "ok" : "FAILED", off ? "ok" : "FAILED", page ? "ok" : "FAILED");
    return on && next && npc && back && off && page ? 1 : -33;
}
