// ---- v5.1 six fighters, route A step 2 (owner 2026-10-25): room for 6 fighters everywhere the game keeps one entry per fighter, and fighters 5-6 in a 3 vs 3 mode.
// Facts and every changed byte: tools/sixf2_manifest.py (Ghidra batch_065, capstone; research log
// 07_Combat_and_Skills/SIX_FIGHTERS_ROUTE_A_2026-10-25.md), generated into cwo_sixf2_sites.h by tools/build_sixf2.py.
//   - 25 per-fighter stores move to bigger copies (433 instructions repointed; the capsule table gets a new layout);
//   - 350 values change: fighter-count compares (4 -> 6), the fighter pool (0x2d0000 -> 0x390000, the stage's computer
//     and camera data move up), the battle-data count, pdBattlePsp's and MSEffect's per-fighter parts (moved to the end
//     of bigger objects), the process heap (1 -> 2 MB);
//   - 5 detours: 3 HUD writes keep to its 4 panels, and two calls into the loader at match start.
// Applied in DllMain after step 1, all-or-nothing; only when CwoSixFighters.txt says "on"; never online (the loader is
// taken out before online play). In a match with 4 fighters or fewer the game plays as before (the new entries are empty).
// A 3 vs 3 match: the mode in use (Create a Mode) has Players = "3 vs 3"; fighters 5-6 are CPUs, one per team, copies of
// a CPU of that team (character, deck, brain), so every model they need is already loaded.
#include "cwo_sixf2_apply.inl"
static ULONG_PTR g_six2_block, g_six2_stubs;
static char g_six_cpu[6][0x60];                                // the added fighters' CPU records (copies)
static const int SIXF_MAX = 8;                                 // entries per moved store (6 used)
static void sixhud_reset();                                    // v5.7 cwo_sixhud.inl

// the address of a game store, wherever it is now (old RVA in, live address out)
static ULONG_PTR cwo_gaddr(ULONG_PTR rva) {
    if (g_six2_block) {
        ULONG_PTR a = sixf2_map(g_base, g_six2_block, rva);
        if (a) return a;
    }
    return g_base + rva;
}
static char *cwo_caps(ULONG_PTR off) { return (char *)cwo_gaddr(0x584f80 + off); }   // capsule table field (old offset)

static const ULONG_PTR RVA6_SETUP = 0x4bc420, RVA6_AURA = 0x4bc958, RVA6_TARGETS = 0x4bbd30, RVA6_CAPPTR = 0x585fe8,
                       RVA6_SKILLHDR = 0x4bdc40, RVA6_SPAWNDAT = 0x530320, RVA6_ORIGIN = 0x4bca04, RVA6_TYPE = 0x4bc9d4;

static bool sixf_mode_wants_six();                             // cwo_edgar.inl: the mode in use is 3 vs 3

// the stage's start points (FUN_1401d1da0): block b of the spawn file, or 0 entries
static int sixf_spawn_count(int b) {
    char *d = *(char **)(g_base + RVA6_SPAWNDAT);
    if (!d) return 0;
    unsigned o = *(unsigned *)(d + 4 + (unsigned)(b + 0x28) * 4);
    if (o == 0xFFFFFFFFu) return 0;
    return *(unsigned char *)(d + o + 4);
}

// v5.180.2 (BFR 1.50.13 test 2026-11-13 23:49: a Single Play 3 vs 3 on Edgar's Dream crashed at exe+14fe27 - the v5.32 crash
// of a six-fighter creator match). Ghidra / disassembly (clean PDUWP 9ba7411b): in Single Play the game re-reads each
// fighter's capsule spots every 10 frames (0x14014fe02: frame counter % 10 == 1 and the byte 0x140530287 == 0) with
// FUN_14012a650(f, k): page f / 4 -> byte 0x14052dd63, FUN_1401d1da0 looks up header entry 0x28 + page of the stage's start
// file (*0x140530320 = word 0xA4 + 4 * page), and returns block + 0x14 + 80 * ((f % 4) % count) + 12 * k - or, when that
// entry is -1 (a stage with 4 start points has no block 2), 0 + the same offsets, which the caller reads (rax 0x2c = f 4,
// k 2). Fighters 5-6 already start on their teammates' start points (lob_fill / the copies); now, in a six-fighter match on
// such a stage, the loaded start file's block 2 entry points at block 1, so their capsule spots are their teammates' too.
static void sixf_block2_fill() {
    __try {
        char *d = *(char **)(g_base + RVA6_SPAWNDAT);
        if (!d) return;
        unsigned *b1 = (unsigned *)(d + 4 + 0x28 * 4), *b2 = (unsigned *)(d + 4 + 0x29 * 4);
        if (*b2 != 0xFFFFFFFFu || *b1 == 0xFFFFFFFFu) return;
        *b2 = *b1;
        logf("six fighters: the stage has no second start block - fighters 5-6 read the first one (their teammates' start and capsule spots)");
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("six fighters: the start file could not be read - block 2 left as it is"); }
}

// FUN_140128cc0 entry (create_hook), game thread: records 5-8 cleared; in a 3 vs 3 match every empty record up to 6 is
// filled so that player 1's team and the other team have 3 each (v5.1.1, owner: one controller, from Edgar's Dream - any
// Single Play battle, even 1 vs 1, becomes 3 vs 3). A filled record is a copy of a CPU of its team (or of the other team's
// CPU when its own has none): its own copy of the CPU record (0x5c: level, name, program), its deck counts, aura factor
// and target, as FUN_140192a50 sets them for its CPUs
static char g_six_keep[4 * 0x48];                              // the launcher's 4 records, put back on a fault
// v5.9 (owner 21:32: "the CPU doesn't fight back. They literally just ... stand there"; log 21:03 / 21:04: fighter 2 ran
// chkprog000.ssb while the 3 vs 3 copies ran eneprog006.ssb): Single Play's debug rows bring the practice dummy
// (com\single2.csv "CHKability": Decoy, chkprog000.ssb, deck DECK000) and v5.7 switched only the copies. In an offline
// match outside the creators' sessions (Stage Creator, Skill Test Stage: their CPUs stay frozen in place) each such CPU
// gets a record of its own (the game's single2 row is not touched) with the fighting program and no deck plan, as the
// 3 VS 3 LOBBY's CPUs (lob_cpu_program; FUN_14017b350 loads com\ai\<record +0x46>).
// v5.12 (owner 2026-10-26): with a CPU Level chosen (3 VS 3 lobby X > CPU Level) every CPU of
// the match gets a record of its own naming that enemyparam row (+0x18; FUN_14017b020 copies the row into slots 30-38)
static int sixf_cpu_fight(char *rec) {
    int n = 0;
    cpu_level_load();
    for (int p = 0; p < 4; p++) {
        char *r = rec + 0x48 * p;
        char *c = *(char **)(r + 0x20);
        if (!*(int *)r || *(int *)(r + 0x10) != 0 || !c) continue;
        if (*(int *)(c + 4) >= 1 && *(int *)(c + 4) <= 5) continue;   // v5.26 a story boss keeps its own record and program
        bool dummy = !_strnicmp(c + 0x46, "chkprog", 7);
        if (!dummy && g_cpu_level <= 0) continue;
        if (c != g_six_cpu[p]) memcpy(g_six_cpu[p], c, 0x5c);
        if (dummy) {
            lob_cpu_program(g_six_cpu[p]);
            logf("CPU fights: fighter %d had the practice dummy's program - now %s", p + 1, LOB_CPU_PROG);
        } else {
            cpu_level_apply(g_six_cpu[p], g_cpu_level);
            logf("CPU level: fighter %d (%s) - %s, enemyparam row %d", p + 1, g_six_cpu[p] + 0x46, CPU_LV_NAMES[g_cpu_level],
                 *(int *)(g_six_cpu[p] + 0x18));
        }
        *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)g_six_cpu[p];
        n++;
    }
    return n;
}

// v5.12 CPU starting life (Ghidra: FUN_14024e790, a CPU fighter's set-up, takes it from byte 0 of the fighter's
// enemyparam row - 20 in every stock row - and only humans take the rule table's that Create a Mode writes; FUN_140128cc0
// runs every fighter's set-up between sixf_before_create and sixf_after_start). With a mode in use, during the set-up of an
// offline Single Play match byte 0 of every row = the mode's starting life; put back when the start is done.
static signed char g_cpu_life_keep[100];
static signed char *g_cpu_life_tbl;
static signed char *g_cpu_life_test_tbl;                        // self-test stand-in
static void cpu_life_begin(int life) {
    __try {
        signed char *t = g_cpu_life_test_tbl ? g_cpu_life_test_tbl : *(signed char **)(g_base + 0x4c0188);
        if (!t || g_cpu_life_tbl || life < 1 || life > 127) return;
        for (int i = 0; i < 100; i++) { g_cpu_life_keep[i] = t[4 + 10 * i]; if (t[4 + 10 * i]) t[4 + 10 * i] = (signed char)life; }
        g_cpu_life_tbl = t;
        logf("CPU life: the mode's starting life %d for this match's CPUs", life);
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_cpu_life_tbl = NULL; }
}
static void cpu_life_end() {
    __try {
        if (g_cpu_life_tbl)
            for (int i = 0; i < 100; i++) g_cpu_life_tbl[4 + 10 * i] = g_cpu_life_keep[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_cpu_life_tbl = NULL;
}

// v5.13 Create a Quest "Your life" (Ghidra: FUN_14024ea70 gives the quest player life 20, a constant). FUN_1401282f0 runs
// FUN_140129260 (the quest set-up) when DAT_1404bbed0 = the quest record; at its end (sixf_after_start) the saved quest
// whose name is that record's title (the quest install writes it) sets fighter 0's life words (part +0x40: +0xc / +0x10
// / +0x14, as FUN_14024d020 does)
static const char *g_edq_test_rec;
static char *g_edq_test_fighter;
static void ed_quest_after_start() {
    __try {
        const char *qr = g_edq_test_rec ? g_edq_test_rec : *(const char **)(g_base + 0x4bbed0);
        if (!qr || (!g_edq_test_rec && ed_online())) return;
        int id = *(const short *)(qr + 2);
        ed_load_all();
        const EdCreator &c = g_edc[EDP_QUEST];
        for (int k = 0; k < c.n; k++) {
            const EdItem &it = c.item[k];
            if (it.v[0] != id || strcmp(qr + 4, it.name)) continue;
            int life = it.v[28];
            char *f = g_edq_test_fighter ? g_edq_test_fighter : *(char **)cwo_gaddr(0x4bc9d8);
            if (!f || life == 20 || life < 1 || life > 99) return;
            int *w = (int *)(f + 0x40);
            int was = w[3];
            w[3] = life; w[4] = life; w[5] = life;
            logf("quest %d '%s': your life %d (the game gave %d)", id, it.name, life, was);
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static int cpu_level_self_test() {                             // stand-ins: the epa table, two CPUs and a human
    static signed char tbl[1004];
    memset(tbl, 0, sizeof(tbl));
    for (int i = 0; i < 86; i++) tbl[4 + 10 * i] = 20;
    tbl[10 * 83 + 7] = 100;
    static char rec[4 * 0x48], c1[0x60], c2[0x60];
    char keep[sizeof(g_six_cpu)];
    memcpy(keep, g_six_cpu, sizeof(keep));
    memset(rec, 0, sizeof(rec)); memset(c1, 0, sizeof(c1)); memset(c2, 0, sizeof(c2));
    *(int *)rec = 1; *(int *)(rec + 0x10) = 1;                                       // you
    *(int *)(rec + 0x48) = 1; *(ULONG_PTR *)(rec + 0x48 + 0x20) = (ULONG_PTR)c1;      // a CPU with its own program
    *(int *)(c1 + 0x18) = 7; strcpy_s(c1 + 0x46, 0x16, "eneprog003.ssb");
    *(int *)(rec + 0x90) = 1; *(ULONG_PTR *)(rec + 0x90 + 0x20) = (ULONG_PTR)c2;      // the practice dummy
    *(int *)(c2 + 0x18) = 7; strcpy_s(c2 + 0x46, 0x16, "chkprog000.ssb");
    int keep_lv = g_cpu_level; bool keep_ld = g_cpu_lv_loaded;
    g_cpu_level = 2; g_cpu_lv_loaded = true; g_cpu_lv_test_tbl = (const signed char *)tbl; g_cwoprog_test = 0;
    int n = sixf_cpu_fight(rec);
    bool level = n == 2 && *(ULONG_PTR *)(rec + 0x48 + 0x20) == (ULONG_PTR)g_six_cpu[1] && *(int *)(g_six_cpu[1] + 0x18) == 83 &&
                 !strcmp(g_six_cpu[1] + 0x46, "eneprog003.ssb") && *(int *)(g_six_cpu[2] + 0x18) == 83 &&
                 !strcmp(g_six_cpu[2] + 0x46, LOB_CPU_PROG) && *(int *)(c1 + 0x18) == 7 && *(ULONG_PTR *)(rec + 0x20) == 0;
    g_cpu_level = 0;                                                                  // the game's own: only the dummy changes
    *(ULONG_PTR *)(rec + 0x48 + 0x20) = (ULONG_PTR)c1; *(ULONG_PTR *)(rec + 0x90 + 0x20) = (ULONG_PTR)c2;
    bool own = sixf_cpu_fight(rec) == 1 && *(ULONG_PTR *)(rec + 0x48 + 0x20) == (ULONG_PTR)c1;
    char c3[0x60]; memcpy(c3, c1, sizeof(c3)); g_cwoprog_test = 1;                    // v5.14 installed: Brutal runs it
    cpu_level_apply(c3, 2);
    bool prog = !strcmp(c3 + 0x46, CWO_PROG) && *(int *)(c3 + 0x18) == 83;
    cpu_level_apply(c3, 1);                                                           // Tough: the row only
    tbl[10 * 82 + 7] = 95;                                                            // v5.14.2 Tough's row 82 there too
    memcpy(c3, c1, sizeof(c3)); cpu_level_apply(c3, 1);
    prog &= !strcmp(c3 + 0x46, "eneprog003.ssb") && *(int *)(c3 + 0x18) == 82;        // reads rows: its own program kept
    strcpy_s(c3 + 0x46, 0x16, "eneprog001_nodef.ssb"); cpu_level_apply(c3, 1);
    prog &= !strcmp(c3 + 0x46, LOB_CPU_PROG) && *(int *)(c3 + 0x18) == 82;          // reads none: eneprog006
    tbl[10 * 82 + 7] = 0;
    level &= prog;
    g_cpu_level = keep_lv; g_cpu_lv_loaded = keep_ld; g_cpu_lv_test_tbl = NULL; g_cwoprog_test = -1;
    memcpy(g_six_cpu, keep, sizeof(keep));
    g_cpu_life_test_tbl = tbl;
    cpu_life_begin(35);
    bool in = tbl[4 + 10 * 0] == 35 && tbl[4 + 10 * 85] == 35 && tbl[4 + 10 * 86] == 0 && tbl[10 * 83 + 7] == 100;
    cpu_life_begin(50);                                                               // a second begin changes nothing
    in = in && tbl[4 + 10 * 5] == 35;
    cpu_life_end();
    bool back = tbl[4 + 10 * 0] == 20 && tbl[4 + 10 * 85] == 20 && tbl[4 + 10 * 86] == 0 && !g_cpu_life_tbl;
    g_cpu_life_test_tbl = NULL;
    // v5.13 a quest record titled as a saved quest that asks for life 40: fighter 0 gets 40; another title: untouched
    static char qrec[0x60], fighter[0x80];
    memset(qrec, 0, sizeof(qrec)); memset(fighter, 0, sizeof(fighter));
    *(short *)(qrec + 2) = 7; strcpy_s(qrec + 4, 0x30, "Life test");
    int *fw = (int *)(fighter + 0x40); fw[3] = fw[4] = fw[5] = 20;
    EdCreator &qcr = g_edc[EDP_QUEST];
    int keep_n = qcr.n; bool keep_loaded = g_ed_loaded;
    EdItem keep0 = qcr.item[0];
    g_ed_loaded = true;
    ed_defaults(EDP_QUEST, qcr.item[0], "Life test"); qcr.item[0].v[0] = 7; qcr.item[0].v[28] = 40; qcr.n = 1;
    g_edq_test_rec = qrec; g_edq_test_fighter = fighter;
    ed_quest_after_start();
    bool qlife = fw[3] == 40 && fw[4] == 40 && fw[5] == 40;
    fw[3] = fw[4] = fw[5] = 20; strcpy_s(qrec + 4, 0x30, "Another quest");
    ed_quest_after_start();
    qlife &= fw[3] == 20;
    g_edq_test_rec = NULL; g_edq_test_fighter = NULL;
    qcr.item[0] = keep0; qcr.n = keep_n; g_ed_loaded = keep_loaded;
    logf("self-test: CPU level on every offline CPU %s, the game's own kept %s, the mode's life in %s / back %s, quest life %s",
         level ? "ok" : "FAILED", own ? "ok" : "FAILED", in ? "ok" : "FAILED", back ? "ok" : "FAILED", qlife ? "ok" : "FAILED");
    return level && own && in && back && qlife ? 1 : -73;
}
static bool g_create_lite;                                        // v5.90.3 the lite match-start hook (no six-fighter build)
static void sixf_before_create_main() {
    __try { npc_swap_records(); } __except (EXCEPTION_EXECUTE_HANDLER) {}   // v5.99 the NPC page: each NPC its own row
    // v5.3.7 (owner 17:05: a 1 vs 1 after a 3 vs 3 crashed, dump PDUWP.exe.203840 at exe+0x14f52b, FUN_14014f4e0 reading
    // battle data 5 of the last match): FUN_1401281e0, which FUN_140128990 runs just before this, clears entries 1-4 of the
    // fighter, battle data and object tables one by one, so entries 5-8 of the moved tables are cleared here the same way
    if (!g_create_lite) __try {
        for (int t = 0; t < 3; t++)
            if (g_six_tab[t])
                for (int i = 4; i < SIXF_MAX; i++) ((ULONG_PTR *)g_six_tab[t])[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try {
        char *rec = (char *)cwo_gaddr(RVA6_SETUP);
        if (!g_create_lite) {                                   // the game's own table has 4 records: nothing past them
            memcpy(g_six_keep, rec, sizeof(g_six_keep));
            memset(rec + 4 * 0x48, 0, 4 * 0x48);
        }
        InterlockedExchange(&g_six_match, 0);
        int origin = *(int *)(g_base + RVA6_ORIGIN), type = *(int *)(g_base + RVA6_TYPE);
        if (origin == 1 && type != 0 && type != 4 && type != 5 && !g_forge_session && !ed_online()) {
            // v5.152 SINGLE PLAY One-on-One / Tag Team / Battle Royale: the lobby's 2 or 4 fighters in the game's own 4 records
            if (g_lob_go && g_lob_go_n <= 4) {
                int ns4 = *(int *)(*(char **)(g_base + RVA6_SKILLHDR) + 4);
                if (lob_fill(rec, (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR), (float *)cwo_gaddr(RVA6_AURA), g_six_cpu, ns4, false, g_lob_go_n)) {
                    logf("SINGLE PLAY: the lobby's %d fighters set up", (int)g_lob_go_n);
                    sixf_cpu_fight(rec);
                    if (g_ed_rules_written) cpu_life_begin(g_ed_armed_vals[1]);
                    return;
                }
                logf("SINGLE PLAY: this battle has no CPU record to build the lobby's fighters from - the battle as the game set it");
            }
            sixf_cpu_fight(rec);
            if (g_ed_rules_written) cpu_life_begin(g_ed_armed_vals[1]);    // v5.12 the mode's life for the CPUs too
            // v5.34 the Mirror: player 1's double in a free record of their team (cwo_mirror.inl)
            // v5.36 (owner; log 19:11: a Boss Raid match, JD the ally, Germano the boss):
            // Boss Raid filled the match and returned before the Mirror got its record. The Mirror goes first when the
            // arsenal holds it (one record), Boss Raid's allies take the records left
            bool mirror = !sixf_mode_wants_six() && !g_lob_go &&
                mr_fill(rec, (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR), *(int *)(*(char **)(g_base + RVA6_SKILLHDR) + 4), (float *)cwo_gaddr(RVA6_AURA)) != 0;
            // v5.131 the Hologram: its double in the next free record (cwo_hologram.inl)
            bool holo = !sixf_mode_wants_six() && !g_lob_go &&
                ho_fill(rec, (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR), *(int *)(*(char **)(g_base + RVA6_SKILLHDR) + 4), (float *)cwo_gaddr(RVA6_AURA)) != 0;
            // v5.26 Boss Raid: a Single Play battle against a story boss gets your CPU allies (cwo_bossraid.inl)
            br_boss_fight(rec);                                 // v5.113 the Boss Fight: a CPU opponent becomes the boss
            if (br_fill(rec, (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR), (float *)cwo_gaddr(RVA6_AURA), *(int *)(*(char **)(g_base + RVA6_SKILLHDR) + 4))) return;
            if (mirror || holo) return;
        }
        // v5.18 a split-screen battle with another game type: its CPU fighters (cwo_gametype.inl). v5.20.1: the split-screen
        // battle is FUN_1401d0750 -> FUN_140128990(stage, 0, 6, records): type 6 (set before the fighters are created; the
        // origin 4 only after), two human records
        if (type == 6 && !g_forge_session && !ed_online() && !g_lob_go) {
            int n = gt_fill(rec, (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR), (float *)cwo_gaddr(RVA6_AURA),
                            *(int *)(*(char **)(g_base + RVA6_SKILLHDR) + 4), sixf_spawn_count(1) >= 2);
            // v5.113 the Boss Fight in split screen: after the game type's CPUs exist, the first one on the other side
            if (br_boss_fight(rec) || (!n && br_split_boss(rec)))    // v5.117 two players, no CPU: the boss is added
                br_fill(rec, (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR), (float *)cwo_gaddr(RVA6_AURA), *(int *)(*(char **)(g_base + RVA6_SKILLHDR) + 4));
            // v5.148.1 (owner 2026-11-10; log: no "mirror:" / "hologram:" set-up line - the doubles were made only
            // for Single Play, origin 1): in a split-screen match where the game type added no CPU (One-on-One, Map Test) the
            // free records 3 / 4 take the Mirror's and the Hologram's parked doubles, as in Single Play
            // v5.148.8 OFF again (owner 17:1x: "those invisible characters are running around again ... they have their own
            // spawns ... picking up capsules"): a parked double is parked at its own start and its CPU still moves, picks up
            // capsules and fights; parking is not real in split screen. Off until the doubles are made at the cast instead.
            static const bool MR_SPLIT_DOUBLES = true;      // v5.148.15 on again: parked doubles are inert now (brain off, 300 m up)
            if (MR_SPLIT_DOUBLES && !n && !sixf_mode_wants_six()) {
                int ns6 = *(int *)(*(char **)(g_base + RVA6_SKILLHDR) + 4);
                mr_fill(rec, (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR), ns6, (float *)cwo_gaddr(RVA6_AURA));
                ho_fill(rec, (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR), ns6, (float *)cwo_gaddr(RVA6_AURA));
            }
            if (n) { if (n == 4) InterlockedExchange(&g_six_match, 1); return; }
        }
        if (g_create_lite || !sixf_mode_wants_six()) return;
        if (origin != 1 || type == 0 || type == 4 || type == 5 || type == 6) {
            logf("six fighters: a 3 vs 3 mode is on, but this match (origin %d, type %d) is not an offline versus - 4 fighters", origin, type);
            return;
        }
        if (!*(int *)rec) { logf("six fighters: no player 1 in this match - 4 fighters"); return; }
        if (g_lob_go) {                                         // v5.2 the 3 VS 3 LOBBY's six fighters (cwo_lobby.inl)
            int ns = *(int *)(*(char **)(g_base + RVA6_SKILLHDR) + 4);
            if (lob_fill(rec, (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR), (float *)cwo_gaddr(RVA6_AURA), g_six_cpu, ns, sixf_spawn_count(1) >= 2, g_lob_go_n)) {   // v5.188.26 5 for Battle Royale 5
                logf("six fighters: the lobby's %d fighters set up", (int)g_lob_go_n);
                InterlockedExchange(&g_six_match, 1);
                return;
            }
            logf("six fighters: this battle has no CPU record to build the lobby's fighters from - teams made from copies");
        }
        // v5.7 (owner 19:05: "I went to create a skill and it has 6 players ... a different mode that I didn't even attend to
        // create"): a 3 vs 3 mode left on must not fill the creators' Skill Test Stage (Map Slot 7 holding pcblank)
        if (*(int *)(g_base + RVA_STAGE) == LIST_STAGE && g_mx_cur >= 0 && g_mx_cur < g_mx_count && !strcmp(g_mx[g_mx_cur].file, "pcblank")) {
            logf("six fighters: the Skill Test Stage - the creators' test stays as the game sets it (4 fighters)");
            return;
        }
        // v5.32 (owner 2026-10-27 19:19 - the log: a Create a Stage match on stage 1 had been made 3 vs 3 by the game type
        // left on 3v3, then the exit crashed at exe 14014fe27): no Edgar's Dream creator match (stage, skill, try it now) is
        // ever made six fighters; only the 3 VS 3 LOBBY (above) is
        if (g_forge_session) {
            logf("six fighters: an Edgar's Dream creator match - it stays as the game sets it (4 fighters)");
            return;
        }
        int ta = *(int *)(rec + 4), tb = -1, na = 0, nb = 0, cpu_a = -1, cpu_b = -1, used = 0;
        for (int p = 0; p < 4; p++) {
            char *r = rec + 0x48 * p;
            if (!*(int *)r) continue;
            int t = *(int *)(r + 4), slot = *(int *)(r + 0x18);
            if (t != ta && tb < 0) tb = t;
            bool cpu = *(int *)(r + 0x10) == 0 && *(ULONG_PTR *)(r + 0x20);
            if (t == ta) { na++; if (cpu && cpu_a < 0) cpu_a = p; }
            else if (t == tb) { nb++; if (cpu && cpu_b < 0) cpu_b = p; }
            if (slot >= 0 && slot < 4) used |= 1 << slot;
        }
        if (tb < 0) tb = ta == 0 ? 1 : 0;
        if (cpu_a < 0 && cpu_b < 0) { logf("six fighters: no CPU in this match to copy - 4 fighters"); return; }
        int nskills = *(int *)(*(char **)(g_base + RVA6_SKILLHDR) + 4);
        ULONG_PTR *ptrs = (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR);
        float *aura = (float *)cwo_gaddr(RVA6_AURA);
        ULONG_PTR *targets = (ULONG_PTR *)cwo_gaddr(RVA6_TARGETS);
        bool block1 = sixf_spawn_count(1) >= 2;                 // a six-spawn stage: start points 5-6 of its own
        int added = 0;
        for (int i = 1; i < 6; i++) {
            char *r = rec + 0x48 * i;
            if (*(int *)r) continue;
            int t, src;
            if (na < 3) { t = ta; src = cpu_a >= 0 ? cpu_a : cpu_b; na++; }
            else if (nb < 3) { t = tb; src = cpu_b >= 0 ? cpu_b : cpu_a; nb++; }
            else break;
            char *s = rec + 0x48 * src;
            memcpy(r, s, 0x48);
            *(int *)(r + 4) = t;
            memcpy(g_six_cpu[i], *(void **)(s + 0x20), 0x5c);
            *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)g_six_cpu[i];
            // v5.7 (owner: the copies "all stood still"): a copy of the practice dummy (program chkprog*, Single Play's
            // "Basic/Enemy" row) gets the fighting program the 3 VS 3 LOBBY uses (FUN_14017b350 loads com\ai\<+0x46>)
            if (!_strnicmp(g_six_cpu[i] + 0x46, "chkprog", 7)) {
                memset(g_six_cpu[i] + 0x46, 0, 0x16);
                strcpy_s(g_six_cpu[i] + 0x46, 0x16, LOB_CPU_PROG);
            }
            int slot = *(int *)(s + 0x18);                       // a start point: a free one of the first 4, or 5-6 on a six-spawn
            if (i >= 4 && block1) slot = i;                      // stage, or the copied fighter's own
            else for (int k = 0; k < 4; k++) if (!(used & (1 << k))) { slot = k; used |= 1 << k; break; }
            *(int *)(r + 0x18) = slot;
            if (ptrs[i] && ptrs[src] && nskills > 0 && nskills < 4096) memcpy((void *)ptrs[i], (void *)ptrs[src], (size_t)(nskills - 1) * 4);   // FUN_14014ede0: (count - 1) ints
            aura[i] = aura[src];
            targets[i] = targets[src];
            added++;
            logf("six fighters: fighter %d = copy of fighter %d (CPU), team %d, start %d", i + 1, src + 1, t, slot);
        }
        logf("six fighters: 3 vs 3 set up - %d fighters added, team %d has %d, team %d has %d%s", added, ta, na, tb, nb,
             block1 ? "" : " (the stage has 4 start points: some share)");
        InterlockedExchange(&g_six_match, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("six fighters: setting up the 3 vs 3 faulted - 4 fighters");
        __try {
            char *rec = (char *)cwo_gaddr(RVA6_SETUP);
            memcpy(rec, g_six_keep, sizeof(g_six_keep));
            memset(rec + 4 * 0x48, 0, 4 * 0x48);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        InterlockedExchange(&g_six_match, 0);
    }
}

static void dr_at_create();                                    // v5.25 the draft arsenal (cwo_draft.inl)
static void sixf_before_create() { sixf_before_create_main(); dr_at_create(); }
// v5.90.3 (owner 2026-11-03): the lobby lists Tag Team / Battle Royale CPUs (ops 57-60) but only the
// six-fighter build (CwoSixFighters.txt "on") has the match-start call that adds them (gt_fill), so with it off they never
// came in. Without that build the loader now puts only that call at FUN_140128cc0's entry (the same place, original
// instruction `mov qword ptr [rsp + 8], rcx` = 48 89 4c 24 08 checked first) and runs the 4-fighter part of
// sixf_before_create (the game's 4 records only: the CPUs of Tag Team / Battle Royale, Boss Raid allies, the Mirror, the CPU
// level).
static int g_create_lite_state;
static void __fastcall create_lite() {
    __try { g_create_lite = true; sixf_before_create_main(); dr_at_create(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_create_lite = false;
    __try {                                                       // v5.95 the lobby rule Aura Rush: every fighter's aura factor x 2
        if (ed_online() && bn_rule() == 6) {
            float *aura = (float *)(g_base + RVA6_AURA);
            for (int i = 0; i < 4; i++) if (aura[i] > 0 && aura[i] < 50) aura[i] *= 2.0f;
            logf("lobby rule Aura Rush: aura factors x 2 (%.2f %.2f %.2f %.2f)", aura[0], aura[1], aura[2], aura[3]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try {                                                       // probe 2026-11-03: the 4 setup records (the split-screen camera question)
        const unsigned char *rec = (const unsigned char *)(g_base + RVA6_SETUP);
        for (int i = 0; i < 4; i++) {
            char hx[0x48 * 3 + 1]; int n = 0;
            for (int k = 0; k < 0x48; k += 4) n += sprintf_s(hx + n, sizeof(hx) - n, "%08x ", *(const unsigned *)(rec + 0x48 * i + k));
            logf("setup record %d: %s", i + 1, hx);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void create_lite_install() {
    if (g_sixf2_state == 1 || g_create_lite_state) return;
    unsigned char *site = (unsigned char *)(g_base + 0x128cc0);
    static const unsigned char OLD[5] = {0x48, 0x89, 0x4c, 0x24, 0x08};
    if (memcmp(site, OLD, 5)) { g_create_lite_state = -1; logf("match start call: FUN_140128cc0 is not the game's - CPUs of the lobby game types off"); return; }
    ULONG_PTR stub = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!stub) { g_create_lite_state = -1; return; }
    unsigned char *c = (unsigned char *)stub; int n = 0;
    auto put = [&](std::initializer_list<unsigned char> b) { for (unsigned char x : b) c[n++] = x; };
    put({0x51, 0x52, 0x41, 0x50, 0x41, 0x51});                    // push rcx, rdx, r8, r9
    put({0x48, 0x83, 0xec, 0x28});                                // sub rsp, 0x28 (shadow + alignment)
    put({0x48, 0xb8}); *(ULONGLONG *)(c + n) = (ULONGLONG)(ULONG_PTR)create_lite; n += 8;   // mov rax, create_lite
    put({0xff, 0xd0});                                            // call rax
    put({0x48, 0x83, 0xc4, 0x28});                                // add rsp, 0x28
    put({0x41, 0x59, 0x41, 0x58, 0x5a, 0x59});                    // pop r9, r8, rdx, rcx
    put({0x48, 0x89, 0x4c, 0x24, 0x08});                          // the game's own first instruction
    put({0x48, 0xb8}); *(ULONGLONG *)(c + n) = (ULONGLONG)(g_base + 0x128cc5); n += 8;     // mov rax, back
    put({0xff, 0xe0});                                            // jmp rax
    FlushInstructionCache(GetCurrentProcess(), c, n);
    LONGLONG rel = (LONGLONG)stub - (LONGLONG)(ULONG_PTR)(site + 5);
    if (rel > 0x7fffffffLL || rel < -0x80000000LL) { g_create_lite_state = -1; logf("match start call: no memory within reach"); return; }
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) { g_create_lite_state = -1; return; }
    site[0] = 0xe9; *(int *)(site + 1) = (int)rel;
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    g_create_lite_state = 1;
    logf("match start call: FUN_140128cc0 -> the loader (stub %p) - the lobby's Tag Team / Battle Royale CPUs join the match", (void *)stub);
}

// ---- v5.102 the BFR rules leave the rating alone (owner 2026-11-03). Game facts
// (Ghidra full pass 065 + capstone, 2026-11-03): FUN_140187370 (session start) sets *0x1405277cc bit 0 = a rated session and
// copies the session's rule (0x140518cf0) to 0x1405277fc; after the match FUN_140187710 (and FUN_140188290) call the rating
// writer FUN_1401835c0 (rating 0x14051c0b0 += the change, clamped 0..999999, + the stats upload) only when bit 0 is set and
// that rule is below 3 - so Heavy (3), Special Environment (4) ... Annihilate the Odd One (11) are unrated, the game type plays
// no part. Our eight rules sit on the game's Standard Battle (0); while one is the session's rule (bn_rule), the writer's entry
// returns at once - the same as the game's unrated rules (nothing else changes: the rule shown and sent stays the game's).
static int g_rate_state;
static int __fastcall rate_skip() {
    __try {
        if (ed_online() && bn_rule() > 0) { logf("rating: the session's rule is a BFR rule (%d) - no rating change, as the game's unrated rules", bn_rule()); return 1; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
static void rate_install() {
    if (g_rate_state) return;
    unsigned char *site = (unsigned char *)(g_base + 0x1835c0);
    static const unsigned char OLD[5] = {0x48, 0x89, 0x5c, 0x24, 0x08};   // mov [rsp + 8], rbx
    if (memcmp(site, OLD, 5)) { g_rate_state = -1; logf("rating: FUN_1401835c0 is not the game's - the BFR rules stay rated"); return; }
    ULONG_PTR stub = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!stub) { g_rate_state = -1; return; }
    unsigned char *c = (unsigned char *)stub; int n = 0;
    auto put = [&](std::initializer_list<unsigned char> b) { for (unsigned char x : b) c[n++] = x; };
    put({0x51, 0x52, 0x41, 0x50, 0x41, 0x51});                    // push rcx, rdx, r8, r9
    put({0x48, 0x83, 0xec, 0x28});                                // sub rsp, 0x28
    put({0x48, 0xb8}); *(ULONGLONG *)(c + n) = (ULONGLONG)(ULONG_PTR)rate_skip; n += 8;   // mov rax, rate_skip
    put({0xff, 0xd0});                                            // call rax
    put({0x48, 0x83, 0xc4, 0x28});                                // add rsp, 0x28
    put({0x41, 0x59, 0x41, 0x58, 0x5a, 0x59});                    // pop r9, r8, rdx, rcx
    put({0x85, 0xc0});                                            // test eax, eax
    put({0x74, 0x03});                                            // jz +3 (the game's own path)
    put({0x31, 0xc0, 0xc3});                                      // xor eax, eax; ret (skipped)
    put({0x48, 0x89, 0x5c, 0x24, 0x08});                          // the game's own first instruction
    put({0x48, 0xb8}); *(ULONGLONG *)(c + n) = (ULONGLONG)(g_base + 0x1835c5); n += 8;     // mov rax, back
    put({0xff, 0xe0});                                            // jmp rax
    FlushInstructionCache(GetCurrentProcess(), c, n);
    LONGLONG rel = (LONGLONG)stub - (LONGLONG)(ULONG_PTR)(site + 5);
    if (rel > 0x7fffffffLL || rel < -0x80000000LL) { g_rate_state = -1; logf("rating: no memory within reach"); return; }
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) { g_rate_state = -1; return; }
    site[0] = 0xe9; *(int *)(site + 1) = (int)rel;
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    g_rate_state = 1;
    logf("rating: FUN_1401835c0 -> the loader first (the BFR rules unrated)");
}

// end of FUN_1401282f0 (start_done_hook), game thread: entries 5-8 of the stores the game resets for fighters 1-4 only,
// set to the same start values (FUN_1401282f0, FUN_140128990, FUN_14017b5a0)
static void sixf_after_start() {
    cpu_life_end();                                            // v5.12 the enemyparam rows' life back (set up is done)
    ed_quest_after_start();                                    // v5.13 Create a Quest: your life
    __try {
        int *rank = (int *)cwo_gaddr(0x4bc5c0), *outf = (int *)cwo_gaddr(0x4bca08), *b8a8 = (int *)cwo_gaddr(0x4bc8a8);
        int *d58 = (int *)cwo_gaddr(0x4bbd58), *cb38 = (int *)cwo_gaddr(0x4bcb38), *e10 = (int *)cwo_gaddr(0x4bbe10);
        for (int i = 4; i < SIXF_MAX; i++) { rank[i] = -1; outf[i] = 0; b8a8[i] = -1; d58[i] = 0; cb38[i] = 0; e10[i] = 0; }
        memset((char *)cwo_gaddr(0x4bc800) + 4 * 0x24, 0xFF, 4 * 0x24);          // capsule view: -1
        memset((char *)cwo_gaddr(0x4bbd70) + 4 * 0x24, 0, 4 * 0x24);             // script flags
        memset((char *)cwo_gaddr(0x4bc970) + 0x40, 0, 0xC0);                     // kills with fighters 5-6 (kill_count stub)
        char *lock = (char *)cwo_gaddr(0x58bd40);                                // 0x1c records: 0, 0, -1, -1
        for (int i = 4; i < SIXF_MAX; i++) {
            char *l = lock + 0x1c * i;
            memset(l, 0, 0xc);
            memset(l + 0xc, 0xFF, 0x10);
        }
        memset((char *)cwo_gaddr(0x4bcc60) + 4 * 8, 0, 4 * 8);
        // FUN_140128990 clears present 1-4 after this, then sets every filled record's flag (its loop reaches the new end)
        int *present = (int *)cwo_gaddr(0x4bc8c8);
        for (int i = 4; i < SIXF_MAX; i++) present[i] = 0;
        char *psp = *(char **)(g_base + 0x4bbd50);                               // pdBattlePsp: records moved to +0x1520
        if (psp)                                                                  // FUN_140128990: flag 0x20 off per record
            for (int i = 4; i < SIXF_MAX; i++) *(unsigned *)(psp + 0x1520 + 0x120 * i) &= ~0x20u;
        char *aura = *(char **)(g_base + 0x4bbee0);                              // pdEftAura01 (0x350 now): records 5-8
        if (aura)                                                                 // start with no effect, as FUN_14019e280
            for (int i = 4; i < SIXF_MAX; i++) *(int *)(aura + 0x40 + 0x60 * i) = -1;   // does for 1-4
        if (g_six_match) { logf("six fighters: match started with 6 fighters"); sixf_block2_fill(); }   // v5.180.2
        sixhud_reset();                                        // v5.7 fighters 5 / 6's HUD entries, as the game's 0-3
        // v5.6 (owner screenshots 17:02 / 18:30: player 1's panel read "ranked"): in a 3 VS 3 LOBBY match player 1 is named
        // after the slot's character, as the CPUs are (names store 0x4bca30, 0x41 each, FUN_1401276e0)
        if (g_six_match && g_lob_go && g_lob_nch > 0) {
            char *names = (char *)cwo_gaddr(0x4bca30);
            const LobChar &c = g_lob_ch[g_lob_go_slots[0].ch];
            if (c.name[0]) { memset(names, 0, 0x41); strncpy_s(names, 0x41, c.name, _TRUNCATE); }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    bigslot_log_match();                                        // v5.4 where each fighter's model goes
}

// ---- v5.5 match diagnostics (owner 18:30: CPUs stand still, fighters 5 / 6 cannot be locked on to or hit by projectiles,
// the match does not end when everyone is down). Logging only; nothing is written to the game.
//  - 5 s after a match starts: game type *0x1404bc9d4, stage *0x1404c01d8, CPU stage data *0x1404c0180 / ready *0x1404c015c
//    (FUN_1401282f0 / FUN_140176630; FUN_1401775b0 runs the brains while it is set), and per fighter its team (+0x1c), ready
//    (+0x28), driver (+0x30: 0 brain), brain on (+0x142d0), brain loaded (+0x14408 + 0x24e0, FUN_14017b4a0), program (setup
//    record +0x20 -> +0x46), life (+0x4c / +0x50) and state (+0x12f04);
//  - during the match: each fighter's KO state 0x1b entered / left, active (+0x20) changes, and the winner *0x1404bbe04 /
//    the results (rank store 0x4bc5c0) when they change (FUN_14012c8c0).
static int g_dg_state[8], g_dg_active[8], g_dg_win, g_dg_res;
static ULONGLONG g_dg_t0;
static bool g_dg_done, g_dg_was;
static void sixf_diag_step() {
    if (g_in_match != (LONG)g_dg_was) {
        g_dg_was = g_in_match != 0;
        g_dg_t0 = GetTickCount64(); g_dg_done = false;
        for (int i = 0; i < 8; i++) { g_dg_state[i] = -2; g_dg_active[i] = -2; }
        g_dg_win = -2; g_dg_res = -2;
    }
    if (!g_in_match) return;
    __try {
        ULONG_PTR *act = cwo_actors();
        char *rec = (char *)cwo_gaddr(RVA6_SETUP);
        if (!g_dg_done && GetTickCount64() - g_dg_t0 > 5000) {
            g_dg_done = true;
            logf("match check: type %d, stage %d, CPU stage data %p (ready %d)", *(int *)(g_base + RVA6_TYPE), *(int *)(g_base + 0x4c01d8),
                 *(void **)(g_base + 0x4c0180), *(int *)(g_base + 0x4c015c));
            for (int i = 0; i < SIXF_MAX; i++) {
                ULONG_PTR a = act[i];
                if (!a) continue;
                const char *prog = "";
                ULONG_PTR cr = *(ULONG_PTR *)(rec + 0x48 * i + 0x20);
                if (cr) prog = (const char *)(cr + 0x46);
                logf("match check: fighter %d team %d ready %d driver %d brain %d loaded %d program %.20s life %d/%d state %x",
                     i + 1, *(int *)(a + 0x1c), *(int *)(a + 0x28), *(int *)(a + 0x30), *(int *)(a + 0x142d0),
                     *(int *)(a + 0x14408 + 0x24e0), prog, *(int *)(a + 0x4c), *(int *)(a + 0x50), *(int *)(a + 0x12f04));
                // v5.11.1 / v5.11.2 (owner 2026-10-26: how the CPU works): the CPU program's
                // memory, read only. FUN_14017b220 (the CPU tick) runs the program with the block = brain + 0x2070, the
                // brain at fighter + 0x14408 (FUN_14017b020 / FUN_14017b100 set it up); slot k = block + 4 + 4k. The set-up
                // copies bytes 1-9 of com\param\enemyparam.epa record N (N = CPU record +0x18 or quest slot +0x34) into
                // slots 30-38 (brain + 0x20ec .. 0x210c); 5.11.1 read the other controller's block (fighter + 0x397a0)
                const int *blk = cr ? (const int *)(a + 0x14408 + 0x2070) : NULL;
                if (blk) {
                    char line[320];
                    int n = sprintf_s(line, "match check: fighter %d CPU slots 20-38:", i + 1);
                    for (int k = 20; k <= 38 && n > 0 && n < 300; k++) n += sprintf_s(line + n, sizeof(line) - n, " %d", blk[1 + k]);
                    logf("%s", line);
                    const int *w = (const int *)cr;
                    logf("match check: fighter %d CPU record %08x %08x %08x %08x %08x %08x %08x %08x", i + 1, w[0], w[1], w[2], w[3],
                         w[4], w[5], w[6], w[7]);
                }
            }
            // v5.6 the lockable list *0x1404bcb50 (32 entries; FUN_14024d440 registers each battle data's 4 lock points
            // bd + 0x40 + j * 0xc0 into the first free entries; FUN_1402543e0 accepts entry k as a lock target when its
            // owner battle data (+0xb8) is active, the point's +0x14 / +0x1c are set, the owner's life (bd + 0xc) >= 1 and
            // the team differs): which fighter each entry belongs to and those flags
            char ll[600] = "";
            ULONG_PTR *lk = (ULONG_PTR *)(g_base + 0x4bcb50);
            for (int k = 0; k < 32; k++) {
                ULONG_PTR e = lk[k];
                if (!e) continue;
                ULONG_PTR ob = *(ULONG_PTR *)(e + 0xb8);
                int owner = -1, life = -1;
                if (ob) { ULONG_PTR f = *(ULONG_PTR *)(ob + 0x61a0); owner = f ? *(int *)(f + 0x14) : -1; life = *(int *)(ob + 0xc); }
                sprintf_s(ll + strlen(ll), sizeof(ll) - strlen(ll), " %d:f%d/%d/%d/%d", k, owner + 1, *(int *)(e + 0x14) != 0,
                          *(int *)(e + 0x1c) != 0, life);
            }
            logf("match check: lockable list (entry:fighter/on/on/life):%s", ll);
        }
        for (int i = 0; i < SIXF_MAX; i++) {
            ULONG_PTR a = act[i];
            if (!a) continue;
            int st = *(int *)(a + 0x12f04), on = *(int *)(a + 0x20);
            bool ko = st == 0x1b, was = g_dg_state[i] == 0x1b;
            if (g_dg_state[i] != -2 && ko != was) {
                logf("match check: fighter %d %s (life %d, state %x)", i + 1, ko ? "is down" : "is up again", *(int *)(a + 0x4c), st);
                // v5.7.1 its lock entries (on / on / life): a downed partner is lockable only while the game keeps them on
                char ll[200] = "";
                ULONG_PTR *lk = (ULONG_PTR *)cwo_gaddr(0x4bcb50);
                for (int k = 0; k < 32; k++) {
                    ULONG_PTR e = lk[k], ob = e ? *(ULONG_PTR *)(e + 0xb8) : 0, f = ob ? *(ULONG_PTR *)(ob + 0x61a0) : 0;
                    if (f != a) continue;
                    sprintf_s(ll + strlen(ll), sizeof(ll) - strlen(ll), " %d:%d/%d/%d", k, *(int *)(e + 0x14) != 0,
                              *(int *)(e + 0x1c) != 0, *(int *)(ob + 0xc));
                }
                logf("match check: fighter %d lock entries%s", i + 1, ll[0] ? ll : " none");
            }
            if (g_dg_active[i] != -2 && on != g_dg_active[i]) logf("match check: fighter %d active %d -> %d", i + 1, g_dg_active[i], on);
            g_dg_state[i] = st; g_dg_active[i] = on;
        }
        int win = *(int *)(g_base + 0x4bbe04), res = *(int *)cwo_gaddr(0x4bc5c0);
        if (g_dg_win != -2 && (win != g_dg_win || res != g_dg_res)) logf("match check: winner %d, result of fighter 1 %d", win, res);
        g_dg_win = win; g_dg_res = res;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- v5.6 the battle's end with six fighters (owner 18:30). FUN_140129e70 (a fighter leaves the battle) clears every
// battle-data entry whose owner (+0x61a0 -> +0x14) is that fighter with four unrolled tests, so entries 5-8 of the moved
// table keep a fighter that has left; with the teams-still-in loop now reaching six (SET_AT 0x14012c4f6) that stale entry
// would keep its team in forever. Game thread, every frame: entries 5-8 whose owner is no longer in the fighter table are
// cleared, as FUN_140129e70 does for 1-4.
static void sixf_end_step() {
    if (!g_six_match || !g_in_match || !g_six_tab[1]) return;
    __try {
        ULONG_PTR *act = cwo_actors(), *bd = cwo_bdata();
        for (int j = 4; j < SIXF_MAX; j++) {
            ULONG_PTR b = bd[j];
            if (!b) continue;
            ULONG_PTR owner = *(ULONG_PTR *)(b + 0x61a0);
            int k = owner ? *(int *)(owner + 0x14) : -1;
            if (k >= 0 && k < SIXF_MAX && act[k] == 0) {
                bd[j] = 0;
                logf("six fighters: fighter %d has left the battle - its battle data %d cleared (as the game does for 1-4 at 0x140129e70)", k + 1, j + 1);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- v5.6 lock-on tracer (owner 18:30: "unable to lock on to two of the targets", "Cuff Button was unable to receive ...
// projectiles"). The game's own lock state for fighters (fighter +0x13f1c / +0x13f20 / +0x14210, FUN_140150c30 /
// FUN_14017b220) covers lockable objects; where the player's choice of enemy is kept is not known yet. Logging only: in a
// six-fighter match, the frame RB is pressed and the 20 frames after it, player 1's fighter object (0x10000 - 0x17400) and
// battle data (0 - 0x6200) are compared with the frame before the press; dwords that change to a fighter number (-1..7) or
// to another fighter's / battle data's address are logged (first 6 presses of a match, 24 lines each).
static volatile LONG g_lk_press;                               // set by the input thread on RB in a match
static unsigned char *g_lk_prev_f, *g_lk_prev_b, *g_lk_pre_f, *g_lk_pre_b;
static int g_lk_left, g_lk_presses, g_lk_lines;
static ULONG_PTR g_lk_pa, g_lk_pb;
static const int LK_F0 = 0x10000, LK_F1 = 0x17400, LK_B1 = 0x6200;
static void sixf_lock_trace_step() {
    if (!g_six_match || !g_in_match) { g_lk_presses = 0; return; }
    if (!g_lk_prev_f) {
        g_lk_prev_f = (unsigned char *)malloc(LK_F1 - LK_F0); g_lk_pre_f = (unsigned char *)malloc(LK_F1 - LK_F0);
        g_lk_prev_b = (unsigned char *)malloc(LK_B1); g_lk_pre_b = (unsigned char *)malloc(LK_B1);
        if (!g_lk_prev_f || !g_lk_pre_f || !g_lk_prev_b || !g_lk_pre_b) return;
    }
    __try {
        ULONG_PTR *act = cwo_actors(), *bdt = cwo_bdata();
        ULONG_PTR pa = act[0], pb = bdt[0];
        if (!pa || !pb) return;
        if (InterlockedExchange(&g_lk_press, 0) && g_lk_presses < 6 && pa == g_lk_pa && pb == g_lk_pb) {
            memcpy(g_lk_pre_f, g_lk_prev_f, LK_F1 - LK_F0);    // the frame before the press
            memcpy(g_lk_pre_b, g_lk_prev_b, LK_B1);
            g_lk_left = 20; g_lk_lines = 0; g_lk_presses++;
            logf("lock trace: RB press %d", g_lk_presses);
        }
        if (g_lk_left > 0 && g_lk_lines < 24) {
            g_lk_left--;
            for (int pass = 0; pass < 2; pass++) {
                const unsigned char *cur = (const unsigned char *)(pass ? pb : pa + LK_F0);
                unsigned char *pre = pass ? g_lk_pre_b : g_lk_pre_f;
                int n = pass ? LK_B1 : LK_F1 - LK_F0;
                for (int o = 0; o + 8 <= n && g_lk_lines < 24; o += 4) {
                    int nv = *(const int *)(cur + o), ov = *(const int *)(pre + o);
                    if (nv == ov) continue;
                    ULONG_PTR q = *(const ULONG_PTR *)(cur + o);
                    bool idx = nv >= -1 && nv <= 7 && ov >= -1 && ov <= 7;
                    bool ptr = false;
                    for (int k = 0; k < SIXF_MAX && !(o & 7); k++) ptr |= q && (q == act[k] || q == bdt[k]);
                    if (!idx && !ptr) continue;
                    logf("lock trace: %s +0x%x  %d -> %d%s", pass ? "battle data" : "fighter", (pass ? 0 : LK_F0) + o, ov, nv,
                         ptr ? " (address of another fighter / battle data)" : "");
                    g_lk_lines++;
                    memcpy(pre + o, cur + o, 4);                // report each change once
                }
            }
        }
        memcpy(g_lk_prev_f, (const void *)(pa + LK_F0), LK_F1 - LK_F0);
        memcpy(g_lk_prev_b, (const void *)pb, LK_B1);
        g_lk_pa = pa; g_lk_pb = pb;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- v5.6 downed fighters glow (owner 18:30: "when somebody dies, when their health reaches zero ... their body should
// glow so that we can find exactly where they are"; design TAG_REVIVE_RESEARCH_2026-10-21: a downed partner can be picked
// up into a skill slot and revived, FUN_140261be0 / FUN_140251f80). While a fighter is down (active +0x20, +0x24 == 0, as
// FUN_1402516a0 finds a downed teammate), the effect of Reincarnation (skill 297, the revive skill; its effect id through
// the game's skill -> effect rule, part 0) loops at the fighter's position with the game's own effect calls (the aura FX
// module's start / loop / move / stop); it stops when the fighter is up again or has left the battle. Offline matches with
// a partner (six fighters or the game's 2 vs 2).
static unsigned g_dn_h[8] = {FX_NONE, FX_NONE, FX_NONE, FX_NONE, FX_NONE, FX_NONE, FX_NONE, FX_NONE};
static int g_dn_eff = -1;
static void sixf_down_glow_step() {
    FxGame g;
    bool any_team = false;
    if (!g_in_match || !fx_game(&g)) { memset(g_dn_h, 0xFF, sizeof(g_dn_h)); return; }
    __try {
        ULONG_PTR *act = cwo_actors();
        int team0 = -2;
        for (int i = 0; i < cwo_nfighters(); i++)
            if (act[i] && *(int *)(act[i] + 0x20)) { int t = *(int *)(act[i] + 0x1c); if (team0 == -2) team0 = t; else if (t == team0) any_team = true; }
        if (!any_team || ed_online()) return;                  // free for all: nobody to revive
        if (g_dn_eff < 0) {
            short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)297 * 0x90);
            g_dn_eff = g.effect_of(prof, 297);
            logf("downed glow: Reincarnation (297) profile %d -> effect %d", prof, g_dn_eff);
            if (g_dn_eff <= 0) g_dn_eff = 0;
        }
        if (g_dn_eff <= 0) return;
        for (int i = 0; i < cwo_nfighters() && i < 8; i++) {
            ULONG_PTR a = act[i];
            bool down = a && *(int *)(a + 0x20) && !*(int *)(a + 0x24) && i != g_mr_idx;   // v5.34 not the Mirror's double
            if (!down) {
                if (fx_alive(g, g_dn_h[i])) g.stop(g_dn_h[i]);
                g_dn_h[i] = FX_NONE;
                continue;
            }
            float m[16];
            memcpy(m, g.identity, sizeof(m));
            memcpy(m + 12, (const float *)(a + ACTOR_POS), 12);
            if (fx_alive(g, g_dn_h[i])) { g.move(g_dn_h[i], m); continue; }
            unsigned h = g.start(g.mgr, *(int *)(a + 0x14), g_dn_eff, 0, m, 0);
            if (h == FX_NONE) continue;
            g.loop(h);
            g_dn_h[i] = h;
            logf("downed glow: fighter %d is down - glow %08X", i + 1, h);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

#include "cwo_sixhud.inl"                                // v5.7 fighters 5 and 6 on the game's own HUD
static void ru_flash_extra(int i) { if (i >= 4 && i <= 5) *(int *)(g_sh[i].ent + 0x18) = 0x10; }   // v5.53 (cwo_rules.inl)

// v5.188.26 FUN_14012c440 (teams_clear detour, every end check): teams 4-7 of the moved team arrays start as the game starts
// teams 0-3 - not in (0), no out time (-1)
static void sixf_teams_clear() {
    __try {
        int *fl = (int *)cwo_gaddr(0x4bc890), *tt = (int *)cwo_gaddr(0x4bbeb0);
        if ((ULONG_PTR)fl == g_base + 0x4bc890) return;       // not moved: nothing past team 3
        for (int i = 4; i < 8; i++) { fl[i] = 0; tt[i] = -1; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static ULONG_PTR sixf2_resolve(const char *name) {
    if (!strcmp(name, "sixf_teams_clear")) return (ULONG_PTR)&sixf_teams_clear;   // v5.188.26
    if (!strcmp(name, "sixf_before_create")) return (ULONG_PTR)&sixf_before_create;
    if (!strcmp(name, "sixf_after_start")) return (ULONG_PTR)&sixf_after_start;
    if (!strcmp(name, "sixf_hud_more")) return (ULONG_PTR)&sixf_hud_more;          // v5.7
    if (!strcmp(name, "sixf_hud_popup")) return (ULONG_PTR)&sixf_hud_popup;
    if (!strcmp(name, "sixf_hud_flash")) return (ULONG_PTR)&sixf_hud_flash;
    if (!strcmp(name, "sixf_hud_present")) return (ULONG_PTR)&sixf_hud_present;
    return 0;
}

static ULONG_PTR sixf2_near_alloc(SIZE_T n, DWORD prot) {      // within 2 GB after the image (both addressing forms)
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    ULONG_PTR end = g_base + nt->OptionalHeader.SizeOfImage;
    for (ULONG_PTR a = (end + 0xFFFF) & ~(ULONG_PTR)0xFFFF; a < g_base + 0x40000000; a += 0x10000) {
        void *p = VirtualAlloc((void *)a, n, MEM_RESERVE | MEM_COMMIT, prot);
        if (p) return (ULONG_PTR)p;
    }
    return 0;
}

// the detours' first home: the zero padding at the end of .text's last page (clean exe: VirtualSize 0x2d1546). The live
// exe's own patches (IGT-058 stage capsules, the overlay hook) use 0x2d2550 - 0x2d2797, so the detours take
// 0x2d2800 - 0x2d3000: executable already and within reach, no new executable memory needed
static const ULONG_PTR SIXF2_CAVE = 0x2d2800, SIXF2_CAVE_N = 0x800;
static bool sixf2_cave_free() {
    for (ULONG_PTR i = 0; i < SIXF2_CAVE_N; i++) if (*(unsigned char *)(g_base + SIXF2_CAVE + i)) return false;
    return true;
}

static size_t sixf2_stub_bytes() {                             // the detours as sixf2_apply lays them out
    size_t n = 0;
    for (size_t i = 0; i < SIXF2_NSTUBS; i++) n += (SIXF2_STUBS[i].code_len + 15) & ~15u;
    return n;
}

static void sixf2_install() {                                  // DllMain, right after step 1 (which must have moved)
    if (g_sixf_state != 1) return;
    char why[96] = "";
    ULONG_PTR blk = sixf2_near_alloc(SIXF2_BLOCK, PAGE_READWRITE), stubs = 0;
    bool cave = sixf2_cave_free() && sixf2_stub_bytes() <= SIXF2_CAVE_N;   // v5.7: 42 detours need more than the cave
    if (cave) { stubs = g_base + SIXF2_CAVE; g_sixf2_room = SIXF2_CAVE_N; }
    else { stubs = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE); g_sixf2_room = 0x1000; }
    if (!blk || !stubs) {
        g_sixf2_state = -1;
        if (blk) VirtualFree((void *)blk, 0, MEM_RELEASE);
        if (stubs && !cave) VirtualFree((void *)stubs, 0, MEM_RELEASE);
        logf("six fighters step 2: no memory within reach (block %p, detours %p) - 4 fighters", (void *)blk, (void *)stubs);
        return;
    }
    ULONG_PTR text = g_base + 0x1000;
    SIZE_T text_n = 0x2d2000;                                  // .text with its last page (the cave)
    DWORD old = 0;
    if (!VirtualProtect((void *)text, text_n, PAGE_EXECUTE_READWRITE, &old)) {
        g_sixf2_state = -1;
        VirtualFree((void *)blk, 0, MEM_RELEASE);
        if (!cave) VirtualFree((void *)stubs, 0, MEM_RELEASE);
        logf("six fighters step 2: code not writable (%lu) - 4 fighters", GetLastError());
        return;
    }
    bool ok = sixf2_apply(g_base, blk, stubs, sixf2_resolve, why, sizeof(why));
    VirtualProtect((void *)text, text_n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)text, text_n);
    if (!ok) {
        g_sixf2_state = -1;
        VirtualFree((void *)blk, 0, MEM_RELEASE);
        if (!cave) VirtualFree((void *)stubs, 0, MEM_RELEASE);
        logf("six fighters step 2: refused (%s) - nothing changed, 4 fighters", why);
        return;
    }
    if (!cave) {
        DWORD o2 = 0;
        VirtualProtect((void *)stubs, 0x1000, PAGE_EXECUTE_READ, &o2);
        FlushInstructionCache(GetCurrentProcess(), (void *)stubs, 0x1000);
    }
    g_six2_stubs = stubs;
    g_six2_block = blk;                                        // from here cwo_gaddr gives the new addresses
    g_sixf2_state = 1;
    {                                                          // per-match values the game never sets for entries 5-8
        int *rank = (int *)cwo_gaddr(0x4bc5c0), *b8a8 = (int *)cwo_gaddr(0x4bc8a8);
        for (int i = 4; i < SIXF_MAX; i++) { rank[i] = -1; b8a8[i] = -1; }
    }
    logf("six fighters step 2: %zu stores moved (block %p), %zu sites, %zu values, %zu detours (%s %p)", SIXF2_NSTORES,
         (void *)g_six2_block, SIXF2_NSITES, SIXF2_NPATCHES, SIXF2_NSTUBS, cave ? "in the .text padding" : "new page", (void *)stubs);
}

static int sixf2_self_test() {                                 // the step-2 patcher on a stand-in image carrying its bytes
    const SIZE_T img = 0x600000;
    unsigned char *base = (unsigned char *)VirtualAlloc(NULL, img + 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) return -60;
    for (size_t i = 0; i < SIXF2_NSITES; i++) memcpy(base + SIXF2_SITES[i].rva, SIXF2_SITES[i].b, SIXF2_SITES[i].len);
    for (size_t i = 0; i < SIXF2_NPATCHES; i++) memcpy(base + SIXF2_PATCHES[i].rva, SIXF2_PATCHES[i].old, SIXF2_PATCHES[i].len);
    for (size_t i = 0; i < SIXF2_NSTUBS; i++) memcpy(base + SIXF2_STUBS[i].rva, SIXF2_STUBS[i].site, SIXF2_STUBS[i].len);
    char why[96] = "";
    ULONG_PTR block = (ULONG_PTR)base + img, stubs = (ULONG_PTR)base + SIXF2_CAVE;   // the detours in the cave, as in game
    size_t room = g_sixf2_room;
    g_sixf2_room = sixf2_stub_bytes() <= SIXF2_CAVE_N ? SIXF2_CAVE_N : 0x1000;   // as sixf2_install picks
    bool ok = sixf2_apply((ULONG_PTR)base, block, stubs, sixf2_resolve, why, sizeof(why));
    for (size_t i = 0; ok && i < SIXF2_NSITES; i++) {           // every site reaches its store's new copy
        const Sixf2Site &s = SIXF2_SITES[i];
        int d;
        memcpy(&d, base + s.rva + s.dispoff, 4);
        ULONG_PTR tgt = s.mode == 0 ? (ULONG_PTR)base + s.rva + s.len + d : (ULONG_PTR)base + (unsigned)d;
        ok = tgt == block + SIXF2_STORES[s.store].at + s.newoff;
    }
    for (size_t i = 0; ok && i < SIXF2_NSTUBS; i++) ok = base[SIXF2_STUBS[i].rva] == 0xE9;
    ULONG_PTR caps = sixf2_map((ULONG_PTR)base, block, 0x584f80 + 0xc80);  // deck position moved to +0x1900
    bool map_ok = false;
    for (size_t k = 0; k < SIXF2_NSTORES; k++)
        if (SIXF2_STORES[k].rva == 0x584f80) map_ok = caps == block + SIXF2_STORES[k].at + 0x1900;
    unsigned char keep[16];
    memcpy(keep, base + SIXF2_SITES[0].rva, 16);
    bool refused = !sixf2_apply((ULONG_PTR)base, block, stubs, sixf2_resolve, why, sizeof(why)) && !memcmp(keep, base + SIXF2_SITES[0].rva, 16);
    g_sixf2_room = room;
    VirtualFree(base, 0, MEM_RELEASE);
    // v5.9 the practice dummy fights: player 1 (driver) and a fighting CPU untouched, the dummy CPU on a record of its own
    char rec[4 * 0x48] = {0}, dummy[0x60] = {0}, fighter[0x60] = {0};
    strcpy_s(dummy + 0x46, 0x16, "chkprog000.ssb"); strcpy_s(dummy + 0x31, 0x15, "DECK000");
    strcpy_s(fighter + 0x46, 0x16, "eneprog006.ssb");
    *(int *)rec = 1; *(int *)(rec + 0x10) = 1;                            // player 1: a pad
    *(int *)(rec + 0x48) = 1; *(ULONG_PTR *)(rec + 0x48 + 0x20) = (ULONG_PTR)dummy;
    *(int *)(rec + 0x90) = 1; *(ULONG_PTR *)(rec + 0x90 + 0x20) = (ULONG_PTR)fighter;
    char keep6[sizeof(g_six_cpu)];
    memcpy(keep6, g_six_cpu, sizeof(keep6));
    int switched = sixf_cpu_fight(rec);
    char *c1 = *(char **)(rec + 0x48 + 0x20);
    bool fight = switched == 1 && c1 == g_six_cpu[1] && !strcmp(c1 + 0x46, LOB_CPU_PROG) && !c1[0x31] &&
                 !strcmp(dummy + 0x46, "chkprog000.ssb") && *(char **)(rec + 0x90 + 0x20) == fighter && sixf_cpu_fight(rec) == 0;
    memcpy(g_six_cpu, keep6, sizeof(keep6));
    logf("self-test: six fighters step 2 %s (%zu sites, %zu values, %zu detours), capsule remap %s, refused on a second run %s, dummy CPU fights %s",
         ok ? "ok" : "FAILED", SIXF2_NSITES, SIXF2_NPATCHES, SIXF2_NSTUBS, map_ok ? "ok" : "FAILED", refused ? "ok" : "FAILED", fight ? "ok" : "FAILED");
    return ok && map_ok && refused && fight ? 1 : -61;
}

// ---- v5.14.1 Create a Quest "Play as" (owner 2026-10-26).
// Ghidra / capstone on PDUWP 9ba7411b: FUN_14024ea70 (the quest player's set-up, run by FUN_140129260) takes the character
// word from DAT_1404c5238 - the profile's character, set from the profile by FUN_140188810 - with `mov eax, [rip+0x27677b]`
// at 0x14024eab7; the model, sound bank and body numbers all follow it (FUN_14024e190 -> FUN_140250f70 queues
// player\<name>\<name><outfit>.alr). Only that one read is pointed at a dword of the loader's (a page within reach of the
// image), so the profile's own value is never written. Each frame the dword = the profile's character, or - while the
// selected quest (0x14052e820, copied by 4.465 when it is chosen) is a saved quest whose name is its title and that asks for
// one - that character and outfit.
static const ULONG_PTR RVA_QP_LOAD = 0x24eab7, RVA_QP_PROFILE = 0x4c5238, RVA_QP_QUEST = 0x52e820;
static const unsigned char QP_LOAD_BYTES[6] = {0x8b, 0x05, 0x7b, 0x67, 0x27, 0x00};
static volatile LONG *g_qp_word;                                // the dword the patched read takes
static const char *g_qp_test_rec;                               // self-test stand-in for the selected quest
static int qplayer_wanted(const char *qr, int profile) {        // the word FUN_14024ea70 should take
    __try {
        if (!qr || (!g_qp_test_rec && ed_online())) return profile;
        int id = *(const short *)(qr + 2);
        const EdCreator &c = g_edc[EDP_QUEST];
        for (int k = 0; k < c.n; k++) {
            const EdItem &it = c.item[k];
            if (it.v[0] == id && !strcmp(qr + 4, it.name) && it.v[29] >= 0) return (it.v[30] << 16) | (it.v[29] & 0xFFFF);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return profile;
}
static bool qplayer_install() {                                 // DllMain (host checked)
    unsigned char *at = (unsigned char *)(g_base + RVA_QP_LOAD);
    __try {
        if (memcmp(at, QP_LOAD_BYTES, 6)) { logf("quest play-as: refused, the instruction at exe+0x%llx differs", (unsigned long long)RVA_QP_LOAD); return false; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_READWRITE);
    if (!page) { logf("quest play-as: no page within reach - not installed"); return false; }
    LONGLONG d = (LONGLONG)page - (LONGLONG)(g_base + RVA_QP_LOAD + 6);
    if (d < INT_MIN || d > INT_MAX) { VirtualFree((void *)page, 0, MEM_RELEASE); logf("quest play-as: page out of reach"); return false; }
    g_qp_word = (volatile LONG *)page;
    *g_qp_word = *(LONG *)(g_base + RVA_QP_PROFILE);
    DWORD old = 0;
    if (!VirtualProtect(at + 2, 4, PAGE_EXECUTE_READWRITE, &old)) { g_qp_word = NULL; VirtualFree((void *)page, 0, MEM_RELEASE); return false; }
    *(int *)(at + 2) = (int)d;
    VirtualProtect(at + 2, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 6);
    logf("quest play-as: the quest player's character is read from %p (the profile's own unless a saved quest asks)", (void *)page);
    return true;
}
static void qplayer_step() {                                    // CwoOverlayFrame, game thread
    if (!g_qp_word) return;
    __try {
        int profile = *(int *)(g_base + RVA_QP_PROFILE);
        if (!g_ed_loaded) ed_load_all();
        int want = qplayer_wanted((const char *)(g_base + RVA_QP_QUEST), profile);
        if (*g_qp_word != want) {
            InterlockedExchange(g_qp_word, want);
            if (want != profile)
                logf("quest play-as: quest %d '%.24s' - you play as character row %d outfit %d", *(short *)(g_base + RVA_QP_QUEST + 2),
                     (const char *)(g_base + RVA_QP_QUEST + 4), want & 0xFFFF, (want >> 16) + 1);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static int qplayer_self_test() {                                // a selected quest titled as a saved quest that plays as Freia 4
    static char qrec[0x60];
    memset(qrec, 0, sizeof(qrec));
    *(short *)(qrec + 2) = 12; strcpy_s(qrec + 4, 0x30, "As Freia");
    EdCreator &qc = g_edc[EDP_QUEST];
    int keep_n = qc.n; bool keep_loaded = g_ed_loaded;
    EdItem keep0 = qc.item[0];
    g_ed_loaded = true; g_qp_test_rec = qrec;
    ed_defaults(EDP_QUEST, qc.item[0], "As Freia"); qc.item[0].v[0] = 12; qc.item[0].v[29] = 2; qc.item[0].v[30] = 3; qc.n = 1;
    const int profile = (1 << 16) | 0;
    bool as = qplayer_wanted(qrec, profile) == ((3 << 16) | 2);
    qc.item[0].v[29] = -1;
    bool self = qplayer_wanted(qrec, profile) == profile;
    qc.item[0].v[29] = 2; strcpy_s(qrec + 4, 0x30, "Another");
    bool other = qplayer_wanted(qrec, profile) == profile && qplayer_wanted(NULL, profile) == profile;
    g_qp_test_rec = NULL;
    qc.item[0] = keep0; qc.n = keep_n; g_ed_loaded = keep_loaded;
    unsigned char clean[6]; memcpy(clean, QP_LOAD_BYTES, 6);
    bool bytes = clean[0] == 0x8b && clean[1] == 0x05 && (0x24eab7 + 6 + 0x27677b) == 0x4c5238;   // the read's target
    logf("self-test: quest play-as wanted %s, yourself %s, other quests %s, instruction target %s", as ? "ok" : "FAILED",
         self ? "ok" : "FAILED", other ? "ok" : "FAILED", bytes ? "ok" : "FAILED");
    return as && self && other && bytes ? 1 : -75;
}
