// Offline test of Create an Outfit (no game runs): CwoLoader.dll builds one outfit from a folder of clean game files.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef int (*PFN_T)(const wchar_t *, const wchar_t *, int, int, int, int, int, int, const wchar_t *);
int wmain(int argc, wchar_t **argv) {
    if (argc < 11) { wprintf(L"usage: co_test CwoLoader.dll install\ data\ ch outfit skin headx top bottom out.alr\n"); return 2; }
    HMODULE dll = LoadLibraryW(argv[1]);
    PFN_T t = dll ? (PFN_T)GetProcAddress(dll, "CwoOutfitTest") : NULL;
    if (!t) { wprintf(L"load failed\n"); return 2; }
    int r = t(argv[2], argv[3], _wtoi(argv[4]), _wtoi(argv[5]), _wtoi(argv[6]), _wtoi(argv[7]), _wtoi(argv[8]), _wtoi(argv[9]), argv[10]);
    wprintf(L"CwoOutfitTest = %d (1 = built)\n", r);
    return r == 1 ? 0 : 1;
}
