// ---- v5.15 the Underground as one battle stage: stage 30 gets a model buffer of its own (owner 2026-10-26; "go ahead and get started on it. I want you to make this possible").
// ---- v5.39 every big stage gets its own buffer (owner 2026-10-31: every big stage its own bigger block of
// memory, Map Slot 7 included, so big stage packs fit at full texture quality in normal play and the stage creator).
// Game facts (Ghidra full pass 065 / batch_078 + capstone on PDUWP 9ba7411b; MODLOG 2026-10-26 Underground entry,
// 08_Stages_and_Environments/ST00_SHARED_EFFECTS.md, MAP_SLOTS_2026-10-23.md):
//   FUN_1401d85f0 queues the stage .alr for *0x14052ded0 + 0x400000 (`add r8, 0x400000` at 0x1401d86a3) and FUN_1401d8820
//   points *0x140530328 / *0x140530338 there (`lea rcx, [rax + 0x400000]` at 0x1401d887a) before FUN_1402934e0 registers
//   it; the stage .dat follows at +0x1ce0000 (+0x1000000 when *0x14052fc96 == 2), so a model may be 0x18e0000 bytes
//   (24.9 MB), and only 12 MB in that mode. The reader (FUN_14028b690 / FUN_14028af80) reads the whole file in 16 MB
//   pieces, one after another, rounded up to 2 KB, with no size check. *0x140530264 is the stage id FUN_1401d85f0
//   indexes the file table (0x1403f2b60, stages 0-34, entry 2N = stage N's .alr name) with.
// What this does (official Windows documentation only: VirtualAlloc, VirtualProtect, FlushInstructionCache): at start-up
// the size of every stage's .alr is read (stage N: the file table's name; Map Slot 7 / stage 10: also every pack in
// Map\mx_list.txt, since the loader switches its names). Each stage whose biggest model is over 12 MB (the smallest room
// the game has, so it fits in every mode, the Stage Creator's included) gets a committed buffer of that size + 1 MB,
// in a table of 35 cells (one per stage number). The two instructions become calls to stubs that do the same thing and
// then, when the cell of the stage being loaded is set, give that buffer instead (tools/build_bigstage.py assembled and
// checked the stubs; cwo_bigstage_expect.h holds its bytes and the self-test compares them). Every other stage keeps
// the stock address. No stage over 12 MB: no code change at all. Switching Map Slot 7 to a pack that has grown past its
// buffer since start-up is refused (mx_apply asks bigstage_fits). Also: the fall-out line *0x140530070 is 0 on every
// stage and -500 in the hub (FUN_1401d1b90, the stage % 10 == 9 test on *0x1403e0304); the Underground's floors reach
// y -59, so while stage 30 is loaded the line is the hub's -500.
#include "cwo_bigstage_expect.h"
static const ULONG_PTR BIGSTAGE_SITE_REQ = 0x1d86a3, BIGSTAGE_SITE_PTR = 0x1d887a;
static const ULONG_PTR RVA_REQ_STAGE = 0x530264, RVA_CUR_STAGE = 0x3e0304, RVA_FALL_Y = 0x530070;
static const int BIGSTAGE_UNDERGROUND = 30, BIGSTAGE_STAGES = 35;
static const ULONGLONG BIGSTAGE_ROOM_MIN = 0xC00000;           // 12 MB: the stock room in every mode
static const unsigned char BIGSTAGE_OLD_REQ[7] = {0x49, 0x81, 0xc0, 0x00, 0x00, 0x40, 0x00};   // add r8, 0x400000
static const unsigned char BIGSTAGE_OLD_PTR[7] = {0x48, 0x8d, 0x88, 0x00, 0x00, 0x40, 0x00};   // lea rcx, [rax + 0x400000]
static int g_bigstage_state;                                    // 0 off (no stage over 12 MB), 1 on, -1 refused
static ULONG_PTR g_bigstage_page, g_bigstage_buf[BIGSTAGE_STAGES];
static SIZE_T g_bigstage_size[BIGSTAGE_STAGES];
static volatile LONG g_bigstage_said;

static void put32(unsigned char *p, LONGLONG v) { int d = (int)v; memcpy(p, &d, 4); }

// the stub at `at`: <old instruction>; push rax; push rdx; mov eax, [rip -> stage id]; cmp eax, 35; jae done;
// lea rdx, [rip -> cells]; mov rax, [rdx + rax*8]; test rax, rax; je done; mov <r8 | rcx>, rax; done: pop rdx; pop rax;
// ret   (42 bytes, as tools/build_bigstage.py)
static size_t bigstage_stub(unsigned char *p, ULONG_PTR at, const unsigned char *old, bool r8, ULONG_PTR stage, ULONG_PTR cells) {
    memcpy(p, old, 7);
    p[7] = 0x50; p[8] = 0x52;
    p[9] = 0x8b; p[10] = 0x05; put32(p + 11, (LONGLONG)stage - (LONGLONG)(at + 15));
    p[15] = 0x83; p[16] = 0xf8; p[17] = (unsigned char)BIGSTAGE_STAGES;
    p[18] = 0x73; p[19] = 0x13;
    p[20] = 0x48; p[21] = 0x8d; p[22] = 0x15; put32(p + 23, (LONGLONG)cells - (LONGLONG)(at + 27));
    p[27] = 0x48; p[28] = 0x8b; p[29] = 0x04; p[30] = 0xc2;
    p[31] = 0x48; p[32] = 0x85; p[33] = 0xc0;
    p[34] = 0x74; p[35] = 0x03;
    p[36] = r8 ? 0x49 : 0x48; p[37] = 0x89; p[38] = r8 ? 0xc0 : 0xc1;
    p[39] = 0x5a; p[40] = 0x58; p[41] = 0xc3;
    return 42;
}
static void bigstage_call(unsigned char *p, ULONG_PTR site, ULONG_PTR stub) {     // call stub; nop; nop
    p[0] = 0xe8; put32(p + 1, (LONGLONG)stub - (LONGLONG)(site + 5)); p[5] = 0x90; p[6] = 0x90;
}
static bool bigstage_reach(ULONG_PTR a, ULONG_PTR b) { LONGLONG d = (LONGLONG)a - (LONGLONG)b; return d == (int)d; }

// writes the two stubs into `page` (cells at page + 0x100) and the two calls into the image at `base`; all-or-nothing
static bool bigstage_apply(ULONG_PTR base, ULONG_PTR page, char *why, size_t n) {
    ULONG_PTR req = base + BIGSTAGE_SITE_REQ, ptr = base + BIGSTAGE_SITE_PTR, cells = page + 0x100;
    if (memcmp((void *)req, BIGSTAGE_OLD_REQ, 7)) { sprintf_s(why, n, "request site +0x%llx differs", (unsigned long long)BIGSTAGE_SITE_REQ); return false; }
    if (memcmp((void *)ptr, BIGSTAGE_OLD_PTR, 7)) { sprintf_s(why, n, "pointer site +0x%llx differs", (unsigned long long)BIGSTAGE_SITE_PTR); return false; }
    if (!bigstage_reach(page, req) || !bigstage_reach(page + 0x40, ptr) || !bigstage_reach(base + RVA_REQ_STAGE, page + 0x40)) {
        strcpy_s(why, n, "stub page out of reach"); return false;
    }
    bigstage_stub((unsigned char *)page, page, BIGSTAGE_OLD_REQ, true, base + RVA_REQ_STAGE, cells);
    bigstage_stub((unsigned char *)page + 0x40, page + 0x40, BIGSTAGE_OLD_PTR, false, base + RVA_REQ_STAGE, cells);
    bigstage_call((unsigned char *)req, req, page);
    bigstage_call((unsigned char *)ptr, ptr, page + 0x40);
    return true;
}

// size of Map\<name> (the Store mode override when there is one); 0 when it is not there
static ULONGLONG bigstage_file_size(const char *name) {
    if (!name || !name[0]) return 0;
    wchar_t exe[MAX_PATH], p[MAX_PATH], ov[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\');
    if (sl) sl[1] = 0;
    swprintf_s(p, MAX_PATH, L"%sAssets\\Data\\Map\\%hs", exe, name);
    if (sm_redirect(p, ov, MAX_PATH, false)) wcscpy_s(p, MAX_PATH, ov);   // v5.38 Store mode override
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &fa)) return 0;
    return ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
}

// how many bytes stage `stage`'s model may have: its own buffer, else the 12 MB every mode has
static ULONGLONG bigstage_room(int stage) {
    if (stage >= 0 && stage < BIGSTAGE_STAGES && g_bigstage_buf[stage]) return g_bigstage_size[stage] - 0x800;
    return BIGSTAGE_ROOM_MIN;
}

// mx_apply: may Map Slot 7 (stage `stage`) load the pack whose .alr is `file`.alr?
static bool bigstage_fits(int stage, const char *file, char *why, size_t n) {
    char name[32], dat[32];
    sprintf_s(name, "%.24s.alr", file);
    sprintf_s(dat, "%.24s.dat", file);
    ULONGLONG sz = bigstage_file_size(name), room = bigstage_room(stage);
    // v5.155 (owner 2026-11-12, BFR: Highway Pieces Test and Grand Panorama crashed in the level list): a pack whose files are
    // not there (its patch was refused) left the game reading nothing - the stage memory kept the previous pack, whose freed
    // collision the game then walked. A pack is switched in only when both files exist.
    if (sz == 0 || bigstage_file_size(dat) == 0) {
        sprintf_s(why, n, "%s or %s is missing (see the patches lines above)", name, dat);
        return false;
    }
    if (sz <= room) return true;
    sprintf_s(why, n, "%s is %.1f MB, stage %d has room for %.1f MB (restart the game to resize)", name, sz / 1048576.0,
              stage, room / 1048576.0);
    return false;
}

static void bigstage_install() {                                // DllMain (host checked): before any game code runs
    ULONGLONG need[BIGSTAGE_STAGES] = {};
    char biggest[BIGSTAGE_STAGES][32] = {};
    for (int s = 0; s < BIGSTAGE_STAGES; s++) {
        const char *nm = NULL;
        __try { nm = *mx_entry(2 * s); if (nm && IsBadStringPtrA(nm, 32)) nm = NULL; } __except (EXCEPTION_EXECUTE_HANDLER) { nm = NULL; }
        ULONGLONG sz = bigstage_file_size(nm);
        if (sz > need[s]) { need[s] = sz; strncpy_s(biggest[s], nm, _TRUNCATE); }
    }
    // Map Slot 7: every pack the loader can switch it to
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\');
    if (sl) sl[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Map\\mx_list.txt", exe);
    FILE *f = NULL;
    int packs = 0;
    if (cwo_wfopen(&f, path, L"r") == 0 && f) {
        char line[256], file[24], name[32];
        while (fgets(line, sizeof(line), f)) {
            if (sscanf_s(line, "%15[^\t]", file, (unsigned)sizeof(file)) != 1) continue;
            sprintf_s(name, "%s.alr", file);
            ULONGLONG sz = bigstage_file_size(name);
            packs++;
            if (sz > need[LIST_STAGE]) { need[LIST_STAGE] = sz; strcpy_s(biggest[LIST_STAGE], name); }
        }
        fclose(f);
    }
    int big = 0;
    for (int s = 0; s < BIGSTAGE_STAGES; s++) if (need[s] > BIGSTAGE_ROOM_MIN) big++;
    if (!big) {
        logf("big stages: no stage model over 12 MB (stages 0-34, %d Map Slot 7 packs) - stock stage slots", packs);
        return;
    }
    char why[96] = "";
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    bool ok = page != 0;
    if (!ok) strcpy_s(why, "no stub page near the game");
    for (int s = 0; ok && s < BIGSTAGE_STAGES; s++) {
        if (need[s] <= BIGSTAGE_ROOM_MIN) continue;
        SIZE_T size = (SIZE_T)((need[s] + 0x100000 + 0xFFFF) & ~0xFFFFull);
        g_bigstage_buf[s] = (ULONG_PTR)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!g_bigstage_buf[s]) { sprintf_s(why, "%llu MB for stage %d not available", (unsigned long long)(size >> 20), s); ok = false; break; }
        g_bigstage_size[s] = size;
        ((ULONG_PTR *)(page + 0x100))[s] = g_bigstage_buf[s];
    }
    if (ok) {
        ULONG_PTR text = g_base + 0x1000;
        SIZE_T text_n = 0x2d1600;
        DWORD old = 0;
        ok = false;
        if (VirtualProtect((void *)text, text_n, PAGE_EXECUTE_READWRITE, &old)) {
            ok = bigstage_apply(g_base, page, why, sizeof(why));
            VirtualProtect((void *)text, text_n, old, &old);
            FlushInstructionCache(GetCurrentProcess(), (void *)text, text_n);
        } else {
            strcpy_s(why, "code not writable");
        }
    }
    if (!ok) {
        g_bigstage_state = -1;
        for (int s = 0; s < BIGSTAGE_STAGES; s++) {
            if (g_bigstage_buf[s]) VirtualFree((void *)g_bigstage_buf[s], 0, MEM_RELEASE);
            g_bigstage_buf[s] = 0; g_bigstage_size[s] = 0;
        }
        if (page) VirtualFree((void *)page, 0, MEM_RELEASE);
        logf("big stages: refused (%s) - nothing changed; %d stage(s) over 12 MB would not fit, do not pick them", why, big);
        for (int s = 0; s < BIGSTAGE_STAGES; s++)
            if (need[s] > BIGSTAGE_ROOM_MIN) logf("big stages:   stage %d: %s %.1f MB", s, biggest[s], need[s] / 1048576.0);
        return;
    }
    FlushInstructionCache(GetCurrentProcess(), (void *)page, 0x1000);
    g_bigstage_page = page; g_bigstage_state = 1;
    for (int s = 0; s < BIGSTAGE_STAGES; s++)
        if (g_bigstage_buf[s])
            logf("big stages: stage %d%s: biggest model %s %.1f MB -> its own buffer of %llu MB at %p", s,
                 s == LIST_STAGE ? " (Map Slot 7)" : "", biggest[s], need[s] / 1048576.0,
                 (unsigned long long)(g_bigstage_size[s] >> 20), (void *)g_bigstage_buf[s]);
    logf("big stages: stubs %p (cells %p); every other stage keeps the stock slot", (void *)page, (void *)(page + 0x100));
}

// v5.108 (owner 2026-11-07): a Map
// Slot 7 pack whose mx_list row has a 5th column (kill height, PD units) gets that fall-out line. Game facts (full pass
// 065): FUN_1401d1b90 resets *0x140530070 to 0 at each stage load; FUN_14024ec80 calls the game's own fall-off
// FUN_140251d80(fighter, -3) (back to its start, life cost) when the fighter's y (+0x12e44) is below the line.
static volatile LONG g_kill_said = -1;
static void kill_step() {
    __try {
        LONG k = g_mx_cur;
        if (*(int *)(g_base + RVA_CUR_STAGE) != LIST_STAGE || k < 0 || k >= g_mx_count || !g_mx[k].has_kill) {
            InterlockedExchange(&g_kill_said, -1); return;
        }
        float *fall = (float *)(g_base + RVA_FALL_Y);
        if (*fall == 0.0f) {
            *fall = g_mx[k].kill;
            if (InterlockedExchange(&g_kill_said, k) != k)
                logf("kill zone: %s (%s) fall-out line 0 -> %.1f", g_mx[k].file, g_mx[k].name, g_mx[k].kill);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// CwoOverlayFrame: while stage 30 is the loaded stage, the fall-out line is the hub's -500 (FUN_1401d1b90 wrote 0)
static void bigstage_step() {
    kill_step();
    __try {
        if (*(int *)(g_base + RVA_CUR_STAGE) != BIGSTAGE_UNDERGROUND) { InterlockedExchange(&g_bigstage_said, 0); return; }
        float *fall = (float *)(g_base + RVA_FALL_Y);
        if (*fall == 0.0f) {
            *fall = -500.0f;
            if (!InterlockedExchange(&g_bigstage_said, 1))
                logf("Underground (stage 30): fall-out line 0 -> -500 (the hub's own value); model at %p (%s)",
                     *(void **)(g_base + 0x530338), *(ULONG_PTR *)(g_base + 0x530338) == g_bigstage_buf[BIGSTAGE_UNDERGROUND] &&
                     g_bigstage_buf[BIGSTAGE_UNDERGROUND] ? "its own buffer" : "the stock slot");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int bigstage_self_test() {                               // the stubs and calls on a stand-in image, against the
    const SIZE_T img = 0x300000;                                 // bytes tools/build_bigstage.py assembled and checked
    unsigned char *base = (unsigned char *)VirtualAlloc(NULL, img + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) return -77;
    memcpy(base + BIGSTAGE_SITE_REQ, BIGSTAGE_OLD_REQ, 7);
    memcpy(base + BIGSTAGE_SITE_PTR, BIGSTAGE_OLD_PTR, 7);
    char why[96] = "";
    ULONG_PTR page = (ULONG_PTR)base + img;
    bool ok = bigstage_apply((ULONG_PTR)base, page, why, sizeof(why));
    // the expected bytes were assembled for image 0x140000000 / page 0x140300000: same relative layout as here
    ok = ok && !memcmp((void *)page, BIGSTAGE_EXPECT_REQ, sizeof(BIGSTAGE_EXPECT_REQ)) &&
         !memcmp((void *)(page + 0x40), BIGSTAGE_EXPECT_PTR, sizeof(BIGSTAGE_EXPECT_PTR)) &&
         !memcmp(base + BIGSTAGE_SITE_REQ, BIGSTAGE_EXPECT_REQ_SITE, 7) && !memcmp(base + BIGSTAGE_SITE_PTR, BIGSTAGE_EXPECT_PTR_SITE, 7);
    // a second run must refuse (the sites are no longer stock)
    bool again = bigstage_apply((ULONG_PTR)base, page, why, sizeof(why));
    VirtualFree(base, 0, MEM_RELEASE);
    // room: a stage without a buffer has the 12 MB every mode has; one with a buffer has the buffer (less 2 KB)
    bool room = bigstage_room(LIST_STAGE) == (g_bigstage_buf[LIST_STAGE] ? g_bigstage_size[LIST_STAGE] - 0x800 : BIGSTAGE_ROOM_MIN) &&
                bigstage_room(-1) == BIGSTAGE_ROOM_MIN && bigstage_room(BIGSTAGE_STAGES) == BIGSTAGE_ROOM_MIN;
    logf("self-test: big-stage stubs %s, second apply %s, room rule %s", ok ? "match the checked bytes" : "DIFFER",
         again ? "WRONGLY accepted" : "refused", room ? "ok" : "WRONG");
    return ok && !again && room ? 1 : -77;
}
