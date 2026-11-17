// ---- v5.29 Change Game Type in the Xbox Live / system link lobby (owner 2026-10-27, with screenshots of the Xbox Live
// LEVEL LIST and a private LOBBY: "you're not able to change the game type in the lobby. That should be a thing"; then
// "Change game type, like all that. Just go ahead and build that"). OPTIONS > Change Game Type (the host) opens GAME TYPE:
// One-on-One / Tag Team / Battle Royale, the cursor on the session's; a choice changes the session the game's own way and
// the lobby is built again (tools/build_live_gametype.py; native 2.8 ops 55 / 56).
// Game facts (Ghidra full pass 065 via pcode.py / pd_fn.py, capstone, 2026-10-27; MODLOG 2026-10-26 / 27):
//  - the host's game type DAT_14050d2d8 (0 Battle Royale, 1 Tag Team, 2 One-on-One) and rule DAT_140518cf0; 4.578 =
//    FUN_140183010(?, dtype, drule, drating, dsession) is CREATE GAME SESSION's step: the type moves by dtype (mod 3), a new
//    type puts the rule back to 0, and rules the type does not allow are stepped over.
//  - the match copies the lobby reads: 4.591 = DAT_1405277b4 (type: the LOBBY builds 2 player rows, 4 when it is not 2;
//    SESSION INFO's Game Type row), 4.592 = DAT_1405277fc (rule); FUN_1401910a0 sends both in this machine's player info,
//    FUN_140191210 sends it to every member, clients copy them in FUN_140191a60.
//  - FUN_140181ba0 makes the session: room DAT_14051c0a4 = 2 for One-on-One (or when DAT_14050d2c0 == 1), else 4;
//    FUN_1401832a0 works out the open places from it (DAT_14050e13c); DAT_140594e94 = 1 has the next online tick
//    (FUN_140181210) publish the session's properties again (FUN_140182b60 / FUN_140283220).
//  - members: DAT_140594ea4 entries of 0x298 at 0x140595ea8 (+0 in use); this machine's member number DAT_140527bc4 (0 = the
//    host).
static const ULONG_PTR RVA_LT_MATCH_TYPE = 0x5277b4, RVA_LT_ROOM = 0x51c0a4, RVA_LT_KIND = 0x50d2c0, RVA_LT_PUBLISH = 0x594e94,
                       RVA_LT_MEMBERS = 0x594ea4, RVA_LT_MEMBER0 = 0x595ea8, RVA_LT_OPEN_FN = 0x1832a0;
static const char *LT_NAMES[3] = {"Battle Royale", "Tag Team", "One-on-One"};
typedef void(__fastcall *PFN_LT_VOID)();
static unsigned char *g_lt_test;                                // self-test: a stand-in for the globals (offset = RVA & 0xffff)
static int g_lt_t_steps, g_lt_t_sends, g_lt_t_opens;

static int *lt_int(ULONG_PTR rva) { return g_lt_test ? (int *)(g_lt_test + (rva & 0xffff)) : (int *)(g_base + rva); }
static int lt_players() {                                       // the host + every member in use
    int n = *lt_int(RVA_LT_MEMBERS), p = 1;
    for (int k = 0; k < n && k < 8; k++) if (*lt_int(RVA_LT_MEMBER0 + 0x298 * (ULONG_PTR)k)) p++;
    return p;
}
// the session's game type to t (0-2); false (and why, on screen) when it cannot be
static bool lt_set(int t) {
    char msg[96];
    int *type = lt_int(RVA_LIVE_TYPE), *rule = lt_int(RVA_LIVE_RULE);
    if (t < 0 || t > 2) return false;
    if (*lt_int(RVA_LIVE_ME) != 0) { set_status("Only the host can change the game type"); return false; }
    if (*type == t) return true;
    if (t != 2 && *lt_int(RVA_LT_KIND) == 1) { set_status("This kind of session has room for 2: One-on-One only"); return false; }
    int players = lt_players();
    if (t == 2 && players > 2) {
        sprintf_s(msg, "One-on-One is for 2 players - %d are in this session", players);
        set_status(msg);
        return false;
    }
    int from = *type;
    for (int i = 0; i < 3 && *type != t; i++) {                 // the game's own step (the rule goes back to Standard)
        if (g_lt_test) { *type = (*type + 1) % 3; *rule = 0; g_lt_t_steps++; }
        else ((PFN_LIVE_STEP)(g_base + RVA_LIVE_STEP))(0, 1, 0, 0, 0);
    }
    if (*type != t) { logf("Xbox Live lobby: game type %d -> %d failed (now %d)", from, t, *type); return false; }
    *lt_int(RVA_LT_ROOM) = t == 2 ? 2 : 4;                       // room as the game makes it for this type
    *lt_int(RVA_LT_MATCH_TYPE) = t;
    *lt_int(RVA_LIVE_MATCH_RULE) = *rule;
    if (g_lt_test) { g_lt_t_opens++; g_lt_t_sends++; }
    else {
        ((PFN_LT_VOID)(g_base + RVA_LT_OPEN_FN))();             // the open places
        *lt_int(RVA_LT_PUBLISH) = 1;                             // the session's properties again at the next tick
        ((PFN_LT_VOID)(g_base + RVA_LIVE_SEND_INFO))();          // type and rule to every member
    }
    logf("Xbox Live lobby: game type %s -> %s (rule %d, room %d, %d player(s)), sent to the others", LT_NAMES[from % 3],
         LT_NAMES[t], *rule, *lt_int(RVA_LT_ROOM), players);
    sprintf_s(msg, "Game type: %s (the rule is back to Standard Battle)", LT_NAMES[t]);
    set_status(msg);
    return true;
}
// native 2.8 op 55: the GAME TYPE list's choice (args[1] = the type); op 56: the list's row for the session's type
// (rows: Battle Royale, Tag Team, One-on-One - the row is the type number)
static INT64 lt_native(int op, INT64 *args) {
    __try {
        if (op == 55) return lt_set((int)args[1]) ? 1 : 0;
        if (op == 56) { int t = *lt_int(RVA_LIVE_TYPE); return t >= 0 && t <= 2 ? t : 0; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("Xbox Live lobby: game type change failed (fault)"); }
    return 0;
}
static int lt_self_test() {
    static unsigned char mem[0x10000];
    memset(mem, 0, sizeof(mem));
    g_lt_test = mem; g_lt_t_steps = g_lt_t_sends = g_lt_t_opens = 0;
    *lt_int(RVA_LIVE_TYPE) = 2; *lt_int(RVA_LIVE_RULE) = 3; *lt_int(RVA_LT_ROOM) = 2; *lt_int(RVA_LT_MATCH_TYPE) = 2;
    INT64 a[2] = {56, 0};
    bool row = lt_native(56, a) == 2;                           // One-on-One = row 2
    a[1] = 0;
    bool br = lt_native(55, a) == 1 && *lt_int(RVA_LIVE_TYPE) == 0 && *lt_int(RVA_LT_MATCH_TYPE) == 0 && *lt_int(RVA_LT_ROOM) == 4 &&
              *lt_int(RVA_LIVE_MATCH_RULE) == 0 && g_lt_t_sends == 1 && lt_native(56, a) == 0;
    *lt_int(RVA_LT_MEMBERS) = 3; *lt_int(RVA_LT_MEMBER0) = 1; *lt_int(RVA_LT_MEMBER0 + 0x298) = 1;   // 3 players in
    a[1] = 2;
    bool full = lt_native(55, a) == 0 && *lt_int(RVA_LIVE_TYPE) == 0;
    *lt_int(RVA_LIVE_ME) = 1;                                   // a client: never
    a[1] = 1;
    bool client = lt_native(55, a) == 0 && *lt_int(RVA_LIVE_TYPE) == 0;
    g_lt_test = NULL;
    logf("self-test: Xbox Live game type: row %s, change %s, too many for 1v1 %s, client %s", row ? "ok" : "FAILED",
         br ? "ok" : "FAILED", full ? "ok" : "FAILED", client ? "ok" : "FAILED");
    return row && br && full && client ? 1 : -74;
}
