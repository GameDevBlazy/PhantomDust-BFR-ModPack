// Read-only look at a running PDUWP.exe (owner-permitted runtime memory READ, offline): for every thread, its
// instruction pointer and the return addresses found on its stack, as module+offset. No writes, no injection.
// Usage: stack_peek <pid> [max_threads]
#include <windows.h>
#include <tlhelp32.h>
#define PSAPI_VERSION 2
#include <psapi.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <algorithm>
struct Mod { ULONG_PTR lo, hi; std::wstring name; };
static std::vector<Mod> mods;
static const Mod *find(ULONG_PTR a) { for (auto &m : mods) if (a >= m.lo && a < m.hi) return &m; return NULL; }
static void name(ULONG_PTR a, wchar_t *out) {
    const Mod *m = find(a);
    if (m) swprintf_s(out, 160, L"%s+%llx", m->name.c_str(), (unsigned long long)(a - m->lo));
    else swprintf_s(out, 160, L"%llx", (unsigned long long)a);
}
int wmain(int argc, wchar_t **argv) {
    DWORD pid = argc > 1 ? wcstoul(argv[1], NULL, 10) : 0;
    int maxt = argc > 2 ? _wtoi(argv[2]) : 100;
    HANDLE p = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!p) { wprintf(L"cannot open pid %lu (%lu)\n", pid, GetLastError()); return 1; }
    HMODULE hm[1024]; DWORD got = 0;
    EnumProcessModulesEx(p, hm, sizeof(hm), &got, LIST_MODULES_64BIT);
    for (DWORD i = 0; i < got / sizeof(HMODULE); i++) {
        MODULEINFO mi; wchar_t n[MAX_PATH];
        if (GetModuleInformation(p, hm[i], &mi, sizeof(mi)) && GetModuleBaseNameW(p, hm[i], n, MAX_PATH))
            mods.push_back({(ULONG_PTR)mi.lpBaseOfDll, (ULONG_PTR)mi.lpBaseOfDll + mi.SizeOfImage, n});
    }
    wprintf(L"%zu modules\n", mods.size());
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = {sizeof(te)};
    std::vector<DWORD> tids;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) if (te.th32OwnerProcessID == pid) tids.push_back(te.th32ThreadID);
    CloseHandle(snap);
    std::sort(tids.begin(), tids.end());
    int shown = 0;
    for (DWORD tid : tids) {
        if (shown++ >= maxt) break;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME, FALSE, tid);
        if (!t) { wprintf(L"tid %lu: cannot open\n", tid); continue; }
        DWORD sc = SuspendThread(t);                      // already suspended threads keep their count +1 for the read
        CONTEXT c = {}; c.ContextFlags = CONTEXT_CONTROL;
        bool okc = GetThreadContext(t, &c) != 0;
        wchar_t rip[160], buf[160];
        if (okc) name(c.Rip, rip); else wcscpy_s(rip, L"?");
        wprintf(L"tid %lu (suspend count was %ld) rip %s\n", tid, (long)sc, rip);
        if (okc) {
            static ULONGLONG stack[16384]; SIZE_T rd = 0;
            if (ReadProcessMemory(p, (void *)c.Rsp, stack, sizeof(stack), &rd)) {
                int n = 0;
                for (SIZE_T i = 0; i < rd / 8 && n < 40; i++) {
                    const Mod *m = find((ULONG_PTR)stack[i]);
                    if (!m) continue;
                    // only code-looking values: preceded by a call is not checked; filter ntdll/kernel wait stubs lightly
                    name((ULONG_PTR)stack[i], buf);
                    wprintf(L"      [rsp+%03zx] %s\n", i * 8, buf);
                    n++;
                }
            }
        }
        ResumeThread(t);
        CloseHandle(t);
    }
    CloseHandle(p);
    return 0;
}
