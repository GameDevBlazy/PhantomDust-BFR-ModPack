// Offline test of the 15 outfits (no game runs): CwoLoader.dll builds one outfit from a folder of clean game files.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef int (*PFN_T)(const wchar_t *, const wchar_t *, const char *, int, const wchar_t *);
int wmain(int argc, wchar_t **argv) {
    if (argc < 7) { wprintf(L"usage: of_test CwoLoader.dll install\ data\ stem k out.alr\n"); return 2; }
    HMODULE dll = LoadLibraryW(argv[1]);
    bool design = argc > 7 && !wcscmp(argv[7], L"--design");   // v5.66: k = the design number
    PFN_T t = dll ? (PFN_T)GetProcAddress(dll, design ? "CwoDesignTest" : "CwoOutfit15Test") : NULL;
    if (!t) { wprintf(L"load failed\n"); return 2; }
    char stem[16]; size_t cv; wcstombs_s(&cv, stem, argv[4], _TRUNCATE);
    int r = t(argv[2], argv[3], stem, _wtoi(argv[5]), argv[6]);
    wprintf(L"CwoOutfit15Test = %d (1 = built)\n", r);
    return r == 1 ? 0 : 1;
}
