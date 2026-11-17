// ---- v5.164.0 the school limit counts the Illusion school (owner 2026-11-13 quality task: arsenal creation and editing).
// Game facts (MODLOG 2026-11-13: capstone on the clean exe 9ba7411b, ssb_disasm of Menu\arsenaledit_en.ssb, the native
// table 0x1402ddd00 -> group 5 at 0x1403f1d50):
//  - Edit Arsenal's check (arsenaledit script 0x6d78) calls native 5.31 = 0x140216cc0: edit = *0x1404bec08; n = the number of
//    schools 1-5 with a count (edit +0x54 .. +0x64 > 0); returns n > the case's limit *(short *)(*(edit + 0x48) + 0x50);
//    1 = "This arsenal has skills from too many schools. You are limited to %d schools." (the limit from native 5.127);
//  - the edit's school counts are edit +0x50 + school * 4 (FUN_140159db0), so school 6 lands on +0x68, the type-0 count:
//    an Illusion skill never counted as a school - a 1-school case (aura x1.3) could hold Psycho + Illusion.
// The loader's native 5.31 counts schools 1-5 the game's way and school 6 from the arsenal's own 30 numbers (record +0x14,
// skill table *0x1404bdc50 + id * 0x90, +0x12 == 6), then compares with the same limit. Online: the game's own native.
static const ULONG_PTR RVA_NATIVES_G5 = 0x3f1d50, RVA_N531 = 0x216cc0, RVA_SLIM_EDIT = 0x4bec08, RVA_SLIM_SKILLS = 0x4bdc50;
static const int SLIM_NATIVE = 31;
typedef ULONG_PTR(__fastcall *PFN_SLIM)(void *);
static PFN_SLIM g_slim_orig;
static bool g_slim_ok, g_slim_test;
static bool ed_online();

static int slim_schools(ULONG_PTR edit, ULONG_PTR skills) {       // the number of schools in the arsenal being edited
    int n = 0;
    for (int s = 1; s <= 5; s++) if (*(int *)(edit + 0x50 + 4 * s) > 0) n++;
    const short *ids = (const short *)(*(ULONG_PTR *)(edit + 0x48) + 0x14);
    for (int k = 0; k < 30; k++) {
        int id = ids[k];
        if (id < 753 || !(g_slim_test || bs_listed(id))) continue;   // schools 1-5 are already counted; 753+ only the loader's own lists
        if (*(short *)(skills + 0x12 + (ULONG_PTR)id * 0x90) == 6) { n++; break; }
    }
    return n;
}

static ULONG_PTR __fastcall slim_native(void *args) {             // native 5.31 (game thread, Edit Arsenal)
    if (!g_slim_test && ed_online()) return g_slim_orig(args);
    __try {
        ULONG_PTR edit = *(ULONG_PTR *)(g_base + RVA_SLIM_EDIT), skills = *(ULONG_PTR *)(g_base + RVA_SLIM_SKILLS);
        if (!edit || !skills || !*(ULONG_PTR *)(edit + 0x48)) return g_slim_test ? 0 : g_slim_orig(args);
        int n = slim_schools(edit, skills), lim = *(short *)(*(ULONG_PTR *)(edit + 0x48) + 0x50);
        static int said;
        if (said < 6 && !g_slim_test) { said++; logf("school limit: this arsenal has %d school(s) (Illusion counted), its case allows %d - %s", n, lim, n > lim ? "too many" : "ok"); }
        return n > lim ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_slim_test ? 0 : g_slim_orig(args);
}

static void slim_install() {
    void **slot = (void **)(g_base + RVA_NATIVES_G5 + 8 * SLIM_NATIVE);
    __try {
        if (*slot != (void *)(g_base + RVA_N531)) { logf("school limit: refused, native 5.31 holds %p", *slot); return; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    g_slim_orig = (PFN_SLIM)*slot;
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return;
    *slot = (void *)slim_native;
    VirtualProtect(slot, sizeof(void *), old, &old);
    g_slim_ok = true;
    logf("school limit: Edit Arsenal's school check (native 5.31) counts the Illusion school too");
}

// offline test on a stand-in image: Psycho + Illusion in a 1-school case = too many; Illusion alone = ok; Psycho + Optical
// in a 2-school case = ok, + Illusion = too many; a number past the skill table is not read
static int slim_self_test() {
    const size_t SZ = RVA_SLIM_EDIT + 0x10;
    unsigned char *img = (unsigned char *)VirtualAlloc(NULL, SZ, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    unsigned char *edit = (unsigned char *)VirtualAlloc(NULL, 0x200, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    unsigned char *rec = (unsigned char *)VirtualAlloc(NULL, 0x100, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    unsigned char *tab = (unsigned char *)VirtualAlloc(NULL, 0x90 * 830, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!img || !edit || !rec || !tab) return -100;
    ULONG_PTR keep = g_base; bool kt = g_slim_test;
    g_base = (ULONG_PTR)img; g_slim_test = true;
    *(ULONG_PTR *)(img + RVA_SLIM_EDIT) = (ULONG_PTR)edit; *(ULONG_PTR *)(img + RVA_SLIM_SKILLS) = (ULONG_PTR)tab;
    *(ULONG_PTR *)(edit + 0x48) = (ULONG_PTR)rec;
    *(short *)(tab + 0x12 + 10 * 0x90) = 1; *(short *)(tab + 0x12 + 20 * 0x90) = 2; *(short *)(tab + 0x12 + 760 * 0x90) = 6;
    auto set = [&](int lim, int c1, int c2, std::initializer_list<int> ids) {
        memset(edit + 0x50, 0, 0x40); for (int k = 0; k < 30; k++) *(short *)(rec + 0x14 + 2 * k) = -1;
        *(int *)(edit + 0x54) = c1; *(int *)(edit + 0x58) = c2; *(short *)(rec + 0x50) = (short)lim;
        int k = 0; for (int id : ids) { *(short *)(rec + 0x14 + 2 * k++) = (short)id; if (id >= 753) *(int *)(edit + 0x68) += 1; }
    };
    bool ok = true;
    set(1, 3, 0, {10, 10, 10, 760}); ok = ok && slim_native(NULL) == 1;     // Psycho + Illusion, 1 school
    set(1, 0, 0, {760, 760});        ok = ok && slim_native(NULL) == 0;     // Illusion alone
    set(2, 2, 1, {10, 10, 20});      ok = ok && slim_native(NULL) == 0;     // Psycho + Optical, 2 schools
    set(2, 2, 1, {10, 10, 20, 760}); ok = ok && slim_native(NULL) == 1;     // + Illusion = 3
    set(3, 2, 1, {10, 20, 760});     ok = ok && slim_native(NULL) == 0;     // 3 in a 3-school case
    g_base = keep; g_slim_test = kt;
    VirtualFree(img, 0, MEM_RELEASE); VirtualFree(edit, 0, MEM_RELEASE); VirtualFree(rec, 0, MEM_RELEASE); VirtualFree(tab, 0, MEM_RELEASE);
    logf("self-test: the school limit counts the Illusion school %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -101;
}

// ---- v5.166.0 the MASK window's Illusion row + the picker's school row with six icons (owner 2026-11-13).
// (a) build_ars_mask6.py gives Edit Arsenal's MASK a 6th row 'Illusion'; the script keeps rows 0-4's Display / Hide in its
//     own 20-word array (no slot free), row 5's is kept here: native 2.8 op 121 = get, op 122 = set (1 Hide, 2 Display, the
//     script's own values). The bit itself is the game's: 5.116 / 5.117 with school 6 (lab +0x4f6c bit 6), which the stock
//     list FUN_140172910 already honours.
static volatile LONG g_mask6_row = 2;
static INT64 mask6_native(int op, INT64 *args) {
    if (op == 121) return g_mask6_row;
    INT64 v = 2;
    __try { v = args[1]; } __except (EXCEPTION_EXECUTE_HANDLER) { v = 2; }
    InterlockedExchange(&g_mask6_row, v == 1 ? 1 : 2);
    return 0;
}

// v5.178.0 (owner 2026-11-13): the MASK window's Illusion row draws
// its icon. Game facts (capstone on the clean exe 9ba7411b, 2026-11-13): draw_attmsk (arsenaledit 0x2ba8) draws rows 0-4 with
// 2.159(slot 0, sprite 11..15, x, y, colour) = 0x1402077a0 -> FUN_140246c70 -> FUN_1401983e0(.., table[sprite], x, y, 0, 0, 0,
// &colour, 0, 0); the picker draws the Lab's school marks with the same call on the Lab's table *(lab + 0x470) (0x14017234d);
// the Lab's sprite 19 holds the Illusion mark (cwo_school6.inl). Op 123(x, y, colour) = that call for sprite 19; offline only
// (online the row keeps no icon, as before).
typedef void (__fastcall *PFN_SPRDRAW_M6)(ULONG_PTR menu, unsigned handle, int x, int y, int p, float *sx, float *sy, unsigned *colour,
                                         void *p9, void *p10);   // = cwo_selgrid.inl PFN_SPRDRAW (included later)
static INT64 mask6_icon_native(INT64 *args) {
    if (ed_online()) return 0;
    __try {
        // v5.179.1 (in game: the Lab mark is ~19 x 10 against the MASK icons' ~12 x 11 and took the row's teal text colour,
        // while the five keep their own colours) -> 80 %, 1 lower, in the Illusion school's smoke silver
        int x = (int)args[1], y = (int)args[2] + 1; unsigned col = 0xFFD8DCE4; (void)args[3];
        float sx = 0.8f, sy = 0.8f;
        ULONG_PTR lab = *(ULONG_PTR *)(g_base + 0x4c0140);
        if (!lab) return 0;
        const unsigned *tab = *(const unsigned **)(lab + 0x470);
        ULONG_PTR menu = *(ULONG_PTR *)(g_base + 0x533c58);           // the first argument as 2.159 passes it (cwo_selgrid.inl)
        if (!tab || !menu) return 0;
        ((PFN_SPRDRAW_M6)(g_base + 0x1983e0))(menu, tab[19], x, y, 0, &sx, &sy, &col, NULL, NULL);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

// (b) Game facts (capstone on the clean exe 9ba7411b, Ghidra full pass 065 FUN_140171970; memory read 13 Nov: the Lab's mode
//     lab +0x12d8 = 2 while Edit Arsenal's skill picker is open): the picker's school row is drawn by the loop at
//     0x1401722d7..0x14017235e: r14d = r13d + 0x14 (x), rbp = &0x1402f2b10 (sprite table 21 20 18 16 17 = the Lab's school
//     marks), r15d = 5 (count), per icon: rol ebx,1 (bit 1..5), colour 0xff5aa992 or 0xff3c3c3c when lab +0x4f6c has the bit,
//     FUN_1401983e0(sprite, *(lab +0x470)[sprite], x, y), add r14d, 0x28. Sprite 19 is the Lab mark the loader paints the
//     Illusion mark into (cwo_school6.inl). Six icons: count 6, our table 21 20 18 16 17 19 (near the exe for the rip-relative
//     lea), step 34 and 5 units further left - the six take the width of the five (+5). Offline only: online the game's bytes.
static const ULONG_PTR RVA_SR_X = 0x1722d7, RVA_SR_TAB = 0x1722e0, RVA_SR_N = 0x1722ea, RVA_SR_STEP = 0x172352;
static const unsigned char SR_X0[] = {0x45, 0x8d, 0x75, 0x14}, SR_TAB0[] = {0x48, 0x8d, 0x2d, 0x29, 0x08, 0x18, 0x00},
                           SR_N0[] = {0x41, 0xbf, 0x05, 0x00, 0x00, 0x00}, SR_STEP0[] = {0x41, 0x83, 0xc6, 0x28};
static int g_sr_state;                                          // 0 not set up, 1 ready, -1 refused
static bool g_sr_on;
static unsigned char g_sr_x1[4], g_sr_tab1[7], g_sr_n1[6], g_sr_step1[4];

static bool sr_write(ULONG_PTR rva, const unsigned char *b, size_t n) {
    DWORD old;
    if (!VirtualProtect((void *)(g_base + rva), n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void *)(g_base + rva), b, n);
    VirtualProtect((void *)(g_base + rva), n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(g_base + rva), n);
    return true;
}

static void sr_step() {                                         // game thread (CwoOverlayFrame), where the Lab draws
    if (g_sr_state == 0) {
        g_sr_state = -1;
        if (memcmp((void *)(g_base + RVA_SR_X), SR_X0, 4) || memcmp((void *)(g_base + RVA_SR_TAB), SR_TAB0, 7) ||
            memcmp((void *)(g_base + RVA_SR_N), SR_N0, 6) || memcmp((void *)(g_base + RVA_SR_STEP), SR_STEP0, 4)) {
            logf("school row: the picker's icon loop is not the expected code - five icons as the game draws them");
            return;
        }
        int *tab = (int *)sixf2_near_alloc(0x1000, PAGE_READWRITE);
        if (!tab) { logf("school row: no memory within reach - five icons"); return; }
        static const int T[6] = {21, 20, 18, 16, 17, 19};
        memcpy(tab, T, sizeof(T));
        long long d = (long long)(ULONG_PTR)tab - (long long)(g_base + RVA_SR_TAB + 7);
        if (d < INT_MIN || d > INT_MAX) { logf("school row: table out of reach - five icons"); return; }
        memcpy(g_sr_x1, SR_X0, 4); g_sr_x1[3] = 0x0f;
        memcpy(g_sr_tab1, SR_TAB0, 3); int rel = (int)d; memcpy(g_sr_tab1 + 3, &rel, 4);
        memcpy(g_sr_n1, SR_N0, 6); g_sr_n1[2] = 0x06;
        memcpy(g_sr_step1, SR_STEP0, 4); g_sr_step1[3] = 0x22;
        g_sr_state = 1;
    }
    if (g_sr_state != 1) return;
    bool want = !ed_online();
    if (want == g_sr_on) return;
    bool ok = sr_write(RVA_SR_X, want ? g_sr_x1 : SR_X0, 4) && sr_write(RVA_SR_TAB, want ? g_sr_tab1 : SR_TAB0, 7) &&
              sr_write(RVA_SR_N, want ? g_sr_n1 : SR_N0, 6) && sr_write(RVA_SR_STEP, want ? g_sr_step1 : SR_STEP0, 4);
    g_sr_on = want;
    logf("school row: Edit Arsenal's picker shows %s school icons%s", want ? "six (Illusion last)" : "the game's five", ok ? "" : " (a write failed)");
}
