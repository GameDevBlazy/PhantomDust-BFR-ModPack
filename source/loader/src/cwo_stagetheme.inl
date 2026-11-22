// ---- v5.191.0 VISUAL THEMES (owner 2026-11-21, then "all these in order"): HD Remaster, Anime / Cel, Neon Ruins, Overgrown, Frozen Earth.
// MODS edition, offline only. The themed files are built by 30_PD_UE_Stage_Bridge/tools/texture_theme.py into
// <loader folder>\themes\<theme>\Assets\Data\... (every stage file + Edgar's outfit 1): the same files with only the colour
// data of the colour textures rewritten in place (same sizes, alpha / see-through texels kept exactly, lighting data such as
// normal / bump / reflection maps and effect sprites untouched). The game opens files through CreateFile2 (cwo_loader.cpp
// h_CreateFile2); while a theme is on, a file that exists in that theme's folder is opened from there instead - read only.
//
// v5.192.0 (owner 2026-11-21): an in-game option, live.
// - PAUSE MENU row "Theme: ..." (tools/build_pause_theme.py: text = 2.8 op 124, A = op 125) and MOD LIST R3 move to the next
//   theme (off, hd, cel, neon, overgrown, frozen); the choice is kept in CwoStageTheme.txt.
// - The stage that is on screen changes at once. Game facts (recorded: MODLOG 2026-10-26 Underground pass + 2026-10-31
//   stage portraits): FUN_1401d8820 points *0x140530328 at the loaded stage .alr before FUN_1402934e0 registers it; the
//   .alr's texture table = chunk 0x15 at file + u32[1] (u16 count at +8, 28-byte entries from +0xc); once created, an entry's
//   i32 is -1 and entry + 4 holds the texture object {+8 pixels, +0x20 ID3D11Texture2D, +0x28 shader resource view}
//   (FUN_14001de70 / FUN_1400bcac0 / FUN_14001d340). The loader reads the theme's copy (or the stock file) of the stage that
//   was opened, checks that every texture's bytes in memory are the bytes of the file it was loaded from (same offset - the
//   theme files only differ inside texture data), and gives each changed texture its own DEFAULT-usage texture (the
//   cwo_portrait.inl way) holding the new pixels; later switches overwrite it with UpdateSubresource. The bytes in memory are
//   updated too, so the next check compares against the new file. The game's old texture / view are released only when
//   nothing but the object holds them (reference count), otherwise kept.
// - Edgar's outfit 1 takes the theme from its next load (the fighter files' place in memory is not researched yet).
static const wchar_t *ST_THEMES[6] = {L"", L"hd", L"cel", L"neon", L"overgrown", L"frozen"};
static const char *ST_NAMES[6] = {"OFF", "HD REMASTER", "ANIME / CEL", "NEON RUINS", "OVERGROWN", "FROZEN EARTH"};
static volatile LONG g_st_theme;                                 // 0 off, 1..5
static int g_st_switch = -1;
static ULONGLONG g_st_t;
static const ULONG_PTR RVA_STAGE_ALR = 0x530328;
static wchar_t g_st_open[MAX_PATH];                              // the stage file the game opened last (its own path)
static wchar_t g_st_rel[MAX_PATH];
static volatile LONG g_st_open_theme = -1;                       // the theme it was opened with
static volatile LONG g_st_mem_theme = -1;                        // the theme its textures in memory have now
static volatile LONG g_st_live;                                  // 1 = swap the stage on screen
static CRITICAL_SECTION g_st_lock;
static LONG g_st_lock_ready;

static bool st_theme_path(const wchar_t *f, wchar_t *out) {     // the theme's copy of game file f, when it has one
    LONG t = g_st_theme;
    if (t <= 0 || t > 5 || !f) return false;
    wchar_t rel[MAX_PATH];
    if (!sm_relative(f, rel, MAX_PATH)) return false;
    if (swprintf_s(out, MAX_PATH, L"%sthemes\\%s\\%s", g_dir, ST_THEMES[t], rel) < 0) return false;
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

static void st_lock_init() { if (!InterlockedExchange(&g_st_lock_ready, 1)) InitializeCriticalSection(&g_st_lock); }

// CreateFile2: a battle / hub stage file (Map\stNN.alr; not st00 = the shared effects, not the hub overlays stNN_MM)
static void st_note_open(const wchar_t *f, int theme) {
    wchar_t rel[MAX_PATH];
    if (!f || !sm_relative(f, rel, MAX_PATH)) return;
    const wchar_t *b = wcsrchr(rel, L'\\');
    b = b ? b + 1 : rel;
    size_t n = wcslen(b);
    if (n != 8 || _wcsnicmp(b, L"st", 2) || !iswdigit(b[2]) || !iswdigit(b[3]) || _wcsicmp(b + 4, L".alr")) return;
    if (b[2] == L'0' && b[3] == L'0') return;
    // v5.192.3 the game opens "Assets/Data/map\st13.alr" (lower-case map; file_requests.csv 2026-11-22) - compare without case
    wchar_t low[MAX_PATH];
    wcscpy_s(low, rel);
    _wcslwr_s(low, MAX_PATH);
    if (_wcsnicmp(low, L"map\\", 4) && !wcsstr(low, L"\\map\\")) return;
    st_lock_init();
    EnterCriticalSection(&g_st_lock);
    wcscpy_s(g_st_open, f);
    wcscpy_s(g_st_rel, rel);
    InterlockedExchange(&g_st_open_theme, theme);
    InterlockedExchange(&g_st_mem_theme, theme);
    LeaveCriticalSection(&g_st_lock);
}

static unsigned char *st_read_all(const wchar_t *p, DWORD *len) {
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz = {};
    unsigned char *m = NULL;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0x1000 && sz.QuadPart < 0x8000000) {
        m = (unsigned char *)malloc((size_t)sz.QuadPart);
        DWORD got = 0;
        if (m && (!ReadFile(h, m, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart)) { free(m); m = NULL; }
        if (m) *len = got;
    }
    CloseHandle(h);
    return m;
}

static UINT st_block(DXGI_FORMAT f, UINT *pix) {                // bytes per 4x4 block (BC) or per pixel (*pix = 1)
    *pix = 0;
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: return 8;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: return 16;
    case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B4G4R4A4_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM: *pix = 1; return 2;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM: *pix = 1; return 4;
    default: return 0;
    }
}

static ULONG st_refs(IUnknown *u) { if (!u) return 0; u->AddRef(); return u->Release(); }

// swap the textures of the stage on screen to theme t; game thread
static void st_live_swap(int t) {
    unsigned char *alr = NULL;
    __try { alr = *(unsigned char **)(g_base + RVA_STAGE_ALR); } __except (EXCEPTION_EXECUTE_HANDLER) { alr = NULL; }
    ID3D11Device *dev = *(ID3D11Device **)(g_base + RVA_D3D_DEVICE);
    ID3D11DeviceContext *ctx = *(ID3D11DeviceContext **)(g_base + RVA_D3D_CTX);
    st_lock_init();
    wchar_t open[MAX_PATH], rel[MAX_PATH];
    EnterCriticalSection(&g_st_lock);
    wcscpy_s(open, g_st_open); wcscpy_s(rel, g_st_rel);
    LeaveCriticalSection(&g_st_lock);
    int cur = g_st_mem_theme;
    // v5.192.1 (owner test 22:03: every press said "no stage on screen"): *0x140530328 is set while the stage is registered and
    // is not kept, so the stage's own slot is used too: *0x14052ded0 + 0x400000 (FUN_1401d85f0, recorded 2026-10-26); either
    // address is only used when its first 4 KB are the stage file's own bytes (checked below)
    unsigned char *slot = NULL;
    __try { unsigned char *pool = *(unsigned char **)(g_base + 0x52ded0); slot = pool ? pool + 0x400000 : NULL; } __except (EXCEPTION_EXECUTE_HANDLER) { slot = NULL; }
    if (!dev || !ctx) { logf("visual theme live: no D3D device yet"); return; }
    if (!open[0] || cur < 0) { logf("visual theme live: no stage file seen opening (Map\\stNN.alr)"); return; }
    if (cur == t) { logf("visual theme live: the stage already has %s", ST_NAMES[t]); return; }
    wchar_t pc[MAX_PATH], pn[MAX_PATH], ov[MAX_PATH];             // the file its memory holds now / the file wanted
    const wchar_t *stock = sm_redirect(open, ov, MAX_PATH, false) ? ov : open;
    if (cur) swprintf_s(pc, MAX_PATH, L"%sthemes\\%s\\%s", g_dir, ST_THEMES[cur], rel); else wcscpy_s(pc, stock);
    if (t) swprintf_s(pn, MAX_PATH, L"%sthemes\\%s\\%s", g_dir, ST_THEMES[t], rel); else wcscpy_s(pn, stock);
    DWORD lc = 0, ln = 0;
    unsigned char *fc = st_read_all(pc, &lc), *fn = st_read_all(pn, &ln);
    int swapped = 0, same = 0, refused = 0, kept_old = 0;
    __try {
        if (!fc || !fn || lc != ln) { logf("visual theme live: %ls / %ls could not be read or differ in size - nothing changed", pc, pn); __leave; }
        // v5.192.3 (in game 2026-11-22, memory read of st13 in my session: bytes 0..0x1a7 = the file, 859 bytes after differ):
        // the game rewrites the texture table's entries (-1 + object pointer) once loaded, and the chunks after it get pointer
        // fix-ups too (0x784.. differ), so the check compares the header + chunk offsets up to the table's entries; every
        // texture is then checked byte for byte against the current file below (pixels are never rewritten)
        DWORD t0 = *(DWORD *)(fc + 4), head = t0 + 0xc <= 0x1000 ? t0 + 0xc : 0x1000;
        DWORD tend = 0, tail = 0;
        if (head < 0x40) { logf("visual theme live: the stage file's table is too near its start - nothing changed"); __leave; }
        bool a_ok = false, s_ok = false;
        __try { a_ok = alr && !memcmp(alr, fc, head) && (!tail || !memcmp(alr + tend, fc + tend, tail)); } __except (EXCEPTION_EXECUTE_HANDLER) { a_ok = false; }
        __try { s_ok = slot && !memcmp(slot, fc, head) && (!tail || !memcmp(slot + tend, fc + tend, tail)); } __except (EXCEPTION_EXECUTE_HANDLER) { s_ok = false; }
        if (!a_ok && s_ok) alr = slot;
        if (!a_ok && !s_ok) {
            logf("visual theme live: %ls not found in memory (pointer %p, slot %p) - nothing changed", rel, alr, slot);
            __leave;
        }
        DWORD to = *(DWORD *)(fc + 4);
        if (to + 12 > lc || *(DWORD *)(fc + to) != 0x15) { logf("visual theme live: no texture table - nothing changed"); __leave; }
        int count = *(unsigned short *)(fc + to + 8);
        for (int i = 0; i < count && to + 0xc + 28 * (i + 1) <= lc; i++) {
            unsigned char *e = alr + to + 0xc + 28 * i;
            if (*(int *)e != -1) continue;
            unsigned char *obj = *(unsigned char **)(e + 4);
            if (!obj) continue;
            unsigned char *pix = *(unsigned char **)(obj + 8);
            ID3D11Texture2D *tex = *(ID3D11Texture2D **)(obj + 0x20);
            if (!pix || !tex || pix < alr || pix >= alr + lc) { refused++; continue; }
            DWORD off = (DWORD)(pix - alr);
            D3D11_TEXTURE2D_DESC td = {};
            tex->GetDesc(&td);
            UINT isp = 0, bb = st_block(td.Format, &isp);
            if (!bb || td.ArraySize != 1 || td.MipLevels < 1 || td.MipLevels > 14) { refused++; continue; }
            D3D11_SUBRESOURCE_DATA sub[14] = {};
            UINT64 total = 0;
            for (UINT m = 0, w = td.Width, h = td.Height; m < td.MipLevels; m++, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1) {
                UINT pitch = isp ? w * bb : (w + 3) / 4 * bb, rows = isp ? h : (h + 3) / 4;
                sub[m].pSysMem = fn + off + total; sub[m].SysMemPitch = pitch;
                total += (UINT64)pitch * rows;
            }
            if (off + total > lc) { refused++; continue; }
            if (memcmp(pix, fc + off, (size_t)total)) { refused++; continue; }     // memory is not the file it came from
            if (!memcmp(fc + off, fn + off, (size_t)total)) { same++; continue; }   // this texture is not themed
            if (td.Usage == D3D11_USAGE_DEFAULT && (td.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
                for (UINT m = 0; m < td.MipLevels; m++) ctx->UpdateSubresource(tex, m, NULL, sub[m].pSysMem, sub[m].SysMemPitch, 0);
            } else {
                D3D11_TEXTURE2D_DESC nd = td;
                nd.Usage = D3D11_USAGE_DEFAULT; nd.CPUAccessFlags = 0; nd.BindFlags = D3D11_BIND_SHADER_RESOURCE; nd.MiscFlags = 0;
                ID3D11Texture2D *mine = NULL; ID3D11ShaderResourceView *srv = NULL;
                if (FAILED(dev->CreateTexture2D(&nd, sub, &mine)) || !mine) { refused++; continue; }
                D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
                vd.Format = nd.Format; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = nd.MipLevels;
                if (FAILED(dev->CreateShaderResourceView(mine, &vd, &srv)) || !srv) { mine->Release(); refused++; continue; }
                IUnknown *old_tex = *(IUnknown **)(obj + 0x20), *old_srv = *(IUnknown **)(obj + 0x28);
                *(ID3D11Texture2D **)(obj + 0x20) = mine;
                *(ID3D11ShaderResourceView **)(obj + 0x28) = srv;
                if (old_srv && st_refs(old_srv) == 1) old_srv->Release(); else if (old_srv) kept_old++;
                if (old_tex && st_refs(old_tex) == 1) old_tex->Release(); else if (old_tex) kept_old++;
            }
            memcpy(pix, fn + off, (size_t)total);
            swapped++;
        }
        InterlockedExchange(&g_st_mem_theme, t);
        logf("visual theme live: %ls %s -> %s: %d textures changed, %d not themed, %d refused, %d old kept (still held elsewhere)",
             rel, ST_NAMES[cur], ST_NAMES[t], swapped, same, refused, kept_old);
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("visual theme live: stopped by an exception after %d textures", swapped); }
    free(fc); free(fn);
}

static void st_write_choice(int t) {
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoStageTheme.txt", g_dir);
    if (!t) { DeleteFileW(p); return; }
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") == 0 && f) { fprintf(f, "%ls\n", ST_THEMES[t]); fclose(f); }
}

static int st_next() {                                           // the pause-menu row / MOD LIST R3: the next theme, live
    if (ed_online()) { logf("visual theme: refused (online - offline only)"); return g_st_theme; }   // v5.199.0 owner 2026-11-22 ("add the following to the BFR mods"): in the BFR edition too
    int t = (g_st_theme + 1) % 6;
    st_write_choice(t);
    InterlockedExchange(&g_st_theme, t);
    g_st_switch = t;
    InterlockedExchange(&g_st_live, 1);
    logf("visual theme: %s chosen in game", ST_NAMES[t]);
    return t;
}

static const char *st_name(int t) { return ST_NAMES[t >= 0 && t <= 5 ? t : 0]; }

static INT64 st_native(int op) {                                 // 2.8 op 124 (row text) / 125 (A on the row)
    static char txt[40];
    if (op == 125) st_next();
    sprintf_s(txt, "Theme: %s", ST_NAMES[g_st_theme >= 0 && g_st_theme <= 5 ? g_st_theme : 0]);
    return (INT64)(ULONG_PTR)txt;
}

static void st_step() {                                          // game thread: the switch file every 3 s, a live swap at once
    if (InterlockedExchange(&g_st_live, 0)) {                    // only while a match runs: outside one the stage's texture
        if (g_in_match) st_live_swap(g_st_theme);                // objects may be gone; the choice then waits for the next load
        else logf("visual theme: %s kept for the next stage load (no match on screen)", ST_NAMES[g_st_theme]);
    }
    ULONGLONG now = GetTickCount64();
    if (now - g_st_t < 3000) return;
    g_st_t = now;
    int t = 0;
    if (!ed_online()) {                                          // v5.199.0 owner 2026-11-22 ("add the following to the BFR mods"): in the BFR edition too
        wchar_t p[MAX_PATH]; FILE *f = NULL;
        swprintf_s(p, MAX_PATH, L"%sCwoStageTheme.txt", g_dir);
        if (cwo_wfopen(&f, p, L"r") == 0 && f) {
            char w[24] = "";
            if (fscanf_s(f, "%23s", w, (unsigned)sizeof(w)) == 1)
                for (int k = 1; k <= 5; k++) { char n[16]; sprintf_s(n, "%ls", ST_THEMES[k]); if (!_stricmp(w, n)) t = k; }
            fclose(f);
        }
    }
    if (t != g_st_switch) {
        bool first = g_st_switch < 0;
        InterlockedExchange(&g_st_theme, t);
        g_st_switch = t;
        if (!first) InterlockedExchange(&g_st_live, 1);          // the file changed by hand: the stage on screen follows too
        logf("visual theme: %s (CwoStageTheme.txt)", ST_NAMES[t]);
    }
}

static int st_self_test() {
    int prev = g_st_theme;
    InterlockedExchange(&g_st_theme, 3);
    const char *s = (const char *)(ULONG_PTR)st_native(124);
    bool ok = s && !strcmp(s, "Theme: NEON RUINS");
    InterlockedExchange(&g_st_theme, prev);
    UINT p = 0;
    ok = ok && st_block(DXGI_FORMAT_BC1_UNORM, &p) == 8 && !p && st_block(DXGI_FORMAT_B5G6R5_UNORM, &p) == 2 && p;
    st_note_open(L"C:\\x\\Assets\\Data\\Map\\st00.alr", 0);
    ok = ok && !g_st_open[0];
    st_note_open(L"Assets/Data/map\\st13.alr", 0);                 // v5.192.3 the game's own spelling (lower-case map)
    ok = ok && !_wcsicmp(g_st_rel, L"Assets\\Data\\map\\st13.alr");
    g_st_open[0] = 0; g_st_rel[0] = 0;
    logf("self-test: visual theme row %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -91;
}
