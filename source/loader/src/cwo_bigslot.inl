// ---- v5.4 bigger fighter model slots (owner 2026-10-25 17:17: Agent Smith's head on Nanashi "looks jacked up"; 16:49 /
// 17:02: the created character BLAZY "looks messed up" in 3 vs 3; owner).
// Game facts (Ghidra / capstone on PDUWP 9ba7411b, MODLOG 2026-10-25 17:40; tools/build_bigslot.py):
//   FUN_14024e190 puts a fighter's .alr at *0x14052ded8 + 0xc000 + index * 0x180000, so a model may be 0x174000 bytes
//   (1,523,712) before the next fighter's slot. BLAZY's files are 3,106,304 bytes and the Smith test 2,053,888: the next
//   fighter's model loaded over their tails. 36 places compute index * 0x180000 (`lea r, [i + i*2]; shl r, 0x13`), 23
//   loads of the pool base feed them, FUN_14017c690 keeps a 4-slot table; the pool's other users (boss data at +0x480000,
//   the menus at +0x200000 and up, the set-up) keep the pool.
// What this does (official Windows documentation only: VirtualAlloc, VirtualProtect, FlushInstructionCache): one block of
// 8 slots x 0x600000 is committed for the fighters (a model may now be 0x5f4000 = 6,242,304 bytes); the shifts become 0x15
// (index * 0x600000), the 23 loads read the block's address from a loader cell in a page right after the image (their
// RIP-relative reach), the table offsets follow the new stride. Every site is checked first and nothing is written unless
// all 62 match. In DllMain, before any game code. "off" in CwoBigSlots.txt (the loader's data folder) leaves the game as it is.
#include "cwo_bigslot_sites.h"
static const int BIGSLOT_N = 8;
static int g_bigslot_state;                                    // 0 off, 1 on, -1 refused
static ULONG_PTR g_bigslot_pool, g_bigslot_cell;

static bool bigslot_apply(ULONG_PTR base, ULONG_PTR cell, char *why, size_t n) {
    const size_t cnt = sizeof(BIGSLOT_SITES) / sizeof(*BIGSLOT_SITES);
    for (size_t i = 0; i < cnt; i++) {
        const BigslotSite &s = BIGSLOT_SITES[i];
        if (memcmp((const void *)(base + s.rva), s.b, s.len)) { sprintf_s(why, n, "site %zu at +0x%x differs", i, s.rva); return false; }
        LONGLONG d = (LONGLONG)cell - (LONGLONG)(base + s.rva + s.len);
        if (s.kind == 1 && d != (int)d) { sprintf_s(why, n, "cell out of reach of +0x%x", s.rva); return false; }
    }
    for (size_t i = 0; i < cnt; i++) {
        const BigslotSite &s = BIGSLOT_SITES[i];
        unsigned char *p = (unsigned char *)(base + s.rva + s.at);
        if (s.kind == 0) {
            *p = (unsigned char)s.newv;                         // shl r, 0x13 -> 0x15
        } else if (s.kind == 1) {
            int d = (int)((LONGLONG)cell - (LONGLONG)(base + s.rva + s.len));
            memcpy(p, &d, 4);                                   // [rip + pool base] -> [rip + cell]
        } else {
            memcpy(p, &s.newv, 4);                              // lea r, [base + slot n] with the new stride
        }
    }
    return true;
}

// v5.4.1 sound banks 7 / 8 (owner 17:52: a skill hitting fighter 5 / 6 crashed; FAULT exe+0x27fa6d with bank 8 empty).
// A fighter's bank is its index + 3 (FUN_14024e190 requests it, FUN_14024f6c0 registers it), and FUN_1402419b0 loads bank b
// into *0x14052df00 + int table 0x1402f6628[b]: 3 0xc68000, 4 0xce8000, 5 0xd68000, 6 0xde8000, 7 0xd68000, 8 0xc68000, so
// fighter 5's voices would load over fighter 3's and fighter 6's over fighter 1's. With the fighters' models in their own
// block, the old pool's first 0x200000 bytes are unused (its other users start at +0x200000: FUN_1401ba960; boss data
// +0x480000): banks 7 / 8 get 0x80000 each there. *0x14052ded8 - *0x14052df00 = -0x2900000 (FUN_1401ca3d0 lays out both).
static const ULONG_PTR RVA_BANK_OFFS = 0x2f6628;
static bool bigslot_banks(ULONG_PTR base, bool rw) {
    int *t = (int *)(base + RVA_BANK_OFFS);
    if (t[3] != 0xc68000 || t[5] != 0xd68000 || t[7] != 0xd68000 || t[8] != 0xc68000) return false;
    DWORD old = 0;
    if (!rw && !VirtualProtect(t + 7, 8, PAGE_READWRITE, &old)) return false;
    t[7] = -0x2900000;                                          // old pool +0
    t[8] = -0x2880000;                                          // old pool +0x80000
    if (!rw) VirtualProtect(t + 7, 8, old, &old);
    return true;
}

static bool bigslot_wanted() {
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%sCwoBigSlots.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return true;
    char line[32] = "";
    fgets(line, sizeof(line), f);
    fclose(f);
    return strncmp(line, "off", 3) != 0;
}

static void bigslot_install() {                                // DllMain (host checked): before any game code runs
    if (!bigslot_wanted()) { logf("bigger fighter slots: off (CwoBigSlots.txt)"); return; }
    char why[96] = "";
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    ULONG_PTR end = g_base + nt->OptionalHeader.SizeOfImage, cell = 0;
    for (ULONG_PTR a = (end + 0xFFFF) & ~(ULONG_PTR)0xFFFF; a < g_base + 0x40000000 && !cell; a += 0x10000)
        cell = (ULONG_PTR)VirtualAlloc((void *)a, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!cell) { g_bigslot_state = -1; logf("bigger fighter slots: no page within reach - slots left as they are"); return; }
    ULONG_PTR pool = (ULONG_PTR)VirtualAlloc(NULL, (SIZE_T)BIGSLOT_N * BIGSLOT_STRIDE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!pool) {
        g_bigslot_state = -1; VirtualFree((void *)cell, 0, MEM_RELEASE);
        logf("bigger fighter slots: %u MB not available - slots left as they are", (unsigned)(BIGSLOT_N * (BIGSLOT_STRIDE >> 20)));
        return;
    }
    *(ULONG_PTR *)cell = pool;
    ULONG_PTR text = g_base + 0x1000;
    SIZE_T text_n = 0x2d1600;
    DWORD old = 0;
    bool ok = false;
    if (VirtualProtect((void *)text, text_n, PAGE_EXECUTE_READWRITE, &old)) {
        ok = bigslot_apply(g_base, cell, why, sizeof(why));
        VirtualProtect((void *)text, text_n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)text, text_n);
    } else {
        strcpy_s(why, "code not writable");
    }
    if (!ok) {
        g_bigslot_state = -1;
        VirtualFree((void *)pool, 0, MEM_RELEASE);
        VirtualFree((void *)cell, 0, MEM_RELEASE);
        logf("bigger fighter slots: refused (%s) - nothing changed", why);
        return;
    }
    g_bigslot_pool = pool; g_bigslot_cell = cell; g_bigslot_state = 1;
    logf("bigger fighter slots: sound banks 7 / 8 (fighters 5 / 6) %s", bigslot_banks(g_base, false) ?
         "have buffers of their own (old pool +0 / +0x80000)" : "left as they are (table not as expected)");
    logf("bigger fighter slots: %zu sites changed; %d slots of %u MB at %p (cell %p): a fighter's model may be %u bytes (was 1523712)",
         sizeof(BIGSLOT_SITES) / sizeof(*BIGSLOT_SITES), BIGSLOT_N, (unsigned)(BIGSLOT_STRIDE >> 20), (void *)pool, (void *)cell,
         (unsigned)(BIGSLOT_STRIDE - 0xc000));
}

// at each match start (cwo_sixf2.inl sixf_after_start): where each fighter's model goes (FUN_14024e190 set +0x12c80)
static void bigslot_log_match() {
    if (g_bigslot_state != 1) return;
    char line[256] = "";
    __try {
        ULONG_PTR *act = cwo_actors();
        for (int i = 0; i < BIGSLOT_N; i++) {
            ULONG_PTR a = act[i];
            if (!a) continue;
            ULONG_PTR buf = *(ULONG_PTR *)(a + 0x12c80);
            bool in = buf >= g_bigslot_pool && buf < g_bigslot_pool + (ULONG_PTR)BIGSLOT_N * BIGSLOT_STRIDE;
            sprintf_s(line + strlen(line), sizeof(line) - strlen(line), " %d:%s", i + 1,
                      in ? "new" : "OLD");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    logf("bigger fighter slots: this match's fighters (model slot new/old):%s", line);
}

static int bigslot_self_test() {                               // the patcher on a stand-in image carrying the 62 sites
    const size_t cnt = sizeof(BIGSLOT_SITES) / sizeof(*BIGSLOT_SITES);
    const SIZE_T img = 0x300000;
    unsigned char *base = (unsigned char *)VirtualAlloc(NULL, img + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) return -70;
    for (size_t i = 0; i < cnt; i++) memcpy(base + BIGSLOT_SITES[i].rva, BIGSLOT_SITES[i].b, BIGSLOT_SITES[i].len);
    char why[96] = "";
    ULONG_PTR cell = (ULONG_PTR)base + img;
    bool ok = bigslot_apply((ULONG_PTR)base, cell, why, sizeof(why));
    int shifts = 0, loads = 0, tables = 0;
    for (size_t i = 0; ok && i < cnt; i++) {
        const BigslotSite &s = BIGSLOT_SITES[i];
        unsigned char *p = base + s.rva;
        if (s.kind == 0) { ok = p[s.at] == 0x15 && ((ULONG_PTR)3 << 0x15) == BIGSLOT_STRIDE; shifts++; }
        else if (s.kind == 1) { int d; memcpy(&d, p + s.at, 4); ok = (ULONG_PTR)p + s.len + d == cell; loads++; }
        else { unsigned v; memcpy(&v, p + s.at, 4); ok = v == s.newv && v % BIGSLOT_STRIDE == 0; tables++; }
        for (unsigned k = 0; ok && k < s.len; k++) if (k < s.at || k >= (unsigned)(s.at + s.size)) ok = p[k] == s.b[k];   // only the value changed
    }
    int *bt = (int *)(base + RVA_BANK_OFFS);                    // the bank table: 7 / 8 move, a changed table is left alone
    const int stock[10] = {0, 0xa68000, 0xce8000, 0xc68000, 0xce8000, 0xd68000, 0xde8000, 0xd68000, 0xc68000, 0xce8000};
    memcpy(bt, stock, sizeof(stock));
    bool banks = bigslot_banks((ULONG_PTR)base, true) && bt[7] == -0x2900000 && bt[8] == -0x2880000 && bt[5] == 0xd68000 &&
                 bt[3] == 0xc68000 && !bigslot_banks((ULONG_PTR)base, true);
    ok = ok && banks;
    unsigned char keep[16];
    memcpy(keep, base + BIGSLOT_SITES[0].rva, 16);
    bool refused = !bigslot_apply((ULONG_PTR)base, cell, why, sizeof(why)) && !memcmp(keep, base + BIGSLOT_SITES[0].rva, 16);
    VirtualFree(base, 0, MEM_RELEASE);
    ok = ok && shifts == 36 && loads == 23 && tables == 3;
    logf("self-test: bigger fighter slots %s (%d shifts, %d loads, %d table offsets, sound banks 7 / 8 %s), refused on a second run %s",
         ok ? "ok" : "FAILED", shifts, loads, tables, banks ? "ok" : "FAILED", refused ? "ok" : "FAILED");
    return ok && refused ? 1 : -71;
}
