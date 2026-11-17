// ---- v5.47 skill ids above 999 (owner 2026-10-31: find extra memory if needed).
// Game facts (PD Ghidra full pass 065 + capstone on PDUWP 9ba7411b; MODLOG 2026-11-01): the skill-id space is 1,000 wide in
// these places, and nowhere else:
//  - two id maps of 1,000 ints (first / last record for an id) at 0x1404bcca0 and 0x1404bdc60, directly followed by the
//    table pointers; 6 instructions take their base (lea rip-relative x5, one `[rcx + rax*4 + rva]` with rcx = image base);
//    FUN_14017dcb0 fills them from the records (map[id] = index) after clearing 1,000 entries of the first;
//  - the profile's stock bytes (1 per id) and supplemental entries (4 per id) are inside the save block (+0x644, +0xa2c);
//    the game reaches them through per-player pointers prof+0x3a38 / +0x3a40 (+0x38 per player), set by FUN_14015bcd0;
//  - the Lab manager (0x4fb8 bytes, FUN_14015e780) keeps a stock copy of 1,000 bytes at +0x4a88 (13 instructions, one
//    of them `lea rdx, [rbx+0x4a89]` - found by the 5.47.2 write watch after 5.47.0 crashed in the Lab);
//  - loops and clamps with 1000 / 999 / 998 in FUN_140172910, FUN_14015bdd0, FUN_1400bb6c0, FUN_140162b70,
//    FUN_140186ac0/b70/c30, FUN_1401c3de0 and FUN_140249e40 (the Lab copy's memset and fill).
// What this does (official Windows documentation only: VirtualAlloc, VirtualProtect, FlushInstructionCache):
//  - one block near the exe holds two maps of 1,200 ints and, per player slot, 1,200 stock bytes + 1,200 x 4 supplemental
//    bytes; the 6 map instructions are re-aimed at our maps; the 16 bounds become 1,200 / 1,199 / 1,198; the Lab object
//    grows to 0x5470 and its stock copy moves to +0x4fb8 (12 instructions), so ids to 1,199 fit everywhere;
//  - each frame, a player's stock / supplemental pointers that point into the save block are switched to our arrays (the
//    block's 1,000 / 4,000 bytes copied in first), and our first 1,000 / 4,000 bytes are copied back into the block, so
//    saves keep every change; ids 1,000-1,199 are never saved (the loader restocks them at every start).
// All patches are checked against the stock bytes first; any mismatch leaves everything untouched. A restart undoes it.
static const int BI_N = 1200, BI_PLAYERS = 4;
static const ULONG_PTR BI_LAB_SIZE_NEW = 0x5500, BI_LAB_STOCK_NEW = 0x4fb8;
enum { BI_IMM = 0, BI_RIP_A = 1, BI_RIP_B = 2, BI_BASE_A = 3 };
struct BiPatch { unsigned rva; unsigned char len; unsigned char old[10]; unsigned char at; unsigned char kind; unsigned value; };
static const BiPatch BI_PATCHES[] = {
    // id maps
    {0x15be37, 7, {0x4c, 0x8d, 0x25, 0x62, 0x0e, 0x36, 0x00}, 3, BI_RIP_A, 0},          // FUN_14015bdd0 lea r12, map A
    {0x0bef59, 7, {0x48, 0x8d, 0x0d, 0x40, 0xdd, 0x3f, 0x00}, 3, BI_RIP_A, 0},          // FUN_1400bef20 lea rcx, map A
    {0x17dd90, 7, {0x4c, 0x8d, 0x1d, 0x09, 0xef, 0x33, 0x00}, 3, BI_RIP_A, 0},          // FUN_14017dcb0 lea r11, map A (clear + fill)
    {0x17dd26, 7, {0x4c, 0x8d, 0x3d, 0x33, 0xff, 0x33, 0x00}, 3, BI_RIP_B, 0},          // FUN_14017dcb0 lea r15, map B (fill)
    {0x27490e, 7, {0x48, 0x8d, 0x3d, 0x4b, 0x93, 0x24, 0x00}, 3, BI_RIP_B, 0},          // FUN_140274900 lea rdi, map B
    {0x0bb9ac, 8, {0x48, 0x63, 0x94, 0x81, 0xa0, 0xcc, 0x4b, 0x00}, 4, BI_BASE_A, 0},   // FUN_1400bb6c0 movsxd rdx, [base + id*4 + map A]
    // bounds 1000 -> 1200, 999 -> 1199, 998 -> 1198
    {0x172a45, 7, {0x41, 0x81, 0xf8, 0xe8, 0x03, 0x00, 0x00}, 3, BI_IMM, 1200},        // FUN_140172910 loop end
    {0x15be05, 7, {0x41, 0x81, 0xf8, 0xe8, 0x03, 0x00, 0x00}, 3, BI_IMM, 1200},        // FUN_14015bdd0
    {0x15be13, 5, {0xb8, 0xe7, 0x03, 0x00, 0x00}, 1, BI_IMM, 1199},
    {0x15be47, 5, {0x3d, 0xe6, 0x03, 0x00, 0x00}, 1, BI_IMM, 1198},
    {0x0bb995, 6, {0x81, 0xfd, 0xe8, 0x03, 0x00, 0x00}, 2, BI_IMM, 1200},              // FUN_1400bb6c0
    {0x0bb99b, 5, {0xb9, 0xe7, 0x03, 0x00, 0x00}, 1, BI_IMM, 1199},
    {0x0bb9b7, 5, {0x3d, 0xe6, 0x03, 0x00, 0x00}, 1, BI_IMM, 1198},
    {0x162c36, 5, {0xbf, 0xe8, 0x03, 0x00, 0x00}, 1, BI_IMM, 1200},                    // FUN_140162b70 x2
    {0x162cde, 5, {0xbf, 0xe8, 0x03, 0x00, 0x00}, 1, BI_IMM, 1200},
    {0x186b35, 7, {0x48, 0x81, 0xf9, 0xe8, 0x03, 0x00, 0x00}, 3, BI_IMM, 1200},        // FUN_140186ac0
    {0x186bf1, 7, {0x49, 0x81, 0xf9, 0xe8, 0x03, 0x00, 0x00}, 3, BI_IMM, 1200},        // FUN_140186b70
    {0x186cb1, 7, {0x49, 0x81, 0xf9, 0xe8, 0x03, 0x00, 0x00}, 3, BI_IMM, 1200},        // FUN_140186c30
    {0x1c3e78, 7, {0x49, 0x81, 0xf9, 0xe8, 0x03, 0x00, 0x00}, 3, BI_IMM, 1200},        // FUN_1401c3de0
    {0x249e82, 6, {0x41, 0xb8, 0xe8, 0x03, 0x00, 0x00}, 2, BI_IMM, 1200},              // FUN_140249e40 memset length
    {0x249f0d, 7, {0x41, 0x81, 0xf8, 0xe7, 0x03, 0x00, 0x00}, 3, BI_IMM, 1199},        // FUN_140249e40 id bound
    // the Lab manager: size and the stock copy's offset
    {0x15e7b3, 5, {0xba, 0xb8, 0x4f, 0x00, 0x00}, 1, BI_IMM, (unsigned)BI_LAB_SIZE_NEW},         // FUN_14015e780 operator new
    {0x15e7d7, 6, {0x41, 0xb8, 0xb8, 0x4f, 0x00, 0x00}, 2, BI_IMM, (unsigned)BI_LAB_SIZE_NEW},   // FUN_14015e780 memset
    {0x167d86, 5, {0xba, 0xb8, 0x4f, 0x00, 0x00}, 1, BI_IMM, (unsigned)BI_LAB_SIZE_NEW},         // FUN_140167d00 sized delete
    {0x158e7d, 7, {0x48, 0x81, 0xc6, 0x88, 0x4a, 0x00, 0x00}, 3, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},        // FUN_140158e10 add rsi
    {0x159797, 8, {0x49, 0x8d, 0x94, 0x24, 0x88, 0x4a, 0x00, 0x00}, 4, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},  // FUN_1401596c0 lea rdx
    {0x170e9e, 7, {0x48, 0x8d, 0x8b, 0x88, 0x4a, 0x00, 0x00}, 3, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},        // FUN_140170e40 lea rcx
    {0x170fdd, 7, {0xfe, 0x8c, 0x19, 0x88, 0x4a, 0x00, 0x00}, 3, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},        // FUN_140170e40 dec byte
    {0x171004, 7, {0x48, 0x8d, 0x93, 0x88, 0x4a, 0x00, 0x00}, 3, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},        // FUN_140170e40 lea rdx
    {0x170f54, 7, {0x48, 0x8d, 0x93, 0x89, 0x4a, 0x00, 0x00}, 3, BI_IMM, (unsigned)BI_LAB_STOCK_NEW + 1},    // FUN_140170e40 lea rdx, copy+1 (5.47.3: the 5.47.0 crash - inc byte loop wrote past the old copy)
    {0x172daa, 8, {0x0f, 0xbe, 0x94, 0x37, 0x88, 0x4a, 0x00, 0x00}, 4, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},  // FUN_140172a80 movsx
    {0x17306c, 9, {0x44, 0x0f, 0xbe, 0xa4, 0x37, 0x88, 0x4a, 0x00, 0x00}, 5, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},
    {0x173453, 8, {0x0f, 0xbe, 0x8c, 0x30, 0x88, 0x4a, 0x00, 0x00}, 4, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},
    {0x216c1d, 7, {0x28, 0x84, 0x0a, 0x88, 0x4a, 0x00, 0x00}, 3, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},        // FUN_140216b60 sub byte
    {0x216c3a, 8, {0x0f, 0xbe, 0x84, 0x02, 0x88, 0x4a, 0x00, 0x00}, 4, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},  // FUN_140216b60 movsx
    {0x249e94, 7, {0x48, 0x8d, 0x8e, 0x88, 0x4a, 0x00, 0x00}, 3, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},        // FUN_140249e40 lea rcx
    {0x249f1b, 8, {0x41, 0x88, 0x8c, 0x30, 0x88, 0x4a, 0x00, 0x00}, 4, BI_IMM, (unsigned)BI_LAB_STOCK_NEW},  // FUN_140249e40 mov byte
    // 5.48.0: 9 blocks -> 1,256 bytes, so every id to 1,199 reaches the copies (the Lab object grows to 0x5500: 0x4fb8 +
    // 1,256 = 0x54a0; the arsenal scratch is a 0x5000 block with nothing else below +0x2800).
    // 5.47.4: the three unrolled 1,000-byte stock copies (7 x 128 + 104) run 8 blocks -> 1,128 bytes: ids to 1,127 reach the
    // Lab copy (FUN_140170e40, FUN_140158e10) and the arsenal-usage scratch (FUN_14015aea0); the loader keeps 1,200 bytes
    // per player slot, so the longer copy stays inside our arrays and inside the Lab object (0x4fb8 + 1128 < 0x5470)
    {0x170e83, 6, {0x41, 0xb8, 0x07, 0x00, 0x00, 0x00}, 2, BI_IMM, 9},                 // FUN_140170e40 mov r8d, 7
    {0x15aea8, 5, {0xba, 0x07, 0x00, 0x00, 0x00}, 1, BI_IMM, 9},                       // FUN_14015aea0 mov edx, 7
    {0x158e84, 5, {0xb9, 0x07, 0x00, 0x00, 0x00}, 1, BI_IMM, 9},                       // FUN_140158e10 mov ecx, 7
};
static const int BI_NPATCH = sizeof(BI_PATCHES) / sizeof(BI_PATCHES[0]);

static unsigned char *g_bi_mem;                                 // near-exe block
static int *g_bi_mapA, *g_bi_mapB;                              // 1,200 ints each
static unsigned char *g_bi_stock[BI_PLAYERS];                   // 1,200 bytes per player slot
static unsigned char *g_bi_supp[BI_PLAYERS];                    // 1,200 x 4 bytes per player slot
static unsigned char *g_bi_src_stock[BI_PLAYERS], *g_bi_src_supp[BI_PLAYERS];   // the save-block bytes each slot mirrors
static int g_bi_state;                                          // 0 off, 1 on, -1 refused
static int g_bi_mirrors;

static bool bi_alloc() {
    size_t n = (size_t)BI_N * 4 * 2 + (size_t)BI_PLAYERS * (BI_N + BI_N * 4);
    for (int k = 1; k <= 32 && !g_bi_mem; k++) {
        ULONG_PTR hint = (g_base + 0x3000000 * (ULONG_PTR)k) & ~(ULONG_PTR)0xffff;
        g_bi_mem = (unsigned char *)VirtualAlloc((void *)hint, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    if (!g_bi_mem) return false;
    LONGLONG d = (LONGLONG)(g_bi_mem - (unsigned char *)g_base);
    if (d > 0x70000000LL || d < -0x70000000LL) { VirtualFree(g_bi_mem, 0, MEM_RELEASE); g_bi_mem = NULL; return false; }
    g_bi_mapA = (int *)g_bi_mem;
    g_bi_mapB = g_bi_mapA + BI_N;
    unsigned char *p = (unsigned char *)(g_bi_mapB + BI_N);
    for (int k = 0; k < BI_PLAYERS; k++) { g_bi_stock[k] = p; p += BI_N; g_bi_supp[k] = p; p += BI_N * 4; }
    for (int i = 0; i < BI_N; i++) g_bi_mapA[i] = g_bi_mapB[i] = -1;
    return true;
}

// the patch bytes for one site (into `out`, len bytes): the old bytes with the field replaced
static void bi_new_bytes(const BiPatch &p, ULONG_PTR site, unsigned char *out) {
    memcpy(out, p.old, p.len);
    unsigned v = p.value;
    if (p.kind == BI_RIP_A || p.kind == BI_RIP_B) v = (unsigned)(LONGLONG)((ULONG_PTR)(p.kind == BI_RIP_A ? g_bi_mapA : g_bi_mapB) - (site + p.len));
    else if (p.kind == BI_BASE_A) v = (unsigned)(LONGLONG)((ULONG_PTR)g_bi_mapA - g_base);
    memcpy(out + p.at, &v, 4);
}

// apply every patch to code at `base` (the exe, or a stand-in copy of the sites laid out at the same RVAs): 1 ok, -1 refused
// v5.55.1 (owner 2026-11-01 22:50: crash entering the campaign Lab; log 22:49 "lab size: ... block 0x4fe0 (SMALLER)"): when the
// loader is late-loaded after the game has already made the Lab manager (FUN_14015e780, arena block 0x4fb8 + 0x28), the
// manager keeps the stock size, and the 1,256-byte stock copy at +0x4fb8 runs over the mission menu object the arena put right
// behind it (the 11:30 and 22:50 crashes at exe+0x15ebb1, its vtable = stock counts). Then the Lab sites (size, stock copy
// offset, the three copy counts) are left stock for the whole session: ids from 1,000 have no stock copy in the Lab (they
// show 0) until the next start, nothing runs past the manager. The block size is the arena header's int just before the
// manager (the first int in -0x10 .. -0x40 that looks like a size followed by two zero ints, as cwo_labfix.inl reads it).
static bool g_bi_lab_small;
static int bi_lab_block() {                                     // the existing Lab manager's arena block size, 0 = none / unknown
    __try {
        ULONG_PTR lab = *(ULONG_PTR *)(g_base + 0x4c0140);
        if (!lab) return 0;
        for (ULONG_PTR q = lab - 0x10; q >= lab - 0x40; q -= 8) {
            int v = *(int *)q;
            if (v >= 0x1000 && v < 0x40000 && *(int *)(q + 8) == 0 && *(int *)(q + 12) == 0) return v;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
static bool bi_lab_site(const BiPatch &p) {                     // a site that only works with the bigger Lab manager
    return p.kind == BI_IMM && (p.value == (unsigned)BI_LAB_SIZE_NEW || p.value == (unsigned)BI_LAB_STOCK_NEW ||
                                p.value == (unsigned)BI_LAB_STOCK_NEW + 1 || p.value == 9);
}

static int bi_patch_all(ULONG_PTR base, bool standin) {
    for (int i = 0; i < BI_NPATCH; i++)                         // check all first: nothing changes on a mismatch
        if (memcmp((void *)(base + BI_PATCHES[i].rva), BI_PATCHES[i].old, BI_PATCHES[i].len)) return -(i + 1);
    for (int i = 0; i < BI_NPATCH; i++) {
        const BiPatch &p = BI_PATCHES[i];
        if (g_bi_lab_small && !standin && bi_lab_site(p)) continue;   // v5.55.1 the manager is already the stock size
        ULONG_PTR site = g_base + p.rva;                        // displacements are computed for the real site
        unsigned char nb[10];
        bi_new_bytes(p, site, nb);
        unsigned char *at = (unsigned char *)(base + p.rva);
        DWORD old;
        if (!standin && !VirtualProtect(at, p.len, PAGE_EXECUTE_READWRITE, &old)) return -100 - i;
        memcpy(at, nb, p.len);
        if (!standin) { VirtualProtect(at, p.len, old, &old); FlushInstructionCache(GetCurrentProcess(), at, p.len); }
    }
    return 1;
}

static void bi_apply() {                                        // worker start, before any game code runs
    if (!bi_alloc()) { g_bi_state = -1; logf("big ids: no memory near the exe - ids stay below 1000"); return; }
    int lab = bi_lab_block();
    g_bi_lab_small = lab > 0 && lab < (int)BI_LAB_SIZE_NEW;
    if (g_bi_lab_small) logf("big ids: the Lab manager already exists at the stock size (block %#x) - the Lab keeps its stock layout this session (stocks of ids 1000+ show 0 until the next start)", lab);
    int r = bi_patch_all(g_base, false);
    if (r != 1) { g_bi_state = -1; logf("big ids: site %d is not the stock code (%d) - nothing changed, ids stay below 1000", r < -99 ? -100 - r : -r - 1, r); return; }
    g_bi_state = 1;
    logf("big ids: skill ids 0-%d (maps at %p / %p, %d sites patched, Lab object 0x%llx, stock copy at +0x%llx)", BI_N - 1,
         (void *)g_bi_mapA, (void *)g_bi_mapB, BI_NPATCH, (unsigned long long)BI_LAB_SIZE_NEW, (unsigned long long)BI_LAB_STOCK_NEW);
}

// v5.148.9 (log 2026-11-10: "new skill 468 / 469 / 472 / 480: 3 copies in stock" every second): ns_maps_and_stock wrote the copies into
// the save block, and bi_step copies OUR wide stock back over the block every frame - so they were wiped and written again each
// second. While our stock is the live one, the copies go into it too.
static void ns_big_stock(int id) {
    if (g_bi_state != 1 || id < 0 || id >= BI_N) return;
    ULONG_PTR prof = *(ULONG_PTR *)(g_base + RVA_ARSENAL);
    if (!prof || *(unsigned char **)(prof + 0x3a38) != g_bi_stock[0]) return;
    if (g_bi_stock[0][id] == 0 || g_bi_stock[0][id] == 0xFF) {
        g_bi_stock[0][id] = 3;
        short *sup = (short *)(g_bi_supp[0] + 4 * id);
        if (sup[0] == 0 && sup[1] == 0) sup[1] = 2;
    }
}

// every frame: each player slot's stock / supplemental pointers mirrored (see the header)
static void bi_step() {
    if (g_bi_state != 1) return;
    ULONG_PTR prof = *(ULONG_PTR *)(g_base + RVA_ARSENAL);
    if (!prof) return;
    for (int k = 0; k < BI_PLAYERS; k++) {
        unsigned char **ps = (unsigned char **)(prof + 0x3a38 + 0x38 * k), **pq = (unsigned char **)(prof + 0x3a40 + 0x38 * k);
        unsigned char *s = *ps, *q = *pq;
        if (s == g_bi_stock[k]) {                               // ours: copy the saved part back into the block
            if (g_bi_src_stock[k]) memcpy(g_bi_src_stock[k], g_bi_stock[k], 1000);
            if (g_bi_src_supp[k] && q == g_bi_supp[k]) memcpy(g_bi_src_supp[k], g_bi_supp[k], 4000);
            continue;
        }
        if (!s || !q || s < (unsigned char *)prof || s >= (unsigned char *)prof + 0x10000 || q != s + (0xa2c - 0x644)) continue;   // not a save block: left alone
        memcpy(g_bi_stock[k], s, 1000);                         // take the block's bytes, then point the game at ours
        memcpy(g_bi_supp[k], q, 4000);
        for (int i = 1000; i < BI_N; i++) { g_bi_stock[k][i] = 0; *(unsigned *)(g_bi_supp[k] + 4 * i) = 0; }
        g_bi_src_stock[k] = s; g_bi_src_supp[k] = q;
        *ps = g_bi_stock[k]; *pq = g_bi_supp[k];
        g_bi_mirrors++;
        if (g_bi_mirrors <= 8) logf("big ids: player slot %d stock pointers -> ours (block %p)", k, (void *)s);
    }
}

static int bi_self_test() {                                     // the patch table against a stand-in of the sites
    // a stand-in image: the sites' old bytes at their RVAs (only the pages that hold sites are committed)
    size_t span = 0x280000;
    unsigned char *img = (unsigned char *)VirtualAlloc(NULL, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!img) return -82;
    for (int i = 0; i < BI_NPATCH; i++) memcpy(img + BI_PATCHES[i].rva, BI_PATCHES[i].old, BI_PATCHES[i].len);
    if (!g_bi_mem && !bi_alloc()) { VirtualFree(img, 0, MEM_RELEASE); return -82; }   // the real near-exe maps (a far stand-in would not fit a 32-bit displacement)
    int r = bi_patch_all((ULONG_PTR)img, true);
    bool ok = r == 1;
    // every immediate site now holds its value; the map sites aim at the stand-in maps when read as the real site
    for (int i = 0; ok && i < BI_NPATCH; i++) {
        const BiPatch &p = BI_PATCHES[i];
        unsigned v; memcpy(&v, img + p.rva + p.at, 4);
        ULONG_PTR site = g_base + p.rva;
        if (p.kind == BI_IMM) ok = v == p.value;
        else if (p.kind == BI_BASE_A) ok = (ULONG_PTR)(g_base + (int)v) == (ULONG_PTR)g_bi_mapA;
        else ok = (ULONG_PTR)(site + p.len + (int)v) == (ULONG_PTR)(p.kind == BI_RIP_A ? g_bi_mapA : g_bi_mapB);
    }
    // a changed site is refused and nothing else is written
    img[BI_PATCHES[3].rva] ^= 0xff;
    for (int i = 0; i < BI_NPATCH; i++) if (i != 3) memcpy(img + BI_PATCHES[i].rva, BI_PATCHES[i].old, BI_PATCHES[i].len);
    bool refused = bi_patch_all((ULONG_PTR)img, true) == -4 && !memcmp(img + BI_PATCHES[0].rva, BI_PATCHES[0].old, BI_PATCHES[0].len);
    VirtualFree(img, 0, MEM_RELEASE);
    logf("self-test: big ids %d sites %s (%d), changed site refused %s", BI_NPATCH, ok ? "ok" : "FAILED", r, refused ? "ok" : "FAILED");
    return ok && refused ? 1 : -82;
}
