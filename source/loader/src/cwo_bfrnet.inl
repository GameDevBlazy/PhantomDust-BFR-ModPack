// ---- v5.57 BFR lobby features over the game's own player-info packet (owner 2026-11-01: KO rules / Survival as game rules
// in the Xbox Live lobby's Change Rule, "the last person who dealt damage will get that KO point", the tie-breaker vote,
// Next Map as a lobby row; AGENTS.md "Multiplayer rules online" + "BFR mod pack ... online").
// Game facts (Ghidra full pass 065, pd_fn + capstone on the clean exe, MODLOG 2026-11-01 22:5x / 23:5x):
//  - FUN_1401910a0 builds this machine's 0x80-byte player info at rsp+0x38 and sends it (FUN_140125430); packet +0x0c and
//    +0x10 are written as zero by `mov qword [rsp+0x44], rcx` (rcx = 0) at 0x14019112c; the receiver FUN_140191a60(_, pk)
//    reads pk+8 (member), +0x18, +0x20, +0x24, +0x28..+0x3c (host: stage, type, rule ...), +0x40 (name) - never +0x0c.
//    FUN_140191210 resends this machine's info to every member. The host is local member *0x140527bc4 == 0.
//  - FUN_140274d20(attacker, target, skill, damage, ...) is called by the hit applier FUN_14026f870 for every landed hit
//    (attacker = *(skill object +0x40) + 4, target = the hit fighter + 4: battle data numbers).
// What this does:
//  - the loader's 4 bytes ride in packet +0x0c (byte 0 the host's lobby mode, byte 1 bit 7 Next Map on, byte 2 this
//    player's tie-breaker vote 1 yes / 2 no, byte 3 0xB5); a change is sent at once with the game's own resend;
//  - every received packet's +0x0c is kept per member; a guest plays the host's lobby mode (byte 0 of member 0);
//  - each landed hit records the attacker per target (the KO point goes to the last one who dealt damage).
// Hooks (official Windows documentation only: VirtualProtect, FlushInstructionCache): 0x14019112c (5 bytes -> call a stub
// that loads our 8 bytes into packet +0x0c / +0x10 and leaves rcx = 0 as before); 0x140274d20 (10 bytes) and 0x140191a60
// (15 bytes) -> jmp to C hooks that call the original through a trampoline (no relative operands in the moved bytes).
static const ULONG_PTR BN_SEND = 0x19112c, BN_HIT = 0x274d20, BN_RECV = 0x191a60, BN_RESEND = 0x191210, BN_ME = 0x527bc4;
static const unsigned char BN_SEND_OLD[5] = {0x48, 0x89, 0x4c, 0x24, 0x44};
static const unsigned char BN_HIT_OLD[10] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30};
static const unsigned char BN_RECV_OLD[15] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20};
static const unsigned BN_MAGIC = 0xB5u;
static int g_bn_state;                                          // 1 hooks in, -1 refused
static volatile unsigned long long *g_bn_cell;                  // what goes into packet +0x0c (low) / +0x10 (high, 0)
static unsigned g_bn_in[8], g_bn_sent;
static ULONGLONG g_bn_in_t[8];
static const ULONG_PTR BN_MATE_LOCK = 0x52dfb9, BN_LIVE_RULE = 0x518cf0;
static bool g_bn_setting;                                       // v5.62 the loader itself puts the game's rule back to Standard
static bool g_dg_on = true;                                     // v5.103 Downed Glow (the host's switch online; cwo_downglow.inl)
static int g_bn_rule_host;                                      // the host's lobby mode: 0 none, 1 Most KOs, 2 First to 5 KOs, 3 Survival (3 revives)
static int g_bn_vote;                                           // this player's tie-breaker vote: 0 none, 1 yes, 2 no
static int g_lh[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static ULONGLONG g_lh_t[8];
// v5.95 (owner 2026-11-03 "I APPROVE ALL OF THIS": five new rules, AGENTS.md "New game rules ... online"): 4 King of the
// Hill, 5 Shrinking Arena, 6 Aura Rush, 7 Sudden Death, 8 Realistic Collision - chosen in the lobby list or in Create Game
// Session (liverulesel, tools/build_rule_cycle.py), sent in the player-info packet like the first three
static const int BN_NRULES = 8;
static const char *BN_RULES[BN_NRULES] = {"Most KOs", "First to 5 KOs", "Survival (3 revives)", "King of the Hill", "Shrinking Arena",
                                          "Aura Rush", "Sudden Death", "Realistic Collision"};

typedef void (*PFN_BN_HIT)(INT64, INT64, INT64, INT64, INT64, INT64);
typedef void (*PFN_BN_RECV)(INT64, INT64);
static PFN_BN_HIT g_bn_hit_orig;
static PFN_BN_RECV g_bn_recv_orig;

static void bn_hit_hook(INT64 a, INT64 t, INT64 sk, INT64 dmg, INT64 p5, INT64 p6) {
    __try {
        int ai = (int)a, ti = (int)t;
        if ((int)dmg > 0 && ai >= 0 && ai < 8 && ti >= 0 && ti < 8) { g_lh[ti] = ai; g_lh_t[ti] = GetTickCount64(); }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_bn_hit_orig(a, t, sk, dmg, p5, p6);
}
static void bn_recv_hook(INT64 p1, INT64 pk) {
    __try {
        int m = *(int *)(pk + 8);
        unsigned v = *(unsigned *)(pk + 0xc);
        if (m >= 0 && m < 8) { g_bn_in[m] = v; g_bn_in_t[m] = GetTickCount64(); }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_bn_recv_orig(p1, pk);
}

static void bn_put32(unsigned char *p, LONGLONG v) { int d = (int)v; memcpy(p, &d, 4); }
static void bn_far_jmp(unsigned char *at, void *to) { at[0] = 0x48; at[1] = 0xb8; *(unsigned long long *)(at + 2) = (unsigned long long)(ULONG_PTR)to; at[10] = 0xff; at[11] = 0xe0; }

// page: +0x00 send stub, +0x40 hit trampoline, +0x60 its far jump, +0x80 receive trampoline, +0xa0 its far jump, +0x200 cell
static bool bn_apply(ULONG_PTR base, ULONG_PTR page, bool live) {
    unsigned char *s1 = (unsigned char *)(base + BN_SEND), *s2 = (unsigned char *)(base + BN_HIT), *s3 = (unsigned char *)(base + BN_RECV);
    if (memcmp(s1, BN_SEND_OLD, 5) || memcmp(s2, BN_HIT_OLD, 10) || memcmp(s3, BN_RECV_OLD, 15)) return false;
    unsigned char *pg = (unsigned char *)page;
    // send stub: mov rcx, [rip + cell]; mov [rsp + 0x4c], rcx; xor ecx, ecx; ret
    pg[0] = 0x48; pg[1] = 0x8b; pg[2] = 0x0d; bn_put32(pg + 3, (LONGLONG)(page + 0x200) - (LONGLONG)(page + 7));
    pg[7] = 0x48; pg[8] = 0x89; pg[9] = 0x4c; pg[10] = 0x24; pg[11] = 0x4c; pg[12] = 0x31; pg[13] = 0xc9; pg[14] = 0xc3;
    memcpy(pg + 0x40, BN_HIT_OLD, 10); pg[0x4a] = 0xe9; bn_put32(pg + 0x4b, (LONGLONG)(base + BN_HIT + 10) - (LONGLONG)(pg + 0x4f));
    bn_far_jmp(pg + 0x60, (void *)bn_hit_hook);
    memcpy(pg + 0x80, BN_RECV_OLD, 15); pg[0x8f] = 0xe9; bn_put32(pg + 0x90, (LONGLONG)(base + BN_RECV + 15) - (LONGLONG)(pg + 0x94));
    bn_far_jmp(pg + 0xa0, (void *)bn_recv_hook);
    *(unsigned long long *)(pg + 0x200) = 0;
    unsigned char c1[5] = {0xe8}; bn_put32(c1 + 1, (LONGLONG)page - (LONGLONG)(base + BN_SEND + 5));
    unsigned char c2[10]; memset(c2, 0x90, 10); c2[0] = 0xe9; bn_put32(c2 + 1, (LONGLONG)(page + 0x60) - (LONGLONG)(base + BN_HIT + 5));
    unsigned char c3[15]; memset(c3, 0x90, 15); c3[0] = 0xe9; bn_put32(c3 + 1, (LONGLONG)(page + 0xa0) - (LONGLONG)(base + BN_RECV + 5));
    g_bn_hit_orig = (PFN_BN_HIT)(pg + 0x40);                        // v5.148.17 before the sites are patched (race: fps hook 19:38)
    g_bn_recv_orig = (PFN_BN_RECV)(pg + 0x80);
    g_bn_cell = (volatile unsigned long long *)(pg + 0x200);
    if (live) FlushInstructionCache(GetCurrentProcess(), pg, 0x300);
    struct { unsigned char *at; unsigned char *b; int n; } w[3] = {{s1, c1, 5}, {s2, c2, 10}, {s3, c3, 15}};
    for (auto &x : w) {
        DWORD old;
        if (live && !VirtualProtect(x.at, x.n, PAGE_EXECUTE_READWRITE, &old)) return false;
        memcpy(x.at, x.b, x.n);
        if (live) { VirtualProtect(x.at, x.n, old, &old); FlushInstructionCache(GetCurrentProcess(), x.at, x.n); }
    }
    return true;
}

static void bn_install() {                                       // worker start
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!page) { g_bn_state = -1; logf("BFR lobby: no memory near the exe - off"); return; }
    if (!bn_apply(g_base, page, true)) { g_bn_state = -1; logf("BFR lobby: a site is not the stock code - off"); return; }
    g_bn_state = 1;
    logf("BFR lobby: player-info field +0x0c, hit record and receive hooked");
}

static bool bn_host() {
    __try { return *(int *)(g_base + BN_ME) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static unsigned bn_member_value(int m) { return (g_bn_in[m] >> 24) == BN_MAGIC ? g_bn_in[m] : 0; }

// the lobby mode in force for this machine (host: its own; guest: the host's from member 0's packet)
static int bn_rule() {
    if (bn_host()) return g_bn_rule_host;
    int r = (int)(bn_member_value(0) & 0xff);
    return r >= 1 && r <= BN_NRULES ? r : 0;
}

// the tie-breaker (owner): pending when this session stands at 1 - 1
static bool bn_tb_pending();
static void bn_tb_count(int *yes, int *no);
// v5.86 the tiebreaker series (owner 2026-11-03). A series starts with
// a battle and keeps the line-up of that battle (each fighter slot's team, fighter +0x1c, read in the match); a battle with
// another line-up starts a new series. After 2 battles at 1 W - 1 L the vote opens by itself (tb_draw / tb_input); all
// players YES = the tiebreaker (battle 3, same teams), any NO = the teams are free.
static int g_tb_battles, g_tb_w, g_tb_l, g_tb_lineup[8], g_tb_cur[8];
static bool g_tb_vote_open, g_tb_on;
static void bn_series_reset() { g_tb_battles = g_tb_w = g_tb_l = 0; g_tb_vote_open = g_tb_on = false; InterlockedExchange(&g_tbv_show, 0); }
static void bn_series_watch() {                                   // game thread, every frame of a match: the line-up
    if (!g_in_match) return;
    __try {
        ULONG_PTR *act = cwo_actors();
        int n = cwo_nfighters(); if (n > 8) n = 8;
        for (int d = 0; d < 8; d++) g_tb_cur[d] = d < n && act[d] && *(int *)(act[d] + 0x20) ? *(int *)(act[d] + 0x1c) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static bool bn_series_locked() { return g_tb_battles > 0 && (g_tb_battles < 2 || g_tb_vote_open || g_tb_on); }
static void bn_series_result(int rank) {                          // cwo_livesess.inl: this machine's result is in
    if (g_tb_battles > 0 && memcmp(g_tb_cur, g_tb_lineup, sizeof(g_tb_cur))) {
        logf("tiebreaker: the teams changed - a new series");
        bn_series_reset();
    }
    if (g_tb_battles == 0) memcpy(g_tb_lineup, g_tb_cur, sizeof(g_tb_cur));
    g_tb_battles++;
    if (rank == 0) g_tb_w++; else if (rank == 1 || rank == 2) g_tb_l++;
    if (g_tb_on) { logf("tiebreaker: played (%d W - %d L) - teams free", g_tb_w, g_tb_l); bn_series_reset(); set_status("Tiebreaker played - the teams are free"); return; }
    if (g_tb_battles >= 2) {
        if (g_tb_w == 1 && g_tb_l == 1) { g_tb_vote_open = true; g_bn_vote = 0; InterlockedExchange(&g_tbv_show, 1); logf("tiebreaker: 1 - 1 with the same teams - the vote opens"); }
        else { logf("tiebreaker: series over at %d W - %d L - teams free", g_tb_w, g_tb_l); bn_series_reset(); }
    } else logf("tiebreaker: battle 1 of 2 (%s) - the teams stay locked", rank == 0 ? "won" : "lost");
}
static void bn_series_vote_step() {                               // every frame: everyone YES = tiebreaker, a NO = free
    if (!g_tb_vote_open) return;
    int y, no; bn_tb_count(&y, &no);
    int me = -1; __try { me = *(int *)(g_base + BN_ME); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    int players = 1;
    for (int m = 0; m < 8; m++) if (m != me && bn_member_value(m)) players++;
    if (no > 0) { g_tb_vote_open = false; g_bn_vote = 0; InterlockedExchange(&g_tbv_show, 0); bn_series_reset(); set_status("Tiebreaker: NO - everybody can switch teams"); logf("tiebreaker: voted down"); }
    else if (y >= players) { g_tb_vote_open = false; g_tb_on = true; InterlockedExchange(&g_tbv_show, 0); set_status("TIEBREAKER! Same teams - the host starts the round"); logf("tiebreaker: everyone said yes (%d)", y); }
}
static void tb_input(WORD pressed) {                              // pad thread while the vote window shows
    if (pressed & BTN_A) { g_bn_vote = 1; InterlockedExchange(&g_tbv_show, 0); InterlockedExchange(&g_ed_game_hold, 1); set_status("Tiebreaker vote: YES"); rumble(1); }
    else if (pressed & BTN_B) { g_bn_vote = 2; InterlockedExchange(&g_tbv_show, 0); InterlockedExchange(&g_ed_game_hold, 1); set_status("Tiebreaker vote: NO"); rumble(1); }
}
static void tb_draw() {
    if (!g_tbv_show || g_in_match) return;
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 200 || *(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4]; memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    DWORD accent = 0xFF80E0D0;
    auto box = [&](int px, int py, DWORD col, int w, int h) { *space = 0; *gap = 0; *fh = h; *fw = w; set_rgba(rgba, col); text(px, py, "\x81\xa1"); };
    auto put = [&](int px, int py, const char *s, DWORD col) { *space = 0; *gap = 2; *fh = 14; *fw = 7; set_rgba(rgba, 0xFF000000); text(px + 1, py + 1, s); set_rgba(rgba, col); text(px, py, s); };
    const int X = 170, Y = 180, W = 300;
    for (int by = Y - 10; by < Y + 90; by += 48) for (int bx = X - 12; bx < X + W; bx += 48) box(bx, by, 0xF0061614, 48, 64);
    put(X, Y, "TIEBREAKER VOTE", accent);
    put(X, Y + 24, "1 win each with the same teams.", 0xFFFFFFFF);
    put(X, Y + 43, "Play the tiebreaker round?", 0xFFFFFFFF);
    put(X, Y + 68, "A: Yes      B: No", 0xFFC8C8C8);
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}
static int ls_wins();                                            // cwo_livesess.inl (included later)
static int ls_losses();
static bool bn_tb_pending() { return ed_online() && g_tb_vote_open; }   // v5.86 the series (below) opens it
static void bn_tb_count(int *yes, int *no) {
    *yes = g_bn_vote == 1; *no = g_bn_vote == 2;
    int me = -1; __try { me = *(int *)(g_base + BN_ME); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    for (int m = 0; m < 8; m++) {
        if (m == me) continue;
        int v = (int)((bn_member_value(m) >> 16) & 0xff);
        *yes += v == 1; *no += v == 2;
    }
}
static void bn_session_suffix(char *out, size_t n) {            // SESSION INFO's "This Session" row (cwo_livesess.inl)
    out[0] = 0;
    if (g_bn_state != 1) return;                                 // the hooks are only in a real game session
    int r = bn_rule();
    if (r >= 1 && r <= BN_NRULES) _snprintf_s(out, n, _TRUNCATE, "  %s", BN_RULES[r - 1]);
    size_t l = strlen(out);
    if (g_tb_on) _snprintf_s(out + l, n - l, _TRUNCATE, "  TIEBREAKER!");
    else if (bn_tb_pending()) { int y, no; bn_tb_count(&y, &no); _snprintf_s(out + l, n - l, _TRUNCATE, "  Tiebreaker? Yes %d No %d", y, no); }
    else if (bn_series_locked()) _snprintf_s(out + l, n - l, _TRUNCATE, "  Teams locked (battle %d of 2)", g_tb_battles + 1);
}

// every frame: our 4 bytes, sent again when they change (online only)
static void bn_step() {
    if (g_bn_state != 1) return;
    if (g_bn_rule_host > 0 && bn_host() && ed_online()) {          // v5.62 a lobby mode plays on Standard Battle underneath
        static ULONGLONG t;
        ULONGLONG now = GetTickCount64();
        if (now - t > 500) {
            t = now;
            int rule = 0;
            __try { rule = *(int *)(g_base + BN_LIVE_RULE); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            if (rule != 0) { g_bn_setting = true; INT64 sa[2] = {38, 0}; ars_native(38, sa); g_bn_setting = false; }
        }
    }
    bn_series_vote_step();
    if (!g_tb_vote_open) g_bn_vote = 0;                         // v5.86 a vote only while it is open
    bool host = bn_host();
    // v5.188.4 (owner 2026-11-15, BFR online: the player who joined saw "stage 10 dummy"; "online multiplayer and split screen
    // multiplayer must have the same ... stages"): the host's Map Slot 7 pack goes with the lobby bytes (bits 8-13 = list row
    // + 1, the same pack = the same mx_list on every BFR player), and a player who joined switches Map Slot 7 to it (outside
    // a match), so the stage, its files and its name are the host's
    LONG cur = g_mx_cur;
    unsigned pk = host && cur >= 0 && cur < 62 ? (unsigned)(cur + 1) : 0u;
    if (!host && ed_online() && !g_in_match) {
        int hp = (int)((bn_member_value(0) >> 8) & 0x3f);
        if (hp > 0 && hp - 1 < g_mx_count && hp - 1 != (int)cur) {
            InterlockedExchange(&g_mx_sel, hp - 1);
            mx_apply(hp - 1);
            logf("BFR lobby: the host's stage pack %s (%s) - Map Slot 7 switched to it", g_mx[hp - 1].file, g_mx[hp - 1].name);
        }
    }
    unsigned v = (BN_MAGIC << 24) | ((unsigned)(g_bn_vote & 0xff) << 16) | (host && g_nm_on > 0 ? 0x8000u : 0) | (host && g_dg_on ? 0x4000u : 0) | (pk << 8) | (host ? (unsigned)(g_bn_rule_host & 0xff) : 0);
    *g_bn_cell = v;
    if (v != g_bn_sent && ed_online()) {
        g_bn_sent = v;
        __try { ((void(__fastcall *)())(g_base + BN_RESEND))(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        logf("BFR lobby: sent mode %d, vote %d, next map %d", v & 0xff, (v >> 16) & 0xff, (v >> 15) & 1);
    }
}

// the lobby script (tools/build_live_bfr.py): native 2.8 ops 83 mode name, 84 mode chosen, 85 vote, 86 Next Map row text,
// 87 Next Map switch
static INT64 ars_native(int op, INT64 *args);
// v5.62 the lobby lists (tools/build_live_bfr.py v2): each row's value is its position; op 92 lays the rows out, op 91
// names them, op 89 does a row, op 93 the cursor, op 90 the SESSION INFO rule line. Teammate Lock-on is the game's own
// option byte 0x14052dfb9 (Ghidra full pass 065: the options screens FUN_14024b700 / FUN_14024b9d0 list it with
// 0x14052dfb0..; the lock-on test FUN_1402543e0 refuses the own team only while it is 0) - this machine only.
static bool live_rule_ok(int r);                                // cwo_arsenal.inl
static bool hf_show();                                         // v5.109 cwo_hudface.inl
static void hf_set_show(bool on);
static int g_bn_list, g_bn_map[16], g_bn_nmap, g_bn_cursor;
static char g_bn_txt[16][48];
static int bn_layout(int list) {
    g_bn_list = list; g_bn_nmap = 0;
    if (list == 0) {
        for (int r = 0; r < 12; r++) if (live_rule_ok(r)) g_bn_map[g_bn_nmap++] = r;
        for (int k = 0; k < BN_NRULES && g_bn_nmap < 16; k++) g_bn_map[g_bn_nmap++] = 100 + k;
    } else {
        g_bn_map[g_bn_nmap++] = 200;                              // Teammate Lock-on (everyone)
        if (bn_host()) g_bn_map[g_bn_nmap++] = 201;               // Next Map (the host)
        g_bn_map[g_bn_nmap++] = 202;                              // Tiebreaker Vote (everyone)
        g_bn_map[g_bn_nmap++] = 203;                              // v5.65 HUD Preset (everyone, this machine)
        if (bn_host()) g_bn_map[g_bn_nmap++] = 204;               // v5.103 Downed Glow (the host; everyone follows)
        g_bn_map[g_bn_nmap++] = 205;                              // v5.109 HUD Faces (everyone, this machine)
    }
    return g_bn_nmap;
}
static bool bn_mate_lock() { __try { return *(unsigned char *)(g_base + BN_MATE_LOCK) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
// v5.148.21 (owner queue 2026-11-09: "Pause menu: Target lock-on on/off"): in an OFFLINE match, while the game's pause menu is up
// (menu id 0x31 - FUN_140167580 opens it with FUN_140246130(menu, 0x31); the current id at *(*0x140533c58 + 0x105f4)), Y turns
// the game's own lock-on option byte 0x14052dfb9 (teammate lock-on, the same byte as the lobby's BFR Options row) on / off.
// Online it stays the lobby's toggle only (the owner's online exception covers the lobby).
// v5.152 (owner 2026-11-12): the pause menu has its own row now (build_pause_lockon.py: row 4
// of the PAUSE menus, text = op 116, A = op 117); the 5.148.21 status line + Y on the pause menu are gone
static void bn_pause_press(WORD) {}
static INT64 bn_pause_lock_native(int op) {                      // game thread (the pause menu script)
    static char t[40];
    if (op == 117) {
        if (ed_online()) logf("pause menu: Teammate Lock-on row refused online (the lobby's BFR Options row sets it there)");
        else {
            __try {
                unsigned char *b = (unsigned char *)(g_base + BN_MATE_LOCK);
                DWORD o = 0;
                if (VirtualProtect(b, 1, PAGE_READWRITE, &o)) { *b = *b ? 0 : 1; VirtualProtect(b, 1, o, &o); }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            logf("pause menu: teammate lock-on %s", bn_mate_lock() ? "on" : "off");
        }
    }
    sprintf_s(t, "Teammate Lock-on: %s", bn_mate_lock() ? "ON" : "OFF");
    return (INT64)(ULONG_PTR)t;
}
static INT64 bn_native(int op, INT64 *args) {
    INT64 a1 = 0;
    __try { a1 = args[1]; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    static char nm_text[32];
    switch (op) {
    case 83: return a1 >= 0 && a1 < BN_NRULES ? (INT64)(ULONG_PTR)BN_RULES[a1] : 0;
    case 84: {
        if (a1 < 0 || a1 >= BN_NRULES || !bn_host()) return 0;
        INT64 sa[2] = {38, 0};                                   // the game's rule underneath: Standard Battle
        ars_native(38, sa);
        g_bn_rule_host = (int)a1 + 1;
        char msg[64]; sprintf_s(msg, "Game rule: %s", BN_RULES[a1]); set_status(msg);
        logf("BFR lobby: the host chose %s", BN_RULES[a1]);
        return 1;
    }
    case 85: {
        if (!bn_tb_pending()) { set_status(bn_series_locked() ? "Teams locked: the vote opens by itself after a 1 - 1" : "The vote opens by itself after 1 win each, same teams"); return 0; }
        g_bn_vote = g_bn_vote == 1 ? 2 : 1;
        set_status(g_bn_vote == 1 ? "Tiebreaker vote: YES" : "Tiebreaker vote: NO");
        return 1;
    }
    case 86: sprintf_s(nm_text, "Next Map: %s", g_nm_on > 0 ? "ON" : "OFF"); return (INT64)(ULONG_PTR)nm_text;
    case 92: return bn_layout((int)a1);
    case 91: {
        int k = (int)a1;
        if (k < 0 || k >= g_bn_nmap || k >= 16) return 0;
        int v = g_bn_map[k];
        char *t = g_bn_txt[k];
        if (v < 12) strcpy_s(t, 48, ED_RULES[v]);
        else if (v < 100 + BN_NRULES) strcpy_s(t, 48, BN_RULES[v - 100]);
        else if (v == 200) sprintf_s(t, 48, "Teammate Lock-on: %s", bn_mate_lock() ? "ON" : "OFF");
        else if (v == 201) sprintf_s(t, 48, "Next Map: %s", g_nm_on > 0 ? "ON" : "OFF");
        else if (v == 202) sprintf_s(t, 48, "Tiebreaker: %s", g_tb_on ? "ON - same teams" : !bn_tb_pending() ? "auto after 1 - 1" : g_bn_vote == 1 ? "vote YES" : g_bn_vote == 2 ? "vote NO" : "vote now");
        else if (v == 203) strcpy_s(t, 48, hud_row_text());
        else if (v == 204) sprintf_s(t, 48, "Downed Glow: %s", g_dg_on ? "ON" : "OFF");
        else if (v == 205) sprintf_s(t, 48, "HUD Faces: %s", hf_show() ? "ON" : "OFF");
        else return 0;
        return (INT64)(ULONG_PTR)t;
    }
    case 93: {
        if (g_bn_list == 1) return g_bn_cursor >= 0 && g_bn_cursor < g_bn_nmap ? g_bn_cursor : 0;
        int cur = -1;
        __try { cur = g_bn_rule_host > 0 && bn_host() ? 100 + g_bn_rule_host - 1 : *(int *)(g_base + BN_LIVE_RULE); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        for (int k = 0; k < g_bn_nmap; k++) if (g_bn_map[k] == cur) return k;
        return 0;
    }
    case 89: {
        int k = (int)a1;
        if (k < 0 || k >= g_bn_nmap) return 1;
        int v = g_bn_map[k];
        if (v < 12) { INT64 sa[2] = {38, v}; ars_native(38, sa); return 1; }
        if (v < 100 + BN_NRULES) { INT64 sa[2] = {84, v - 100}; bn_native(84, sa); return 1; }
        g_bn_cursor = k;
        if (v == 200) {
            __try { unsigned char *b = (unsigned char *)(g_base + BN_MATE_LOCK); *b = *b ? 0 : 1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
            set_status(bn_mate_lock() ? "Teammate Lock-on ON: RB can lock on to your teammates" : "Teammate Lock-on OFF");
            logf("BFR lobby: teammate lock-on %s", bn_mate_lock() ? "on" : "off");
            return 2;
        }
        if (v == 201) { INT64 sa[2] = {87, 0}; bn_native(87, sa); return 2; }
        if (v == 202) { INT64 sa[2] = {85, 0}; bn_native(85, sa); return 2; }
        if (v == 203) { hud_cycle(); return 2; }
        if (v == 204) { g_dg_on = !g_dg_on; set_status(g_dg_on ? "Downed Glow ON: a glow on every downed fighter, for everyone" : "Downed Glow OFF"); logf("BFR lobby: downed glow %s", g_dg_on ? "on" : "off"); return 2; }
        if (v == 205) { hf_set_show(!hf_show()); set_status(hf_show() ? "HUD Faces ON" : "HUD Faces OFF"); return 2; }
        return 1;
    }
    case 90: { int r = bn_rule(); return r >= 1 && r <= BN_NRULES ? (INT64)(ULONG_PTR)BN_RULES[r - 1] : 0; }
    case 111: {                                                   // v5.95 Create Game Session: Left / Right on the rule row
        int dir = (int)a1, live = -1;
        __try { live = *(int *)(g_base + BN_LIVE_RULE); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        int ours = g_bn_rule_host;
        if (ours > 0) {                                           // one of ours is on (the game's rule underneath: Standard)
            int nx = ours + (dir > 0 ? 1 : -1);
            if (nx >= 1 && nx <= BN_NRULES) { g_bn_rule_host = nx; return 1; }
            g_bn_rule_host = 0;
            return dir > 0 ? 1 : 0;                                 // past the last: Standard Battle; before the first: the game's last
        }
        if (dir > 0 && live == 11) { g_bn_rule_host = 1; return 0; }   // after Annihilate the Odd One: ours (the game wraps to Standard)
        if (dir < 0 && live == 0) { g_bn_rule_host = BN_NRULES; return 1; }
        return 0;
    }
    case 112: return g_bn_rule_host >= 1 && g_bn_rule_host <= BN_NRULES ? (INT64)(ULONG_PTR)BN_RULES[g_bn_rule_host - 1] : 0;
    case 87: g_nm_on = g_nm_on > 0 ? 0 : 1; nm_save_switch(); set_status(g_nm_on ? "Next Map ON" : "Next Map OFF"); return 1;
    }
    return 0;
}

static int bn_self_test() {                                      // the three sites on a stand-in, the stub's bytes, the value
    static unsigned char fake[0x200000], page[0x300];
    memset(page, 0, sizeof(page));
    ULONG_PTR base = (ULONG_PTR)fake - 0x180000;                  // the sites 0x19112c / 0x191a60 / 0x274d20 inside fake
    memcpy((void *)(base + BN_SEND), BN_SEND_OLD, 5); memcpy((void *)(base + BN_HIT), BN_HIT_OLD, 10); memcpy((void *)(base + BN_RECV), BN_RECV_OLD, 15);
    PFN_BN_HIT kh = g_bn_hit_orig; PFN_BN_RECV kr = g_bn_recv_orig; volatile unsigned long long *kc = g_bn_cell;
    bool ok = bn_apply(base, (ULONG_PTR)page, false);
    unsigned char *s1 = (unsigned char *)(base + BN_SEND);
    int rel; memcpy(&rel, s1 + 1, 4);
    ok = ok && s1[0] == 0xe8 && (ULONG_PTR)(s1 + 5 + rel) == (ULONG_PTR)page;
    memcpy(&rel, page + 3, 4); ok = ok && (ULONG_PTR)(page + 7 + rel) == (ULONG_PTR)(page + 0x200) && page[11] == 0x4c && page[14] == 0xc3;
    memcpy(&rel, page + 0x4b, 4); ok = ok && (ULONG_PTR)(page + 0x4f + rel) == base + BN_HIT + 10 && !memcmp(page + 0x40, BN_HIT_OLD, 10);
    memcpy(&rel, page + 0x90, 4); ok = ok && (ULONG_PTR)(page + 0x94 + rel) == base + BN_RECV + 15 && !memcmp(page + 0x80, BN_RECV_OLD, 15);
    ok = ok && !bn_apply(base, (ULONG_PTR)page, false);          // a patched site is refused
    g_bn_hit_orig = kh; g_bn_recv_orig = kr; g_bn_cell = kc;
    // the vote rule: 3 yes, or as many yes as no, = tiebreaker
    int keep_vote = g_bn_vote; unsigned keep_in[8]; memcpy(keep_in, g_bn_in, sizeof(keep_in));
    memset(g_bn_in, 0, sizeof(g_bn_in));
    g_bn_vote = 1; g_bn_in[1] = (BN_MAGIC << 24) | (1u << 16); g_bn_in[2] = (BN_MAGIC << 24) | (2u << 16); g_bn_in[3] = 0x12345678;
    int y, n; bn_tb_count(&y, &n);
    bool vote = y == 2 && n == 1;
    g_bn_vote = keep_vote; memcpy(g_bn_in, keep_in, sizeof(keep_in));
    logf("self-test: BFR lobby hooks %s, vote count %s", ok ? "ok" : "FAILED", vote ? "ok" : "FAILED");
    return ok && vote ? 1 : -90;
}
