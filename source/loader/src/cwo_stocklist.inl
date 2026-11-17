// ---- v5.46 the stock list for skill ids above 500 (owner 2026-10-31).
// Game facts (PD Ghidra full pass 065 + capstone on PDUWP 9ba7411b, MODLOG 2026-11-01): the Lab's stock list is built by
// FUN_140172910 into 7 per-capsule-type lists of a count + 1,000 ids (Lab manager *0x1404c0140 + 5000, 0x7d4 each);
// its loop over ids 1-999 only takes an id when `id < 0x1f5`: `cmp r8d, 0x1f4; jg` at 0x1401729a9 (41 81 f8 f4 01 00 00).
// Nothing else bounds the ids (the only such compare in the exe); the lists hold 1,000 each.
// What this does: that one immediate becomes 0x3e7 (999) in memory (VirtualProtect + FlushInstructionCache, official
// Windows documentation), after the 7 bytes are checked to be the stock ones. No file changes; a restart undoes it.
static const ULONG_PTR RVA_STOCK_CMP = 0x1729a9;
static const unsigned char STOCK_CMP_OLD[7] = {0x41, 0x81, 0xF8, 0xF4, 0x01, 0x00, 0x00};
static const unsigned char STOCK_CMP_NEW[7] = {0x41, 0x81, 0xF8, 0xAF, 0x04, 0x00, 0x00};   // 1199 (v5.47)
static int g_stl_state;                                         // 0 not yet, 1 patched, -1 refused

static int stl_patch(unsigned char *at) {                       // the site (or a stand-in): 1 patched, 0 already, -1 refused
    if (!memcmp(at, STOCK_CMP_NEW, 7)) return 0;
    if (memcmp(at, STOCK_CMP_OLD, 7)) return -1;
    DWORD old;
    if (!VirtualProtect(at, 7, PAGE_EXECUTE_READWRITE, &old)) return -1;
    memcpy(at, STOCK_CMP_NEW, 7);
    VirtualProtect(at, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 7);
    return 1;
}

static void stl_apply() {                                       // worker start (after the Store-mode memory patch)
    int r = stl_patch((unsigned char *)(g_base + RVA_STOCK_CMP));
    g_stl_state = r >= 0 ? 1 : -1;
    if (r > 0) logf("stock list: ids up to 1199 listed (the id bound at %llx was 500)", (unsigned long long)(g_base + RVA_STOCK_CMP));
    else if (r < 0) logf("stock list: the bytes at %llx are not the stock compare - left alone", (unsigned long long)(g_base + RVA_STOCK_CMP));
}

static int stl_self_test() {
    unsigned char *buf = (unsigned char *)VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!buf) return -81;
    memcpy(buf, STOCK_CMP_OLD, 7);
    bool a = stl_patch(buf) == 1 && !memcmp(buf, STOCK_CMP_NEW, 7);
    bool b = stl_patch(buf) == 0;
    buf[0] = 0x90;
    bool c = stl_patch(buf) == -1;
    VirtualFree(buf, 0, MEM_RELEASE);
    logf("self-test: stock list patch %s, second apply %s, wrong bytes refused %s", a ? "ok" : "FAILED", b ? "ok" : "FAILED", c ? "ok" : "FAILED");
    return a && b && c ? 1 : -81;
}

// ---- v5.60 boss / campaign skills shown with 0 copies (owner 2026-11-02). Game facts (Ghidra full pass 065 pd_fn.py, capstone, 2026-11-02):
// FUN_140172910(lab, stock, lists, school counts, ?, by type) is called only from 0x1401597f7 and 0x140171033 (e8 rel32);
// for each record r = 1..999 it reads stock[r] (signed char) and takes r when stock[r] > 0 and the record's type
// (+0x0E) - 1 >= 0: into list (by type ? type - 1 : 0) = lists + list * 0x7d4 (int count, then 1,000 shorts) unless the
// school bit (*(lab + 0x4f6c) >> (+0x12 & 31) & 1) hides it, and school counts[(+0x12) - 1] + 1; records from *0x1404bdc50.
// Both calls go through a stub to stl_build: the locked records' stock becomes 0 (no copies to put in an arsenal), the
// game's list is built, then the locked records are added to their lists the same way, so they show with 0.
static const ULONG_PTR RVA_STL_FN = 0x172910, RVA_STL_CALL[2] = {0x1597f7, 0x171033}, RVA_SKILL_RECS = 0x4bdc50;
typedef void(__fastcall *PFN_STL)(INT64, signed char *, int *, int *, INT64, int);
static ULONG_PTR sixf2_near_alloc(SIZE_T n, DWORD prot);         // cwo_sixf2.inl (included later)
static int g_stl_lock_state;                                    // 1 the calls go to stl_build, -1 refused
static const char *g_stl_test_recs;                             // self-test: a stand-in record table

static int stl_add_locked(INT64 lab, signed char *stock, int *lists, int *counts, int by_type) {
    (void)stock;
    const char *recs = g_stl_test_recs ? g_stl_test_recs : *(const char **)(g_base + RVA_SKILL_RECS);
    if (!recs) return 0;
    int n = 0;
    for (int i = 0; i < g_bfr_lock_n; i++) {
        int r = g_bfr_lock[i];
        const char *rec = recs + 0x90 * (size_t)r;
        short type = *(const short *)(rec + 0x0E), school = *(const short *)(rec + 0x12);
        if (type - 1 < 0) continue;
        int li = by_type ? type - 1 : 0;
        if (li < 0 || li > 6) continue;
        int *cnt = lists + 0x1f5 * li;
        short *ids = (short *)(cnt + 1);
        bool dup = false;
        for (int k = 0; k < *cnt && k < 1000; k++) dup |= ids[k] == r;
        if (dup || *cnt >= 1000) continue;
        if (((*(unsigned *)(lab + 0x4f6c) >> (*(const unsigned char *)(rec + 0x12) & 0x1f)) & 1) == 0) { ids[*cnt] = (short)r; (*cnt)++; n++; }
        if (school - 1 >= 0 && school - 1 < 6) counts[school - 1]++;
    }
    return n;
}
// v5.87 (owner 2026-11-03):
// records 750 / 751 (the game's hidden Jump / Dash copies, shown ids 257 / 294 - cwo_bigskills.inl) are never listed:
// their stock is 0 when the game builds the lists, and any id 750 / 751 is taken out of every list afterwards
static int stl_drop_system(int *lists, int nlists) {
    int gone = 0;
    for (int li = 0; li < nlists; li++) {
        int *cnt = lists + 0x1f5 * li; short *ids = (short *)(cnt + 1);
        int w = 0;
        for (int k = 0; k < *cnt && k < 1000; k++) { if (ids[k] == 750 || ids[k] == 751) { gone++; continue; } ids[w++] = ids[k]; }
        *cnt = w;
    }
    return gone;
}
static void __fastcall stl_build(INT64 lab, signed char *stock, int *lists, int *counts, INT64 p5, int by_type) {
    __try { if (stock) { for (int i = 0; i < g_bfr_lock_n; i++) stock[g_bfr_lock[i]] = 0; stock[750] = 0; stock[751] = 0; } } __except (EXCEPTION_EXECUTE_HANDLER) {}
    ((PFN_STL)(g_base + RVA_STL_FN))(lab, stock, lists, counts, p5, by_type);
    __try { int g = stl_drop_system(lists, by_type ? 7 : 1); static bool said; if (g && !said) { said = true; logf("stock list: the hidden Jump / Dash copies (750 / 751) left out"); } } __except (EXCEPTION_EXECUTE_HANDLER) {}
    // v5.62 only the stock view (one list, by_type 0 - 0x140171033); Arsenal Edit's type lists (by_type 1 - 0x1401597f7) never
    // v5.186.9 (owner 2026-11-15, BFR View Stock: the blank 701-741 rows "won't be available ... only make the skills visible in the
    // vault"): the BFR edition leaves the locked skills out of View Stock - they show in the Skill Vault only (not swappable)
    __try { if (g_bfr_lock_n && !by_type && !g_bfr_edition) stl_add_locked(lab, stock, lists, counts, by_type); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void stl_lock_install() {                                 // worker start, after bfr_load
    if (g_stl_lock_state) return;                                   // v5.87 always (750 / 751), not only with locked skills
    unsigned char *site[2];
    for (int k = 0; k < 2; k++) {
        site[k] = (unsigned char *)(g_base + RVA_STL_CALL[k]);
        if (site[k][0] != 0xE8 || (ULONG_PTR)(site[k] + 5 + *(int *)(site[k] + 1)) != g_base + RVA_STL_FN) {
            logf("boss skills: the stock list call at %p is not the game's - left alone", (void *)site[k]);
            g_stl_lock_state = -1;
            return;
        }
    }
    ULONG_PTR stub = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!stub) { g_stl_lock_state = -1; return; }
    unsigned char *c = (unsigned char *)stub;
    c[0] = 0x48; c[1] = 0xB8; *(ULONGLONG *)(c + 2) = (ULONGLONG)(ULONG_PTR)stl_build; c[10] = 0xFF; c[11] = 0xE0;   // mov rax, stl_build; jmp rax
    FlushInstructionCache(GetCurrentProcess(), c, 12);
    for (int k = 0; k < 2; k++) {
        DWORD old;
        if (!VirtualProtect(site[k], 5, PAGE_EXECUTE_READWRITE, &old)) { g_stl_lock_state = -1; return; }
        *(int *)(site[k] + 1) = (int)(stub - (ULONG_PTR)(site[k] + 5));
        VirtualProtect(site[k], 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site[k], 5);
    }
    g_stl_lock_state = 1;
    logf("boss skills: %d locked - in the stock list with 0 copies, kept out of arsenals (stub %p)", g_bfr_lock_n, (void *)stub);
}
static int stl_lock_self_test() {                                // the add on stand-ins: type list, school filter, counts, no doubles
    static char recs[0x90 * 760];
    static int lists[7 * 0x1f5], counts[6];
    static unsigned char lab[0x5000];
    short keep[64]; int keep_n = g_bfr_lock_n; memcpy(keep, g_bfr_lock, sizeof(keep));
    memset(recs, 0, sizeof(recs)); memset(lists, 0, sizeof(lists)); memset(counts, 0, sizeof(counts)); memset(lab, 0, sizeof(lab));
    *(short *)(recs + 0x90 * 701 + 0x0E) = 1; *(short *)(recs + 0x90 * 701 + 0x12) = 2;       // attack, school 2
    *(short *)(recs + 0x90 * 735 + 0x0E) = 4; *(short *)(recs + 0x90 * 735 + 0x12) = 3;       // status, school 3 (hidden below)
    *(short *)(recs + 0x90 * 740 + 0x0E) = 0;                                                  // no type: never listed
    g_bfr_lock[0] = 701; g_bfr_lock[1] = 735; g_bfr_lock[2] = 740; g_bfr_lock_n = 3;
    *(unsigned *)(lab + 0x4f6c) = 1u << 3;
    g_stl_test_recs = recs;
    int n1 = stl_add_locked((INT64)(ULONG_PTR)lab, NULL, lists, counts, 1), n2 = stl_add_locked((INT64)(ULONG_PTR)lab, NULL, lists, counts, 1);
    g_stl_test_recs = NULL;
    bool ok = n1 == 1 && n2 == 0 && lists[0] == 1 && ((short *)(lists + 1))[0] == 701 && lists[3 * 0x1f5] == 0 &&
              counts[1] == 1 && counts[2] == 2;          // 701 counted once (a second build skips it); 735 hidden, counted each time
    memcpy(g_bfr_lock, keep, sizeof(keep)); g_bfr_lock_n = keep_n;
    logf("self-test: boss skills in the stock list %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -60;
}

// ---- v5.186.12 View Stock length (owner's BFR View Stock 2026-11-15: counter "1005 / 996", then page after page of
// "000 Aura Particle x79" after the last real skill). Game facts (Ghidra full pass 065 via pd_fn.py, 2026-11-15):
//  - FUN_140173650 (stock view open) sets Lab +0x4f8c = 300, then raises it to the highest id i (300 .. record count) whose
//    entry in the player's table (*(DAT_1404bec10 + player * 0x38 + 0x3a40) + i * 4, short at +2) is non-zero;
//  - FUN_1401742c0 draws the view: rows = +0x4f8c while (+0x4f90 - 0x100) & ~0x100 == 0, pages = (rows + 14) / 15, and prints
//    the counter "%d" (cells drawn) and "/%03d" (rows) from it;
//  - the rows show list 0 of the Lab (+5000: count, then ids) in order; past its count they show id 0.
// Memory read (BFR, the owner's profile): that table holds marks 1 / 2 on ids up to 996 (old test skills, removed imports),
// list 0 holds 473 skills -> 996 rows. So while the view is open the loader keeps the rows at max(300, list count), never
// above the game's own value, and the empty cells of the last page read id 750 (0 copies); the profile is not changed.
static void stl_rows_step() {
    static int s_written = -1, s_orig;
    __try {
        ULONG_PTR lab = *(ULONG_PTR *)(g_base + 0x4c0140);
        if (!lab) { s_written = -1; return; }
        int mode = *(int *)(lab + 0x4f90);
        if (((unsigned)(mode - 0x100) & 0xfffffeffu) != 0) { s_written = -1; return; }
        int *rows = (int *)(lab + 0x4f8c), cnt = *(int *)(lab + 5000);
        if (cnt < 0 || cnt > 1000 || *rows < 300 || *rows > 1199) return;
        if (*rows != s_written) s_orig = *rows;                   // the game (re)set it
        int want = cnt > 300 ? cnt : 300;
        if (want > s_orig) want = s_orig;
        if (*rows != want) {
            logf("stock view: rows %d -> %d (the list holds %d skills; marks on higher ids are not skills you own)", *rows, want, cnt);
            *rows = want;
            s_written = want;
        }
        // the counter's first number counts every cell of every page (FUN_1401742c0: pages * 15 list entries, an entry counts
        // when its stock is > 1, or 1 and marked) - the cells after the last skill read id 0 (Aura Particle, which players
        // own many of), so 473 skills read "480". Those cells get id 750 (the hidden Jump copy: stock 0, never listed or shown)
        short *ids = (short *)(lab + 5004);
        int end = ((*rows + 14) / 15) * 15;
        for (int k = cnt; k < end && k < 1000; k++) if (ids[k] == 0) ids[k] = 750;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
