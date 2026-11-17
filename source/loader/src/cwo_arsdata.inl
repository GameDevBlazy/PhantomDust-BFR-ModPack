// ---- v5.64 Arsenal Data per stage (owner 2026-11-02; the maps named: palace, highway, panorama, city, refinery, lane, ruins).
// Game facts (Ghidra batch_120b_arsenal_data_natives, 2026-11-02):
//  - native 5.101 = FUN_140217b40(args): args[0] the player (5.113 = FUN_140217eb0 = *(*0x1404bec08 + 0xb50)),
//    args[1] the arsenal, args[2] the value: the arsenal record *(*0x1404bec10 + (player + 0x10a) * 0x38) + arsenal * 100
//    holds +0x54 missions cleared, +0x58 mission attempts, +0x5c Xbox Live wins, +0x60 Xbox Live attempts; values 2 / 5
//    are wins * 100 / attempts (0 while either is 0);
//  - the stage of a match: *0x1403e0304 (cwo_dbtl.inl); the VS stages are 2-8 with their lighting variants + 10 / + 20
//    (cwo_stagelist.inl): 2 Spiral Highway, 3 Lost Palace, 4 Panorama, 5 Dawn City, 6 Storm Refinery, 7 Twilight Lane,
//    8 Sein (the ruins) - asset catalogue 2026-11-01 from the game's stage list.
// The page (tools/build_ars_data.py): the "XBOX LIVE" heading is this text (2.104 keeps the pointer, so a change shows at
// once), its three values ask op 97 / 96 first, Y = op 98. The counts: when the game adds to an arsenal's Xbox Live wins /
// attempts, the loader adds the same to that arsenal's row for the match's stage (file CwoArsStages.txt; an arsenal put in
// another arsenal's slot, or its results reset, starts at 0 again).
// The counting watches the records during Xbox Live / system link sessions, read only (owner 2026-11-02 "Yes yes and yes",
// AGENTS.md "Arsenal Data per stage online"); AD_ONLINE_COUNT false would make the loader do nothing for it online.
static const bool AD_ONLINE_COUNT = true;
static const int AD_MAPS = 7;
static const char *AD_NAME[AD_MAPS] = {"PALACE", "HIGHWAY", "PANORAMA", "CITY", "REFINERY", "LANE", "RUINS"};
static const int AD_STAGE[AD_MAPS] = {3, 2, 4, 5, 6, 7, 8};
struct AdEnt { unsigned char name[16]; int w[AD_MAPS], a[AD_MAPS]; };
struct AdSnap { bool ok; int list; unsigned char name[ARS_N][16]; int w[ARS_N], a[ARS_N]; };
static AdEnt g_ad[2][ARS_LISTS][ARS_N];
static AdSnap g_ad_snap[2];
static bool g_ad_loaded;
static int g_ad_mode, g_ad_match_map = -1;
static char g_ad_title[48] = "XBOX LIVE: ALL";
static unsigned char *g_ad_t_recs[2];                          // self-test stand-ins
static int g_ad_t_stage = -1, g_ad_t_list = -1;

static int ad_map_of(int stage) {                              // a VS stage (any lighting) -> 0..6, else -1
    if (stage < 2 || stage >= 30) return -1;
    for (int m = 0; m < AD_MAPS; m++) if (stage % 10 == AD_STAGE[m]) return m;
    return -1;
}
static void ad_path(wchar_t *p) { swprintf_s(p, MAX_PATH, L"%sCwoArsStages.txt", g_dir); }
static void ad_load() {
    g_ad_loaded = true;
    if (g_ad_t_recs[0]) return;
    wchar_t p[MAX_PATH];
    ad_path(p);
    FILE *f = NULL;
    if (_wfopen_s(&f, p, L"rb") || !f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int s, l, i, n = 0;
        char hex[40] = "";
        if (line[0] == '#' || sscanf_s(line, "%d %d %d %39s%n", &s, &l, &i, hex, (unsigned)sizeof(hex), &n) != 4) continue;
        if (s < 0 || s > 1 || l < 0 || l >= ARS_LISTS || i < 0 || i >= ARS_N || strlen(hex) != 32) continue;
        AdEnt &e = g_ad[s][l][i];
        for (int k = 0; k < 16; k++) { unsigned v = 0; sscanf_s(hex + 2 * k, "%2x", &v); e.name[k] = (unsigned char)v; }
        const char *q = line + n;
        for (int m = 0; m < AD_MAPS; m++) {
            int c = 0;
            if (sscanf_s(q, "%d %d%n", &e.w[m], &e.a[m], &c) != 2) { e.w[m] = e.a[m] = 0; break; }
            q += c;
        }
    }
    fclose(f);
}
static void ad_save() {
    if (g_ad_t_recs[0]) return;
    wchar_t p[MAX_PATH], t[MAX_PATH];
    ad_path(p);
    swprintf_s(t, L"%s.tmp", p);
    FILE *f = NULL;
    if (_wfopen_s(&f, t, L"wb") || !f) return;
    fprintf(f, "# Xbox Live results of each arsenal per stage: set list slot, the arsenal's name (hex), then wins attempts for");
    for (int m = 0; m < AD_MAPS; m++) fprintf(f, " %s", AD_NAME[m]);
    fprintf(f, "\n");
    for (int s = 0; s < 2; s++)
        for (int l = 0; l < ARS_LISTS; l++)
            for (int i = 0; i < ARS_N; i++) {
                const AdEnt &e = g_ad[s][l][i];
                int any = 0;
                for (int m = 0; m < AD_MAPS; m++) any |= e.a[m] | e.w[m];
                if (!any) continue;
                fprintf(f, "%d %d %d ", s, l, i);
                for (int k = 0; k < 16; k++) fprintf(f, "%02x", e.name[k]);
                for (int m = 0; m < AD_MAPS; m++) fprintf(f, " %d %d", e.w[m], e.a[m]);
                fprintf(f, "\n");
            }
    fclose(f);
    MoveFileExW(t, p, MOVEFILE_REPLACE_EXISTING);
}
static unsigned char *ad_recs(int s) {
    if (g_ad_t_recs[0]) return g_ad_t_recs[s];
    ULONG_PTR prof = ars_prof();
    return prof ? ars_records(prof, s) : NULL;
}
static int ad_list(int s) {
    if (g_ad_t_list >= 0) return g_ad_t_list;
    int l = g_ars_loaded ? g_ars.active[s] : 0;
    return l >= 0 && l < ARS_LISTS ? l : 0;
}
static int ad_stage() { return g_ad_t_stage >= 0 ? g_ad_t_stage : *(int *)(g_base + RVA_STAGE); }
static void ad_take(AdSnap &sn, const unsigned char *r, int l) {
    sn.ok = true;
    sn.list = l;
    for (int i = 0; i < ARS_N; i++) {
        const unsigned char *rec = r + i * ARS_REC;
        memcpy(sn.name[i], rec + 4, 16);
        sn.w[i] = *(const int *)(rec + 0x5c);
        sn.a[i] = *(const int *)(rec + 0x60);
    }
}
static void ad_step() {                                        // CwoOverlayFrame, game thread
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (!g_ad_t_recs[0]) {
        if (now < next) return;
        next = now + 200;
        if (!AD_ONLINE_COUNT && ed_online()) { g_ad_snap[0].ok = g_ad_snap[1].ok = false; return; }   // no exception yet
    }
    if (!g_ad_loaded) ad_load();
    bool dirty = false;
    __try {
        if (g_in_match || g_ad_t_recs[0]) { int m = ad_map_of(ad_stage()); if (m >= 0) g_ad_match_map = m; }
        for (int s = 0; s < 2; s++) {
            unsigned char *r = ad_recs(s);
            AdSnap &sn = g_ad_snap[s];
            if (!r) { sn.ok = false; continue; }
            int l = ad_list(s);
            if (!sn.ok || sn.list != l) { ad_take(sn, r, l); continue; }   // another list laid in: nothing counted
            int map = g_ad_match_map >= 0 ? g_ad_match_map : ad_map_of(ad_stage());
            for (int i = 0; i < ARS_N; i++) {
                const unsigned char *rec = r + i * ARS_REC;
                int w = *(const int *)(rec + 0x5c), a = *(const int *)(rec + 0x60);
                AdEnt &e = g_ad[s][l][i];
                bool same = !memcmp(sn.name[i], rec + 4, 16);
                if (memcmp(e.name, rec + 4, 16)) {             // another arsenal in this slot: its own counts from 0
                    memset(&e, 0, sizeof(e));
                    memcpy(e.name, rec + 4, 16);
                    dirty = true;
                }
                if (same) {
                    if (a < sn.a[i] || w < sn.w[i]) {          // the results were reset
                        if (a == 0) { memset(e.w, 0, sizeof(e.w)); memset(e.a, 0, sizeof(e.a)); dirty = true; }
                    } else if (map >= 0) {
                        int da = a - sn.a[i], dw = w - sn.w[i];
                        if (da > 0 && da <= 2) e.a[map] += da;
                        if (dw > 0 && dw <= 2) e.w[map] += dw;
                        if (da > 0 || dw > 0) {
                            dirty = true;
                            logf("arsenal data: set %d list %d arsenal %d on %s: %d W of %d", s, l + 1, i + 1, AD_NAME[map], e.w[map], e.a[map]);
                        }
                    }
                }
                memcpy(sn.name[i], rec + 4, 16);
                sn.w[i] = w;
                sn.a[i] = a;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_ad_snap[0].ok = g_ad_snap[1].ok = false; }
    if (dirty) ad_save();
}
static void ad_title() {
    sprintf_s(g_ad_title, "XBOX LIVE: %s", g_ad_mode ? AD_NAME[g_ad_mode - 1] : "ALL");
}
static INT64 ad_native(int op, INT64 *args) {                  // native 2.8 ops 96-99 (build_ars_data.py)
    if (op == 97) return g_ad_mode;                            // 0 = all: the game's own values
    if (op == 99) { g_ad_mode = 0; ad_title(); return (INT64)(ULONG_PTR)g_ad_title; }   // the page is built
    if (op == 98) { g_ad_mode = (g_ad_mode + 1) % (AD_MAPS + 1); ad_title(); return 0; }   // Y
    if (op != 96 || g_ad_mode < 1) return 0;
    int s = (int)args[1], i = (int)args[2], k = (int)args[3], m = g_ad_mode - 1;   // 5.101's player, arsenal, value
    if (s < 0 || s > 1 || i < 0 || i >= ARS_N) return 0;
    if (!g_ad_loaded) ad_load();
    const AdEnt &e = g_ad[s][ad_list(s)][i];
    __try {
        unsigned char *r = ad_recs(s);
        if (!r || memcmp(e.name, r + i * ARS_REC + 4, 16)) return 0;   // not this arsenal's counts
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (k == 3) return e.w[m];
    if (k == 4) return e.a[m];
    if (k == 5) return e.w[m] > 0 && e.a[m] > 0 ? e.w[m] * 100 / e.a[m] : 0;
    return 0;
}
static int ad_self_test() {
    static unsigned char recs[2][ARS_N * ARS_REC];
    memset(recs, 0, sizeof(recs));
    memcpy(recs[0] + 2 * ARS_REC + 4, "FIRE DECK", 9);
    g_ad_t_recs[0] = recs[0]; g_ad_t_recs[1] = recs[1];
    g_ad_t_list = 1; g_ad_t_stage = 13;                        // Light Palace
    memset(g_ad, 0, sizeof(g_ad)); memset(g_ad_snap, 0, sizeof(g_ad_snap)); g_ad_match_map = -1;
    ad_step();                                                 // the snapshot
    *(int *)(recs[0] + 2 * ARS_REC + 0x60) = 1; *(int *)(recs[0] + 2 * ARS_REC + 0x5c) = 1;
    ad_step();                                                 // a win on the Palace
    g_ad_t_stage = 7; g_ad_match_map = -1;
    *(int *)(recs[0] + 2 * ARS_REC + 0x60) = 2;
    ad_step();                                                 // a loss on the Lane
    INT64 a[4] = {96, 0, 2, 3};
    g_ad_mode = 1; INT64 pw = ad_native(96, a); a[3] = 4; INT64 pa = ad_native(96, a); a[3] = 5; INT64 pr = ad_native(96, a);
    g_ad_mode = 6; a[3] = 3; INT64 lw = ad_native(96, a); a[3] = 4; INT64 la = ad_native(96, a);
    ad_native(99, a); bool t0 = !strcmp(g_ad_title, "XBOX LIVE: ALL") && ad_native(97, a) == 0;
    for (int n = 0; n < 7; n++) ad_native(98, a);
    bool t7 = !strcmp(g_ad_title, "XBOX LIVE: RUINS");
    ad_native(98, a); bool tw = g_ad_mode == 0;
    *(int *)(recs[0] + 2 * ARS_REC + 0x60) = 0; *(int *)(recs[0] + 2 * ARS_REC + 0x5c) = 0;
    ad_step();                                                 // Reset Battle Results
    bool rs = g_ad[0][1][2].a[0] == 0 && g_ad[0][1][2].a[5] == 0;
    *(int *)(recs[0] + 2 * ARS_REC + 0x60) = 1;
    ad_step();
    memcpy(recs[0] + 2 * ARS_REC + 4, "ICE DECK\0", 9);
    ad_step();                                                 // another arsenal in the slot
    bool nw = g_ad[0][1][2].a[5] == 0 && !memcmp(g_ad[0][1][2].name, "ICE DECK", 8);
    bool maps = ad_map_of(2) == 1 && ad_map_of(23) == 0 && ad_map_of(8) == 6 && ad_map_of(10) == -1 && ad_map_of(9) == -1 && ad_map_of(32) == -1;
    bool ok = pw == 1 && pa == 1 && pr == 100 && lw == 0 && la == 1 && t0 && t7 && tw && rs && nw && maps;
    logf("self-test: arsenal data per stage %s (%lld/%lld %lld%%, lane %lld/%lld)", ok ? "ok" : "FAILED", pw, pa, pr, lw, la);
    g_ad_t_recs[0] = g_ad_t_recs[1] = NULL; g_ad_t_list = g_ad_t_stage = -1; g_ad_mode = 0; g_ad_match_map = -1;
    memset(g_ad, 0, sizeof(g_ad)); memset(g_ad_snap, 0, sizeof(g_ad_snap)); g_ad_loaded = false;
    return ok ? 1 : -64;
}
