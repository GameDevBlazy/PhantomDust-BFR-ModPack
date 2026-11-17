// Offline test host for CwoLoader.dll (2026-10-23): loads the DLL into an ordinary process (not the game).
// Expected: the DLL logs that it loaded, its host check FAILS (this is not PDUWP.exe), it stays idle, nothing crashes,
// CwoLoaderVersion() returns 1 and CwoSelfTest() (marker add/delete file code) returns 1.
#include <windows.h>
#include <stdio.h>

int main() {
    HMODULE h = LoadLibraryW(L"CwoLoader.dll");
    if (!h) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    typedef int(*PFN)();
    PFN v = (PFN)GetProcAddress(h, "CwoLoaderVersion");
    PFN st = (PFN)GetProcAddress(h, "CwoSelfTest");
    printf("CwoLoaderVersion = %d\n", v ? v() : -1);
    printf("CwoSelfTest = %d (1 = pass)\n", st ? st() : -9);
    // v0.8: the file monitor's import-slot switch, done on THIS program's own GetFileAttributesW import
    typedef int (*PFN_SEEN)(const wchar_t *);
    PFN hook = (PFN)GetProcAddress(h, "CwoTestHookImports");
    PFN_SEEN seen = (PFN_SEEN)GetProcAddress(h, "CwoTestSeen");
    if (hook && seen) {
        int hooked = hook();
        DWORD a = GetFileAttributesW(L"C:\\Windows");                 // through the switched import slot
        DWORD b = GetFileAttributesW(L"C:\\no_such_dir_cwo\\x.dat");
        DWORD eb = GetLastError();
        int sa = seen(L"C:\\Windows"), sb = seen(L"C:\\no_such_dir_cwo\\x.dat");
        bool pass = hooked == 1 && a != INVALID_FILE_ATTRIBUTES && b == INVALID_FILE_ATTRIBUTES && eb == ERROR_PATH_NOT_FOUND &&
                    sa == 0 && sb == ERROR_PATH_NOT_FOUND;
        printf("ImportSwitchTest = %d (hooked %d, found-entry %d, missing-entry %d, last error %lu)\n", pass ? 1 : 0, hooked, sa, sb, eb);
    }
    Sleep(500);                       // let the worker thread write its log and exit
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wprintf(L"log folder: %sCwoLoader\\\n", tmp);
    return 0;
}
