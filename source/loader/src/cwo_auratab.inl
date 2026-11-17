// ---- v5.31 the aura draw's per-fighter table moved (owner 2026-10-27 17:48: a split-screen 3 vs 3 crashed as it started;
// the loader's FAULT line: exe 14001de96 reading b4000000b4 - the screen-split draw FUN_140286c60 bound the texture held
// at 0x14054c200, and that qword held two ints of 180).
// Game facts (Ghidra full pass via pd_fn.py, capstone on the clean exe 9ba7411b, 2026-10-27):
//  - FUN_14019b520 (the fighters' aura draw) walks the fighters and stores one int per fighter into the table at
//    0x14054c1f0 (r12, loaded by `lea r12, [rip + 0x3b0bcf]` at 0x14019b61a), and in split screen (*0x140530034 bit 3)
//    the same value again two entries further on (puVar17[2]);
//  - the table has 4 ints: 0x14054c200 / 0x14054c208 are the textures FUN_140286c60 / FUN_140287460 bind each frame, so a
//    fighter numbered 4 or 5, or fighter 2 / 3 in split screen (the loader's Tag Team, Battle Royale and 3 vs 3), writes
//    over them;
//  - the only other user is FUN_140287460, `lea rcx, [rip + 0x2c49bc]` at 0x14028782d, which reads entries
//    [view, view + count) of it.
// What this does: a 16-int copy of the table in a page of the loader's own within reach of the code, both lea
// instructions pointed at it (bytes checked first, all or nothing). The stock game's four entries keep their meaning.
static const struct { unsigned rva; unsigned char b[7]; } AURA_SITES[2] = {
    {0x19b61a, {0x4c, 0x8d, 0x25, 0xcf, 0x0b, 0x3b, 0x00}},    // lea r12, [rip + 0x3b0bcf] -> 0x14054c1f0 (the writer)
    {0x28782d, {0x48, 0x8d, 0x0d, 0xbc, 0x49, 0x2c, 0x00}}};   // lea rcx, [rip + 0x2c49bc] -> 0x14054c1f0 (the reader)
static const unsigned AURA_RVA = 0x54c1f0;
static ULONG_PTR g_aura_tab;

static bool aura_apply(ULONG_PTR base, ULONG_PTR tab, char *why, size_t nwhy) {   // tab: 64 bytes within +-2 GB of base
    for (int i = 0; i < 2; i++)
        if (memcmp((const void *)(base + AURA_SITES[i].rva), AURA_SITES[i].b, 7)) { sprintf_s(why, nwhy, "site +0x%x differs", AURA_SITES[i].rva); return false; }
    for (int i = 0; i < 2; i++) {
        long long d = (long long)tab - (long long)(base + AURA_SITES[i].rva + 7);
        if (d < INT_MIN || d > INT_MAX) { sprintf_s(why, nwhy, "out of reach"); return false; }
    }
    memcpy((void *)tab, (const void *)(base + AURA_RVA), 16);
    for (int i = 0; i < 2; i++) {
        int d = (int)((long long)tab - (long long)(base + AURA_SITES[i].rva + 7));
        memcpy((void *)(base + AURA_SITES[i].rva + 3), &d, 4);
    }
    return true;
}

static void aura_install() {                                     // DllMain (host checked), before any game code runs
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    ULONG_PTR end = g_base + nt->OptionalHeader.SizeOfImage, page = 0;
    for (ULONG_PTR a = (end + 0xFFFF) & ~(ULONG_PTR)0xFFFF; a < g_base + 0x40000000 && !page; a += 0x10000)
        page = (ULONG_PTR)VirtualAlloc((void *)a, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!page) { logf("aura table: no page within reach - left where it is"); return; }
    DWORD old = 0;
    ULONG_PTR text = g_base + 0x1000;
    SIZE_T text_n = 0x2d1600;
    if (!VirtualProtect((void *)text, text_n, PAGE_EXECUTE_READWRITE, &old)) { VirtualFree((void *)page, 0, MEM_RELEASE); logf("aura table: code not writable"); return; }
    char why[64] = "";
    bool ok = aura_apply(g_base, page, why, sizeof(why));
    VirtualProtect((void *)text, text_n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)text, text_n);
    if (!ok) { VirtualFree((void *)page, 0, MEM_RELEASE); logf("aura table: refused (%s) - nothing changed", why); return; }
    g_aura_tab = page;
    logf("aura table: the aura draw's per-fighter table moved to %p (16 entries; 0x14054c200 / 208 are the frame's textures again)", (void *)page);
}

static int aura_self_test() {                                   // on a stand-in image: both sites move, a changed site is refused
    const size_t n = AURA_RVA + 0x20;
    unsigned char *img = (unsigned char *)VirtualAlloc(NULL, n + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!img) return -74;
    for (int i = 0; i < 2; i++) memcpy(img + AURA_SITES[i].rva, AURA_SITES[i].b, 7);
    int stock[4] = {11, 22, 33, 44};
    memcpy(img + AURA_RVA, stock, 16);
    ULONG_PTR tab = (ULONG_PTR)img + n;
    char why[64] = "";
    bool ok = aura_apply((ULONG_PTR)img, tab, why, sizeof(why));
    for (int i = 0; ok && i < 2; i++) {
        int d = 0;
        memcpy(&d, img + AURA_SITES[i].rva + 3, 4);
        ok = (ULONG_PTR)img + AURA_SITES[i].rva + 7 + d == tab;
    }
    ok = ok && !memcmp((void *)tab, stock, 16);
    bool again = !aura_apply((ULONG_PTR)img, tab, why, sizeof(why));   // the sites no longer read as the clean exe
    VirtualFree(img, 0, MEM_RELEASE);
    logf("self-test: aura table move %s, refused on a changed site %s", ok ? "ok" : "FAILED", again ? "ok" : "FAILED");
    return ok && again ? 1 : -75;
}
