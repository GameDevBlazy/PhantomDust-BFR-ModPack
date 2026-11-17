// CWO-S6T-1 - session size test for Phantom Dust (PDUWP.exe). Independently authored 2026-10-26, part of CWO-LOADER-01.
// Owner 2026-10-26: "Go" (to the proposed private-lobby test: host a private Xbox Live lobby with a build that asks for
// 6 members, to learn whether Xbox Live accepts more than 4). Online under the AGENTS.md 3v3 online exception (the 3v3
// mode's code only; here the one change the 3v3 mode needs online first). Nothing else of the loader is in this DLL.
//
// Provenance:
//   requirements: the owner's go above; 16_Reports/ONLINE_SESSION_SIZE_2026-10-26.md;
//   game evidence (Ghidra full pass 065, human-reviewed, + capstone on the clean exe 9ba7411b, MODLOG 2026-10-26):
//     FUN_140181ba0 (the host sets up an Xbox Live game) loads its member limit DAT_14051c0a4 (4; 2 for some rules) at
//     0x140181d34 `mov edi, [rip -> 0x14051c0a4]` and passes it as argument 5 to XonlineSessionCreate FUN_1402826f0;
//     FUN_140281f40 -> FUN_1400ea0c0 "__MaxMembersCount" -> *(*(*0x1404bbc48) + 0xe0) + 0x48 -> FUN_1400a4be0 ->
//     MultiplayerSession(maxMembersInSession);
//     FUN_1400e5890 (StateManager::UpdateGameSessionFullStatusAndOpenOrCloseLobbyIfHost) reads the game session's
//     member count (r12d, Members Size) and member limit (r15d, the session constants as Xbox Live returned them) and
//     compares them at 0x1400e5a11 `cmp r12d, r15d; setae al` (full when count >= limit);
//   documentation: official Windows documentation only (DllMain, CreateThread, VirtualAlloc, VirtualProtect,
//     FlushInstructionCache, GetTempPath); x64 instruction encodings from the Intel SDM;
//   dependencies: KERNEL32 only (static runtime);
//   tests: tools/test_s6t.py (capstone listing of both detours, site checks and a dry run on the clean exe mapped with
//     LoadLibraryEx DONT_RESOLVE_DLL_REFERENCES, second apply refused).
//
// What it does:
//   1. request: at 0x140181d34 the limit the game asks for becomes 6 when the game would ask for 4 (2 stays 2). Only the
//      value handed to the session changes; DAT_14051c0a4 itself (the game's own count) is not written.
//   2. observer (read-only): at 0x1400e5a11 the two numbers the game compares are copied to this DLL before the game's
//      own compare runs unchanged.
//   3. a worker thread writes <TEMP>\CwoLoader\cwo_s6t.log when anything changes: requests made, the limit stored in the
//      session settings, and each observed (member count, member limit). No names, ids or other players' data.
// Both sites are checked byte for byte before anything changes; any difference and nothing is changed.
#include <windows.h>
#include <stdio.h>
#include <string.h>

static ULONG_PTR g_base;
static wchar_t g_dir[MAX_PATH];
static CRITICAL_SECTION g_log_lock;
static volatile LONG g_req_count;                  // times the game asked for 4 and was given 6
static struct { volatile LONG members, limit, count; } g_obs;   // filled by the observer detour
static int g_state;                                // 0 idle, 1 applied, -1 refused

static const ULONG_PTR RVA_REQ = 0x181d34, RVA_REQ_BACK = 0x181d3a, RVA_LIMIT = 0x51c0a4;
static const ULONG_PTR RVA_OBS = 0xe5a11, RVA_OBS_BACK = 0xe5a17, RVA_SETTINGS = 0x4bbc48;

// the clean exe's bytes around both sites (capstone, 9ba7411b); the site itself is the first 6 bytes of each
static const unsigned char REQ_SITE[] = {0x8b, 0x3d, 0x6a, 0xa3, 0x39, 0x00};                    // mov edi, [rip+0x39a36a]
static const unsigned char REQ_NEXT[] = {0x41, 0x2b, 0xd8, 0xe8, 0x96, 0xad, 0x12, 0x00};        // sub ebx, r8d; call 0x1402acad8
static const unsigned char REQ_ARG[] = {0x89, 0x7c, 0x24, 0x20};                                 // 0x140181d45 mov [rsp+0x20], edi
static const unsigned char REQ_CALL[] = {0xe8, 0x9a, 0x09, 0x10, 0x00};                          // 0x140181d51 call 0x1402826f0
static const unsigned char OBS_SITE[] = {0x45, 0x3b, 0xe7, 0x0f, 0x93, 0xc0};                    // cmp r12d, r15d; setae al
static const unsigned char OBS_NEXT[] = {0x41, 0x88, 0x45, 0x2c};                                // mov [r13+0x2c], al
static const unsigned char OBS_LIMIT[] = {0x44, 0x8b, 0x7c, 0x24, 0x48};                         // 0x1400e59fe mov r15d, [rsp+0x48]

static void logf(const char *fmt, ...) {
    EnterCriticalSection(&g_log_lock);
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%scwo_s6t.log", g_dir);
    FILE *f = NULL;
    for (int i = 0; i < 20 && (_wfopen_s(&f, path, L"a") != 0 || !f); i++) Sleep(5);
    if (!f) { LeaveCriticalSection(&g_log_lock); return; }
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fputc('\n', f);
    fclose(f);
    LeaveCriticalSection(&g_log_lock);
}

static bool same(ULONG_PTR base, ULONG_PTR rva, const unsigned char *b, size_t n) {
    __try {
        return memcmp((const void *)(base + rva), b, n) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 0 = every expected byte is there; else the number of the first check that failed
static int sites_check(ULONG_PTR base) {
    if (!same(base, RVA_REQ, REQ_SITE, sizeof REQ_SITE)) return 1;
    if (!same(base, RVA_REQ_BACK, REQ_NEXT, sizeof REQ_NEXT)) return 2;
    if (!same(base, 0x181d45, REQ_ARG, sizeof REQ_ARG)) return 3;
    if (!same(base, 0x181d51, REQ_CALL, sizeof REQ_CALL)) return 4;
    if (!same(base, RVA_OBS, OBS_SITE, sizeof OBS_SITE)) return 5;
    if (!same(base, RVA_OBS_BACK, OBS_NEXT, sizeof OBS_NEXT)) return 6;
    if (!same(base, 0xe59fe, OBS_LIMIT, sizeof OBS_LIMIT)) return 7;
    return 0;
}

static void put64(unsigned char *p, ULONG_PTR v) { memcpy(p, &v, 8); }

// the two detours, laid out from cave: request at +0, observer at +0x40. Returns the bytes used (0x80).
static size_t build_caves(ULONG_PTR base, ULONG_PTR cave, unsigned char *out) {
    memset(out, 0xcc, 0x80);
    static const unsigned char req[] = {
        0x50,                                      // push rax
        0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,        // mov rax, base + RVA_LIMIT
        0x8b, 0x38,                                // mov edi, [rax]          (the game's own load)
        0x83, 0xff, 0x04,                          // cmp edi, 4
        0x75, 0x12,                                // jne done
        0xbf, 0x06, 0x00, 0x00, 0x00,              // mov edi, 6
        0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,        // mov rax, &g_req_count
        0xf0, 0xff, 0x00,                          // lock inc dword [rax]
        0x58,                                      // done: pop rax
        0xff, 0x25, 0, 0, 0, 0,                    // jmp [rip]
        0, 0, 0, 0, 0, 0, 0, 0};                   //   base + RVA_REQ_BACK
    static const unsigned char obs[] = {
        0x50,                                      // push rax
        0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,        // mov rax, &g_obs
        0x44, 0x89, 0x20,                          // mov [rax], r12d          (member count)
        0x44, 0x89, 0x78, 0x04,                    // mov [rax+4], r15d        (member limit)
        0xf0, 0xff, 0x40, 0x08,                    // lock inc dword [rax+8]
        0x58,                                      // pop rax
        0x45, 0x3b, 0xe7,                          // cmp r12d, r15d           (the game's own two instructions)
        0x0f, 0x93, 0xc0,                          // setae al
        0xff, 0x25, 0, 0, 0, 0,                    // jmp [rip]
        0, 0, 0, 0, 0, 0, 0, 0};                   //   base + RVA_OBS_BACK
    memcpy(out, req, sizeof req);
    put64(out + 3, base + RVA_LIMIT);
    put64(out + 25, (ULONG_PTR)&g_req_count);
    put64(out + 43, base + RVA_REQ_BACK);
    memcpy(out + 0x40, obs, sizeof obs);
    put64(out + 0x40 + 3, (ULONG_PTR)&g_obs);
    put64(out + 0x40 + 35, base + RVA_OBS_BACK);
    (void)cave;
    return 0x80;
}

static void site_bytes(ULONG_PTR site, ULONG_PTR target, unsigned char out[6]) {   // jmp rel32 ; nop
    LONG rel = (LONG)((LONGLONG)target - (LONGLONG)(site + 5));
    out[0] = 0xe9;
    memcpy(out + 1, &rel, 4);
    out[5] = 0x90;
}

static ULONG_PTR near_alloc(ULONG_PTR base) {      // one page within 2 GB after the image (rel32 reach)
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    ULONG_PTR end = base + nt->OptionalHeader.SizeOfImage;
    for (ULONG_PTR a = (end + 0xFFFF) & ~(ULONG_PTR)0xFFFF; a < base + 0x40000000; a += 0x10000) {
        void *p = VirtualAlloc((void *)a, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return (ULONG_PTR)p;
    }
    return 0;
}

static bool write_code(ULONG_PTR at, const unsigned char *b, size_t n) {
    DWORD old;
    if (!VirtualProtect((void *)at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void *)at, b, n);
    DWORD tmp;
    VirtualProtect((void *)at, n, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, n);
    return true;
}

// all or nothing: 0 = applied, else why not (1-7 a site byte differs, 10 no memory in reach, 11 out of reach, 12 write)
static int apply(ULONG_PTR base) {
    int bad = sites_check(base);
    if (bad) return bad;
    ULONG_PTR cave = near_alloc(base);
    if (!cave) return 10;
    unsigned char c[0x80], s1[6], s2[6];
    build_caves(base, cave, c);
    LONGLONG d1 = (LONGLONG)cave - (LONGLONG)(base + RVA_REQ + 5), d2 = (LONGLONG)(cave + 0x40) - (LONGLONG)(base + RVA_OBS + 5);
    if (d1 != (LONG)d1 || d2 != (LONG)d2) { VirtualFree((void *)cave, 0, MEM_RELEASE); return 11; }
    memcpy((void *)cave, c, sizeof c);
    FlushInstructionCache(GetCurrentProcess(), (void *)cave, sizeof c);
    site_bytes(base + RVA_REQ, cave, s1);
    site_bytes(base + RVA_OBS, cave + 0x40, s2);
    if (!write_code(base + RVA_OBS, s2, 6)) return 12;                 // the observer first: it changes nothing
    if (!write_code(base + RVA_REQ, s1, 6)) { write_code(base + RVA_OBS, OBS_SITE, 6); return 12; }
    return 0;
}

static LONG read_limit_setting() {                 // *(*(*0x1404bbc48) + 0xe0) + 0x48, -1 when not there
    __try {
        ULONG_PTR p = *(ULONG_PTR *)(g_base + RVA_SETTINGS);
        if (!p) return -1;
        ULONG_PTR obj = *(ULONG_PTR *)p;
        if (!obj) return -1;
        ULONG_PTR st = *(ULONG_PTR *)(obj + 0xe0);
        if (!st) return -1;
        return *(LONG *)(st + 0x48);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

static DWORD WINAPI worker(LPVOID) {
    LONG last_req = 0, last_count = 0, last_members = -1, last_limit = -1, last_setting = -2;
    for (;;) {
        Sleep(500);
        LONG req = g_req_count, count = g_obs.count, members = g_obs.members, limit = g_obs.limit;
        LONG setting = read_limit_setting();
        if (req != last_req) logf("request: the game asked for 4 members, the session was given 6 (time %ld)", req);
        if (setting != last_setting) logf("session settings member limit: %ld (game's own count %ld)", setting,
                                          *(volatile LONG *)(g_base + RVA_LIMIT));
        if (count != last_count && (members != last_members || limit != last_limit))
            logf("game's full check (from Xbox Live's session): members %ld, member limit %ld%s", members, limit,
                 members == 0 && limit == 1 ? " (no game session yet)" : "");
        if (count != last_count) last_members = members, last_limit = limit;
        last_req = req, last_count = count, last_setting = setting;
    }
}

extern "C" __declspec(dllexport) int CwoLoaderVersion() { return 0x5336; }       // "S6"
extern "C" __declspec(dllexport) void CwoOverlayFrame() {}                       // the exe's per-frame call: nothing to draw

// offline test entry points (tools/test_s6t.py)
extern "C" __declspec(dllexport) int S6TCheckSites(ULONG_PTR base) { return sites_check(base); }
extern "C" __declspec(dllexport) int S6TBuild(ULONG_PTR base, ULONG_PTR cave, unsigned char *out) {
    return (int)build_caves(base, cave, out);
}
extern "C" __declspec(dllexport) int S6TApply(ULONG_PTR base) { return apply(base); }
extern "C" __declspec(dllexport) ULONG_PTR S6TData(int which) {
    return which == 0 ? (ULONG_PTR)&g_req_count : (ULONG_PTR)&g_obs;
}

static bool host_is_game() {
    wchar_t p[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, p, MAX_PATH);
    return n >= 9 && n < MAX_PATH && !_wcsicmp(p + n - 9, L"PDUWP.exe");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        if (!host_is_game()) return TRUE;                          // loaded by a test: stay idle
        InitializeCriticalSection(&g_log_lock);
        g_base = (ULONG_PTR)GetModuleHandleW(NULL);
        DWORD n = GetTempPathW(MAX_PATH, g_dir);
        if (n == 0 || n + 12 >= MAX_PATH) g_dir[0] = 0;
        wcscat_s(g_dir, MAX_PATH, L"CwoLoader\\");
        CreateDirectoryW(g_dir, NULL);
        int r = apply(g_base);
        g_state = r ? -1 : 1;
        logf("CWO-S6T-1 session size test: %s (code %d); asks Xbox Live for 6 members where the game asks for 4; "
             "nothing else changed", r ? "REFUSED, nothing changed" : "active", r);
        if (!r) {
            HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
            if (t) CloseHandle(t);
        }
    }
    return TRUE;
}
