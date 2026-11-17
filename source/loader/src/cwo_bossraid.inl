// ---- v5.26 Boss Raid (owner 2026-10-27; "go ahead and work on adding all 8 of those ideas"). Edgar's Dream > Boss Raid: on, with 1 or 2 CPU
// allies; then Single Play, any stage, your arsenal, and one of the boss rows of your Single Play list (BOSS0-Peccato,
// BOSS1-Thalasso, BOSS2-Carcino, BOSS3-DFreia-Phase1, BOSS3b-DFreia-Phase2): the allies fight the boss beside you.
// Game facts (Ghidra batch_065, pcode.py, 2026-10-27; the live com\single2.csv read):
//  - FUN_140128cc0 creates each fighter by its CPU record's kind (+4): 0 a fighter, 1 pdBoss00Class (FUN_140127820),
//    2 pdBoss01Class (FUN_140135ec0 + FUN_140136190(record +0x14, record +0x46 program)), 3 pdBoss02Class (FUN_140139fb0
//    + FUN_14013a250), 4 pdBoss03Class (FUN_140127b60), 5 pdBoss03bClass (FUN_140127dc0); the story's quests make the
//    same classes (FUN_140129260, battle types 0x15 / 0x19 / 0x16 / 0x1C / 0x1D); boss data boss\boss00 .. boss03b;
//  - the live single2.csv (the Single Play list) has rows BOSS00-BOSS04 for the five (bossprog00.ssb), the boss in
//    fighter slot 1 and you in slot 0; the boss's own program locks onto fighter 0 (4.244 / 6.4).
// The allies are built as the split-screen CPUs are (cwo_gametype.inl: FUN_140214770's records), on your team, at free
// start points. The loader's CPU fixes leave a boss's record alone (sixf_cpu_fight skips kinds 1-5).
static bool g_br_on, g_br_loaded;
static int g_br_allies = 2, g_br_row;
// v5.113 Boss Fight (owner 2026-11-07; item 14). Ghidra 2026-11-08: the versus
// create FUN_140128cc0 builds the boss class from the CPU record's kind (+4, 1-5) and its init reads only the record
// (pdBoss00: vtable init FUN_14012ea10 -> FUN_14012ea30(boss, fighter, record +0x14, record +0x46) - the same set-up the
// story quests call with 20 and "<quest>.ssb"; it loads boss00.alr / BossSE_*.bin itself). The game's own Single Play
// rows (com\single2.csv) are BOSS00-04 = kinds 1-5 with "bossprog00.ssb" and 20. So any offline versus with a CPU on the
// other side can be a boss fight without Single Play: that CPU's record becomes the boss's.
static int g_br_boss;                                           // 0 off, 1-5 the boss kind
// v5.117.1 (owner test 2026-11-08: split screen Tag Team / Battle Royale / One-on-One with a Boss chosen "Crash after the match
// started immediately after the count down"; log 09:10:44 / 09:12:17 / 09:14:29 / 09:18:02: exe+296933 reading +0x10 of a null,
// called from pdBoss00's own code (return address exe+133830) 45 ms after phase 6): the boss built into a versus match by
// the loader is missing data the game's own Single Play / quest set-up gives it. Off until that is found in Ghidra; the
// game's own Single Play BOSS rows are unaffected.
static const bool BR_BOSS_FIGHT_READY = false;
static char g_br_bossrec[0x60];
// v5.114 the Boss Slayer reward (owner 2026-11-08). Ghidra 2026-11-08: an attack's damage (FUN_140272400) = its
// STR formula (table 0x1402dd0a0) + FUN_14024d4d0(owner battle data, 0) + ...; FUN_14024d4d0 sums status n = battle data
// +0x61b8 + 12n (lasting) + 0x61bc + 12n (timed) + the stage environment (FUN_140271660, Lunar Force), capped by the table
// 0x1402dcf30 (status 0: 9). Heat (+1) / Stimulate / Tiger's Strength / Trance (+2) are that status. The reward adds 2 to
// player 1's lasting status 0 when each offline fight starts - every attack +2, the game's cap still applies.
static bool g_br_slayer, g_br_fight_now;
static char g_br_cpu[4][0x60];
static const char *BR_BOSS[5] = {"Peccato (boss00)", "Thalasso (boss01)", "Carcino (boss02)", "Demented Freia (boss03)", "Demented Freia, phase 2 (boss03b)"};

static void br_path(wchar_t *p) { swprintf_s(p, MAX_PATH, L"%sCwoBossRaid.txt", g_dir); }
static void br_load();
static void br_save();
// v5.152 SINGLE PLAY's Boss Raid game type (cwo_lobby.inl): on for its matches, off for the others; the allies count
static bool br_set_on(bool on) { br_load(); if (g_br_on != on) { g_br_on = on; br_save(); } return on; }
static int br_allies() { br_load(); return g_br_allies; }
static void br_allies_step(int d) { br_load(); g_br_allies = ((g_br_allies + d) % 3 + 3) % 3; br_save(); }
static void br_load() {
    if (g_br_loaded) return;
    g_br_loaded = true;
    wchar_t p[MAX_PATH];
    br_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[64]; int a;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf_s(line, "on %d", &a) == 1) g_br_on = a != 0;
        else if (sscanf_s(line, "allies %d", &a) == 1) g_br_allies = a >= 0 && a <= 2 ? a : 2;
        else if (sscanf_s(line, "boss %d", &a) == 1) g_br_boss = a >= 0 && a <= 5 ? a : 0;
        else if (sscanf_s(line, "slayer %d", &a) == 1) g_br_slayer = a != 0;
    }
    fclose(f);
}
static void br_save() {
    wchar_t p[MAX_PATH];
    br_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# Boss Raid (loader)\non %d\nallies %d\nboss %d\nslayer %d\n", g_br_on ? 1 : 0, g_br_allies, g_br_boss,
            g_br_slayer ? 1 : 0);
    fclose(f);
}
static int br_boss_kind(char *rec) {                            // the battle's boss (1-5), or 0
    for (int p = 0; p < 4; p++) {
        char *r = rec + 0x48 * p, *c = *(char **)(r + 0x20);
        if (*(int *)r && c) { int k = *(int *)(c + 4); if (k >= 1 && k <= 5) return k; }
    }
    return 0;
}
// v5.113 the Boss Fight: the first CPU of the other team becomes the chosen boss (offline only; one boss; a match that
// already has a boss is left alone). Returns the boss kind set, or 0.
static bool ed_online();
static int br_boss_fight(char *rec) {
    br_load();
    if (!BR_BOSS_FIGHT_READY && !g_ed_test_gs) {
        static bool said;
        if (!said && g_br_on && g_br_boss) { said = true; logf("boss fight: off for now (the boss crashed at the fight start, 8 Nov) - no boss added"); }
        return 0;
    }
    if (!g_br_on || g_br_boss < 1 || g_br_boss > 5 || ed_online() || br_boss_kind(rec) || !*(int *)rec) return 0;
    int me_team = *(int *)(rec + 4);
    for (int p = 1; p < 4; p++) {
        char *r = rec + 0x48 * p, *c = *(char **)(r + 0x20);
        if (!*(int *)r || *(int *)(r + 0x10) || *(int *)(r + 4) == me_team) continue;     // present, a CPU, the other side
        if (c) memcpy(g_br_bossrec, c, 0x60); else memset(g_br_bossrec, 0, 0x60);
        *(int *)(g_br_bossrec + 4) = g_br_boss;                      // kind: 1 Boss00 .. 5 Boss03b
        *(int *)(g_br_bossrec + 0x14) = 20;                          // single2.csv BOSS rows / the quests' value
        memset(g_br_bossrec + 0x46, 0, 0x60 - 0x46);
        strcpy_s(g_br_bossrec + 0x46, 0x60 - 0x46, "bossprog00.ssb");   // the boss's own program (single2.csv BOSS rows)
        *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)g_br_bossrec;
        g_br_fight_now = true;
        logf("boss fight: fighter %d (team %d) is now %s", p + 1, *(int *)(r + 4), BR_BOSS[g_br_boss - 1]);
        set_status("BOSS FIGHT: the boss is here");
        return g_br_boss;
    }
    logf("boss fight: no CPU on the other side in this match - no boss (add a CPU opponent)");
    return 0;
}
// v5.117 (owner 2026-11-08: split screen One-on-One with Boss Raid on and Peccato chosen: "There's no boss character ... It's
// just me and the second player"): with two players and no CPU, player 2 joins player 1's team and record 3 becomes the
// boss (the same CPU data as br_boss_fight: kind, 20, bossprog00.ssb); the allies (br_fill) take record 4.
static int br_split_boss(char *rec) {
    br_load();
    if (!BR_BOSS_FIGHT_READY) return 0;
    if (!g_br_on || g_br_boss < 1 || g_br_boss > 5 || ed_online() || br_boss_kind(rec)) return 0;
    char *p1 = rec, *p2 = rec + 0x48, *r = rec + 0x48 * 2;
    if (!*(int *)p1 || !*(int *)p2 || !*(int *)(p1 + 0x10) || !*(int *)(p2 + 0x10) || *(int *)r || *(int *)(rec + 0x48 * 3)) return 0;
    int t1 = *(int *)(p1 + 4);
    *(int *)(p2 + 4) = t1;                                       // the two players: one side
    int used = 0;
    for (int i = 0; i < 2; i++) { int s = *(int *)(rec + 0x48 * i + 0x18); if (s >= 0 && s < 4) used |= 1 << s; }
    int slot = 0;
    for (int k = 0; k < 4; k++) if (!(used & (1 << k))) { slot = k; break; }
    memset(g_br_bossrec, 0, 0x60);
    *(int *)g_br_bossrec = 1; *(int *)(g_br_bossrec + 8) = 1;
    *(int *)(g_br_bossrec + 4) = g_br_boss;
    *(int *)(g_br_bossrec + 0x10) = slot;
    *(int *)(g_br_bossrec + 0x14) = 20; *(int *)(g_br_bossrec + 0x18) = 20;
    strcpy_s(g_br_bossrec + 0x20, 0x11, "Boss");
    strcpy_s(g_br_bossrec + 0x46, 0x60 - 0x46, "bossprog00.ssb");
    memcpy(r, p2, 0x48);
    *(int *)r = 1; *(int *)(r + 4) = t1 == 0 ? 1 : 0;
    *(unsigned *)(r + 8) = 0;
    *(int *)(r + 0xc) = -1; *(int *)(r + 0x10) = 0; *(int *)(r + 0x14) = 1; *(int *)(r + 0x18) = slot;
    *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)g_br_bossrec;
    g_br_fight_now = true;
    logf("boss fight (split screen): players 1 + 2 on team %d against %s (fighter 3, start %d)", t1, BR_BOSS[g_br_boss - 1], slot);
    set_status("BOSS FIGHT: both players against the boss");
    return g_br_boss;
}
// create_hook (sixf_before_create_main), game thread: a Single Play battle against a boss gets your CPU allies
static int br_fill(char *rec, ULONG_PTR *ptrs, float *aura, int nskills) {
    br_load();
    if (!g_br_on || g_br_allies <= 0) return 0;
    int kind = br_boss_kind(rec);
    if (!kind || !*(int *)rec || !*(int *)(rec + 0x10)) return 0;    // a boss and you
    g_br_fight_now = true;                                       // v5.114 a boss match with Boss Raid on: the reward counts
    int me_team = *(int *)(rec + 4), used = 0, added = 0, taken[4], nt = 0;
    taken[nt++] = *(int *)(rec + 8) & 0xFFFF;
    for (int p = 0; p < 4; p++) { char *r = rec + 0x48 * p; int s = *(int *)(r + 0x18); if (*(int *)r && s >= 0 && s < 4) used |= 1 << s; }
    lob_chars_ready();
    bool test = g_lob_t_ars != NULL;
    if (!test) cpu_level_load();
    char *ars = test ? g_lob_t_ars : lob_ars_base(), *gs = test ? g_lob_t_gs : *(char **)(g_base + RVA_GSDATA);
    int na = test ? g_lob_t_nars : lob_ars_count(), maxid = test ? g_lob_t_max : *(int *)(*(char **)(g_base + 0x4bdc40) + 0x10);
    float def = test ? g_lob_t_def : *(float *)(g_base + 0x2f7da8);
    int named[64], nn = 0;
    for (int a = 0; ars && a < na && a < 64; a++) if (ars[100 * a + 4]) named[nn++] = a;
    int pool = g_lob_nch < 15 ? g_lob_nch : 15;
    for (int i = 1; i < 4 && added < g_br_allies; i++) {
        char *r = rec + 0x48 * i;
        if (*(int *)r) continue;                                 // the boss's slot and any other taken one
        int ch = 0;
        for (int tries = 0; tries < 40; tries++) {
            ch = pool > 0 ? (int)(gt_rand() % (unsigned)pool) : 0;
            bool dup = false;
            for (int k = 0; k < nt; k++) dup |= pool > 0 && g_lob_ch[ch].row == taken[k];
            if (!dup) break;
        }
        const LobChar &lc = g_lob_ch[pool > 0 ? ch : 0];
        if (nt < 4) taken[nt++] = lc.row;
        int slot = 0;
        for (int k = 0; k < 4; k++) if (!(used & (1 << k))) { slot = k; used |= 1 << k; break; }
        char *c = g_br_cpu[i];
        memset(c, 0, 0x60);
        *(int *)c = 1; *(int *)(c + 8) = 1; *(int *)(c + 0xc) = lc.row; *(int *)(c + 0x10) = slot;
        *(int *)(c + 0x14) = 20; *(int *)(c + 0x18) = 20;
        strncpy_s(c + 0x20, 0x11, lc.name, _TRUNCATE);
        lob_cpu_program(c);
        memcpy(r, rec, 0x48);                                    // your record's other words
        *(int *)r = 1; *(int *)(r + 4) = me_team;
        *(unsigned *)(r + 8) = (unsigned)(lc.row & 0xFFFF);
        *(int *)(r + 0xc) = -1; *(int *)(r + 0x10) = 0; *(int *)(r + 0x14) = 1; *(int *)(r + 0x18) = slot;
        *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)c;
        int a = nn > 0 ? named[gt_rand() % (unsigned)nn] : 0;
        if (ars && na > 0 && ptrs && ptrs[i] && nskills > 1) {
            int *cnt = (int *)ptrs[i], u = 0;
            memset(cnt, 0, (size_t)(nskills - 1) * 4);
            short *ids = (short *)(ars + 100 * a + 0x14);
            for (int k = 0; k < 30; k++) if (ids[k] != -1 && ids[k] <= maxid && ids[k] > 0 && ids[k] < nskills - 1) { cnt[ids[k]]++; u++; }
            cnt[0] = 30 - u;
        }
        unsigned short sc = ars && na > 0 ? *(unsigned short *)(ars + 100 * a + 0x50) : 0;
        if (aura) aura[i] = (unsigned short)(sc - 1) < 3 && gs ? *(float *)(gs + 0x33034 + 4 * (sc - 1)) : def;
        added++;
        logf("boss raid: ally %d = %s (fighter %d), start %d, arsenal %d - against %s", added, lc.name, i + 1, slot, a + 1, BR_BOSS[kind - 1]);
    }
    if (added) set_status("BOSS RAID: your allies are here");
    return added;
}

// v5.114 called by in_step (cwo_intro.inl) on every match phase change, game thread. Phase 5 = countdown, 6 = fight,
// 7 = fewer than 2 teams in (FUN_14012c510). Offline only; never a Download Battle (scoreboard).
static void br_phase(int from, int to) {
    br_load();
    if (ed_online() || g_db_match) { g_br_fight_now = false; return; }
    ULONG_PTR *acts = cwo_actors();
    if (to == 6 && from == 5 && g_br_slayer) {
        ULONG_PTR a = acts[0];
        if (a && *(int *)(a + 0x10) == 0) {
            int *lasting = (int *)(a + 0x40 + 0x61b8);           // status 0 (attack power), the lasting part
            *lasting += 2;
            logf("boss slayer: player 1 attack power +2 (status 0 lasting part now %d)", *lasting);
        }
    }
    if (to == 7 && from == 6 && g_br_fight_now) {
        g_br_fight_now = false;
        ULONG_PTR me = acts[0];
        int nf = cwo_nfighters(); if (nf > 8) nf = 8;
        int team = -1000; bool draw = false;
        for (int p = 0; p < nf; p++) {
            ULONG_PTR a = acts[p];
            if (!a || !*(int *)(a + 0x20) || *(int *)(a + 0x24) != 1) continue;
            int t = *(int *)(a + 0x1c);
            if (team == -1000) team = t; else if (t != team) draw = true;
        }
        bool won = me && !draw && team != -1000 && team == *(int *)(me + 0x1c);
        logf("boss fight over: %s", won ? "your side won" : draw ? "time out" : "the boss won");
        if (won && !g_br_slayer) {
            g_br_slayer = true;
            br_save();
            set_status("BOSS DEFEATED! Boss Slayer: your attacks +2 from your next match on (offline)");
        }
    }
}

// ---- the page (controller thread)
static int br_csv_rows(char found[5]) {                        // which boss rows the Single Play list has
    memset(found, 0, 5);
    wchar_t exe[MAX_PATH], p[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\');
    if (sl) sl[1] = 0;
    swprintf_s(p, MAX_PATH, L"%sAssets\\Data\\com\\single2.csv", exe);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"rb") != 0 || !f) return -1;
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), f))
        for (int k = 0; k < 5; k++) { char t[8]; sprintf_s(t, "BOSS0%d,", k); if (!strncmp(line, t, 7) && !found[k]) { found[k] = 1; n++; } }
    fclose(f);
    return n;
}
static void br_input(WORD pressed) {
    br_load();
    const int N = 5;
    if (pressed & BTN_B) { InterlockedExchange(&g_ed_page, EDP_CHOOSER); return; }
    if (pressed & BTN_DOWN) g_br_row = (g_br_row + 1) % N;
    if (pressed & BTN_UP) g_br_row = (g_br_row + N - 1) % N;
    int dir = (pressed & BTN_RIGHT) ? 1 : (pressed & BTN_LEFT) ? -1 : 0;
    if (g_br_row == 1 && dir) { g_br_allies = ((g_br_allies + dir) % 3 + 3) % 3; br_save(); }
    if (g_br_row == 2 && dir) { g_br_boss = ((g_br_boss + dir) % 6 + 6) % 6; br_save(); }
    if (!(pressed & BTN_A)) return;
    if (g_br_row == 0) { g_br_on = !g_br_on; br_save(); set_status(g_br_on ? "Boss Raid on: Single Play > a BOSS row" : "Boss Raid off"); }
    else if (g_br_row == 1) { g_br_allies = (g_br_allies + 1) % 3; br_save(); }
    else if (g_br_row == 2) { g_br_boss = (g_br_boss + 1) % 6; br_save(); }
    else if (g_br_row == 4) InterlockedExchange(&g_ed_page, EDP_CHOOSER);
}
template <class PUT, class BOX> static void br_draw(PUT &put, BOX &box, int x, int y) {
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF, GREY = 0xFFB0B0B0, GREEN = 0xFF80FF80, RED = 0xFFFF8080;
    const int LINE = 16;
    (void)box;
    br_load();
    put(x, y, "BOSS RAID                  (B: back)", GOLD);
    y += LINE + 6;
    char line[128];
    sprintf_s(line, "%sBoss Raid:  %s", g_br_row == 0 ? "> " : "  ", g_br_on ? "ON" : "off");
    put(x, y, line, g_br_row == 0 ? GOLD : g_br_on ? GREEN : WHITE); y += LINE;
    sprintf_s(line, "%sCPU allies:  < %d >  (their level: the 3 VS 3 LOBBY's CPU Level)", g_br_row == 1 ? "> " : "  ", g_br_allies);
    put(x, y, line, g_br_row == 1 ? GOLD : WHITE); y += LINE;
    sprintf_s(line, "%sBoss:  < %s >%s", g_br_row == 2 ? "> " : "  ", g_br_boss ? BR_BOSS[g_br_boss - 1] : "the match's own (off)",
              BR_BOSS_FIGHT_READY ? "" : "   (not working yet - no boss is added)");
    put(x, y, line, g_br_row == 2 ? GOLD : g_br_boss ? GREEN : WHITE); y += LINE;
    put(x, y, g_br_row == 3 ? "> How to start" : "  How to start", g_br_row == 3 ? GOLD : WHITE); y += LINE;
    put(x, y, g_br_row == 4 ? "> Back" : "  Back", g_br_row == 4 ? GOLD : WHITE); y += LINE + 8;
    put(x, y, "With a Boss chosen: any offline versus with a CPU opponent (split screen, Create a Mode):", BLUE); y += LINE;
    put(x, y, "  the first CPU on the other side is the boss; the allies join yours. No Single Play needed.", WHITE); y += LINE;
    put(x, y, "Or: Single Play > any stage > your arsenal > a boss row:", BLUE); y += LINE;
    char found[5];
    int n = br_csv_rows(found);
    for (int k = 0; k < 5; k++, y += LINE) {
        sprintf_s(line, "  BOSS%d  %s%s", k, BR_BOSS[k], n < 0 ? "" : found[k] ? "" : "   (not in your Single Play list)");
        put(x, y, line, n < 0 ? GREY : found[k] ? WHITE : RED);
    }
    put(x, y + 4, "The allies join your side; the boss itself is the game's own (story program).", GREY);
    put(x, y + 4 + LINE, g_br_slayer ? "Boss Slayer: EARNED - your attacks +2 in every offline match (like Stimulate, always on)"
                                     : "Boss Slayer: beat a boss here to earn +2 power on all your attacks for good", g_br_slayer ? GREEN : GREY);
}

static int sixf_cpu_fight(char *rec);                          // cwo_sixf2.inl
static int br_self_test() {                                     // two allies on your side, the boss untouched, no boss: nothing
    static char rec[8 * 0x48], boss[0x60], ars[2 * 100], gs[0x33040];
    static int cnt[6][9];
    static ULONG_PTR ptrs[8];
    static float aura[8];
    memset(rec, 0, sizeof(rec)); memset(boss, 0, sizeof(boss)); memset(ars, 0, sizeof(ars));
    for (int i = 0; i < 6; i++) ptrs[i] = (ULONG_PTR)cnt[i];
    *(int *)rec = 1; *(int *)(rec + 0x10) = 1; *(int *)(rec + 4) = 0; *(int *)(rec + 0x18) = 2;            // you, start 2
    char *r1 = rec + 0x48;
    *(int *)r1 = 1; *(int *)(r1 + 4) = 1; *(int *)(r1 + 0x18) = 0; *(ULONG_PTR *)(r1 + 0x20) = (ULONG_PTR)boss;
    *(int *)(boss + 4) = 4; strcpy_s(boss + 0x46, 0x16, "bossprog00.ssb");                                 // Demented Freia
    strcpy_s(ars + 4, 16, "cityCamo");
    for (int k = 0; k < 30; k++) *(short *)(ars + 0x14 + 2 * k) = (short)(1 + k % 5);
    *(unsigned short *)(ars + 0x50) = 2; *(float *)(gs + 0x33034 + 4) = 1.25f;
    g_lob_t_ars = ars; g_lob_t_gs = gs; g_lob_t_max = 8; g_lob_t_nars = 1; g_lob_t_def = 1.0f;
    LobChar keep_ch[3]; int keep_n = g_lob_nch;
    memcpy(keep_ch, g_lob_ch, sizeof(keep_ch));
    g_lob_ch[0] = {0, 15, "Player"}; g_lob_ch[1] = {1, 15, "Edgar"}; g_lob_ch[2] = {3, 15, "Meister"};
    g_lob_nch = 3;
    bool keep_on = g_br_on, keep_loaded = g_br_loaded; int keep_al = g_br_allies, keep_lv = g_cpu_level;
    g_br_on = true; g_br_loaded = true; g_br_allies = 2; g_cpu_level = 0; g_gt_t_seed = 3;
    int n = br_fill(rec, ptrs, aura, 10);
    char *a2 = rec + 0x48 * 2, *a3 = rec + 0x48 * 3;
    bool allies = n == 2 && *(int *)(a2 + 4) == 0 && *(int *)(a3 + 4) == 0 && *(int *)(a2 + 0x10) == 0 && *(int *)(a3 + 0x10) == 0 &&
                  *(int *)(a2 + 0x18) == 1 && *(int *)(a3 + 0x18) == 3 && (*(int *)(a2 + 8) & 0xFFFF) != 0 && (*(int *)(a3 + 8) & 0xFFFF) != 0 &&
                  *(int *)((char *)*(ULONG_PTR *)(a2 + 0x20) + 4) == 0 && cnt[2][0] == 0 && cnt[2][1] == 6 && aura[2] == 1.25f;
    bool boss_kept = *(ULONG_PTR *)(r1 + 0x20) == (ULONG_PTR)boss && *(int *)(boss + 4) == 4 && !strcmp(boss + 0x46, "bossprog00.ssb");
    memset(rec + 0x48 * 2, 0, 2 * 0x48);
    *(int *)(boss + 4) = 0;                                      // an ordinary CPU: not a raid
    bool plain = br_fill(rec, ptrs, aura, 10) == 0 && !*(int *)(rec + 0x48 * 2);
    *(int *)(boss + 4) = 2; g_br_on = false;                     // off: nothing
    bool off = br_fill(rec, ptrs, aura, 10) == 0;
    // the CPU fixes leave a boss alone (sixf_cpu_fight): a Brutal level would otherwise swap its program
    g_cpu_level = 2; *(int *)(boss + 4) = 4;
    sixf_cpu_fight(rec);
    bool fixes = *(ULONG_PTR *)(r1 + 0x20) == (ULONG_PTR)boss && !strcmp(boss + 0x46, "bossprog00.ssb");
    // v5.113 the Boss Fight: an ordinary CPU opponent becomes the chosen boss; a boss already there is kept
    static char cpu[0x60];
    memset(rec, 0, sizeof(rec)); memset(cpu, 0, sizeof(cpu));
    *(int *)rec = 1; *(int *)(rec + 0x10) = 1;
    *(int *)r1 = 1; *(int *)(r1 + 4) = 1; *(ULONG_PTR *)(r1 + 0x20) = (ULONG_PTR)cpu; strcpy_s(cpu + 0x46, 0x16, "eneprog006.ssb");
    int keep_boss = g_br_boss; g_br_on = true; g_br_boss = 5;
    char *keep_tgs = g_ed_test_gs; g_ed_test_gs = gs;                 // offline for the online check
    int k5 = br_boss_fight(rec);
    char *bc = *(char **)(r1 + 0x20);
    bool fight = k5 == 5 && bc == g_br_bossrec && *(int *)(bc + 4) == 5 && *(int *)(bc + 0x14) == 20 && !strcmp(bc + 0x46, "bossprog00.ssb") &&
                 *(int *)(cpu + 4) == 0 && br_boss_fight(rec) == 0;                       // already a boss: left alone
    g_br_boss = 0;
    *(ULONG_PTR *)(r1 + 0x20) = (ULONG_PTR)cpu;
    bool fight_off = br_boss_fight(rec) == 0 && *(ULONG_PTR *)(r1 + 0x20) == (ULONG_PTR)cpu;
    g_br_boss = keep_boss; g_ed_test_gs = keep_tgs;
    fixes = fixes && fight && fight_off;
    memcpy(g_lob_ch, keep_ch, sizeof(keep_ch)); g_lob_nch = keep_n; g_lob_t_ars = NULL; g_lob_t_gs = NULL;
    g_br_on = keep_on; g_br_loaded = keep_loaded; g_br_allies = keep_al; g_cpu_level = keep_lv; g_gt_t_seed = 0;
    logf("self-test: boss raid allies %s, boss kept %s, no boss %s, off %s, CPU fixes skip the boss + boss fight %s", allies ? "ok" : "FAILED",
         boss_kept ? "ok" : "FAILED", plain ? "ok" : "FAILED", off ? "ok" : "FAILED", fixes ? "ok" : "FAILED");
    return allies && boss_kept && plain && off && fixes ? 1 : -69;
}
