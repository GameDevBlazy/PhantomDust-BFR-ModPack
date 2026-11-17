// CwoLaunch.exe - starts the Microsoft Store Phantom Dust with CWO-LOADER-01 loaded (v5.38 Store mode, 2026-10-31).
// Independently authored from official Windows documentation only: IPackageDebugSettings (EnableDebugging /
// DisableDebugging: Windows starts the app's process suspended and runs the given "debugger" command with
// "-p <pid> -tid <tid>"), IApplicationActivationManager::ActivateApplication, GetPackagesByPackageFamily,
// VirtualAllocEx / WriteProcessMemory / CreateRemoteThread(LoadLibraryW), ResumeThread. No community code or design.
//
// CwoLaunch.exe                 (late mode, default) start the game normally and load the DLL right after Windows
//                               initialised the process; the old paused start is --paused.
// CwoLaunch.exe --paused        copy CwoLoader.dll (next to this exe) into the game's own temp folder
//                               (%LOCALAPPDATA%\Packages\<family>\AC\Temp\CwoLoader\, readable by the game), turn the
//                               debug start on for this one launch, start the game, turn it off again.
// CwoLaunch.exe --attach -p P -tid T   (run by Windows) load the DLL into the paused game, then let it run.
// The Store files are never touched; starting Phantom Dust normally (Start menu) runs the stock game.
#include <windows.h>
#include <shobjidl.h>
#include <appmodel.h>
#include <tlhelp32.h>
#define PSAPI_VERSION 2
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")

static const wchar_t *FAMILY = L"Microsoft.MSEsper_8wekyb3d8bbwe";
static const wchar_t *APP_ID = L"Microsoft.MSEsper_8wekyb3d8bbwe!App";
static const wchar_t *DONE_EVENT = L"Local\\CwoLaunchAttachDone";
static wchar_t g_log[MAX_PATH];

static void say(const wchar_t *fmt, ...) {
    wchar_t line[1024];
    va_list a;
    va_start(a, fmt);
    vswprintf_s(line, 1024, fmt, a);
    va_end(a);
    SYSTEMTIME t;
    GetLocalTime(&t);
    FILE *f = NULL;
    if (g_log[0] && _wfopen_s(&f, g_log, L"a, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"%04d-%02d-%02d %02d:%02d:%02d %s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, line);
        fclose(f);
    }
    wprintf(L"%s\n", line);
}

// where the DLL goes: LocalState\CwoLoader\ (readable by the app, medium integrity). NOT the AC\ tree: everything under
// AC\ carries the Low mandatory label (icacls) and a Store app refuses a low-label image - Windows ends it with
// "issue with Windows binary" (Event 9, 0xC000047E; seen 2026-10-31 with the DLL in AC\Temp). The loader's own data
// (log, overrides) stays in the game's temp folder AC\Temp\CwoLoader\: data reads are fine there.
static bool data_dir(wchar_t *out) {             // the game's temp folder + CwoLoader\ (the loader's own data folder)
    wchar_t la[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) return false;
    swprintf_s(out, MAX_PATH, L"%s\\Packages\\%s\\LocalState\\CwoLoader\\", la, FAMILY);
    return true;
}

#include <ctype.h>
#include "cwo_update.inl"                                     // v5.153 the BFR pack updater
#include "cwo_resync.inl"                                     // v5.188.8 the BFR pack resync (fail-safe)

// ---- v5.188.10 the player's own save backup on request (owner 2026-11-16, "go"). The game (a sandboxed Store app) cannot write to the desktop and
// its save may be open while it runs, so MOD LIST > X only leaves <data>\CwoSaveBackupRequest.txt; here, before the game
// starts, the pack's own backup_save.ps1 (next to this launcher, or one / two folders up) makes the backup on the desktop
// ("Phantom Dust save backups", newest 3 kept) and leaves one line in <data>\CwoSaveBackupResult.txt for MOD LIST.
// v5.188.14 (owner 2026-11-16: a player's launcher window closed at once - the game was already running, suspended): when
// the game did not start, the window waits (a key, or 60 s) so the reason can be read
#include <conio.h>
static void hold_window() {
    say(L"");
    say(L"Phantom Dust did not start - the lines above say why. Press any key to close this window (it closes by itself in 60 s).");
    ULONGLONG t = GetTickCount64();
    while (GetTickCount64() - t < 60000) { if (_kbhit()) { _getch(); break; } Sleep(100); }
}
static void save_backup_request(const wchar_t *data) {
    wchar_t req[MAX_PATH], res[MAX_PATH], ps1[MAX_PATH], exe[MAX_PATH];
    swprintf_s(req, MAX_PATH, L"%sCwoSaveBackupRequest.txt", data);
    if (GetFileAttributesW(req) == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileW(req);
    swprintf_s(res, MAX_PATH, L"%sCwoSaveBackupResult.txt", data);
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    bool found = false;
    for (int up = 0; up < 3 && !found; up++) {
        wchar_t *sl = wcsrchr(exe, L'\\'); if (!sl) break;
        *sl = 0;
        swprintf_s(ps1, MAX_PATH, L"%s\\backup_save.ps1", exe);
        found = GetFileAttributesW(ps1) != INVALID_FILE_ATTRIBUTES;
    }
    if (!found) { say(L"save backup: backup_save.ps1 not found next to the launcher"); FILE *f = NULL; if (_wfopen_s(&f, res, L"w") == 0 && f) { fputs("not made: run the install again\n", f); fclose(f); } return; }
    say(L"Backing up your Phantom Dust save (asked for in the game) ...");
    wchar_t cmd[2048];
    swprintf_s(cmd, 2048, L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\" -Why \"asked for in the game\" -ResultFile \"%s\"", ps1, res);
    STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { say(L"save backup: powershell could not start (%lu)", GetLastError()); return; }
    WaitForSingleObject(pi.hProcess, 120000);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}

static bool same_file(const wchar_t *a, const wchar_t *b) {
    HANDLE ha = CreateFileW(a, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE hb = CreateFileW(b, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    bool same = false;
    if (ha != INVALID_HANDLE_VALUE && hb != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sa, sb;
        if (GetFileSizeEx(ha, &sa) && GetFileSizeEx(hb, &sb) && sa.QuadPart == sb.QuadPart) {
            same = true;
            static BYTE x[1 << 16], y[1 << 16];
            DWORD ga, gb;
            while (same && ReadFile(ha, x, sizeof(x), &ga, NULL) && ReadFile(hb, y, sizeof(y), &gb, NULL) && ga) same = ga == gb && !memcmp(x, y, ga);
        }
    }
    if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
    if (hb != INVALID_HANDLE_VALUE) CloseHandle(hb);
    return same;
}

// is a module with this file name in the process's loaded-module list? (true only after Windows initialised the process)
static bool has_module(HANDLE p, const wchar_t *name) {
    HMODULE mods[512];
    DWORD got = 0;
    if (!EnumProcessModulesEx(p, mods, sizeof(mods), &got, LIST_MODULES_64BIT)) return false;
    for (DWORD i = 0; i < got / sizeof(HMODULE); i++) {
        wchar_t n[MAX_PATH];
        if (GetModuleBaseNameW(p, mods[i], n, MAX_PATH) && !_wcsicmp(n, name)) return true;
    }
    return false;
}

// load the DLL into the process through LoadLibraryW; true only when the DLL is in the module list afterwards
static bool inject(HANDLE p, const wchar_t *dll) {
    size_t n = (wcslen(dll) + 1) * sizeof(wchar_t);
    void *mem = VirtualAllocEx(p, NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    FARPROC ll = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");   // same address in every process this boot
    HANDLE rt = NULL;
    if (mem && WriteProcessMemory(p, mem, dll, n, NULL))
        rt = CreateRemoteThread(p, NULL, 0, (LPTHREAD_START_ROUTINE)ll, mem, 0, NULL);
    if (!rt) { say(L"could not start the load in the game (error %lu)", GetLastError()); if (mem) VirtualFreeEx(p, mem, 0, MEM_RELEASE); return false; }
    DWORD w = WaitForSingleObject(rt, 30000), code = 0;
    GetExitCodeThread(rt, &code);
    CloseHandle(rt);
    DWORD pc = 0;
    bool alive = GetExitCodeProcess(p, &pc) && pc == STILL_ACTIVE;
    const wchar_t *nm = wcsrchr(dll, L'\\'); nm = nm ? nm + 1 : dll;
    bool listed = alive && has_module(p, nm);
    if (alive) VirtualFreeEx(p, mem, 0, MEM_RELEASE);
    say(L"load: wait %lu, LoadLibrary result %lx, game %s, %s %s", w, code, alive ? L"running" : L"GONE", nm, listed ? L"in the module list" : L"NOT loaded");
    return listed;
}

static int attach(DWORD pid, DWORD tid) {
    wchar_t dir[MAX_PATH], dll[MAX_PATH];
    data_dir(dir);
    swprintf_s(dll, MAX_PATH, L"%sCwoLoader.dll", dir);
    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, DONE_EVENT);
    int rc = 1;
    HANDLE p = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME, FALSE, tid);
    if (!p || !t) say(L"attach: cannot open the game process %lu / thread %lu (error %lu)", pid, tid, GetLastError());
    else if (inject(p, dll)) { say(L"attach: CwoLoader.dll loaded into PDUWP.exe (pid %lu)", pid); rc = 0; }
    else say(L"attach: CwoLoader.dll did NOT load - the game runs stock (or was ended by Windows)");
    if (t) { ResumeThread(t); CloseHandle(t); }     // always let the game run
    if (p) CloseHandle(p);
    if (ev) { SetEvent(ev); CloseHandle(ev); }
    return rc;
}

// late mode (default): the game starts normally; as soon as Windows has initialised the process (kernel32 in its module
// list) the DLL is loaded - the way UWP Dumper loads its DLL into Store apps. The paused start (--paused) ended the game:
// Windows refuses an unsigned DLL that is already inside a Store app process when it initialises (event 9, 0xC000047E).
static const wchar_t *g_dll_override;            // --dll <path>: load this DLL instead (tests)

// --wait: do not start the game; wait (up to 30 s) for a PDUWP.exe that something else starts (the owner's double-click,
// or an Explorer shell:AppsFolder activation from a script - a Store app activated straight from a background shell
// starts invisible and suspended), then load the DLL as late mode does.
static bool g_wait_mode;

static int launch_late() {
    wchar_t dir[MAX_PATH], src[MAX_PATH], dst[MAX_PATH], self[MAX_PATH];
    if (!data_dir(dir)) return 2;
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wcscpy_s(src, MAX_PATH, self);
    wchar_t *sl = wcsrchr(src, L'\\');
    wcscpy_s(sl + 1, MAX_PATH - (sl + 1 - src), L"CwoLoader.dll");
    swprintf_s(dst, MAX_PATH, L"%sCwoLoader.dll", dir);
    if (g_dll_override) {                                    // tests: the named DLL, copied under its own name
        wcscpy_s(src, MAX_PATH, g_dll_override);
        const wchar_t *nm = wcsrchr(src, L'\\'); nm = nm ? nm + 1 : src;
        swprintf_s(dst, MAX_PATH, L"%s%s", dir, nm);
    }
    if (GetFileAttributesW(src) == INVALID_FILE_ATTRIBUTES) { say(L"No %s.", src); return 2; }
    UINT32 count = 0, buflen = 0;
    GetPackagesByPackageFamily(FAMILY, &count, NULL, &buflen, NULL);
    if (!count) { say(L"Phantom Dust (%s) is not installed for this Windows account.", FAMILY); return 2; }
    DWORD running = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = {sizeof(pe)};
    if (snap != INVALID_HANDLE_VALUE) {
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
            if (!_wcsicmp(pe.szExeFile, L"PDUWP.exe")) running = pe.th32ProcessID;
        CloseHandle(snap);
    }
    if (running && !g_wait_mode) { say(L"Phantom Dust is already running (pid %lu) - maybe an earlier start that froze in the background (\"Suspended\").", running); say(L"Close it: Task Manager > Details > PDUWP.exe > End task, then start Phantom Dust BFR again."); return 3; }
    CreateDirectoryW(dir, NULL);
    if (!same_file(src, dst)) {
        if (!CopyFileW(src, dst, FALSE)) { say(L"cannot copy CwoLoader.dll to %s (error %lu)", dst, GetLastError()); return 2; }
        say(L"CwoLoader.dll copied to %s", dst);
    }
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return 2;
    IApplicationActivationManager *am = NULL;
    int rc = 2;
    if (FAILED(CoCreateInstance(CLSID_ApplicationActivationManager, NULL, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&am)))) say(L"Windows app start interface unavailable");
    else {
        DWORD pid = 0;
        HRESULT hr = S_OK;
        if (g_wait_mode) {
            ULONGLONG w0 = GetTickCount64();
            while (!pid && GetTickCount64() - w0 < 30000) {
                HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                PROCESSENTRY32W e = {sizeof(e)};
                if (sn != INVALID_HANDLE_VALUE) {
                    for (BOOL ok = Process32FirstW(sn, &e); ok; ok = Process32NextW(sn, &e))
                        if (!_wcsicmp(e.szExeFile, L"PDUWP.exe")) pid = e.th32ProcessID;
                    CloseHandle(sn);
                }
                if (!pid) Sleep(5);
            }
            if (!pid) { say(L"wait mode: no PDUWP.exe appeared in 30 s"); hr = E_FAIL; }
            else say(L"wait mode: PDUWP.exe appeared (pid %lu)", pid);
        } else hr = am->ActivateApplication(APP_ID, NULL, AO_NONE, &pid);
        if (FAILED(hr)) { if (!g_wait_mode) say(L"could not start Phantom Dust (0x%08lx)", hr); }
        else {
            ULONGLONG t0 = GetTickCount64();
            HANDLE p = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
            if (!p) say(L"Phantom Dust started (pid %lu) but its process cannot be opened (error %lu)", pid, GetLastError());
            else {
                while (!has_module(p, L"kernel32.dll") && GetTickCount64() - t0 < 15000) Sleep(2);
                say(L"Phantom Dust started (pid %lu); initialised after %llu ms - loading %s", pid, GetTickCount64() - t0, dst);
                if (inject(p, dst)) {
                    Sleep(3000);
                    DWORD pc = 0;
                    bool alive = GetExitCodeProcess(p, &pc) && pc == STILL_ACTIVE;
                    say(alive ? L"done: the game is running with the loader (3 s check)" : L"the game ended within 3 s of the load - see the Windows event log");
                    rc = alive ? 0 : 1;
                } else rc = 1;
                CloseHandle(p);
            }
        }
        am->Release();
    }
    CoUninitialize();
    return rc;
}

static int launch() {
    wchar_t dir[MAX_PATH], src[MAX_PATH], dst[MAX_PATH], self[MAX_PATH];
    if (!data_dir(dir)) return 2;
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wcscpy_s(src, MAX_PATH, self);
    wchar_t *sl = wcsrchr(src, L'\\');
    wcscpy_s(sl + 1, MAX_PATH - (sl + 1 - src), L"CwoLoader.dll");
    swprintf_s(dst, MAX_PATH, L"%sCwoLoader.dll", dir);
    if (GetFileAttributesW(src) == INVALID_FILE_ATTRIBUTES) { say(L"No CwoLoader.dll next to CwoLaunch.exe."); return 2; }
    UINT32 count = 0, buflen = 0;
    GetPackagesByPackageFamily(FAMILY, &count, NULL, &buflen, NULL);
    if (!count) { say(L"Phantom Dust (%s) is not installed for this Windows account.", FAMILY); return 2; }
    PWSTR names[4] = {};
    wchar_t buf[1024];
    if (count > 4 || buflen > 1024 || GetPackagesByPackageFamily(FAMILY, &count, names, &buflen, buf) != ERROR_SUCCESS) { say(L"package lookup failed"); return 2; }
    say(L"Phantom Dust package: %s", names[0]);
    DWORD running = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = {sizeof(pe)};
    if (snap != INVALID_HANDLE_VALUE) {
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
            if (!_wcsicmp(pe.szExeFile, L"PDUWP.exe")) running = pe.th32ProcessID;
        CloseHandle(snap);
    }
    if (running) { say(L"Phantom Dust is already running (pid %lu) - maybe an earlier start that froze in the background (\"Suspended\").", running); say(L"Close it: Task Manager > Details > PDUWP.exe > End task, then start Phantom Dust BFR again."); return 3; }
    CreateDirectoryW(dir, NULL);
    if (!same_file(src, dst)) {
        if (!CopyFileW(src, dst, FALSE)) { say(L"cannot copy CwoLoader.dll to %s (error %lu)", dst, GetLastError()); return 2; }
        say(L"CwoLoader.dll copied to %s", dst);
    }
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, DONE_EVENT);
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return 2;
    IPackageDebugSettings *dbg = NULL;
    IApplicationActivationManager *am = NULL;
    int rc = 2;
    if (FAILED(CoCreateInstance(CLSID_PackageDebugSettings, NULL, CLSCTX_ALL, IID_PPV_ARGS(&dbg))) ||
        FAILED(CoCreateInstance(CLSID_ApplicationActivationManager, NULL, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&am)))) {
        say(L"Windows app start interfaces unavailable");
    } else {
        wchar_t cmd[MAX_PATH + 32];
        swprintf_s(cmd, MAX_PATH + 32, L"\"%s\" --attach", self);
        HRESULT hr = dbg->EnableDebugging(names[0], cmd, NULL);
        if (FAILED(hr)) say(L"could not set the paused start (0x%08lx)", hr);
        else {
            DWORD pid = 0;
            hr = am->ActivateApplication(APP_ID, NULL, AO_NONE, &pid);
            if (FAILED(hr)) say(L"could not start Phantom Dust (0x%08lx)", hr);
            else {
                say(L"Phantom Dust started (pid %lu); waiting for the loader", pid);
                DWORD r = WaitForSingleObject(ev, 60000);
                say(r == WAIT_OBJECT_0 ? L"done - see CwoLaunch.log for the load result" : L"no answer from the attach step in 60 s");
                rc = r == WAIT_OBJECT_0 ? 0 : 1;
            }
            dbg->DisableDebugging(names[0]);           // always: the next normal start is the stock game
            say(L"paused start turned off again");
        }
    }
    if (am) am->Release();
    if (dbg) dbg->Release();
    CoUninitialize();
    if (ev) CloseHandle(ev);
    return rc;
}


// v5.150.0 (owner 2026-11-11: every player gets "the exact same stuff"; AGENTS.md: no third-party font in a pack): the CREATE
// editor's font atlas CwoUiFont.bin is made HERE on the player's own PC from the Segoe UI fonts Windows installs, when the
// edition's data folder (AC\Temp\CwoLoader[BFR]\) does not have it yet. The same atlas as tools/bake_ui_font.py: 'CWUF' u16 1,
// u16 1024, u16 256, u16 5 fonts; per font u16 px, i16 line, i16 ascent, 95 glyph records (u16 x, u16 y, u8 w, u8 h, i8 ox,
// i8 oy, i8 advance, u8 0); then 1024 x 256 coverage bytes, (0..3, 0..3) solid white. Windows GDI only (GetGlyphOutlineW
// GGO_GRAY8_BITMAP).
static bool bake_ui_font(const wchar_t *out) {
    struct F { const wchar_t *face; int px; int weight; };
    static const F FONTS[5] = {{L"Segoe UI Semibold", 30, 600}, {L"Segoe UI", 22, 400}, {L"Segoe UI", 18, 400},
                               {L"Segoe UI Semibold", 18, 600}, {L"Bahnschrift", 40, 400}};
    const int AW = 1024, AH = 256;
    static unsigned char atlas[1024 * 256];
    memset(atlas, 0, sizeof(atlas));
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) atlas[y * AW + x] = 255;
    static unsigned char blob[5 * (6 + 95 * 10)];
    int bn = 0;
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) return false;
    MAT2 id = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
    int cx = 8, cy = 0, row_h = 0;
    static unsigned char buf[256 * 256];
    for (int fi = 0; fi < 5; fi++) {
        HFONT hf = CreateFontW(-FONTS[fi].px, 0, 0, 0, FONTS[fi].weight, 0, 0, 0, 0, 0, 0, 5, 0, FONTS[fi].face);
        HGDIOBJ old = SelectObject(hdc, hf);
        TEXTMETRICW tm = {};
        GetTextMetricsW(hdc, &tm);
        short line = (short)(tm.tmHeight + tm.tmExternalLeading), asc = (short)tm.tmAscent;
        unsigned short px = (unsigned short)FONTS[fi].px;
        memcpy(blob + bn, &px, 2); memcpy(blob + bn + 2, &line, 2); memcpy(blob + bn + 4, &asc, 2); bn += 6;
        for (int ch = 32; ch < 127; ch++) {
            GLYPHMETRICS gm = {};
            DWORD n = GetGlyphOutlineW(hdc, ch, GGO_GRAY8_BITMAP, &gm, 0, NULL, &id);
            int w = (n != GDI_ERROR && n > 0) ? (int)gm.gmBlackBoxX : 0, h = (n != GDI_ERROR && n > 0) ? (int)gm.gmBlackBoxY : 0;
            if (ch == 32 || n == GDI_ERROR || n == 0) w = h = 0;
            if (cx + w + 2 > AW) { cx = 0; cy += row_h + 2; row_h = 0; }
            int gx = cx, gy = cy;
            if (w && h && n <= sizeof(buf) && gy + h <= AH) {
                GetGlyphOutlineW(hdc, ch, GGO_GRAY8_BITMAP, &gm, n, buf, &id);
                int pitch = (w + 3) & ~3;
                for (int yy = 0; yy < h; yy++)
                    for (int xx = 0; xx < w; xx++) { int v = buf[yy * pitch + xx] * 255 / 64; atlas[(gy + yy) * AW + gx + xx] = (unsigned char)(v > 255 ? 255 : v); }
                cx += w + 2;
                if (h > row_h) row_h = h;
            }
            auto clamp8 = [](int v) { return (signed char)(v < -128 ? -128 : v > 127 ? 127 : v); };
            unsigned short ux = (unsigned short)gx, uy = (unsigned short)gy;
            memcpy(blob + bn, &ux, 2); memcpy(blob + bn + 2, &uy, 2);
            blob[bn + 4] = (unsigned char)w; blob[bn + 5] = (unsigned char)h;
            blob[bn + 6] = (unsigned char)clamp8(gm.gmptGlyphOrigin.x); blob[bn + 7] = (unsigned char)clamp8(tm.tmAscent - gm.gmptGlyphOrigin.y);
            blob[bn + 8] = (unsigned char)clamp8(gm.gmCellIncX); blob[bn + 9] = 0;
            bn += 10;
        }
        SelectObject(hdc, old);
        DeleteObject(hf);
        cx = 0; cy += row_h + 4; row_h = 0;
    }
    DeleteDC(hdc);
    if (cy + 4 > AH) { say(L"UI font: atlas too small"); return false; }
    FILE *f = NULL;
    if (_wfopen_s(&f, out, L"wb") != 0 || !f) return false;
    unsigned short hd[4] = {1, (unsigned short)AW, (unsigned short)AH, 5};
    fwrite("CWUF", 1, 4, f); fwrite(hd, 2, 4, f); fwrite(blob, 1, bn, f); fwrite(atlas, 1, sizeof(atlas), f);
    fclose(f);
    say(L"UI font: CwoUiFont.bin made from this PC's Segoe UI fonts");
    return true;
}

int wmain(int argc, wchar_t **argv) {
    GetModuleFileNameW(NULL, g_log, MAX_PATH);
    wchar_t *sl = wcsrchr(g_log, L'\\');
    if (sl) wcscpy_s(sl + 1, MAX_PATH - (sl + 1 - g_log), L"CwoLaunch.log");
    if (argc >= 2 && !_wcsicmp(argv[1], L"--attach")) {
        DWORD pid = 0, tid = 0;
        for (int i = 2; i + 1 < argc; i++) {
            if (!_wcsicmp(argv[i], L"-p")) pid = wcstoul(argv[i + 1], NULL, 10);
            if (!_wcsicmp(argv[i], L"-tid")) tid = wcstoul(argv[i + 1], NULL, 10);
        }
        return attach(pid, tid);
    }
    if (argc >= 2 && !_wcsicmp(argv[1], L"--off")) {   // emergency: turn the paused start off
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        IPackageDebugSettings *dbg = NULL;
        UINT32 count = 0, buflen = 0; PWSTR names[4] = {}; wchar_t buf[1024];
        GetPackagesByPackageFamily(FAMILY, &count, NULL, &buflen, NULL);
        if (count && count <= 4 && buflen <= 1024 && GetPackagesByPackageFamily(FAMILY, &count, names, &buflen, buf) == ERROR_SUCCESS &&
            SUCCEEDED(CoCreateInstance(CLSID_PackageDebugSettings, NULL, CLSCTX_ALL, IID_PPV_ARGS(&dbg)))) {
            dbg->DisableDebugging(names[0]); dbg->Release(); say(L"paused start turned off");
        }
        CoUninitialize();
        return 0;
    }
    if (argc >= 3 && !_wcsicmp(argv[1], L"--bake-font")) return bake_ui_font(argv[2]) ? 0 : 1;   // v5.150.0 (tests, installers)
    if (argc >= 3 && !_wcsicmp(argv[1], L"--save-backup-test")) { save_backup_request(argv[2]); return 0; }   // tests: <data\>, no game
    if (argc >= 4 && !_wcsicmp(argv[1], L"--resync-check")) return bfr_resync(argv[2], argv[3]) == 1 ? 0 : 1;   // tests: <pack\> <data\>, no game
    if (argc >= 2 && !_wcsicmp(argv[1], L"--resync-selftest")) { bool ok = rs_selftest(); say(L"self-test: resync rules %s", ok ? L"ok" : L"FAILED"); return ok ? 0 : 1; }
    const wchar_t *edition = L"";
    bool no_update = false, update_only = false;
    for (int i = 1; i < argc; i++) {
        if (!_wcsicmp(argv[i], L"--no-update")) no_update = true;           // v5.153
        else if (!_wcsicmp(argv[i], L"--update-only")) update_only = true; // v5.153 check / install an update, no game
        else if (!_wcsicmp(argv[i], L"--dll") && i + 1 < argc) g_dll_override = argv[++i];
        else if (!_wcsicmp(argv[i], L"--wait")) g_wait_mode = true;
        else if (!_wcsicmp(argv[i], L"--edition") && i + 1 < argc) edition = argv[++i];   // v5.57.1 BFR = the BFR mod pack's data folder
    }
    {   // v5.154 FEEDBACK out (both editions) and the pack version the FEEDBACK page sends with an entry
        wchar_t la2[MAX_PATH], df2[MAX_PATH], exe2[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", la2, MAX_PATH)) {
            bool bfr = !_wcsicmp(edition, L"BFR");
            swprintf_s(df2, MAX_PATH, L"%s\\Packages\\%s\\AC\\Temp\\CwoLoader%s\\", la2, FAMILY, bfr ? L"BFR" : L"");
            CreateDirectoryW(df2, NULL);
            if (bfr) {                                            // <pack>\VERSION (this exe is <pack>\launcher\CwoLaunch.exe)
                GetModuleFileNameW(NULL, exe2, MAX_PATH);
                wchar_t *a = wcsrchr(exe2, L'\\'); if (a) *a = 0;
                wchar_t *b = wcsrchr(exe2, L'\\'); if (b) b[1] = 0;
                std::vector<unsigned char> v;
                if (a && b && up_read(std::wstring(exe2) + L"VERSION", v)) up_write(std::wstring(df2) + L"pack_version.txt", v);
            }
            fb_send(df2);
            save_backup_request(df2);                               // v5.188.10 MOD LIST > X: the player's save, before the game
        }
    }
    if (!_wcsicmp(edition, L"BFR") && !no_update && !(argc >= 2 && !_wcsicmp(argv[1], L"--paused"))) {
        // v5.153 the BFR pack updater: the pack folder = this exe's folder's parent (<pack>\launcher\CwoLaunch.exe)
        wchar_t exe[MAX_PATH], la[MAX_PATH], dfold[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wchar_t *s1 = wcsrchr(exe, L'\\'); if (s1) *s1 = 0;
        wchar_t *s2 = wcsrchr(exe, L'\\'); if (s2) s2[1] = 0;
        if (s1 && s2 && GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) {
            swprintf_s(dfold, MAX_PATH, L"%s\\Packages\\%s\\AC\\Temp\\CwoLoaderBFR\\", la, FAMILY);
            CreateDirectoryW(dfold, NULL);
            bool relaunch = false;
            int r = bfr_update(exe, dfold, &relaunch);
            if (r == 1 && relaunch && !update_only) {                 // the launcher itself was updated: start the new one
                wchar_t me[MAX_PATH], cmd[2048];
                GetModuleFileNameW(NULL, me, MAX_PATH);
                swprintf_s(cmd, 2048, L"\"%s\" --edition BFR --no-update%s", me, g_wait_mode ? L" --wait" : L"");
                STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {};
                if (CreateProcessW(me, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); say(L"updater: the new launcher takes over"); return 0; }
            }
        }
        if (update_only) return 0;
    }
    if (!_wcsicmp(edition, L"BFR") && !update_only && !(argc >= 2 && !_wcsicmp(argv[1], L"--paused"))) {
        // v5.188.8 the fail-safe (owner 2026-11-16): the pack's files and the data folder the loader reads must be the pack's
        wchar_t exe[MAX_PATH], la[MAX_PATH], dfold[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wchar_t *s1 = wcsrchr(exe, L'\\'); if (s1) *s1 = 0;
        wchar_t *s2 = wcsrchr(exe, L'\\'); if (s2) s2[1] = 0;
        if (s1 && s2 && GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) {
            swprintf_s(dfold, MAX_PATH, L"%s\\Packages\\%s\\AC\\Temp\\CwoLoaderBFR\\", la, FAMILY);
            CreateDirectoryW(dfold, NULL);
            int r = bfr_resync(exe, dfold);
            if (r == 0) { hold_window(); return 1; }                // changed pack files that could not be put back: no game
            if (r == 2) {                                           // the launcher itself was put back: the right one takes over
                wchar_t me[MAX_PATH], cmd[2048];
                GetModuleFileNameW(NULL, me, MAX_PATH);
                swprintf_s(cmd, 2048, L"\"%s\" --edition BFR --no-update%s", me, g_wait_mode ? L" --wait" : L"");
                STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {};
                if (CreateProcessW(me, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); say(L"resync: the pack's launcher takes over"); return 0; }
                return 1;
            }
        }
    }
    {                                                            // the loader reads it at start (CwoLoaderBFR\ or CwoLoader\)
        wchar_t dd[MAX_PATH], ep[MAX_PATH];
        if (data_dir(dd)) {
            CreateDirectoryW(dd, NULL);
            swprintf_s(ep, MAX_PATH, L"%sedition.txt", dd);
            FILE *f = NULL;
            if (_wfopen_s(&f, ep, L"w") == 0 && f) { fwprintf(f, L"%s\n", edition); fclose(f); }
            if (*edition) say(L"edition: %s", edition);
        }
        wchar_t la[MAX_PATH], fp[MAX_PATH];                     // v5.150.0 the CREATE editor's font, made on this PC if missing
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) {
            swprintf_s(fp, MAX_PATH, L"%s\\Packages\\%s\\AC\\Temp\\CwoLoader%s\\CwoUiFont.bin", la, FAMILY, _wcsicmp(edition, L"BFR") ? L"" : L"BFR");
            if (GetFileAttributesW(fp) == INVALID_FILE_ATTRIBUTES) bake_ui_font(fp);
        }
    }
    int rc = (argc >= 2 && !_wcsicmp(argv[1], L"--paused")) ? launch() : launch_late();
    if (rc != 0) hold_window();                                  // v5.188.14 the reason stays readable (a player's window closed at once)
    return rc;
}
