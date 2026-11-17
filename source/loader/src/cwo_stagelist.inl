// ---- v5.43 the Map Slot 7 stage packs as rows of the stage lists (owner 2026-10-31).
// Game facts (listings 25_Menu_Research/stage_view_2026-10-27 + the installed vsmenu, MODLOG 2026-10-31):
//   the split-screen STAGE LIST (vsmenu builder 0x21dc) and the Xbox Live LEVEL LIST (livestagesel 0x15cc) are built
//   from a 17-entry table of stage ids (2-8, 12-17, 22-26 = catalog row + 10 x variant): for each id with
//   2.151(id, 56) and 2.151(id, 57) set, the row text 2.151(id, 51) is appended (2.123, row id = stage + 100) and a
//   32-entry script table maps list row -> stage id. The cursor's row goes through that table into script global 1 (the
//   frame: 0x2374 / 0x1764), which the picture (table_0x1f38 / 0x1328[id]), the stage view (op 49) and the confirm
//   (vsmenu: menu var 1 = id, 2.154; livestagesel: 4.576(id)) use.
// What this does (tools/build_stage_rows.py patches the two scripts): the script's row table is replaced by the
// loader's (ops 72 / 73 / 74), a second loop appends one row per listed pack (ops 70 / 71) with the value 1000 + k, the
// picture asks op 76 for the sprite, and the confirm passes the value through op 75 (a pack: Map Slot 7 switched to it,
// stage 10 returned). The stage view shows a pack row as stage 10 with the pack applied (cwo_stageview.inl). Listed
// packs = the mx_list rows whose file starts with "mx" (not the Stage Creator templates). 5.44: online too (owner 2026-10-31 "Yes do it";
// AGENTS.md "Pack rows in the Xbox Live LEVEL LIST").
static const int SL_ROWS = 256, SL_CODE = 1000, SL_ID_BASE = 130;     // list item ids (2.123): the scripts take A only for ids < 256 (mode = id + 100 < 356)
static int g_sl_val[SL_ROWS];                                  // list row -> stage id or SL_CODE + listed index
static int g_sl_rows;
static int g_sl_list[512], g_sl_n;                             // listed index -> g_mx index
static char g_sl_names[512][28];                               // display names: long phrases shortened, 26 chars
// g_sl_hl (cwo_portrait.inl): the highlighted pack row's g_mx index (-1 = none)

static bool g_sl_test;                                          // self-test: no game memory for the online check
static void sl_build_list() {                                  // after mx_load_list
    g_sl_n = 0; InterlockedExchange(&g_sl_hl, -1); memset(g_sl_names, 0, sizeof(g_sl_names));
    int missing = 0;
    for (int k = 0; k < g_mx_count && g_sl_n < 512; k++) {
        if (g_mx[k].file[0] != 'm' || g_mx[k].file[1] != 'x') continue;
        if (!g_sl_test) {                                       // v5.155 a pack without its files (patch refused) is not a row
            char a[32], d[32];
            sprintf_s(a, "%.24s.alr", g_mx[k].file); sprintf_s(d, "%.24s.dat", g_mx[k].file);
            if (!bigstage_file_size(a) || !bigstage_file_size(d)) {
                logf("stage rows: '%s' (%s) left out - its files are missing", g_mx[k].name, g_mx[k].file); missing++; continue;
            }
        }
        g_sl_list[g_sl_n++] = k;
    }
    logf("stage rows: %d of %d Map Slot 7 packs listed in the stage lists (%d left out: files missing)", g_sl_n, g_mx_count, missing);
}

static int sl_pack(int value) {                                // value -> g_mx index, -1 when not a pack row
    int i = value - SL_CODE;
    return i >= 0 && i < g_sl_n ? g_sl_list[i] : -1;
}

// the names shortened to the list window's 26 characters: a replacement table (in order, each only while the name is
// still too long), then "The " dropped, then a cut at a word boundary; checked on the 81 names: all unique, max 26
static const char *SL_REPL[][2] = {
    {"Reconstruction", "Rebuilt"}, {"Final Battle", "Last Battle"}, {"The ", ""}, {"Evening", "Eve"}, {"Destroyed", "Destr."}, {"Village", "Vil."}};
static void sl_replace(char *t, size_t n, const char *a, const char *b) {
    char *at = strstr(t, a);
    if (!at) return;
    char tail[64];
    strcpy_s(tail, at + strlen(a));
    at[0] = 0;
    strcat_s(t, n, b);
    strcat_s(t, n, tail);
}
static const char *sl_short_name(int i) {
    char *d = g_sl_names[i];
    if (!d[0]) {
        char t[64];
        strcpy_s(t, g_mx[g_sl_list[i]].name);
        for (int r = 0; r < (int)(sizeof(SL_REPL) / sizeof(SL_REPL[0])) && strlen(t) > 26; r++) {
            if (!strcmp(SL_REPL[r][0], "The ")) { if (!strncmp(t, "The ", 4)) memmove(t, t + 4, strlen(t) - 3); }
            else sl_replace(t, sizeof(t), SL_REPL[r][0], SL_REPL[r][1]);
        }
        if (strlen(t) > 26) {
            t[26] = 0;
            char *sp = strrchr(t, ' ');
            if (sp && sp - t >= 14) *sp = 0;
        }
        strncpy_s(d, 28, t, 26);
    }
    return d;
}
// ---- v5.158 WHAT-IF COUNTER (owner task 2026-11-12 "what could go wrong ... create a counter"): a stage list past 128 rows.
// Game fact (Ghidra 2026-11-12, FUN_140246770 = 2.123): a window keeps row text pointers at + 0x64 + row * 8 and row ids at
// + 0x464 + row * 4 and accepts 256 rows - from row 128 on the pointers overwrite the FIRST rows' ids (the stock stages: their
// A event / handler name break). The scripts also take A only for item ids < 256 (pack rows: 130 + k). On 2026-11-12 the Xbox
// Live list held 17 + 109 = 126 rows. Counter: the pack count the scripts get (op 70) never goes past what fits - 128 rows in
// the window counting the rows already appended, and 126 pack ids - and the loader says what it left out. A test file
// Data\CwoListCap.txt (a number) lowers the cap, to see the counter work in game.
static const int SL_WIN_ROWS = 128, SL_MAX_PACK_IDS = 256 - SL_ID_BASE;
static int g_sl_rows0 = -1, g_sl_cap_test, g_sl_cap_said = -1;
static int sl_cap_count(int count) {
    if (g_sl_rows0 < 0) g_sl_rows0 = g_sl_rows;                // the rows appended before the first pack row of this build
    int cap = SL_WIN_ROWS - g_sl_rows0;
    if (cap > SL_MAX_PACK_IDS) cap = SL_MAX_PACK_IDS;
    if (g_sl_cap_test > 0 && g_sl_cap_test < cap) cap = g_sl_cap_test;
    if (cap < 0) cap = 0;
    if (count > cap && g_sl_cap_said != count) {
        g_sl_cap_said = count;
        logf("stage list: %d pack rows, room for %d (a list window holds %d rows; %d before the packs) - the last %d are left out%s",
             count, cap, SL_WIN_ROWS, g_sl_rows0, count - cap, g_sl_cap_test > 0 ? " [CwoListCap.txt test cap]" : "");
    }
    return count > cap ? cap : count;
}
static void sl_cap_load() {
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoListCap.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    int v = 0;
    if (fscanf_s(f, "%d", &v) == 1 && v > 0) { g_sl_cap_test = v; logf("stage list: TEST cap %d pack rows (CwoListCap.txt)", v); }
    fclose(f);
}
static int sl_online_count() { return g_sl_n; }            // 5.44 (owner "Yes do it", AGENTS.md "Pack rows in the Xbox Live LEVEL LIST"): online too

// ---- v5.156 compact split-screen STAGE LIST (owner request 2026-11-12: "one row per stage family ... D-pad Right (and
// Left) cycles that row's variants and wraps back to the first ... Sein, then Edgar's Dream: Summit"). Split screen only:
// the installed vsmenu calls op 120 (5.156.0 used 80 - taken by the Lab's BFR row) after its stock rows (tools/build_compact_list.py); livestagesel (Xbox Live) never does.
// Game facts (Ghidra on PDUWP 9ba7411b, 2026-11-12; the installed vsmenu listing):
//   FUN_140246770 (2.123) appends to the current window *0x140533c58 + *(short *)(+0x10402) * 0x103c: text pointer at
//   window + 0x64 + row * 8, id at + 0x464 + row * 4, count at + 0x56 (cap 0x100); the two arrays overlap from row 128 on.
//   The stock rows' ids are stage + 100; the frame (0x2374) sets global 1 = op 73(cursor row) every frame, and the
//   picture, the stage view (op 49) and the confirm (op 75) read global 1. FUN_14024b000: Left / Right in a list give
//   menu events 0x13 / 0x14 and leave the cursor alone; the stage list state (ssel) has no handler for them.
// What this does: op 120 folds the stock rows into one row per catalog row (stage id % 10) in list order - the window's
// text / id arrays, the count and the loader's row table - and returns the new count (the script's row counter); the
// pack rows follow. Sein's family also holds the Summit pack (mx401), which is then not listed as its own row (op 70 /
// 71 / 72 skip it). Left / Right (the game's own menu event, read in op 73 - v5.157.1) step the cursor row's family:
// its row text (own buffer "name (i/n)"), its row id (the variant's own, so A works as on the stock row) and its value.
static bool ed_online();                                        // cwo_edgar.inl
static const int SL_FAM_MAX = 160, SL_VAR_MAX = 6;           // v5.157: pack families too (up to 5 versions)
struct SlFam { int key, row, n, cur, val[SL_VAR_MAX], sid[SL_VAR_MAX]; ULONG_PTR txt[SL_VAR_MAX]; char text[48]; };
static SlFam g_sl_fam[SL_FAM_MAX];
static int g_sl_fam_n, g_sl_win = -1, g_sl_skip = -1, g_sl_skip2 = -1;   // v5.187.2 two packs folded into Sein
static int g_sl_pf0, g_sl_pf_n;                                 // v5.157 the pack families: g_sl_fam[g_sl_pf0 + j]
static int g_sl_last_pack[512];                                 // v5.157 per pack family (its first listed index): the value chosen last
static bool g_sl_compact, g_sl_cbuild;
static volatile LONG g_sl_cycle;
static volatile LONGLONG g_sl_seen;
static int g_sl_last[10];                                      // the variant value chosen last per family key (0 = none)

static ULONG_PTR sl_win_addr() {
    if (g_sl_test || g_sl_win < 0) return 0;
    ULONG_PTR menu = *(ULONG_PTR *)(g_base + RVA_MENU_MGR);
    return menu ? menu + (ULONG_PTR)g_sl_win * 0x103c : 0;
}

// v5.157.3 (in game 17:52: "Edgar's Dream (Summit) (2/" - the window shows 26 characters): "name (i/n)" when it fits,
// else "name i/n", else the name cut to fit
static void sl_label(char *out, size_t n, const char *name, int i, int k) {
    char suf[16];
    sprintf_s(suf, " (%d/%d)", i, k);
    if (k > 1 && strlen(name) + strlen(suf) > 26) sprintf_s(suf, " %d/%d", i, k);
    if (k <= 1) suf[0] = 0;
    size_t room = 26 - strlen(suf);
    char nm[40];
    strncpy_s(nm, name, room < sizeof(nm) - 1 ? room : sizeof(nm) - 1);
    sprintf_s(out, n, "%s%s", nm, suf);
}


// v5.171.2 (owner 2026-11-13; some pack rows were cut in the
// middle of a word): base + variant into `room` characters by whole words - the variant's first
// comma part, no leading "The", shorter forms of long phrases, then the last word of the longer part dropped (never a
// trailing "of" / "the"), and only a single word left too long ends in a dot. Checked offline on every family in mx_list.
static void sl_squeeze(char *t) {
    char *w = t; bool sp = true;
    for (char *r = t; *r; r++) { if (*r == ' ') { if (!sp) *w++ = ' '; sp = true; } else { *w++ = *r; sp = false; } }
    while (w > t && w[-1] == ' ') w--;
    *w = 0;
}
static void sl_tidy(char *t) {                                    // no trailing stop word
    static const char *STOP[] = {"the", "of", "a", "to", "and", "in", "on"};
    for (;;) {
        char *sp = strrchr(t, ' ');
        if (!sp) return;
        bool stop = false;
        for (const char *w : STOP) if (!_stricmp(sp + 1, w)) stop = true;
        if (!stop) return;
        *sp = 0;
    }
}
static void sl_fit_words(char *base, size_t bn, char *var, size_t vn, size_t room) {
    auto L = [&]() { return strlen(base) + (var[0] ? strlen(var) + 1 : 0); };
    if (L() <= room) return;
    char *cm = strstr(var, ", ");
    if (cm) *cm = 0;
    if (L() > room && !strncmp(base, "The ", 4)) memmove(base, base + 4, strlen(base + 4) + 1);
    static const char *PH[][2] = {
                                  {"Final Battle", "Finale"}, {"Reconstruction", "Rebuilt"}, {"Destroyed", "Ruined"},
                                  {"Evening", "Dusk"}, {"Village", "Vil."}};
    for (auto &p : PH) if (L() > room) { sl_replace(base, bn, p[0], p[1]); sl_replace(var, vn, p[0], p[1]); sl_squeeze(base); sl_squeeze(var); }
    for (int guard = 0; guard < 40 && L() > room; guard++) {
        size_t lb = strlen(base), lv = strlen(var);
        bool vm = strchr(var, ' ') != NULL, bm = strchr(base, ' ') != NULL;
        char *t = vm && (lv >= lb || !bm) ? var : bm ? base : NULL;
        if (t) { *strrchr(t, ' ') = 0; sl_tidy(t); continue; }
        char *w = lv >= lb ? var : base;
        size_t other = lv >= lb ? lb : lv, keep = room > other + 5 ? room - other - 2 : 3;
        if (strlen(w) > keep) { w[keep] = '.'; w[keep + 1] = 0; }
        break;
    }
}
static int sl_fit_self_test() {                                // the rows that read badly in game, each whole-word now
    struct T { const char *b, *v, *want; } t[] = {
        {"RoofTop Day", "old, Classic", "RoofTop Day old"}, {"Quiet Shore Village", "Reconstruction", "Quiet Shore Rebuilt"},
        {"Quiet Shore Village", "Destroyed", "Quiet Shore Ruined"}, {"The Silent Valley", "Final Battle", "Silent Valley Finale"},
        {"Grand Panorama", "Sunset", "Grand Panorama Sunset"}, {"Palace", "UE Bridge test", "Palace UE Bridge test"}};
    bool ok = true;
    for (auto &x : t) {
        char b[48], v[40], out[96];
        strcpy_s(b, x.b); strcpy_s(v, x.v);
        sl_fit_words(b, sizeof(b), v, sizeof(v), 22);
        sprintf_s(out, "%s%s%s", b, v[0] ? " " : "", v);
        if (strcmp(out, x.want)) { ok = false; logf("self-test: stage row '%s (%s)' -> '%s' (wanted '%s')", x.b, x.v, out, x.want); }
    }
    logf("self-test: stage list family names fit by whole words %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -104;
}
static void sl_pack_text(SlFam *f) {                           // v5.157 "short name (i/n)" in 26 characters
    int i = f->val[f->cur] - SL_CODE;
    const char *full = i >= 0 && i < g_sl_n ? sl_short_name(i) : "";
    if (f->n <= 1) { sl_label(f->text, sizeof(f->text), full, 1, 1); return; }
    // v5.157.7 (in game 18:37: "Grand Panorama (Sunset 2/2"): a family row reads "Base Variant i/n" - the variant without its
    // brackets and kept whole, the base shortened first ("Village" -> "Vil.", then cut)
    char base[48], var[40] = "", suf[12];
    strcpy_s(base, full);
    char *par = strrchr(base, '(');
    if (par && par > base && par[-1] == ' ') {
        strcpy_s(var, par + 1);
        char *close = strrchr(var, ')');
        if (close) *close = 0;
        par[-1] = 0;
    }
    // v5.186.6 (2026-11-15 quality hour 3, BFR list seen 09:0x: "Grand Panorama 1/2" beside "Lost Palace (1/3)"): the same
    // "(i/n)" brackets as the game's own family rows whenever the whole name fits; only a longer one keeps " i/n"
    // one style for the whole row (seen: "Training Field Day (1/2)" but "Training Field Evening 2/2"): brackets only when every
    // variant's name fits with them
    bool brackets = true;
    for (int k = 0; k < f->n && brackets; k++) {
        int j = f->val[k] - SL_CODE;
        const char *nm = j >= 0 && j < g_sl_n ? sl_short_name(j) : "";
        size_t len = 0;                                         // "Base (Variant)" reads "Base Variant": one character less
        for (const char *c = nm; *c; c++) if (*c != '(' && *c != ')') len++;
        if (len + 6 > 26) brackets = false;                     // " (i/n)"
    }
    if (brackets) sprintf_s(suf, " (%d/%d)", f->cur + 1, f->n); else sprintf_s(suf, " %d/%d", f->cur + 1, f->n);
    sl_fit_words(base, sizeof(base), var, sizeof(var), 26 - strlen(suf));   // v5.171.2 whole words only
    sprintf_s(f->text, "%s%s%s%s", base, var[0] ? " " : "", var, suf);
}

// v5.157.5 the chosen variants survive a restart: Data\CwoStageFamilies.txt, "stock <catalog row> <stage id>" and
// "pack <first member's file> <chosen member's file>" (files, not list positions: a new pack does not shift them)
static int g_sl_saved_n;
static char g_sl_saved[64][2][24];                              // pack: first file -> chosen file
static void sl_fam_save() {
    if (g_sl_test) return;
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoStageFamilies.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fputs("# compact STAGE LIST: the variant each family row shows (loader 5.157.5; Left / Right in the split-screen STAGE LIST)\n", f);
    for (int k = 0; k < 10; k++) if (g_sl_last[k]) fprintf(f, "stock %d %d\n", k, g_sl_last[k]);
    for (int j = 0; j < g_sl_saved_n; j++) fprintf(f, "pack %s %s\n", g_sl_saved[j][0], g_sl_saved[j][1]);
    fclose(f);
}
static void sl_fam_load() {                                     // worker start, after sl_build_list
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoStageFamilies.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[128];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        int k, v;
        char a[24], b[24];
        if (sscanf_s(line, "stock %d %d", &k, &v) == 2 && k >= 0 && k < 10 && v >= 0 && v < 30 && v % 10 == k) { g_sl_last[k] = v; n++; }
        else if (sscanf_s(line, "pack %23s %23s", a, (unsigned)sizeof(a), b, (unsigned)sizeof(b)) == 2 && g_sl_saved_n < 64) {
            strcpy_s(g_sl_saved[g_sl_saved_n][0], a); strcpy_s(g_sl_saved[g_sl_saved_n][1], b); g_sl_saved_n++; n++;
        }
    }
    fclose(f);
    logf("stage list: %d remembered family choices (CwoStageFamilies.txt)", n);
}
static void sl_pack_remember(SlFam *f) {                        // the pack family's chosen member, by file
    const char *first = g_mx[g_sl_list[f->val[0] - SL_CODE]].file, *cur = g_mx[g_sl_list[f->val[f->cur] - SL_CODE]].file;
    for (int j = 0; j < g_sl_saved_n; j++)
        if (!strcmp(g_sl_saved[j][0], first)) { strcpy_s(g_sl_saved[j][1], cur); return; }
    if (g_sl_saved_n < 64) { strcpy_s(g_sl_saved[g_sl_saved_n][0], first); strcpy_s(g_sl_saved[g_sl_saved_n][1], cur); g_sl_saved_n++; }
}

static void sl_fam_apply(SlFam *f) {                           // the family's current variant -> its row
    int v = f->val[f->cur];
    if (f->key >= 10) {                                         // v5.157 a pack family: its row shows f->text (op 71 gave it)
        sl_pack_text(f);
        if (f->row >= 0 && f->row < SL_ROWS) g_sl_val[f->row] = v;
        int first = f->val[0] - SL_CODE;
        if (first >= 0 && first < 512) g_sl_last_pack[first] = v;
        return;
    }
    if (f->n > 1) {
        char name[40] = "";
        __try { strncpy_s(name, (const char *)f->txt[f->cur], 30); } __except (EXCEPTION_EXECUTE_HANDLER) { name[0] = 0; }
        sl_label(f->text, sizeof(f->text), name, f->cur + 1, f->n);
    }
    g_sl_val[f->row] = v;
    g_sl_last[f->key] = v;
    ULONG_PTR W = sl_win_addr();
    if (!W) return;
    __try {
        if (*(short *)(W + 0x56) <= f->row) return;
        *(ULONG_PTR *)(W + 0x64 + (ULONG_PTR)f->row * 8) = f->n > 1 ? (ULONG_PTR)f->text : f->txt[f->cur];
        unsigned *id = (unsigned *)(W + 0x464 + (ULONG_PTR)f->row * 4);
        *id = (*id & ~0x1ffu) | ((unsigned)f->sid[f->cur] & 0x1ffu);
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("stage list: row write fault - compact list off"); g_sl_compact = false; }
}

static int sl_compact() {                                      // op 120: returns the row count the pack rows continue from
    int n = g_sl_rows;
    g_sl_compact = false; g_sl_cbuild = false; g_sl_skip = -1; g_sl_skip2 = -1; g_sl_fam_n = 0;
    // v5.186.10 (owner 2026-11-15, AGENTS.md "Level list variants online"): the Xbox Live LEVEL LIST is compacted too (BFR
    // livestagesel calls op 120); the MODS edition keeps its online list as it was
    if ((!g_sl_test && ed_online() && !g_bfr_edition) || n <= 0 || n > 32) return n;
    ULONG_PTR W = 0;
    ULONG_PTR txt[32] = {0};
    unsigned sid[32] = {0};
    if (!g_sl_test) {
        ULONG_PTR menu = *(ULONG_PTR *)(g_base + RVA_MENU_MGR);
        if (!menu) return n;
        g_sl_win = *(short *)(menu + 0x10402);
        W = menu + (ULONG_PTR)g_sl_win * 0x103c;
        if (*(short *)(W + 0x56) != n) { logf("stage list: window %d has %d rows, the row table %d - not compacted", g_sl_win, *(short *)(W + 0x56), n); return n; }
        for (int r = 0; r < n; r++) {
            txt[r] = *(ULONG_PTR *)(W + 0x64 + (ULONG_PTR)r * 8);
            sid[r] = *(unsigned *)(W + 0x464 + (ULONG_PTR)r * 4);
            if ((int)(sid[r] & 0x1ff) != g_sl_val[r] + 100 || g_sl_val[r] < 0 || g_sl_val[r] >= 30) {
                logf("stage list: row %d id %u value %d - not the stock rows, not compacted", r, sid[r] & 0x1ff, g_sl_val[r]); return n;
            }
        }
    } else {
        for (int r = 0; r < n; r++) { txt[r] = (ULONG_PTR)"test"; sid[r] = (unsigned)(g_sl_val[r] + 100); }
    }
    for (int r = 0; r < n; r++) {                              // group by catalog row, in list order
        int key = g_sl_val[r] % 10, f = 0;
        while (f < g_sl_fam_n && g_sl_fam[f].key != key) f++;
        if (f == g_sl_fam_n) {
            if (g_sl_fam_n >= SL_FAM_MAX) return n;
            memset(&g_sl_fam[f], 0, sizeof(SlFam));
            g_sl_fam[f].key = key; g_sl_fam[f].row = f; g_sl_fam_n++;
        }
        SlFam *F = &g_sl_fam[f];
        if (F->n < SL_VAR_MAX) { F->val[F->n] = g_sl_val[r]; F->txt[F->n] = txt[r]; F->sid[F->n] = (int)(sid[r] & 0x1ff); F->n++; }
    }
    // v5.187.2 (owner 2026-11-15 "recreate the summit"): the Summit remake (mx403) is a Sein version too, after the Summit
    for (int i = 0; i < g_sl_n; i++)                           // Sein (key 8) -> Edgar's Dream: Summit (mx401), remake (mx403)
        if (!_stricmp(g_mx[g_sl_list[i]].file, "mx401") || !_stricmp(g_mx[g_sl_list[i]].file, "mx403"))
            for (int f = 0; f < g_sl_fam_n; f++)
                if (g_sl_fam[f].key == 8 && g_sl_fam[f].n < SL_VAR_MAX) {
                    SlFam *F = &g_sl_fam[f];
                    F->val[F->n] = SL_CODE + i; F->txt[F->n] = (ULONG_PTR)g_mx[g_sl_list[i]].name; F->sid[F->n] = F->sid[0]; F->n++;
                    if (g_sl_skip < 0) g_sl_skip = i; else g_sl_skip2 = i;
                }
    for (int r = 0; r < g_sl_fam_n; r++) {                     // the variant chosen last stays chosen
        SlFam *F = &g_sl_fam[r];
        for (int v = 0; v < F->n; v++) if (g_sl_last[F->key] && F->val[v] == g_sl_last[F->key]) F->cur = v;
    }
    // v5.184.1 (owner 2026-11-14): the split-screen list is a fixed
    // table of 17 catalog rows in vsmenu (0x20ac / 0x1fb8, loops of 17) - the Underground (catalog row 30, stage 30, UG-01) was
    // never in it. When its own model buffer exists (Map\st30.alr installed), it becomes a family row of its own after Sein:
    // value 30 and row id 130 (= catalog row + 100, as the script's own rows), name "Underground".
    if (!g_sl_test && g_bigstage_buf[BIGSTAGE_UNDERGROUND] && g_sl_fam_n < SL_FAM_MAX && g_sl_fam_n < n) {
        SlFam *U = &g_sl_fam[g_sl_fam_n];
        memset(U, 0, sizeof(SlFam));
        U->key = BIGSTAGE_UNDERGROUND % 10; U->row = g_sl_fam_n;
        U->val[0] = BIGSTAGE_UNDERGROUND; U->txt[0] = (ULONG_PTR)"Underground"; U->sid[0] = 100 + BIGSTAGE_UNDERGROUND; U->n = 1;
        g_sl_fam_n++;
    }
    int stock_rows = g_sl_fam_n;
    // v5.157 pack families: listed packs whose names share the part before a final " (...)" - e.g. "Grand Panorama" and
    // "Grand Panorama (Sunset)" - in list order; the Summit is in Sein
    g_sl_pf0 = g_sl_fam_n; g_sl_pf_n = 0;
    for (int i = 0; i < g_sl_n && g_sl_fam_n < SL_FAM_MAX; i++) {
        if (i == g_sl_skip || i == g_sl_skip2) continue;
        char base[64];
        strcpy_s(base, g_mx[g_sl_list[i]].name);
        char *par = strrchr(base, '(');
        if (par && par > base && par[-1] == ' ' && strchr(par, ')')) par[-1] = 0;
        int f = g_sl_pf0;
        for (; f < g_sl_fam_n; f++) {
            char b2[64];
            strcpy_s(b2, g_mx[g_sl_list[g_sl_fam[f].val[0] - SL_CODE]].name);
            char *p2 = strrchr(b2, '(');
            if (p2 && p2 > b2 && p2[-1] == ' ' && strchr(p2, ')')) p2[-1] = 0;
            if (!strcmp(base, b2) && g_sl_fam[f].n < SL_VAR_MAX) break;
        }
        if (f == g_sl_fam_n) {
            memset(&g_sl_fam[f], 0, sizeof(SlFam));
            g_sl_fam[f].key = 10 + g_sl_pf_n; g_sl_fam[f].row = -1; g_sl_fam_n++; g_sl_pf_n++;
        }
        SlFam *F = &g_sl_fam[f];
        F->val[F->n] = SL_CODE + i; F->txt[F->n] = 0; F->sid[F->n] = 0; F->n++;
    }
    for (int j = 0; j < g_sl_pf_n; j++) {                     // the variant chosen last stays chosen (v5.157.5: across restarts)
        SlFam *F = &g_sl_fam[g_sl_pf0 + j];
        int first = F->val[0] - SL_CODE, last = first >= 0 && first < 512 ? g_sl_last_pack[first] : 0;
        for (int v = 0; v < F->n; v++) if (last && F->val[v] == last) F->cur = v;
        if (!last && first >= 0 && first < g_sl_n)
            for (int q = 0; q < g_sl_saved_n; q++)
                if (!strcmp(g_sl_saved[q][0], g_mx[g_sl_list[first]].file))
                    for (int v = 0; v < F->n; v++)
                        if (!strcmp(g_mx[g_sl_list[F->val[v] - SL_CODE]].file, g_sl_saved[q][1])) F->cur = v;
        sl_pack_text(F);
    }
    if (W) *(short *)(W + 0x56) = (short)stock_rows;
    g_sl_rows = stock_rows;
    g_sl_compact = true; g_sl_cbuild = true; g_sl_rows0 = -1;
    for (int r = 0; r < stock_rows; r++) sl_fam_apply(&g_sl_fam[r]);
    logf("stage list: compact - %d stock rows -> %d family rows, %d packs -> %d pack rows%s", n, stock_rows, g_sl_n, g_sl_pf_n,
         g_sl_skip >= 0 ? " (Summit inside Sein)" : "");
    return stock_rows;
}

static void sl_cycle_row(int row) {                            // menu thread (op 73): apply a pending Left / Right
    LONG d = InterlockedExchange(&g_sl_cycle, 0);
    if (!d || !g_sl_compact || row < 0) return;
    SlFam *F = NULL;
    for (int f = 0; f < g_sl_fam_n && !F; f++) if (g_sl_fam[f].row == row) F = &g_sl_fam[f];
    if (!F || F->n < 2) return;
    F->cur = (F->cur + d + F->n) % F->n;
    sl_fam_apply(F);
    if (F->key >= 10) sl_pack_remember(F);
    sl_fam_save();
    logf("stage list: row %d -> variant %d of %d (value %d)", row, F->cur + 1, F->n, F->val[F->cur]);
}

// native 2.8 ops 70-76
// g_sel_seen: cwo_holiday.inl (v5.76)
static INT64 sl_native(int op, INT64 *args) {
    __try {
        switch (op) {
        case 70: return sl_cap_count(g_sl_cbuild ? g_sl_pf_n : sl_online_count());   // listed packs (v5.157 families; v5.158 capped)
        case 71: {                                              // name of listed pack k, shortened to the window's width
            if (g_sl_cbuild) {                                  // v5.157 pack family k: its own text buffer
                int k = (int)args[1];
                return k >= 0 && k < g_sl_pf_n ? (INT64)(ULONG_PTR)g_sl_fam[g_sl_pf0 + k].text : (INT64)(ULONG_PTR)"";
            }
            int i = (int)args[1];
            return i >= 0 && i < sl_online_count() ? (INT64)(ULONG_PTR)sl_short_name(i) : (INT64)(ULONG_PTR)"";
        }
        case 72: {                                              // row table store: (row, value); row 0 starts a new list
            int value = (int)args[1], row = (int)args[2];
            if (g_sl_cbuild && value >= SL_CODE) {               // v5.157 pack family k -> its row, its current member
                int k = value - SL_CODE;
                if (k >= 0 && k < g_sl_pf_n) { SlFam *F = &g_sl_fam[g_sl_pf0 + k]; F->row = row; value = F->val[F->cur]; }
            }
            else if (row == 0) { memset(g_sl_val, 0, sizeof(g_sl_val)); g_sl_rows = 0; g_sl_compact = false; g_sl_cbuild = false; g_sl_skip = -1; g_sl_skip2 = -1; g_sl_rows0 = -1; }
            if (row >= 0 && row < SL_ROWS) { g_sl_val[row] = value; if (row >= g_sl_rows) g_sl_rows = row + 1; }
            return 0;
        }
        case 73: {                                              // row -> value
            int row = (int)args[1];
            InterlockedExchange64(&g_sl_seen, (LONGLONG)GetTickCount64());   // v5.156 the list is on screen
            // v5.157.1 Left / Right from the game's own menu event (Ghidra 2026-11-12: FUN_140248230, the menu frame, sets
            // *0x140533c58 + 0x10414 = 0 first; the list window's update FUN_14024b000 sets it to 0x13 / 0x14 for Left / Right
            // with the window index at + 0x10418; then the script runs - this op - in the same frame): one step per press,
            // keyboard and every pad alike (5.157.0 read only the loader's own pad thread, which a scripted pad never reached)
            if (g_sl_compact && !g_sl_test) {
                __try {
                    ULONG_PTR menu = *(ULONG_PTR *)(g_base + RVA_MENU_MGR);
                    int ev = *(int *)(menu + 0x10414), w = *(int *)(menu + 0x10418);
                    // v5.157.2 (in game 17:48: one Right stepped EVERY family row - the script reads all rows through this
                    // op while it draws the list): only the window's cursor row (window + 0x12, FUN_14024b000), once per press
                    short cur = *(short *)(menu + 0x40 + (ULONG_PTR)g_sl_win * 0x103c + 0x12);
                    static ULONGLONG s_last;
                    ULONGLONG now = GetTickCount64();
                    if ((ev == 0x13 || ev == 0x14) && w == g_sl_win && row == cur && now - s_last > 150) {
                        s_last = now;
                        InterlockedExchange(&g_sl_cycle, ev == 0x14 ? 1 : -1);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (g_sl_cycle) sl_cycle_row(row);
            return row >= 0 && row < g_sl_rows ? g_sl_val[row] : 0;
        }
        case 74: {                                              // value -> row (0 when none, as the script's search did)
            int value = (int)args[1];
            if (g_sl_compact)                                   // v5.156 a variant inside a family row: that row, showing it
                for (int f = 0; f < g_sl_fam_n; f++)
                    for (int v = 0; v < g_sl_fam[f].n; v++)
                        // v5.157.6: only the row - the variant shown stays the remembered one (in game 18:28 the cursor
                        // restore after the build switched the Palace row back to Lost Palace)
                        if (g_sl_fam[f].val[v] == value && g_sl_fam[f].row >= 0) return g_sl_fam[f].row;
            for (int r = 0; r < g_sl_rows; r++) if (g_sl_val[r] == value) return r;
            return 0;
        }
        case 75: {                                              // confirm: the stage id the game gets
            int value = (int)args[1], k = sl_pack(value);
            if (k < 0) return value;
            InterlockedExchange(&g_mx_sel, k);
            mx_apply(k);
            logf("stage rows: '%s' chosen -> Map Slot 7 (stage %d)", g_mx[k].name, LIST_STAGE);
            return g_mx_cur == k ? LIST_STAGE : value;
        }
        case 76: {                                              // picture sprite for (image slot, value)
            int value = (int)args[2], slot = (int)args[1], k = sl_pack(value);
            for (int i = 0; i < g_pt_img_n; i++)
                if (g_pt_img[i].slot == slot) {
                    if (k >= 0) return g_pt_img[i].sprite;
                    return value >= 0 && value < 30 && g_pt_img[i].table_n == 30 ? g_pt_img[i].table[value] : 0;
                }
            return 0;
        }
        case 77: {                                              // v5.73 select portrait: (base, monster page?, stock sprite)
            // owner 2026-11-02. The split-screen
            // grid (vsmenu) reads its slot's sprite from table 0x6bac (stock 23 21 22 21 24 .. 31 for slots 0-11) for both
            // pages; on the monster page (script routine 0x450 true) the loader answers base + slot instead
            // (tools/build_monster_portraits.py adds those sprites).
            g_sel_seen = GetTickCount64();                     // v5.76 the split-screen grid is on screen (Coming soon cell)
            int stock = (int)args[3], mon = (int)args[2], base = (int)args[1];
            g_sel_mon = mon != 0;                               // v5.96 which page the grid shows
            static const int SLOT_OF[11] = {1, 2, 0, 4, 5, 6, 7, 8, 9, 10, 11};   // stock sprite 21 .. 31 -> grid slot
            if (!mon && g_npc_page && base > 0 && stock == base + 3) return so_npc_cell(stock);   // v5.100 the NPC cell's portrait
            if (!mon) return so_sprite(stock, 21, false);   // v5.77 the COLORS character's cell = its highlighted outfit
            if (base <= 0 || stock < 21 || stock > 31) return stock;
            return so_sprite_mon(base + SLOT_OF[stock - 21], SLOT_OF[stock - 21], false);   // v5.86 its outfit too
        }
        case 78: {                                              // v5.76 the Xbox Live select (livecharasel table 0x378: 40 38 39 41 .. 48)
            g_sel_live_seen = GetTickCount64();                // v5.87 the Xbox Live grid is on screen (wide selects)
            int stock = (int)args[3], mon = (int)args[2], base = (int)args[1];
            static const int SLOT_OF2[11] = {1, 2, 0, 3, 4, 5, 6, 7, 8, 9, 10};   // stock sprite 38 .. 48 -> grid slot
            if (!mon) return so_sprite(stock, 38, true);
            if (base <= 0 || stock < 38 || stock > 48) return stock;
            return so_sprite_mon(base + SLOT_OF2[stock - 38], SLOT_OF2[stock - 38], true);
        }
        case 120: return sl_compact();                         // v5.157 compact split-screen STAGE LIST (vsmenu only; op 80 = the Lab's BFR row)
        case 79:                                                // v5.77 a COLORS row is drawn (_draw_plcolo; cwo_selout.inl)
            so_row_drawn();
            return 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("stage rows: fault in op %d", op); }
    return 0;
}

static int sl_self_test() {                                    // the row table and the pack codes on stand-ins
    int save_n = g_sl_n, save_mx = g_mx_count;
    g_sl_test = true;
    g_mx_count = 3; strcpy_s(g_mx[0].file, "pc01"); strcpy_s(g_mx[1].file, "mx101"); strcpy_s(g_mx[1].name, "Pack One");
    strcpy_s(g_mx[2].file, "mx102"); strcpy_s(g_mx[2].name, "Pack Two");
    sl_build_list();
    bool listed = g_sl_n == 2 && g_sl_list[0] == 1 && g_sl_list[1] == 2;
    INT64 a[4] = {72, 3, 0, 0};
    sl_native(72, a);                                          // row 0 = stage 3
    a[1] = 13; a[2] = 1; sl_native(72, a);                     // row 1 = stage 13
    a[1] = SL_CODE + 1; a[2] = 2; sl_native(72, a);            // row 2 = pack 1
    a[1] = 1; bool r1 = sl_native(73, a) == 13;
    a[1] = SL_CODE + 1; bool r2 = sl_native(74, a) == 2;
    a[1] = 99; bool r3 = sl_native(74, a) == 0;
    a[1] = 1; bool nm = !strcmp((const char *)(ULONG_PTR)sl_native(71, a), "Pack Two");
    a[1] = 0; a[2] = 0; sl_native(72, a);                      // row 0 again: a new list
    bool cleared = g_sl_rows == 1;
    // v5.156 compaction: stock rows 3 13 23 2 12 -> 2 family rows; Right on row 0 -> 13
    int vals[5] = {3, 13, 23, 2, 12};
    for (int r = 0; r < 5; r++) { a[1] = vals[r]; a[2] = r; sl_native(72, a); }
    int fams = sl_compact();
    InterlockedExchange(&g_sl_cycle, 1);
    a[1] = 0; INT64 v13 = sl_native(73, a);
    a[1] = 23; INT64 r23 = sl_native(74, a);
    bool comp = fams == 2 && g_sl_rows == 2 && v13 == 13 && r23 == 0 && g_sl_val[0] == 13 && g_sl_val[1] == 2;   // v5.157.6 op 74 keeps the variant
    a[1] = 0; a[2] = 0; sl_native(72, a);
    memset(g_sl_last, 0, sizeof(g_sl_last));
    cleared = cleared && comp;
    // v5.158 the counter: 17 rows appended, 200 packs -> 111 (128 - 17); 0 rows, 200 packs -> 126 (pack ids < 256)
    int save_n2 = g_sl_n;
    a[1] = 0; a[2] = 0; sl_native(72, a);
    for (int r = 1; r < 17; r++) { a[1] = r % 10 + 2; a[2] = r; sl_native(72, a); }
    g_sl_n = 200; INT64 c1 = sl_native(70, a);
    a[1] = 0; a[2] = 0; sl_native(72, a); g_sl_rows = 0; g_sl_rows0 = -1;
    INT64 c2 = sl_cap_count(200);
    g_sl_n = save_n2; g_sl_rows0 = -1; g_sl_cap_said = -1;
    bool capok = c1 == 111 && c2 == 126;
    if (!capok) logf("self-test: list cap FAILED (%lld, %lld)", c1, c2);
    cleared = cleared && capok;
    if (!comp) logf("self-test: compact list FAILED (families %d, rows %d, value %lld, row %lld)", fams, g_sl_rows, v13, r23);
    g_sl_n = save_n; g_mx_count = save_mx; g_sl_test = false;
    logf("self-test: stage rows list %s, table %s %s %s, name %s, new list %s", listed ? "ok" : "FAILED", r1 ? "ok" : "FAILED",
         r2 ? "ok" : "FAILED", r3 ? "ok" : "FAILED", nm ? "ok" : "FAILED", cleared ? "ok" : "FAILED");
    return listed && r1 && r2 && r3 && nm && cleared ? 1 : -79;
}
