// ---- v5.32 CREATE A STAGE in the game's own windows (owner 2026-10-27; "I wanted it
// to pretty much do the same thing that it's supposed to do in a multiplayer offline and online lobby where you go to select
// the level and it shows that level in the background fully rendered with the camera slowly rotating"; "when I select 00
// dummy map, it's completely pitch black").
// The page (mainmenu EDPG, tools/build_create_stage_page.py: aedgr_s0 asks op 51 first; the page's draw asks op 61 whether
// to draw the menu background) lists:
//   - Default template: the row of stage 20, "Edgar's Dream Stage Creator" (Map\st20.forge: an open floor with 223 objects of
//     every stage - Edgar's Lab, Highway, Palace, Panorama, Dawn City, Refinery, Sein - in storage under it);
//   - every stage of the game's stage list in number order, "Name (NN)": an empty copy (its objects are put away when the
//     editor starts: F_CLEAR_ALL; every one of them can be placed again);
//   - the Map Slot 7 canvases of Map\mx_list.txt (the piece sets and stage packs), "(10)".
// The "00 DUMMY" row is left out: it is stage 1 with no lighting variant (stageset row 0: variants 0), so it drew black;
// the "Map Slot" rows are the creators' own slots. The highlighted row's stage is loaded behind the page by the stage view
// (cwo_stageview.inl, as the LEVEL LIST), with the menu background left out while it shows. A on a row: the loader goes into
// Single Play with the screen black and presses through its lists (stage, the first arsenal, 'debug'), and the editor opens
// on that stage. Facts: Single Play's stage list rows 0x509ba0 (0x28 each: +0 name, +0x18 stage, +0x1c variants, count
// 0x50a5d0), its cursor *0x140509b88 {+0 row, +0xC variant} (FUN_140192780), the stage file table 0x1403f2b60.
struct CsEnt { int kind, row, mx, stage; char name[48]; };     // kind 0 default template, 1 stage template, 2 Map Slot 7 canvas
static CsEnt g_cs[160];
static int g_cs_n, g_cs_row;                                   // the cursor (g_cs_go: cwo_forge.inl)
static int g_cs_slot7_row = -1;

static void cs_name_clean(const char *in, char *out, size_t n) {   // one space between words, none at the ends
    size_t k = 0;
    bool sp = false;
    for (const char *p = in; *p && k + 1 < n; p++) {
        if (*p == ' ') { sp = k > 0; continue; }
        if (sp && k + 2 < n) out[k++] = ' ';
        sp = false;
        out[k++] = *p;
    }
    out[k] = 0;
}
static void cs_build_list() {
    g_cs_n = 0;
    g_cs_slot7_row = -1;
    int def_row = -1;
    struct R { int row, stage; char name[48]; } rows[96];
    int nr = 0;
    __try {
        int n = *(int *)(g_base + RVA_LV_ROWS);
        for (int i = 0; i < n && i < 96; i++) {
            char *row = (char *)(g_base + RVA_LV_LIST + 0x28 * i);
            const char *nm = *(const char **)row;
            int st = *(int *)(row + 0x18), var = *(int *)(row + 0x1c);
            if (!nm || !nm[0] || (unsigned char)nm[0] >= 0x80 || st <= 0) continue;
            char up[48] = "";
            for (int c = 0; c < 47 && nm[c]; c++) up[c] = (char)toupper((unsigned char)nm[c]);
            if (st == LIST_STAGE && g_cs_slot7_row < 0) g_cs_slot7_row = i;
            if (st == 20 && def_row < 0) { def_row = i; continue; }
            if (strstr(up, "DUMMY") || !strncmp(up, "MAP SLOT", 8) || var < 1) continue;
            if (lv_stage_of(i) < 0) continue;                   // its files are not there
            rows[nr].row = i; rows[nr].stage = st;
            cs_name_clean(nm, rows[nr].name, sizeof(rows[nr].name));
            nr++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    for (int a = 1; a < nr; a++) { R t = rows[a]; int b = a - 1; while (b >= 0 && (rows[b].stage > t.stage || (rows[b].stage == t.stage && rows[b].row > t.row))) { rows[b + 1] = rows[b]; b--; } rows[b + 1] = t; }
    if (def_row >= 0) { CsEnt &e = g_cs[g_cs_n++]; e = {0, def_row, -1, 20, ""}; strcpy_s(e.name, "Default template: all objects (20)"); }
    for (int i = 0; i < nr && g_cs_n < 150; i++) {
        CsEnt &e = g_cs[g_cs_n++];
        e = {1, rows[i].row, -1, rows[i].stage, ""};
        sprintf_s(e.name, "%.32s (%02d)", rows[i].name, rows[i].stage);
    }
    for (int k = 0; k < g_mx_count && g_cs_n < 160 && g_cs_slot7_row >= 0; k++) {
        CsEnt &e = g_cs[g_cs_n++];
        e = {2, g_cs_slot7_row, k, LIST_STAGE, ""};
        char nm[48];
        cs_name_clean(g_mx[k].name, nm, sizeof(nm));
        sprintf_s(e.name, "%.32s (%02d)", nm, LIST_STAGE);
    }
    if (g_cs_row >= g_cs_n) g_cs_row = 0;
}

static bool cs_back_row() { return g_cs_row >= g_cs_n; }
static void cs_reset_list() { g_cs_n = 0; cs_build_list(); }

static void cs_input(WORD pressed) {
    if (!g_cs_n) cs_build_list();
    if (pressed & BTN_B) { InterlockedExchange(&g_ed_page, EDP_CHOOSER); return; }
    if (!g_cs_n) return;
    if (pressed & BTN_DOWN) g_cs_row = (g_cs_row + 1) % (g_cs_n + 1);
    if (pressed & BTN_UP) g_cs_row = (g_cs_row + g_cs_n) % (g_cs_n + 1);
    // v5.36 (owner, back on this page after leaving the editor: "when you select on any of them, it doesn't load up"): A on
    // the loader's own page (Single Play's list behind it) had no handler - only the game's window's A (op 54) built. Now
    // it goes the same way: the chooser's hand-over (g_ed_auto_choose), then the loader's presses through the lists
    if (pressed & BTN_A) {
        if (cs_back_row()) { InterlockedExchange(&g_ed_page, EDP_CHOOSER); return; }
        if (cs_build()) { InterlockedExchange(&g_ed_auto_choose, 1); rumble(1); }
    }
}
static const char *CS_H[] = {"CREATE A STAGE - how to use", "Up / Down: a stage - it is drawn behind the list",
                             "Default template: an open floor with every stage's objects", "  in storage under it, ready to place",
                             "A stage: an empty copy of it - its objects are put away,", "  every one of them can be placed again",
                             "A: build on it   B: back", "Pieces: as many as the stage's memory takes", NULL};
template <class PUT, class BOX> static void cs_draw(PUT &put, BOX &box, int x, int y) {
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF;
    const int LINE = 16;
    (void)box;
    if (!g_cs_n) cs_build_list();
    if (g_ed_help) {
        put(x, y, CS_H[0], GOLD);
        y += LINE + 4;
        for (int i = 1; CS_H[i]; i++, y += LINE) put(x, y, CS_H[i], WHITE);
        put(x, y + 4, "Y / A / B: close this help", BLUE);
        return;
    }
    put(x, y, "CREATE A STAGE", GOLD);
    y += LINE + 4;
    char line[96];
    // v5.36 (owner's screenshot: the list "stops at Underground"): on the screen (after leaving the editor) the page
    // shows 20 rows around the cursor, with "..." where more follow; the game's window reads every row and scrolls itself
    // v5.117.3 (owner test 2026-11-08: A on RoofTop Day and every row past the first 64 did nothing; seen live 11:0x: the
    // loader's cursor was right (row 119) but the menu script has row handlers aedpg_s0 .. s63 only (build_ed_pages.py ROWS),
    // so a row >= 64 never called op 54): the game's window gets at most 60 rows, a window around the cursor
    int rows = g_cs_n + 1, win = g_epg_capturing ? (rows < 60 ? rows : 60) : 20;
    int top = g_cs_row - win / 2;
    if (top > rows - win) top = rows - win;
    if (top < 0) top = 0;
    if (top > 0 && !g_epg_capturing) { put(x, y, "  ...", WHITE); y += LINE; }
    for (int i = top; i < rows && i < top + win; i++, y += LINE) {
        sprintf_s(line, "%s%s", i == g_cs_row ? "> " : "  ", i < g_cs_n ? g_cs[i].name : "Back");
        put(x, y, line, i == g_cs_row ? GOLD : WHITE);
    }
    if (top + win < rows && !g_epg_capturing) { put(x, y, "  ...", WHITE); y += LINE; }
    const char *note = g_cs_row >= g_cs_n ? "Back to Edgar's Dream" :
                       g_cs[g_cs_row].kind == 0 ? "An open floor with every stage's objects in storage, ready to place" :
                       g_cs[g_cs_row].kind == 1 ? "This stage, empty: its objects are put away; place any of them again" :
                                                  "A Map Slot 7 canvas: its own pieces in storage under the floor";
    put(x, y + 4, note, WHITE);
    put(x, y + 4 + LINE, "Up / Down: a stage   A: build on it   B: back", BLUE);
}

// the page's frame (op 53): the highlighted entry's stage behind the list
static void cs_frame() {
    if (!g_cs_n) cs_build_list();
    if (g_cs_row >= g_cs_n) { g_lv_seen = g_lv_clock; if (g_lv_want != -1) { g_lv_want = -1; g_lv_rest = 0; } lv_step(true); return; }
    const CsEnt &e = g_cs[g_cs_row];
    if (e.kind == 2) {                                          // a canvas: Map Slot 7 has to hold it (when no stage is up)
        if (g_mx_cur != e.mx) {
            if (g_lv_phase == LV_SHOWN && g_lv_row == e.row) InterlockedExchange(&g_lv_reload, 1);
            if (g_lv_phase == LV_IDLE) mx_apply(e.mx);
        }
    }
    g_lv_seen = g_lv_clock;
    if (e.row != g_lv_want) { g_lv_want = e.row; g_lv_rest = 0; } else if (g_lv_rest < 1000) g_lv_rest++;
    if (e.kind == 2 && g_mx_cur != e.mx && g_lv_phase == LV_IDLE) return;   // the canvas first
    lv_step(true);
    if (lv_showing()) lv_camera();
}

// A on a row (op 54): into Single Play for that stage; returns true when it goes
static bool cs_build() {
    if (!g_cs_n || g_cs_row >= g_cs_n) return false;
    g_cs_go = g_cs_row;
    InterlockedExchange(&g_cs_empty_pending, g_cs[g_cs_row].kind == 1 ? 1 : 0);
    InterlockedExchange(&g_ed_direct, 0);                       // the session starts as Create a Stage (no chooser)
    // v5.33 (owner): the
    // session's first step is the chooser's A on entry 0 (edm_session_start), which only happens on the chooser page -
    // left on this page, the A went to cs_input and the hand-over never began
    InterlockedExchange(&g_ed_page, EDP_CHOOSER);
    InterlockedExchange(&g_ed_sel, 0);
    InterlockedExchange(&g_cs_hold_cover, 1);                   // black until the editor is on
    logf("Create a Stage: '%s' (row %d, kind %d) - into Single Play", g_cs[g_cs_row].name, g_cs[g_cs_row].row, g_cs[g_cs_row].kind);
    return true;
}

// the loader's presses through Single Play's lists (forge_input, worker thread): g_ed_auto 31 .. 35
static bool cs_auto_step(LONG st, ULONGLONG now) {
    static ULONGLONG t;
    LONG au = g_ed_auto;
    if (au < 31 || au > 35) return false;
    if (au == 31) { t = now; InterlockedExchange(&g_ed_auto, 32); return true; }
    if (au == 32 && st == 0 && now - t > 700) {
        if (g_cs_go >= 0 && g_cs_go < g_cs_n) {
            const CsEnt &e = g_cs[g_cs_go];
            __try {
                if (e.kind == 2) { mx_apply(e.mx); f_cursor_slot7(); }
                else { int *cur = *(int **)(g_base + 0x509b88); if (cur) { cur[0] = e.row; cur[3] = 0; } }
                ULONG_PTR m = f_mode_obj(), so = m ? *(ULONG_PTR *)(m + 0x60) : 0;
                if (so) *(int *)(so + 0x10) = 0;                // FUN_140192780: +0x10 set = a random stage instead of the row
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        f_press(0x4, 150); t = now; InterlockedExchange(&g_ed_auto, 33); return true;
    }
    // v5.188.48 (owner 2026-11-17): one A per list, never again - a lost press left the black cover
    // up for good. Now a list still showing 2.5 s after its press gets A again, and a list the game (or the player) already
    // passed is picked up where the game is (state 0 stage list, 2 deck list, 3 battle list, SP_MATCH the match)
    static ULONGLONG s_start;
    if (au == 32 && !s_start) s_start = now;
    if (au >= 32 && au <= 34 && st == 3) { if (au < 34) { t = now - 701; InterlockedExchange(&g_ed_auto, 34); au = 34; logf("Create a Stage: the battle list is up - on from there"); } }
    else if (au == 32 && st == 2) { t = now - 701; InterlockedExchange(&g_ed_auto, 33); au = 33; logf("Create a Stage: the deck list is up - on from there"); }
    static ULONGLONG s_retry;
    if (au == 33 && st == 0 && now - t > 2500 && now - s_retry > 2500) { f_press(0x4, 150); s_retry = now; logf("Create a Stage: the stage list is still up - A again"); return true; }
    if (au == 34 && st == 2 && now - t > 2500 && now - s_retry > 2500) { f_press(0x4, 150); s_retry = now; logf("Create a Stage: the deck list is still up - A again"); return true; }
    if (au == 35 && st == 3 && now - t > 2500 && now - s_retry > 2500) { f_press(0x4, 150); s_retry = now; logf("Create a Stage: the battle list is still up - A again"); return true; }
    if (au == 33 && st == 2 && now - t > 700) { f_press(0x4, 150); t = now; InterlockedExchange(&g_ed_auto, 34); return true; }   // the first arsenal
    if (au == 34 && st == 3 && now - t > 700) { f_press(0x4, 150); t = now; InterlockedExchange(&g_ed_auto, 35); return true; }   // 'debug'
    if (st == SP_MATCH && au >= 33) { InterlockedExchange(&g_ed_auto, 0); InterlockedExchange(&g_cs_hold_cover, 0); g_cs_go = -1; s_start = 0; return true; }
    if (s_start && now - s_start > 30000) s_start = 0, t = 0;          // the 30 s limit below counts from the start, not the last press
    if (now - t > 20000) {
        s_start = 0;
        InterlockedExchange(&g_ed_auto, 0);
        InterlockedExchange(&g_cs_hold_cover, 0);
        InterlockedExchange(&g_cs_empty_pending, 0);
        g_cs_go = -1;
        set_status("Could not open the stage on its own: pick it in the list");
        logf("Create a Stage: the lists did not come - left to the player");
    }
    return true;
}

static int cs_self_test() {                                     // names, order and the default row on a stand-in list
    char out[48];
    cs_name_clean("  dARK   Highway  ", out, sizeof(out));
    bool clean = !strcmp(out, "dARK Highway");
    cs_name_clean("Doom City    ", out, sizeof(out));
    clean &= !strcmp(out, "Doom City");
    logf("self-test: Create a Stage names %s", clean ? "ok" : "FAILED");
    return clean ? 1 : -77;
}
