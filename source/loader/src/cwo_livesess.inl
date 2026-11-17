// ---- v5.11 the Xbox Live lobby's session win / loss (owner 2026-10-26; online exception
// "I made an exception to this request only", AGENTS.md).
// Game facts (2026-10-26):
//  - the menu in use: menu object *0x140533c58, its id the short at +0x105f4 (FUN_140246130, which pushes it on a change);
//    ids = the menu name table 0x1403fa3f8 (24-byte entries, name at +8) index + 2: livemain 36, livequick 37,
//    livesearch 38, livecreategame 39, livesession 43, livelobby 44 (laboratory 45, livecharasel 46, livestagesel 47 as
//    recorded 2026-10-25);
//  - the results: FUN_14012c8c0 writes per fighter 0x1404bc5c0[i] = 0 (its side won) or 1 (lost); 3 for the fighter
//    *0x1404bbe00 when fewer than 2 fighters are left (no contest). *0x1404bbe00 is this machine's own fighter (also
//    FUN_140129fb0); the stores read through cwo_gaddr (cwo_sixf2.inl moves them in six-fighter builds);
//  - how a match started: *0x1404bca04, 2 / 3 = online (FUN_140187710).
// Leaving to the Xbox Live main menu, Quick Match, Search, Create Match or the session list arms a reset; the next lobby
// starts at 0 W - 0 L. While in the lobby the count stays in the loader's status box.
static const int LS_LOBBY = 44;
static int g_ls_w, g_ls_l;
static bool g_ls_pending = true, g_ls_armed;
static ULONGLONG g_ls_show_t;
static int g_ls_t_menu = -1, *g_ls_t_rank, g_ls_t_me = -1, g_ls_t_origin = -1;   // self-test stand-ins

// v5.16 (owner 2026-10-26):
// the lobby script's SESSION INFO row "This Session" asks for the text with native 2.8 op 39 (build_arsenal_menus.py);
// while that row is being drawn the status box stays off. An older lobby script (no row) keeps the status box.
static char g_ls_text[112] = "0 W - 0 L";
static ULONGLONG g_ls_row_t;                                     // last time the SESSION INFO row asked for the text

static int ls_wins() { return g_ls_w; }                         // v5.57 for cwo_bfrnet.inl's tie-breaker
static int ls_losses() { return g_ls_l; }
static void bn_session_suffix(char *out, size_t n);              // v5.57 cwo_bfrnet.inl: the lobby mode and the tie-breaker
static void ls_make_text() {
    int n = g_ls_w + g_ls_l;
    char tail[64] = "";
    bn_session_suffix(tail, sizeof(tail));
    if (n) sprintf_s(g_ls_text, "%d W - %d L   (%d%% won)%s", g_ls_w, g_ls_l, 100 * g_ls_w / n, tail);
    else sprintf_s(g_ls_text, "%d W - %d L%s", g_ls_w, g_ls_l, tail);
}

static INT64 ls_native(int op, INT64 *) {                       // game thread (the lobby script's draw)
    if (op != 39) return 0;
    g_ls_row_t = GetTickCount64();
    ls_make_text();
    return (INT64)(ULONG_PTR)g_ls_text;
}

// ---- v5.152 win streaks in the lobbies (owner 2026-11-12; Xbox Live: "Show win streaks in the Xbox Live lobby display only", AGENTS.md).
// Each match's own results (0x1404bc5c0[i]: 0 its side won, 1 / 2 lost, 3 no contest - FUN_14012c8c0) count a streak per
// fighter: a win adds one, a loss ends it. Xbox Live / system link (origin 2 / 3): per session (reset with the session W / L);
// split screen (origin 4): players 1 and 2 for as long as the game runs. Shown on the lobby rows (cwo_lobstrip.inl op 110):
// "STREAK n" on the member's face strip from 2 wins on. Display only, this machine only; nothing is sent or written.
static int g_wst_live[8], g_wst_split[8];
static bool g_wst_armed;
static int g_wst_origin;
static void wst_results(const int *rank, int n, int origin) {
    int *t = origin == 4 ? g_wst_split : g_wst_live;
    for (int i = 0; i < n && i < 8; i++) {
        if (rank[i] == 0) t[i]++;
        else if (rank[i] == 1 || rank[i] == 2) t[i] = 0;
    }
    logf("win streaks (%s): %d %d %d %d", origin == 4 ? "split screen" : "Xbox Live session", t[0], t[1], t[2], t[3]);
}
static void wst_step(int origin, int me, const int *rank) {     // game thread, every frame (ls_step)
    if (g_ls_t_rank || (origin != 2 && origin != 3 && origin != 4)) return;   // (not in ls_self_test's stand-in matches)
    if (me < 0 || me >= 4) return;
    int r = rank[me];
    if (r == -1 && (g_in_match || g_ls_t_rank)) { g_wst_armed = true; g_wst_origin = origin; }
    else if (g_wst_armed && r >= 0) { g_wst_armed = false; wst_results(rank, 4, g_wst_origin); }
}
static int wst_of(int where, int fighter) {                      // where 3 = the Xbox Live lobby, 0 = the split-screen lobby
    if (fighter < 0 || fighter >= 8) return 0;
    return where == 3 ? g_wst_live[fighter] : g_wst_split[fighter];
}
static int ls_menu_id() {
    if (g_ls_t_menu >= 0) return g_ls_t_menu;
    __try { ULONG_PTR m = *(ULONG_PTR *)(g_base + RVA_ARS_MENU); return m ? *(short *)(m + 0x105f4) : -1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void ls_step() {                                          // CwoOverlayFrame, game thread (menus and matches)
    __try {
        int id = ls_menu_id();
        if (id == 36 || id == 37 || id == 38 || id == 39 || id == 43) g_ls_pending = true;
        if (id == LS_LOBBY && g_ls_pending) { g_ls_pending = false; g_ls_w = g_ls_l = 0; memset(g_wst_live, 0, sizeof(g_wst_live)); bn_series_reset(); logf("session: a new Xbox Live session - 0 W - 0 L"); }
        int origin = g_ls_t_origin >= 0 ? g_ls_t_origin : *(int *)(g_base + 0x4bca04);
        {                                                            // v5.152 the win streaks
            int me = g_ls_t_me >= 0 ? g_ls_t_me : *(int *)(g_base + 0x4bbe00);
            int *rank = g_ls_t_rank ? g_ls_t_rank : (int *)cwo_gaddr(0x4bc5c0);
            wst_step(origin, me, rank);
        }
        if (origin == 2 || origin == 3) {
            int me = g_ls_t_me >= 0 ? g_ls_t_me : *(int *)(g_base + 0x4bbe00);
            int *rank = g_ls_t_rank ? g_ls_t_rank : (int *)cwo_gaddr(0x4bc5c0);
            if (me >= 0 && me < 4) {
                int r = rank[me];
                if (r == -1 && (g_in_match || g_ls_t_rank)) { g_ls_armed = true; if (!g_ls_t_rank) bn_series_watch(); }   // a match of this session is on
                else if (g_ls_armed && r >= 0) {                                      // its result is in
                    g_ls_armed = false;
                    if (r == 0) g_ls_w++; else if (r == 1 || r == 2) g_ls_l++;
                    if (!g_ls_t_rank) bn_series_result(r);   // v5.86 the tiebreaker series
                    logf("session: result %d -> %d W - %d L", r, g_ls_w, g_ls_l);
                }
            }
        }
        if (id == LS_LOBBY && !g_ls_t_rank) {
            ULONGLONG now = GetTickCount64();
            if (now - g_ls_show_t > 1000 && now - g_ls_row_t > 3000) {   // v5.16: only without the SESSION INFO row
                g_ls_show_t = now;
                char msg[96];
                ls_make_text();
                sprintf_s(msg, "THIS SESSION:  %s", g_ls_text);
                set_status(msg);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int ls_self_test() {                                      // stand-ins: reset on a new session, wins, losses, no contest
    int rank[4] = {-1, -1, -1, -1};
    int kw = g_ls_w, kl = g_ls_l; bool kp = g_ls_pending, ka = g_ls_armed;
    g_ls_t_rank = rank; g_ls_t_me = 1; g_ls_t_origin = 2; g_ls_w = 5; g_ls_l = 5;
    g_ls_t_menu = 39; ls_step();                                 // Create Match
    g_ls_t_menu = LS_LOBBY; ls_step();                           // the lobby: a new session
    bool reset = g_ls_w == 0 && g_ls_l == 0;
    ls_step();                                                   // match on (rank -1)
    rank[1] = 0; ls_step();                                      // won
    rank[1] = -1; ls_step(); rank[1] = 1; ls_step();             // lost
    rank[1] = -1; ls_step(); rank[1] = 3; ls_step();             // no contest: not counted
    bool count = g_ls_w == 1 && g_ls_l == 1;
    g_ls_t_menu = LS_LOBBY; ls_step();                           // back in the same lobby: kept
    bool kept = g_ls_w == 1 && g_ls_l == 1;
    g_ls_t_origin = 1; rank[1] = -1; ls_step(); rank[1] = 0; ls_step();   // an offline match: not counted
    bool offline = g_ls_w == 1;
    INT64 args[2] = {39, 0};                                     // v5.16 the SESSION INFO text
    bool text = !strcmp((const char *)(ULONG_PTR)ls_native(39, args), "1 W - 1 L   (50% won)") && ls_native(38, args) == 0;
    g_ls_row_t = 0;
    g_ls_t_rank = NULL; g_ls_t_me = -1; g_ls_t_origin = -1; g_ls_t_menu = -1;
    g_ls_w = kw; g_ls_l = kl; g_ls_pending = kp; g_ls_armed = ka;
    logf("self-test: session W/L reset %s, count %s, kept %s, offline ignored %s, row text %s", reset ? "ok" : "FAILED",
         count ? "ok" : "FAILED", kept ? "ok" : "FAILED", offline ? "ok" : "FAILED", text ? "ok" : "FAILED");
    return reset && count && kept && offline && text ? 1 : -42;
}
