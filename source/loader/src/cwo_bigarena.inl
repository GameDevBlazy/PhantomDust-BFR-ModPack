// ---- v5.48 a bigger stage collision arena (owner 2026-11-01).
// Game facts (Ghidra full pass 065 + capstone on PDUWP 9ba7411b; MODLOG 2026-11-01):
//  - FUN_1401d3b30 (stage load) sets the arena *0x140583558 = stage block + 0x1ee0000 (+0x1200000 in mode 2) and its
//    fill *0x140583560 = 0; FUN_1401d2c20 sizes the collision grid from the .dat's vertex chunks (cells 30 x 20 x 30,
//    (nx+2)(ny+2)(nz+2) x 8 bytes) and FUN_1401d28b0 / FUN_1401d29b0 / FUN_1401d3040 add 0x18-byte nodes, all from that
//    arena, each refusing past 0x200000 (2 MB; five `cmp reg, 0x200000`). A refused grid leaves the grid pointer null
//    and FUN_1401d29b0 then reads null + cell * 8: the 05:36:53 crash at exe+0x1d29bd reading 0x19908 (Mountainous
//    Region, mx108: 6.8 MB of grid). 10 of the 82 packs need more than 2 MB (mx219 9.0 MB).
// What this does (official Windows documentation only: VirtualAlloc, VirtualProtect, FlushInstructionCache): one 64 MB
// arena of our own; the base store's computation (0x1401d3bf0, 10 bytes: je / add rax, rcx / jmp / mov rax, rbx) becomes
// `mov rax, [cell]; nop x3` with the cell (near the exe) holding our arena, and the five limits become 64 MB. Every site
// is checked against the stock bytes first; any mismatch leaves all of them untouched. A restart undoes it.
static const SIZE_T BA_SIZE = 0x4000000;                        // 64 MB
enum { BA_IMM = 0, BA_CELL = 1 };
struct BaPatch { unsigned rva; unsigned char len; unsigned char old[10]; unsigned char at; unsigned char kind; };
static const BaPatch BA_PATCHES[] = {
    {0x1d3bf0, 10, {0x74, 0x05, 0x48, 0x03, 0xc1, 0xeb, 0x03, 0x48, 0x8b, 0xc3}, 3, BA_CELL},   // FUN_1401d3b30: base
    {0x1d28dd, 6, {0x48, 0x3d, 0x00, 0x00, 0x20, 0x00}, 2, BA_IMM},     // FUN_1401d28b0 x3
    {0x1d292b, 6, {0x48, 0x3d, 0x00, 0x00, 0x20, 0x00}, 2, BA_IMM},
    {0x1d2967, 6, {0x48, 0x3d, 0x00, 0x00, 0x20, 0x00}, 2, BA_IMM},
    {0x1d29dd, 6, {0x48, 0x3d, 0x00, 0x00, 0x20, 0x00}, 2, BA_IMM},     // FUN_1401d29b0
    {0x1d2fcf, 7, {0x48, 0x81, 0xfa, 0x00, 0x00, 0x20, 0x00}, 3, BA_IMM},   // FUN_1401d2c20 (the grid)
};
static const int BA_NPATCH = sizeof(BA_PATCHES) / sizeof(BA_PATCHES[0]);
static unsigned char *g_ba_cellpage, *g_ba_arena;
static int g_ba_state;                                          // 0 off, 1 on, -1 refused

static bool ba_alloc() {
    for (int k = 1; k <= 64 && !g_ba_cellpage; k++) {
        ULONG_PTR hint = (g_base + 0x5000000 + 0x10000 * (ULONG_PTR)k) & ~(ULONG_PTR)0xffff;
        g_ba_cellpage = (unsigned char *)VirtualAlloc((void *)hint, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    if (!g_ba_cellpage) return false;
    LONGLONG d = (LONGLONG)(g_ba_cellpage - (unsigned char *)g_base);
    if (d > 0x70000000LL || d < -0x70000000LL) { VirtualFree(g_ba_cellpage, 0, MEM_RELEASE); g_ba_cellpage = NULL; return false; }
    if (!g_ba_arena) g_ba_arena = (unsigned char *)VirtualAlloc(NULL, BA_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_ba_arena) return false;
    *(unsigned char **)g_ba_cellpage = g_ba_arena;
    return true;
}

static void ba_new_bytes(const BaPatch &p, ULONG_PTR site, unsigned char *out) {
    if (p.kind == BA_CELL) {                                    // mov rax, [rip + disp32]; nop; nop; nop
        static const unsigned char mov[3] = {0x48, 0x8b, 0x05};
        memcpy(out, mov, 3);
        int disp = (int)(LONGLONG)((ULONG_PTR)g_ba_cellpage - (site + 7));
        memcpy(out + 3, &disp, 4);
        memset(out + 7, 0x90, p.len - 7);
    } else {
        memcpy(out, p.old, p.len);
        unsigned v = (unsigned)BA_SIZE;
        memcpy(out + p.at, &v, 4);
    }
}

static int ba_patch_all(ULONG_PTR base, bool standin) {
    for (int i = 0; i < BA_NPATCH; i++)
        if (memcmp((void *)(base + BA_PATCHES[i].rva), BA_PATCHES[i].old, BA_PATCHES[i].len)) return -(i + 1);
    for (int i = 0; i < BA_NPATCH; i++) {
        const BaPatch &p = BA_PATCHES[i];
        unsigned char nb[10];
        ba_new_bytes(p, g_base + p.rva, nb);
        unsigned char *at = (unsigned char *)(base + p.rva);
        DWORD old;
        if (!standin && !VirtualProtect(at, p.len, PAGE_EXECUTE_READWRITE, &old)) return -100 - i;
        memcpy(at, nb, p.len);
        if (!standin) { VirtualProtect(at, p.len, old, &old); FlushInstructionCache(GetCurrentProcess(), at, p.len); }
    }
    return 1;
}

static void ba_apply() {                                        // worker start, before any stage loads
    if (!ba_alloc()) { g_ba_state = -1; logf("big arena: no memory - stage collision keeps the 2 MB arena"); return; }
    int r = ba_patch_all(g_base, false);
    if (r != 1) { g_ba_state = -1; logf("big arena: site %d is not the stock code (%d) - nothing changed", r < -99 ? -100 - r : -r - 1, r); return; }
    g_ba_state = 1;
    logf("big arena: stage collision arena %p (%u MB, was 2 MB inside the stage block), %d sites patched", (void *)g_ba_arena,
         (unsigned)(BA_SIZE >> 20), BA_NPATCH);
}

static int ba_self_test() {                                     // the patch table on a stand-in of the sites
    unsigned char *img = (unsigned char *)VirtualAlloc(NULL, 0x200000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!img) return -83;
    for (int i = 0; i < BA_NPATCH; i++) memcpy(img + BA_PATCHES[i].rva, BA_PATCHES[i].old, BA_PATCHES[i].len);
    if (!g_ba_cellpage && !ba_alloc()) { VirtualFree(img, 0, MEM_RELEASE); return -83; }
    int r = ba_patch_all((ULONG_PTR)img, true);
    bool ok = r == 1;
    for (int i = 0; ok && i < BA_NPATCH; i++) {
        const BaPatch &p = BA_PATCHES[i];
        unsigned char *at = img + p.rva;
        if (p.kind == BA_IMM) { unsigned v; memcpy(&v, at + p.at, 4); ok = v == (unsigned)BA_SIZE; }
        else {
            int disp; memcpy(&disp, at + 3, 4);
            ok = at[0] == 0x48 && at[1] == 0x8b && at[2] == 0x05 && (ULONG_PTR)(g_base + p.rva + 7 + disp) == (ULONG_PTR)g_ba_cellpage
                 && at[7] == 0x90 && at[8] == 0x90 && at[9] == 0x90 && *(unsigned char **)g_ba_cellpage == g_ba_arena;
        }
    }
    img[BA_PATCHES[2].rva] ^= 0xff;                             // a changed site: refused, nothing written
    for (int i = 0; i < BA_NPATCH; i++) if (i != 2) memcpy(img + BA_PATCHES[i].rva, BA_PATCHES[i].old, BA_PATCHES[i].len);
    bool refused = ba_patch_all((ULONG_PTR)img, true) == -3 && !memcmp(img + BA_PATCHES[0].rva, BA_PATCHES[0].old, BA_PATCHES[0].len);
    VirtualFree(img, 0, MEM_RELEASE);
    logf("self-test: big arena %d sites %s (%d), changed site refused %s", BA_NPATCH, ok ? "ok" : "FAILED", r, refused ? "ok" : "FAILED");
    return ok && refused ? 1 : -83;
}
