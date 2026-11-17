// ---- v5.18 Change Game Type in the split-screen lobby (owner 2026-10-27). The game's split-screen lobby is One-on-One only (vsmenu SESSION INFO row 0 is the
// fixed text "Game Type: One-on-One"). Lobby X > Change Game Type (tools/build_game_type_menu.py) lists One-on-One, Tag
// Team, Battle Royale and 3 vs 3; the two players keep their own slots and the loader adds CPU fighters for the others:
//   Tag Team: player 1 + a CPU against player 2 + a CPU;   Battle Royale: player 1, player 2 and two CPUs, each alone;
//   3 vs 3: player 1 + 2 CPUs against player 2 + 2 CPUs (the six-fighter build, cwo_sixf2.inl).
// Each CPU: a random character of the select screen (not one of the players') in a random outfit, a random arsenal of
// player 1's list, the game's fighting program and the CPU Level of the 3 VS 3 LOBBY.
// Game facts (Ghidra batch_065, capstone, 2026-10-27):
//  - FUN_140214770 (Download Battle) builds a CPU fighter from nothing: its setup record (fighter table 0x1404bc420, 0x48
//    bytes: +0 in, +4 team, +8 character word, +0xc pad (-1 for a CPU, as the 3 VS 3 LOBBY), +0x10 human 0, +0x14 1,
//    +0x18 start point, +0x20 the CPU record) and the CPU record it points at (0x1405b18b0, 0x5c bytes: +0 1, +4 0
//    (ordinary fighter), +8 1, +0xc character row, +0x10 start point, +0x14 20, +0x18 20 (enemyparam row), +0x20 name,
//    +0x31 deck plan "" (none: it plays the arsenal's counts), +0x46 "eneprog006.ssb");
//  - FUN_1401907a0: the stage's 4 start points, each fighter its own;
//  - the split-screen battle is an offline versus with two humans (records 0 and 1, +0x10 = 1) built before FUN_140128cc0
//    creates the fighters (create_hook -> sixf_before_create), the same place the 3 VS 3 fighters are added.
// v5.145 (owner 2026-11-10): row 4 Map Test (tools/build_map_test_row.py adds
// it to the lobby's GAME TYPE list): no CPUs are added (as One-on-One), and during the match the loader keeps bit 3 (0x8) of
// *0x140530034 clear - Ghidra full pass 065, FUN_1401c8110 (the frame draw, from the step FUN_1401c85c0): with that bit
// clear it draws ONE view (*0x140530060 = 0, view 0 = player 1's camera) on the whole screen; with it set view 0 and view 1
// (the split). Offline only (split screen is offline).
static const char *GT_NAMES[] = {"One-on-One", "Tag Team", "Battle Royale", "3 vs 3", "Map Test"};
static const int GT_N = 5, GT_ONE = 0, GT_TAG = 1, GT_ROYALE = 2, GT_SIX = 3, GT_MAP = 4;
static volatile LONG g_gt;                                     // the split-screen lobby's game type (CwoGameType.txt)
static bool g_gt_loaded;
static char g_gt_cpu[6][0x60];                                 // the added CPUs' records
static unsigned g_gt_t_seed;                                   // self-test: a fixed choice sequence
static int g_gt_t_six = -1;                                    // self-test: six fighters available (1) or not (0)

// v5.31 (owner 2026-10-27): the CPUs are picked when the lobby is drawn for a game type (not at the start any more), so the
// LOBBY window lists every fighter of the match - player 1 and their CPUs, then player 2 and theirs (Battle Royale: the
// two players, then the two CPUs) - and the match uses exactly those CPUs. The lobby script asks with ops 57-60
// (tools/build_lobby_slots.py): 57(0) = the fighters in the match (0: the game's own two-row lobby), 57(row) = who that
// row is (-1 player 1, -2 player 2, 2..5 the CPU fighter), 58(k) = CPU k's character word, 60(k) = its arsenal (player 1's
// list, as the game's 2.149(34, n) names it). The players' characters are the menu's variables 2 / 3 (draw_lobb).
struct GtPick { int ch, outfit, ars; };
static GtPick g_gt_pick[6];                                    // fighters 2..5 of the match
static int g_gt_pick_type = -1, g_gt_pick_p1 = -1, g_gt_pick_p2 = -1;
static const int GT_ORDER_TAG[4] = {-1, 2, -2, 3}, GT_ORDER_ROY[4] = {-1, -2, 2, 3}, GT_ORDER_SIX[6] = {-1, 2, 4, -2, 3, 5};
static int gt_players(int type) { return type == GT_SIX ? 6 : type == GT_TAG || type == GT_ROYALE ? 4 : 2; }
static unsigned gt_rand();
// pick the CPUs for a game type: characters of the select screen that neither player has (and not twice), an outfit,
// one of player 1's named arsenals (the same rules gt_fill used at the start)
static void gt_pick(int type, int p1row, int p2row) {
    lob_chars_ready();
    int pool = g_lob_nch < 15 ? g_lob_nch : 15;
    char *ars = g_lob_t_ars ? g_lob_t_ars : lob_ars_base();
    int na = g_lob_t_ars ? g_lob_t_nars : lob_ars_count();
    int named[64], nn = 0;
    for (int a = 0; ars && a < na && a < 64; a++) if (ars[100 * a + 4]) named[nn++] = a;
    int taken[8] = {p1row, p2row}, nt = 2;
    for (int i = 2; i < gt_players(type); i++) {
        int ch = 0;
        for (int tries = 0; tries < 40; tries++) {
            ch = pool > 0 ? (int)(gt_rand() % (unsigned)pool) : 0;
            bool dup = false;
            for (int k = 0; k < nt; k++) dup |= pool > 0 && g_lob_ch[ch].row == taken[k];
            if (!dup) break;
        }
        const LobChar &lc = g_lob_ch[pool > 0 ? ch : 0];
        taken[nt++] = lc.row;
        g_gt_pick[i] = {pool > 0 ? ch : 0, lc.outfits > 1 ? (int)(gt_rand() % (unsigned)lc.outfits) : 0, nn > 0 ? named[gt_rand() % (unsigned)nn] : 0};
    }
    g_gt_pick_type = type; g_gt_pick_p1 = p1row; g_gt_pick_p2 = p2row;
    for (int i = 2; i < gt_players(type); i++)
        logf("split-screen %s: lobby row for fighter %d = CPU %s outfit %d, arsenal %d", GT_NAMES[type], i + 1,
             g_lob_ch[g_gt_pick[i].ch].name, g_gt_pick[i].outfit + 1, g_gt_pick[i].ars + 1);
}
static int g_gt_t_vars[2] = {-1, -1};                          // self-test: the players' characters
static int gt_menu_char(int var) {                             // the lobby's player character (menu variable 2 / 3)
    if (g_gt_t_vars[0] >= 0) return g_gt_t_vars[var - 2];
    __try {
        ULONG_PTR menu = *(ULONG_PTR *)(g_base + 0x533c58);
        return menu ? (int)(*(INT64 *)(menu + 0x10430 + 8 * (ULONG_PTR)var) & 0xFFFF) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void gt_path(wchar_t *p) { swprintf_s(p, MAX_PATH, L"%sCwoGameType.txt", g_dir); }
static void gt_load() {
    if (g_gt_loaded) return;
    g_gt_loaded = true;
    wchar_t p[MAX_PATH];
    gt_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[96];
    int a;
    while (fgets(line, sizeof(line), f))
        if (sscanf_s(line, "gametype %d", &a) == 1 && a >= 0 && a < GT_N) InterlockedExchange(&g_gt, a);
    fclose(f);
}
static void gt_save() {
    wchar_t p[MAX_PATH];
    gt_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# split-screen lobby game type (loader): 0 One-on-One, 1 Tag Team, 2 Battle Royale, 3 3 vs 3, 4 Map Test\ngametype %ld\n", g_gt);
    fclose(f);
}

static INT64 gt_native(int op, INT64 *args) {                  // game thread (vsmenu's lobby and its GAME TYPE list)
    gt_load();
    switch (op) {
    case 40: return (INT64)(ULONG_PTR)GT_NAMES[g_gt >= 0 && g_gt < GT_N ? g_gt : 0];    // SESSION INFO's Game Type
    case 41: { INT64 k = args[1]; return k >= 0 && k < GT_N ? (INT64)(ULONG_PTR)GT_NAMES[k] : 0; }
    case 42: return g_gt;                                                                // the list's cursor
    case 43: {
        INT64 k = args[1];
        if (k < 0 || k >= GT_N) return 0;
        InterlockedExchange(&g_gt, (LONG)k);
        g_gt_pick_type = -1;                                    // v5.31 new CPUs for the new type
        gt_save();
        logf("split-screen lobby: game type %s", GT_NAMES[k]);
        return 1;
    }
    case 57: {                                                  // v5.31 the LOBBY rows
        int type = g_gt;
        bool six = g_gt_t_six >= 0 ? g_gt_t_six == 1 : g_sixf2_state == 1;
        if (type == GT_SIX && !six) type = GT_TAG;              // as gt_fill
        if (type <= GT_ONE || type >= GT_N || type == GT_MAP) return 0;   // v5.145 Map Test: the game's own two rows
        INT64 r = args[1];
        if (r == 0) {
            int p1 = gt_menu_char(2), p2 = gt_menu_char(3);
            if (g_gt_pick_type != type || p1 != g_gt_pick_p1 || p2 != g_gt_pick_p2) gt_pick(type, p1, p2);
            return gt_players(type);
        }
        const int *order = type == GT_SIX ? GT_ORDER_SIX : type == GT_TAG ? GT_ORDER_TAG : GT_ORDER_ROY;
        return r >= 1 && r <= gt_players(type) ? order[r - 1] : -99;
    }
    case 58: { INT64 k = args[1]; if (k < 2 || k > 5 || g_gt_pick_type < 0) return 0;
               const GtPick &p = g_gt_pick[k]; return ((INT64)p.outfit << 16) | (g_lob_ch[p.ch].row & 0xFFFF); }
    case 59: return (INT64)(ULONG_PTR)"CPU";
    case 60: { INT64 k = args[1]; return k >= 2 && k <= 5 && g_gt_pick_type >= 0 ? g_gt_pick[k].ars : 0; }
    }
    return 0;
}

static unsigned gt_rand() {
    if (g_gt_t_seed) { g_gt_t_seed = g_gt_t_seed * 1103515245u + 12345u; return (g_gt_t_seed >> 8) & 0x7fff; }
    return (unsigned)rand();
}

// create_hook (sixf_before_create), game thread: a split-screen battle (two humans) with another game type gets its CPU
// fighters. rec = the setup records, ptrs / aura = the per-fighter deck counts and aura factors. Returns the fighters added.
static int gt_fill(char *rec, ULONG_PTR *ptrs, float *aura, int nskills, bool block1) {
    gt_load();
    int type = g_gt;
    if (type <= GT_ONE || type >= GT_N || type == GT_MAP) return 0;   // v5.145 Map Test adds no CPUs
    char *p1 = rec, *p2 = rec + 0x48;
    if (!*(int *)p1 || !*(int *)p2 || !*(int *)(p1 + 0x10) || !*(int *)(p2 + 0x10)) {        // two humans only
        logf("split-screen %s: not two players in this match - no CPUs added", GT_NAMES[type]); return 0;
    }
    // v5.116.1 (owner 2026-11-08; log
    // 08:19 / 08:20: lobby rows picked, setup records 3-4 still empty): without the six-fighter build the game's table holds 4
    // records (0x1404bc420 + 0x48 i), so records 5-6 were the game's next data (not zero) and read as "the battle has its own
    // others". Only the records that exist are looked at.
    bool six_tab = g_gt_t_six >= 0 ? g_gt_t_six == 1 : g_sixf2_state == 1;
    int nrec = six_tab ? 6 : 4;
    for (int i = 2; i < nrec; i++) if (*(int *)(rec + 0x48 * i)) {
        logf("split-screen %s: record %d is already used - no CPUs added", GT_NAMES[type], i + 1); return 0;
    }
    bool six = g_gt_t_six >= 0 ? g_gt_t_six == 1 : g_sixf2_state == 1;
    if (type == GT_SIX && !six) { logf("split-screen lobby: 3 vs 3 needs the six-fighter build (off) - Tag Team instead"); type = GT_TAG; }
    int add = type == GT_SIX ? 4 : 2;
    int t1 = *(int *)(p1 + 4), t2 = *(int *)(p2 + 4);
    if (t2 == t1) { t2 = t1 == 0 ? 1 : 0; *(int *)(p2 + 4) = t2; }
    int free_t[2], nf = 0;                                     // Battle Royale: two teams nobody has
    for (int t = 0; t < 4 && nf < 2; t++) if (t != t1 && t != t2) free_t[nf++] = t;
    lob_chars_ready();
    bool test = g_lob_t_ars != NULL;
    if (!test) cpu_level_load();
    char *ars = test ? g_lob_t_ars : lob_ars_base(), *gs = test ? g_lob_t_gs : *(char **)(g_base + RVA_GSDATA);
    int na = test ? g_lob_t_nars : lob_ars_count(), maxid = test ? g_lob_t_max : *(int *)(*(char **)(g_base + 0x4bdc40) + 0x10);
    float def = test ? g_lob_t_def : *(float *)(g_base + 0x2f7da8);
    int named[64], nn = 0;                                     // player 1's arsenals that have a name (as FUN_140192a50)
    for (int a = 0; ars && a < na && a < 64; a++) if (ars[100 * a + 4]) named[nn++] = a;
    int taken[6], nt = 0;                                      // characters in use: the players' and the CPUs' so far
    taken[nt++] = *(int *)(p1 + 8) & 0xFFFF;
    taken[nt++] = *(int *)(p2 + 8) & 0xFFFF;
    int used = 0;                                              // start points in use (the stage's first 4)
    for (int i = 0; i < 2; i++) { int s = *(int *)(rec + 0x48 * i + 0x18); if (s >= 0 && s < 4) used |= 1 << s; }
    int pool = g_lob_nch < 15 ? g_lob_nch : 15;                // the select screen's 15 fighters
    for (int i = 2; i < 2 + add; i++) {
        char *r = rec + 0x48 * i, *c = g_gt_cpu[i];
        int team = type == GT_ROYALE ? free_t[i - 2] : (i % 2 == 0 ? t1 : t2);
        bool picked = g_gt_pick_type == type && g_gt_pick[i].ch >= 0 && g_gt_pick[i].ch < (pool > 0 ? pool : 1);   // v5.31 the lobby's CPU
        int ch = picked ? g_gt_pick[i].ch : 0;
        for (int k = 0; picked && k < nt; k++) if (pool > 0 && g_lob_ch[ch].row == taken[k]) picked = false;
        for (int tries = 0; !picked && tries < 40; tries++) {
            ch = pool > 0 ? (int)(gt_rand() % (unsigned)pool) : 0;
            bool dup = false;
            for (int k = 0; k < nt; k++) dup |= pool > 0 && g_lob_ch[ch].row == taken[k];
            if (!dup) break;
        }
        const LobChar &lc = g_lob_ch[pool > 0 ? ch : 0];
        if (nt < 6) taken[nt++] = lc.row;
        int outfit = picked ? g_gt_pick[i].outfit : lc.outfits > 1 ? (int)(gt_rand() % (unsigned)lc.outfits) : 0;
        int slot = -1;
        if (i < 4) { for (int k = 0; k < 4; k++) if (!(used & (1 << k))) { slot = k; used |= 1 << k; break; } }
        else if (block1) slot = i;
        if (slot < 0) slot = *(int *)(rec + 0x48 * (team == t1 ? 0 : 1) + 0x18);   // shares a teammate's start
        memset(c, 0, 0x60);
        *(int *)c = 1; *(int *)(c + 8) = 1;                     // FUN_140214770's CPU record
        *(int *)(c + 0xc) = lc.row;
        *(int *)(c + 0x10) = slot;
        *(int *)(c + 0x14) = 20; *(int *)(c + 0x18) = 20;
        strncpy_s(c + 0x20, 0x11, lc.name, _TRUNCATE);
        lob_cpu_program(c);                                     // eneprog006 (or the CPU Level's), no deck plan
        memcpy(r, p2, 0x48);                                    // the record's other words as the battle has them
        *(int *)r = 1;
        *(int *)(r + 4) = team;
        *(unsigned *)(r + 8) = ((unsigned)outfit << 16) | (unsigned)(lc.row & 0xFFFF);
        *(int *)(r + 0xc) = -1;
        *(int *)(r + 0x10) = 0;                                 // a CPU
        *(int *)(r + 0x14) = 1;
        *(int *)(r + 0x18) = slot;
        *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)c;
        int a = picked && g_gt_pick[i].ars >= 0 && g_gt_pick[i].ars < na ? g_gt_pick[i].ars : nn > 0 ? named[gt_rand() % (unsigned)nn] : 0;
        if (ars && na > 0 && ptrs && ptrs[i] && nskills > 1) { // deck counts, as FUN_14014f830
            int *cnt = (int *)ptrs[i], u = 0;
            memset(cnt, 0, (size_t)(nskills - 1) * 4);
            short *ids = (short *)(ars + 100 * a + 0x14);
            for (int k = 0; k < 30; k++)
                if (ids[k] != -1 && ids[k] <= maxid && ids[k] > 0 && ids[k] < nskills - 1) { cnt[ids[k]]++; u++; }
            cnt[0] = 30 - u;
        }
        unsigned short sc = ars && na > 0 ? *(unsigned short *)(ars + 100 * a + 0x50) : 0;
        if (aura) aura[i] = (unsigned short)(sc - 1) < 3 && gs ? *(float *)(gs + 0x33034 + 4 * (sc - 1)) : def;
        logf("split-screen %s: fighter %d = CPU %s outfit %d, team %d, start %d, arsenal %d", GT_NAMES[type], i + 1, lc.name,
             outfit + 1, team, slot, a + 1);
    }
    return add;
}

static int gt_self_test() {                                    // gt_fill on stand-in records, arsenals and GSDATA
    static char rec[8 * 0x48], ars[3 * 100], gs[0x33040];
    static int cnt[6][9];
    static ULONG_PTR ptrs[8];
    static float aura[8];
    memset(rec, 0, sizeof(rec)); memset(ars, 0, sizeof(ars)); memset(cnt, 0, sizeof(cnt));
    for (int i = 0; i < 6; i++) ptrs[i] = (ULONG_PTR)cnt[i];
    *(int *)rec = 1; *(int *)(rec + 0x10) = 1; *(int *)(rec + 8) = 0; *(int *)(rec + 0x18) = 1;                 // player 1
    char *r1 = rec + 0x48;
    *(int *)r1 = 1; *(int *)(r1 + 4) = 1; *(int *)(r1 + 0x10) = 1; *(int *)(r1 + 8) = 2; *(int *)(r1 + 0x18) = 3; // player 2
    strcpy_s(ars + 4, 16, "cityCamo"); strcpy_s(ars + 204, 16, "pal");                                         // 0 and 2 named
    for (int k = 0; k < 30; k++) { *(short *)(ars + 0x14 + 2 * k) = k < 20 ? (short)(1 + k % 5) : -1; *(short *)(ars + 200 + 0x14 + 2 * k) = 3; }
    *(unsigned short *)(ars + 0x50) = 2; *(unsigned short *)(ars + 200 + 0x50) = 1;
    *(float *)(gs + 0x33034 + 4) = 1.25f; *(float *)(gs + 0x33034) = 1.5f;
    g_lob_t_ars = ars; g_lob_t_gs = gs; g_lob_t_max = 8; g_lob_t_nars = 3; g_lob_t_def = 1.0f;
    LobChar keep_ch[4]; int keep_n = g_lob_nch;
    memcpy(keep_ch, g_lob_ch, sizeof(keep_ch));
    g_lob_ch[0] = {0, 15, "Player"}; g_lob_ch[1] = {1, 15, "Edgar"}; g_lob_ch[2] = {2, 15, "Freia"}; g_lob_ch[3] = {3, 15, "Meister"};
    g_lob_nch = 4;
    LONG keep_gt = g_gt; bool keep_loaded = g_gt_loaded; int keep_lv = g_cpu_level;
    g_gt_loaded = true; g_cpu_level = 0; g_gt_t_seed = 7; g_gt_t_six = 1;
    // One-on-One: nothing added
    InterlockedExchange(&g_gt, GT_ONE);
    bool one = gt_fill(rec, ptrs, aura, 10, false) == 0 && !*(int *)(rec + 0x48 * 2);
    // Tag Team: a CPU on each side, the free start points 0 and 2, never Player / Freia (the players' characters)
    InterlockedExchange(&g_gt, GT_TAG);
    int n = gt_fill(rec, ptrs, aura, 10, false);
    char *c2 = rec + 0x48 * 2, *c3 = rec + 0x48 * 3;
    int ch2 = *(int *)(c2 + 8) & 0xFFFF, ch3 = *(int *)(c3 + 8) & 0xFFFF;
    bool tag = n == 2 && *(int *)(c2 + 4) == 0 && *(int *)(c3 + 4) == 1 && *(int *)(c2 + 0x10) == 0 && *(int *)(c3 + 0x10) == 0 &&
               *(int *)(c2 + 0x18) == 0 && *(int *)(c3 + 0x18) == 2 && ch2 != 0 && ch2 != 2 && ch3 != 0 && ch3 != 2 && ch2 != ch3 &&
               *(int *)(c2 + 0xc) == -1 && !strcmp((char *)*(ULONG_PTR *)(c2 + 0x20) + 0x46, LOB_CPU_PROG) &&
               *(int *)((char *)*(ULONG_PTR *)(c2 + 0x20) + 0xc) == ch2 && *(int *)((char *)*(ULONG_PTR *)(c3 + 0x20) + 0x10) == 2;
    bool deck = (cnt[2][0] == 10 && cnt[2][1] == 4) || (cnt[2][0] == 0 && cnt[2][3] == 30);   // arsenal 1 or 3, never the unnamed 2
    bool aura_ok = aura[2] == 1.25f || aura[2] == 1.5f;
    // a second go on the same battle adds nothing (the slots are taken)
    bool again = gt_fill(rec, ptrs, aura, 10, false) == 0;
    // Battle Royale: teams 2 and 3
    memset(rec + 0x48 * 2, 0, 4 * 0x48);
    InterlockedExchange(&g_gt, GT_ROYALE);
    n = gt_fill(rec, ptrs, aura, 10, false);
    bool royale = n == 2 && *(int *)(c2 + 4) == 2 && *(int *)(c3 + 4) == 3;
    // 3 vs 3: four CPUs, two a side; without the six-fighter build: Tag Team
    memset(rec + 0x48 * 2, 0, 4 * 0x48);
    InterlockedExchange(&g_gt, GT_SIX);
    n = gt_fill(rec, ptrs, aura, 10, false);
    bool six = n == 4 && *(int *)(rec + 0x48 * 4 + 4) == 0 && *(int *)(rec + 0x48 * 5 + 4) == 1 &&
               *(int *)(rec + 0x48 * 4 + 0x18) == 1 && *(int *)(rec + 0x48 * 5 + 0x18) == 3;   // 4 start points: a teammate's
    memset(rec + 0x48 * 2, 0, 4 * 0x48);
    g_gt_t_six = 0;
    bool fallback = gt_fill(rec, ptrs, aura, 10, false) == 2 && !*(int *)(rec + 0x48 * 4);
    // Single Play (one human) is never touched
    memset(rec + 0x48 * 2, 0, 4 * 0x48);
    *(int *)(r1 + 0x10) = 0;
    InterlockedExchange(&g_gt, GT_TAG);
    bool single = gt_fill(rec, ptrs, aura, 10, false) == 0;
    INT64 a1[2] = {41, 2};
    bool names = !strcmp((const char *)(ULONG_PTR)gt_native(41, a1), "Battle Royale") && gt_native(42, a1) == GT_TAG;
    // v5.31 the lobby rows: 3 vs 3 lists 6 (player 1, two CPUs, player 2, two CPUs) and the match uses those CPUs
    g_gt_t_vars[0] = 0; g_gt_t_vars[1] = 2; g_gt_t_six = 1; g_gt_pick_type = -1;
    InterlockedExchange(&g_gt, GT_SIX);
    INT64 q[2] = {57, 0};
    bool rows = gt_native(57, q) == 6;
    q[1] = 1; rows = rows && gt_native(57, q) == -1;
    q[1] = 4; rows = rows && gt_native(57, q) == -2;
    q[1] = 2; rows = rows && gt_native(57, q) == 2;
    q[1] = 2; int w2 = (int)(gt_native(58, q) & 0xFFFF);
    rows = rows && w2 != 0 && w2 != 2 && gt_native(60, q) >= 0 && gt_native(60, q) < 3;
    memset(rec + 0x48 * 2, 0, 4 * 0x48);
    *(int *)(r1 + 0x10) = 1;
    rows = rows && gt_fill(rec, ptrs, aura, 10, false) == 4 && (*(int *)(rec + 0x48 * 2 + 8) & 0xFFFF) == w2;
    InterlockedExchange(&g_gt, GT_ONE);
    q[1] = 0; rows = rows && gt_native(57, q) == 0;
    g_gt_t_vars[0] = g_gt_t_vars[1] = -1; g_gt_pick_type = -1;
    names = names && rows;
    if (!rows) logf("self-test: split-screen lobby rows FAILED");
    memcpy(g_lob_ch, keep_ch, sizeof(keep_ch)); g_lob_nch = keep_n;
    g_lob_t_ars = NULL; g_lob_t_gs = NULL;
    InterlockedExchange(&g_gt, keep_gt); g_gt_loaded = keep_loaded; g_cpu_level = keep_lv; g_gt_t_seed = 0; g_gt_t_six = -1;
    logf("self-test: game type one %s, tag %s, deck %s, aura %s, again %s, royale %s, 3v3 %s, fallback %s, single play %s, names %s",
         one ? "ok" : "FAILED", tag ? "ok" : "FAILED", deck ? "ok" : "FAILED", aura_ok ? "ok" : "FAILED", again ? "ok" : "FAILED",
         royale ? "ok" : "FAILED", six ? "ok" : "FAILED", fallback ? "ok" : "FAILED", single ? "ok" : "FAILED", names ? "ok" : "FAILED");
    return one && tag && deck && aura_ok && again && royale && six && fallback && single && names ? 1 : -61;
}

// v5.145 Map Test: player 1's view on the whole screen (game thread, every drawn frame)
static void gt_maptest_step() {
    static bool logged;
    if (!g_in_match || ed_online() || g_gt != GT_MAP) { logged = false; InterlockedExchange(&g_maptest_on, 0); return; }
    __try {
        if (*(int *)(g_base + 0x4bc9d4) != 6) { InterlockedExchange(&g_maptest_on, 0); return; }   // a split-screen battle only
        InterlockedExchange(&g_maptest_on, 1);
        unsigned *fl = (unsigned *)(g_base + 0x530034);
        if (*fl & 8) {
            *fl &= ~8u;
            if (!logged) { logged = true; logf("Map Test: split view off - player 1's view on the whole screen"); }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
