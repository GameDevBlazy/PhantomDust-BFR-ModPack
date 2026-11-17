// ---- v5.23 stage music picker (owner 2026-10-27; "go ahead and work on adding all 8 of those ideas"). Edgar's Dream > Stage Music: one row per stage; L/R picks
// what it plays (the game's own, random, any of the 7 stage soundtracks, or one of the game's other tracks), A plays it
// here, B goes back (the menu's music again). Kept in CwoMusic.txt (loader data folder). Offline only.
// Game facts (Ghidra batch_065, pcode.py, capstone on the clean exe, 2026-10-27):
//  - FUN_14019cdf0(track, mode) plays track 0..0x3c of the table 0x1403e8bf0 (16-byte entries: +0 the stream name
//    "NN_Name.stx", +8 flags, +9 the stage group 0-6 or 0xff); the track playing is the u16 *0x14052acec. Its only callers
//    are the script natives 1.16 = FUN_140260ee0 (mode 0; 1092 calls in 311 scripts: menus, battles) and 1.18 =
//    FUN_140260f00 (mode 1; the quest scripts); native group 1's array is 0x1403fafe0 (group table 0x1402ddd00), so their
//    slots are 0x1403fb060 and 0x1403fb070.
//  - the stage soundtracks are tracks 1-21, three per stage group: Shopping Mall 1-3, Highway 4-6, Panorama 7-9, Yojo
//    Plant 10-12, Gravity Reversal 13-15, City of Memories 16-18, Ruins 19-21 (group byte +9 = 0, 1, 2, 4, 3, 5, 6).
// The loader takes the two slots (checked first); a stage track asked for during a battle on a stage with a choice becomes
// that choice - the same part (1st / 2nd / 3rd) of the chosen soundtrack, or the chosen single track. Everything else
// (menus, other tracks, online) goes to the game unchanged.
static const ULONG_PTR RVA_NATIVES_G1 = 0x3fafe0, RVA_BGM_N16 = 0x260ee0, RVA_BGM_N18 = 0x260f00, RVA_BGM_PLAY = 0x19cdf0,
                       RVA_BGM_CUR = 0x52acec;
typedef INT64(__fastcall *PFN_NATIVE)(INT64 *);
typedef UINT64(__fastcall *PFN_BGM)(unsigned, int);
static PFN_NATIVE g_mu_o16, g_mu_o18;
static bool g_mu_ok;
static const char *MU_FAM[7] = {"Shopping Mall", "Highway", "Panorama", "Yojo Plant", "Gravity Reversal", "City of Memories", "Ruins"};
struct MuSingle { int track; const char *name; };
static const MuSingle MU_ONE[] = {{22, "Boss 1"}, {23, "Boss 2"}, {24, "Last Boss"}, {25, "Last Boss 2"}, {26, "Terminal"},
                                  {27, "Bar (Sakaba)"}, {28, "Committee (Iinkai)"}, {29, "Arsenal Edit"}, {30, "Memory Box"},
                                  {31, "Title"}, {33, "Options"}, {34, "Versus Select"}, {35, "Xbox Live Select"},
                                  {37, "Battle Win"}, {38, "Battle Lose"}, {41, "Switch (Syokirikae)"}, {45, "Ending"}, {60, "Shop"}};
static const int MU_NONE = (int)(sizeof(MU_ONE) / sizeof(MU_ONE[0]));
// a pick: 0 the game's own, 1 random soundtrack, 2..8 soundtrack 0..6, 9.. MU_ONE[pick - 9]
static const int MU_PICKS = 9 + MU_NONE;
struct MuStage { int stage, pick; };
static MuStage g_mu[96];
static int g_mu_n;
static bool g_mu_loaded;
static int g_mu_rand = -1;                                     // this battle's random soundtrack
static volatile LONG g_mu_preview = -1, g_mu_restore = -1;     // page -> game thread: play a track / the menu's again
static int g_mu_menu_track = -1;
static int g_mu_t_stage = -1, g_mu_t_online, g_mu_t_seen = -1, g_mu_t_mode = -1;   // self-test stand-ins

static void mu_path(wchar_t *p) { swprintf_s(p, MAX_PATH, L"%sCwoMusic.txt", g_dir); }
static void mu_load() {
    if (g_mu_loaded) return;
    g_mu_loaded = true;
    if (g_mu_t_stage >= 0) return;
    wchar_t p[MAX_PATH];
    mu_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[96];
    int s, k;
    while (fgets(line, sizeof(line), f) && g_mu_n < 96)
        if (sscanf_s(line, "stage %d %d", &s, &k) == 2 && k > 0 && k < MU_PICKS) g_mu[g_mu_n++] = {s, k};
    fclose(f);
}
static void mu_save() {
    if (g_mu_t_stage >= 0) return;
    wchar_t p[MAX_PATH];
    mu_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# Stage music (loader): stage id, pick (1 random, 2-8 a stage soundtrack, 9.. one track)\n");
    for (int i = 0; i < g_mu_n; i++) fprintf(f, "stage %d %d\n", g_mu[i].stage, g_mu[i].pick);
    fclose(f);
}
static int mu_pick_of(int stage) {
    mu_load();
    for (int i = 0; i < g_mu_n; i++) if (g_mu[i].stage == stage) return g_mu[i].pick;
    return 0;
}
static void mu_set(int stage, int pick) {
    for (int i = 0; i < g_mu_n; i++)
        if (g_mu[i].stage == stage) {
            if (pick) g_mu[i].pick = pick; else g_mu[i] = g_mu[--g_mu_n];
            mu_save();
            return;
        }
    if (pick && g_mu_n < 96) { g_mu[g_mu_n++] = {stage, pick}; mu_save(); }
}
static void mu_pick_name(int pick, char *out, size_t n) {
    if (pick <= 0) strcpy_s(out, n, "the game's own");
    else if (pick == 1) strcpy_s(out, n, "random soundtrack");
    else if (pick < 9) sprintf_s(out, n, "%s soundtrack", MU_FAM[pick - 2]);
    else sprintf_s(out, n, "%s", MU_ONE[pick - 9].name);
}
// the track to play for a stage track asked for during a battle on `stage` (0 = leave it)
static int mu_map(int track, int stage) {
    if (track < 1 || track > 21) return 0;
    int pick = mu_pick_of(stage);
    if (pick <= 0) return 0;
    int part = (track - 1) % 3;
    if (pick == 1) {
        if (g_mu_rand < 0) g_mu_rand = (int)(GetTickCount64() / 7 % 7);
        return g_mu_rand * 3 + 1 + part;
    }
    if (pick < 9) return (pick - 2) * 3 + 1 + part;
    return MU_ONE[pick - 9].track;
}
static INT64 mu_call(PFN_NATIVE orig, INT64 *args, int mode) {
    int track = 0, stage = 0;
    __try {
        track = (int)(unsigned)args[0];
        bool online = g_mu_t_stage >= 0 ? g_mu_t_online != 0 : ed_online();
        if (!online && track >= 1 && track <= 21) {
            stage = g_mu_t_stage >= 0 ? g_mu_t_stage : *(int *)(g_base + RVA_STAGE);
            int t = mu_map(track, stage);
            if (t > 0) {
                logf("stage music: stage %d asked for track %d (mode %d) - plays %d", stage, track, mode, t);
                INT64 a[4] = {t, args[1], args[2], args[3]};
                return orig(a);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return orig(args);
}
static INT64 __fastcall mu_native16(INT64 *args) { return mu_call(g_mu_o16, args, 0); }
static INT64 __fastcall mu_native18(INT64 *args) { return mu_call(g_mu_o18, args, 1); }

static bool mu_install() {
    void **s16 = (void **)(g_base + RVA_NATIVES_G1 + 8 * 16), **s18 = (void **)(g_base + RVA_NATIVES_G1 + 8 * 18);
    __try {
        if (*s16 != (void *)(g_base + RVA_BGM_N16) || *s18 != (void *)(g_base + RVA_BGM_N18)) {
            logf("stage music: refused, natives 1.16 / 1.18 hold %p / %p", *s16, *s18);
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    g_mu_o16 = (PFN_NATIVE)*s16; g_mu_o18 = (PFN_NATIVE)*s18;
    DWORD old;
    if (!VirtualProtect(s16, 0x18, PAGE_READWRITE, &old)) return false;
    *s16 = (void *)mu_native16;
    *s18 = (void *)mu_native18;
    VirtualProtect(s16, 0x18, old, &old);
    logf("stage music: natives 1.16 / 1.18 taken (stage picks in CwoMusic.txt)");
    return g_mu_ok = true;
}
static void mu_step(bool in_match) {                           // game thread, every frame: previews from the page
    if (!in_match) g_mu_rand = -1;
    if (!g_mu_ok) return;
    LONG p = InterlockedExchange(&g_mu_preview, -1), r = InterlockedExchange(&g_mu_restore, -1);
    __try {
        PFN_BGM play = (PFN_BGM)(g_base + RVA_BGM_PLAY);
        if (p >= 0) {
            if (g_mu_menu_track < 0) g_mu_menu_track = *(unsigned short *)(g_base + RVA_BGM_CUR);
            play((unsigned)p, 0);
        } else if (r >= 0 && g_mu_menu_track >= 0) {
            play((unsigned)g_mu_menu_track, 0);
            g_mu_menu_track = -1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- the page (controller thread)
static int g_mu_row, g_mu_top;
static int mu_stage_rows(int *ids, char names[][32], int cap) {  // the Single Play stage list's stages, once each
    int n = 0;
    __try {
        int rows = *(int *)(g_base + RVA_STAGE_LIST_N);
        for (int i = 0; i < rows && i < 120 && n < cap; i++) {
            char *row = (char *)(g_base + RVA_STAGE_LIST + 0x28 * i);
            const char *nm = *(const char **)row;
            int id = *(int *)(row + 0x18);
            if (!nm || !nm[0] || (unsigned char)nm[0] >= 0x80 || id <= 0) continue;
            bool dup = false;
            for (int k = 0; k < n; k++) dup |= ids[k] == id;
            if (dup) continue;
            ids[n] = id;
            strncpy_s(names[n], 32, nm, _TRUNCATE);
            n++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}
static int mu_preview_track(int pick) {
    if (pick == 1) return 1;
    if (pick >= 2 && pick < 9) return (pick - 2) * 3 + 1;
    if (pick >= 9) return MU_ONE[pick - 9].track;
    return -1;
}
static void mu_input(WORD pressed) {
    static int ids[96]; static char names[96][32];
    int n = mu_stage_rows(ids, names, 96);
    if (pressed & BTN_B) { InterlockedExchange(&g_mu_restore, 1); InterlockedExchange(&g_ed_page, EDP_CHOOSER); return; }
    if (!n) return;
    if (pressed & BTN_DOWN) g_mu_row = (g_mu_row + 1) % n;
    if (pressed & BTN_UP) g_mu_row = (g_mu_row + n - 1) % n;
    if (g_mu_row >= n) g_mu_row = 0;
    int dir = (pressed & BTN_RIGHT) ? 1 : (pressed & BTN_LEFT) ? -1 : 0;
    if (dir) { int k = mu_pick_of(ids[g_mu_row]); mu_set(ids[g_mu_row], ((k + dir) % MU_PICKS + MU_PICKS) % MU_PICKS); }
    if (pressed & BTN_X) { mu_set(ids[g_mu_row], 0); set_status("The game's own music for this stage"); }
    if (pressed & BTN_A) {
        int t = mu_preview_track(mu_pick_of(ids[g_mu_row]));
        if (t > 0) { InterlockedExchange(&g_mu_preview, t); set_status("Playing it (B: back to the menu's music)"); }
        else set_status("The game's own: the stage plays its usual music");
    }
}
static const char *MU_H[] = {"STAGE MUSIC - how to use", "Up / Down: a stage   L / R: what it plays", "  the game's own, a random soundtrack,",
                             "  any of the 7 stage soundtracks, or one of the game's other tracks", "A: listen to it here   X: the game's own again",
                             "In a match the stage plays your pick (offline only)", NULL};
template <class PUT, class BOX> static void mu_draw(PUT &put, BOX &box, int x, int y) {
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF, GREY = 0xFFB0B0B0, GREEN = 0xFF80FF80;
    const int LINE = 16, ROWS = 16;
    (void)box;
    if (g_ed_help) {
        put(x, y, MU_H[0], GOLD);
        y += LINE + 4;
        for (int i = 1; MU_H[i]; i++, y += LINE) put(x, y, MU_H[i], WHITE);
        put(x, y + 4, "Y / A / B: close this help", BLUE);
        return;
    }
    put(x, y, "STAGE MUSIC                (B: back   Y: help)", GOLD);
    y += LINE + 4;
    if (!g_mu_ok) { put(x, y, "Not available: the game's music natives were not the expected ones (see the log).", 0xFFFF6060); return; }
    static int ids[96]; static char names[96][32];
    int n = mu_stage_rows(ids, names, 96);
    if (!n) { put(x, y, "The stage list is not loaded yet.", GREY); return; }
    if (g_mu_row >= n) g_mu_row = 0;
    if (g_mu_row < g_mu_top) g_mu_top = g_mu_row;
    if (g_mu_row >= g_mu_top + ROWS) g_mu_top = g_mu_row - ROWS + 1;
    char line[128], pick[48];
    for (int i = g_mu_top; i < n && i < g_mu_top + ROWS; i++, y += LINE) {
        int k = mu_pick_of(ids[i]);
        mu_pick_name(k, pick, sizeof(pick));
        sprintf_s(line, "%s%-24.24s  %s%s%s", i == g_mu_row ? "> " : "  ", names[i], i == g_mu_row ? "< " : "", pick, i == g_mu_row ? " >" : "");
        put(x, y, line, i == g_mu_row ? GOLD : k ? GREEN : WHITE);
    }
    if (n > ROWS) { sprintf_s(line, "%d-%d of %d stages", g_mu_top + 1, g_mu_top + ROWS < n ? g_mu_top + ROWS : n, n); put(x, y + 4, line, GREY); }
    put(x, y + LINE + 6, "L/R: the music   A: listen   X: the game's own", BLUE);
}

static int mu_self_test() {                                    // the mapping and the natives on stand-ins
    MuStage keep[96]; int keep_n = g_mu_n; bool keep_loaded = g_mu_loaded;
    memcpy(keep, g_mu, sizeof(keep));
    g_mu_loaded = true; g_mu_n = 0; g_mu_t_stage = 7; g_mu_t_online = 0;
    g_mu[g_mu_n++] = {7, 2 + 1};                               // stage 7: the Highway soundtrack
    g_mu[g_mu_n++] = {9, 9 + 2};                               // stage 9: Last Boss
    bool map = mu_map(1, 7) == 4 && mu_map(2, 7) == 5 && mu_map(9, 7) == 6 && mu_map(22, 7) == 0 &&   // the same part; not a stage track
               mu_map(19, 9) == 24 && mu_map(4, 3) == 0;                                               // a single track; no pick
    g_mu_rand = -1;
    g_mu[g_mu_n++] = {11, 1};
    int r1 = mu_map(1, 11), r2 = mu_map(3, 11);
    bool rnd = r1 >= 1 && r1 <= 21 && (r1 - 1) % 3 == 0 && r2 == r1 + 2;   // one random soundtrack for the whole battle
    struct O { static INT64 __fastcall f(INT64 *a) { g_mu_t_seen = (int)a[0]; return 0; } };
    g_mu_o16 = O::f; g_mu_o18 = O::f;
    INT64 a[4] = {7, 0, 0, 0};
    mu_native16(a);
    bool nat = g_mu_t_seen == 4 && a[0] == 7;                  // 7 (Panorama 1) on the Highway stage -> 4; the script's value kept
    a[0] = 31; mu_native16(a); nat &= g_mu_t_seen == 31;       // the title music: unchanged
    g_mu_t_online = 1; a[0] = 7; mu_native18(a); nat &= g_mu_t_seen == 7;   // online: unchanged
    char nm[48]; mu_pick_name(9 + 2, nm, sizeof(nm)); bool names = !strcmp(nm, "Last Boss");
    g_mu_o16 = g_mu_o18 = NULL; g_mu_t_stage = -1; g_mu_t_online = 0; g_mu_rand = -1;
    memcpy(g_mu, keep, sizeof(keep)); g_mu_n = keep_n; g_mu_loaded = keep_loaded;
    logf("self-test: stage music map %s, random %s, natives %s, names %s", map ? "ok" : "FAILED", rnd ? "ok" : "FAILED", nat ? "ok" : "FAILED", names ? "ok" : "FAILED");
    return map && rnd && nat && names ? 1 : -66;
}
