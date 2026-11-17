// v5.37 the partner test build ends on a date (owner 2026-10-29). Compiled in only with /DCWO_EXPIRES=YYYYMMDD (build_partner.bat); the owner's
// own build has none of this.
// - The date is the PC's local date, checked once when the game starts. A clock set back does not help: the loader keeps
//   the latest date it has seen (CwoSeen.dat in its data folder) and also reads the write dates of its own log and
//   settings, so a date behind any of them counts as that date.
// - From the cutoff on, nothing of the loader runs: no hooks, no patches, no creators, no 3 VS 3, no new skills. Only
//   native 2.8 stays, answering the patched menus with their 'off' values (no Edgar's Dream rows, no 3 VS 3 LOBBY, the
//   game's own outfit names, no stage view), and every frame a black screen in the game's debug font says the build has
//   ended and how to remove it (UNINSTALL MODS.bat), since the loader cannot remove the game's files itself.
#ifdef CWO_EXPIRES
static int g_ex_date;                                          // the date the check used (YYYYMMDD)

static int ex_ymd(const SYSTEMTIME &t) { return t.wYear * 10000 + t.wMonth * 100 + t.wDay; }

static int ex_file_date(const wchar_t *name) {                 // local write date of a file in the data folder, or 0
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%s%s", g_dir, name);
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a)) return 0;
    FILETIME lt;
    SYSTEMTIME s;
    if (!FileTimeToLocalFileTime(&a.ftLastWriteTime, &lt) || !FileTimeToSystemTime(&lt, &s)) return 0;
    return ex_ymd(s);
}

// pure decision (also used by the self-test): the date in force is the later of today and the latest date seen
static int ex_decide(int today, int seen) { return (today > seen ? today : seen) >= CWO_EXPIRES; }

static bool ex_check() {                                       // DllMain, before anything writes the log
    SYSTEMTIME t;
    GetLocalTime(&t);
    int today = ex_ymd(t), seen = 0;
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoSeen.dat", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (fscanf_s(f, "%d", &seen) != 1) seen = 0; fclose(f); }
    static const wchar_t *const WATCH[] = {L"cwo_loader.log", L"CwoSeen.dat", L"CwoEdgar.txt", L"CwoCharacters.txt",
                                           L"CwoNewSkills.txt", L"CwoModes.txt", L"CwoLobby.txt", L"CwoSpectator.txt"};
    for (const wchar_t *w : WATCH) { int d = ex_file_date(w); if (d > seen) seen = d; }
    g_ex_date = today > seen ? today : seen;
    if (cwo_wfopen(&f, p, L"w") == 0 && f) { fprintf(f, "%d\n", g_ex_date); fclose(f); }
    return ex_decide(today, seen) != 0;
}

// native 2.8 after the cutoff: the patched menu scripts get the values that mean 'nothing here'
static INT64 ex_native(int op, INT64 *args) {
    (void)args;
    if (op == 46 || op == 48) return 0;                       // Edgar's Dream menu: no rows
    if (op == 47) return 1;
    if (op >= 21 && op <= 28) return 0;                       // 3 VS 3 LOBBY: never opens, START refused
    if (op == 44) return (INT64)(ULONG_PTR)ON_GAME;           // COLORS: the game's own row text
    if (op == 49) return 0;                                   // stage view: off
    if (op == 50) return 3;
    if (op >= 51 && op <= 54) return op == 54 ? 2 : 0;        // Edgar's Dream pages: closed
    if (op == 61) return 1;
    // (35-43, 55-60, the Xbox Live / split-screen lobby rows, never come here: forge_native keeps answering them with
    //  the game's own functions, since the lobby scripts need their answers)
    return 2;                                                 // 0 / 1 (the old Forge session) and the rest: nothing
}

// every frame: a black screen with the notice, in the game's debug font
static void ex_draw() {
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 300) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    game_box(0, 0, 640, 480, 0xFF000000);
    *space = 0; *gap = 2; *fh = 16; *fw = 8;
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    char when[48];
    int y = CWO_EXPIRES / 10000, m = CWO_EXPIRES / 100 % 100, d = CWO_EXPIRES % 100;
    static const char *const MON[] = {"", "January", "February", "March", "April", "May", "June", "July", "August",
                                      "September", "October", "November", "December"};
    sprintf_s(when, "ended on %s %d, %d.", m >= 1 && m <= 12 ? MON[m] : "?", d, y);
    struct { DWORD c; const char *s; } L[] = {
        {0xFFFFD040, "PHANTOM DUST - PRIVATE TEST BUILD"},
        {0xFFFFFFFF, "This test build"},
        {0xFFFFFFFF, when},
        {0xFFFFFFFF, ""},
        {0xFFA0E0FF, "Close the game and run UNINSTALL MODS.bat"},
        {0xFFA0E0FF, "to play the normal game again."},
    };
    int yy = 170;
    for (auto &l : L) {
        set_rgba(rgba, l.c);
        int x = (640 - (int)strlen(l.s) * 8) / 2;
        if (l.s[0]) text(x < 0 ? 0 : x, yy, l.s);
        yy += 22;
    }
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w;
    memcpy(rgba, s_rgba, sizeof(s_rgba));
}

// self-test (test_host / CwoSelfTest): the decision on both sides of the date and with a clock set back
static bool ex_selftest() {
    int day_before = CWO_EXPIRES - 1, after = CWO_EXPIRES + 1;
    return !ex_decide(day_before, 0) && ex_decide(CWO_EXPIRES, 0) && ex_decide(after, 0) &&
           ex_decide(day_before, CWO_EXPIRES) && !ex_decide(20261001, 20261029);
}
#endif
