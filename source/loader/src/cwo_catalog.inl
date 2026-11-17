// ---- v5.184.2 the stage catalog after a late start (2026-11-14, found while checking the Underground for the owner)
// In Store mode the loader starts after the game; some sessions read setup\stageset\stageset_en.dat BEFORE the file
// redirect is in, and the game keeps the STOCK catalog all session (read in game 14:34: 30 rows, 'Spiral Highway',
// 'Midnight City', row 29 '29 Dummy' - while the installed MODS file has 31 rows; 3 of 4 sessions today had the MODS one).
// Game facts (capstone on PDUWP 9ba7411b, 2026-11-14): the setup loader table at exe+0x3e7df8 pairs load / parse functions
// (stageset: load FUN_14017d720, parse FUN_14017d830). The parse reads the file buffer: u32 +4 row count, i32 +8 string pool
// offset, rows of 20 bytes from +16 (3 string offsets into the pool, then shorts stage / variants / flag), and fills the
// runtime table at exe+0x509ba0 (0x28 per row: 3 name pointers, ints stage +0x18, variants +0x1c, flag +0x20) and the
// count at exe+0x50a5d0 (room for 65 rows before the count). The names stay pointers into the file buffer.
// So: once the game has its count, the loader compares it with the override file's; when they differ it refills the table
// the same way from its own copy of the file (kept for the whole session). Store mode only; once per session.
static bool g_cat_done;
static unsigned char *g_cat_buf;
static void catalog_check() {
    if (g_cat_done || !g_store_mode) return;
    const ULONG_PTR RVA_CAT_ROWS = 0x50a5d0, RVA_CAT_LIST = 0x509ba0;
    const int CAT_MAX = 64;
    __try {
        int rows = *(volatile int *)(g_base + RVA_CAT_ROWS);
        if (rows <= 0) return;                                  // not parsed yet
        g_cat_done = true;
        wchar_t exe[MAX_PATH], p[MAX_PATH], ov[MAX_PATH], sw[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wchar_t *sl = wcsrchr(exe, L'\\');
        if (sl) sl[1] = 0;
        swprintf_s(p, MAX_PATH, L"%sAssets\\Data\\setup\\stageset\\stageset_en.dat", exe);
        // offline test switch (CwoTestCatalogStock.txt in the loader's data folder): first put the game folder's STOCK catalog in
        // the table, as a session that read it before the redirect has it - then the check below must find it and refill
        swprintf_s(sw, MAX_PATH, L"%sCwoTestCatalogStock.txt", g_dir);
        if (!ed_online() && GetFileAttributesW(sw) != INVALID_FILE_ATTRIBUTES) {
            FILE *t = NULL;
            if (_wfopen_s(&t, p, L"rb") == 0 && t) {           // the Store game folder itself (read only)
                static unsigned char sb[0x4000];
                size_t sn = fread(sb, 1, sizeof(sb) - 1, t);
                fclose(t);
                int sc = *(int *)(sb + 4), sp = *(int *)(sb + 8);
                if (sn > 16 && sc > 0 && sc <= CAT_MAX && sp == 16 + 20 * sc && (size_t)sp < sn) {
                    for (int r = 0; r < sc; r++) {
                        unsigned char *row = sb + 16 + 20 * r;
                        ULONG_PTR e = g_base + RVA_CAT_LIST + 0x28 * (ULONG_PTR)r;
                        for (int k = 0; k < 3; k++) *(ULONG_PTR *)(e + 8 * k) = (ULONG_PTR)(sb + sp + *(int *)(row + 4 * k));
                        *(int *)(e + 0x18) = *(short *)(row + 12); *(int *)(e + 0x1c) = *(short *)(row + 14); *(int *)(e + 0x20) = *(short *)(row + 16);
                    }
                    *(volatile int *)(g_base + RVA_CAT_ROWS) = sc;
                    rows = sc;
                    logf("stage catalog: TEST switch - the stock catalog put in first (%d rows, row 2 '%s')", sc, *(const char **)(g_base + RVA_CAT_LIST + 0x28 * 2));
                }
            }
        }
        if (!sm_redirect(p, ov, MAX_PATH, false)) { logf("stage catalog: no override file - the game's own (%d rows)", rows); return; }
        FILE *f = NULL;
        if (_wfopen_s(&f, ov, L"rb") != 0 || !f) { logf("stage catalog: override not readable"); return; }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n < 16 || n > 0x10000) { fclose(f); logf("stage catalog: override size %ld refused", n); return; }
        unsigned char *b = (unsigned char *)VirtualAlloc(NULL, (SIZE_T)n + 16, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!b) { fclose(f); return; }
        size_t got = fread(b, 1, (size_t)n, f);
        fclose(f);
        int count = *(int *)(b + 4), pool = *(int *)(b + 8);
        if ((long)got != n || count <= 0 || count > CAT_MAX || pool != 16 + 20 * count || pool >= n) {
            VirtualFree(b, 0, MEM_RELEASE); logf("stage catalog: override layout refused (rows %d, pool %d, %ld bytes)", count, pool, n); return;
        }
        for (int r = 0; r < count; r++)                         // every string offset inside the pool, NUL-terminated by the file end
            for (int k = 0; k < 3; k++) {
                int o = *(int *)(b + 16 + 20 * r + 4 * k);
                if (o < 0 || pool + o >= n || !memchr(b + pool + o, 0, (size_t)(n - pool - o))) {
                    VirtualFree(b, 0, MEM_RELEASE); logf("stage catalog: override row %d name %d out of the file - refused", r, k); return;
                }
            }
        if (count == rows) {                                    // the same row count: check the first name too
            const char *g0 = *(const char **)(g_base + RVA_CAT_LIST + 0x28 * 2);
            const char *o0 = (const char *)(b + pool + *(int *)(b + 16 + 20 * 2));
            if (g0 && !strcmp(g0, o0)) { VirtualFree(b, 0, MEM_RELEASE); logf("stage catalog: the game read the override (%d rows); character catalog %d rows", rows, *(int *)(g_base + 0x4c0174)); return; }
        }
        for (int r = 0; r < count; r++) {
            unsigned char *row = b + 16 + 20 * r;
            ULONG_PTR e = g_base + RVA_CAT_LIST + 0x28 * (ULONG_PTR)r;
            for (int k = 0; k < 3; k++) *(ULONG_PTR *)(e + 8 * k) = (ULONG_PTR)(b + pool + *(int *)(row + 4 * k));
            *(int *)(e + 0x18) = *(short *)(row + 12);
            *(int *)(e + 0x1c) = *(short *)(row + 14);
            *(int *)(e + 0x20) = *(short *)(row + 16);
        }
        *(volatile int *)(g_base + RVA_CAT_ROWS) = count;
        g_cat_buf = b;
        logf("stage catalog: the game had read the stock file before the redirect (%d rows) - refilled from the override (%d rows)", rows, count);
        logf("character catalog: %d rows this session", *(int *)(g_base + 0x4c0174));
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_cat_done = true; logf("stage catalog: fault - left as the game read it"); }
}

// ---- v5.184.4 the character catalog the same way (seen 2026-11-14 14:53: a session with the stock charaset, 47 rows - the
// EXTRAS characters (rows 47-49) could not be picked; the 5.184.3 guard keeps a stray pick from an empty row).
// Game facts (capstone, 2026-11-14): FUN_14017d300 parses the file in the shared setup load slot *(exe+0x590190): count u32 +4
// -> exe+0x4c0174, rows (64 or 76 bytes, by (pool - 16) / count) -> the table at exe+0x4c01e0 (0xA0 per row; names stay
// pointers into the file). It touches nothing else. The slot is shared by the 4 boot files (load / parse pairs at
// exe+0x3e7df8), so the loader waits for the title (menu manager up + 3 s), then lends the slot its own copy of the override
// file for one call of the game's own parse and puts the slot back. Store mode; once; only when the counts differ.
static bool g_cc_done;
static unsigned char *g_cc_buf;
static unsigned char *cat_read(const wchar_t *path, long *len) {
    FILE *f = NULL;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 16 || n > 0x40000) { fclose(f); return NULL; }
    unsigned char *b = (unsigned char *)VirtualAlloc(NULL, (SIZE_T)n + 16, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { VirtualFree(b, 0, MEM_RELEASE); b = NULL; }
    fclose(f);
    *len = n;
    return b;
}
static bool cc_layout_ok(const unsigned char *b, long n) {
    int count = *(const int *)(b + 4), pool = *(const int *)(b + 8);
    if (count <= 0 || count > 64 || pool <= 16 || pool >= n) return false;
    int rs = (pool - 16) / count;
    return (rs == 0x40 || rs == 0x4C) && 16 + rs * count == pool;
}
static void cc_parse_with(unsigned char *b) {                    // the game's own parse, the slot lent for one call
    ULONG_PTR *slot = (ULONG_PTR *)(g_base + 0x590190);
    ULONG_PTR old = *slot;
    *slot = (ULONG_PTR)b;
    ((void (*)())(g_base + 0x17d300))();
    *slot = old;
}
static void chara_catalog_check() {
    if (g_cc_done || !g_store_mode || !g_cat_done || g_in_match) return;
    static ULONGLONG t0;
    __try {
        if (!*(ULONG_PTR *)(g_base + 0x533c58)) return;            // the title's menus are not up yet
        if (!t0) { t0 = GetTickCount64(); return; }
        if (GetTickCount64() - t0 < 3000) return;
        g_cc_done = true;
        wchar_t exe[MAX_PATH], p[MAX_PATH], ov[MAX_PATH], sw[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wchar_t *sl = wcsrchr(exe, L'\\');
        if (sl) sl[1] = 0;
        swprintf_s(p, MAX_PATH, L"%sAssets\\Data\\setup\\charaset\\charaset_en.dat", exe);
        if (!sm_redirect(p, ov, MAX_PATH, false)) { logf("character catalog: no override file - the game's own (%d rows)", *(int *)(g_base + 0x4c0174)); return; }
        swprintf_s(sw, MAX_PATH, L"%sCwoTestCatalogStock.txt", g_dir);   // offline test switch: the stock one first, as a late session has it
        if (!ed_online() && GetFileAttributesW(sw) != INVALID_FILE_ATTRIBUTES) {
            long sn = 0;
            unsigned char *sb = cat_read(p, &sn);                  // the Store game folder itself (read only)
            if (sb && cc_layout_ok(sb, sn)) { cc_parse_with(sb); logf("character catalog: TEST switch - the stock catalog put in first (%d rows)", *(int *)(g_base + 0x4c0174)); }
        }
        int have = *(int *)(g_base + 0x4c0174);
        long n = 0;
        unsigned char *b = cat_read(ov, &n);
        if (!b || !cc_layout_ok(b, n)) { if (b) VirtualFree(b, 0, MEM_RELEASE); logf("character catalog: override not readable / layout refused"); return; }
        int count = *(int *)(b + 4), pool = *(int *)(b + 8);
        for (int r = 0; r < count; r++)                            // every name offset inside the file
            for (int k = 0; k < 3; k++) {
                int o = *(int *)(b + 16 + ((pool - 16) / count) * r + 4 * k);
                if (o < 0 || pool + o >= n) { VirtualFree(b, 0, MEM_RELEASE); logf("character catalog: override row %d name %d out of the file - refused", r, k); return; }
            }
        if (count == have) { VirtualFree(b, 0, MEM_RELEASE); logf("character catalog: the game read the override (%d rows)", have); return; }
        cc_parse_with(b);
        g_cc_buf = b;
        logf("character catalog: the game had read the stock file before the redirect (%d rows) - parsed the override (%d rows)", have, *(int *)(g_base + 0x4c0174));
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_cc_done = true; logf("character catalog: fault - left as the game read it"); }
}

// ---- v5.184.5 the CPU personality table (com\param\enemyparam.epa, CPU Levels rows 82-85; MODLOG 2026-10-26): its parse
// FUN_14017d1e0 only keeps the file buffer's pointer at exe+0x4c0188 (u32 row count, then 10-byte rows). After a late start
// it would be the stock file (82 rows): the loader points it at its own copy of the override when the counts differ.
static bool g_epa_done;
static void epa_check() {
    if (g_epa_done || !g_store_mode || !g_cc_done || g_in_match) return;
    g_epa_done = true;
    __try {
        unsigned char *cur = *(unsigned char **)(g_base + 0x4c0188);
        if (!cur) { logf("CPU table: not loaded yet - left"); return; }
        wchar_t exe[MAX_PATH], p[MAX_PATH], ov[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wchar_t *sl = wcsrchr(exe, L'\\');
        if (sl) sl[1] = 0;
        swprintf_s(p, MAX_PATH, L"%sAssets\\Data\\com\\param\\enemyparam.epa", exe);
        if (!sm_redirect(p, ov, MAX_PATH, false)) { logf("CPU table: no override file - the game's own (%u rows)", *(unsigned *)cur); return; }
        wchar_t sw[MAX_PATH];                                       // offline test switch: the stock table first, as a late session has it
        swprintf_s(sw, MAX_PATH, L"%sCwoTestCatalogStock.txt", g_dir);
        if (!ed_online() && GetFileAttributesW(sw) != INVALID_FILE_ATTRIBUTES) {
            long sn = 0;
            unsigned char *sb = cat_read(p, &sn);
            if (sb && sn >= 4) { InterlockedExchangePointer((PVOID *)(g_base + 0x4c0188), sb); cur = sb; logf("CPU table: TEST switch - the stock table put in first (%u rows)", *(unsigned *)sb); }
        }
        long n = 0;
        unsigned char *b = cat_read(ov, &n);
        unsigned want = b ? *(unsigned *)b : 0;
        if (!b || want == 0 || want > 100 || (long)(4 + 10 * want) > n) { if (b) VirtualFree(b, 0, MEM_RELEASE); logf("CPU table: override layout refused"); return; }
        if (*(unsigned *)cur == want && !memcmp(cur, b, 4 + 10 * want)) { VirtualFree(b, 0, MEM_RELEASE); logf("CPU table: the game read the override (%u rows)", want); return; }
        unsigned had = *(unsigned *)cur;
        InterlockedExchangePointer((PVOID *)(g_base + 0x4c0188), b);
        logf("CPU table: the game had read the stock file before the redirect (%u rows) - now the override (%u rows)", had, want);
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("CPU table: fault - left as the game read it"); }
}
