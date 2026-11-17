// Offline oracle for cwo_theme.inl: each case = a base file, a theme 1-10, the file outfit_generator.apply_theme wrote.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/cwo_charbuild.inl"
#include "../src/cwo_theme.inl"
int wmain(int argc, wchar_t **argv) {
    if (argc < 2) return 2;
    wchar_t cases[MAX_PATH];
    swprintf_s(cases, L"%scases.txt", argv[1]);
    FILE *f = NULL;
    if (_wfopen_s(&f, cases, L"r") || !f) return 2;
    char base[128], exp[128];
    int theme, n = 0, bad = 0;
    while (fscanf_s(f, "%127s %d %127s", base, (unsigned)sizeof(base), &theme, exp, (unsigned)sizeof(exp)) == 3) {
        wchar_t pb[MAX_PATH], pe[MAX_PATH];
        swprintf_s(pb, L"%s%S", argv[1], base);
        swprintf_s(pe, L"%s%S", argv[1], exp);
        Bytes b, e;
        if (!read_all(pb, b) || !read_all(pe, e)) { printf("missing %s\n", exp); bad++; n++; continue; }
        DWORD t0 = GetTickCount();
        bool ok = th_apply(b, theme);
        DWORD ms = GetTickCount() - t0;
        bool same = ok && b == e;
        size_t i = 0;
        if (!same) while (i < b.size() && i < e.size() && b[i] == e[i]) i++;
        printf("%-26s %s (%lu ms)%s\n", exp, same ? "IDENTICAL" : "DIFFERENT", ms, same ? "" : "");
        if (!same) printf("   ok=%d first difference at 0x%zx\n", ok, i);
        bad += !same; n++;
    }
    fclose(f);
    printf("%d of %d identical\n", n - bad, n);
    return bad ? 1 : 0;
}
