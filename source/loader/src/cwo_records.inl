// ---- v5.21 Records (owner 2026-10-27; "go ahead and work on adding all
// 8 of those ideas"). Edgar's Dream > Records: your offline matches - wins, losses and draws overall, by character and by
// mode, the current and best winning streak, and the skills you use most. Kept in CwoRecords.txt (loader data folder).
// Online and scoreboard matches are never counted (the loader stays out of them).
// Game facts (Ghidra batch_065, pcode.py; recorded 2026-10-26 for the Xbox Live session W/L, cwo_livesess.inl):
//  - FUN_14012c8c0 writes each fighter's result at the end: 0x1404bc5c0[i] = 0 its side won, 1 / 2 lost, 3 no contest;
//    -1 while the match runs. *0x1404bbe00 = this machine's own fighter (FUN_140128990's second argument, 0 offline);
//  - the fighter's setup record (0x1404bc420 + 0x48 i): +8 the character word (outfit << 16 | charaset row);
//  - how the match started: *0x1404bc9d4 (FUN_140128990's third argument: 2 a quest, 4 online, 6 split screen) and
//    *0x1404bca04 (1 Single Play / offline, 2-3 online, 4 split screen, 5 Download Battle, 6 a story-started battle);
//  - every skill move starts in FUN_14025a3e0(action): skill *(action + 0x5c), its fighter *(action + 0xbd0) whose +0x14
//    is the fighter's number (the Skill Fuser's detour already calls the loader there: fus_move_start).
static const int RC_MAXK = 64;
struct RcRow { char key[28]; int w, l, d; };
static RcRow g_rc_char[RC_MAXK], g_rc_mode[RC_MAXK];
static int g_rc_nchar, g_rc_nmode;
static int g_rc_w, g_rc_l, g_rc_d, g_rc_streak, g_rc_best, g_rc_sv_best;   // v5.22 + Survival's best wave
static bool g_rc_sv;                                           // v5.22 this match is a Survival
static int g_rc_uses[FU_RECS];
static bool g_rc_loaded, g_rc_dirty;
static bool g_rc_on, g_rc_armed;                                // this match is counted; it showed -1 (running)
static int g_rc_me = -1;
static char g_rc_cur_char[28], g_rc_cur_mode[28];
static bool g_rc_was_in;
static int g_rc_page, g_rc_scroll, g_rc_clear;                 // the page: section, first row, X pressed once
static int *g_rc_t_rank, g_rc_t_me = -1;                       // self-test stand-ins
static bool g_rc_t;

// v5.188.2 (owner 2026-11-15): the
// records belong to the signed-in gamertag - CwoRecords_<gamertag>.txt; CwoRecords.txt (one file for the whole PC) is kept
// as the source of a profile's first file only when it is this PC's own (not the copy the 1.49-1.50.33 packs shipped).
static const char *fb_tag_now();                                 // cwo_feedback.inl: the signed-in gamertag ("" unknown)
static char g_rc_tag[48];                                        // the profile the loaded records belong to
static const unsigned long long RC_SHIPPED_FNV = 0x4101c167a9ada588ULL;   // data/CwoRecords.txt of BFR 1.49 - 1.50.33 (688 bytes)
static void rc_tag_file(const char *tag, wchar_t *p) {
    char s[48]; size_t n = 0;
    for (const char *c = tag; *c && n < 40; c++) s[n++] = isalnum((unsigned char)*c) ? *c : '_';
    s[n] = 0;
    swprintf_s(p, MAX_PATH, L"%sCwoRecords_%hs.txt", g_dir, s);
}
static void rc_path(wchar_t *p) {
    if (g_rc_tag[0]) rc_tag_file(g_rc_tag, p);
    else swprintf_s(p, MAX_PATH, L"%sCwoRecords.txt", g_dir);
}
static bool rc_legacy_is_shipped(const wchar_t *p) {             // the old PC-wide file is the pack's shipped copy
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"rb") != 0 || !f) return false;
    unsigned long long h = 0xcbf29ce484222325ULL; long n = 0; int c;
    while ((c = fgetc(f)) != EOF) { h = (h ^ (unsigned char)c) * 0x100000001b3ULL; n++; }
    fclose(f);
    return n == 688 && h == RC_SHIPPED_FNV;
}
static RcRow *rc_row(RcRow *t, int &n, const char *key) {
    for (int i = 0; i < n; i++) if (!strcmp(t[i].key, key)) return &t[i];
    if (n >= RC_MAXK) return NULL;
    RcRow &r = t[n++];
    memset(&r, 0, sizeof(r));
    strncpy_s(r.key, key, _TRUNCATE);
    return &r;
}
static void rc_save();
static void rc_load() {
    const char *tag = g_rc_t ? "" : fb_tag_now();
    if (g_rc_loaded && tag && tag[0] && strcmp(tag, g_rc_tag)) {   // another profile signed in: its own records
        if (g_rc_dirty && g_rc_tag[0]) rc_save();
        g_rc_loaded = false;
        g_rc_w = g_rc_l = g_rc_d = g_rc_streak = g_rc_best = g_rc_sv_best = 0;
        g_rc_nchar = g_rc_nmode = 0;
        memset(g_rc_uses, 0, sizeof(g_rc_uses));
    }
    if (g_rc_loaded) return;
    g_rc_loaded = true;
    if (g_rc_t) return;
    strcpy_s(g_rc_tag, tag ? tag : "");
    wchar_t p[MAX_PATH];
    rc_path(p);
    if (g_rc_tag[0] && GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) {     // first time for this profile
        wchar_t legacy[MAX_PATH];
        swprintf_s(legacy, MAX_PATH, L"%sCwoRecords.txt", g_dir);
        if (rc_legacy_is_shipped(legacy)) { logf("records: %s starts fresh (the shipped records file is not theirs)", "this profile"); return; }
        wcscpy_s(p, MAX_PATH, legacy);                           // this PC's own records -> the profile's first file
        g_rc_dirty = true;
        logf("records: this PC's records become the signed-in profile's own");
    }
    if (!g_rc_tag[0] && rc_legacy_is_shipped(p)) return;        // the profile is not known yet: never the shipped copy
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        int a, b, c, d, e;
        if (sscanf_s(line, "total %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) { g_rc_w = a; g_rc_l = b; g_rc_d = c; g_rc_streak = d; g_rc_best = e; continue; }
        if (sscanf_s(line, "skill %d %d", &a, &b) == 2) { if (a > 0 && a < FU_RECS) g_rc_uses[a] = b; continue; }
        if (sscanf_s(line, "survival %d", &a) == 1) { g_rc_sv_best = a; continue; }
        bool ch = !strncmp(line, "char ", 5), mo = !strncmp(line, "mode ", 5);
        if (!ch && !mo) continue;
        char *bar = strrchr(line, '|');                          // "char <name>|w l d"
        if (!bar || sscanf_s(bar + 1, "%d %d %d", &a, &b, &c) != 3) continue;
        *bar = 0;
        RcRow *r = ch ? rc_row(g_rc_char, g_rc_nchar, line + 5) : rc_row(g_rc_mode, g_rc_nmode, line + 5);
        if (r) { r->w = a; r->l = b; r->d = c; }
    }
    fclose(f);
}
static void rc_save() {
    g_rc_dirty = false;
    if (g_rc_t) return;
    wchar_t p[MAX_PATH];
    rc_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# Records (CWO-LOADER-01): offline matches only\ntotal %d %d %d %d %d\n", g_rc_w, g_rc_l, g_rc_d, g_rc_streak, g_rc_best);
    for (int i = 0; i < g_rc_nchar; i++) fprintf(f, "char %s|%d %d %d\n", g_rc_char[i].key, g_rc_char[i].w, g_rc_char[i].l, g_rc_char[i].d);
    for (int i = 0; i < g_rc_nmode; i++) fprintf(f, "mode %s|%d %d %d\n", g_rc_mode[i].key, g_rc_mode[i].w, g_rc_mode[i].l, g_rc_mode[i].d);
    for (int i = 1; i < FU_RECS; i++) if (g_rc_uses[i]) fprintf(f, "skill %d %d\n", i, g_rc_uses[i]);
    if (g_rc_sv_best) fprintf(f, "survival %d\n", g_rc_sv_best);
    fclose(f);
}

static void rc_char_name(int row, char *out, size_t n) {       // the select screen's name, or the charaset's
    lob_chars_ready();
    for (int i = 0; i < g_lob_nch; i++) if (g_lob_ch[i].row == row && g_lob_ch[i].name[0]) { strncpy_s(out, n, g_lob_ch[i].name, _TRUNCATE); return; }
    sprintf_s(out, n, "character %d", row);
    __try {
        char *b = lob_chara();
        if (!b) return;
        int rows = *(int *)(b + 4), pool = *(int *)(b + 8), rs = rows > 0 ? (pool - 16) / rows : 0;
        if (row >= 0 && row < rows && rs >= 0x3C && rs <= 0x100) {
            const char *nm = b + pool + *(int *)(b + 16 + rs * row + 4);
            if (nm[0] && (unsigned char)nm[0] < 0x80) strncpy_s(out, n, nm, _TRUNCATE);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// the match that just started: counted or not, and under which mode / character (game thread, first frame of a match)
static void rc_begin() {
    g_rc_on = false;
    if (g_forge_session || ed_online()) return;                // the creators' tests and anything online: never
    int origin = *(int *)(g_base + 0x4bca04), type = *(int *)(g_base + 0x4bc9d4);
    if (origin == 2 || origin == 3 || origin == 5 || type == 4 || type == 5) return;   // online, Download Battle
    int me = *(int *)(g_base + 0x4bbe00);
    if (me < 0 || me > 3) return;
    const char *mode = "Single Play";
    char buf[28];
    if (type == 6) { sprintf_s(buf, "Split screen %s", GT_NAMES[g_gt >= 0 && g_gt < GT_N ? g_gt : 0]); mode = buf; }
    else if (g_lob_go) mode = "3 VS 3 LOBBY";
    else if (g_six_match) mode = "3 vs 3";
    else if (origin == 6) mode = "Story battle";
    else if (type == 2) mode = "Story quest";
    g_rc_sv = sv_mode(g_ed_mode_on) && !ed_online();
    if (g_rc_sv) mode = "Survival";
    else if (!ed_online() && g_ed_mode_on >= 0 && g_ed_mode_on < g_edc[EDP_MODE].n) {          // v5.24
        int f5 = g_edc[EDP_MODE].item[g_ed_mode_on].v[5];
        if (f5 == AR_RING) mode = "Shrinking arena"; else if (f5 == AR_KOTH) mode = "King of the Hill";
    }
    strncpy_s(g_rc_cur_mode, mode, _TRUNCATE);
    unsigned word = 0;
    __try { word = *(unsigned *)((char *)cwo_gaddr(RVA6_SETUP) + 0x48 * me + 8); } __except (EXCEPTION_EXECUTE_HANDLER) { word = 0; }
    rc_char_name((int)(word & 0xFFFF), g_rc_cur_char, sizeof(g_rc_cur_char));
    g_rc_me = me;
    g_rc_armed = false;
    g_rc_on = true;
    logf("records: counting this match - %s, %s", g_rc_cur_mode, g_rc_cur_char);
}
static void rc_result(int r) {                                 // 0 won, 1 / 2 lost, 3 no contest
    if (g_rc_sv && g_sv_wave > g_rc_sv_best) { g_rc_sv_best = g_sv_wave; logf("records: Survival best wave %d", g_rc_sv_best); }
    RcRow *c = rc_row(g_rc_char, g_rc_nchar, g_rc_cur_char), *m = rc_row(g_rc_mode, g_rc_nmode, g_rc_cur_mode);
    if (r == 0) { g_rc_w++; if (c) c->w++; if (m) m->w++; g_rc_streak++; if (g_rc_streak > g_rc_best) g_rc_best = g_rc_streak; }
    else if (r == 1 || r == 2) { g_rc_l++; if (c) c->l++; if (m) m->l++; g_rc_streak = 0; }
    else { g_rc_d++; if (c) c->d++; if (m) m->d++; }
    g_rc_dirty = true;
    logf("records: %s - %d W %d L %d D, streak %d (best %d)", r == 0 ? "won" : (r == 1 || r == 2) ? "lost" : "no contest",
         g_rc_w, g_rc_l, g_rc_d, g_rc_streak, g_rc_best);
}
static void rc_step(bool in_match) {                           // game thread, every frame
    rc_load();
    __try {
        if (in_match && !g_rc_was_in) rc_begin();
        g_rc_was_in = in_match;
        if (g_rc_on) {
            int *rank = g_rc_t_rank ? g_rc_t_rank : (int *)cwo_gaddr(0x4bc5c0);
            int r = rank[g_rc_me];
            if (r == -1) g_rc_armed = true;
            else if (g_rc_armed && r >= 0 && r <= 3) { rc_result(r); g_rc_on = false; }
            if (g_rc_on && !in_match) g_rc_on = false;         // left without a result: not counted
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_rc_on = false; }
    if (g_rc_dirty) rc_save();
}
static void rc_on_skill(ULONG_PTR action, int id) {            // the skill-move detour (fus_move_start), game thread
    if (!g_rc_on || id <= 0 || id >= FU_RECS) return;
    __try {
        ULONG_PTR fighter = *(ULONG_PTR *)(action + 0xbd0);
        if (fighter && *(int *)(fighter + 0x14) == g_rc_me) { g_rc_uses[id]++; g_rc_dirty = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- the page
static const int RC_SECTIONS = 4, RC_ROWS = 14;
static const char *RC_SEC[RC_SECTIONS] = {"Summary", "By character", "By mode", "Most-used skills"};
static int rc_sorted(int *idx, int sec) {                      // rows of a section, most matches (or uses) first
    int n = 0;
    if (sec == 1 || sec == 2) {
        RcRow *t = sec == 1 ? g_rc_char : g_rc_mode;
        int m = sec == 1 ? g_rc_nchar : g_rc_nmode;
        for (int i = 0; i < m; i++) idx[n++] = i;
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && t[idx[j]].w + t[idx[j]].l + t[idx[j]].d > t[idx[j - 1]].w + t[idx[j - 1]].l + t[idx[j - 1]].d; j--) { int x = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = x; }
    } else if (sec == 3) {
        for (int i = 1; i < FU_RECS; i++) if (g_rc_uses[i]) idx[n++] = i;
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && g_rc_uses[idx[j]] > g_rc_uses[idx[j - 1]]; j--) { int x = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = x; }
    }
    return n;
}
static void rc_input(WORD pressed) {
    rc_load();
    if (pressed & BTN_B) { g_rc_clear = 0; InterlockedExchange(&g_ed_page, EDP_CHOOSER); return; }
    if (pressed & (BTN_RIGHT | BTN_RB)) { g_rc_page = (g_rc_page + 1) % RC_SECTIONS; g_rc_scroll = 0; g_rc_clear = 0; }
    if (pressed & (BTN_LEFT | BTN_LB)) { g_rc_page = (g_rc_page + RC_SECTIONS - 1) % RC_SECTIONS; g_rc_scroll = 0; g_rc_clear = 0; }
    static int idx[FU_RECS];
    int n = rc_sorted(idx, g_rc_page);
    int rc_max = n > RC_ROWS ? n - RC_ROWS : 0;                    // v5.120 the list loops
    if (pressed & BTN_DOWN) g_rc_scroll = g_rc_scroll < rc_max ? g_rc_scroll + 1 : 0;
    if (pressed & BTN_UP) g_rc_scroll = g_rc_scroll > 0 ? g_rc_scroll - 1 : rc_max;
    if (pressed & BTN_X) {
        if (!g_rc_clear) { g_rc_clear = 1; set_status("Clear every record? Press X again (any other button: no)"); }
        else {
            g_rc_clear = 0;
            g_rc_w = g_rc_l = g_rc_d = g_rc_streak = g_rc_best = g_rc_sv_best = 0; g_rc_nchar = g_rc_nmode = 0;
            memset(g_rc_uses, 0, sizeof(g_rc_uses));
            rc_save();
            set_status("Records cleared");
            logf("records: cleared by the owner");
        }
    } else if (pressed) g_rc_clear = 0;
}
static const char *RC_H[] = {"RECORDS - how to use", "Your offline matches are counted when each one ends:",
                             "  Single Play, split screen, the 3 VS 3 LOBBY, 3 vs 3 modes, story quests.",
                             "  Online and scoreboard matches are never counted.",
                             "L / R (or LB / RB): Summary, By character, By mode, Most-used skills",
                             "Up / Down: scroll   X twice: clear every record   B: back", NULL};
template <class PUT, class BOX> static void rc_draw(PUT &put, BOX &box, int x, int y) {
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF, GREEN = 0xFF80FF80, GREY = 0xFFB0B0B0;
    const int LINE = 16;
    (void)box;
    rc_load();
    if (g_ed_help) {
        put(x, y, RC_H[0], GOLD);
        y += LINE + 4;
        for (int i = 1; RC_H[i]; i++, y += LINE) put(x, y, RC_H[i], WHITE);
        put(x, y + 4, "Y / A / B: close this help", BLUE);
        return;
    }
    char line[128];
    sprintf_s(line, "RECORDS  <  %s  >          (B: back   Y: help)", RC_SEC[g_rc_page]);
    put(x, y, line, GOLD);
    y += LINE + 6;
    int all = g_rc_w + g_rc_l + g_rc_d;
    if (g_rc_page == 0) {
        sprintf_s(line, "Matches %d     Won %d     Lost %d     No contest %d", all, g_rc_w, g_rc_l, g_rc_d); put(x, y, line, WHITE); y += LINE;
        sprintf_s(line, "Won %d %%", g_rc_w + g_rc_l ? 100 * g_rc_w / (g_rc_w + g_rc_l) : 0); put(x, y, line, GREEN); y += LINE;
        sprintf_s(line, "Winning streak now %d     Best %d", g_rc_streak, g_rc_best); put(x, y, line, WHITE); y += LINE;
        if (g_rc_sv_best) { sprintf_s(line, "Survival: best wave %d", g_rc_sv_best); put(x, y, line, GREEN); y += LINE; }
        y += 6;
        static int idx[FU_RECS];
        int n = rc_sorted(idx, 1);
        if (n) { sprintf_s(line, "Most played: %s (%d matches)", g_rc_char[idx[0]].key, g_rc_char[idx[0]].w + g_rc_char[idx[0]].l + g_rc_char[idx[0]].d); put(x, y, line, BLUE); y += LINE; }
        n = rc_sorted(idx, 3);
        if (n) { sprintf_s(line, "Favourite skill: %.30s (used %d times)", sc_skill_name(idx[0]), g_rc_uses[idx[0]]); put(x, y, line, BLUE); y += LINE; }
        if (!all) put(x, y, "No match counted yet: play any offline match and come back.", GREY);
    } else {
        static int idx[FU_RECS];
        int n = rc_sorted(idx, g_rc_page);
        if (g_rc_page == 3) put(x, y, "Skill                                 Used", GREY);
        else put(x, y, g_rc_page == 1 ? "Character                 Matches   W    L    NC   Won" : "Mode                      Matches   W    L    NC   Won", GREY);
        y += LINE;
        for (int k = g_rc_scroll; k < n && k < g_rc_scroll + RC_ROWS; k++, y += LINE) {
            if (g_rc_page == 3) sprintf_s(line, "%2d  %-32.32s %5d", k + 1, sc_skill_name(idx[k]), g_rc_uses[idx[k]]);
            else {
                RcRow &r = (g_rc_page == 1 ? g_rc_char : g_rc_mode)[idx[k]];
                int m = r.w + r.l + r.d;
                sprintf_s(line, "%-24.24s %6d %5d %4d %4d %5d %%", r.key, m, r.w, r.l, r.d, r.w + r.l ? 100 * r.w / (r.w + r.l) : 0);
            }
            put(x, y, line, WHITE);
        }
        if (!n) put(x, y, "Nothing yet.", GREY);
        else if (n > RC_ROWS) { sprintf_s(line, "%d-%d of %d  (Up / Down)", g_rc_scroll + 1, g_rc_scroll + RC_ROWS < n ? g_rc_scroll + RC_ROWS : n, n); put(x, y + 4, line, GREY); }
    }
    if (g_rc_clear) put(x, y + LINE * 2, "X again clears every record", 0xFFFF6060);
}

static int rc_self_test() {                                    // counts on stand-ins: a win, a loss, a no contest, a streak, skills
    int keep_w = g_rc_w, keep_l = g_rc_l, keep_d = g_rc_d, keep_s = g_rc_streak, keep_b = g_rc_best, keep_nc = g_rc_nchar, keep_nm = g_rc_nmode;
    bool keep_loaded = g_rc_loaded, keep_on = g_rc_on, keep_in = g_rc_was_in;
    static int keep_uses[FU_RECS];
    memcpy(keep_uses, g_rc_uses, sizeof(keep_uses));
    g_rc_t = true; g_rc_loaded = true;
    g_rc_w = g_rc_l = g_rc_d = g_rc_streak = g_rc_best = 0; g_rc_nchar = g_rc_nmode = 0; memset(g_rc_uses, 0, sizeof(g_rc_uses));
    int rank[4] = {-1, -1, -1, -1};
    g_rc_t_rank = rank;
    auto match = [&](const char *ch, const char *mode, int result) {   // as rc_begin would set it, then the result
        strcpy_s(g_rc_cur_char, ch); strcpy_s(g_rc_cur_mode, mode); g_rc_me = 0; g_rc_on = true; g_rc_armed = false; g_rc_was_in = true;
        rank[0] = 3; rc_step(true);                              // the last match's result still there: not counted
        rank[0] = -1; rc_step(true);                             // running: nothing yet
        rank[0] = result; rc_step(true);
    };
    match("Freia", "Single Play", 0);
    match("Freia", "Single Play", 0);
    match("Edgar", "Split screen Tag Team", 1);
    match("Freia", "Single Play", 0);
    match("Freia", "3 VS 3 LOBBY", 3);
    bool totals = g_rc_w == 3 && g_rc_l == 1 && g_rc_d == 1 && g_rc_streak == 1 && g_rc_best == 2;
    bool chars = g_rc_nchar == 2 && !strcmp(g_rc_char[0].key, "Freia") && g_rc_char[0].w == 3 && g_rc_char[0].d == 1 && g_rc_char[1].l == 1;
    bool modes = g_rc_nmode == 3 && g_rc_mode[0].w == 3 && g_rc_mode[1].l == 1 && g_rc_mode[2].d == 1;
    // a skill used by your fighter counts, another fighter's does not, and nothing outside a counted match
    static char fighter0[0x20], fighter1[0x20], act0[0xc00], act1[0xc00];
    *(int *)(fighter0 + 0x14) = 0; *(int *)(fighter1 + 0x14) = 1;
    *(ULONG_PTR *)(act0 + 0xbd0) = (ULONG_PTR)fighter0; *(ULONG_PTR *)(act1 + 0xbd0) = (ULONG_PTR)fighter1;
    g_rc_on = true; g_rc_me = 0;
    rc_on_skill((ULONG_PTR)act0, 61); rc_on_skill((ULONG_PTR)act0, 61); rc_on_skill((ULONG_PTR)act1, 61); rc_on_skill((ULONG_PTR)act0, 7);
    g_rc_on = false; rc_on_skill((ULONG_PTR)act0, 7);
    static int idx[FU_RECS];
    bool skills = g_rc_uses[61] == 2 && g_rc_uses[7] == 1 && rc_sorted(idx, 3) == 2 && idx[0] == 61;
    // left without a result: not counted
    g_rc_on = true; rank[0] = -1; g_rc_was_in = true; rc_step(false);
    bool left = !g_rc_on && g_rc_w + g_rc_l + g_rc_d == 5;
    g_rc_t_rank = NULL; g_rc_t = false;
    g_rc_w = keep_w; g_rc_l = keep_l; g_rc_d = keep_d; g_rc_streak = keep_s; g_rc_best = keep_b; g_rc_nchar = keep_nc; g_rc_nmode = keep_nm;
    memcpy(g_rc_uses, keep_uses, sizeof(keep_uses)); g_rc_loaded = keep_loaded; g_rc_on = keep_on; g_rc_was_in = keep_in; g_rc_dirty = false;
    logf("self-test: records totals %s, characters %s, modes %s, skills %s, left %s", totals ? "ok" : "FAILED", chars ? "ok" : "FAILED",
         modes ? "ok" : "FAILED", skills ? "ok" : "FAILED", left ? "ok" : "FAILED");
    return totals && chars && modes && skills && left ? 1 : -64;
}
