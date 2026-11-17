// Offline check of six fighters steps 1 + 2 on the real exe: PDUWP.exe (clean, 9ba7411b) is laid out as Windows maps it,
// sixf_apply (step 1) and sixf2_apply (step 2) run on it, and the patched .text, the stores' block and the stubs are
// written out for tests/check_sixf2.py (capstone) to read.
// Usage: sixf2_test PDUWP.exe OUTDIR   (writes text.bin, block.bin, stubs.bin, layout.txt)
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <vector>
static ULONG_PTR g_base;
#include "../src/cwo_sixf_apply.inl"
#include "../src/cwo_sixf2_apply.inl"
static void test_fn() {}
static ULONG_PTR test_resolve(const char *) { return (ULONG_PTR)&test_fn; }
static bool put(const wchar_t *dir, const wchar_t *name, const void *p, size_t n) {
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\%s", dir, name);
    FILE *o = NULL;
    if (_wfopen_s(&o, path, L"wb") || !o) return false;
    fwrite(p, 1, n, o);
    fclose(o);
    return true;
}
int wmain(int argc, wchar_t **argv) {
    if (argc < 3) return 2;
    FILE *f = NULL;
    if (_wfopen_s(&f, argv[1], L"rb") || !f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> file(n);
    fread(file.data(), 1, n, f); fclose(f);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(file.data() + ((IMAGE_DOS_HEADER *)file.data())->e_lfanew);
    DWORD img = nt->OptionalHeader.SizeOfImage;
    unsigned char *base = (unsigned char *)VirtualAlloc(NULL, img + 0x50000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (sec[i].SizeOfRawData) memcpy(base + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData, sec[i].SizeOfRawData);
    ULONG_PTR block1 = ((ULONG_PTR)base + img + 0xFFFF) & ~(ULONG_PTR)0xFFFF;
    ULONG_PTR block2 = block1 + 0x10000, stubs = (ULONG_PTR)base + 0x2d2800;   // the .text padding, as the loader does
    for (int i = 0; i < 0x800; i++) if (base[0x2d2800 + i]) { printf("the .text padding is not empty%c", 10); return 1; }
    g_sixf2_room = 0x800;
    size_t need = 0;                                           // v5.7: more detours than the padding holds -> a page after
    for (size_t i = 0; i < SIXF2_NSTUBS; i++) need += (SIXF2_STUBS[i].code_len + 15) & ~15u;
    if (need > 0x800) { stubs = block2 + 0x10000; g_sixf2_room = 0x1000; }
    printf("detours: %zu bytes at +0x%llx%c", need, (unsigned long long)(stubs - (ULONG_PTR)base), 10);
    char why[96] = "";
    bool ok1 = sixf_apply((ULONG_PTR)base, block1, why, sizeof(why));
    printf("step 1: %s %s\n", ok1 ? "MOVED" : "REFUSED", why);
    if (!ok1) return 1;
    bool ok2 = sixf2_apply((ULONG_PTR)base, block2, stubs, test_resolve, why, sizeof(why));
    printf("step 2: %s %s (%zu stores, %zu sites, %zu patches, %zu stubs)\n", ok2 ? "APPLIED" : "REFUSED", why, SIXF2_NSTORES,
           SIXF2_NSITES, SIXF2_NPATCHES, SIXF2_NSTUBS);
    if (!ok2) return 1;
    // refused on a changed byte, nothing written
    unsigned char keep[16];
    memcpy(keep, base + SIXF2_SITES[0].rva, 16);
    bool refused = !sixf2_apply((ULONG_PTR)base, block2, stubs, test_resolve, why, sizeof(why)) && !memcmp(keep, base + SIXF2_SITES[0].rva, 16);
    printf("second run refused: %s (%s)\n", refused ? "ok" : "FAILED", why);
    char lay[256];
    sprintf_s(lay, "block1 0x%llx\nblock2 0x%llx\nstubs 0x%llx\n", (unsigned long long)(block1 - (ULONG_PTR)base),
              (unsigned long long)(block2 - (ULONG_PTR)base), (unsigned long long)(stubs - (ULONG_PTR)base));
    put(argv[2], L"layout.txt", lay, strlen(lay));
    put(argv[2], L"text.bin", base + 0x1000, 0x2d1600);
    put(argv[2], L"block.bin", (void *)block2, SIXF2_BLOCK);
    put(argv[2], L"stubs.bin", (void *)stubs, g_sixf2_room);
    return refused ? 0 : 1;
}
