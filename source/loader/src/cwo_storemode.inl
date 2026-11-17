// v5.38 Store mode (owner 2026-10-31). Independently authored from the game's own
// facts and official Windows documentation (PE/COFF, VirtualProtect, FlushInstructionCache, CreateFile2, GetTempPath).
//
// The Microsoft Store install keeps PDUWP.exe and the game files encrypted and unchangeable on disk, so in Store mode:
// - the launcher (tools/CwoLaunch) starts the game paused and loads this DLL before any game code runs;
// - the exe changes are written into the game's memory here (cwo_mempatch.h, built by tools/build_mem_patch.py from the
//   clean vs the modded exe): every range is checked against the clean bytes first, then written, the modded exe's
//   pointer sites get Windows' load offset added, and the code's call into CwoOverlayFrame goes through a slot in
//   the free .text tail that this DLL fills with its own export;
// - every game file open (the import-table wrappers) looks in <TEMP>\CwoLoader\game\ first: a file there with the
//   same path as in the game folder (e.g. game\Assets\Data\Map\st30.dat) is used instead; new files can be added the
//   same way. The loader's own reads of game-folder files (cwo_wfopen) do the same, and its writes go there.
// Store mode is on only when the running exe has no '.cwo' section (the disk-patched exe of a loose copy has it; there
// nothing changes).
#include "cwo_mempatch.h"
#include "cwo_sha256.h"                         // v5.58 the clean-byte check by hash

static bool g_store_mode;                        // the clean exe is running: memory patch + file overrides
static int g_mp_state;                           // 0 not tried, 1 applied, -1 refused (bytes not clean), 2 not needed
static wchar_t g_install[MAX_PATH];              // the game folder (exe folder) with a trailing backslash
static wchar_t g_over[MAX_PATH];                 // <TEMP>\CwoLoader\game\ (Store mode overrides)
static HMODULE g_self;
static const ULONG_PTR MP_PREFERRED_BASE = 0x140000000;

static bool exe_has_section(const char *name) {
    BYTE *b = (BYTE *)g_base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (strncmp((const char *)s[i].Name, name, 8) == 0) return true;
    return false;
}

static bool mp_write(ULONG_PTR addr, const void *src, size_t n) {
    DWORD old;
    if (!VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void *)addr, src, n);
    VirtualProtect((void *)addr, n, old, &old);
    return true;
}

static const unsigned MP_TEXT_END = 0x2d2546;           // .text RVA 0x1000 + VirtualSize 0x2d1546 (the clean exe's header)
static int g_mp_paused;                                  // how many game threads were paused for pass 2 (logged later)

// Pause every other thread of the game (no heap use: the thread list is taken before any thread is paused, the rest is
// system calls). A paused thread whose RIP is inside a code range about to change (not at its first byte) is let run on
// and the pause tried again, up to 50 times.
static void mp_resume(HANDLE *h, int k) { for (int i = 0; i < k; i++) { ResumeThread(h[i]); CloseHandle(h[i]); } }
static bool mp_inside_range(ULONG_PTR rip) {
    for (const MpRange &r : MP_RANGES)
        if (r.rva < MP_TEXT_END && rip > g_base + r.rva && rip < g_base + r.rva + r.len) return true;
    return false;
}
static int mp_pause_others(HANDLE *h, int cap) {
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (int tries = 0; tries < 50; tries++) {
        DWORD ids[256]; int n = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        THREADENTRY32 te; te.dwSize = sizeof(te);
        if (Thread32First(snap, &te))
            do {
                if (te.th32OwnerProcessID == pid && te.th32ThreadID != me && n < 256) ids[n++] = te.th32ThreadID;
                te.dwSize = sizeof(te);
            } while (Thread32Next(snap, &te));
        CloseHandle(snap);
        int k = 0; bool busy = false;
        for (int i = 0; i < n && k < cap; i++) {
            HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ids[i]);
            if (!t) continue;
            if (SuspendThread(t) == (DWORD)-1) { CloseHandle(t); continue; }
            h[k++] = t;
            CONTEXT c; memset(&c, 0, sizeof(c)); c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(t, &c) && mp_inside_range((ULONG_PTR)c.Rip)) busy = true;
        }
        if (!busy) return k;
        mp_resume(h, k);
        Sleep(1);
    }
    return 0;                                            // 50 tries: written without the pause (as before 5.177)
}

// returns 1 applied, 2 not needed (disk-patched exe), -1 refused (a range is neither clean nor patched), -2 write failed
static int mp_apply() {
    if (exe_has_section(".cwo")) return 2;
    const int n = sizeof(MP_RANGES) / sizeof(MP_RANGES[0]);
    for (int i = 0; i < n; i++) {
        const MpRange &r = MP_RANGES[i];
        unsigned char hh[32];
        if (r.check) cwo_sha256((const void *)(g_base + r.rva), r.len, hh);
        if (r.check && memcmp(hh, MP_CLEAN_SHA + 32 * i, 32) != 0) {
            logf("store mode: exe bytes at %llx are not the clean 1.40 build; memory patch refused",
                 (unsigned long long)(MP_PREFERRED_BASE + r.rva));
            return -1;
        }
    }
    ULONG_PTR delta = g_base - MP_PREFERRED_BASE;
    // v5.177.0 (start-up crash 2026-11-13 14:05 at PDUWP+0x2d2550): the hooks at 0x17f380 (jmp 0x2d2550) and 0x28caa0 (jmp 0x2d2780)
    // went in BEFORE the new code they jump to; a game thread running FUN_14017f380 in that gap jumped into the empty padding.
    // Now: pass 1 = everything outside the game's code (new code after .text's end 0x2d2546, strings, tables) + the loader slots;
    // pass 2 = the changes inside the game's code, with the game's other threads paused.
    for (int pass = 1; pass <= 2; pass++) {
        HANDLE held[256]; int nh = 0;
        if (pass == 2) {
            FlushInstructionCache(GetCurrentProcess(), (void *)g_base, 0x603000);
            nh = mp_pause_others(held, 256);
        }
        bool ok = true;
        for (int i = 0; i < n && ok; i++) {
            const MpRange &r = MP_RANGES[i];
            if ((r.rva < MP_TEXT_END) != (pass == 2)) continue;
            BYTE buf[2048];
            if (r.len > sizeof(buf)) { ok = false; break; }
            memcpy(buf, MP_BYTES + r.off, r.len);
            for (unsigned k = 0; k < sizeof(MP_RELOCS) / sizeof(MP_RELOCS[0]); k++) {
                unsigned s = MP_RELOCS[k];
                if (s >= r.rva && s + 8 <= r.rva + r.len) *(ULONGLONG *)(buf + (s - r.rva)) += delta;
            }
            if (!mp_write(g_base + r.rva, buf, r.len)) ok = false;
        }
        if (pass == 1)
            for (unsigned k = 0; k < sizeof(MP_SLOTS) / sizeof(MP_SLOTS[0]) && ok; k++) {
                ULONGLONG fn = (ULONGLONG)GetProcAddress(g_self, MP_SLOTS[k].name);
                if (!fn || !mp_write(g_base + MP_SLOTS[k].rva, &fn, 8)) ok = false;
            }
        if (pass == 2) {
            FlushInstructionCache(GetCurrentProcess(), (void *)g_base, 0x603000);
            mp_resume(held, nh);
            g_mp_paused = nh;
        }
        if (!ok) return -2;
    }
    FlushInstructionCache(GetCurrentProcess(), (void *)g_base, 0x603000);
    return 1;
}

// the game-folder relative part of a path ("Assets/Data/.\menu\x.ssb", "3dsprite_ps.cso", or an absolute path inside the
// game folder) -> "Assets\Data\menu\x.ssb"; false for paths outside the game folder
static bool sm_relative(const wchar_t *in, wchar_t *rel, size_t cap) {
    if (!in || !in[0]) return false;
    size_t li = wcslen(g_install);
    const wchar_t *p = in;
    if ((in[0] && in[1] == L':') || (in[0] == L'\\' && in[1] == L'\\')) {
        if (!li || _wcsnicmp(in, g_install, li) != 0) return false;
        p = in + li;
    }
    size_t o = 0;
    while (*p && o + 1 < cap) {
        wchar_t c = *p == L'/' ? L'\\' : *p;
        if (c == L'.' && (o == 0 || rel[o - 1] == L'\\') && (p[1] == L'\\' || p[1] == L'/')) { p += 2; continue; }
        if (c == L'\\' && o > 0 && rel[o - 1] == L'\\') { p++; continue; }
        if (c == L'\\' && o == 0) { p++; continue; }
        rel[o++] = c;
        p++;
    }
    rel[o] = 0;
    return o > 0 && !*p;
}

typedef DWORD(WINAPI *PFN_GFA2)(LPCWSTR);
static PFN_GFA2 g_real_gfa;                      // the real GetFileAttributesW (never the game's switched slot)

// Store mode: the override path for a game-folder path. want_write: the override is used (its folders created) even if
// the file does not exist yet. Returns false when the original path should be used.
static bool sm_redirect(const wchar_t *in, wchar_t *out, size_t cap, bool want_write) {
    if (!g_store_mode || !g_over[0]) return false;
    wchar_t rel[MAX_PATH];
    if (!sm_relative(in, rel, MAX_PATH)) return false;
    if (swprintf_s(out, cap, L"%s%s", g_over, rel) < 0) return false;
    if (wcschr(rel, L'*') || wcschr(rel, L'?')) return false;          // folder listings stay on the game folder
    if (!g_real_gfa) g_real_gfa = GetFileAttributesW;
    if (g_real_gfa(out) != INVALID_FILE_ATTRIBUTES) return true;
    if (!want_write) return false;
    for (wchar_t *s = wcschr(out + wcslen(g_over), L'\\'); s; s = wcschr(s + 1, L'\\')) {
        *s = 0; CreateDirectoryW(out, NULL); *s = L'\\';
    }
    return true;
}

// the loader's own file opens: game-folder paths read the override when there is one, and write to the override in
// Store mode (the Store game folder cannot be written)
static errno_t cwo_wfopen(FILE **f, const wchar_t *path, const wchar_t *mode) {
    wchar_t o[MAX_PATH];
    bool w = mode && (wcschr(mode, L'w') || wcschr(mode, L'a') || wcschr(mode, L'+'));
    if (sm_redirect(path, o, MAX_PATH, w)) return _wfopen_s(f, o, mode);
    return _wfopen_s(f, path, mode);
}

static void sm_init(HMODULE self) {
    g_self = self;
    GetModuleFileNameW(NULL, g_install, MAX_PATH);
    wchar_t *slash = wcsrchr(g_install, L'\\');
    if (slash) slash[1] = 0; else g_install[0] = 0;
    swprintf_s(g_over, MAX_PATH, L"%sgame\\", g_dir);
    g_store_mode = !exe_has_section(".cwo");
    if (g_store_mode) CreateDirectoryW(g_over, NULL);
}

// offline test (tests/mp_test.cpp): apply the memory patch to a mapped, not running, copy of the clean exe
extern "C" __declspec(dllexport) int CwoMemPatchTest(HMODULE image) {
    ULONG_PTR keep = g_base;
    g_base = (ULONG_PTR)image;
    int r = mp_apply();
    g_base = keep;
    return r;
}

// offline test (tests/redir_test.cpp): the override lookup with a stand-in game folder and override folder
extern "C" __declspec(dllexport) int CwoRedirectTest(const wchar_t *fake_install, const wchar_t *fake_over) {
    wchar_t keep_i[MAX_PATH], keep_o[MAX_PATH];
    bool keep_s = g_store_mode;
    wcscpy_s(keep_i, MAX_PATH, g_install); wcscpy_s(keep_o, MAX_PATH, g_over);
    wcscpy_s(g_install, MAX_PATH, fake_install); wcscpy_s(g_over, MAX_PATH, fake_over);
    g_store_mode = true;
    wchar_t want[MAX_PATH], o[MAX_PATH], abs_in[MAX_PATH], out_in[MAX_PATH];
    swprintf_s(want, MAX_PATH, L"%sAssets\\Data\\menu\\x.ssb", fake_over);
    swprintf_s(abs_in, MAX_PATH, L"%sAssets\\Data\\menu\\x.ssb", fake_install);
    swprintf_s(out_in, MAX_PATH, L"%sx.ssb", fake_over);
    int fails = 0;
    if (!sm_redirect(L"Assets/Data/.\\menu\\x.ssb", o, MAX_PATH, false) || _wcsicmp(o, want)) fails |= 1;   // the game's mixed form
    if (!sm_redirect(L"assets\\data\\menu\\X.SSB", o, MAX_PATH, false) || _wcsicmp(o, want)) fails |= 2;    // case
    if (!sm_redirect(abs_in, o, MAX_PATH, false) || _wcsicmp(o, want)) fails |= 4;                          // absolute
    if (sm_redirect(L"Assets\\Data\\menu\\missing.ssb", o, MAX_PATH, false)) fails |= 8;                   // no override
    if (sm_redirect(out_in, o, MAX_PATH, false)) fails |= 16;                                               // outside the game folder
    if (sm_redirect(L"Assets\\Data\\menu\\*.ssb", o, MAX_PATH, false)) fails |= 32;                        // listings
    wchar_t wfile[MAX_PATH];
    swprintf_s(wfile, MAX_PATH, L"%snew\\dir\\made.txt", fake_install);
    FILE *f = NULL;
    if (cwo_wfopen(&f, wfile, L"w") != 0 || !f) fails |= 64; else { fputs("x", f); fclose(f); }
    swprintf_s(want, MAX_PATH, L"%snew\\dir\\made.txt", fake_over);
    if (GetFileAttributesW(want) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(wfile) != INVALID_FILE_ATTRIBUTES) fails |= 128;  // write went to the override
    wcscpy_s(g_install, MAX_PATH, keep_i); wcscpy_s(g_over, MAX_PATH, keep_o);
    g_store_mode = keep_s;
    return fails;
}
