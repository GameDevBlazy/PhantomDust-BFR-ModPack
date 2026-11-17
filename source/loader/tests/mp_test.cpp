// Offline test of the Store-mode memory patch (no game runs): map the clean and the modded PDUWP.exe as images
// (LoadLibraryEx DONT_RESOLVE_DLL_REFERENCES: Windows maps and relocates, runs nothing), let CwoLoader.dll apply its
// patch table to the clean one, then compare both images section by section. Differences allowed: 8-byte pointers
// that differ by exactly the two images' base difference (Windows' relocation), the CwoOverlayFrame slot and the
// call that uses it. Everything else must be identical.
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef int (*PFN_T)(HMODULE);
int wmain(int argc, wchar_t **argv) {
    if (argc < 4) { wprintf(L"usage: mp_test CwoLoader.dll clean.exe modded.exe\n"); return 2; }
    HMODULE dll = LoadLibraryW(argv[1]);
    PFN_T t = dll ? (PFN_T)GetProcAddress(dll, "CwoMemPatchTest") : NULL;
    HMODULE a = LoadLibraryExW(argv[2], NULL, DONT_RESOLVE_DLL_REFERENCES);
    HMODULE b = LoadLibraryExW(argv[3], NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!t || !a || !b) { wprintf(L"load failed %p %p %p\n", t, a, b); return 2; }
    BYTE *pa = (BYTE *)a, *pb = (BYTE *)b;
    // second run must refuse nothing silently: first run applies
    int r = t(a);
    wprintf(L"CwoMemPatchTest(clean) = %d   (1 = applied)\n", r);
    int r2 = t(b);
    wprintf(L"CwoMemPatchTest(modded) = %d  (2 = not needed: .cwo section present)\n", r2);
    LONGLONG delta = (LONGLONG)(pa - pb);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(pa + ((IMAGE_DOS_HEADER *)pa)->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    long bad = 0, ptrs = 0, special = 0;
    // the /GS security cookie (load config +0x58) is randomised by Windows in every mapped image: not compared
    IMAGE_DATA_DIRECTORY lcd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    DWORD cookie = lcd.VirtualAddress ? (DWORD)(*(ULONGLONG *)(pa + lcd.VirtualAddress + 0x58) - (ULONGLONG)pa) : 0;
    ULONGLONG fn = (ULONGLONG)GetProcAddress(dll, "CwoOverlayFrame");
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!strncmp((char *)s[i].Name, ".reloc", 8)) continue;
        DWORD va = s[i].VirtualAddress, n = (s[i].Misc.VirtualSize + 0xFFF) & ~0xFFFu;
        for (DWORD k = 0; k < n; k++) {
            if (pa[va + k] == pb[va + k]) continue;
            if (cookie && va + k >= cookie && va + k < cookie + 8) continue;
            // an 8-byte pointer starting at or before k that differs by delta
            bool ok = false;
            for (int back = 0; back < 8 && !ok && k >= (DWORD)back; back++) {
                ULONGLONG x = *(ULONGLONG *)(pa + va + k - back), y = *(ULONGLONG *)(pb + va + k - back);
                if ((LONGLONG)(x - y) == delta && x != y) { ok = true; ptrs++; k = k - back + 7; }
                else if (x == fn && y == 0) { ok = true; special++; k = k - back + 7; }
            }
            if (!ok && special < 8) {                                // the disp32 of the call through the slot
                ok = true; special++; k += 3;
                wprintf(L"  slot call disp at rva %lx\n", va + k - 3);
            }
            if (!ok) { if (bad < 20) wprintf(L"  DIFF %.8S rva %lx: %02x vs %02x\n", s[i].Name, va + k, pa[va + k], pb[va + k]); bad++; }
        }
    }
    wprintf(L"relocated pointers %ld, slot/call differences %ld, other differences %ld\n", ptrs, special, bad);
    return (r == 1 && r2 == 2 && bad == 0 && special <= 2) ? 0 : 1;
}
