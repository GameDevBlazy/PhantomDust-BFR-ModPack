// ---- v5.63 15 outfits for every fighter, built on the player's machine (owner 2026-11-02). The owner's own build has them as files (IGT-037 OG outfits, install_all_outfits.py
// themes); the BFR pack carries no game files, so the loader makes the same kinds of outfit from the player's own files the
// first time the game asks for one:
//  - the colour count of every fighter row (charaset rows 0-26, stems pc00a-pc11a / enm00a-enm15a; row +0x3A, table
//    *0x140590190) becomes 15 (the stock game has 4);
//  - outfit 5 (file 4) of players 0-10 = the OG outfit: outfit 1's body with the original pcNNa.alr animations moved onto
//    it (the Create a Character builder, cb_build2 with the OG move source - the IGT-037 recipe);
//  - the next outfits = the 10 themes of the outfit generator on outfit 1 (fire, ice, shadow, gold, toxic, crimson, ocean,
//    camo, neon, spectral; players keep their skin, enemies take the whole texture on every sheet); fighters without an OG
//    outfit here (the enemies, pc11a) get the themes from outfit 5 on and the 11th, ivory, last.
// A file the game asks for (CreateFile2 of ...\Player\<stem>\<stem><k>.alr, k 4..14) that is in neither the game folder
// nor the loader's game-file folder is built and written to the loader's folder before the game opens it.
static int g_of_built, g_of_failed;
static volatile LONG g_of_recount;                              // the counts changed: the loader's character list again
static bool of_parse(LPCWSTR f, char *stem, int *k) {            // <stem>\<stem><k>.alr
    if (!f) return false;
    size_t n = wcslen(f);
    if (n < 12 || _wcsicmp(f + n - 4, L".alr")) return false;
    const wchar_t *e = f + n - 4;                                // the outfit number (1 or 2 digits)
    const wchar_t *d = e;
    while (d > f && iswdigit(d[-1])) d--;
    if (d == e || e - d > 2) return false;
    int kk = _wtoi(d);
    const wchar_t *s = d;
    while (s > f && s[-1] != L'\\' && s[-1] != L'/') s--;
    size_t sl = (size_t)(d - s);
    if (sl < 5 || sl > 6) return false;
    char st[8] = "";
    for (size_t i = 0; i < sl; i++) st[i] = (char)towlower(s[i]);
    bool pc = !strncmp(st, "pc", 2) && sl == 5 && isdigit((unsigned char)st[2]) && isdigit((unsigned char)st[3]) && st[4] == 'a';
    bool en = !strncmp(st, "enm", 3) && sl == 6 && isdigit((unsigned char)st[3]) && isdigit((unsigned char)st[4]) && st[5] == 'a';
    if (!pc && !en) return false;
    if (kk < 4 || kk > 14) return false;
    strcpy_s(stem, 8, st);
    *k = kk;
    return true;
}
static bool of_has_og(const char *stem) { return !strncmp(stem, "pc", 2) && atoi(stem + 2) <= 10; }
static bool of_src(const char *name) {                           // <data>\charsrc\<name>.alr from Player\<stem>\<name>.alr
    wchar_t dst[MAX_PATH], src[MAX_PATH], st[8];
    swprintf_s(g_co_src, L"%scharsrc\\", g_dir);
    CreateDirectoryW(g_co_src, NULL);
    swprintf_s(dst, L"%s%S.alr", g_co_src, name);
    if (GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES) return true;
    swprintf_s(st, L"%.5S", name);
    swprintf_s(src, L"%sAssets\\Data\\Player\\%s\\%S.alr", g_install, st, name);
    DWORD n = 0;
    BYTE *b = pf_read(src, &n);
    bool ok = b && n && pf_write(dst, b, n);
    free(b);
    return ok;
}
static bool of_build(const char *stem, int k, Bytes &out, char *why, size_t nwhy) {
    wchar_t base[MAX_PATH];
    swprintf_s(base, L"%sAssets\\Data\\Player\\%S\\%S0.alr", g_install, stem, stem);
    DWORD n = 0;
    BYTE *b = pf_read(base, &n);                                 // the game's own outfit 1
    if (!b || !n) { free(b); sprintf_s(why, nwhy, "%s0.alr not readable", stem); return false; }
    out.assign(b, b + n);
    free(b);
    bool og = of_has_og(stem);
    if (og && k == 4) {                                          // the OG outfit
        int ch = atoi(stem + 2);
        char a[8], o[8];
        sprintf_s(a, "pc%02da0", ch); sprintf_s(o, "pc%02da", ch);
        if (!of_src(a) || !of_src(o)) { sprintf_s(why, nwhy, "the OG files of %s are not readable", stem); return false; }
        g_cb_src = g_co_src;
        CbSpec2 s2 = {};
        s2.base.body = ch; s2.base.colour = 0; s2.base.moves = 11 + ch; s2.base.size_fit = 0;
        for (int i = 0; i < 130; i++) s2.base.from[i] = -1;
        // v5.108 (owner 2026-11-07):
        // the original pcNNa.alr carries jump motions in the evade slots 33-36 (DASH_A .. DASHEND_B); the dashers
        // (charaset +0x36 = 751: rows 1, 5, 6, 9, 10) take those four from their own outfit 1 - the IGT-037b fix (2026-10-21,
        // seen in game), which the file-based MODS outfit has and this build lacked.
        if (ch == 1 || ch == 5 || ch == 6 || ch == 9 || ch == 10)
            for (int i = 33; i <= 36; i++) s2.base.from[i] = ch;
        s2.head = s2.mid = s2.low = s2.headx = -1; s2.mstyle = s2.lstyle = 0;
        return cb_build2(s2, out, why, nwhy);
    }
    // v5.66 designer outfits instead of one-colour themes (owner 2026-11-02);
    // slot 5 stays the OG outfit for players 0-10 (owner)
    int design = og ? k - 5 : k - 4;                             // 0..9 (players with an OG), 0..10 (the rest)
    return ds_build(stem, out, design, why, nwhy);
}
static void of_ensure(LPCWSTR f, const char *stem, int k) {       // the game is about to open f
    wchar_t ov[MAX_PATH], rel[MAX_PATH], gp[MAX_PATH];
    if (sm_redirect(f, ov, MAX_PATH, false)) return;             // in the loader's folder already
    if (!sm_relative(f, rel, MAX_PATH)) return;
    swprintf_s(gp, L"%s%s", g_install, rel);
    if (GetFileAttributesW(gp) != INVALID_FILE_ATTRIBUTES) return;   // a file of the game
    if (!sm_redirect(f, ov, MAX_PATH, true)) return;
    Bytes out;
    char why[128] = "";
    ULONGLONG t0 = GetTickCount64();
    if (!of_build(stem, k, out, why, sizeof(why))) { g_of_failed++; logf("outfits: %s%d not built (%s)", stem, k, why); return; }
    if (!pf_write(ov, out.data(), (DWORD)out.size())) { g_of_failed++; logf("outfits: %s%d not written", stem, k); return; }
    g_of_built++;
    logf("outfits: %s%d.alr built (%s, %zu bytes, %llu ms)", stem, k, of_has_og(stem) && k == 4 ? "OG" : "design", out.size(), GetTickCount64() - t0);
}
static void of_counts_step() {                                   // every second: the fighters' colour count 15
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 1000;
    __try {
        char *b = *(char **)(g_base + 0x590190);
        if (!b) return;
        int rows = *(int *)(b + 4), pool = *(int *)(b + 8);
        int rs = rows > 0 ? (pool - 16) / rows : 0;
        if (rows <= 0 || rows > 200 || rs < 0x3C || rs > 0x100) return;
        int changed = 0;
        for (int r = 0; r < rows && r < 27; r++) {
            char *rec = b + 16 + rs * r;
            const char *stem = b + pool + *(int *)rec;
            bool fighter = (!_strnicmp(stem, "pc", 2) && strlen(stem) == 5) || (!_strnicmp(stem, "enm", 3) && strlen(stem) == 6);
            short *cnt = (short *)(rec + 0x3A);
            if (fighter && *cnt >= 1 && *cnt < 15) { *cnt = 15; changed++; }
        }
        if (changed) InterlockedExchange(&g_of_recount, 1);
        if (changed) logf("outfits: %d fighters have 15 outfits (the 5th = OG where the game has one, then the themes)", changed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static int of_self_test() {
    char st[8]; int k = -1;
    bool p = of_parse(L"Assets/Data/Player\\pc02a\\pc02a7.alr", st, &k) && !strcmp(st, "pc02a") && k == 7 &&
             of_parse(L"x\\ENM03A\\ENM03A14.ALR", st, &k) && !strcmp(st, "enm03a") && k == 14 &&
             !of_parse(L"x\\pc02a\\pc02a3.alr", st, &k) && !of_parse(L"x\\pc02a\\pc02a.alr", st, &k) &&
             !of_parse(L"x\\npc00a\\npc00a5.alr", st, &k) && !of_parse(L"x\\pc02a\\pc02a15.alr", st, &k);
    bool og = of_has_og("pc00a") && of_has_og("pc10a") && !of_has_og("pc11a") && !of_has_og("enm00a");
    logf("self-test: 15 outfits names %s, OG rule %s", p ? "ok" : "FAILED", og ? "ok" : "FAILED");
    return p && og ? 1 : -63;
}

// offline test (tests/of_test.cpp): one outfit built from a folder of clean files
extern "C" __declspec(dllexport) int CwoOutfit15Test(const wchar_t *install, const wchar_t *data_dir, const char *stem, int k, const wchar_t *out_path) {
    wcscpy_s(g_install, install);
    wcscpy_s(g_dir, data_dir);
    Bytes out;
    char why[128] = "";
    if (!of_build(stem, k, out, why, sizeof(why))) { logf("outfit15 test: %s", why); return -1; }
    return pf_write(out_path, out.data(), (DWORD)out.size()) ? 1 : -2;
}
