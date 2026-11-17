// ---- v5.50.1 the common sound bank from the override folder (owner 2026-10-31: Storm sounds for the imported skills).
// Game facts (Ghidra batches 071a-071l, SOUND_FORMATS_2026-10-23.md; full pass 065): FUN_1402419b0 loads Sound\<name>.bin
// for slot N into the stage block at *0x14052df00 + table[N] (table 0x1402f6628: slot 0 = common.bin at +0, slot 1 at
// +0xa68000, so common.bin has 0xa68000 bytes of room) and the bank pointer *0x140582700[N] is where it landed; play
// calls read the STH2 sections (REAL / WAVE) straight from that memory. The common bank loads at boot - before the Store
// mode loader is in the process - so a file override cannot reach it.
// What this does: reads <override>\Assets\Data\Sound\common.bin once (STH2, at most the room), then each frame until
// done: when *0x140582700[0] points at an STH2 bank, copies ours over it (same address, so every pointer stays valid)
// and logs the sound / wave counts. Nothing else changes; a restart undoes it.
static const ULONG_PTR RVA_BANK_PTRS = 0x582700;
static const SIZE_T SNDBANK_ROOM = 0xa68000;
static unsigned char *g_sb_data;
static SIZE_T g_sb_len;
static int g_sb_state;                                          // 0 nothing to do, 1 waiting, 2 done, -1 refused

static void sb_load() {
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Sound\\common.bin", g_over);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;                      // no override bank: nothing to do
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0x100 || sz.QuadPart > (LONGLONG)SNDBANK_ROOM - 0x10) { CloseHandle(h); g_sb_state = -1; logf("sound bank: override common.bin is %lld bytes - not between 256 and %llu", sz.QuadPart, (unsigned long long)SNDBANK_ROOM - 0x10); return; }
    g_sb_data = (unsigned char *)VirtualAlloc(NULL, (SIZE_T)sz.QuadPart, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD got = 0;
    if (!g_sb_data || !ReadFile(h, g_sb_data, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart || memcmp(g_sb_data, "STH2", 4)) { CloseHandle(h); g_sb_state = -1; logf("sound bank: override common.bin could not be read or is not STH2"); return; }
    CloseHandle(h);
    g_sb_len = (SIZE_T)sz.QuadPart; g_sb_state = 1;
}

static unsigned sb_count(const unsigned char *bank, unsigned sec_ptr) {   // REAL / WAVE count at header +sec_ptr
    unsigned off = *(const unsigned *)(bank + sec_ptr);
    return *(const unsigned *)(bank + off + 8);
}

static void sb_step() {                                         // every frame until done
    if (g_sb_state != 1) return;
    unsigned char *bank = *(unsigned char **)(g_base + RVA_BANK_PTRS);
    if (!bank) return;
    __try {
        if (memcmp(bank, "STH2", 4)) return;
        unsigned sounds = sb_count(bank, 0x24), waves = sb_count(bank, 0x28);
        memcpy(bank, g_sb_data, g_sb_len);
        g_sb_state = 2;
        logf("sound bank: common.bin in memory (%p) replaced by the override (%u -> %u sounds, %u -> %u waves, %u KB)", (void *)bank,
             sounds, sb_count(bank, 0x24), waves, sb_count(bank, 0x28), (unsigned)(g_sb_len >> 10));
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_sb_state = -1; logf("sound bank: fault while replacing - left as loaded"); }
}
