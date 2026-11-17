// Offline check of the six-fighter table move on the real exe: PDUWP.exe (clean, 9ba7411b) is laid out as Windows maps it,
// sixf_apply runs on it, and the patched .text is written out for tests/check_sixf.py (capstone) to read.
// Usage: sixf_test PDUWP.exe OUT_text.bin   (prints the block address used, relative to the image)
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <vector>
static ULONG_PTR g_base;
#include "../src/cwo_sixf_apply.inl"
int wmain(int argc, wchar_t **argv) {
    if (argc < 3) return 2;
    FILE *f = NULL;
    if (_wfopen_s(&f, argv[1], L"rb") || !f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> file(n);
    fread(file.data(), 1, n, f); fclose(f);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(file.data() + ((IMAGE_DOS_HEADER *)file.data())->e_lfanew);
    DWORD img = nt->OptionalHeader.SizeOfImage;
    unsigned char *base = (unsigned char *)VirtualAlloc(NULL, img + 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (sec[i].SizeOfRawData) memcpy(base + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData, sec[i].SizeOfRawData);
    ULONG_PTR block = ((ULONG_PTR)base + img + 0xFFFF) & ~(ULONG_PTR)0xFFFF;
    char why[96] = "";
    bool ok = sixf_apply((ULONG_PTR)base, block, why, sizeof(why));
    printf("%s %s block +0x%llx\n", ok ? "MOVED" : "REFUSED", why, (unsigned long long)(block - (ULONG_PTR)base));
    if (!ok) return 1;
    FILE *o = NULL;
    if (_wfopen_s(&o, argv[2], L"wb") || !o) return 2;
    fwrite(base + 0x1000, 1, 0x2d1600, o); fclose(o);
    return 0;
}
