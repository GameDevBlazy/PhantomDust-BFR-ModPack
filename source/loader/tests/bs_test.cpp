// Offline test of the body shape (no game runs): CwoLoader.dll builds the map of a CwoBodyShape.txt line from a folder of
// clean game files and puts it on one file (or the way back with --inverse).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef int (*PFN_T)(const wchar_t *, const char *, const wchar_t *, const wchar_t *, int);
int wmain(int argc, wchar_t **argv) {
    if (argc < 6) { wprintf(L"usage: bs_test CwoLoader.dll install\ \"stem band radius amount [cx]\" in.alr out.alr [--inverse]\n"); return 2; }
    HMODULE dll = LoadLibraryW(argv[1]);
    PFN_T t = dll ? (PFN_T)GetProcAddress(dll, "CwoBodyShapeTest") : NULL;
    if (!t) { wprintf(L"load failed\n"); return 2; }
    char line[128]; size_t cv; wcstombs_s(&cv, line, argv[3], _TRUNCATE);
    int r = t(argv[2], line, argv[4], argv[5], argc > 6 && !wcscmp(argv[6], L"--inverse"));
    wprintf(L"CwoBodyShapeTest = %d (points moved; < 0 = refused)\n", r);
    return r >= 0 ? 0 : 1;
}
