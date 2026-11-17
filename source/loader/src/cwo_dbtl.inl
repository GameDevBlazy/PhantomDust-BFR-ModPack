// ---- v5.11 Download Battle choices (owner 2026-10-26; online / scoreboard exception "I made an exception to this request only", AGENTS.md).
// Game facts (Ghidra batches 075a / 075b / 075c on PDUWP 9ba7411b, capstone on the clean exe, 2026-10-26):
//  - Scoreboard > Download Battle runs menu\livedbtl_en.ssb: initmenu calls native 4.841 (FUN_140215e70) and then
//    4.661 = FUN_140214770, which builds the battle; the LOADING page then waits on 4.460 (FUN_140129fb0, battle ready).
//  - FUN_140214770: player 1 = record 0 of the fighter table 0x1404bc420 (template 0x1402f83e0, character word +8 = 0,
//    the Player), with your arsenal DAT_1403de844 (written to profile +0x3b08); the opponent = record 1 (template
//    0x1402f8530, word +8 = 23: the Guard robot), driven by the CPU record its +0x20 points at (0x1405b18b0: +0xc = 23,
//    life 20, the downloaded player's name, eneprog006.ssb) with the downloaded arsenal (DAT_1404bc930, DAT_1404bc8f4).
//  - The stage is the immediate of `mov dword [rsp+0x28], 2` (c7 44 24 28 02 00 00 00) at 0x1402149e9: FUN_140128990's
//    sixth argument -> DAT_1404bc8dc -> DAT_1403e0304 and RDX of FUN_1401d1950 (0x140128c27), which builds the stage set
//    in the same call (FUN_1401d1a60 -> FUN_1401d8430(stage)). The fighters' words are read later (FUN_140128cc0).
//  - The stage list 0x140509ba0 (rows of 0x28, +0x18 stage id; *0x14050a5d0 rows) is the one the Xbox Live LEVEL LIST
//    uses too (4.576 = FUN_140213c00).
//  - Native group 4's array is at 0x1403f0090 (group table 0x1402ddd00); its entry 661 holds FUN_140214770.
// The loader holds 4.661 back and shows the choices over the LOADING page; A calls the game's own FUN_140214770 with the
// stage immediate set for that one call (bytes checked at install, put back right after), your arsenal index set
// before and the two characters set after. B starts the game's own battle unchanged.
static const ULONG_PTR RVA_NATIVES_G4 = 0x3f0090, RVA_DBTL_FN = 0x214770, RVA_DBTL_IMM = 0x2149e9, RVA_DBTL_ARS = 0x3de844;
static const int DBTL_NATIVE = 661, DBTL_ROWS = 11;            // v5.68 + Line-up, Fighter 3, Fighter 4
static const unsigned char DBTL_IMM_BYTES[8] = {0xc7, 0x44, 0x24, 0x28, 0x02, 0x00, 0x00, 0x00};
typedef INT64(__fastcall *PFN_DBTL)(INT64 *);
static PFN_DBTL g_db_orig;
static bool g_db_ok;
static int g_db_row;
struct DbChoice { int stage_row, you, you_outfit, opp, opp_outfit, ars, opp_level, lineup, ex3, ex4; };   // you / opp: g_lob_ch index, opp -1 = Guard
// v5.68 line-ups (owner 2026-11-02, AGENTS.md "Scoreboard line-ups and HUD faces"): lineup 0 1v1, 1 1v2, 2 1v3, 3 2v2 (fighter 3
// your CPU ally); ex3 / ex4 = the added fighters' characters, 0 random, k + 1 = g_lob_ch[k]
static const char *DB_LINEUPS[] = {"1 vs 1 (the game's)", "1 vs 2", "1 vs 3", "2 vs 2 (a CPU ally)"};
static DbChoice g_db = {-1, 0, 0, -1, 0, -1, 0};
static unsigned char *g_db_t_imm;                                // self-test stand-ins
static char *g_db_t_rec;
static int *g_db_t_ars, g_db_t_stage;

static void dbtl_path(wchar_t *p) { swprintf_s(p, MAX_PATH, L"%sCwoDownloadBattle.txt", g_dir); }
static void dbtl_save() {
    wchar_t p[MAX_PATH];
    dbtl_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# Download Battle choices (loader): stage_row you you_outfit opp opp_outfit arsenal opp_level lineup fighter3 fighter4\n%d %d %d %d %d %d %d %d %d %d\n",
            g_db.stage_row, g_db.you, g_db.you_outfit, g_db.opp, g_db.opp_outfit, g_db.ars, g_db.opp_level, g_db.lineup, g_db.ex3, g_db.ex4);
    fclose(f);
}
static void dbtl_load() {
    wchar_t p[MAX_PATH];
    dbtl_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        DbChoice c = {-1, 0, 0, -1, 0, -1, 0, 0, 0, 0};
        if (line[0] != '#' && sscanf_s(line, "%d %d %d %d %d %d %d %d %d %d", &c.stage_row, &c.you, &c.you_outfit, &c.opp, &c.opp_outfit, &c.ars, &c.opp_level, &c.lineup, &c.ex3, &c.ex4) >= 6) g_db = c;
    }
    fclose(f);
}

static int dbtl_stage_id(int row) {
    if (g_db_t_stage) return g_db_t_stage;
    __try { return *(int *)(g_base + 0x509ba0 + 0x28 * row + 0x18); } __except (EXCEPTION_EXECUTE_HANDLER) { return 2; }
}
static void dbtl_fix() {                                          // keep every choice in range (lists can change)
    lob_chars_ready();
    int sr = lob_stage_rows(), na = lob_ars_count();
    if (g_db.stage_row < 0 || g_db.stage_row >= sr || lob_stage_dummy(g_db.stage_row)) {
        g_db.stage_row = -1;
        for (int r = 0; r < sr; r++) if (!lob_stage_dummy(r) && dbtl_stage_id(r) == 2) { g_db.stage_row = r; break; }   // Highway
    }
    if (g_db.you < 0 || g_db.you >= g_lob_nch) g_db.you = 0;
    if (g_db.opp < -1 || g_db.opp >= g_lob_nch) g_db.opp = -1;
    if (g_db.you_outfit < 0 || g_db.you_outfit >= g_lob_ch[g_db.you].outfits) g_db.you_outfit = 0;
    if (g_db.opp < 0 || g_db.opp_outfit < 0 || g_db.opp_outfit >= g_lob_ch[g_db.opp].outfits) g_db.opp_outfit = 0;
    g_db.opp_level = 0;                                          // v5.14 not offered (see dbtl_apply_start)
    if (g_db.lineup < 0 || g_db.lineup > 3) g_db.lineup = 0;
    if (g_db.ex3 < 0 || g_db.ex3 > g_lob_nch) g_db.ex3 = 0;
    if (g_db.ex4 < 0 || g_db.ex4 > g_lob_nch) g_db.ex4 = 0;
    if (g_db.ars < 0 || g_db.ars >= na) {
        int cur = -1;
        __try { cur = *(int *)(g_base + RVA_DBTL_ARS); } __except (EXCEPTION_EXECUTE_HANDLER) { cur = -1; }
        g_db.ars = cur >= 0 && cur < na ? cur : 0;
    }
}

static INT64 __fastcall dbtl_native(INT64 *) {                  // 4.661 from livedbtl's initmenu: show the choices first
    if (g_db_state == 0) {
        dbtl_load();
        dbtl_fix();
        g_db_row = DBTL_ROWS - 2;                                // the cursor on START
        InterlockedExchange(&g_db_state, 1);
        logf("download battle: held back for the choices");
    }
    return 0;                                                    // the LOADING page waits on 4.460 meanwhile
}

static bool dbtl_install() {
    void **slot = (void **)(g_base + RVA_NATIVES_G4 + 8 * DBTL_NATIVE);
    __try {
        if (*slot != (void *)(g_base + RVA_DBTL_FN)) { logf("download battle: refused, native 4.661 holds %p", *slot); return false; }
        if (memcmp((void *)(g_base + RVA_DBTL_IMM), DBTL_IMM_BYTES, 8)) { logf("download battle: refused, the stage instruction differs"); return false; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    g_db_orig = (PFN_DBTL)*slot;
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return false;
    *slot = (void *)dbtl_native;
    VirtualProtect(slot, sizeof(void *), old, &old);
    return g_db_ok = true;
}

// v5.68 the added fighters (game thread, right after the game's FUN_140214770 built records 0 / 1, before FUN_140128cc0
// creates the fighters - as the characters below). Game facts (PD Ghidra full pass 065, MODLOG 2026-11-02): FUN_140214770
// gives each fighter its deck counts with FUN_14014f830(school count, fighter, the 30 skill ids) - the opponent from the
// downloaded arsenal (ids 0x1404bc8f4, school count 0x1404bc930), you from profile (*0x1404bec10) + 0x3a30 + 100 x arsenal
// (+0x14 ids, +0x50 school count) - and its aura factor at 0x1404bc958 + 4 x fighter (GSDATA + 0x33034 by school count,
// else 1.0); the record / CPU record layout as cwo_gametype.inl (FUN_140214770's own CPU fighter). The result script
// (livedbtlres) sends no rating or win count - only the optional arsenal feedback the player answers (FUN_140180460).
static const ULONG_PTR RVA_DB_DECK = 0x14f830, RVA_DB_DL_IDS = 0x4bc8f4, RVA_DB_DL_SC = 0x4bc930, RVA_DB_AURA = 0x4bc958, RVA_DB_PROFILE = 0x4bec10;
typedef void(__fastcall *PFN_DB_DECK)(int, int, void *);
static char g_db_cpu[2][0x60];
static PFN_DB_DECK g_db_t_deck;                                   // self-test stand-in
static void dbtl_lineup(char *rec) {
    int lu = g_db.lineup;
    if (lu < 1 || lu > 3) return;
    int add = lu == 1 ? 1 : 2;
    char *p0 = rec, *p1 = rec + 0x48;
    if (!*(int *)p0 || !*(int *)p1) { logf("download battle: line-up %s refused - the battle has no opponent record", DB_LINEUPS[lu]); return; }
    int t0 = *(int *)(p0 + 4), t1 = *(int *)(p1 + 4);
    if (t1 == t0) { t1 = t0 == 0 ? 1 : 0; *(int *)(p1 + 4) = t1; }
    int used = 0;
    for (int i = 0; i < 2; i++) { int st = *(int *)(rec + 0x48 * i + 0x18); if (st >= 0 && st < 4) used |= 1 << st; }
    int taken[4] = {*(int *)(p0 + 8) & 0xFFFF, *(int *)(p1 + 8) & 0xFFFF}, nt = 2;
    int pool = g_lob_nch < 15 ? g_lob_nch : 15;
    static unsigned seed;
    if (!seed) seed = (unsigned)GetTickCount() | 1;
    char *gs = g_db_t_rec ? NULL : *(char **)(g_base + RVA_GSDATA);
    PFN_DB_DECK deck = g_db_t_deck ? g_db_t_deck : (PFN_DB_DECK)(g_base + RVA_DB_DECK);
    for (int k = 0; k < add; k++) {
        int i = 2 + k;
        char *r = rec + 0x48 * i, *c = g_db_cpu[k];
        bool ally = lu == 3 && k == 0;
        int pick = k == 0 ? g_db.ex3 : g_db.ex4, ch = 0;
        if (pick > 0 && pick <= g_lob_nch) ch = pick - 1;
        else for (int tries = 0; tries < 40; tries++) {               // random: a select-screen fighter not in the battle yet
            seed = seed * 1103515245u + 12345u;
            ch = pool > 0 ? (int)((seed >> 8) % (unsigned)pool) : 0;
            bool dup = false;
            for (int q = 0; q < nt; q++) dup |= g_lob_ch[ch].row == taken[q];
            if (!dup) break;
        }
        const LobChar &lc = g_lob_ch[ch];
        if (nt < 4) taken[nt++] = lc.row;
        int slot = 0;
        for (int q = 0; q < 4; q++) if (!(used & (1 << q))) { slot = q; used |= 1 << q; break; }
        memset(c, 0, 0x60);
        *(int *)c = 1; *(int *)(c + 8) = 1;
        *(int *)(c + 0xc) = lc.row;
        *(int *)(c + 0x10) = slot;
        *(int *)(c + 0x14) = 20; *(int *)(c + 0x18) = 20;
        strncpy_s(c + 0x20, 0x11, lc.name, _TRUNCATE);
        strcpy_s(c + 0x46, 0x16, "eneprog006.ssb");                  // FUN_140214770's own CPU program (no deck plan)
        memcpy(r, p1, 0x48);
        *(int *)r = 1;
        *(int *)(r + 4) = ally ? t0 : t1;
        *(unsigned *)(r + 8) = (unsigned)(lc.row & 0xFFFF);
        *(int *)(r + 0xc) = -1;
        *(int *)(r + 0x10) = 0;
        *(int *)(r + 0x14) = 1;
        *(int *)(r + 0x18) = slot;
        *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)c;
        int sc = 0;
        void *ids = NULL;
        __try {
            if (ally) {
                char *prof = g_db_t_rec ? NULL : *(char **)(g_base + RVA_DB_PROFILE);
                char *a = prof ? *(char **)(prof + 0x3a30) + 100 * (g_db.ars >= 0 ? g_db.ars : 0) : NULL;
                if (a) { sc = *(short *)(a + 0x50); ids = a + 0x14; }
            } else if (!g_db_t_rec) { sc = *(unsigned short *)(g_base + RVA_DB_DL_SC); ids = (void *)(g_base + RVA_DB_DL_IDS); }
            if (ids || g_db_t_deck) deck(sc, i, ids);
            if (!g_db_t_rec) *(float *)(g_base + RVA_DB_AURA + 4 * i) = (unsigned)(sc - 1) < 3 && gs ? *(float *)(gs + 0x33034 + 4 * (sc - 1)) : 1.0f;
        } __except (EXCEPTION_EXECUTE_HANDLER) { logf("download battle: fighter %d's arsenal could not be set", i + 1); }
        logf("download battle: %s - fighter %d = CPU %s, %s, start %d, %s arsenal", DB_LINEUPS[lu], i + 1, lc.name, ally ? "your team" : "the opponent's team",
             slot, ally ? "your" : "the downloaded");
    }
}

// the start, with the choices (game thread): stage for this one call, arsenal before, characters after
static void dbtl_apply_start() {
    int stage = g_db.stage_row >= 0 || g_db_t_stage ? dbtl_stage_id(g_db.stage_row) : 2;
    if (stage <= 0) stage = 2;                                   // FUN_1401d1950 treats 0 as "Highway" anyway
    int *ars = g_db_t_ars ? g_db_t_ars : (int *)(g_base + RVA_DBTL_ARS);
    if (g_db.ars >= 0) *ars = g_db.ars;
    unsigned char *imm = g_db_t_imm ? g_db_t_imm : (unsigned char *)(g_base + RVA_DBTL_IMM);
    DWORD old = 0;
    bool patched = g_db_t_imm || VirtualProtect(imm + 4, 4, PAGE_EXECUTE_READWRITE, &old);
    if (patched) { *(int *)(imm + 4) = stage; if (!g_db_t_imm) FlushInstructionCache(GetCurrentProcess(), imm + 4, 4); }
    if (g_db_orig) g_db_orig(NULL);
    if (patched) {
        *(int *)(imm + 4) = 2;
        if (!g_db_t_imm) { FlushInstructionCache(GetCurrentProcess(), imm + 4, 4); VirtualProtect(imm + 4, 4, old, &old); }
    }
    char *rec = g_db_t_rec ? g_db_t_rec : (char *)cwo_gaddr(RVA_FIGHTERS);
    const LobChar &y = g_lob_ch[g_db.you];
    *(int *)(rec + 8) = (g_db.you_outfit << 16) | y.row;
    if (g_db.opp >= 0) {
        const LobChar &o = g_lob_ch[g_db.opp];
        *(int *)(rec + 0x48 + 8) = (g_db.opp_outfit << 16) | o.row;
        char *cpu = *(char **)(rec + 0x48 + 0x20);
        if (cpu) *(int *)(cpu + 0xc) = o.row;
    }
    dbtl_lineup(rec);                                            // v5.68 1 vs 2 / 1 vs 3 / 2 vs 2
    // v5.14: no opponent level here (5.12.0 had one) - the online / scoreboard exception covers only the owner's own
    // choices (stage, characters, arsenal), so the opponent keeps the game's own CPU row and program
    logf("download battle: started - stage %d, you %s (outfit %d), opponent %s (outfit %d), arsenal %d%s", stage, y.name,
         g_db.you_outfit + 1, g_db.opp >= 0 ? g_lob_ch[g_db.opp].name : "Guard (the game's)", g_db.opp_outfit + 1, g_db.ars + 1,
         g_db.ars < 0 ? " (the game's)" : "");
}

// v5.97 a Download Battle match is on (AGENTS.md "New game rules, NPC characters and the Download Battle spectator camera":
// the free camera may be used there, this machine's camera only)
// g_db_match is declared in cwo_freecam.inl (included first)
static void dbtl_step() {                                        // CwoOverlayFrame, game thread
    static LONG seen;
    if (g_db_match) { if (g_in_match) seen = 1; else if (seen) { g_db_match = false; seen = 0; } }
    LONG st = g_db_state;
    if (st != 2 && st != 3) return;
    g_db_match = true; seen = 0;
    InterlockedExchange(&g_db_state, 4);
    __try {
        if (st == 2) dbtl_apply_start();
        else { if (g_db_orig) g_db_orig(NULL); logf("download battle: the game's own battle (Highway, the Player vs Guard)"); }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("download battle: fault while starting"); }
    InterlockedExchange(&g_db_state, 0);
}

static void dbtl_input(WORD pressed) {                           // controller thread, while the choices are shown
    if (pressed & BTN_UP) g_db_row = (g_db_row + DBTL_ROWS - 1) % DBTL_ROWS;
    if (pressed & BTN_DOWN) g_db_row = (g_db_row + 1) % DBTL_ROWS;
    int d = (pressed & BTN_RIGHT) ? 1 : (pressed & BTN_LEFT) ? -1 : 0;
    if (d) {
        int sr = lob_stage_rows(), na = lob_ars_count();
        switch (g_db_row) {
        case 0: for (int t = 0; t < sr && sr > 0; t++) { g_db.stage_row = (g_db.stage_row + d + sr) % sr; if (!lob_stage_dummy(g_db.stage_row)) break; } break;
        case 1: g_db.you = (g_db.you + d + g_lob_nch) % g_lob_nch; g_db.you_outfit = 0; break;
        case 2: { int n = g_lob_ch[g_db.you].outfits; g_db.you_outfit = (g_db.you_outfit + d + n) % n; } break;
        case 3: g_db.opp += d; if (g_db.opp >= g_lob_nch) g_db.opp = -1; if (g_db.opp < -1) g_db.opp = g_lob_nch - 1; g_db.opp_outfit = 0; break;
        case 4: if (g_db.opp >= 0) { int n = g_lob_ch[g_db.opp].outfits; g_db.opp_outfit = (g_db.opp_outfit + d + n) % n; } break;
        case 5: g_db.lineup = (g_db.lineup + d + 4) % 4; break;
        case 6: g_db.ex3 = (g_db.ex3 + d + g_lob_nch + 1) % (g_lob_nch + 1); break;
        case 7: g_db.ex4 = (g_db.ex4 + d + g_lob_nch + 1) % (g_lob_nch + 1); break;
        case 8: if (na > 0) g_db.ars = (g_db.ars + d + na) % na; break;
        }
        rumble(1);
    }
    if ((pressed & BTN_A) && g_db_row == DBTL_ROWS - 1) { InterlockedExchange(&g_db_state, 3); rumble(1); return; }
    if (pressed & BTN_A) { dbtl_save(); InterlockedExchange(&g_db_state, 2); rumble(1); return; }
    if (pressed & BTN_B) { InterlockedExchange(&g_db_state, 3); rumble(1); }
}

static void dbtl_draw() {                                        // the choices over the LOADING page
    if (g_db_state != 1) return;
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
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, YEL = 0xFFFFE040, TEAL = 0xFF80E0D0;
    const DWORD PANEL = 0xF0061614, EDGE = 0xFF2E7A70, BAR = 0xFF0E3A38, ROWON = 0xFF8A2E3C;
    auto box = [&](int px, int py, DWORD col, int w, int h) { *space = 0; *gap = 0; *fh = h; *fw = w; set_rgba(rgba, col); text(px, py, "\x81\xa1"); };
    auto put = [&](int px, int py, const char *s, DWORD col) { *space = 0; *gap = 2; *fh = 14; *fw = 7; set_rgba(rgba, 0xFF000000); text(px + 1, py + 1, s); set_rgba(rgba, col); text(px, py, s); };
    const int X = 120, Y = 60, W = 400, H = 30 + 22 * DBTL_ROWS + 30;
    for (int by = Y - 10; by < Y + H; by += 64)                  // the panel: ~30 big dark squares (the text budget)
        for (int bx = X - 14; bx < X + W; bx += 64) box(bx, by, PANEL, 48, 96);
    box(X - 8, Y - 4, BAR, 220, 24);
    (void)EDGE;
    put(X + 4, Y, "DOWNLOAD BATTLE  -  choose before it loads", WHITE);
    char line[96], v[64], an[24];
    int y = Y + 30;
    for (int i = 0; i < DBTL_ROWS; i++) {
        v[0] = 0;
        const char *label = "";
        switch (i) {
        case 0: label = "Stage"; if (g_db.stage_row >= 0) { lob_stage_name(g_db.stage_row, 0, line, sizeof(line)); sprintf_s(v, "< %.40s >", line); } else strcpy_s(v, "< Highway (the game's) >"); break;
        case 1: label = "You"; sprintf_s(v, "< %s >", g_lob_ch[g_db.you].name); break;
        case 2: label = "  your outfit"; sprintf_s(v, "< %d of %d >", g_db.you_outfit + 1, g_lob_ch[g_db.you].outfits); break;
        case 3: label = "Opponent"; sprintf_s(v, "< %s >", g_db.opp >= 0 ? g_lob_ch[g_db.opp].name : "Guard (the game's)"); break;
        case 4: label = "  its outfit"; if (g_db.opp >= 0) sprintf_s(v, "< %d of %d >", g_db.opp_outfit + 1, g_lob_ch[g_db.opp].outfits); else strcpy_s(v, "-"); break;
        case 5: label = "Line-up"; sprintf_s(v, "< %s >", DB_LINEUPS[g_db.lineup >= 0 && g_db.lineup < 4 ? g_db.lineup : 0]); break;
        case 6: label = g_db.lineup == 3 ? "  your ally" : "  fighter 3";
                if (g_db.lineup < 1) strcpy_s(v, "-"); else sprintf_s(v, "< %s >", g_db.ex3 > 0 ? g_lob_ch[g_db.ex3 - 1].name : "random"); break;
        case 7: label = "  fighter 4";
                if (g_db.lineup < 2) strcpy_s(v, "-"); else sprintf_s(v, "< %s >", g_db.ex4 > 0 ? g_lob_ch[g_db.ex4 - 1].name : "random"); break;
        case 8: label = "Your arsenal"; lob_ars_name(g_db.ars, an, sizeof(an)); sprintf_s(v, "< %d %s >", g_db.ars + 1, an); break;
        case 9: label = "START with these"; break;
        case 10: label = "The game's own (Highway vs Guard)"; break;
        }
        bool on = i == g_db_row;
        if (on) box(X - 6, y - 3, ROWON, 196, 20);
        sprintf_s(line, "%-16s %s", label, v);
        put(X + 4, y, line, on ? YEL : (i >= 9 ? GOLD : WHITE));
        y += 22;
    }
    put(X + 4, y + 6, "Up/Down choose   Left/Right change   A start   B the game's own", TEAL);
    put(X + 4, y + 22, "The opponent keeps the downloaded arsenal and name", GOLD);
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}

static int dbtl_self_test() {                                    // the start on stand-ins: stage, arsenal, both characters
    static unsigned char imm[8];
    static char rec[0x48 * 4], cpu[0x60];
    static int ars;
    memcpy(imm, DBTL_IMM_BYTES, 8);
    memset(rec, 0, sizeof(rec)); memset(cpu, 0, sizeof(cpu));
    *(int *)(rec + 0x48 + 8) = 23; *(char **)(rec + 0x48 + 0x20) = cpu; *(int *)(cpu + 0xc) = 23;
    ars = -1;
    DbChoice keep = g_db;
    PFN_DBTL keep_orig = g_db_orig;
    static int seen;                                             // the stage immediate while the game's function runs
    struct S { static INT64 __fastcall orig(INT64 *) { seen = *(int *)(g_db_t_imm + 4); return 0; } };
    g_db_t_imm = imm; g_db_t_rec = rec; g_db_t_ars = &ars; g_db_orig = S::orig; seen = 0; g_db_t_stage = 5;
    lob_chars_ready();
    *(int *)(cpu + 0x18) = 5; strcpy_s(cpu + 0x46, 0x16, "eneprog006.ssb");
    g_db = {3, 2, 1, 4, 2, 3, 2};                                // stage 5, Freia outfit 2 vs Chunky outfit 3, arsenal 4 (a saved level: ignored)
    dbtl_apply_start();
    bool stage_default = seen == 5 && *(int *)(imm + 4) == 2 && !memcmp(imm, DBTL_IMM_BYTES, 8);   // 5 in the call, 2 after
    bool chars = *(int *)(rec + 8) == ((1 << 16) | g_lob_ch[2].row) && *(int *)(rec + 0x48 + 8) == ((2 << 16) | g_lob_ch[4].row) &&
                 *(int *)(cpu + 0xc) == g_lob_ch[4].row;
    bool arsenal = ars == 3 && *(int *)(cpu + 0x18) == 5 && !strcmp(cpu + 0x46, "eneprog006.ssb");   // v5.14 CPU row + program kept
    g_db = {-1, 0, 0, -1, 0, -1, 1}; ars = 7; *(int *)(rec + 0x48 + 8) = 23; *(int *)(cpu + 0xc) = 23; g_db_t_stage = 0;
    dbtl_apply_start();
    bool guard = seen == 2 && *(int *)(rec + 0x48 + 8) == 23 && *(int *)(cpu + 0xc) == 23 && ars == 7 &&   // Highway, Guard,
                 *(int *)(cpu + 0x18) == 5;                     // arsenal kept
    // v5.68 the 2 vs 2 line-up: your ally on your team, the added enemy on the opponent's, own starts, decks for 3 and 4
    static int decked;
    struct D { static void __fastcall deck(int, int f, void *) { decked |= 1 << f; } };
    memset(rec, 0, sizeof(rec));
    *(int *)rec = 1; *(int *)(rec + 4) = 0; *(int *)(rec + 0x18) = 1;
    *(int *)(rec + 0x48) = 1; *(int *)(rec + 0x48 + 4) = 0; *(int *)(rec + 0x48 + 0x18) = 3; *(char **)(rec + 0x48 + 0x20) = cpu;
    decked = 0; g_db_t_deck = D::deck;
    g_db = {-1, 0, 0, 1, 0, -1, 0, 3, 3, 0};                     // 2 vs 2, your ally = Freia (g_lob_ch[2]), fighter 4 random
    dbtl_apply_start();
    char *a2 = rec + 0x48 * 2, *a3 = rec + 0x48 * 3;
    bool lineup = *(int *)a2 == 1 && *(int *)a3 == 1 && *(int *)(rec + 0x48 + 4) == 1 && *(int *)(a2 + 4) == 0 && *(int *)(a3 + 4) == 1 &&
                  (*(int *)(a2 + 8) & 0xFFFF) == g_lob_ch[2].row && *(int *)(a2 + 0x18) == 0 && *(int *)(a3 + 0x18) == 2 &&
                  *(int *)(a2 + 0x10) == 0 && *(char **)(a2 + 0x20) == g_db_cpu[0] && decked == 0x0C;
    g_db_t_deck = NULL;
    g_db = keep; g_db_orig = keep_orig; g_db_t_imm = NULL; g_db_t_rec = NULL; g_db_t_ars = NULL; g_db_t_stage = 0;
    logf("self-test: download battle stage in the call + put back %s, characters %s, arsenal %s, game's own kept %s, 2 vs 2 line-up %s", stage_default ? "ok" : "FAILED",
         chars ? "ok" : "FAILED", arsenal ? "ok" : "FAILED", guard ? "ok" : "FAILED", lineup ? "ok" : "FAILED");
    return stage_default && chars && arsenal && guard && lineup ? 1 : -41;
}
