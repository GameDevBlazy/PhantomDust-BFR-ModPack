// ---- v5.190.0 CAMPAIGN RULES (owner 2026-11-21, answering the campaign findings "1 2 and 3": 1 change the campaign order /
// add our own missions to it, 2 open any quest or area state on a save, 3 our own campaign chain with Create a Quest).
// MODS edition, offline only. File: CwoCampaign.txt in the loader folder, one command per line ('#' = note):
//   after A [B C D] open Q   standing rule: when quests A (B, C, D) are all cleared, quest Q is put on the quest list
//   lock Q until A [B C D]   standing rule: quest Q stays off the list until A (B ...) are all cleared, then it is on it
//   open Q                   once: quest Q onto the list now
//   phase P                  once: the whole hub's story phase = P (0-9; the area scripts st09_areaAA_PP.ssb follow it)
//   chapter C                once: the story chapter (profile short 9) = C
// One-shot lines run once per line text and are then written to CwoCampaignDone.txt (delete a line there to run it again).
// Game facts (Ghidra full pass 065 + the natives' code, MODLOG 2026-11-21; 16_Reports/CAMPAIGN_STUDY section 8;
// 07_Combat_and_Skills/CAMPAIGN_QUEST_SPEC_BATCH023-025.md):
//  - quest table 0x14052dff0: 256 x 8 bytes = 4 shorts per quest (natives 7.35 / 7.36 / 7.37: 0x140218680 / 6c0 / 710):
//    short 0 = bits (0 on the list, 1 shown but closed, 2 cleared), short 1 = clears;
//  - the hub's area scripts list (mode 0) every quest with bit 0 set and bit 1 clear; quests 24 and 26 are never listed
//    and keep their entries elsewhere - left alone here;
//  - profile shorts 0x1404615c2 + 2 i (7.32 / 7.33; i 0..12): short 7 (0x1404615d0) = the quest just played, short 9
//    (0x1404615d4) = the story chapter (the shop reads it, CAMPAIGN_STUDY section 7);
//  - hub phase: byte 0x14059aa54 + stage x 16 + area; FUN_1401b1a10 (hub_phase_set) writes one value to all 16 bytes of
//    stage 9 (0x14059aae4) and to the 0x200 bytes after them (0x14059aaf4) - 'phase' does the same (v5.190.1);
//    FUN_1401b9ec0 picks st09_areaAA_PP.ssb from it when an area loads;
//  - the game saves this memory at its own save points, so a change lasts once the game has saved.
// Applied only with a loaded profile (the quest table not all zero), never during a battle (current stage 9 = the hub, or
// no match), not online, MODS only. A change shows the next time an area (its quest list) loads.
static const ULONG_PTR CP_QUESTS = 0x52dff0, CP_PROFILE = 0x4615c2, CP_PHASES = 0x59aa54;
struct CpRule { int kind; int q; int need[4]; int nneed; char text[96]; };   // kind 1 after-open, 2 lock, 3 open, 4 phase, 5 chapter
static CpRule g_cp[96];
static int g_cp_n = -1;
static FILETIME g_cp_ft;
static ULONGLONG g_cp_t;

static short *cp_q(int q) { return (short *)(g_base + CP_QUESTS + (ULONG_PTR)q * 8); }
static bool cp_cleared(int q) { short *e = cp_q(q); return (e[0] & 4) || e[1] > 0; }

static void cp_load(const wchar_t *test_path = NULL) {          // read CwoCampaign.txt when it changed
    wchar_t p[MAX_PATH];
    if (test_path) wcscpy_s(p, test_path); else swprintf_s(p, MAX_PATH, L"%sCwoCampaign.txt", g_dir);
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a)) { if (g_cp_n != 0) { g_cp_n = 0; } return; }
    if (!test_path && g_cp_n >= 0 && !CompareFileTime(&a.ftLastWriteTime, &g_cp_ft)) return;
    g_cp_ft = a.ftLastWriteTime;
    g_cp_n = 0;
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") || !f) return;
    char line[160];
    while (fgets(line, sizeof(line), f) && g_cp_n < (int)(sizeof(g_cp) / sizeof(*g_cp))) {
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *e = line + strlen(line); while (e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (!line[0]) continue;
        CpRule r = {}; char w1[16] = "", w2[16] = ""; int v[6] = {-1, -1, -1, -1, -1, -1};
        strncpy_s(r.text, line, _TRUNCATE);
        bool ok = false;
        if (sscanf_s(line, "%15s", w1, (unsigned)sizeof(w1)) == 1) {
            if (!_stricmp(w1, "after")) {                         // after A [B C D] open Q
                char *o = strstr(line, " open ");
                if (o) {
                    *o = 0;
                    int n = sscanf_s(line + 5, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]);
                    if (n >= 1 && sscanf_s(o + 6, "%d", &r.q) == 1) { r.kind = 1; r.nneed = n; for (int k = 0; k < n; k++) r.need[k] = v[k]; ok = true; }
                }
            } else if (!_stricmp(w1, "lock")) {                   // lock Q until A [B C D]
                char *u = strstr(line, " until ");
                if (u) {
                    *u = 0;
                    int n = sscanf_s(u + 7, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]);
                    if (n >= 1 && sscanf_s(line + 4, "%d", &r.q) == 1) { r.kind = 2; r.nneed = n; for (int k = 0; k < n; k++) r.need[k] = v[k]; ok = true; }
                }
            } else if (!_stricmp(w1, "open") && sscanf_s(line, "%15s %d", w2, (unsigned)sizeof(w2), &r.q) == 2) { r.kind = 3; ok = true; }
            else if (!_stricmp(w1, "phase") && sscanf_s(line, "%15s %d", w2, (unsigned)sizeof(w2), &r.q) == 2 && r.q >= 0 && r.q <= 9) { r.kind = 4; ok = true; }
            else if (!_stricmp(w1, "chapter") && sscanf_s(line, "%15s %d", w2, (unsigned)sizeof(w2), &r.q) == 2 && r.q >= 0 && r.q <= 30) { r.kind = 5; ok = true; }
        }
        if (ok && r.kind <= 3) {                                 // quest numbers 1..255 (24 / 26 refused: kept elsewhere by the game)
            bool bad = r.q < 1 || r.q > 255 || r.q == 24 || r.q == 26;
            for (int k = 0; k < r.nneed; k++) if (r.need[k] < 1 || r.need[k] > 255) bad = true;
            if (bad) ok = false;
        }
        if (ok) g_cp[g_cp_n++] = r;
        else logf("campaign: line not understood (skipped): %s", r.text);
    }
    fclose(f);
    logf("campaign: %d rule(s) from CwoCampaign.txt", g_cp_n);
}

static bool cp_done(const char *text, bool mark) {              // one-shot lines already run (CwoCampaignDone.txt)
    wchar_t p[MAX_PATH]; FILE *f = NULL;
    swprintf_s(p, MAX_PATH, L"%sCwoCampaignDone.txt", g_dir);
    if (mark) {
        if (!cwo_wfopen(&f, p, L"a") && f) { fprintf(f, "%s\n", text); fclose(f); }
        return true;
    }
    bool found = false;
    if (!cwo_wfopen(&f, p, L"r") && f) {
        char line[160];
        while (!found && fgets(line, sizeof(line), f)) {
            char *e = line + strlen(line); while (e > line && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
            if (!strcmp(line, text)) found = true;
        }
        fclose(f);
    }
    return found;
}

static void cp_step() {                                          // game thread, every drawn frame
    ULONGLONG now = GetTickCount64();
    if (now - g_cp_t < 2000) return;
    g_cp_t = now;
    if (g_bfr_edition || ed_online()) return;
    cp_load();
    if (g_cp_n <= 0) return;
    __try {
        int stage = *(int *)(g_base + RVA_STAGE);
        if (g_in_match && stage != 9) return;                     // never during a battle (the hub is stage 9)
        bool loaded = false;                                      // a loaded profile: the quest table is not all zero
        for (int q = 1; q < 256 && !loaded; q++) if (cp_q(q)[0] || cp_q(q)[1]) loaded = true;
        if (!loaded) return;
        for (int i = 0; i < g_cp_n; i++) {
            CpRule &r = g_cp[i];
            bool all = true;
            for (int k = 0; k < r.nneed; k++) if (!cp_cleared(r.need[k])) all = false;
            short *e = r.kind <= 3 ? cp_q(r.q) : NULL;
            if (r.kind == 1) {                                    // after ... open Q
                if (all && !cp_cleared(r.q) && ((e[0] & 1) == 0 || (e[0] & 2))) {
                    e[0] = (short)((e[0] | 1) & ~2);
                    logf("campaign: '%s' -> quest %d on the list", r.text, r.q);
                }
            } else if (r.kind == 2) {                             // lock Q until ...: off the list until then, on it after
                if (!all && (e[0] & 1) && !cp_cleared(r.q)) {
                    e[0] = (short)(e[0] & ~1);
                    logf("campaign: '%s' -> quest %d kept off the list", r.text, r.q);
                } else if (all && !cp_cleared(r.q) && ((e[0] & 1) == 0 || (e[0] & 2))) {
                    e[0] = (short)((e[0] | 1) & ~2);
                    logf("campaign: '%s' -> quest %d on the list now", r.text, r.q);
                }
            } else if (!cp_done(r.text, false)) {                 // one-shot
                if (r.kind == 3) { e[0] = (short)((e[0] | 1) & ~2); }
                else if (r.kind == 4) {                           // as the game's FUN_1401b1a10: the 16 stage-9 bytes 0x14059aae4
                    unsigned char *ph = (unsigned char *)(g_base + CP_PHASES + 9 * 16);   // and the 0x200 bytes after them
                    for (int a = 0; a < 16 + 0x200; a++) ph[a] = (unsigned char)r.q;      // (0x14059aaf4) take the phase
                }
                else if (r.kind == 5) { *(short *)(g_base + CP_PROFILE + 9 * 2) = (short)r.q; }
                cp_done(r.text, true);
                logf("campaign: '%s' done (once; recorded in CwoCampaignDone.txt)", r.text);
                set_status("Campaign: change made - it shows when an area loads");
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("campaign: the save data could not be read - skipped"); g_cp_t = now + 30000; }
}

static int cp_self_test() {                                     // the rule reader on a sample file (no game memory touched)
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%scwo_campaign_selftest.txt", g_dir);
    FILE *f = NULL;
    if (_wfopen_s(&f, p, L"w") || !f) return -301;
    fputs("# test\nafter 21 57 open 70\nlock 30 until 5\nopen 57   # note\nphase 4\nchapter 3\nopen 24\nafter 1 open 300\nnonsense 5\n", f);
    fclose(f);
    CpRule keep[96]; int keep_n = g_cp_n; FILETIME keep_ft = g_cp_ft; memcpy(keep, g_cp, sizeof(keep));
    cp_load(p);
    _wremove(p);
    int er = 1;
    if (g_cp_n != 5) er = -302;
    else if (g_cp[0].kind != 1 || g_cp[0].q != 70 || g_cp[0].nneed != 2 || g_cp[0].need[0] != 21 || g_cp[0].need[1] != 57) er = -303;
    else if (g_cp[1].kind != 2 || g_cp[1].q != 30 || g_cp[1].nneed != 1 || g_cp[1].need[0] != 5) er = -304;
    else if (g_cp[2].kind != 3 || g_cp[2].q != 57 || strcmp(g_cp[2].text, "open 57")) er = -305;
    else if (g_cp[3].kind != 4 || g_cp[3].q != 4 || g_cp[4].kind != 5 || g_cp[4].q != 3) er = -306;
    memcpy(g_cp, keep, sizeof(keep)); g_cp_n = keep_n; g_cp_ft = keep_ft;
    logf("self-test: campaign rules reader %s (%d)", er == 1 ? "ok" : "FAILED", er);
    return er;
}
