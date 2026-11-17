// ---- v5.52.2 a Lab manager that is smaller than the loader expects (owner 2026-11-01: crash in multiplayer Edit Arsenal).
// Game facts (full-pass 065): FUN_14015e780 creates the Lab manager with FUN_140285aa0(*0x14052dfe8 + 0x10, 0x4fb8, 0x10)
// (a first-fit list allocator: 0x10-byte block header, 16-byte alignment, a block split when the remainder is 0x20 or
// more) and keeps it at holder+0x58, the global *0x1404c0140 and, right after it in the same arena, the mission menu
// object (0x78, DAT_1404bec38) at holder+0x60. The big-ids patch raises the size to 0x5500 (the stock copy moved to
// +0x4fb8, 1,256 bytes); a manager created with the stock size - the 11:30 crash showed the menu object at manager +
// 0x4fe0 = 0x4fb8 rounded up plus a header - is overrun by that copy every second and the arena beside it is corrupted.
// What this does: every frame, when a Lab manager exists that was not checked yet, reads its block size from the header
// (int at manager - 0x10). Smaller than 0x5500: a new 0x5500 block from the same arena, the old 0x4fb8 bytes copied in,
// the holder (the qword pair {manager, menu object} just before the manager) and the global re-pointed; the old block
// stays (leaked, 20 KB) so nothing else that still holds it is freed. Logged either way.
static const ULONG_PTR RVA_LAB_PTR = 0x4c0140, RVA_LAB_ARENA = 0x52dfe8, RVA_LAB_MENU = 0x4bec38, FN_LAB_ALLOC = 0x285aa0;
typedef void *(*PFN_LAB_ALLOC)(ULONG_PTR arena, long long size, unsigned align);
static ULONG_PTR g_lf_seen;                                     // the manager last checked
static int g_lf_fixed, g_lf_state;

// v5.167.5 (in game 2026-11-13 11:21: crash entering View Stock, exe+0x15ebb1 with the menu object full of stock counts; log
// 11:19:38 "40 sites patched" then 11:19:39 "block 0x4fe0 (SMALLER ...)"): the start-up guard (v5.55.1, bi_apply) read the
// Lab global before the game had stored its stock-size manager, so the Lab sites were patched and the 1,256-byte copy ran over
// the next object. When this check finds such a manager, the Lab sites go back to the game's bytes at once (each must still
// hold our bytes) - the state v5.55.1 sets: the Lab keeps its stock layout this session (ids 1000+ show 0 in the Lab).
static int bi_lab_revert() {
    int n = 0;
    for (int i = 0; i < BI_NPATCH; i++) {
        const BiPatch &p = BI_PATCHES[i];
        if (!bi_lab_site(p)) continue;
        unsigned char nb[10];
        bi_new_bytes(p, g_base + p.rva, nb);
        unsigned char *at = (unsigned char *)(g_base + p.rva);
        if (memcmp(at, nb, p.len)) continue;                    // not our bytes: left alone
        DWORD old;
        if (!VirtualProtect(at, p.len, PAGE_EXECUTE_READWRITE, &old)) continue;
        memcpy(at, p.old, p.len);
        VirtualProtect(at, p.len, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, p.len);
        n++;
    }
    g_bi_lab_small = true;
    return n;
}

static void lf_step() {
    if (g_lf_state < 0) return;
    ULONG_PTR lab = *(ULONG_PTR *)(g_base + RVA_LAB_PTR);
    if (!lab || lab == g_lf_seen) return;
    g_lf_seen = lab;
    __try {
        // v5.52.3: log only. The 11:48 run read 0 at -0x10 (the header is {size, ?, 0, 0} and the returned pointer may be
        // padded past it), and the move at 11:48 was followed by a crash at 11:51 - other objects keep pointers into the
        // manager. The block header is found by probing back; the size is logged so the real cause can be read next time.
        int at10 = *(int *)(lab - 0x10), at18 = *(int *)(lab - 0x18), at20 = *(int *)(lab - 0x20), at28 = *(int *)(lab - 0x28);
        int size = 0;
        for (ULONG_PTR q = lab - 0x10; q >= lab - 0x40; q -= 8) {
            int v = *(int *)q;
            if (v >= 0x1000 && v < 0x40000 && *(int *)(q + 8) == 0 && *(int *)(q + 12) == 0) { size = v; break; }
        }
        logf("lab size: Lab manager %llx header ints -0x10 %#x -0x18 %#x -0x20 %#x -0x28 %#x -> block %#x (%s)", (unsigned long long)lab, at10, at18, at20, at28, size,
             size >= (int)BI_LAB_SIZE_NEW ? "fine" : size ? "SMALLER than the loader's layout" : "unknown");
        if (size && size < (int)BI_LAB_SIZE_NEW && g_bi_state == 1 && !g_bi_lab_small) {
            int n = bi_lab_revert();
            logf("lab size: the Lab sites back to the game's layout (%d sites) - the Lab keeps its stock layout this session (stocks of ids 1000+ show 0 in the Lab)", n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("lab size: fault while checking the Lab manager - off"); g_lf_state = -1; }
}


// offline test (v5.167.5): on a stand-in image with every big-id site at its RVA, patch all, revert the Lab sites: the Lab sites
// hold the game's bytes again, every other site keeps ours
static int lf_revert_self_test() {
    ULONG_PTR top = 0;
    for (int i = 0; i < BI_NPATCH; i++) if (BI_PATCHES[i].rva + 16 > top) top = BI_PATCHES[i].rva + 16;
    unsigned char *img = (unsigned char *)VirtualAlloc(NULL, top, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!img) return -102;
    for (int i = 0; i < BI_NPATCH; i++) memcpy(img + BI_PATCHES[i].rva, BI_PATCHES[i].old, BI_PATCHES[i].len);
    ULONG_PTR keep = g_base; bool ks = g_bi_lab_small;
    g_base = (ULONG_PTR)img; g_bi_lab_small = false;
    bool ok = bi_patch_all((ULONG_PTR)img, true) == 1;
    int lab = 0;
    for (int i = 0; i < BI_NPATCH; i++) if (bi_lab_site(BI_PATCHES[i])) lab++;
    int n = ok ? bi_lab_revert() : -1;
    for (int i = 0; ok && i < BI_NPATCH; i++) {
        const BiPatch &p = BI_PATCHES[i];
        unsigned char nb[10]; bi_new_bytes(p, g_base + p.rva, nb);
        bool is_old = !memcmp(img + p.rva, p.old, p.len), is_new = !memcmp(img + p.rva, nb, p.len);
        ok = bi_lab_site(p) ? is_old : (is_new || !memcmp(nb, p.old, p.len));
    }
    ok = ok && n == lab && lab > 0 && g_bi_lab_small;
    g_base = keep; g_bi_lab_small = ks;
    VirtualFree(img, 0, MEM_RELEASE);
    logf("self-test: a small Lab manager gets the game's Lab sites back (%d of %d sites) %s", n, BI_NPATCH, ok ? "ok" : "FAILED");
    return ok ? 1 : -103;
}
