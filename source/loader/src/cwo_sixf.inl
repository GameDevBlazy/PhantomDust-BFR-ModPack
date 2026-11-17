// ---- v5.0 six fighters, route A step 1 (owner 2026-10-25): the three
// per-fighter tables move to 8-entry copies, with no change in behaviour yet (fighters 5-6 come in step 2).
// Facts (capstone over PDUWP 9ba7411b; Ghidra census census_6_fighters_2026-10-25; tools/build_sixf_sites.py):
//   fighter pointers 0x1404bc9d8, battle data 0x1404bc608, fighter objects 0x1404bc3f0, each 4 x 8 bytes and followed by
//   other variables; 515 instructions reach them, RIP-relative (391) or image-base-relative (124: [image + index + RVA],
//   the image register checked for every one); no data pointer points into them.
// The move runs in DllMain, before any game code: every site's bytes are checked first and nothing is written unless all
// 515 match; the new tables sit in one page reserved right after the image (both addressing forms need it within 2 GB);
// the old contents are copied. Turned on by CwoSixFighters.txt ("on") in the loader's data folder; never online.
#include "cwo_sixf_apply.inl"
static const ULONG_PTR SIXF_RVA[3] = {0x4bc9d8, 0x4bc608, 0x4bc3f0};
static int g_sixf_state;                                       // 0 off, 1 moved, -1 refused

static bool sixf_wanted() {
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%sCwoSixFighters.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return false;
    char line[32] = "";
    fgets(line, sizeof(line), f);
    fclose(f);
    return !strncmp(line, "on", 2);
}

static void sixf_install() {                                   // DllMain (host checked): before any game code runs
    if (!sixf_wanted()) return;
    char why[96] = "";
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    ULONG_PTR end = g_base + nt->OptionalHeader.SizeOfImage, block = 0;
    for (ULONG_PTR a = (end + 0xFFFF) & ~(ULONG_PTR)0xFFFF; a < g_base + 0x40000000 && !block; a += 0x10000)
        block = (ULONG_PTR)VirtualAlloc((void *)a, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!block) { g_sixf_state = -1; logf("six fighters: no page within reach - tables left where they are"); return; }
    for (int t = 0; t < 3; t++) memcpy((void *)(block + t * SIXF_STRIDE), (const void *)(g_base + SIXF_RVA[t]), 0x20);
    ULONG_PTR text = g_base + 0x1000;
    SIZE_T text_n = 0x2d1600;
    DWORD old = 0;
    if (!VirtualProtect((void *)text, text_n, PAGE_EXECUTE_READWRITE, &old)) {
        g_sixf_state = -1; VirtualFree((void *)block, 0, MEM_RELEASE); logf("six fighters: code not writable"); return;
    }
    bool ok = sixf_apply(g_base, block, why, sizeof(why));
    VirtualProtect((void *)text, text_n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)text, text_n);
    if (!ok) { g_sixf_state = -1; VirtualFree((void *)block, 0, MEM_RELEASE); logf("six fighters: refused (%s) - nothing changed", why); return; }
    for (int t = 0; t < 3; t++) g_six_tab[t] = block + t * SIXF_STRIDE;
    g_sixf_state = 1;
    logf("six fighters: %zu sites moved to 8-entry tables at %p (fighters %p, battle data %p, objects %p)",
         sizeof(SIXF_SITES) / sizeof(*SIXF_SITES), (void *)block, (void *)g_six_tab[0], (void *)g_six_tab[1], (void *)g_six_tab[2]);
}

static int sixf_self_test() {                                  // the patcher on a stand-in image carrying the 515 sites
    const size_t n = sizeof(SIXF_SITES) / sizeof(*SIXF_SITES);
    const SIZE_T img = 0x600000;
    unsigned char *base = (unsigned char *)VirtualAlloc(NULL, img + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) return -50;
    for (size_t i = 0; i < n; i++) memcpy(base + SIXF_SITES[i].rva, SIXF_SITES[i].b, SIXF_SITES[i].len);
    char why[96] = "";
    ULONG_PTR block = (ULONG_PTR)base + img;
    bool ok = sixf_apply((ULONG_PTR)base, block, why, sizeof(why));
    for (size_t i = 0; ok && i < n; i++) {                      // each site now reaches its table's new copy at the same offset
        const SixfSite &s = SIXF_SITES[i];
        int d;
        memcpy(&d, base + s.rva + s.dispoff, 4);
        ULONG_PTR tgt = s.mode == 0 ? (ULONG_PTR)base + s.rva + s.len + d : (ULONG_PTR)base + (unsigned)d;
        ok = tgt == block + s.table * SIXF_STRIDE + s.ofs;
        for (unsigned k = 0; ok && k < s.len; k++) if (k < s.dispoff || k >= s.dispoff + 4u) ok = base[s.rva + k] == s.b[k];   // only the displacement changed
    }
    base[SIXF_SITES[7].rva] ^= 0xFF;                            // one byte off: the whole move is refused, nothing written
    unsigned char keep[16];
    memcpy(keep, base + SIXF_SITES[0].rva, 16);
    bool refused = !sixf_apply((ULONG_PTR)base, block + 0x1000, why, sizeof(why)) && !memcmp(keep, base + SIXF_SITES[0].rva, 16);
    VirtualFree(base, 0, MEM_RELEASE);
    logf("self-test: six fighters table move %s (%zu sites), refused on a changed byte %s", ok ? "ok" : "FAILED", n, refused ? "ok" : "FAILED");
    return ok && refused ? 1 : -51;
}
