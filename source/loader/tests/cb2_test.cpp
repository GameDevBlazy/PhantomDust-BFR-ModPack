// Offline oracle for cwo_charbuild2.inl: each spec of cb2\specs2.txt built by the C++ port, compared byte for byte with
// what 13_Independent_Research_Tools/build_character.py wrote (make_cb2_expect.py); a spec the Python builder refused
// (expect_<name>.fail) must be refused here too.
// specs2.txt lines: name body colour moves size_fit head mid mstyle low lstyle headx
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/cwo_charbuild.inl"
#include "../src/cwo_charbuild2.inl"
int wmain(int argc, wchar_t **argv) {
    if (argc < 4) { printf("usage: cb2_test SRC_DIR\\ EXPECT_DIR\\ SPECS.txt\n"); return 2; }
    g_cb_src = argv[1];
    FILE *f = NULL;
    if (_wfopen_s(&f, argv[3], L"r") || !f) return 2;
    char name[64];
    int fails = 0, n = 0;
    while (fscanf_s(f, "%63s", name, (unsigned)sizeof(name)) == 1) {
        CbSpec2 s2;
        CbSpec &sp = s2.base;
        if (fscanf_s(f, "%d %d %d %d %d %d %d %d %d %d", &sp.body, &sp.colour, &sp.moves, &sp.size_fit, &s2.head, &s2.mid, &s2.mstyle,
                     &s2.low, &s2.lstyle, &s2.headx) != 10) break;
        for (int s = 0; s < 130; s++) sp.from[s] = -1;
        Bytes out, want;
        char why[160] = "";
        DWORD t0 = GetTickCount();
        bool ok = cb_build2(s2, out, why, sizeof(why));
        DWORD ms = GetTickCount() - t0;
        wchar_t wp[MAX_PATH], wf[MAX_PATH];
        swprintf_s(wp, L"%sexpect_%S.alr", argv[2], name);
        swprintf_s(wf, L"%sexpect_%S.fail", argv[2], name);
        bool refuse = GetFileAttributesW(wf) != INVALID_FILE_ATTRIBUTES;
        bool have = !refuse && read_all(wp, want);
        bool same = refuse ? !ok : (ok && have && out == want);
        printf("%-24s %s  (%zu bytes, expected %zu, %lu ms) %s\n", name, same ? (refuse ? "REFUSED (as Python)" : "IDENTICAL") : "DIFFERENT",
               out.size(), want.size(), ms, why);
        if (!same && ok && have) {
            size_t i = 0;
            while (i < out.size() && i < want.size() && out[i] == want[i]) i++;
            printf("   first difference at 0x%zx\n", i);
            wchar_t wo[MAX_PATH];
            swprintf_s(wo, L"%sgot_%S.alr", argv[2], name);
            FILE *g = NULL;
            if (_wfopen_s(&g, wo, L"wb") == 0 && g) { fwrite(out.data(), 1, out.size(), g); fclose(g); }
        }
        fails += !same;
        n++;
    }
    fclose(f);
    printf("%d of %d as Python\n", n - fails, n);
    return fails ? 1 : 0;
}
