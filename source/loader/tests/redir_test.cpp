// Offline test of the Store-mode file override lookup (no game): stand-in folders under %TEMP%\cwo_redir_test.
#include <windows.h>
#include <stdio.h>
typedef int (*PFN_T)(const wchar_t *, const wchar_t *);
int wmain(int argc, wchar_t **argv) {
    wchar_t t[MAX_PATH], inst[MAX_PATH], over[MAX_PATH], f[MAX_PATH];
    GetTempPathW(MAX_PATH, t);
    swprintf_s(inst, MAX_PATH, L"%scwo_redir_test\\install\\", t);
    swprintf_s(over, MAX_PATH, L"%scwo_redir_test\\over\\", t);
    const wchar_t *dirs[] = {L"cwo_redir_test", L"cwo_redir_test\\install", L"cwo_redir_test\\over", L"cwo_redir_test\\over\\Assets",
                             L"cwo_redir_test\\over\\Assets\\Data", L"cwo_redir_test\\over\\Assets\\Data\\menu"};
    for (auto d : dirs) { swprintf_s(f, MAX_PATH, L"%s%s", t, d); CreateDirectoryW(f, NULL); }
    swprintf_s(f, MAX_PATH, L"%sAssets\\Data\\menu\\x.ssb", over);
    HANDLE h = CreateFileW(f, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); CloseHandle(h);
    HMODULE dll = LoadLibraryW(argc > 1 ? argv[1] : L"CwoLoader.dll");
    PFN_T fn = dll ? (PFN_T)GetProcAddress(dll, "CwoRedirectTest") : NULL;
    if (!fn) { wprintf(L"load failed\n"); return 2; }
    int r = fn(inst, over);
    wprintf(L"CwoRedirectTest = %d (0 = all 8 checks pass)\\n", r);
    return r ? 1 : 0;
}
