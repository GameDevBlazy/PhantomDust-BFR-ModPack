// Offline test of the BFR pack's game-file patches (no game runs): CwoLoader.dll's own patch code applies each real
// .cwopatch to the clean game file and writes the result; the caller compares the results with the expected files.
#include <windows.h>
#include <stdio.h>
typedef int (*PFN_T)(const wchar_t *, const wchar_t *, const wchar_t *);
int wmain(int argc, wchar_t **argv) {
    if (argc == 6 && !wcscmp(argv[2], L"--dir")) {             // v5.64.3: pf_test DLL --dir PATCHES\ INSTALL\ OUT\  (the real walk)
        HMODULE d = LoadLibraryW(argv[1]);
        PFN_T w = d ? (PFN_T)GetProcAddress(d, "CwoPatchDirTest") : NULL;
        if (!w) { wprintf(L"load failed\n"); return 2; }
        int r = w(argv[3], argv[4], argv[5]);
        wprintf(L"built %d, refused %d\n", r / 1000, r % 1000);
        return r % 1000 ? 1 : 0;
    }
    if (argc < 5 || (argc - 2) % 3) { wprintf(L"usage: pf_test CwoLoader.dll (patch stock out)... | --dir PATCHES INSTALL OUT\n"); return 2; }
    HMODULE dll = LoadLibraryW(argv[1]);
    PFN_T t = dll ? (PFN_T)GetProcAddress(dll, "CwoPatchFileTest") : NULL;
    if (!t) { wprintf(L"load failed\n"); return 2; }
    int bad = 0;
    for (int i = 2; i + 2 < argc; i += 3) {
        int r = t(argv[i], argv[i + 1], argv[i + 2]);
        wprintf(L"%d  %s\n", r, argv[i]);
        if (r != 1) bad++;
    }
    return bad ? 1 : 0;
}
