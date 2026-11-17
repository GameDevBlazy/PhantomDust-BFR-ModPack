// ---- v5.133 the Skill Vault (owner 2026-11-10).
// Game facts (recorded, cwo_arsenal.inl / MODLOG 2026-10-25 batch_065): profile *0x1404bec10, arsenal set in use = *(profile
// + 0x33a0); player p's descriptor = set + 0x3a30 + p * 0x38: +0 the 16 arsenal records (100 bytes: +4 name, +0x14 30 skill
// numbers as shorts, -1 none), +8 the stock counts (one signed byte per skill id - the Lab's stock list FUN_140172910 reads the
// same kind of array, id 1.. upward, "> 0" = owned); FUN_14015a730(profile, p) (native 5.115) recounts and checks the stock.
// Skill names: the game's own text rows (sc_live_name, cwo_newskills.inl). View Stock's skill card = menu window type 9
// (FUN_140249df0, cwo_sixhud.inl).
// What this does: on View Stock (a type-9 window up, not in a match) Back + Y opens the SKILL VAULT page: every skill you own 0
// of (ids 1-999 with a name). A: pick it; then your owned skills (not used in any of your 16 arsenals) are listed - A: that one
// is replaced: the vault skill takes its copies (its stock slot now holds the new id) and the replaced skill goes to the vault.
// The layout is checked first (every skill in your arsenals must show as owned) - else nothing is changed.
// EDP_VAULT (58) is declared in cwo_edgar.inl with the other page ids
static const int VT_MAX_ID = 1000;
static int g_vt_phase, g_vt_row, g_vt_top, g_vt_pick = -1;
static short g_vt_list[VT_MAX_ID];
static int g_vt_n;
static char g_vt_msg[96];

// v5.133.1 (in game 06:35, memory read): the descriptors live at profile + 0x3a30 (as ars_records reads them); *(profile +
// 0x33a0) pointed at the empty second set (+0x3fb8). Player 1 = p 0: 16 arsenals, stock counts = a byte per id (cnt[id]).
static ULONG_PTR vt_set() { return *(ULONG_PTR *)(g_base + RVA_PROFILE); }
static signed char *vt_counts(ULONG_PTR set) { return set ? *(signed char **)(set + 0x3a30 + 8) : NULL; }
static unsigned char *vt_recs(ULONG_PTR set) { return set ? *(unsigned char **)(set + 0x3a30) : NULL; }

static bool vt_in_arsenal(unsigned char *recs, int id) {
    for (int a = 0; a < 16; a++) {
        unsigned char *r = recs + 100 * a;
        if (!r[4]) continue;
        for (int k = 0; k < 30; k++) if (*(short *)(r + 0x14 + 2 * k) == id) return true;
    }
    return false;
}
static bool vt_layout_ok(signed char *cnt, unsigned char *recs) {   // every skill in a named arsenal shows as owned
    int seen = 0, owned = 0;
    for (int a = 0; a < 16; a++) {
        unsigned char *r = recs + 100 * a;
        if (!r[4]) continue;
        for (int k = 0; k < 30; k++) {
            short v = *(short *)(r + 0x14 + 2 * k);
            if (v <= 0 || v >= VT_MAX_ID) continue;
            seen++;
            if (cnt[v] > 0) owned++;
        }
    }
    return seen >= 5 && owned * 100 >= seen * 95;
}
static void vt_build() {                                           // phase 0: the vault, phase 1: replaceable stock
    g_vt_n = 0;
    __try {
        ULONG_PTR set = vt_set();
        signed char *cnt = vt_counts(set);
        unsigned char *recs = vt_recs(set);
        if (!cnt || !recs) { strcpy_s(g_vt_msg, "No profile loaded"); return; }
        if (!vt_layout_ok(cnt, recs)) { strcpy_s(g_vt_msg, "Stock layout not recognized - the vault is off (nothing changed)"); return; }
        for (int id = 1; id < VT_MAX_ID && g_vt_n < VT_MAX_ID; id++) {
            if (!sc_live_name(id)) continue;
            bool in_vault = cnt[id] <= 0;
            if (g_vt_phase == 0 ? in_vault : (!in_vault && !vt_in_arsenal(recs, id))) g_vt_list[g_vt_n++] = (short)id;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_vt_n = 0; strcpy_s(g_vt_msg, "Could not read the stock"); }
}
static void vt_open() {                                            // pad loop, not in a match, View Stock up
    g_vt_phase = 0; g_vt_row = g_vt_top = 0; g_vt_pick = -1; g_vt_msg[0] = 0;
    vt_build();
    ed_reopen(EDP_VAULT);
    InterlockedExchange(&g_vt_shown, 1);
    logf("skill vault: opened - %d skills in the vault%s%s", g_vt_n, g_vt_msg[0] ? " - " : "", g_vt_msg);
}
static bool vt_stock_view() {                                      // a View Stock skill card (window type 9) is up
    __try {
        ULONG_PTR m = *(ULONG_PTR *)(g_base + RVA_ARS_MENU);
        if (!m) return false;
        for (int k = 0; k < 16; k++) if (*(short *)(m + 0x40 + 0x103c * (ULONG_PTR)k + 8) == 9) return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
static void vt_input(WORD pressed) {
    if (pressed & BTN_B) {
        if (g_vt_phase == 1) { g_vt_phase = 0; g_vt_row = g_vt_top = 0; vt_build(); return; }
        ed_close(); InterlockedExchange(&g_vt_shown, 0); return;
    }
    if (!g_vt_n) return;
    if (pressed & BTN_DOWN) g_vt_row = (g_vt_row + 1) % g_vt_n;
    if (pressed & BTN_UP) g_vt_row = (g_vt_row + g_vt_n - 1) % g_vt_n;
    if (pressed & BTN_RIGHT) g_vt_row = g_vt_row + 10 < g_vt_n ? g_vt_row + 10 : g_vt_n - 1;
    if (pressed & BTN_LEFT) g_vt_row = g_vt_row >= 10 ? g_vt_row - 10 : 0;
    if (!(pressed & BTN_A)) return;
    int id = g_vt_list[g_vt_row];
    if (g_vt_phase == 0 && bfr_locked(id)) {                    // v5.186.9 shown in the vault, not playable
        sprintf_s(g_vt_msg, "%d %s is not playable yet - it stays in the vault", id, sc_skill_name(id));
        return;
    }
    if (g_vt_phase == 0) {
        g_vt_pick = id; g_vt_phase = 1; g_vt_row = g_vt_top = 0; vt_build();
        sprintf_s(g_vt_msg, "Replace which stock skill with %d %s?", id, sc_skill_name(id));
        return;
    }
    __try {                                                        // the swap: the vault skill takes the stock slot's copies
        ULONG_PTR set = vt_set();
        signed char *cnt = vt_counts(set);
        unsigned char *recs = vt_recs(set);
        if (!cnt || !recs || !vt_layout_ok(cnt, recs) || cnt[g_vt_pick] > 0 || cnt[id] <= 0 || vt_in_arsenal(recs, id)) {
            strcpy_s(g_vt_msg, "Not changed (the stock moved - open the vault again)"); return;
        }
        signed char c = cnt[id];
        cnt[g_vt_pick] = c; cnt[id] = 0;
        ULONG_PTR prof = *(ULONG_PTR *)(g_base + RVA_PROFILE);
        ((PFN_ARS_COUNT)(g_base + RVA_ARS_COUNT_FN))(prof, 0);     // native 5.115: recount + stock check
        sprintf_s(g_vt_msg, "%d %s replaced %d %s (x%d)", g_vt_pick, sc_skill_name(g_vt_pick), id, sc_skill_name(id), (int)c);
        logf("skill vault: skill %d took the stock of skill %d (x%d); skill %d is in the vault now", g_vt_pick, id, (int)c, id);
        set_status(g_vt_msg);
    } __except (EXCEPTION_EXECUTE_HANDLER) { strcpy_s(g_vt_msg, "Could not change the stock"); }
    g_vt_phase = 0; g_vt_row = g_vt_top = 0; g_vt_pick = -1; vt_build();
}
template <class PUT, class BOX> static void vt_draw(PUT &put, BOX &box, int x, int y) {
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF, GREY = 0xFFB0B0B0;
    const int LINE = 16, SHOW = 14;
    (void)box;
    put(x, y, g_vt_phase == 0 ? "SKILL VAULT - skills not in your stock   (A: pick  B: close)" :
                                "SKILL VAULT - replace which stock skill?   (A: replace  B: back)", GOLD);
    y += LINE + 4;
    if (g_vt_msg[0]) { put(x, y, g_vt_msg, BLUE); y += LINE; }
    if (!g_vt_n) { put(x, y, g_vt_phase == 0 ? "The vault is empty" : "No free stock skill (all are in your arsenals)", WHITE); return; }
    if (g_vt_row < g_vt_top) g_vt_top = g_vt_row;
    if (g_vt_row >= g_vt_top + SHOW) g_vt_top = g_vt_row - SHOW + 1;
    char line[96];
    for (int i = g_vt_top; i < g_vt_n && i < g_vt_top + SHOW; i++, y += LINE) {
        int id = g_vt_list[i];
        sprintf_s(line, "%s%4d  %s", i == g_vt_row ? "> " : "  ", id, sc_skill_name(id));
        put(x, y, line, i == g_vt_row ? GOLD : WHITE);
    }
    sprintf_s(line, "%d of %d   Up/Down: move   Left/Right: 10 at a time", g_vt_row + 1, g_vt_n);
    put(x, y + 4, line, GREY);
}
