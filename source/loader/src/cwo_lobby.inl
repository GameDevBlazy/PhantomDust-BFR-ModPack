// ---- v5.2 the 3 VS 3 LOBBY (owner 2026-10-25; reference: the owner's
// lobby screenshots 2026-10-24 230153 / 230230 / 230236). Drawn by the loader in Edgar's Dream, one controller; A starts
// the match through Single Play's own lists (the loader presses through them) and cwo_sixf2.inl builds the 6 fighters.
// Game facts (Ghidra batch_065, capstone):
//  - every fighter loads its own model at its start: FUN_14024e190 -> FUN_140250f70 builds "player\<stem>" and
//    "<stem><outfit>.alr" from the record word +8 (low 16 bits = charaset row, high 16 = outfit; outfits only when the
//    row's flag 0x1404c0258 is set) into the fighter's own model slot. The word is set before FUN_140128cc0 creates the
//    fighters, so any character and outfit can be in any slot;
//  - the charaset image the game keeps, *0x140590190 (FUN_14017d300 copies it into 0x1404c01e0): +4 rows, +8 string pool,
//    rows of (pool - 16) / rows bytes from +16: +0 stem, +4 shown name (pool offsets), +0x3A outfits;
//  - Single Play's stage list 0x140509ba0 (rows of 0x28: +8 name, +0x18 stage, +0x1c variants; *0x14050a5d0 rows) and
//    its cursor *0x140509b88 ([0] row, [3] variant): FUN_140192780 takes them on A;
//  - arsenals *(*0x1404bec10 + 0x3a30): 100-byte records, +4 name, +0x14 30 skill ids, +0x50 school count; the one in
//    use *(*0x1404bec10 + 0x3b08) (FUN_140192a50); deck counts as FUN_14014f830; the aura factor from GSDATA +0x33034 by
//    school count 1-3, else *0x1402f7da8.
// v5.2.2 (owner test 2026-10-25, photos of 5.2.1): OPTIONS is a solid bordered panel in the rows' place (no text through
// it); slot 1 can be you or a CPU ("Player 1 shouldn't be player 1 ... or it should be an option to make that a CPU");
// START GAME is an entry under SESSION INFO; the dummy stage rows are skipped
static const char *CPU_LV_NAMES[] = {"the game's own", "Tough", "Brutal", "Brutal - weakest first", "Brutal escort"};
static const int CPU_LV_REC[] = {-1, 82, 83, 84, 85}, CPU_LV_N = 5;
static int g_cpu_level;                                        // lobby / Single Play CPUs (CwoLobby.txt "cpulevel")
static bool g_cpu_lv_loaded;                                   // g_cpu_level read from CwoLobby.txt
static const signed char *g_cpu_lv_test_tbl;                    // self-test stand-in
static const int LOB_N = 6, LOB_START = LOB_N;                // cursor 0-5 slots, 6 START GAME
// v5.152 SINGLE PLAY (owner 2026-11-08 under MULTIPLAYER; 2026-11-12: Boss Raid as a game type, Spectator and Draft Arsenal into the lobbies, "Do all of
// these"): this one-controller lobby is SINGLE PLAY - its game type sets how many fighters there are
enum { LO_TYPE, LO_STAGE, LO_PLAYER, LO_CHAR, LO_OUTFIT, LO_ARS, LO_TEAM, LO_RULE, LO_CPU, LO_BOSS, LO_ALLIES, LO_SPECT, LO_DRAFT,
       LO_START, LO_CLOSE, LOB_OPTS };
static const char *LOB_OPT[LOB_OPTS] = {"Game Type", "Change Stage", "Change Player", "Change Character", "Change Outfit",
                                        "Change Arsenal", "Change Team", "Change Rule", "CPU Level", "Boss", "CPU Allies",
                                        "Spectator Mode", "Draft Arsenal", "Start Game", "Close"};
// v5.188.24 (owner 2026-11-16): SP_TRIO = six fighters in three teams of two, through the 3 vs 3 path
// (the game's per-team arrays hold 4 teams - sixf2_manifest: FUN_14012c8c0 loops over 4 teams - so teams 0-2 fit as they are).
// Added last: CwoLobby.txt keeps the type by number
enum { SP_ONE, SP_TAG, SP_ROYALE, SP_SIX, SP_RAID, SP_TRIO, SP_N };
static const char *SP_TYPES[SP_N] = {"One-on-One", "Tag Team", "Battle Royale", "3 vs 3", "Boss Raid", "2 vs 2 vs 2"};
static int g_lob_trio;                                          // v5.188.26 the lobby match is 2 vs 2 vs 2
static int g_lob_ffa;                                           // v5.188.26 the lobby match is Battle Royale (each alone)
static char g_lob_pick[24];                                     // v5.188.32 a Create a Mode mode picked in Change Rule ('' = a game rule)
static int g_sp_br_n = 4;                                       // v5.188.26 Battle Royale fighters 2-6 (owner 2026-11-16), CwoLobby.txt brn
static int g_sp_type = SP_SIX;                                   // CwoLobby.txt "type"
static int g_sp_boss;                                            // Boss Raid: BOSS0 .. BOSS3b of the game's Single Play list
static const char *SP_BOSS[5] = {"Peccato", "Thalasso", "Carcino", "Demented Freia", "Demented Freia (phase 2)"};
// g_lob_go_n (fighters of the lobby match: 6, 4 or 2) and g_sp_boss_row (Boss Raid's battle-list row): cwo_loader.cpp
static int br_allies();                                          // cwo_bossraid.inl
static void br_allies_step(int d);
static int sp_slots(int t) { return t == SP_SIX || t == SP_TRIO ? 6 : t == SP_ROYALE ? g_sp_br_n : t == SP_TAG ? 4 : t == SP_ONE ? 2 : 1; }
struct LobSlot { int ch, outfit, ars, team, cpu; };            // cpu: slot 1 only (the others are always CPUs)
struct LobChar { int row, outfits; char name[24]; };
static LobSlot g_lob[LOB_N];
static LobChar g_lob_ch[40];
static int g_lob_nch = -1, g_lob_loaded;
static bool g_lob_no_table;                                      // v5.59 built before the game's character table was readable                       // g_lob_stage / g_lob_var / g_lob_go: cwo_loader.cpp
static int g_lob_rate[64];
static volatile LONG g_lob_sel, g_lob_opt, g_lob_osel;
static LobSlot g_lob_go_slots[LOB_N];                          // the slots that match was started with

static char *lob_chara() { __try { return *(char **)(g_base + 0x590190); } __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; } }
static void lob_build_chars() {                                // the 15 players of the select screen + the created characters
    g_lob_nch = 0;
    char *b = lob_chara();
    int rows = 0, pool = 0, rs = 0;
    __try {
        if (b) { rows = *(int *)(b + 4); pool = *(int *)(b + 8); rs = rows > 0 ? (pool - 16) / rows : 0; }
        if (rows <= 0 || rows > 200 || rs < 0x3C || rs > 0x100) b = NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) { b = NULL; }
    auto add = [&](int row, const char *fallback) {
        LobChar &c = g_lob_ch[g_lob_nch];
        c.row = row; c.outfits = 4;                              // v5.59 the stock game's count (was 6: outfits 5 / 6 do not exist - Download Battle hung)
        strncpy_s(c.name, fallback, _TRUNCATE);
        __try {
            if (b && row < rows) {
                char *r = b + 16 + rs * row;
                short n = *(short *)(r + 0x3A);
                if (n >= 1 && n <= 32) c.outfits = n;
                const char *nm = b + pool + *(int *)(r + 4);
                if (!fallback[0] && nm[0]) strncpy_s(c.name, nm, _TRUNCATE);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_lob_nch++;
    };
    g_lob_no_table = b == NULL;
    for (int i = 0; i < 15; i++) add(i, ED_CHARAS[i]);
    if (g_slot_n < 0) slot_load();
    for (int k = 0; k < g_slot_n && g_lob_nch < 40; k++) add(g_slots[k].row, g_slots[k].name);
}

static void lob_chars_ready() { if (g_lob_nch <= 0 || g_lob_no_table || InterlockedExchange(&g_of_recount, 0)) lob_build_chars(); }   // v5.63 the 15 outfits   // v5.59
static int lob_outfits_of_row(int row) {                       // v5.13 Create a Quest: a character's outfits (0 = not known)
    lob_chars_ready();
    for (int i = 0; i < g_lob_nch; i++) if (g_lob_ch[i].row == row) return g_lob_ch[i].outfits;
    char *b = lob_chara();
    __try {
        if (b) {
            int rows = *(int *)(b + 4), pool = *(int *)(b + 8), rs = rows > 0 ? (pool - 16) / rows : 0;
            if (row >= 0 && row < rows && rs >= 0x3C && rs <= 0x100) { short n = *(short *)(b + 16 + rs * row + 0x3A); if (n >= 1 && n <= 32) return n; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
static char *lob_ars_base() { __try { ULONG_PTR m = *(ULONG_PTR *)(g_base + RVA_ARSENAL); return m ? *(char **)(m + 0x3a30) : NULL; } __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; } }
static int lob_ars_count() {
    char *a = lob_ars_base();
    int n = 0;
    __try { while (a && n < 16 && a[100 * n + 4]) n++; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}
static void lob_ars_name(int i, char *out, size_t n) {
    char *a = lob_ars_base();
    out[0] = 0;
    __try { if (a && i >= 0 && i < lob_ars_count()) { strncpy_s(out, n, a + 100 * i + 4, 16); } } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!out[0]) strcpy_s(out, n, "-");
}
static int lob_stage_rows() { __try { int n = *(int *)(g_base + 0x50a5d0); return n > 0 && n < 64 ? n : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
static void lob_stage_name(int r, int v, char *out, size_t n) {
    out[0] = 0;
    __try {
        const char *nm = *(const char **)(g_base + 0x509ba0 + 0x28 * r + 8);
        int nv = *(int *)(g_base + 0x509ba0 + 0x28 * r + 0x1c);
        if (nm) { if (nv > 1) sprintf_s(out, n, "%.30s (%d of %d)", nm, v + 1, nv); else sprintf_s(out, n, "%.36s", nm); }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!out[0]) strcpy_s(out, n, "(pick a stage)");
}
static bool lob_stage_dummy(int r) {                           // "00 DUMMY" and other placeholder rows of the list
    __try {
        const char *nm = *(const char **)(g_base + 0x509ba0 + 0x28 * r + 8);
        if (!nm || !nm[0]) return true;
        char up[48] = "";
        for (int i = 0; i < 47 && nm[i]; i++) up[i] = (char)toupper((unsigned char)nm[i]);
        return strstr(up, "DUMMY") != NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}
static int lob_stage_vars(int r) { __try { int v = *(int *)(g_base + 0x509ba0 + 0x28 * r + 0x1c); return v > 0 && v < 16 ? v : 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; } }

// v5.2.1 (owner): one rating per character (CwoRatings.txt "row rating") and one for you ("you rating"), 1000 to start
static int g_lob_rate_you;
static int lob_rating(int row) { return row >= 0 && row < 64 && g_lob_rate[row] > 0 ? g_lob_rate[row] : 1000; }
static int lob_rating_you() { return g_lob_rate_you > 0 ? g_lob_rate_you : 1000; }
static int lob_slot_rating(int i) { return i == 0 && !g_lob[0].cpu ? lob_rating_you() : lob_rating(g_lob_ch[g_lob[i].ch].row); }
static void lob_load() {                                       // CwoLobby.txt (the last lobby) + CwoRatings.txt
    g_lob_loaded = 1;
    if (g_lob_nch < 0) lob_build_chars();
    int cur_ars = 0;
    __try { ULONG_PTR m = *(ULONG_PTR *)(g_base + RVA_ARSENAL); if (m) cur_ars = *(int *)(m + 0x3b08); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    for (int i = 0; i < LOB_N; i++) g_lob[i] = {i < g_lob_nch ? i : 0, 0, cur_ars >= 0 && cur_ars < 16 ? cur_ars : 0, i < 3 ? 0 : 1, i > 0};
    wchar_t p[MAX_PATH];
    FILE *f = NULL;
    ed_path("CwoLobby.txt", p);
    if (cwo_wfopen(&f, p, L"r") == 0 && f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            int a, b, c, d, e, cp = 0;
            if (sscanf_s(line, "stage %d %d", &a, &b) == 2) { g_lob_stage = a; g_lob_var = b; }
            else if (sscanf_s(line, "cpulevel %d", &a) == 1) { g_cpu_level = a >= 0 && a < CPU_LV_N ? a : 0; g_cpu_lv_loaded = true; }
            else if (!strncmp(line, "pick ", 5)) { strncpy_s(g_lob_pick, line + 5, _TRUNCATE); for (char *q = g_lob_pick; *q; q++) if (*q == 13 || *q == 10) *q = 0; }   // v5.188.32
            else if (sscanf_s(line, "brn %d", &a) == 1) g_sp_br_n = a >= 2 && a <= 6 ? a : 4;   // v5.188.26
            else if (sscanf_s(line, "type %d", &a) == 1) g_sp_type = a >= 0 && a < SP_N ? a : SP_SIX;   // v5.152; v5.188.30 2 vs 2 vs 2 in both editions (its end seen working)
            else if (sscanf_s(line, "boss %d", &a) == 1) g_sp_boss = a >= 0 && a < 5 ? a : 0;
            else if (sscanf_s(line, "slot %d %d %d %d %d %d", &a, &b, &c, &d, &e, &cp) >= 5 && a >= 0 && a < LOB_N)
                g_lob[a] = {b >= 0 && b < g_lob_nch ? b : 0, c >= 0 ? c : 0, d >= 0 && d < 16 ? d : 0, e ? 1 : 0, a > 0 || cp};
        }
        fclose(f);
    }
    ed_path("CwoRatings.txt", p);
    if (cwo_wfopen(&f, p, L"r") == 0 && f) {
        char line[64];
        int row, r;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf_s(line, "you %d", &r) == 1) g_lob_rate_you = r;
            else if (sscanf_s(line, "%d %d", &row, &r) == 2 && row >= 0 && row < 64) g_lob_rate[row] = r;
        }
        fclose(f);
    }
    int sr = lob_stage_rows();
    if (g_lob_stage < 0 || g_lob_stage >= sr) { g_lob_stage = 0; g_lob_var = 0; }
    for (int t = 0; t < sr && lob_stage_dummy(g_lob_stage); t++) { g_lob_stage = (g_lob_stage + 1) % sr; g_lob_var = 0; }
}
static void lob_save() {
    wchar_t p[MAX_PATH];
    ed_path("CwoLobby.txt", p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# 3 VS 3 LOBBY: stage row variant; slot n character outfit arsenal team cpu\nstage %d %d\n", g_lob_stage, g_lob_var);
    for (int i = 0; i < LOB_N; i++)
        fprintf(f, "slot %d %d %d %d %d %d\n", i, g_lob[i].ch, g_lob[i].outfit, g_lob[i].ars, g_lob[i].team, g_lob[i].cpu);
    fprintf(f, "cpulevel %d\ntype %d\nboss %d\nbrn %d\n", g_cpu_level, g_sp_type, g_sp_boss, g_sp_br_n);
    if (g_lob_pick[0]) fprintf(f, "pick %s\n", g_lob_pick);   // v5.188.32
    fclose(f);
}

static int lob_mode_sp() {                                      // v5.152 "Single Play" in Create a Mode: the rule of 1 vs 1 / 4 fighters
    EdCreator &c = g_edc[EDP_MODE];
    for (int i = 0; i < c.n; i++) if (!strcmp(c.item[i].name, "Single Play")) return i;
    if (c.n >= 8) return -1;
    int k = c.n++;
    ed_defaults(EDP_MODE, c.item[k], "Single Play");
    c.item[k].v[5] = 0;
    ed_save(EDP_MODE);
    return k;
}
// v5.152 Boss Raid in SINGLE PLAY: the game's own Single Play battle list (Ghidra 2026-11-12, FUN_140192a50 = list state 3:
// cursor at state +4, row r >= 1 = preset r - 1 of com\single2.csv, 300 bytes each; a preset = a PLAYER line and the lines
// after it) has the boss presets BOSS0-Peccato .. BOSS3b-DFreia-Phase2; their row = the count of PLAYER lines before + 1
static int sp_boss_row(int boss) {
    static const char *ALIAS[5] = {"BOSS0-Peccato", "BOSS1-Thalasso", "BOSS2-Carcino", "BOSS3-DFreia-Phase1", "BOSS3b-DFreia-Phase2"};
    if (boss < 0 || boss >= 5) return -1;
    wchar_t p[MAX_PATH];
    FILE *f = NULL;
    swprintf_s(p, MAX_PATH, L"%sAssets\\Data\\com\\single2.csv", g_over);
    if (cwo_wfopen(&f, p, L"rb") != 0 || !f) {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH); wchar_t *sl = wcsrchr(exe, L'\\'); if (sl) sl[1] = 0;
        swprintf_s(p, MAX_PATH, L"%sAssets\\Data\\com\\single2.csv", exe);
        if (cwo_wfopen(&f, p, L"rb") != 0 || !f) return -1;
    }
    char line[256];
    int presets = 0, row = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "PLAYER,", 7)) continue;
        const char *a = strchr(line + 7, ',');                   // PLAYER,<slot>,<alias>,...
        if (a) {
            size_t n = strlen(ALIAS[boss]);
            if (!strncmp(a + 1, ALIAS[boss], n) && a[1 + n] == ',') { row = presets + 1; break; }
        }
        presets++;
    }
    fclose(f);
    return row;
}
static void ed_go_page(LONG d) {                                 // v5.152 main menu SINGLE PLAY, OPTIONS > Records / Visions / Stage Music
    InterlockedExchange(&g_ed_ret_page, 0);
    if (d == ED_GO_SINGLE) { InterlockedExchange(&g_sp_entry, 1); lob_open(false); return; }
    InterlockedExchange(&g_sp_entry, 0);
    InterlockedExchange(&g_ed_from_opt, 1);                     // v5.188.2 B goes back to OPTIONS, not the CREATE chooser
    InterlockedExchange(&g_ed_page, d == ED_GO_RECORDS ? EDP_RECORDS : d == ED_GO_VISIONS ? EDP_VISIONS : EDP_MUSIC);
}
static int lob_mode() {                                        // the 3 vs 3 mode of Create a Mode (made when there is none)
    EdCreator &c = g_edc[EDP_MODE];
    for (int i = 0; i < c.n; i++) if (c.item[i].v[5] == 7) return i;
    if (c.n >= 8) return -1;
    int k = c.n++;
    ed_defaults(EDP_MODE, c.item[k], "3 vs 3");
    c.item[k].v[0] = 15; c.item[k].v[1] = 20; c.item[k].v[2] = 3; c.item[k].v[3] = 3; c.item[k].v[4] = 0; c.item[k].v[5] = 7;   // v5.188.3 3 capsules
    ed_save(EDP_MODE);
    return k;
}

static void lob_open(bool boot) {                              // from Create a Mode
    if (!g_lob_loaded) lob_load();
    InterlockedExchange(&g_lob_sel, 0);
    InterlockedExchange(&g_lob_opt, boot ? 1 : 0);             // boot: OPTIONS on Change Stage, then the character ...
    InterlockedExchange(&g_lob_osel, boot ? LO_STAGE : LO_TYPE);
    InterlockedExchange(&g_ed_page, EDP_LOBBY);
}

static void lob_start();
static void lob_change(int item, int d) {                       // item = LOB_OPT index
    int sl = g_lob_sel < LOB_N ? g_lob_sel : 0;
    LobSlot &s = g_lob[sl];
    char msg[96];
    if (item == 0) {                                           // stage: every row and version of Single Play's list but dummies
        int n = lob_stage_rows();
        if (n <= 0) { set_status("The stage list is not ready"); return; }
        for (int t = 0; t <= n; t++) {
            g_lob_var += d;
            if (g_lob_var >= lob_stage_vars(g_lob_stage)) { g_lob_stage = (g_lob_stage + 1) % n; g_lob_var = 0; }
            if (g_lob_var < 0) { g_lob_stage = (g_lob_stage + n - 1) % n; g_lob_var = lob_stage_vars(g_lob_stage) - 1; }
            if (!lob_stage_dummy(g_lob_stage)) break;
        }
    } else if (item == 1) {
        if (sl != 0) { set_status("Slots 2-6 are CPUs; slot 1 can be you or a CPU"); return; }
        s.cpu ^= 1;
    } else if (item == 2) {
        s.ch = (s.ch + d + g_lob_nch) % g_lob_nch;
        if (s.outfit >= g_lob_ch[s.ch].outfits) s.outfit = 0;
    } else if (item == 3) {
        int n = g_lob_ch[s.ch].outfits;
        s.outfit = (s.outfit + d + n) % n;
    } else if (item == 4) {
        int n = lob_ars_count();
        if (n <= 0) { set_status("No arsenals saved yet"); return; }
        s.ars = (s.ars + d + n) % n;
    } else if (item == 5) {
        s.team ^= 1;
    } else if (item == 6) {                                    // the game rule under the 3 vs 3 mode (KO Battle counts 4)
        // v5.188.32 (hourly Create a Mode pass 5, in game: Change Rule cycled the game's 10 rules only - the saved modes
        // (Quick KO, Long fight ...) could not be picked, though Create a Mode says "pick it in the lobby: X > Change Rule"):
        // the list is the game's rules, then every saved mode; a mode's settings (all but its fighters) go onto the lobby's
        // own mode, a game rule puts the lobby's defaults back with that rule
        int k = lob_mode();
        if (k < 0) { set_status("Create a Mode is full (8): delete one"); return; }
        EdCreator &c = g_edc[EDP_MODE];
        int users[8], nu = 0;
        for (int i = 0; i < c.n && nu < 8; i++)
            if (i != k && strcmp(c.item[i].name, "3 vs 3") && strcmp(c.item[i].name, "Single Play")) users[nu++] = i;
        static const int RULES[11] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11};
        int pos = 0, n = 11 + nu;
        if (g_lob_pick[0]) { for (int u = 0; u < nu; u++) if (!strcmp(c.item[users[u]].name, g_lob_pick)) pos = 11 + u; }
        else for (int q = 0; q < 11; q++) if (RULES[q] == c.item[k].v[4]) pos = q;
        pos = (pos + d + n) % n;
        EdItem &m = c.item[k];
        if (pos < 11) {
            if (g_lob_pick[0]) {                               // back from a saved mode: the lobby's defaults
                ed_defaults(EDP_MODE, m, "3 vs 3");
                m.v[0] = 15; m.v[1] = 20; m.v[2] = 3; m.v[3] = 3; m.v[5] = 7;
                g_lob_pick[0] = 0;
            }
            m.v[4] = RULES[pos];
            sprintf_s(msg, "Game rule: %s", ED_RULES[m.v[4]]);
        } else {
            const EdItem &u = c.item[users[pos - 11]];
            for (int f = 0; f < 40; f++) if (f != 5) m.v[f] = u.v[f];
            strncpy_s(g_lob_pick, u.name, _TRUNCATE);
            sprintf_s(msg, "Mode: %s (%d min, life %d, %d lives, %d capsules, %s)", u.name, u.v[0], u.v[1], u.v[2], u.v[3], ED_RULES[ed_mode_rule(u)]);
        }
        ed_save(EDP_MODE);
        if (g_ed_mode_on == k) ed_mode_arm(k);
        set_status(msg);
    } else if (item == 7) {                                    // v5.12 every CPU's level (lobby and Single Play)
        g_cpu_level = (g_cpu_level + d + CPU_LV_N) % CPU_LV_N;
        g_cpu_lv_loaded = true;
        sprintf_s(msg, "CPU level: %s (every CPU of offline Single Play and 3 VS 3)", CPU_LV_NAMES[g_cpu_level]);
        set_status(msg);
    } else return;
    lob_save();
    rumble(1);
}

static int sp_boss_row(int boss);
static bool br_set_on(bool on);
static int lob_mode_sp();
static void lob_start_six();
static void lob_go_common(const char *what) {                     // the lobby hands over to Single Play's lists
    lob_save();
    InterlockedExchange(&g_ed_mode, 0);
    InterlockedExchange(&g_ed_page, g_sp_entry ? EDP_LOBBY : EDP_MODE);   // SINGLE PLAY opens on its lobby next time
    ed_close();
    ed_state_save();
    InterlockedExchange(&g_forge_session, 0);                 // an ordinary Single Play match (no editor)
    InterlockedExchange(&g_six_auto, 1);                       // the loader presses through stage, arsenal and battle lists
    char msg[80];
    sprintf_s(msg, "SINGLE PLAY: %s - loading the match ...", what);
    set_status(msg);
    rumble(1);
}
static void lob_start() {                                       // v5.152 SINGLE PLAY: by game type
    if (g_el) { g_sp_type = SP_ONE; g_lob[0].cpu = 0; g_lob[1].cpu = 1; }   // v5.194.0 Effect Lab: you against one target
    g_lob_trio = g_sp_type == SP_TRIO;                         // v5.188.24/26 teams A A B B C C (set on the match's slots, the 3 vs 3 teams kept)
    g_lob_ffa = g_sp_type == SP_ROYALE;                        // v5.188.26 Battle Royale: each alone, until one is left
    // v5.188.29 (in game 02:46: a 3-fighter Battle Royale on Single Play's own 4-fighter battle ended as soon as player 1 went
    // down - the owner's 'Battle Royale continues until one fighter is left'): every Battle Royale size takes the lobby path
    if (g_sp_type == SP_SIX || g_sp_type == SP_TRIO || g_sp_type == SP_ROYALE) {
        br_set_on(false); InterlockedExchange(&g_lob_go_n, g_sp_type == SP_ROYALE ? g_sp_br_n : 6); lob_start_six(); return;
    }
    if (lob_stage_rows() <= 0) { set_status("The stage list is not ready: back out and open SINGLE PLAY again"); return; }
    char st[64];
    lob_stage_name(g_lob_stage, g_lob_var, st, sizeof(st));
    if (g_sp_type == SP_RAID) {                                 // the game's own boss row of Single Play + your CPU allies (Boss Raid)
        int row = sp_boss_row(g_sp_boss);
        if (row < 0) { set_status("Boss Raid: that boss is not in the game's Single Play list (com\\single2.csv)"); rumble(2); return; }
        br_set_on(true);
        ed_mode_arm(-1);
        InterlockedExchange(&g_sp_boss_row, row);
        logf("SINGLE PLAY: Boss Raid - %s (battle list row %d), %d CPU allies, stage row %d variant %d (%s)", SP_BOSS[g_sp_boss], row,
             br_allies(), g_lob_stage, g_lob_var, st);
        lob_go_common("Boss Raid");
        return;
    }
    br_set_on(false);
    int n = sp_slots(g_sp_type);
    int k = lob_mode_sp(), k6 = lob_mode();
    if (k >= 0) {                                                // the lobby's rule (Change Rule) on the Single Play mode
        if (k6 >= 0) g_edc[EDP_MODE].item[k].v[4] = g_edc[EDP_MODE].item[k6].v[4];
        if (k6 >= 0 && g_lob_pick[0]) for (int f = 0; f < 40; f++) if (f != 5) g_edc[EDP_MODE].item[k].v[f] = g_edc[EDP_MODE].item[k6].v[f];   // v5.188.32 the picked mode's settings for 1 vs 1 / 4 fighters too
        ed_save(EDP_MODE);
        ed_mode_arm(k);
    } else ed_mode_arm(-1);
    memcpy(g_lob_go_slots, g_lob, sizeof(g_lob));
    for (int i = 0; i < n; i++)                                  // the teams of the game type
        g_lob_go_slots[i].team = g_sp_type == SP_TAG ? (i < 2 ? 0 : 1) : i;
    InterlockedExchange(&g_lob_go_n, n);
    InterlockedExchange(&g_lob_go, 1);
    logf("SINGLE PLAY: start - %s, %d fighters, stage row %d variant %d (%s)", SP_TYPES[g_sp_type], n, g_lob_stage, g_lob_var, st);
    for (int i = 0; i < n; i++) {
        char an[24];
        lob_ars_name(g_lob[i].ars, an, sizeof(an));
        logf("SINGLE PLAY: slot %d %s %s outfit %d, arsenal %s, team %c", i + 1, g_lob[i].cpu ? "CPU" : "you", g_lob_ch[g_lob[i].ch].name,
             g_lob[i].outfit, an, 'A' + g_lob_go_slots[i].team);
    }
    lob_go_common(SP_TYPES[g_sp_type]);
}
static void lob_start_six() {
    if (g_sixf2_state != 1) { set_status("3 vs 3 is not ready: six fighters is off or was refused (see the log)"); rumble(2); return; }
    if (lob_stage_rows() <= 0) { set_status("The stage list is not ready: back out and open Edgar's Dream again"); return; }
    int k = lob_mode();
    if (k < 0) { set_status("Create a Mode is full (8): delete one, then start again"); rumble(2); return; }
    int na = 0;
    for (int i = 0; i < LOB_N; i++) na += g_lob[i].team == 0;
    if (!g_lob_trio && !g_lob_ffa && (na == 0 || na == LOB_N)) { set_status("Both teams need fighters: X > Change Team"); rumble(2); return; }
    if (g_ed_mode_on != k) ed_mode_arm(k);
    lob_save();
    memcpy(g_lob_go_slots, g_lob, sizeof(g_lob));
    for (int i = 0; i < LOB_N; i++) if (g_lob_trio) g_lob_go_slots[i].team = i / 2; else if (g_lob_ffa) g_lob_go_slots[i].team = i;   // v5.188.26
    InterlockedExchange(&g_lob_go, 1);
    InterlockedExchange(&g_ed_mode, 0);
    InterlockedExchange(&g_ed_page, g_sp_entry ? EDP_LOBBY : EDP_MODE);   // SINGLE PLAY opens on its lobby, else Create a Mode
    ed_close();
    ed_state_save();
    InterlockedExchange(&g_forge_session, 0);                 // an ordinary Single Play match (no editor)
    InterlockedExchange(&g_six_auto, 1);                       // the loader presses through stage, arsenal and battle lists
    char st[64];
    lob_stage_name(g_lob_stage, g_lob_var, st, sizeof(st));
    logf("3 VS 3 LOBBY: start - stage row %d variant %d (%s), rule %s", g_lob_stage, g_lob_var, st, ED_RULES[g_edc[EDP_MODE].item[k].v[4]]);
    for (int i = 0; i < LOB_N; i++) {
        char an[24];
        lob_ars_name(g_lob[i].ars, an, sizeof(an));
        logf("3 VS 3 LOBBY: slot %d %s %s outfit %d (row %d), arsenal %s, team %c", i + 1, g_lob[i].cpu ? "CPU" : "you",
             g_lob_ch[g_lob[i].ch].name, g_lob[i].outfit, g_lob_ch[g_lob[i].ch].row, an, 'A' + g_lob[i].team);
    }
    set_status("3 VS 3: loading the match ...");
    rumble(1);
}

static void lob_input(WORD pressed) {
    if (!g_lob_loaded) lob_load();
    lob_chars_ready();
    if (g_lob_opt) {                                           // OPTIONS: Up/Down item, L/R value, LB/RB slot, A on Start / Close
        int o = g_lob_osel;
        if (pressed & BTN_DOWN) o = (o + 1) % LOB_OPTS;
        if (pressed & BTN_UP) o = (o + LOB_OPTS - 1) % LOB_OPTS;
        InterlockedExchange(&g_lob_osel, o);
        int ns = sp_slots(g_sp_type);
        int sl = g_lob_sel < ns ? g_lob_sel : 0;
        if (pressed & BTN_RB) InterlockedExchange(&g_lob_sel, (sl + 1) % ns);
        if (pressed & BTN_LB) InterlockedExchange(&g_lob_sel, (sl + ns - 1) % ns);
        int d = (pressed & BTN_LEFT) ? -1 : (pressed & (BTN_RIGHT | BTN_A)) ? 1 : 0;
        if (d && o == LO_TYPE) {                                // v5.152 the game type
            g_sp_type = (g_sp_type + d + SP_N) % SP_N;
            if (g_lob_sel >= sp_slots(g_sp_type)) InterlockedExchange(&g_lob_sel, 0);
            lob_save(); rumble(1);
        } else if (d && o >= LO_STAGE && o <= LO_CPU) {
            if (o == LO_TEAM && g_sp_type == SP_ROYALE) {             // v5.188.26 Battle Royale: how many fighters (2-6)
                g_sp_br_n = g_sp_br_n + d < 2 ? 6 : g_sp_br_n + d > 6 ? 2 : g_sp_br_n + d;
                if (g_lob_sel >= g_sp_br_n) InterlockedExchange(&g_lob_sel, 0);
                lob_save(); rumble(1);
            } else if (o == LO_TEAM && g_sp_type != SP_SIX) set_status(g_sp_type == SP_TRIO ? "2 vs 2 vs 2: teams A, A, B, B, C, C" : "Teams come from the game type (3 vs 3: you choose them)");
            else lob_change(o - LO_STAGE, d);
        } else if (d && o == LO_BOSS) { g_sp_boss = (g_sp_boss + d + 5) % 5; lob_save(); rumble(1); }
        else if (d && o == LO_ALLIES) { br_allies_step(d); rumble(1); }
        if ((pressed & BTN_A) && (o == LO_SPECT || o == LO_DRAFT)) {   // v5.152 their pages, B comes back here
            InterlockedExchange(&g_lob_opt, 0);
            InterlockedExchange(&g_ed_ret_page, EDP_LOBBY);
            InterlockedExchange(&g_ed_page, o == LO_SPECT ? EDP_SPECT : EDP_DRAFT);
            if (o == LO_SPECT) InterlockedExchange(&g_ed_row, g_ed_rows_sel[EDP_SPECT]);
            rumble(1);
            return;
        }
        if (o == LO_START && (pressed & BTN_A)) { InterlockedExchange(&g_lob_opt, 0); lob_start(); return; }
        if ((pressed & (BTN_B | BTN_X)) || (o == LO_CLOSE && (pressed & BTN_A))) InterlockedExchange(&g_lob_opt, 0);
        return;
    }
    int ns = sp_slots(g_sp_type);
    if (g_lob_sel > ns && g_lob_sel != LOB_START) InterlockedExchange(&g_lob_sel, 0);
    int cur = g_lob_sel == LOB_START ? ns : g_lob_sel;          // the slots of the game type, then START GAME
    if (pressed & BTN_DOWN) cur = (cur + 1) % (ns + 1);
    if (pressed & BTN_UP) cur = (cur + ns) % (ns + 1);
    if (pressed & (BTN_DOWN | BTN_UP)) InterlockedExchange(&g_lob_sel, cur == ns ? LOB_START : cur);
    if (pressed & BTN_X) {                                     // the settings of the slot under the cursor (slot 1 from START)
        if (g_lob_sel >= LOB_N) InterlockedExchange(&g_lob_sel, 0);
        InterlockedExchange(&g_lob_opt, 1); InterlockedExchange(&g_lob_osel, LO_CHAR); rumble(1); return;
    }
    if (pressed & BTN_A) {
        if (g_lob_sel == LOB_START) { lob_start(); return; }
        InterlockedExchange(&g_lob_opt, 1); InterlockedExchange(&g_lob_osel, LO_CHAR); rumble(1); return;   // A on a slot: its settings
    }
    if (pressed & BTN_B) {
        if (g_sp_entry) {                                       // v5.152 SINGLE PLAY: B leaves to the game's lists (B again: the menu)
            InterlockedExchange(&g_sp_entry, 0);
            el_off("B on the lobby");                           // v5.194.0
            ed_close(); ed_state_save();
            set_status("SINGLE PLAY closed - back to the main menu ...");
            InterlockedExchange(&g_sp_leave, 1);                // v5.188.31 the game's own list is left for the player (cwo_forge.inl)
            return;
        }
        InterlockedExchange(&g_ed_page, EDP_MODE); return;
    }
}

template <class PUT, class BOX> static void lob_draw(PUT &put, BOX &box, int x, int y) {
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, TEAL = 0xFF80E0D0, YEL = 0xFFFFE040;
    const DWORD ROW = 0xFF5A1E28, ROWON = 0xFF8A2E3C, BAR = 0xFF0E3A38, PANEL = 0xFF061614, EDGE = 0xFF2E7A70;
    if (!g_lob_loaded) lob_load();
    lob_chars_ready();
    char line[96], an[24], st[64];
    int k = lob_mode();
    const EdItem *m = k >= 0 ? &g_edc[EDP_MODE].item[k] : NULL;
    auto solid = [&](int px, int py, DWORD col, int w, int h) { box(px, py, col, w, h); box(px, py, col, w, h); };   // twice: opaque
    solid(x - 8, y - 4, BAR, 236, 20);
    char ttl[48];
    if (g_el) sprintf_s(ttl, "EFFECT LAB   -   pick a stage");
    else sprintf_s(ttl, "SINGLE PLAY   -   %s", SP_TYPES[g_sp_type]);
    put(x + 150, y, ttl, WHITE);
    y += 22;
    if (g_lob_opt) {                                           // OPTIONS in the rows' place: a bordered panel, nothing under it
        int sl = g_lob_sel < sp_slots(g_sp_type) ? g_lob_sel : 0;
        const LobSlot &s = g_lob[sl];
        int h = 26 + 19 * LOB_OPTS + 24;
        solid(x - 10, y - 4, EDGE, 237, h + 4);                // the border
        solid(x - 8, y - 2, PANEL, 235, h);
        solid(x - 8, y - 2, BAR, 235, 22);
        sprintf_s(line, "OPTIONS  -  slot %d  (%s)", sl + 1, sl == 0 && !s.cpu ? "you" : "CPU");
        put(x + 150, y + 1, line, WHITE);
        int oy = y + 28;
        for (int i = 0; i < LOB_OPTS; i++) {
            char v[48] = "";
            if (i == LO_TYPE) sprintf_s(v, "< %s >", SP_TYPES[g_sp_type]);
            if (i == LO_STAGE) { lob_stage_name(g_lob_stage, g_lob_var, st, sizeof(st)); sprintf_s(v, "< %.30s >", st); }
            if (i == LO_PLAYER) { if (sl == 0) sprintf_s(v, "< %s >", s.cpu ? "CPU (you watch)" : "You (PLAYER 1)"); else strcpy_s(v, "CPU"); }
            if (i == LO_CHAR) sprintf_s(v, "< %s >", g_lob_ch[s.ch].name);
            if (i == LO_OUTFIT) sprintf_s(v, "< %d of %d >", s.outfit, g_lob_ch[s.ch].outfits);
            if (i == LO_ARS) { lob_ars_name(s.ars, an, sizeof(an)); sprintf_s(v, "< %s >", an); }
            if (i == LO_TEAM && g_sp_type == SP_ROYALE) sprintf_s(v, "< %d fighters, each alone >", g_sp_br_n);
            else if (i == LO_TEAM) { if (g_sp_type == SP_SIX) sprintf_s(v, "< Team %c >", 'A' + s.team); else if (g_sp_type == SP_TRIO) sprintf_s(v, "Team %c", 'A' + g_lob_sel / 2); else strcpy_s(v, "by game type"); }
            if (i == LO_RULE) sprintf_s(v, "< %s >", g_lob_pick[0] ? g_lob_pick : m ? ED_RULES[ed_mode_rule(*m)] : "-");   // v5.188.32 a saved mode by name
            if (i == LO_CPU) sprintf_s(v, "< %s >", CPU_LV_NAMES[g_cpu_level]);
            if (i == LO_BOSS) { if (g_sp_type == SP_RAID) sprintf_s(v, "< %s >", SP_BOSS[g_sp_boss]); else strcpy_s(v, "(Boss Raid)"); }
            if (i == LO_ALLIES) { if (g_sp_type == SP_RAID) sprintf_s(v, "< %d >", br_allies()); else strcpy_s(v, "(Boss Raid)"); }
            if (i == LO_SPECT) strcpy_s(v, "A: open");
            if (i == LO_DRAFT) strcpy_s(v, "A: open");
            bool on = i == g_lob_osel;
            if (on) solid(x - 6, oy - 2, ROWON, 233, 17);
            sprintf_s(line, "%-18s %s", i == LO_TEAM && g_sp_type == SP_ROYALE ? "Fighters" : LOB_OPT[i], v);
            put(x + 4, oy, line, on ? YEL : (i == LO_START ? GOLD : WHITE));
            oy += 19;
        }
        put(x + 4, oy + 4, "Up/Down choose   L/R change   LB/RB other slot   B close", GOLD);
        return;
    }
    put(x, y, "Character        Players    Arsenal          Team  Rating", TEAL);
    y += 18;
    for (int i = 0; i < sp_slots(g_sp_type); i++) {             // v5.152 the game type's fighters
        const LobSlot &s = g_lob[i];
        bool on = i == g_lob_sel;
        solid(x - 8, y - 2, on ? ROWON : ROW, 236, 17);
        lob_ars_name(s.ars, an, sizeof(an));
        char who[16];
        if (i == 0 && !s.cpu) strcpy_s(who, "PLAYER 1"); else sprintf_s(who, "CPU %d", i + 1);
        sprintf_s(line, "%-13.13s%2d  %-10s %-16.16s  %c    %4d", g_lob_ch[s.ch].name, s.outfit, who, an, 'A' + (g_sp_type == SP_TRIO ? i / 2 : g_sp_type == SP_ROYALE || g_sp_type == SP_ONE ? i : g_sp_type == SP_TAG ? (i < 2 ? 0 : 1) : s.team),   // v5.188.36 1 vs 1 showed A / A
                  lob_slot_rating(i));
        put(x, y, line, on ? YEL : WHITE);
        y += 18;
    }
    y += 8;
    solid(x - 8, y - 4, BAR, 236, 20);
    put(x + 160, y, "SESSION  INFO", WHITE);
    y += 22;
    if (g_sp_type == SP_RAID) sprintf_s(line, "Game Type:  Boss Raid  (%s, %d CPU allies)", SP_BOSS[g_sp_boss], br_allies());
    else if (g_sp_type == SP_ROYALE) sprintf_s(line, "Game Type:  Battle Royale  (%d fighters, the last one standing wins)", g_sp_br_n);
    else if (g_sp_type == SP_TRIO && m) sprintf_s(line, "Game Type:  2 vs 2 vs 2  (%d min, %d lives, %d capsules)", m->v[0], m->v[2], m->v[3]);
    else if (g_sp_type != SP_SIX) sprintf_s(line, "Game Type:  %s", SP_TYPES[g_sp_type]);
    else if (m) sprintf_s(line, "Game Type:  3 vs 3  (%d min, %d lives, %d capsules)", m->v[0], m->v[2], m->v[3]);
    else strcpy_s(line, "Game Type:  3 vs 3  (Create a Mode is full)");
    put(x, y, line, TEAL);
    y += 16;
    if (g_lob_pick[0] && m) sprintf_s(line, "Game Rule:  %s (%d min, life %d, %d %s)", g_lob_pick, m->v[0], m->v[1], m->v[2], m->v[2] == 1 ? "life" : "lives");   // v5.188.39 the picked mode (said Standard Battle)
    else sprintf_s(line, "Game Rule:  %s", m ? ED_RULES[ed_mode_rule(*m)] : "Standard Battle");
    put(x, y, line, TEAL);
    y += 16;
    lob_stage_name(g_lob_stage, g_lob_var, st, sizeof(st));
    sprintf_s(line, "Stage:      %s", st);
    put(x, y, line, TEAL);
    y += 22;
    bool on = g_lob_sel == LOB_START;                          // START GAME: an entry of its own
    solid(x + 150, y - 3, on ? ROWON : ROW, 80, 19);
    put(x + 196, y, "START  GAME", on ? YEL : WHITE);
    y += 26;
    put(x + 80, y, "A Select   X Change Settings   B Back", GOLD);
}

// the 6 fighters of a lobby match, at FUN_140128cc0's entry (cwo_sixf2.inl): player 1 keeps the launcher's record (you),
// slots 2-6 are CPUs built from the battle's CPU record (level, program) with their own name, character, outfit, team,
// deck counts and aura factor. Returns false when there is no CPU record to build from (then the match keeps its own).
static char *g_lob_t_ars, *g_lob_t_gs;                         // self-test stand-ins (arsenals, GSDATA)
static int g_lob_t_max, g_lob_t_nars;
static float g_lob_t_def;
// v5.5 (owner 18:30: "nobody moved, nobody fought. They just kind of stood there"): the template is the battle's CPU, which
// Single Play builds from the top row of com\single2.csv - "Basic / Enemy", program chkprog000.ssb (a check program: the
// practice dummy) and deck plan DECK000; the file log showed com\ai\chkprog000.ssb opened 5 times. Every lobby CPU now
// runs the game's own fighting program (single2.csv "Program-6": eneprog006.ssb; FUN_14017b350 loads com\ai\<record
// +0x46>) and no deck plan (+0x31 empty: FUN_14017b350 then loads no com\deck file), so it plays the arsenal of its slot.
static const char LOB_CPU_PROG[] = "eneprog006.ssb";
// v5.12 CPU level (owner 2026-10-26). FUN_14017b020 copies bytes 1-9 of
// com\param\enemyparam.epa record N (N = CPU record +0x18) into the CPU program's chance slots 30-38; the install adds
// records 82-85 (13_Independent_Research_Tools/epa_tool.py). Used only when the loaded table *0x1404c0188 has them.
static bool cpu_level_ready(int rec) {
    __try {
        const signed char *t = g_cpu_lv_test_tbl ? g_cpu_lv_test_tbl : *(const signed char **)(g_base + 0x4c0188);
        return t && rec > 0 && rec < 100 && t[10 * rec + 7] != 0;        // slot 32 (attack %) of that record
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void cpu_level_load() {                                  // the saved level, before the lobby was ever opened
    if (g_cpu_lv_loaded) return;
    g_cpu_lv_loaded = true;
    wchar_t p[MAX_PATH];
    ed_path("CwoLobby.txt", p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[128];
    int a;
    while (fgets(line, sizeof(line), f))
        if (sscanf_s(line, "cpulevel %d", &a) == 1) g_cpu_level = a >= 0 && a < CPU_LV_N ? a : 0;
    fclose(f);
}
// v5.14 the Brutal levels also run com\ai\cwoprog006.ssb (13_Independent_Research_Tools/build_cwoprog.py: eneprog006 with
// IGT-057's move timers, the incoming-attack check every tick and 2-tick defend cooldowns) when the install has it; the
// game opens com\ai\<record +0x46> by path (FUN_14017b350 -> FUN_140196ee0 -> FUN_14028b7a0, CreateFile2)
static const char CWO_PROG[] = "cwoprog006.ssb";
static int g_cwoprog_test = -1;                                 // self-test: 0 not installed, 1 installed
static bool cwoprog_present() {
    if (g_cwoprog_test >= 0) return g_cwoprog_test == 1;
    static int have = -1;
    if (have < 0) {
        wchar_t exe[MAX_PATH], p[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wchar_t *sl = wcsrchr(exe, L'\\');
        if (sl) sl[1] = 0;
        swprintf_s(p, MAX_PATH, L"%sAssets\\Data\\com\\ai\\%S", exe, CWO_PROG);
        have = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
        logf("CPU level: %s %s", CWO_PROG, have ? "is installed - the Brutal levels run it" : "is not installed - the Brutal levels run eneprog006");
    }
    return have == 1;
}
static void cpu_level_apply(char *c, int lv) {
    if (lv <= 0 || lv >= CPU_LV_N) return;
    static bool told;
    if (!cpu_level_ready(CPU_LV_REC[lv])) { if (!told) { told = true; logf("CPU level: enemyparam.epa has no record %d - the game's own level kept", CPU_LV_REC[lv]); } return; }
    *(int *)(c + 0x18) = CPU_LV_REC[lv];
    if (lv >= 2 && cwoprog_present()) { memset(c + 0x46, 0, 0x16); strcpy_s(c + 0x46, 0x16, CWO_PROG); return; }
    // v5.14.2 (lifted programs, 07_Combat_and_Skills/ai_2026-10-26/lifted): eneprog000-002 and their _nodef / _noNdef
    // variants never read slots 30-38, so the row alone would change nothing: Tough runs them as eneprog006
    if (!_strnicmp(c + 0x46, "eneprog00", 9) && c[0x46 + 9] >= '0' && c[0x46 + 9] <= '2') {
        logf("CPU level: %s reads no personality row - %s runs eneprog006.ssb", c + 0x46, CPU_LV_NAMES[lv]);
        memset(c + 0x46, 0, 0x16);
        strcpy_s(c + 0x46, 0x16, LOB_CPU_PROG);
    }
}
static int cpu_level_pick() { return g_cpu_level > 0 ? g_cpu_level : 2; }   // the lobby's rule: Brutal unless CPU Level says otherwise
static void lob_cpu_program(char *c) {
    memset(c + 0x31, 0, 0x15);
    memset(c + 0x46, 0, 0x16);
    strcpy_s(c + 0x46, 0x16, LOB_CPU_PROG);
    // v5.36 (owner: 3 VS 3 "all players stay still"; log 19:45: every CPU had eneprog006 loaded but personality row 0, the
    // empty one, and never decided anything; "create one explicitly for the 3v3 that forces all of them to fight brutally"):
    // the lobby's CPUs are Brutal unless the lobby's CPU Level names another level
    static bool told;
    int lv = g_cpu_level > 0 ? g_cpu_level : 2;
    if (g_cpu_level <= 0 && !told) { told = true; logf("CPU level: the lobby's CPUs run Brutal (row %d) - 'the game's own' left them on the empty row 0", CPU_LV_REC[2]); }
    cpu_level_apply(c, lv);
}
static bool lob_fill(char *rec, ULONG_PTR *ptrs, float *aura, char cpu[][0x60], int nskills, bool block1, int nf = LOB_N) {
    int tpl = -1;
    for (int p = 0; p < 4 && tpl < 0; p++) {
        char *r = rec + 0x48 * p;
        if (*(int *)r && *(int *)(r + 0x10) == 0 && *(ULONG_PTR *)(r + 0x20)) tpl = p;
    }
    if (tpl < 0 || !*(int *)rec) return false;
    bool test = g_lob_t_ars != NULL;
    char *ars = test ? g_lob_t_ars : lob_ars_base(), *gs = test ? g_lob_t_gs : *(char **)(g_base + RVA_GSDATA);
    int na = test ? g_lob_t_nars : lob_ars_count(), maxid = test ? g_lob_t_max : *(int *)(*(char **)(g_base + 0x4bdc40) + 0x10);
    float def = test ? g_lob_t_def : *(float *)(g_base + 0x2f7da8);
    char t[0x48];
    memcpy(t, rec + 0x48 * tpl, 0x48);
    char tcpu[0x60];
    memcpy(tcpu, *(void **)(t + 0x20), 0x5c);
    if (nf < 2 || nf > LOB_N) nf = LOB_N;
    for (int i = nf; i < 4; i++) *(int *)(rec + 0x48 * i) = 0;    // v5.152 SINGLE PLAY 1 vs 1 / 4 fighters: the rest stay out
    for (int i = 0; i < nf; i++) {
        const LobSlot &s = g_lob_go_slots[i];
        const LobChar &c = g_lob_ch[s.ch];
        char *r = rec + 0x48 * i;
        if (i == 0 && s.cpu) {                                 // v5.2.2 slot 1 played by the CPU (you watch)
            *(int *)(r + 0xc) = -1;
            *(int *)(r + 0x10) = 0;
            memcpy(cpu[0], tcpu, 0x5c);
            *(int *)(cpu[0] + 4) = 0;
            memset(cpu[0] + 0x20, 0, 0x26);
            strncpy_s(cpu[0] + 0x20, 0x26, c.name, _TRUNCATE);
            lob_cpu_program(cpu[0]);
            *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)cpu[0];
        }
        if (i > 0) {
            memcpy(r, t, 0x48);
            *(int *)r = 1;
            *(int *)(r + 0xc) = -1;
            *(int *)(r + 0x10) = 0;                            // a CPU
            *(int *)(r + 0x14) = 1;
            memcpy(cpu[i], tcpu, 0x5c);
            *(int *)(cpu[i] + 4) = 0;                          // an ordinary fighter (FUN_140128cc0 kind 0)
            memset(cpu[i] + 0x20, 0, 0x26);
            strncpy_s(cpu[i] + 0x20, 0x26, c.name, _TRUNCATE);  // the name the game copies (FUN_1401276e0)
            lob_cpu_program(cpu[i]);
            *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)cpu[i];
            int slot = i;                                      // start points: 1-4 a free one of the first 4; 5-6 their own on
            if (i < 4) {                                       // a six-spawn stage, else a teammate's
                for (int q = 0; q < 4; q++) {
                    bool used = false;
                    for (int j = 0; j < i; j++) used |= *(int *)(rec + 0x48 * j + 0x18) == q;
                    if (!used) { slot = q; break; }
                }
            } else if (!block1) {
                slot = *(int *)(rec + 0x48 * (i - 3) + 0x18);
                for (int j = 0; j < 4; j++) if (g_lob_go_slots[j].team == s.team) { slot = *(int *)(rec + 0x48 * j + 0x18); break; }
            }
            *(int *)(r + 0x18) = slot;
        }
        *(int *)(r + 4) = s.team;
        *(unsigned *)(r + 8) = ((unsigned)s.outfit << 16) | (unsigned)(c.row & 0xFFFF);
        int a = s.ars >= 0 && s.ars < na ? s.ars : 0;
        if (ars && na > 0 && ptrs[i] && nskills > 1) {         // deck counts, as FUN_14014f830
            int *cnt = (int *)ptrs[i], used = 0;
            memset(cnt, 0, (size_t)(nskills - 1) * 4);
            short *ids = (short *)(ars + 100 * a + 0x14);
            for (int k = 0; k < 30; k++)
                if (ids[k] != -1 && ids[k] <= maxid && ids[k] > 0 && ids[k] < nskills - 1) { cnt[ids[k]]++; used++; }
            cnt[0] = 30 - used;
        }
        unsigned short sc = ars && na > 0 ? *(unsigned short *)(ars + 100 * a + 0x50) : 0;
        aura[i] = (unsigned short)(sc - 1) < 3 && gs ? *(float *)(gs + 0x33034 + 4 * (sc - 1)) : def;
        logf("3 VS 3 LOBBY: fighter %d = %s outfit %d (word %08x), team %c, start %d", i + 1, c.name, s.outfit,
             *(unsigned *)(r + 8), 'A' + s.team, *(int *)(r + 0x18));
    }
    return true;
}

static int lob_self_test() {                                   // lob_fill on stand-in records, arsenals and GSDATA
    static char rec[8 * 0x48], cpu0[0x60], cpus[6][0x60], ars[2 * 100], gs[0x33040];
    static int cnt[6][9];
    memset(rec, 0, sizeof(rec)); memset(cpu0, 0, sizeof(cpu0)); memset(ars, 0, sizeof(ars));
    *(int *)rec = 1; *(int *)(rec + 0x10) = 1; *(int *)(rec + 0x18) = 2;               // player 1: you, start 2
    char *r1 = rec + 0x48;
    *(int *)r1 = 1; *(int *)(r1 + 4) = 1; *(int *)(r1 + 0x18) = 0; *(ULONG_PTR *)(r1 + 0x20) = (ULONG_PTR)cpu0;   // the battle's CPU
    *(int *)(cpu0 + 0x18) = 7; strcpy_s(cpu0 + 0x20, 0x26, "Battle CPU"); strcpy_s(cpu0 + 0x46, 0x10, "eneprog");
    strcpy_s(ars + 4, 16, "cityCamo"); strcpy_s(ars + 104, 16, "pal");
    for (int k = 0; k < 30; k++) { *(short *)(ars + 0x14 + 2 * k) = k < 20 ? (short)(1 + k % 5) : -1; *(short *)(ars + 100 + 0x14 + 2 * k) = 3; }
    *(unsigned short *)(ars + 0x50) = 2; *(unsigned short *)(ars + 100 + 0x50) = 0;
    *(float *)(gs + 0x33034 + 4) = 1.25f;
    g_lob_t_ars = ars; g_lob_t_gs = gs; g_lob_t_max = 8; g_lob_t_nars = 2; g_lob_t_def = 1.0f;
    LobChar keep_ch[3]; int keep_n = g_lob_nch; LobSlot keep_s[LOB_N];
    memcpy(keep_ch, g_lob_ch, sizeof(keep_ch)); memcpy(keep_s, g_lob_go_slots, sizeof(keep_s));
    g_lob_ch[0] = {0, 6, "Player"}; g_lob_ch[1] = {2, 16, "Freia"}; g_lob_ch[2] = {28, 4, "DREAMER"};
    g_lob_nch = 3;
    for (int i = 0; i < LOB_N; i++) g_lob_go_slots[i] = {i % 3, i == 1 ? 15 : 0, i & 1, i < 3 ? 0 : 1, i > 0};
    ULONG_PTR ptrs[8] = {0};
    for (int i = 0; i < 6; i++) ptrs[i] = (ULONG_PTR)cnt[i];
    float aura[8] = {0};
    bool ok = lob_fill(rec, ptrs, aura, cpus, 10, false);
    bool words = true, teams = true, names = true, decks = true, auras = true, starts = true, you = true;
    for (int i = 0; ok && i < LOB_N; i++) {
        char *r = rec + 0x48 * i;
        const LobSlot &s = g_lob_go_slots[i];
        words &= *(unsigned *)(r + 8) == (((unsigned)s.outfit << 16) | (unsigned)g_lob_ch[s.ch].row) && *(int *)r == 1;
        teams &= *(int *)(r + 4) == s.team;
        if (i) names &= !strcmp((char *)*(ULONG_PTR *)(r + 0x20) + 0x20, g_lob_ch[s.ch].name) && *(int *)(r + 0x10) == 0 &&
                        *(int *)((char *)*(ULONG_PTR *)(r + 0x20) + 0x18) == 7 && !strcmp((char *)*(ULONG_PTR *)(r + 0x20) + 0x46, LOB_CPU_PROG) &&
                        !*((char *)*(ULONG_PTR *)(r + 0x20) + 0x31);   // v5.5 a fighting program, no deck plan
        int sum = 0;
        for (int k = 0; k < 9; k++) sum += cnt[i][k];
        decks &= sum == 30 && (s.ars == 0 ? cnt[i][1] == 4 && cnt[i][0] == 10 : cnt[i][3] == 30);
        auras &= aura[i] == (s.ars == 0 ? 1.25f : 1.0f);
    }
    you = ok && *(int *)(rec + 0x10) == 1 && *(int *)(rec + 0x18) == 2 && *(ULONG_PTR *)(rec + 0x20) == 0;
    starts = ok && *(int *)(rec + 0x18) == 2 && *(int *)(rec + 0x48 * 1 + 0x18) == 0 && *(int *)(rec + 0x48 * 2 + 0x18) == 1 &&
             *(int *)(rec + 0x48 * 3 + 0x18) == 3 && *(int *)(rec + 0x48 * 4 + 0x18) == 3 &&
             *(int *)(rec + 0x48 * 5 + 0x18) == 3;   // 1-4 all different (yours kept); 5-6: team B's start (4 points)
    // slot 1 as a CPU: its record is a CPU's (human 0, its own CPU record with the character's name)
    memset(rec, 0, sizeof(rec)); *(int *)rec = 1; *(int *)(rec + 0x10) = 1; *(int *)(rec + 0x18) = 2;
    *(int *)r1 = 1; *(int *)(r1 + 4) = 1; *(ULONG_PTR *)(r1 + 0x20) = (ULONG_PTR)cpu0;
    g_lob_go_slots[0].cpu = 1;
    bool cpu1 = lob_fill(rec, ptrs, aura, cpus, 10, false) && *(int *)(rec + 0x10) == 0 &&
                *(ULONG_PTR *)(rec + 0x20) == (ULONG_PTR)cpus[0] && !strcmp(cpus[0] + 0x20, g_lob_ch[g_lob_go_slots[0].ch].name);
    g_lob_go_slots[0].cpu = 0;
    // v5.12 CPU Level Brutal: every CPU record names epa record 83 when the loaded table has it
    static signed char tbl[1004]; memset(tbl, 0, sizeof(tbl)); tbl[10 * 83 + 7] = 100;
    int keep_lv = g_cpu_level; g_cpu_level = 2; g_cpu_lv_test_tbl = tbl;
    memset(rec, 0, sizeof(rec)); *(int *)rec = 1; *(int *)(rec + 0x10) = 1; *(int *)(rec + 0x18) = 2;
    *(int *)r1 = 1; *(int *)(r1 + 4) = 1; *(ULONG_PTR *)(r1 + 0x20) = (ULONG_PTR)cpu0;
    g_cwoprog_test = 1;                                         // v5.14 and the sharper program
    bool level = lob_fill(rec, ptrs, aura, cpus, 10, false);
    for (int i = 1; level && i < LOB_N; i++) level = *(int *)(cpus[i] + 0x18) == 83 && !strcmp(cpus[i] + 0x46, CWO_PROG);
    g_cpu_level = keep_lv; g_cpu_lv_test_tbl = NULL; g_cwoprog_test = -1;
    memset(rec, 0, sizeof(rec)); *(int *)rec = 1; *(int *)(rec + 0x10) = 1;
    bool refused = !lob_fill(rec, ptrs, aura, cpus, 10, false);                      // no CPU record: refused
    g_lob_t_ars = NULL; g_lob_t_gs = NULL;
    memcpy(g_lob_ch, keep_ch, sizeof(keep_ch)); memcpy(g_lob_go_slots, keep_s, sizeof(keep_s)); g_lob_nch = keep_n;
    logf("self-test: 3 VS 3 LOBBY six fighters %s, words %s, teams %s, CPU names %s, decks %s, aura %s, starts %s, player 1 %s, "
         "no CPU refused %s, slot 1 as CPU %s, CPU level %s", ok ? "ok" : "FAILED", words ? "ok" : "FAILED", teams ? "ok" : "FAILED", names ? "ok" : "FAILED",
         decks ? "ok" : "FAILED", auras ? "ok" : "FAILED", starts ? "ok" : "FAILED", you ? "ok" : "FAILED", refused ? "ok" : "FAILED", cpu1 ? "ok" : "FAILED",
         level ? "ok" : "FAILED");
    return ok && words && teams && names && decks && auras && starts && you && refused && cpu1 && level ? 1 : -62;
}

// ---- v5.2.1 ratings after a lobby match. Game facts (Ghidra batch_065): FUN_1401282f0 sets the winning team
// *0x1404bbe04 = -1 and the results *0x1404bc5c0 = -1 at the start; at the end FUN_14012c8c0 (rule != 10) writes every
// fighter's result and *0x1404bbe04 = the one team still standing (by team, fighter +0x1c), or -1 when more than one
// is (time up: a draw). Elo, as online ratings: team averages, K = 32, a draw = half.
static int g_lob_rated = 1;                                    // 1: nothing to rate (rated, or no lobby match yet)
static void lob_rate_save() {
    wchar_t p[MAX_PATH];
    ed_path("CwoRatings.txt", p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# 3 VS 3 LOBBY ratings: 'you' = player 1, then charaset row and rating (everyone starts at 1000)\nyou %d\n", lob_rating_you());
    for (int r = 0; r < 64; r++) if (g_lob_rate[r] > 0) fprintf(f, "%d %d\n", r, g_lob_rate[r]);
    fclose(f);
}
static int lob_elo(int mine, int theirs, float score) {        // the change for one side
    float e = 1.0f / (1.0f + powf(10.0f, (float)(theirs - mine) / 400.0f));
    return (int)floorf(32.0f * (score - e) + 0.5f);
}
static void lob_rate_apply(int winner) {
    // v5.188.26 up to 3 teams (2 vs 2 vs 2; the old [2] arrays were written past their end by team C): each team against the
    // average of the others; the winning team scores 1, the others 0 (no winner: 0.5 each)
    const LobSlot *s = g_lob_go_slots;
    int sum[3] = {0, 0, 0}, n[3] = {0, 0, 0};
    for (int i = 0; i < LOB_N; i++) {
        int t = s[i].team; if (t < 0 || t > 2) continue;
        int r = i == 0 ? lob_rating_you() : lob_rating(g_lob_ch[s[i].ch].row); sum[t] += r; n[t]++;
    }
    int nt = (n[0] > 0) + (n[1] > 0) + (n[2] > 0);
    if (nt < 2) return;
    int avg[3], d[3] = {0, 0, 0};
    for (int t = 0; t < 3; t++) avg[t] = n[t] ? sum[t] / n[t] : 0;
    for (int t = 0; t < 3; t++) {
        if (!n[t]) continue;
        int os = 0, on = 0;
        for (int u = 0; u < 3; u++) if (u != t && n[u]) { os += avg[u]; on++; }
        float score = winner >= 0 && winner < 3 ? (winner == t ? 1.0f : 0.0f) : 0.5f;
        d[t] = lob_elo(avg[t], os / on, score);
    }
    char msg[160];
    for (int i = 0; i < LOB_N; i++) {
        int t = s[i].team, row = g_lob_ch[s[i].ch].row;
        if (t < 0 || t > 2) continue;
        if (i == 0) { int o = lob_rating_you(); g_lob_rate_you = o + d[t] < 100 ? 100 : o + d[t]; logf("3 VS 3 rating: you %d -> %d", o, g_lob_rate_you); }
        else if (row >= 0 && row < 64) {
            int o = lob_rating(row);
            g_lob_rate[row] = o + d[t] < 100 ? 100 : o + d[t];
            logf("3 VS 3 rating: %s (row %d) %d -> %d", g_lob_ch[s[i].ch].name, row, o, g_lob_rate[row]);
        }
    }
    lob_rate_save();
    if (nt == 3 && winner >= 0 && winner < 3) sprintf_s(msg, "2 VS 2 VS 2: Team %c wins - ratings A %+d / B %+d / C %+d", 'A' + winner, d[0], d[1], d[2]);
    else if (nt == 3) sprintf_s(msg, "2 VS 2 VS 2: a draw - ratings A %+d / B %+d / C %+d", d[0], d[1], d[2]);
    else if (winner == 0 || winner == 1) sprintf_s(msg, "3 VS 3: Team %c wins - ratings %+d / Team %c %+d", 'A' + winner, d[winner], 'A' + (winner ^ 1), d[winner ^ 1]);
    else sprintf_s(msg, "3 VS 3: a draw - ratings Team A %+d / Team B %+d", d[0], d[1]);
    set_status(msg);
    logf("%s (team averages %d / %d / %d)", msg, avg[0], avg[1], avg[2]);
}
static void lob_rate_step() {                                  // game thread, every flush
    // v5.188.26 last team standing; v5.188.39 (hourly pass 9, in game: a lobby One-on-One stood at 00:00 with the CPU at 0
    // life - set by hand, the match ended at once): every lobby match but Boss Raid (the game's own boss row decides it)
    if (g_lob_go && g_sp_boss_row < 0 && g_in_match) __try { *(int *)(g_base + 0x4bbec4) = 1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!g_lob_go || !g_six_match || g_lob_nch <= 0 || g_lob_ffa) return;   // Battle Royale: unrated
    // v5.188.26 (in game 02:07: with three teams the match went on after two teams were out): Single Play turns the game's
    // own last-team-standing check off (FUN_140128cc0..: *0x1404bbec4 = 0 for match type 1; FUN_14012c510 only runs
    // FUN_14012c440's team flags when it is set) - for 2 vs 2 vs 2 it is on, as in versus (seen: winner team C, YOU LOSE)
    __try {
        int r0 = *(int *)cwo_gaddr(0x4bc5c0);
        if (g_in_match && r0 == -1 && g_lob_rated) { g_lob_rated = 0; return; }   // a lobby match is on
        if (!g_lob_rated && r0 >= 0) {                          // its results are in
            g_lob_rated = 1;
            lob_rate_apply(*(int *)(g_base + 0x4bbe04));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int lob_rate_self_test() {                              // equal teams: +16 / -16; a draw between equals: 0
    int keep_you = g_lob_rate_you, keep[64];
    memcpy(keep, g_lob_rate, sizeof(keep));
    bool even = lob_elo(1000, 1000, 1.0f) == 16 && lob_elo(1000, 1000, 0.0f) == -16 && lob_elo(1000, 1000, 0.5f) == 0;
    bool upset = lob_elo(900, 1100, 1.0f) > 16 && lob_elo(1100, 900, 1.0f) < 16;   // beating stronger teams pays more
    bool start = lob_rating(5) == 1000 || g_lob_rate[5] > 0;
    g_lob_rate_you = keep_you;
    memcpy(g_lob_rate, keep, sizeof(keep));
    logf("self-test: 3 VS 3 ratings even %s, upset %s, start 1000 %s", even ? "ok" : "FAILED", upset ? "ok" : "FAILED", start ? "ok" : "FAILED");
    return even && upset && start ? 1 : -63;
}

// ---- v5.3 the 3 VS 3 LOBBY inside the game's own lobby (owner 2026-10-25). The menu scripts are patched by
// tools/build_lobby3.py (vsmenu: a 3 vs 3 LOBBY page built from the game's own windows, rows and OPTIONS; mainmenu: the
// hand-over to Split Screen); the slots live here, the scripts read and change them through native 2.8:
//   21 main menu: open Split Screen now?  22 vsmenu start: open the 3 vs 3 lobby?  23 LOBBY builder: build the 3 vs 3 one?
//   24 get (key)  25 set (key, value)  26 start (stage, rule)  27 leave.  key = slot * 8 + field (0 character word
//   colour << 16 | charaset row, 1 arsenal, 2 team, 3 CPU, 4 rating), 100 rule, 101 stage (a row of the stage list).
static ULONGLONG g_l3_t;
static int g_l3_rule, g_l3_live;                                 // g_l3_live: Split Screen still to be 'pressed'
static int lob_ch_of_row(int row) {                              // our character list entry for a charaset row (added if new)
    lob_chars_ready();
    for (int i = 0; i < g_lob_nch; i++) if (g_lob_ch[i].row == row) return i;
    if (g_lob_nch >= 40) return 0;
    LobChar &c = g_lob_ch[g_lob_nch];
    c.row = row; c.outfits = 6; sprintf_s(c.name, "Character %d", row);
    __try {
        char *b = lob_chara();
        if (b) {
            int rows = *(int *)(b + 4), pool = *(int *)(b + 8), rs = rows > 0 ? (pool - 16) / rows : 0;
            if (row < rows && rs >= 0x3C) {
                const char *nm = b + pool + *(int *)(b + 16 + rs * row + 4);
                if (nm[0]) strncpy_s(c.name, nm, _TRUNCATE);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_lob_nch++;
}
static INT64 lob3_native(int op, INT64 *args) {                  // game thread (the menu scripts)
    if (!g_lob_loaded) lob_load();
    INT64 a1 = 0, a2 = 0;
    __try { a1 = args[1]; a2 = args[2]; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    ULONGLONG now = GetTickCount64();
    if (g_l3 == 2 && now - g_l3_t > 20000) { InterlockedExchange(&g_l3, 0); logf("3 VS 3 LOBBY: the hand-over did not happen - off"); }
    switch (op) {
    case 21:                                                     // the main menu, every frame: 'press' MULTIPLAYER MODE
        if (g_l3 == 1) { InterlockedExchange(&g_l3, 2); g_l3_live = 1; g_l3_t = now; logf("3 VS 3 LOBBY: main menu -> MULTIPLAYER"); return 1; }
        if (g_l3 == 3) { InterlockedExchange(&g_l3, 0); logf("3 VS 3 LOBBY: back at the main menu - off"); }
        return 0;
    case 28:                                                     // the MULTIPLAYER list, every frame: 'press' Split Screen
        if (g_l3 == 2 && g_l3_live) { g_l3_live = 0; logf("3 VS 3 LOBBY: MULTIPLAYER -> Split Screen"); return 1; }
        return 0;
    case 22:                                                     // a fresh split-screen menu
        if (g_l3 == 2) { InterlockedExchange(&g_l3, 3); logf("3 VS 3 LOBBY: open"); return 1; }
        return 0;
    case 23: return g_l3 == 3;                                   // the LOBBY builder
    case 24: case 25: {
        int key = (int)a1, set = op == 25;
        INT64 v = a2;
        if (key == 100) { if (set) g_l3_rule = (int)v; return g_l3_rule; }
        if (key == 101) {
            if (set && v >= 0 && v < lob_stage_rows()) { if (g_lob_stage != (int)v) g_lob_var = 0; g_lob_stage = (int)v; lob_save(); }
            return g_lob_stage;
        }
        int slot = key / 8, f = key % 8;
        if (slot < 0 || slot >= LOB_N) return 0;
        LobSlot &s = g_lob[slot];
        if (!set) {
            switch (f) {
            case 0: return ((INT64)s.outfit << 16) | (g_lob_ch[s.ch].row & 0xFFFF);
            case 1: return s.ars;
            case 2: return s.team;
            case 3: return slot == 0 ? s.cpu : 1;
            case 4: return lob_slot_rating(slot);
            }
            return 0;
        }
        switch (f) {
        case 0: s.ch = lob_ch_of_row((int)(v & 0xFFFF)); s.outfit = (int)((v >> 16) & 0xFF); break;
        case 1: if (v >= 0 && v < 16) s.ars = (int)v; break;
        case 2: s.team = v ? 1 : 0; break;
        case 3: if (slot == 0) s.cpu = v ? 1 : 0; break;
        }
        lob_save();
        return 1;
    }
    case 26: {                                                   // START GAME (a1 = stage row, a2 = rule)
        int na = 0;
        for (int i = 0; i < LOB_N; i++) na += g_lob[i].team == 0;
        if (g_sixf2_state != 1 || na == 0 || na == LOB_N) {
            logf("3 VS 3 LOBBY: start refused (%s)", g_sixf2_state != 1 ? "six fighters off" : "both teams need fighters");
            return 0;
        }
        if (a1 >= 0 && a1 < lob_stage_rows()) { if (g_lob_stage != (int)a1) g_lob_var = 0; g_lob_stage = (int)a1; }
        int k = lob_mode();
        if (k < 0) { logf("3 VS 3 LOBBY: start refused (Create a Mode is full)"); return 0; }
        if (a2 >= 0 && a2 < 12 && a2 != 10) { g_edc[EDP_MODE].item[k].v[4] = (int)a2; ed_save(EDP_MODE); }
        ed_mode_arm(k);
        lob_save();
        memcpy(g_lob_go_slots, g_lob, sizeof(g_lob));
        InterlockedExchange(&g_lob_go, 1);
        InterlockedExchange(&g_six_auto, 1);                     // the loader passes Single Play's lists (cover on)
        InterlockedExchange(&g_l3, 0);
        char st[64];
        lob_stage_name(g_lob_stage, g_lob_var, st, sizeof(st));
        logf("3 VS 3 LOBBY: start from the game's lobby - stage row %d (%s), rule %lld", g_lob_stage, st, a2);
        for (int i = 0; i < LOB_N; i++) {
            char an[24];
            lob_ars_name(g_lob[i].ars, an, sizeof(an));
            logf("3 VS 3 LOBBY: slot %d %s %s color %d (row %d), arsenal %s, team %c", i + 1, i == 0 && !g_lob[i].cpu ? "you" : "CPU",
                 g_lob_ch[g_lob[i].ch].name, g_lob[i].outfit, g_lob_ch[g_lob[i].ch].row, an, 'A' + g_lob[i].team);
        }
        return 1;
    }
    case 27: InterlockedExchange(&g_l3, 0); logf("3 VS 3 LOBBY: left (B)"); return 1;
    }
    return 0;
}
static void lob3_request() {                                     // Edgar's Dream > Create a Mode > 3 VS 3 LOBBY
    if (!g_lob_loaded) lob_load();
    InterlockedExchange(&g_l3, 1);
    g_l3_t = GetTickCount64();
    InterlockedExchange(&g_ed_mode, 0);
    InterlockedExchange(&g_ed_page, EDP_MODE);
    ed_close();
    ed_state_save();
    InterlockedExchange(&g_forge_session, 0);
    logf("3 VS 3 LOBBY: asked from Edgar's Dream - to the main menu, then the game's lobby");
    set_status("3 VS 3: opening the lobby ...");
}

static int lob3_self_test() {                                    // native 2.8 ops 21-27 on the loader's slots (no game)
    LobSlot keep[LOB_N];
    memcpy(keep, g_lob, sizeof(keep));
    LONG keep_l3 = g_l3;
    int keep_stage = g_lob_stage;
    INT64 a[3];
    auto call = [&](int op, INT64 x = 0, INT64 y = 0) { a[0] = op; a[1] = x; a[2] = y; return lob3_native(op, a); };
    InterlockedExchange(&g_l3, 1);
    bool hand = call(21) == 1 && g_l3 == 2 && call(21) == 0 && g_l3 == 2 && call(28) == 1 && call(28) == 0 && call(23) == 0 &&
                call(22) == 1 && g_l3 == 3 &&
                call(23) == 1 && call(22) == 0;
    INT64 word = (3LL << 16) | 5;                                 // Meister, colour 4
    bool rt = call(25, 2 * 8 + 0, word) == 1 && call(24, 2 * 8 + 0) == word &&
              call(25, 2 * 8 + 1, 1) == 1 && call(24, 2 * 8 + 1) == 1 &&
              call(25, 2 * 8 + 2, 1) == 1 && call(24, 2 * 8 + 2) == 1 &&
              call(25, 0 * 8 + 3, 1) == 1 && call(24, 0 * 8 + 3) == 1 && call(24, 3 * 8 + 3) == 1 &&
              call(25, 0 * 8 + 3, 0) == 1 && call(24, 0 * 8 + 3) == 0 &&
              call(24, 0 * 8 + 4) == lob_rating_you() && call(24, 2 * 8 + 4) == lob_rating(5);
    for (int i = 0; i < LOB_N; i++) g_lob[i].team = 0;            // everyone on one team: start refused
    bool refused = call(26, 0, 0) == 0;
    bool leave = call(27) == 1 && g_l3 == 0 && call(23) == 0;
    memcpy(g_lob, keep, sizeof(keep));
    g_lob_stage = keep_stage;
    InterlockedExchange(&g_l3, keep_l3);
    lob_save();
    logf("self-test: 3 VS 3 game lobby hand-over %s, slot get/set %s, start refused on one team %s, leave %s",
         hand ? "ok" : "FAILED", rt ? "ok" : "FAILED", refused ? "ok" : "FAILED", leave ? "ok" : "FAILED");
    return hand && rt && refused && leave ? 1 : -64;
}
