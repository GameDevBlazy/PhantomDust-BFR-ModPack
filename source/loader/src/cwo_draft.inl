// ---- v5.25 Draft arsenal (owner 2026-10-27; "go ahead and work on adding all 8 of those ideas"). Edgar's Dream > Draft Arsenal: a pool of
// 24 skills (8 attack, 4 defence, 3 each of erase, status, special and environment); you and the other side pick in
// turns, 10 picks each, each pick 3 capsules = a 30-capsule arsenal. The other side is the CPU (it picks right after you)
// or player 2 (the same controller picks for them). "Use the draft in matches" on: in every offline match your fighter
// plays your draft and every fighter of the other side plays theirs (Single Play, split screen, the 3 VS 3 LOBBY).
// Your saved arsenals are never changed: the draft goes only into the match's own capsule counts.
// Game facts (Ghidra batch_065; recorded for the 3 VS 3 LOBBY, cwo_lobby.inl): the fighters' capsule counts are the
// per-fighter arrays *0x140585fe8[i] (FUN_14014f830 fills them from an arsenal's 30 skill ids: count[id]++, count[0] =
// 30 - the skills), the aura factor 0x1404bc958[i] = GSDATA +0x33034 by school count 1-3 (else *0x1402f7da8); the setup
// records 0x1404bc420 (+4 team, +0x10 human, +0x20 CPU record); a CPU with a deck plan (CPU record +0x31,
// com\deck\<name>, FUN_14017b350) is given its own record without one, as sixf_cpu_fight does, so it plays the counts.
// Skill record (GSDATA records +0x18 + 0x90 id): +0x0E capsule type (1 attack .. 6 environment), +0x12 school (1-5),
// +0x28 aura cost, +0x58 damage.
static const int DR_POOL = 24, DR_PICKS = 10;
static short g_dr_pool[DR_POOL];
static signed char g_dr_owner[DR_POOL];                         // -1 free, 0 you, 1 the other side
static short g_dr_pick[2][DR_PICKS];
static int g_dr_n[2], g_dr_turn, g_dr_vs, g_dr_row, g_dr_top;   // g_dr_vs: 0 the CPU, 1 player 2
static bool g_dr_on, g_dr_loaded;
static char g_dr_cpu[6][0x60];
static int g_dr_t_valid = -1;                                   // self-test: every id up to this is a valid skill

static bool dr_valid(int id) {                                  // a real skill of the arsenal kinds
    if (g_dr_t_valid >= 0) return id >= 1 && id <= g_dr_t_valid;
    if (id < 1 || id > 374) return false;
    int t = fu_stat(id, 0x0E), s = fu_stat(id, 0x12);
    const char *nm = sc_skill_name(id);
    return t >= 1 && t <= 6 && s >= 1 && s <= 5 && nm && nm[0] && nm[0] != '?';
}
static void dr_path(wchar_t *p) { swprintf_s(p, MAX_PATH, L"%sCwoDraft.txt", g_dir); }
static void dr_save() {
    if (g_dr_t_valid >= 0) return;
    wchar_t p[MAX_PATH];
    dr_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# Draft arsenal (loader): on, vs, turn; pool (id owner) x 24\non %d\nvs %d\nturn %d\n", g_dr_on ? 1 : 0, g_dr_vs, g_dr_turn);
    for (int i = 0; i < DR_POOL; i++) fprintf(f, "pool %d %d\n", g_dr_pool[i], g_dr_owner[i]);
    for (int s = 0; s < 2; s++) for (int k = 0; k < g_dr_n[s]; k++) fprintf(f, "pick %d %d\n", s, g_dr_pick[s][k]);
    fclose(f);
}
static void dr_load() {
    if (g_dr_loaded) return;
    g_dr_loaded = true;
    if (g_dr_t_valid >= 0) return;
    wchar_t p[MAX_PATH];
    dr_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[64];
    int a, b, np = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf_s(line, "on %d", &a) == 1) g_dr_on = a != 0;
        else if (sscanf_s(line, "vs %d", &a) == 1) g_dr_vs = a ? 1 : 0;
        else if (sscanf_s(line, "turn %d", &a) == 1) g_dr_turn = a ? 1 : 0;
        else if (sscanf_s(line, "pool %d %d", &a, &b) == 2 && np < DR_POOL) { g_dr_pool[np] = (short)a; g_dr_owner[np++] = (signed char)(b >= 0 && b <= 1 ? b : -1); }
        else if (sscanf_s(line, "pick %d %d", &a, &b) == 2 && a >= 0 && a <= 1 && g_dr_n[a] < DR_PICKS) g_dr_pick[a][g_dr_n[a]++] = (short)b;
    }
    fclose(f);
    if (np != DR_POOL) { memset(g_dr_pool, 0, sizeof(g_dr_pool)); g_dr_n[0] = g_dr_n[1] = 0; }
}
static void dr_deal() {                                         // a new pool: 8 attack, 4 defence, 3 x erase / status / special / environment
    static const int WANT[7] = {0, 8, 4, 3, 3, 3, 3};
    int got = 0;
    memset(g_dr_pool, 0, sizeof(g_dr_pool));
    for (int t = 1; t <= 6; t++) {
        int ids[400], n = 0;
        for (int id = 1; id <= 374 && n < 400; id++)
            if (dr_valid(id) && (g_dr_t_valid >= 0 ? (id % 6) + 1 == t : fu_stat(id, 0x0E) == t)) ids[n++] = id;
        for (int k = 0; k < WANT[t] && n > 0 && got < DR_POOL; k++) {
            int j = rand() % n;
            g_dr_pool[got++] = (short)ids[j];
            ids[j] = ids[--n];
        }
    }
    for (int i = 0; i < DR_POOL; i++) g_dr_owner[i] = -1;
    g_dr_n[0] = g_dr_n[1] = 0;
    g_dr_turn = 0;
    logf("draft: a new pool of %d skills", got);
}
static int dr_cpu_choice() {                                    // the CPU's pick: damage, a fair cost, its own schools, some defence
    int best = -1, bs = -1000, sch[6] = {0}, kinds[7] = {0};
    for (int k = 0; k < g_dr_n[1]; k++) {
        int s = fu_stat(g_dr_pick[1][k], 0x12), t = fu_stat(g_dr_pick[1][k], 0x0E);
        if (s >= 0 && s <= 5) sch[s]++;
        if (t >= 0 && t <= 6) kinds[t]++;
    }
    for (int i = 0; i < DR_POOL; i++) {
        if (g_dr_owner[i] != -1 || !g_dr_pool[i]) continue;
        int id = g_dr_pool[i], t = fu_stat(id, 0x0E), s = fu_stat(id, 0x12), cost = fu_stat(id, 0x28), dmg = fu_stat(id, 0x58);
        int score = dmg * 2 - cost + (t == 1 ? 4 : 0) + (t == 2 && kinds[2] < 2 ? 6 : 0) + (s >= 0 && s <= 5 ? sch[s] * 3 : 0);
        if (score > bs) { bs = score; best = i; }
    }
    return best;
}
static void dr_take(int i) {                                    // the side whose turn it is takes pool row i
    if (i < 0 || i >= DR_POOL || g_dr_owner[i] != -1 || !g_dr_pool[i] || g_dr_n[g_dr_turn] >= DR_PICKS) return;
    g_dr_owner[i] = (signed char)g_dr_turn;
    g_dr_pick[g_dr_turn][g_dr_n[g_dr_turn]++] = g_dr_pool[i];
    if (g_dr_n[g_dr_turn ^ 1] < DR_PICKS) g_dr_turn ^= 1;
    if (g_dr_vs == 0 && g_dr_turn == 1 && g_dr_n[1] < DR_PICKS) dr_take(dr_cpu_choice());   // the CPU answers at once
}
static bool dr_done() { return g_dr_n[0] == DR_PICKS && g_dr_n[1] == DR_PICKS; }

// the counts of one side's draft into a fighter's capsule counts and aura factor
static void dr_fill(int side, int *cnt, float *aura, int nskills, char *gs, float def) {
    memset(cnt, 0, (size_t)(nskills - 1) * 4);
    int used = 0, sch[6] = {0}, nsch = 0;
    for (int k = 0; k < g_dr_n[side]; k++) {
        int id = g_dr_pick[side][k];
        if (id <= 0 || id >= nskills - 1) continue;
        cnt[id] += 3; used += 3;
        int s = fu_stat(id, 0x12);
        if (s >= 1 && s <= 5 && !sch[s]++) nsch++;
    }
    cnt[0] = 30 - used < 0 ? 0 : 30 - used;
    if (aura) *aura = nsch >= 1 && nsch <= 3 && gs ? *(float *)(gs + 0x33034 + 4 * (nsch - 1)) : def;
}
// create_hook, after the other set-ups (game thread)
static void dr_at_create() {
    dr_load();
    if (!g_dr_on || !dr_done() || g_forge_session || ed_online()) return;
    __try {
        int type = *(int *)(g_base + RVA6_TYPE);
        if (type == 0 || type == 4 || type == 5) return;           // online / Download Battle
        char *rec = (char *)cwo_gaddr(RVA6_SETUP);
        ULONG_PTR *ptrs = (ULONG_PTR *)cwo_gaddr(RVA6_CAPPTR);
        float *aura = (float *)cwo_gaddr(RVA6_AURA);
        int nskills = *(int *)(*(char **)(g_base + RVA6_SKILLHDR) + 4);
        char *gs = *(char **)(g_base + RVA_GSDATA);
        float def = *(float *)(g_base + 0x2f7da8);
        if (!*(int *)rec || nskills <= 1 || nskills > 4096) return;
        int me_team = *(int *)(rec + 4), others = 0;
        if (ptrs[0]) dr_fill(0, (int *)ptrs[0], &aura[0], nskills, gs, def);
        for (int i = 1; i < 6; i++) {
            char *r = rec + 0x48 * i;
            if (!*(int *)r || *(int *)(r + 4) == me_team || !ptrs[i]) continue;
            dr_fill(1, (int *)ptrs[i], &aura[i], nskills, gs, def);
            char *c = *(char **)(r + 0x20);
            if (!*(int *)(r + 0x10) && c && c[0x31]) {              // a CPU with a deck plan: its own record without it
                memcpy(g_dr_cpu[i], c, 0x5c);
                memset(g_dr_cpu[i] + 0x31, 0, 0x15);
                *(char **)(r + 0x20) = g_dr_cpu[i];
            }
            others++;
        }
        logf("draft: your draft on fighter 1, the other draft on %d fighter(s)", others);
        set_status("DRAFT: you play your draft, the other side theirs");
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("draft: the set-up faulted - the arsenals as chosen"); }
}

// ---- the page (controller thread)
static const int DR_ROWS = 12;                                  // pool rows on screen
static const char *DR_KIND[7] = {"aura", "attack", "defence", "erase", "status", "special", "environment"};
static const char *DR_SCH[6] = {"-", "Psycho", "Optical", "Nature", "Ki", "Faith"};
// rows: 0 new pool, 1 opponent, 2 .. 2 + 23 the pool, then "use in matches", "back"
static int dr_nrows() { return 2 + DR_POOL + 2; }
static void dr_input(WORD pressed) {
    dr_load();
    int n = dr_nrows();
    if (pressed & BTN_B) { InterlockedExchange(&g_ed_page, EDP_CHOOSER); return; }
    if (pressed & BTN_DOWN) g_dr_row = (g_dr_row + 1) % n;
    if (pressed & BTN_UP) g_dr_row = (g_dr_row + n - 1) % n;
    if ((pressed & (BTN_LEFT | BTN_RIGHT)) && g_dr_row == 1) { g_dr_vs ^= 1; dr_save(); }
    if (!(pressed & BTN_A)) return;
    if (g_dr_row == 0) { dr_deal(); dr_save(); set_status("A new pool: your pick first"); }
    else if (g_dr_row == 1) { g_dr_vs ^= 1; dr_save(); }
    else if (g_dr_row < 2 + DR_POOL) {
        int i = g_dr_row - 2;
        if (!g_dr_pool[0]) { set_status("Deal a pool first (top row)"); return; }
        if (g_dr_owner[i] != -1) { set_status("Already taken"); return; }
        if (dr_done()) { set_status("Both drafts are full"); return; }
        dr_take(i);
        dr_save();
        set_status(dr_done() ? "Draft done: turn on 'Use the draft in matches'" : g_dr_vs && g_dr_turn ? "Player 2's pick" : "Your pick");
    } else if (g_dr_row == 2 + DR_POOL) {
        if (!dr_done()) { set_status("Finish the draft first (10 picks each)"); return; }
        g_dr_on = !g_dr_on; dr_save();
        set_status(g_dr_on ? "Draft on: your next offline matches use it" : "Draft off: your own arsenals again");
    } else InterlockedExchange(&g_ed_page, EDP_CHOOSER);
}
static const char *DR_H[] = {"DRAFT ARSENAL - how to use", "1  A on 'Deal a new pool': 24 skills of every kind",
                             "2  Opponent: the CPU (it picks after you) or player 2 (pass the controller)",
                             "3  A on a skill takes it (3 capsules); 10 picks each = 30 capsules",
                             "4  'Use the draft in matches' on: Single Play, split screen, 3 VS 3 LOBBY",
                             "   your fighter plays your draft, the other side theirs; your arsenals stay as they are", NULL};
template <class PUT, class BOX> static void dr_draw(PUT &put, BOX &box, int x, int y) {
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF, GREY = 0xFFB0B0B0, GREEN = 0xFF80FF80, RED = 0xFFFF8080;
    const int LINE = 16;
    (void)box;
    dr_load();
    if (g_ed_help) {
        put(x, y, DR_H[0], GOLD);
        y += LINE + 4;
        for (int i = 1; DR_H[i]; i++, y += LINE) put(x, y, DR_H[i], WHITE);
        put(x, y + 4, "Y / A / B: close this help", BLUE);
        return;
    }
    char line[128];
    sprintf_s(line, "DRAFT ARSENAL   you %d / %d   %s %d / %d      (B: back   Y: help)", g_dr_n[0], DR_PICKS, g_dr_vs ? "player 2" : "CPU", g_dr_n[1], DR_PICKS);
    put(x, y, line, GOLD);
    y += LINE + 4;
    put(x, y, g_dr_row == 0 ? "> Deal a new pool" : "  Deal a new pool", g_dr_row == 0 ? GOLD : WHITE); y += LINE;
    sprintf_s(line, "%sOpponent:  < %s >", g_dr_row == 1 ? "> " : "  ", g_dr_vs ? "player 2 (same controller)" : "the CPU");
    put(x, y, line, g_dr_row == 1 ? GOLD : WHITE); y += LINE + 2;
    int sel = g_dr_row - 2;
    if (sel >= 0 && sel < DR_POOL) { if (sel < g_dr_top) g_dr_top = sel; if (sel >= g_dr_top + DR_ROWS) g_dr_top = sel - DR_ROWS + 1; }
    if (!g_dr_pool[0]) { put(x, y, "  (no pool yet: A on 'Deal a new pool')", GREY); y += LINE; }
    else for (int i = g_dr_top; i < DR_POOL && i < g_dr_top + DR_ROWS; i++, y += LINE) {
        int id = g_dr_pool[i], t = fu_stat(id, 0x0E), s = fu_stat(id, 0x12);
        sprintf_s(line, "%s%-24.24s %-7s %-11s cost %d  dmg %2d  %s", i == sel ? "> " : "  ", sc_skill_name(id), DR_SCH[s >= 0 && s <= 5 ? s : 0],
                  DR_KIND[t >= 0 && t <= 6 ? t : 0], fu_stat(id, 0x28), fu_stat(id, 0x58),
                  g_dr_owner[i] == 0 ? "YOURS" : g_dr_owner[i] == 1 ? (g_dr_vs ? "P2" : "CPU") : "");
        put(x, y, line, i == sel ? GOLD : g_dr_owner[i] == 0 ? GREEN : g_dr_owner[i] == 1 ? RED : WHITE);
    }
    y += 2;
    sprintf_s(line, "%sUse the draft in matches: %s", g_dr_row == 2 + DR_POOL ? "> " : "  ", g_dr_on ? "ON" : "off");
    put(x, y, line, g_dr_row == 2 + DR_POOL ? GOLD : g_dr_on ? GREEN : WHITE); y += LINE;
    put(x, y, g_dr_row == 3 + DR_POOL ? "> Back" : "  Back", g_dr_row == 3 + DR_POOL ? GOLD : WHITE);
}

static int dr_self_test() {                                     // turns, the CPU's answer, the counts, the end
    bool keep_on = g_dr_on, keep_loaded = g_dr_loaded; int keep_vs = g_dr_vs;
    short kp[DR_POOL]; signed char ko[DR_POOL]; short kk[2][DR_PICKS]; int kn[2] = {g_dr_n[0], g_dr_n[1]}, kt = g_dr_turn;
    memcpy(kp, g_dr_pool, sizeof(kp)); memcpy(ko, g_dr_owner, sizeof(ko)); memcpy(kk, g_dr_pick, sizeof(kk));
    static char gs[0x34000];                                     // a stand-in skill image: type, school, cost, damage
    memset(gs, 0, sizeof(gs));
    *(unsigned *)gs = 0x44004;
    for (int id = 1; id <= 200; id++) {
        char *r = gs + 0x18 + NS_REC * id;
        *(short *)(r + 0x0E) = (short)(id % 6 + 1); *(short *)(r + 0x12) = (short)(id % 5 + 1);
        *(short *)(r + 0x28) = (short)(id % 4 + 1); *(short *)(r + 0x58) = (short)(id % 9);
    }
    char *keep_gs = g_fu_test_gs;
    g_fu_test_gs = gs;
    g_dr_t_valid = 200; g_dr_loaded = true; g_dr_vs = 0;
    dr_deal();
    bool pool = true;
    for (int i = 0; i < DR_POOL; i++) { pool &= g_dr_pool[i] >= 1 && g_dr_pool[i] <= 200; for (int j = 0; j < i; j++) pool &= g_dr_pool[i] != g_dr_pool[j]; }
    dr_take(5);                                                  // you take row 5, the CPU answers with another row
    bool turn = g_dr_n[0] == 1 && g_dr_n[1] == 1 && g_dr_owner[5] == 0 && g_dr_turn == 0;
    int theirs = -1;
    for (int i = 0; i < DR_POOL; i++) if (g_dr_owner[i] == 1) theirs = i;
    dr_take(theirs);                                             // taken already: nothing
    bool taken = g_dr_n[0] == 1;
    for (int i = 0; i < DR_POOL && !dr_done(); i++) if (g_dr_owner[i] == -1) dr_take(i);
    bool done = dr_done() && g_dr_n[0] == 10 && g_dr_n[1] == 10;
    int cnt[300]; float au = 0;
    *(float *)(gs + 0x33034) = 1.5f; *(float *)(gs + 0x33034 + 4) = 1.25f; *(float *)(gs + 0x33034 + 8) = 1.1f;
    dr_fill(0, cnt, &au, 300, gs, 1.0f);
    int total = 0;
    for (int i = 1; i < 299; i++) total += cnt[i];
    bool counts = total == 30 && cnt[0] == 0 && cnt[g_dr_pick[0][0]] >= 3 && (au == 1.0f || au == 1.5f || au == 1.25f || au == 1.1f);
    g_dr_vs = 1; dr_deal(); dr_take(0);                          // player 2: no answer, their turn
    bool p2 = g_dr_n[0] == 1 && g_dr_n[1] == 0 && g_dr_turn == 1;
    dr_take(1);
    p2 &= g_dr_n[1] == 1 && g_dr_turn == 0;
    g_dr_t_valid = -1; g_fu_test_gs = keep_gs;
    memcpy(g_dr_pool, kp, sizeof(kp)); memcpy(g_dr_owner, ko, sizeof(ko)); memcpy(g_dr_pick, kk, sizeof(kk));
    g_dr_n[0] = kn[0]; g_dr_n[1] = kn[1]; g_dr_turn = kt; g_dr_on = keep_on; g_dr_loaded = keep_loaded; g_dr_vs = keep_vs;
    logf("self-test: draft pool %s, turns %s, taken %s, done %s, counts %s, player 2 %s", pool ? "ok" : "FAILED", turn ? "ok" : "FAILED",
         taken ? "ok" : "FAILED", done ? "ok" : "FAILED", counts ? "ok" : "FAILED", p2 ? "ok" : "FAILED");
    return pool && turn && taken && done && counts && p2 ? 1 : -68;
}
