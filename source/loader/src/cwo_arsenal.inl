// ---- v5.9 arsenal lists and the Xbox Live lobby's rule list (owner 2026-10-25; "This should also be in multiplayer split screen and Xbox Live multiplayer"). Included by
// cwo_edgar.inl.
// Game facts (Ghidra batch_065 + capstone, 2026-10-25; 25_Menu_Research/arsenal_2026-10-25):
//   the profile *0x1404bec10 holds two arsenal sets (campaign / multiplayer: +0 and +0x3fb8, the one in use at +0x33a0,
//     FUN_14015c0e0); player p's arsenal descriptor is +0x3a30 + p * 0x38: +0 the 16 arsenal records of 100 bytes
//     (deck_records.py: +4 name, +0x14 30 skill numbers, -1 = none, +0x52 flags), +8 the stock counts, +0x18 the number
//     of arsenals (FUN_14024c220 ops 0x1f / 0x23 / 0x24).
//   FUN_14015a730(profile, p) - native 5.115, which the menus call before an arsenal list - counts the records with a
//     name (from the first), recomputes each one's flags (FUN_14015b830) and checks the stock (FUN_14015a8d0).
//   the arsenal list window is menu window type 16 (class 0x1403fa390); its set-up 0x140248ea0 copies that count into the
//     selector *0x1404bec18 (+0x18) with the cursor at the top (+0 / +8 / +0x10 = 0, +0x28 = 1, +0x30 = p); p = *0x1404bec08
//     +0xb50 (native 5.111). Window k = menu *0x140533c58 + 0x40 + k * 0x103c: +8 type, +0x1024 the title (2.102); the
//     focused window index is menu +0x10410 (2.107).
//   a skill's free stock = owned copies - its uses in every arsenal of the set (FUN_1401c3ea0), so each list uses the stock
//     on its own.
// The loader keeps lists 1-3 of each set in CwoArsenalLists.bin; the game's 16 records always hold the list in use (the
// game saves them as before). The list in use is found again by its records when the game starts.
static const ULONG_PTR RVA_ARS_MENU = 0x533c58, RVA_PROFILE = 0x4bec10, RVA_ARS_UI = 0x4bec08, RVA_ARS_SEL = 0x4bec18, RVA_ARS_COUNT_FN = 0x15a730;
static const int ARS_LISTS = 3, ARS_N = 16, ARS_REC = 100;
typedef void(__fastcall *PFN_ARS_COUNT)(ULONG_PTR, int);
struct ArsFile {
    char magic[8];
    int active[2], used[2][ARS_LISTS];
    unsigned char rec[2][ARS_LISTS][ARS_N * ARS_REC];
};
static ArsFile g_ars;
static bool g_ars_loaded, g_ars_synced[2];
static volatile LONG g_ars_req;                                 // D-pad Left / Right on an arsenal list (-1 / +1)
static ULONGLONG g_ars_sync_t;
static ULONG_PTR g_ars_test_menu, g_ars_test_prof, g_ars_test_ui, g_ars_test_sel;   // self-test stand-ins
static PFN_ARS_COUNT g_ars_test_count;

static ULONG_PTR ars_menu() { return g_ars_test_menu ? g_ars_test_menu : *(ULONG_PTR *)(g_base + RVA_ARS_MENU); }
static ULONG_PTR ars_prof() { return g_ars_test_prof ? g_ars_test_prof : *(ULONG_PTR *)(g_base + RVA_PROFILE); }
static ULONG_PTR ars_ui() { return g_ars_test_ui ? g_ars_test_ui : *(ULONG_PTR *)(g_base + RVA_ARS_UI); }
static ULONG_PTR ars_sel() { return g_ars_test_sel ? g_ars_test_sel : *(ULONG_PTR *)(g_base + RVA_ARS_SEL); }

static void ars_path(wchar_t *p) { swprintf_s(p, MAX_PATH, L"%sCwoArsenalLists.bin", g_dir); }
static void ars_load() {
    g_ars_loaded = true;
    memset(&g_ars, 0, sizeof(g_ars));
    wchar_t p[MAX_PATH];
    ars_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"rb") == 0 && f) {
        ArsFile t;
        if (fread(&t, 1, sizeof(t), f) == sizeof(t) && !memcmp(t.magic, "CWOARSL1", 8)) g_ars = t;
        fclose(f);
    }
    memcpy(g_ars.magic, "CWOARSL1", 8);
    for (int s = 0; s < 2; s++) if (g_ars.active[s] < 0 || g_ars.active[s] >= ARS_LISTS) g_ars.active[s] = 0;
}
static void ars_save() {
    wchar_t p[MAX_PATH], t[MAX_PATH];
    ars_path(p);
    swprintf_s(t, L"%s.tmp", p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, t, L"wb") != 0 || !f) return;
    bool ok = fwrite(&g_ars, 1, sizeof(g_ars), f) == sizeof(g_ars);
    fclose(f);
    if (ok) MoveFileExW(t, p, MOVEFILE_REPLACE_EXISTING); else DeleteFileW(t);
}

// the focused menu window when it is an arsenal list (type 16), else 0
static ULONG_PTR ars_list_window() {
    ULONG_PTR m = ars_menu();
    if (!m) return 0;
    int k = *(int *)(m + 0x10410);
    if (k < 0 || k >= 16) return 0;
    ULONG_PTR win = m + 0x40 + 0x103c * (ULONG_PTR)k;
    return *(short *)(win + 8) == 16 ? win : 0;
}
static int ars_side(ULONG_PTR prof) { return *(ULONG_PTR *)(prof + 0x33a0) == prof ? 0 : 1; }
static unsigned char *ars_records(ULONG_PTR prof, int p) { return *(unsigned char **)(prof + 0x3a30 + 0x38 * (ULONG_PTR)p); }

// the list in use, found again by its records the first time a list shows this run (the game saves the records itself)
static void ars_sync(ULONG_PTR prof, bool force) {
    int s = ars_side(prof);
    unsigned char *r = ars_records(prof, 0);
    if (!r) return;
    if (!g_ars_synced[s]) {
        g_ars_synced[s] = true;
        for (int k = 0; k < ARS_LISTS; k++)
            if (g_ars.used[s][k] && !memcmp(g_ars.rec[s][k], r, ARS_N * ARS_REC)) { g_ars.active[s] = k; break; }
        logf("arsenal lists: set %d, list %d in use", s, g_ars.active[s] + 1);
        force = true;
    }
    int a = g_ars.active[s];
    if (force || !g_ars.used[s][a] || memcmp(g_ars.rec[s][a], r, ARS_N * ARS_REC)) {
        memcpy(g_ars.rec[s][a], r, ARS_N * ARS_REC);
        g_ars.used[s][a] = 1;
        ars_save();
    }
}
// v5.59 (owner 2026-11-02): the list window's own title
// is the loader's buffer (native 2.102 keeps the pointer it is given; tools/build_ars_title.py), kept on the list in use
static char g_ars_title[3][40] = {"ARSENAL LIST 1", "ARSENAL LIST 1: PLAYER 1", "ARSENAL LIST 1: PLAYER 2"};
static void ars_titles() {
    int s = 0;                                                   // v5.62 the set in use (campaign 0, multiplayer 1)
    __try { ULONG_PTR prof = ars_prof(); if (prof) s = ars_side(prof); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    int a0 = g_ars.active[s] + 1, a1 = 1;                       // player 2 edits a copy: no lists
    if (a0 < 1 || a0 > ARS_LISTS) a0 = 1;
    if (a1 < 1 || a1 > ARS_LISTS) a1 = 1;
    sprintf_s(g_ars_title[0], "ARSENAL LIST %d", a0);
    sprintf_s(g_ars_title[1], "ARSENAL LIST %d: PLAYER 1", a0);
    sprintf_s(g_ars_title[2], "ARSENAL LIST: PLAYER 2");
    (void)a1;
}
static INT64 ars_title_native(INT64 *args) {                    // native 2.8 op 88 (args[1]: 0 plain, 1 / 2 the player)
    int k = (int)args[1];
    if (k < 0 || k > 2) k = 0;
    ars_titles();
    return (INT64)(ULONG_PTR)g_ars_title[k];
}
static bool ars_switch(int dir) {                              // game thread
    ULONG_PTR prof = ars_prof(), win = ars_list_window(), ui = ars_ui(), sel = ars_sel();
    if (!prof || !win || !ui || !sel) return false;
    int p = *(int *)(ui + 0xb50);
    if (p != 0) { set_status("Arsenal lists: player 1's arsenals only (player 2 edits a copy)"); return false; }
    unsigned char *r = ars_records(prof, 0);
    if (!r) return false;
    ars_sync(prof, true);
    int s = ars_side(prof), a = g_ars.active[s], n = (a + dir + ARS_LISTS) % ARS_LISTS;
    if (!g_ars.used[s][n]) {                                   // a new list: 16 empty arsenals (no name: the game counts 0)
        memset(g_ars.rec[s][n], 0, ARS_N * ARS_REC);
        for (int i = 0; i < ARS_N; i++) memset(g_ars.rec[s][n] + i * ARS_REC + 0x14, 0xFF, 30 * 2);
        g_ars.used[s][n] = 1;
    }
    bool empty = true;                                         // v5.62 a list with no arsenal: confirming it left the screen stuck
    for (int i = 0; i < ARS_N; i++) empty &= g_ars.rec[s][n][i * ARS_REC + 4] == 0;
    if (empty) {
        const unsigned char *from = g_ars.rec[s][a];
        int f = 0;
        while (f < ARS_N && !from[f * ARS_REC + 4]) f++;
        if (f < ARS_N) memcpy(g_ars.rec[s][n], from + f * ARS_REC, ARS_REC);
        else { memset(g_ars.rec[s][n] + 0x14, 0xFF, 30 * 2); }
        char nm[17]; sprintf_s(nm, "LIST %d ARSENAL", n + 1);
        memset(g_ars.rec[s][n] + 4, 0, 16); memcpy(g_ars.rec[s][n] + 4, nm, strlen(nm));
        logf("arsenal lists: list %d was empty - it starts with a copy of arsenal %d of list %d", n + 1, f + 1, a + 1);
    }
    memcpy(r, g_ars.rec[s][n], ARS_N * ARS_REC);
    g_ars.active[s] = n;
    PFN_ARS_COUNT count = g_ars_test_count ? g_ars_test_count : (PFN_ARS_COUNT)(g_base + RVA_ARS_COUNT_FN);
    count(prof, 0);                                            // native 5.115: count, flags, stock
    *(int *)(sel + 0x18) = *(int *)(prof + 0x3a48);            // the list window's set-up 0x140248ea0
    *(ULONGLONG *)(sel + 0) = 0; *(ULONGLONG *)(sel + 8) = 0; *(ULONGLONG *)(sel + 0x10) = 0;
    *(ULONGLONG *)(sel + 0x28) = 1;
    *(int *)(sel + 0x30) = 0;
    ars_save();
    ars_titles();                                              // v5.59 the title shows the number (no banner)
    logf("arsenal lists: set %d list %d -> %d (%d arsenals)", s, a + 1, n + 1, *(int *)(prof + 0x3a48));
    return true;
}
// v5.64 Add Arsenal (ARSENAL OPTIONS, tools/build_ars_options.py; owner): native 2.8 op 94 before the game's own Add Arsenal - a list that holds 16 is put away and the next
// list with room is laid in the profile (the game then adds the new arsenal there)
static int ars_named(const unsigned char *recs) { int n = 0; for (int i = 0; i < ARS_N; i++) n += recs[i * ARS_REC + 4] != 0; return n; }
static INT64 ars_room_native() {
    __try {
        ULONG_PTR prof = ars_prof(), ui = ars_ui();
        if (!prof || !ui || *(int *)(ui + 0xb50) != 0) return 0;
        unsigned char *r = ars_records(prof, 0);
        if (!r || ars_named(r) < ARS_N) return 0;                // room in this list
        if (!g_ars_loaded) ars_load();
        ars_sync(prof, true);
        int s = ars_side(prof), a = g_ars.active[s];
        for (int d = 1; d < ARS_LISTS; d++) {
            int n = (a + d) % ARS_LISTS;
            if (!g_ars.used[s][n]) {
                memset(g_ars.rec[s][n], 0, ARS_N * ARS_REC);
                for (int i = 0; i < ARS_N; i++) memset(g_ars.rec[s][n] + i * ARS_REC + 0x14, 0xFF, 30 * 2);
                g_ars.used[s][n] = 1;
            }
            if (ars_named(g_ars.rec[s][n]) >= ARS_N) continue;
            memcpy(r, g_ars.rec[s][n], ARS_N * ARS_REC);
            g_ars.active[s] = n;
            PFN_ARS_COUNT count = g_ars_test_count ? g_ars_test_count : (PFN_ARS_COUNT)(g_base + RVA_ARS_COUNT_FN);
            count(prof, 0);
            ars_save();
            ars_titles();
            char msg[64]; sprintf_s(msg, "Arsenal list %d is full: the new arsenal goes in list %d", a + 1, n + 1); set_status(msg);
            logf("arsenal lists: list %d full - Add Arsenal goes to list %d", a + 1, n + 1);
            return 1;
        }
        set_status("All 3 arsenal lists are full (16 each)");
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
// v5.128 auto-sort (owner 2026-11-09). Once per set (campaign / multiplayer), the first
// time an arsenal list shows: every named arsenal of Lists 1-2 that holds a skill numbered 375 or above (the OG set is
// 0-374) moves into a free slot of List 3; Lists 1-2 close up (the game counts named records from the first); a list in use
// left with no arsenal hands over to List 3 (an empty list left the screen stuck, v5.62). CwoArsenalLists.bin is copied to
// CwoArsenalLists.before_sort.bin first (cleanup preserves files). BFR edition: automatic. MODS (the owner's own arsenals):
// only while CwoArsSortNow.txt exists in the loader folder. Marker: CwoArsSorted.txt ("0" / "1" = the sets done).
static const int ARS_MOD_FIRST = 375;
static bool ars_has_mod(const unsigned char *rec) {
    for (int k = 0; k < 30; k++) { short v = *(const short *)(rec + 0x14 + 2 * k); if (v >= ARS_MOD_FIRST) return true; }
    return false;
}
static bool ars_sort_done(int s) {
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoArsSorted.txt", g_dir);
    FILE *f = NULL; char t[16] = "";
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(t, sizeof(t), f)) t[0] = 0; fclose(f); }
    return strchr(t, '0' + s) != NULL;
}
static void ars_sort_mark(int s) {
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoArsSorted.txt", g_dir);
    char t[16] = ""; FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(t, sizeof(t), f)) t[0] = 0; fclose(f); }
    if (!strchr(t, '0' + s)) { size_t n = strlen(t); if (n + 1 < sizeof(t)) { t[n] = (char)('0' + s); t[n + 1] = 0; } }
    if (cwo_wfopen(&f, p, L"w") == 0 && f) { fputs(t, f); fclose(f); }
}
static bool ars_sort_wanted(int s) {
    if (ars_sort_done(s)) return false;
    if (g_bfr_edition) return true;
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoArsSortNow.txt", g_dir);
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}
static void ars_compact(unsigned char *recs) {                 // named records first, order kept
    unsigned char tmp[ARS_N * ARS_REC];
    int n = 0;
    for (int i = 0; i < ARS_N; i++) if (recs[i * ARS_REC + 4]) memcpy(tmp + (n++) * ARS_REC, recs + i * ARS_REC, ARS_REC);
    for (int i = n; i < ARS_N; i++) { memset(tmp + i * ARS_REC, 0, ARS_REC); memset(tmp + i * ARS_REC + 0x14, 0xFF, 30 * 2); }
    memcpy(recs, tmp, sizeof(tmp));
}
static int ars_sort(ULONG_PTR prof) {                          // game thread, an arsenal list on screen, player 1; returns moved
    int s = ars_side(prof);
    unsigned char *r = ars_records(prof, 0);
    if (!r || !ars_sort_wanted(s)) return 0;
    ars_sync(prof, true);                                      // the list in use is current in g_ars
    {   // backup once
        wchar_t p[MAX_PATH], b[MAX_PATH]; ars_path(p); swprintf_s(b, MAX_PATH, L"%sCwoArsenalLists.before_sort.bin", g_dir);
        if (GetFileAttributesW(b) == INVALID_FILE_ATTRIBUTES) CopyFileW(p, b, TRUE);
    }
    unsigned char *l3 = g_ars.rec[s][2];
    if (!g_ars.used[s][2]) { memset(l3, 0, ARS_N * ARS_REC); for (int i = 0; i < ARS_N; i++) memset(l3 + i * ARS_REC + 0x14, 0xFF, 30 * 2); g_ars.used[s][2] = 1; }
    ars_compact(l3);
    int moved = 0, left = 0;
    for (int k = 0; k < 2; k++) {
        if (!g_ars.used[s][k]) continue;
        unsigned char *src = g_ars.rec[s][k];
        for (int i = 0; i < ARS_N; i++) {
            unsigned char *rec = src + i * ARS_REC;
            if (!rec[4] || !ars_has_mod(rec)) continue;
            int free = ars_named(l3);
            if (free >= ARS_N) { left++; continue; }
            memcpy(l3 + free * ARS_REC, rec, ARS_REC);
            char nm[17]; memcpy(nm, rec + 4, 16); nm[16] = 0;
            for (int c = 0; nm[c]; c++) if ((unsigned char)nm[c] < 32 || (unsigned char)nm[c] > 126) nm[c] = '?';
            logf("arsenal auto-sort: set %d list %d arsenal %d '%s' (modded skills) -> list 3 slot %d", s, k + 1, i + 1, nm, free + 1);
            memset(rec, 0, ARS_REC); memset(rec + 0x14, 0xFF, 30 * 2);
            moved++;
        }
        ars_compact(src);
    }
    int a = g_ars.active[s];
    if (!ars_named(g_ars.rec[s][a])) g_ars.active[s] = a = 2;   // never leave the list in use empty
    if (moved) {
        memcpy(r, g_ars.rec[s][a], ARS_N * ARS_REC);
        PFN_ARS_COUNT count = g_ars_test_count ? g_ars_test_count : (PFN_ARS_COUNT)(g_base + RVA_ARS_COUNT_FN);
        count(prof, 0);
        ULONG_PTR sel = ars_sel();
        if (sel) {
            *(int *)(sel + 0x18) = *(int *)(prof + 0x3a48);
            *(ULONGLONG *)(sel + 0) = 0; *(ULONGLONG *)(sel + 8) = 0; *(ULONGLONG *)(sel + 0x10) = 0;
            *(ULONGLONG *)(sel + 0x28) = 1; *(int *)(sel + 0x30) = 0;
        }
        ars_titles();
        char msg[96];
        sprintf_s(msg, "%d arsenal%s with modded skills moved to Arsenal List 3%s", moved, moved == 1 ? "" : "s", left ? " (List 3 is full: some stayed)" : "");
        set_status(msg);
    }
    ars_save();
    ars_sort_mark(s);
    logf("arsenal auto-sort: set %d done - %d moved, %d left (List 3 full), list in use %d", s, moved, left, a + 1);
    return moved;
}
static void ars_step() {                                       // game thread, every flush (menus too)
    if (g_in_match) { InterlockedExchange(&g_ars_req, 0); return; }
    __try {
        if (!g_ars_loaded) ars_load();
        ULONG_PTR win = ars_list_window(), prof = ars_prof(), ui = ars_ui();
        LONG req = InterlockedExchange(&g_ars_req, 0);
        if (!win || !prof || !ui) return;
        if (req) { ars_switch(req); return; }
        if (*(int *)(ui + 0xb50) != 0) return;                 // player 2's copy: no lists
        {   // v5.128 the one-time auto-sort of modded arsenals into List 3
            static bool tried[2];
            int s = ars_side(prof);
            if (!tried[s]) { tried[s] = true; ars_sort(prof); }
        }
        ULONGLONG now = GetTickCount64();
        if (now - g_ars_sync_t > 1000) {
            g_ars_sync_t = now;
            ars_sync(prof, false);
            ars_titles();                                          // v5.59 the list window's title (no floating banner)
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static bool ars_list_focused() {                               // controller thread (read only)
    if (g_in_match) return false;
    __try { return ars_list_window() != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- the Xbox Live lobby's GAME RULE list (tools/build_arsenal_menus.py): native 2.8 ops 35-38. The host's rule
// DAT_140518cf0 and game type DAT_14050d2d8; FUN_140183010 (native 4.578, the Create Game rule screen) steps the rule and
// skips the ones the game type does not allow (rule 1 while DAT_14051c938 is set; type 0: 11; type 1: 10 and 1; type 2:
// 10 and 11); the network tick sends it to the others (FUN_1401841b0).
static const ULONG_PTR RVA_LIVE_RULE = 0x518cf0, RVA_LIVE_TYPE = 0x50d2d8, RVA_LIVE_NOANTE = 0x51c938, RVA_LIVE_STEP = 0x183010;
typedef void(__fastcall *PFN_LIVE_STEP)(ULONG_PTR, int, int, int, int);
static int *g_live_test;                                        // self-test: rule, type, no-ante words
static bool live_rule_ok(int r) {
    int type = g_live_test ? g_live_test[1] : *(int *)(g_base + RVA_LIVE_TYPE);
    int noante = g_live_test ? g_live_test[2] : *(int *)(g_base + RVA_LIVE_NOANTE);
    if (r < 0 || r > 11 || (noante && r == 1)) return false;
    if (type == 0) return r != 11;
    if (type == 1) return r != 10 && r != 1;
    if (type == 2) return r < 10;
    return true;
}
// v5.16 (owner 2026-10-26). The
// step above changes the host's rule DAT_140518cf0, and the online tick already copies it into the session properties
// (FUN_140181210 -> FUN_140182b60 / FUN_140283220). The match itself and SESSION INFO (4.592 = *0x1405277fc) use the
// match copy, which the game fills only when the session is made (FUN_140182f80 -> FUN_140187370: 0x1405277b4 = type,
// 0x1405277fc = rule), and the others get it in the host's player info (FUN_1401910a0 sends 0x1405277fc for member 0;
// clients take it in FUN_140191a60). So for the host (local member *0x140527bc4 == 0): the same copy, then the game's
// own resend of this machine's player info to every member (FUN_140191210, as the online tick does). Ghidra pass 065.
static const ULONG_PTR RVA_LIVE_MATCH_RULE = 0x5277fc, RVA_LIVE_ME = 0x527bc4, RVA_LIVE_SEND_INFO = 0x191210;
static int *g_live_pub_test;                                    // self-test: me, match copy, sends
static void live_rule_publish(int rule) {                       // game thread, after the host's rule step
    if (g_live_test && !g_live_pub_test) return;                // a self-test on stand-ins never touches the game
    if (g_live_pub_test) {
        if (g_live_pub_test[0] != 0) return;
        g_live_pub_test[1] = rule; g_live_pub_test[2]++;
        return;
    }
    __try {
        if (*(int *)(g_base + RVA_LIVE_ME) != 0) return;                              // only the host sends the rule
        int was = *(int *)(g_base + RVA_LIVE_MATCH_RULE);
        *(int *)(g_base + RVA_LIVE_MATCH_RULE) = rule;
        ((void(__fastcall *)())(g_base + RVA_LIVE_SEND_INFO))();
        logf("Xbox Live lobby: match rule %d -> %d, sent to the other players", was, rule);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("Xbox Live lobby: match rule update failed");
    }
}
static INT64 ars_native(int op, INT64 *args) {                 // game thread (the menu scripts)
    INT64 a1 = 0;
    __try { a1 = args[1]; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    int *rule = g_live_test ? &g_live_test[0] : (int *)(g_base + RVA_LIVE_RULE);
    switch (op) {
    case 35: return live_rule_ok((int)a1) ? (INT64)(ULONG_PTR)ED_RULES[a1] : 0;   // the row's name, or 0
    case 36: return *rule;
    case 37: {
        int row = 0;
        if (!g_live_test && g_bn_rule_host > 0) { for (int r = 0; r < 12; r++) row += live_rule_ok(r); return row + g_bn_rule_host - 1; }   // v5.57 a lobby mode: its row after the rules
        for (int r = 0; r < *rule && r < 12; r++) row += live_rule_ok(r);
        return row;
    }
    case 38: {                                                  // the rule chosen: stepped there by the game's own function
        if (!live_rule_ok((int)a1)) return 0;
        if (!g_live_test && !g_bn_setting) g_bn_rule_host = 0;   // v5.57 a game rule chosen: no lobby mode (v5.62: not the loader's own reset)
        int from = *rule;
        for (int i = 0; i < 13 && *rule != (int)a1; i++) {
            if (g_live_test) { do *rule = (*rule + 1) % 12; while (!live_rule_ok(*rule)); }
            else ((PFN_LIVE_STEP)(g_base + RVA_LIVE_STEP))(0, 0, 1, 0, 0);
        }
        logf("Xbox Live lobby: game rule %d -> %d (%s)", from, *rule, *rule >= 0 && *rule < 12 ? ED_RULES[*rule] : "?");
        if (*rule == (int)a1) live_rule_publish(*rule);
        return *rule == (int)a1;
    }
    }
    return 0;
}

static int ars_self_test() {                                    // the list switch and the rule list on stand-ins
    static unsigned char prof[0x4000], menu[0x40 + 16 * 0x103c + 0x400], ui[0xc00], sel[0x40], recs[ARS_N * ARS_REC];
    memset(prof, 0, sizeof(prof)); memset(menu, 0, sizeof(menu)); memset(ui, 0, sizeof(ui)); memset(sel, 0, sizeof(sel));
    memset(recs, 0, sizeof(recs));
    *(ULONG_PTR *)(prof + 0x33a0) = (ULONG_PTR)prof;           // set 0 (campaign) in use
    *(unsigned char **)(prof + 0x3a30) = recs;
    strcpy_s((char *)recs + 4, 16, "Mine");                    // list 1: one arsenal
    *(int *)(prof + 0x3a48) = 1;
    *(int *)(menu + 0x10410) = 2;                              // window 2 focused, an arsenal list, titled
    *(short *)(menu + 0x40 + 2 * 0x103c + 8) = 16;
    static char title[] = "ARSENAL LIST";
    *(char **)(menu + 0x40 + 2 * 0x103c + 0x1024) = title;
    struct C { static void __fastcall count(ULONG_PTR pr, int) {     // the game's count: records with a name
        int n = 0; unsigned char *r = *(unsigned char **)(pr + 0x3a30);
        while (n < ARS_N && r[n * ARS_REC + 4]) n++;
        *(int *)(pr + 0x3a48) = n; } };
    ArsFile keep = g_ars; bool keep_l = g_ars_loaded, keep_s0 = g_ars_synced[0], keep_s1 = g_ars_synced[1];
    wchar_t keep_dir[MAX_PATH]; wcscpy_s(keep_dir, g_dir);
    wcscat_s(g_dir, L"ars_test_");                             // the test's own file
    memset(&g_ars, 0, sizeof(g_ars)); memcpy(g_ars.magic, "CWOARSL1", 8); g_ars_loaded = true; g_ars_synced[0] = g_ars_synced[1] = false;
    g_ars_test_menu = (ULONG_PTR)menu; g_ars_test_prof = (ULONG_PTR)prof; g_ars_test_ui = (ULONG_PTR)ui; g_ars_test_sel = (ULONG_PTR)sel;
    g_ars_test_count = C::count;
    *(int *)(sel + 0x18) = 7; *(ULONGLONG *)sel = 5;
    bool right = ars_switch(1);                                // -> list 2: new - v5.62 it starts with a copy of "Mine"
    bool l2 = right && g_ars.active[0] == 1 && *(int *)(prof + 0x3a48) == 1 && !strcmp((char *)recs + 4, "LIST 2 ARSENAL") && *(int *)(sel + 0x18) == 1 && *(ULONGLONG *)sel == 0 &&
              *(char **)(menu + 0x40 + 2 * 0x103c + 0x1024) == title && !strcmp(g_ars_title[0], "ARSENAL LIST 2") &&   // v5.59:
              recs[ARS_REC + 4] == 0;                            // one arsenal (the copy), the second slot empty
    strcpy_s((char *)recs + 4, 16, "Two"); strcpy_s((char *)recs + ARS_REC + 4, 16, "Three");   // build two in list 2
    bool left = ars_switch(-1);                                // back to list 1: "Mine" again
    bool l1 = left && g_ars.active[0] == 0 && *(int *)(prof + 0x3a48) == 1 && !strcmp((char *)recs + 4, "Mine") &&
              *(char **)(menu + 0x40 + 2 * 0x103c + 0x1024) == title && !strcmp(g_ars_title[0], "ARSENAL LIST 1");
    ars_switch(-1);                                            // round the other way: list 3 (new)
    bool l3 = g_ars.active[0] == 2 && *(int *)(prof + 0x3a48) == 1 && !strcmp((char *)recs + 4, "LIST 3 ARSENAL");
    ars_switch(-1);                                            // list 2 kept its two arsenals
    bool kept = g_ars.active[0] == 1 && *(int *)(prof + 0x3a48) == 2 && !strcmp((char *)recs + ARS_REC + 4, "Three");
    *(int *)(ui + 0xb50) = 1;                                  // player 2: refused
    bool p2 = !ars_switch(1) && g_ars.active[0] == 1;
    *(int *)(ui + 0xb50) = 0;
    *(short *)(menu + 0x40 + 2 * 0x103c + 8) = 12;             // not an arsenal list: refused
    bool other = !ars_switch(1);
    // a new run: the records of list 2 are in the game; it is found as the list in use
    g_ars_synced[0] = false; g_ars.active[0] = 0;
    *(short *)(menu + 0x40 + 2 * 0x103c + 8) = 16;
    ars_sync((ULONG_PTR)prof, false);
    bool found = g_ars.active[0] == 1;
    // v5.128 auto-sort: in use = list 2 ("Two", "Three"); "Three" gets a modded skill (400) -> list 3 after "LIST 3 ARSENAL"
    *(short *)(recs + ARS_REC + 0x14 + 2 * 5) = 400;
    memcpy(g_ars.rec[0][1], recs, ARS_N * ARS_REC);
    wchar_t req[MAX_PATH], mark[MAX_PATH], bak[MAX_PATH];
    swprintf_s(req, MAX_PATH, L"%sCwoArsSortNow.txt", g_dir); swprintf_s(mark, MAX_PATH, L"%sCwoArsSorted.txt", g_dir);
    swprintf_s(bak, MAX_PATH, L"%sCwoArsenalLists.before_sort.bin", g_dir);
    DeleteFileW(mark); DeleteFileW(bak);
    bool keep_bfr = g_bfr_edition; g_bfr_edition = false;
    int none = ars_sort((ULONG_PTR)prof);                      // MODS without the request file: nothing
    FILE *rf = NULL; if (cwo_wfopen(&rf, req, L"w") == 0 && rf) fclose(rf);
    int moved = ars_sort((ULONG_PTR)prof);
    bool sorted = none == 0 && moved == 1 && g_ars.active[0] == 1 && *(int *)(prof + 0x3a48) == 1 && !strcmp((char *)recs + 4, "Two") &&
                  !strcmp((char *)g_ars.rec[0][2] + 4, "LIST 3 ARSENAL") && !strcmp((char *)g_ars.rec[0][2] + ARS_REC + 4, "Three") &&
                  *(short *)(g_ars.rec[0][2] + ARS_REC + 0x14 + 10) == 400 && ars_sort_done(0) && !ars_sort((ULONG_PTR)prof);
    g_bfr_edition = keep_bfr;
    DeleteFileW(req); DeleteFileW(mark); DeleteFileW(bak);
    logf("self-test: arsenal auto-sort %s", sorted ? "ok" : "FAILED");
    found = found && sorted;
    wchar_t p[MAX_PATH]; ars_path(p); DeleteFileW(p);
    wcscpy_s(g_dir, keep_dir);
    g_ars = keep; g_ars_loaded = keep_l; g_ars_synced[0] = keep_s0; g_ars_synced[1] = keep_s1;
    g_ars_test_menu = g_ars_test_prof = g_ars_test_ui = g_ars_test_sel = 0; g_ars_test_count = NULL;
    // the Xbox Live rule list: type 1 (no KO Battle, no Ante), rule 0 -> 'Heavy' (4) -> its row
    int live[3] = {0, 1, 0};
    g_live_test = live;
    INT64 a[3] = {35, 10, 0};
    bool names = ars_native(35, a) == 0 && (a[1] = 1, ars_native(35, a) == 0) && (a[1] = 4, ars_native(35, a) == (INT64)(ULONG_PTR)ED_RULES[4]);
    a[1] = 4;
    bool set = ars_native(38, a) == 1 && live[0] == 4;
    INT64 row = ars_native(37, a);                             // rules 0, 2, 3 before it: row 3
    a[1] = 10;
    bool refuse = ars_native(38, a) == 0 && live[0] == 4;
    int pub[3] = {0, 0, 0};                                    // v5.16: the host's match copy + resend; a client: nothing
    g_live_pub_test = pub;
    a[1] = 5;
    bool host = ars_native(38, a) == 1 && pub[1] == 5 && pub[2] == 1;
    pub[0] = 1; a[1] = 6;
    bool client = ars_native(38, a) == 1 && pub[1] == 5 && pub[2] == 1;
    g_live_pub_test = NULL;
    g_live_test = NULL;
    bool rules = names && set && row == 3 && refuse && host && client;
    logf("self-test: arsenal lists %s%s%s%s%s%s%s, live rule list %s", l2 ? "2" : "x", l1 ? "1" : "x", l3 ? "3" : "x", kept ? "k" : "x",
         p2 ? "p" : "x", other ? "o" : "x", found ? "f" : "x", rules ? "ok" : "FAILED");
    return l2 && l1 && l3 && kept && p2 && other && found && rules ? 1 : -35;
}

// ---- v5.152 skills you made stay offline (owner 2026-11-12 + Single Play). Player-made = a Skill Creator skill (CwoNewSkills, not one of the
// pack's own named skills below) or a Skill Fuser fusion. In an Xbox Live / system link session the Xbox Live arsenal list
// (livearsel, tools/build_live_madeskills.py) asks op 118 before it takes an arsenal: 1 = it holds such a skill -> refused with a
// message, the list stays. Offline (story, split screen, Single Play) nothing changes. Saved arsenals are never edited.
static bool fu_uses(int id);                                      // cwo_fusion.inl
static bool ns_player_made(int id) {
    if (id < 0) return false;
    if (fu_uses(id)) return true;
    for (int k = 0; k < g_ns_n; k++) {
        if (g_ns[k].id != id) continue;
        return !g_ns[k].comm && !ns_is_pack(g_ns[k]);           // v5.186 the pack's own (name AND number) and community skills
    }
    return false;
}
static int ars_made_count(const unsigned char *rec) {
    int n = 0;
    for (int k = 0; k < 30; k++) { short s = *(const short *)(rec + 0x14 + 2 * k); if (s != -1 && ns_player_made(s)) n++; }
    return n;
}
static INT64 ars_made_native(INT64 *args) {                       // native 2.8 op 118 (args[1] = the highlighted arsenal's row)
    int idx = -1;
    __try { idx = (int)args[1]; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!ed_online() || idx < 0 || idx >= ARS_N) return 0;
    int n = 0;
    __try {
        ULONG_PTR prof = ars_prof();
        unsigned char *recs = prof ? ars_records(prof, 0) : NULL;
        if (recs) n = ars_made_count(recs + idx * ARS_REC);
    } __except (EXCEPTION_EXECUTE_HANDLER) { n = 0; }
    if (!n) return 0;
    set_status("This arsenal has skills you made - those are for offline play only (story, split screen, Single Play)");
    logf("Xbox Live arsenal list: arsenal %d refused - %d skill(s) made in the Skill Creator / Skill Fuser", idx + 1, n);
    return 1;
}
static int ars_made_self_test() {                              // v5.186.0: the pack's own = name AND number; community skills
    unsigned char rec[ARS_REC];
    memset(rec, 0xFF, sizeof(rec));
    int keep_n = g_ns_n;
    if (g_ns_n > NS_MAX - 4) return 1;                              // no room for the stand-ins: nothing to test
    NewSkill keep[4];
    for (int i = 0; i < 4; i++) keep[i] = g_ns[g_ns_n + i];
    NewSkill a, b, c, d;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b)); memset(&c, 0, sizeof(c)); memset(&d, 0, sizeof(d));
    a.id = 9990; strcpy_s(a.name, "My Fire");                       // made by the player
    b.id = 472; strcpy_s(b.name, "Hologram");                       // the pack's own (name and number)
    c.id = 9992; strcpy_s(c.name, "Hologram");                      // a player's copy under the pack's name: still made
    d.id = 9993; strcpy_s(d.name, "Shared"); d.comm = 1;            // a community skill: the pack's
    g_ns[g_ns_n] = a; g_ns[g_ns_n + 1] = b; g_ns[g_ns_n + 2] = c; g_ns[g_ns_n + 3] = d; g_ns_n += 4;
    bool pack_ok = true;
    for (int k = 0; k < keep_n; k++) if (g_ns[k].id == 472 && strcmp(g_ns[k].name, "Hologram")) pack_ok = false;   // 472 taken by another: skip
    *(short *)(rec + 0x14) = 472;
    bool p0 = !pack_ok || ars_made_count(rec) == 0;
    *(short *)(rec + 0x14 + 2) = 9990;
    *(short *)(rec + 0x14 + 4) = 9993;
    bool p1 = ars_made_count(rec) == (pack_ok ? 1 : 2);
    *(short *)(rec + 0x14 + 6) = 9992;
    bool p2 = ars_made_count(rec) == (pack_ok ? 2 : 3) && !ns_player_made(-1) && !ns_player_made(5);
    g_ns_n = keep_n;
    for (int i = 0; i < 4; i++) g_ns[keep_n + i] = keep[i];
    bool ok = p0 && p1 && p2;
    logf("self-test: player-made skills offline %s (pack %s, community %s, copy under a pack name %s)", ok ? "ok" : "FAILED",
         p0 ? "ok" : "FAILED", p1 ? "ok" : "FAILED", p2 ? "ok" : "FAILED");
    return ok ? 1 : -71;
}
