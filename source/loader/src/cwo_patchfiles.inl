// v5.58 game files built from patches on the player's own machine (the BFR mod pack; owner 2026-11-01: the pack goes on a
// private GitHub repository; AGENTS.md: "the agent prepares it without the game's own files (patches applied on each
// player's install)"). Independently authored; Windows file API (CreateFileW, ReadFile, WriteFile, MoveFileExW,
// FindFirstFileW); SHA-256 from cwo_sha256.h.
//
// The pack's data folder holds <data>\patches\<game path>.cwopatch. At the worker's start, in Store mode, the loader runs
// inside the game, which can read its own (encrypted on disk) Store files, so it builds each changed file into the
// override folder <data>\game\<game path>, where the game's file opens find it (cwo_storemode.inl).
// File format (our own): "CWOPATCH" | u32 version 1 | u32 stock size | 32 bytes stock SHA-256 | u32 result size |
// 32 bytes result SHA-256 | u32 range count | ranges: u32 offset, u32 length, then either the bytes or - when the length's
// top bit is set - a u32 offset in the player's own stock file to copy from (the game's own bytes that only moved are
// never in the pack). The result = the stock bytes, cut or zero-extended to the result size, with the ranges written over
// them. Explicit manifest: the stock file must match its
// hash and so must the result, or nothing is written. A result already in place (same hash) is kept as it is; an older
// one that no longer matches is renamed to <name>.stale (files are preserved), never deleted.
// v5.149.0 (owner 2026-11-10; AGENTS.md:
// every changed or game-built file only as a patch against the player's own files): format VERSION 2 - a new file built
// from SEVERAL of the player's game files (our stages mix pieces and textures of several stock stages and effect packs):
// "CWOPATCH" | u32 2 | u32 source count n (1..8) | n x (u32 size, 32 bytes SHA-256) | u32 result size | 32 bytes result
// SHA-256 | u32 range count | ranges: u32 offset, u32 length, then the bytes, or - top bit set - u32 source index, u32
// offset in that source. The result starts zero-filled. <file>.cwofrom lists the n sources, one game path per line, in
// order. Every source must match its hash and the result its hash, or nothing is written (as version 1).
static const int PF_MAXSRC = 8;
static int g_pf_built, g_pf_kept, g_pf_refused;

static BYTE *pf_read(const wchar_t *path, DWORD *size) {
    *size = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    BYTE *b = NULL;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart >= 0 && sz.QuadPart < 256LL * 1024 * 1024) {
        b = (BYTE *)malloc((size_t)sz.QuadPart + 1);
        DWORD got = 0;
        if (b && (!ReadFile(h, b, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart)) { free(b); b = NULL; }
        if (b) *size = got;
    }
    CloseHandle(h);
    return b;
}

static bool pf_write(const wchar_t *path, const BYTE *data, DWORD n) {
    wchar_t dir[MAX_PATH], tmp[MAX_PATH];
    wcscpy_s(dir, MAX_PATH, path);
    for (wchar_t *s = wcschr(dir + 3, L'\\'); s; s = wcschr(s + 1, L'\\')) { *s = 0; CreateDirectoryW(dir, NULL); *s = L'\\'; }
    if (swprintf_s(tmp, MAX_PATH, L"%s.tmp", path) < 0) return false;
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(h, data, n, &put, NULL) && put == n;
    CloseHandle(h);
    if (ok) ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
    if (!ok) DeleteFileW(tmp);
    return ok;
}

static unsigned pf_u32(const BYTE *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

// 1 = built into *out (malloc'd, *on bytes); -1 not a patch, -2 the stock file is not the one the patch was made for,
// -3 a range outside the result, -4 the result is not the expected one
static int pf_apply(const BYTE *pt, DWORD pn, const BYTE *stock, DWORD sn, BYTE **out, DWORD *on) {
    *out = NULL; *on = 0;
    if (pn < 8 + 4 + 4 + 32 + 4 + 32 + 4 || memcmp(pt, "CWOPATCH", 8) || pf_u32(pt + 8) != 1) return -1;
    unsigned ssz = pf_u32(pt + 12), rsz = pf_u32(pt + 48), nr = pf_u32(pt + 84);
    const BYTE *ssha = pt + 16, *rsha = pt + 52;
    unsigned char h[32];
    cwo_sha256(stock, sn, h);
    if (sn != ssz || memcmp(h, ssha, 32)) return -2;
    if (rsz > 256u * 1024 * 1024) return -3;
    BYTE *r = (BYTE *)calloc(rsz + 1, 1);
    if (!r) return -3;
    memcpy(r, stock, sn < rsz ? sn : rsz);
    DWORD at = 88;
    for (unsigned i = 0; i < nr; i++) {
        if (at + 8 > pn) { free(r); return -3; }
        unsigned off = pf_u32(pt + at), len = pf_u32(pt + at + 4);
        at += 8;
        if (len & 0x80000000u) {                                     // a copy from the stock file
            len &= 0x7fffffffu;
            if (at + 4 > pn) { free(r); return -3; }
            unsigned src = pf_u32(pt + at);
            at += 4;
            if (src > sn || len > sn - src || off > rsz || len > rsz - off) { free(r); return -3; }
            memcpy(r + off, stock + src, len);
            continue;
        }
        if (len > pn - at || off > rsz || len > rsz - off) { free(r); return -3; }
        memcpy(r + off, pt + at, len);
        at += len;
    }
    cwo_sha256(r, rsz, h);
    if (memcmp(h, rsha, 32)) { free(r); return -4; }
    *out = r; *on = rsz;
    return 1;
}

// v5.149.0 version 2: several sources (see above); same return codes as pf_apply (-2: a source is not the expected file)
static int pf_apply2(const BYTE *pt, DWORD pn, BYTE *const *src, const DWORD *sn, int nsrc, BYTE **out, DWORD *on) {
    *out = NULL; *on = 0;
    if (pn < 16 || memcmp(pt, "CWOPATCH", 8) || pf_u32(pt + 8) != 2) return -1;
    unsigned n = pf_u32(pt + 12);
    if (n < 1 || n > (unsigned)PF_MAXSRC || (int)n != nsrc) return -2;
    DWORD at = 16;
    if (pn < at + n * 36 + 4 + 32 + 4) return -1;
    unsigned char h[32];
    for (unsigned k = 0; k < n; k++, at += 36) {
        if (!src[k] || sn[k] != pf_u32(pt + at)) return -2;
        cwo_sha256(src[k], sn[k], h);
        if (memcmp(h, pt + at + 4, 32)) return -2;
    }
    unsigned rsz = pf_u32(pt + at);
    const BYTE *rsha = pt + at + 4;
    unsigned nr = pf_u32(pt + at + 36);
    at += 40;
    if (rsz > 256u * 1024 * 1024) return -3;
    BYTE *r = (BYTE *)calloc(rsz + 1, 1);
    if (!r) return -3;
    for (unsigned i = 0; i < nr; i++) {
        if (at + 8 > pn) { free(r); return -3; }
        unsigned off = pf_u32(pt + at), len = pf_u32(pt + at + 4);
        at += 8;
        if (len & 0x80000000u) {
            len &= 0x7fffffffu;
            if (at + 8 > pn) { free(r); return -3; }
            unsigned k = pf_u32(pt + at), so = pf_u32(pt + at + 4);
            at += 8;
            if (k >= n || so > sn[k] || len > sn[k] - so || off > rsz || len > rsz - off) { free(r); return -3; }
            memcpy(r + off, src[k] + so, len);
            continue;
        }
        if (len > pn - at || off > rsz || len > rsz - off) { free(r); return -3; }
        memcpy(r + off, pt + at, len);
        at += len;
    }
    cwo_sha256(r, rsz, h);
    if (memcmp(h, rsha, 32)) { free(r); return -4; }
    *out = r; *on = rsz;
    return 1;
}

// the result's size and SHA-256 named in a patch (version 1 or 2); false when it is not a patch we know
static bool pf_result_of(const BYTE *pt, DWORD pn, unsigned *rsz, const BYTE **rsha) {
    if (pn < 16 || memcmp(pt, "CWOPATCH", 8)) return false;
    unsigned v = pf_u32(pt + 8);
    if (v == 1 && pn >= 88) { *rsz = pf_u32(pt + 48); *rsha = pt + 52; return true; }
    if (v == 2) {
        unsigned n = pf_u32(pt + 12);
        DWORD at = 16 + n * 36;
        if (n >= 1 && n <= (unsigned)PF_MAXSRC && pn >= at + 40) { *rsz = pf_u32(pt + at); *rsha = pt + at + 4; return true; }
    }
    return false;
}

// one patch file: rel = the game path ("Assets\Data\Menu\livelobby_en.ssb")
// v5.150.0 (owner 2026-11-11): patches under
// patches\_data\ build into the data folder itself (saved outfits OutfitStock\, charsrc\, outfit_base\, holiday\ menus -
// files the loader keeps beside its settings, made from game files, so they ship only as patches). A different file the
// player already has there is kept as <name>.before_pack (never overwritten, never deleted).
static wchar_t g_pf_data[MAX_PATH];                              // the data folder (empty in the offline test: OUT\_data\...)
static void pf_one(const wchar_t *patch_path, const wchar_t *rel, wchar_t (*stock_rel)[MAX_PATH], int nsrc) {
    wchar_t outp[MAX_PATH];
    bool data_rel = !_wcsnicmp(rel, L"_data\\", 6);
    if (data_rel && (wcsstr(rel, L"..") || wcschr(rel, L':'))) { logf("patches: %ls refused (bad path)", rel); g_pf_refused++; return; }
    if (data_rel && g_pf_data[0]) { if (swprintf_s(outp, MAX_PATH, L"%s%s", g_pf_data, rel + 6) < 0) return; }
    else if (swprintf_s(outp, MAX_PATH, L"%s%s", g_over, rel) < 0) return;
    DWORD pn = 0;
    BYTE *pt = pf_read(patch_path, &pn);
    if (!pt) { logf("patches: %ls unreadable", rel); g_pf_refused++; return; }
    unsigned want_sz = 0; const BYTE *want_sha = NULL;
    if (pf_result_of(pt, pn, &want_sz, &want_sha)) {               // already built and unchanged: kept
        DWORD cn = 0;
        BYTE *cur = pf_read(outp, &cn);
        if (cur) {
            unsigned char h[32];
            cwo_sha256(cur, cn, h);
            bool same = cn == want_sz && !memcmp(h, want_sha, 32);
            free(cur);
            if (same) { g_pf_kept++; free(pt); return; }
        }
    }
    BYTE *srcs[PF_MAXSRC] = {}; DWORD sns[PF_MAXSRC] = {};
    bool all = nsrc >= 1;
    for (int k = 0; k < nsrc; k++) {
        wchar_t sp[MAX_PATH];
        if (swprintf_s(sp, MAX_PATH, L"%s%s", g_install, stock_rel[k]) < 0 || !(srcs[k] = pf_read(sp, &sns[k]))) all = false;
    }
    BYTE *stock = srcs[0]; DWORD sn = sns[0];
    BYTE *res = NULL; DWORD rn = 0;
    int r = !all ? -2 : (pn >= 12 && pf_u32(pt + 8) == 2) ? pf_apply2(pt, pn, srcs, sns, nsrc, &res, &rn) : pf_apply(pt, pn, stock, sn, &res, &rn);
    if (r == 1 && data_rel && GetFileAttributesW(outp) != INVALID_FILE_ATTRIBUTES) {   // the player's own different file: kept
        wchar_t keep[MAX_PATH];
        swprintf_s(keep, MAX_PATH, L"%s.before_pack", outp);
        if (GetFileAttributesW(keep) != INVALID_FILE_ATTRIBUTES) swprintf_s(keep, MAX_PATH, L"%s.before_pack_%llu", outp, GetTickCount64());
        if (MoveFileExW(outp, keep, 0)) logf("patches: your own %ls kept as %ls", rel, keep);
    }
    if (r == 1 && pf_write(outp, res, rn)) {
        g_pf_built++;
        logf("patches: %ls built (%lu bytes)", rel, (unsigned long)rn);
    } else {
        g_pf_refused++;
        logf("patches: %ls REFUSED (%s) - the game's own file is used", rel,
             r == -1 ? "not a patch" : r == -2 ? (stock ? "your game file is not the version the pack was made for" : "game file not readable")
             : r == -3 ? "damaged patch" : r == -4 ? "result check failed" : "write failed");
        if (GetFileAttributesW(outp) != INVALID_FILE_ATTRIBUTES) {   // an older result: kept aside, not used
            wchar_t st[MAX_PATH];
            swprintf_s(st, MAX_PATH, L"%s.stale", outp);
            MoveFileExW(outp, st, MOVEFILE_REPLACE_EXISTING);
        }
    }
    free(res); free(pt);
    for (int k = 0; k < nsrc; k++) free(srcs[k]);
}

static void pf_walk(const wchar_t *dir, const wchar_t *rel, int depth) {
    if (depth > 8) return;
    wchar_t pat[MAX_PATH];
    if (swprintf_s(pat, MAX_PATH, L"%s%s*", dir, rel) < 0) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t r2[MAX_PATH];
        if (swprintf_s(r2, MAX_PATH, L"%s%s", rel, fd.cFileName) < 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            wcscat_s(r2, MAX_PATH, L"\\");
            pf_walk(dir, r2, depth + 1);
            continue;
        }
        size_t l = wcslen(r2);
        if (l <= 9 || _wcsicmp(r2 + l - 9, L".cwopatch")) continue;
        wchar_t full[MAX_PATH], game_rel[MAX_PATH], from[MAX_PATH], stock_rel[PF_MAXSRC][MAX_PATH];
        int nsrc = 1;
        if (swprintf_s(full, MAX_PATH, L"%s%s", dir, r2) < 0) continue;
        wcscpy_s(game_rel, MAX_PATH, r2);
        game_rel[l - 9] = 0;
        wcscpy_s(stock_rel[0], MAX_PATH, game_rel);
        // v5.64.3 a new file made from ANOTHER game file (the BFR pack's Edgar's Dream summit: Map\mx401.dat from
        // Map\st01.dat): <file>.cwofrom names that file, relative under Assets\ (no "..", no drive)
        if (swprintf_s(from, MAX_PATH, L"%s%s.cwofrom", dir, game_rel) > 0) {
            DWORD fn = 0;
            BYTE *fb = pf_read(from, &fn);
            if (fb) {                                                // v5.149.0: one source per line (version 2: up to 8)
                bool bad = false;
                nsrc = 0;
                DWORD i = 0;
                while (i < fn && !bad) {
                    char a[MAX_PATH] = "";
                    int k = 0;
                    while (i < fn && fb[i] != '\n') { if (k < MAX_PATH - 1 && fb[i] != '\r') a[k++] = (char)fb[i]; i++; }
                    i++;
                    while (k > 0 && a[k - 1] == ' ') a[--k] = 0;
                    if (!k) continue;
                    if (nsrc >= PF_MAXSRC || _strnicmp(a, "Assets\\", 7) || strstr(a, "..") || strchr(a, ':')) { bad = true; break; }
                    swprintf_s(stock_rel[nsrc++], MAX_PATH, L"%S", a);
                }
                free(fb);
                if (bad || !nsrc) {
                    logf("patches: %ls REFUSED (its source name is not a game file)", game_rel);
                    g_pf_refused++;
                    continue;
                }
            }
        }
        pf_one(full, game_rel, stock_rel, nsrc);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void pf_build_all() {                                    // worker start (Store mode)
    wchar_t dir[MAX_PATH];
    if (swprintf_s(dir, MAX_PATH, L"%spatches\\", g_dir) < 0) return;
    if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) return;   // no pack patches: nothing to do
    if (!g_store_mode) { logf("patches: a loose copy of the game - the pack's patches are for the Store install; not used"); return; }
    wcscpy_s(g_pf_data, MAX_PATH, g_dir);                        // v5.150.0 _data\ results go beside the settings
    pf_walk(dir, L"", 0);
    logf("patches: %d built, %d already in place, %d refused", g_pf_built, g_pf_kept, g_pf_refused);
    if (g_pf_refused) set_status("BFR: some game files did not match the pack - see the loader log");
}

// offline test: a patch made here (lengthen + change) applies; a changed stock file and a wrong result are refused
static int pf_self_test() {
    bool sha = cwo_sha256_self_test();
    BYTE stock[300], want[340];
    for (int i = 0; i < 300; i++) stock[i] = (BYTE)(i * 7);
    memcpy(want, stock, 300);
    for (int i = 0; i < 40; i++) want[300 + i] = (BYTE)(0xA0 + i);
    want[10] = 0x55; want[11] = 0x66;
    memcpy(want + 200, stock, 20);                                  // moved bytes: a copy range
    BYTE pt[88 + 8 + 2 + 8 + 40 + 12];
    memcpy(pt, "CWOPATCH", 8);
    auto put = [&](int at, unsigned v) { pt[at] = (BYTE)v; pt[at + 1] = (BYTE)(v >> 8); pt[at + 2] = (BYTE)(v >> 16); pt[at + 3] = (BYTE)(v >> 24); };
    put(8, 1); put(12, 300); cwo_sha256(stock, 300, pt + 16); put(48, 340); cwo_sha256(want, 340, pt + 52); put(84, 3);
    put(88, 10); put(92, 2); pt[96] = 0x55; pt[97] = 0x66;
    put(98, 300); put(102, 40); memcpy(pt + 106, want + 300, 40);
    put(146, 200); put(150, 0x80000000u | 20); put(154, 0);
    BYTE *o = NULL; DWORD on = 0;
    bool built = pf_apply(pt, sizeof(pt), stock, 300, &o, &on) == 1 && on == 340 && !memcmp(o, want, 340);
    free(o);
    stock[5] ^= 1;
    bool other = pf_apply(pt, sizeof(pt), stock, 300, &o, &on) == -2 && !o;
    stock[5] ^= 1;
    pt[96] = 0x56;
    bool wrong = pf_apply(pt, sizeof(pt), stock, 300, &o, &on) == -4 && !o;
    pt[96] = 0x55;
    put(102, 400);
    bool range = pf_apply(pt, sizeof(pt), stock, 300, &o, &on) == -3 && !o;
    // v5.149.0 version 2: 10 bytes from source 1, 4 literal bytes, 6 bytes from source 0
    BYTE s0[16], s1[16], w2[20];
    for (int i = 0; i < 16; i++) { s0[i] = (BYTE)(0x10 + i); s1[i] = (BYTE)(0x80 + i); }
    memcpy(w2, s1 + 3, 10); w2[10] = 1; w2[11] = 2; w2[12] = 3; w2[13] = 4; memcpy(w2 + 14, s0 + 2, 6);
    BYTE p2[16 + 72 + 40 + 16 + 12 + 16];
    memcpy(p2, "CWOPATCH", 8);
    auto put2 = [&](int at, unsigned v) { p2[at] = (BYTE)v; p2[at + 1] = (BYTE)(v >> 8); p2[at + 2] = (BYTE)(v >> 16); p2[at + 3] = (BYTE)(v >> 24); };
    put2(8, 2); put2(12, 2); put2(16, 16); cwo_sha256(s0, 16, p2 + 20); put2(52, 16); cwo_sha256(s1, 16, p2 + 56);
    put2(88, 20); cwo_sha256(w2, 20, p2 + 92); put2(124, 3);
    put2(128, 0); put2(132, 0x80000000u | 10); put2(136, 1); put2(140, 3);
    put2(144, 10); put2(148, 4); p2[152] = 1; p2[153] = 2; p2[154] = 3; p2[155] = 4;
    put2(156, 14); put2(160, 0x80000000u | 6); put2(164, 0); put2(168, 2);
    BYTE *srcs2[2] = {s0, s1}; DWORD sn2[2] = {16, 16};
    bool v2 = pf_apply2(p2, 172, srcs2, sn2, 2, &o, &on) == 1 && on == 20 && !memcmp(o, w2, 20);
    free(o);
    s1[0] ^= 1;
    bool v2other = pf_apply2(p2, 172, srcs2, sn2, 2, &o, &on) == -2 && !o;
    s1[0] ^= 1;
    logf("self-test: game-file patches sha-256 %s, built %s, other stock refused %s, wrong result refused %s, bad range refused %s, version 2 %s / other source refused %s",
         sha ? "ok" : "FAILED", built ? "ok" : "FAILED", other ? "ok" : "FAILED", wrong ? "ok" : "FAILED", range ? "ok" : "FAILED",
         v2 ? "ok" : "FAILED", v2other ? "ok" : "FAILED");
    return sha && built && other && wrong && range && v2 && v2other ? 1 : -58;
}

// offline test (tests/pf_test.cpp): one real patch file applied to a stock file, the result written to out_path
// returns pf_apply's code (1 = built and checked), -9 when a file cannot be read or written
extern "C" __declspec(dllexport) int CwoPatchFileTest(const wchar_t *patch_path, const wchar_t *stock_path, const wchar_t *out_path) {
    DWORD pn = 0, sn = 0, rn = 0;
    BYTE *pt = pf_read(patch_path, &pn), *st = pf_read(stock_path, &sn), *res = NULL;
    int r = pt && st ? pf_apply(pt, pn, st, sn, &res, &rn) : -9;
    if (r == 1 && !pf_write(out_path, res, rn)) r = -9;
    free(res); free(st); free(pt);
    return r;
}

// v5.64.3 offline test (tests/pf_test.cpp dir mode): the real walk over a pack's patches folder, built into OUT from the
// game files under INSTALL (.cwofrom sources included); returns built * 1000 + refused
extern "C" __declspec(dllexport) int CwoPatchDirTest(const wchar_t *patches_dir, const wchar_t *install, const wchar_t *out) {
    wcscpy_s(g_install, install);
    wcscpy_s(g_over, out);
    bool sm = g_store_mode;
    g_store_mode = true;
    g_pf_built = g_pf_kept = g_pf_refused = 0;
    pf_walk(patches_dir, L"", 0);
    g_store_mode = sm;
    return g_pf_built * 1000 + g_pf_refused;
}
