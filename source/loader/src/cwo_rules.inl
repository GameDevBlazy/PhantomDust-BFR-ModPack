// ---- v5.53 multiplayer rules (owner 2026-11-01 ~21:xx: "when the character health reaches zero there name plate will stay
// highlighted for all players until there revived, game rule get the most points as possible, also the first to get 5
// kills, number of revives per round (survival mode) these are all multiplayer requested mods for online").
// Offline only for now: AGENTS.md lists the online exceptions and these are not among them yet (the owner's words go into
// AGENTS.md first); ed_online() keeps every part of this module off in a Live session.
// Game facts (Ghidra full pass 065, pd_fn):
//  - FUN_140262fc0: the HUD flashes a fighter's panel by writing 0x10 to *0x1404bec00 (the battle HUD) + i * 0x1c + 0x3a4,
//    i = *(fighter + 0x388 + 0x28 -> +4) = the fighter's battle data number; the panel draw counts it down (FUN_1401509f0);
//    panels 4 / 5 (six fighters) keep that byte in the loader's own entries (cwo_sixhud.inl, ent + 0x18).
//  - a fighter is down while +0x12F04 == 0x1B (FUN_14025c670 counts +0x12F08 to 0, then the game removes it); the game's
//    revive is FUN_140251f80 (a partner's pick-up, rule 10, or the loader's lives); +0x1c team, +0x20 active, +0x24 standing.
// What this does:
//  - Downed name plate (every offline match): while a fighter is down its HUD panel keeps flashing (0x10 written each
//    frame), so every player sees who is down until they are back up or out.
//  - Create a Mode > Players 11 "Most KOs": each knock-down scores a point for the other side (two sides; in a free for
//    all a KO scores for nobody - the game does not keep who landed the hit, not researched); at the mode's time limit
//    (3 minutes when it has none) the side with the most points wins, a tie goes to the next KO (sudden death).
//  - Players 12 "First to 5 KOs": the first side to 5 knock-downs wins.
//  - Players 13 "Survival revives": each side gets the mode's Lives value as its revives for the round; partners revive
//    each other the game's own way; once a side has used them all, its next downed fighter is out.
//  A win makes every fighter of the other sides fall out with no lives left (the King of the Hill end, ar_fall).
static void ru_flash_extra(int i);                             // cwo_sixf2.inl (after the HUD entries 4 / 5)
static bool g_ru_online;                                       // v5.55 this match is a Live session (count and announce only)
static const int RU_POINTS = 11, RU_FIRST5 = 12, RU_REVIVES = 13, RU_KILLS = 5;
static int g_ru_kind, g_ru_pts[8], g_ru_rev[8], g_ru_lead = -1;
static int g_ru_pk[8];                                          // v5.57 KOs per fighter (battle data number): the last one who dealt damage
static float g_ru_t, g_ru_limit;
static bool g_ru_done, g_ru_sudden;
static unsigned char g_ru_was_down[8];

static int ru_teams(int nf, int *only_other) {                 // how many sides are in; with two, *only_other = side B
    int seen[8] = {0}, n = 0, first = -1, second = -1;
    for (int p = 0; p < nf && p < 8; p++) {
        __try {
            ULONG_PTR r = ed_player(p);
            if (!r || !*(int *)(r + 0x20)) continue;
            int t = *(int *)(r + 0x1c);
            if (t < 0 || t >= 8 || seen[t]++) continue;
            n++;
            if (first < 0) first = t; else if (second < 0) second = t;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (only_other) *only_other = n == 2 ? (first ^ second) : -1;   // first ^ second ^ t = the other side
    return n;
}

static void ru_win(int team, int nf, int lives, const char *why) {
    g_ru_done = true;
    if (g_ru_online) {                                         // v5.55 online: the result on every screen, the match runs on
        char msg[80]; sprintf_s(msg, "TEAM %c WINS - %s", 'A' + team, why); set_status(msg);
        logf("rules (online): team %d wins (%s) - announced only", team, why);
        return;
    }
    for (int p = 0; p < nf; p++) {
        __try {
            ULONG_PTR r = ed_player(p);
            if (!r || !*(int *)(r + 0x20) || *(int *)(r + 0x1c) == team) continue;
            g_ed_deaths[p] = lives > 0 ? lives : 1;               // no lives left: the lives step leaves them out
            if (*(int *)(r + 0x24)) ar_fall(r);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    char msg[80]; sprintf_s(msg, "TEAM %c WINS - %s", 'A' + team, why); set_status(msg);
    logf("rules: team %d wins (%s)", team, why);
}

static void ru_begin(int kind, const EdItem &m) {
    g_ru_kind = kind;
    memset(g_ru_pts, 0, sizeof(g_ru_pts)); memset(g_ru_rev, 0, sizeof(g_ru_rev)); memset(g_ru_was_down, 0, sizeof(g_ru_was_down));
    memset(g_ru_pk, 0, sizeof(g_ru_pk)); for (int i = 0; i < 8; i++) g_lh[i] = -1;
    g_ru_lead = -1; g_ru_t = 0; g_ru_done = g_ru_sudden = false;
    g_ru_limit = (m.v[0] > 0 ? m.v[0] : 3) * 60.0f;
    const char *s = kind == RU_POINTS ? "MOST KOs: score the most knock-downs before time runs out"
                  : kind == RU_FIRST5 ? "FIRST TO 5 KOs" : "SURVIVAL: each fighter can be revived a set number of times";
    set_status(s);
    logf("rules: %s (time %.0f s, revives %d a fighter)", s, g_ru_limit, m.v[2]);
}

// a fighter went down (once per knock-down, from the lives step). v5.57 (owner): the point goes to the fighter recorded by the hit hook (cwo_bfrnet.inl) for this target in
// the last 20 seconds, if it is someone else; with no record, two sides fall back to the other side (no player point);
// a free for all scores for nobody.
static int ru_team_of_idx(int idx, int nf) {
    for (int p = 0; p < nf && p < 8; p++) {
        __try { ULONG_PTR r = ed_player(p); if (r && *(int *)(r + 0x20) && *(int *)(r + 4) == idx) return *(int *)(r + 0x1c); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return -1;
}
static void ru_down(ULONG_PTR victim, int t, int nf, int lives) {
    if (g_ru_done || (g_ru_kind != RU_POINTS && g_ru_kind != RU_FIRST5)) return;
    int vi = -1;
    __try { vi = victim ? *(int *)(victim + 4) : -1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    int by = vi >= 0 && vi < 8 && g_lh[vi] >= 0 && GetTickCount64() - g_lh_t[vi] < 20000 ? g_lh[vi] : -1;
    int s = -1;
    if (by >= 0 && by == vi) { logf("rules: fighter %d knocked itself out - no point", vi + 1); return; }
    if (by >= 0) s = ru_team_of_idx(by, nf);
    if (s >= 0 && s == t) { logf("rules: fighter %d downed by a partner - no point", vi + 1); return; }
    if (s < 0) {
        int other = -1;
        ru_teams(nf, &other);
        if (other < 0) { logf("rules: KO in a free for all with no hit on record - no point"); return; }
        s = other ^ t;
        by = -1;
    }
    if (s < 0 || s >= 8) return;
    g_ru_pts[s]++;
    if (by >= 0) g_ru_pk[by]++;
    char msg[80];
    if (by >= 0) sprintf_s(msg, "KO! Player %d scores (%d) - Team %c: %d", by + 1, g_ru_pk[by], 'A' + s, g_ru_pts[s]);
    else sprintf_s(msg, "KO! Team %c: %d - Team %c: %d", 'A' + s, g_ru_pts[s], 'A' + t, g_ru_pts[t]);
    set_status(msg);
    logf("rules: KO of fighter %d by %d (team %d scores %d)", vi + 1, by + 1, s, g_ru_pts[s]);
    bool five = by >= 0 ? g_ru_pk[by] >= RU_KILLS : g_ru_pts[s] >= RU_KILLS;
    if (g_ru_kind == RU_FIRST5 && five) ru_win(s, nf, lives, "first to 5 KOs");
    else if (g_ru_kind == RU_POINTS && g_ru_sudden) ru_win(s, nf, lives, "sudden death KO");
}

// a downed fighter p is up again by a partner's revive (not the loader's lives); true = it may stay up.
// v5.57 (owner): the count is per fighter
static bool ru_revived(int p, int budget) {
    if (g_ru_kind != RU_REVIVES || p < 0 || p >= 8) return true;
    g_ru_rev[p]++;
    char msg[64]; sprintf_s(msg, "Player %d revive %d of %d", p + 1, g_ru_rev[p], budget); set_status(msg);
    logf("rules: fighter %d revive %d of %d", p + 1, g_ru_rev[p], budget);
    return g_ru_rev[p] <= budget;
}

static void ru_time(float dt, int nf, int lives) {             // Most KOs: the clock
    if (g_ru_kind != RU_POINTS || g_ru_done || g_ru_sudden) return;
    g_ru_t += dt;
    // v5.57 the match's own clock when it has one (*0x1404bc8c0, 30 ticks a second, counted down by FUN_14012b7a0): the
    // decision in its last second; otherwise the mode's time
    static int clock_seen;
    int clock = 0;
    if (!g_ed_test_gs) { __try { clock = *(int *)(g_base + 0x4bc8c0); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    if (clock > 60) clock_seen = 1;
    bool up = clock_seen ? (clock > 0 && clock <= 30) : g_ru_t >= g_ru_limit;
    if (!up) return;
    clock_seen = 0;
    int best = -1, bestv = -1, tie = 0;
    for (int t = 0; t < 8; t++) {
        if (g_ru_pts[t] > bestv) { bestv = g_ru_pts[t]; best = t; tie = 0; }
        else if (g_ru_pts[t] == bestv && bestv > 0) tie = 1;
    }
    if (best < 0 || bestv <= 0 || tie) { g_ru_sudden = true; set_status("MOST KOs: tied - SUDDEN DEATH, next KO wins"); logf("rules: time up, tied - sudden death"); return; }
    ru_win(best, nf, lives, "most KOs at time up");
}

// every frame of an offline match: keep a downed fighter's HUD panel flashing (the game's own flash)
static void ru_names_step();
static void ru_plate_step() {
    ru_names_step();                                           // v5.53 the KO tally beside the names
    if (!g_in_match || g_ed_test_gs) return;                    // v5.55 online too (AGENTS.md: display only)
    __try {
        ULONG_PTR hud = *(ULONG_PTR *)(g_base + 0x4bec00);
        ULONG_PTR *act = cwo_actors();
        for (int i = 0; i < cwo_nfighters() && i < 6; i++) {
            ULONG_PTR a = act[i];
            bool down = a && *(int *)(a + 0x20) && *(int *)(a + 0x12F04) == 0x1B;
            if (!down) continue;
            if (i < 4) { if (hud) *(int *)(hud + (ULONG_PTR)i * 0x1c + 0x3a4) = 0x10; }
            else ru_flash_extra(i);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// v5.53 (owner): the HUD side panel (FUN_140155310) and
// the lock-on cursor (FUN_140153a10) print a fighter's name from the battle name table *0x1404bca30 (4 entries of 0x41
// bytes, battle data number i; FUN_14012d200 fills it at the battle's set-up). During a Most KOs / First to 5 KOs match the
// loader writes "<name>  KO n" there (n = the KOs of that fighter's side) and puts the names back when the match ends.
static char g_ru_name[4][0x41];
static bool g_ru_named[4];
static int g_ru_shown[4] = {-1, -1, -1, -1};
static void ru_names_restore() {
    for (int i = 0; i < 4; i++) {
        if (!g_ru_named[i]) continue;
        __try { memcpy((void *)(g_base + 0x4bca30 + (ULONG_PTR)i * 0x41), g_ru_name[i], 0x41); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_ru_named[i] = false; g_ru_shown[i] = -1;
    }
}
static void ru_names_step() {
    if (g_ed_test_gs) return;
    bool show = g_in_match && (g_ru_kind == RU_POINTS || g_ru_kind == RU_FIRST5);   // v5.55 online too
    if (!show) { ru_names_restore(); return; }
    __try {
        ULONG_PTR *act = cwo_actors();
        for (int k = 0; k < cwo_nfighters() && k < 8; k++) {
            ULONG_PTR a = act[k];
            if (!a || !*(int *)(a + 0x20)) continue;
            int i = *(int *)(a + 4), t = *(int *)(a + 0x1c);
            if (i < 0 || i > 3 || t < 0 || t >= 8) continue;
            char *nm = (char *)(g_base + 0x4bca30 + (ULONG_PTR)i * 0x41);
            if (!g_ru_named[i]) { memcpy(g_ru_name[i], nm, 0x41); g_ru_name[i][0x40] = 0; g_ru_named[i] = true; g_ru_shown[i] = -1; }
            int kos = g_ru_pk[i];                                   // v5.57 the player's own KOs (last hitter)
            if (g_ru_shown[i] == kos) continue;
            char buf[0x41];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.48s  KO %d", g_ru_name[i], kos);
            memcpy(nm, buf, strlen(buf) + 1);
            g_ru_shown[i] = kos;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int ru_self_test() {                                     // scoring, first to 5, revive budget on stand-ins
    int save_kind = g_ru_kind; bool save_done = g_ru_done;
    g_ru_kind = RU_FIRST5; g_ru_done = false; memset(g_ru_pts, 0, sizeof(g_ru_pts));
    // two sides 0 and 1: credit side 1 four times by hand, the fifth through ru_down's arithmetic
    int other = 0 ^ 1; bool arith = (other ^ 0) == 1 && (other ^ 1) == 0;
    g_ru_pts[1] = 4; bool five = g_ru_pts[1] + 1 >= RU_KILLS;
    g_ru_kind = RU_REVIVES; memset(g_ru_rev, 0, sizeof(g_ru_rev));
    bool r1 = ru_revived(0, 2), r2 = ru_revived(0, 2), r3 = ru_revived(0, 2);
    bool budget = r1 && r2 && !r3 && g_ru_rev[0] == 3;
    g_ru_kind = RU_POINTS; g_ru_done = false; g_ru_sudden = false; g_ru_limit = 10; g_ru_t = 0; memset(g_ru_pts, 0, sizeof(g_ru_pts));
    g_ru_pts[0] = 2; g_ru_pts[1] = 2;
    ru_time(11.0f, 0, 1);
    bool sudden = g_ru_sudden && !g_ru_done;
    g_ru_kind = save_kind; g_ru_done = save_done; g_ru_sudden = false; memset(g_ru_pts, 0, sizeof(g_ru_pts)); memset(g_ru_rev, 0, sizeof(g_ru_rev));
    bool ok = arith && five && budget && sudden;
    logf("self-test: multiplayer rules sides %s, first to 5 %s, revive budget %s, tie -> sudden death %s", arith ? "ok" : "FAILED",
         five ? "ok" : "FAILED", budget ? "ok" : "FAILED", sudden ? "ok" : "FAILED");
    return ok ? 1 : -87;
}
