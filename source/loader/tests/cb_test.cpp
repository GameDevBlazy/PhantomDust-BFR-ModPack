// Offline oracle for cwo_charbuild.inl: builds each spec of specs.txt with the C++ port and compares the bytes with the
// file 13_Independent_Research_Tools/build_character.py wrote for the same spec.
// specs.txt lines: name body colour moves size_fit then 130 "from" values (-1 = base moves)
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/cwo_charbuild.inl"
int wmain(int argc, wchar_t **argv) {
    if (argc < 3) { printf("usage: cb_test SRC_DIR\\ SPECS.txt\n"); return 2; }
    g_cb_src = argv[1];
    FILE *f = NULL;
    if (_wfopen_s(&f, argv[2], L"r") || !f) return 2;
    char name[64];
    int fails = 0, n = 0;
    CbSpec sp;
    while (fscanf_s(f, "%63s", name, (unsigned)sizeof(name)) == 1) {
        if (fscanf_s(f, "%d %d %d %d", &sp.body, &sp.colour, &sp.moves, &sp.size_fit) != 4) break;
        for (int s = 0; s < 130; s++) fscanf_s(f, "%d", &sp.from[s]);
        Bytes out, want;
        char why[128] = "";
        DWORD t0 = GetTickCount();
        bool ok = cb_build(sp, out, why, sizeof(why));
        DWORD ms = GetTickCount() - t0;
        wchar_t wp[MAX_PATH];
        swprintf_s(wp, L"%s..\\expect_%S.alr", argv[1], name);
        bool have = read_all(wp, want);
        bool same = ok && have && out == want;
        printf("%-14s %s  (%zu bytes, expected %zu, %lu ms) %s\n", name, same ? "IDENTICAL" : "DIFFERENT", out.size(), want.size(), ms, why);
        if (!same && ok && have) {
            size_t i = 0;
            while (i < out.size() && i < want.size() && out[i] == want[i]) i++;
            printf("   first difference at 0x%zx\n", i);
        }
        fails += !same;
        n++;
    }
    fclose(f);
    printf("%d of %d identical\n", n - fails, n);
    return fails ? 1 : 0;
}
