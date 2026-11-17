// ---- v5.91 lobby face strips per outfit (owner 2026-11-03). The lobby scripts send each member's face draw to op 110(where, word, x, y) (tools/build_lobby_rows.py):
// where 0 = the split-screen lobby (VsMode, image slot 0; returns 0 to let the script draw the stock face), 3 = the Xbox
// Live lobby (MNet, image slot 3; the loader draws the stock face itself when it has none); word = character row | outfit
// << 16. The menu image carries a 128 x 256 DXT1 texture of 8 row cells and 8 sprites lobby_row_0..7 (74 x 24); each draw
// takes the next row cell of this frame, the render thread copies that member's strip (Menu\cwo_lobby_strips.dxt, row =
// stem, column = outfit) into the cell (our DEFAULT-usage copy of the texture, as cwo_selout.inl) and the loader draws the
// sprite with the game's 2.82 (thunk 0x140207730). Display only, this machine only (AGENTS.md "Character select portraits
// online" covers the Xbox Live select pictures; the lobby strips are the same kind of picture).
struct LsImage { int slot, textures, sprites, tex, first; unsigned h0, h1; unsigned char *obj; ID3D11Texture2D *mine; int have[8]; };
static LsImage g_ls_img[2];
static int g_ls_img_n, g_ls_aw, g_ls_ah, g_ls_cw, g_ls_ch, g_ls_cols, g_ls_nstem;
static char g_ls_stems[32][12];
static unsigned char *g_ls_atlas;
static volatile LONG g_ls_want[2][8];                             // per image: the atlas cell wanted in row cell k (-1 none)
static int g_ls_next[2];
static LARGE_INTEGER g_ls_last[2];
static const int LS_TW = 128, LS_TH = 256;

// v5.98 (owner 2026-11-03;
// "grab the lobby character portraits that I have on there"): the lobbies show the game's own strip again - window_chara_NN,
// now the owner's colour art + the monsters coloured in (tools/build_lobby_colour.py); the per-outfit row cells stay off
static const bool g_ls_per_outfit = false;
static void ls_load() {                                           // worker start
    {   // v5.96 the NPC page switch: Menu\cwo_npc_page.txt next to the game's menus (installed with npc00a0..9.alr)
        wchar_t e2[MAX_PATH], p2[MAX_PATH]; GetModuleFileNameW(NULL, e2, MAX_PATH); wchar_t *s2 = wcsrchr(e2, L'\\'); if (s2) s2[1] = 0;
        swprintf_s(p2, MAX_PATH, L"%sAssets\\Data\\Menu\\cwo_npc_page.txt", e2);
        g_npc_page = GetFileAttributesW(p2) != INVALID_FILE_ATTRIBUTES;
        if (!g_npc_page) {                                         // Store mode: the override folder
            swprintf_s(p2, MAX_PATH, L"%sgame\\Assets\\Data\\Menu\\cwo_npc_page.txt", g_dir);
            g_npc_page = GetFileAttributesW(p2) != INVALID_FILE_ATTRIBUTES;
        }
        if (g_npc_page) logf("NPC page: on (row 28 = the ten NPCs)");
    }
    if (!g_ls_per_outfit) return;                                  // v5.98 the game's own strips
    for (int i = 0; i < 2; i++) for (int k = 0; k < 8; k++) g_ls_want[i][k] = -1;
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\'); if (sl) sl[1] = 0;
    char line[400];
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Menu\\cwo_lobby_rows.txt", exe);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) { logf("lobby strips: no Menu\\cwo_lobby_rows.txt - the lobbies keep their own faces"); return; }
    while (fgets(line, sizeof(line), f)) {
        int a, b, c, d, e; unsigned h0, h1;
        if (sscanf_s(line, "#image\t%d\t%d\t%d\t%d\t%d\t%u\t%u", &a, &b, &c, &d, &e, &h0, &h1) == 7 && g_ls_img_n < 2 &&
            (a == 0 || a == 3) && b > 0 && b < 256 && c > 0 && c <= 128 && d >= 0 && d < b && e >= 0 && e + 8 <= c) {
            LsImage *im = &g_ls_img[g_ls_img_n++]; memset(im, 0, sizeof(*im));
            im->slot = a; im->textures = b; im->sprites = c; im->tex = d; im->first = e; im->h0 = h0; im->h1 = h1;
            for (int k = 0; k < 8; k++) im->have[k] = -2;
        }
    }
    fclose(f);
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Menu\\cwo_lobby_strips.txt", exe);
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) { g_ls_img_n = 0; return; }
    while (fgets(line, sizeof(line), f)) {
        int a, b, c, d, e;
        if (sscanf_s(line, "#atlas\t%d\t%d\t%d\t%d\t%d", &a, &b, &c, &d, &e) == 5) { g_ls_aw = a; g_ls_ah = b; g_ls_cw = c; g_ls_ch = d; g_ls_cols = e; }
        else if (!strncmp(line, "#stems\t", 7)) {
            char *ctx = NULL, *t = strtok_s(line + 7, "\t\r\n", &ctx);
            for (g_ls_nstem = 0; t && g_ls_nstem < 32; t = strtok_s(NULL, "\t\r\n", &ctx)) strncpy_s(g_ls_stems[g_ls_nstem++], t, _TRUNCATE);
        }
    }
    fclose(f);
    if (g_ls_cw != 76 || g_ls_ch != 24 || g_ls_cols < 1 || g_ls_nstem < 1 || (g_ls_aw & 3) || (g_ls_ah & 3) || g_ls_cols * g_ls_cw > g_ls_aw || g_ls_nstem * g_ls_ch > g_ls_ah) {
        logf("lobby strips: cwo_lobby_strips.txt incomplete - off"); g_ls_img_n = 0; return;
    }
    size_t need = (size_t)(g_ls_aw / 4) * (g_ls_ah / 4) * 8;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Menu\\cwo_lobby_strips.dxt", exe);
    if (cwo_wfopen(&f, path, L"rb") != 0 || !f) { g_ls_img_n = 0; return; }
    g_ls_atlas = (unsigned char *)malloc(need);
    size_t got = g_ls_atlas ? fread(g_ls_atlas, 1, need, f) : 0;
    fclose(f);
    if (got != need) { free(g_ls_atlas); g_ls_atlas = NULL; g_ls_img_n = 0; logf("lobby strips: atlas %zu of %zu bytes - off", got, need); return; }
    logf("lobby strips: %d characters x %d outfits, %d lobby images", g_ls_nstem, g_ls_cols, g_ls_img_n);
}

static int ls_stem_index(int row) {
    const char *stem = on_stem(row);
    for (int i = 0; stem && stem[0] && i < g_ls_nstem; i++) if (!_stricmp(g_ls_stems[i], stem)) return i;
    return -1;
}

typedef INT64 (__fastcall *PFN_LS_NATIVE)(INT64 *args);
static INT64 ls_native_strip(INT64 *args);
// v5.152 the row's win streak (cwo_livesess.inl wst_*): the k-th member row drawn this frame = fighter k in the Xbox Live lobby;
// in the split-screen lobby the rows follow its game type (cwo_gametype.inl GT_ORDER_*: -1 player 1, -2 player 2, else a CPU)
static void wst_draw(int where, int k, int x, int y) {
    int fighter = -1;
    if (where == 3) fighter = k;
    else if (where == 0) {
        int t = g_gt;
        const int *order = t == GT_SIX ? GT_ORDER_SIX : t == GT_TAG ? GT_ORDER_TAG : t == GT_ROYALE ? GT_ORDER_ROY : NULL;
        int who = order ? (k < (t == GT_SIX ? 6 : 4) ? order[k] : 0) : (k == 0 ? -1 : k == 1 ? -2 : 0);
        fighter = who == -1 ? 0 : who == -2 ? 1 : -1;
    }
    int n = wst_of(where, fighter);
    if (n < 2) return;
    __try {
        int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
        if (glyphs > cap - 40) return;
        int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
        int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
        DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
        int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
        DWORD s_rgba[4];
        memcpy(s_rgba, rgba, sizeof(s_rgba));
        PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
        char line[24];
        sprintf_s(line, "STREAK %d", n);
        *space = 0; *gap = 1; *fh = 10; *fw = 5;
        set_rgba(rgba, 0xFF000000); text(x + 3, y + 14, line);
        set_rgba(rgba, 0xFFFFD040); text(x + 2, y + 13, line);
        memcpy(rgba, s_rgba, sizeof(s_rgba));
        *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static INT64 ls_native(INT64 *args) {                             // op 110 (game thread): the face strip, then the win streak
    static LARGE_INTEGER last[4];
    static int next[4];
    int where = 0, x = 0, y = 0;
    __try { where = (int)args[1]; x = (int)args[3]; y = (int)args[4]; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    INT64 r = ls_native_strip(args);
    if (where == 0 || where == 3) {
        int w = where == 3 ? 1 : 0;
        LARGE_INTEGER now, fq; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&fq);
        if ((now.QuadPart - last[w].QuadPart) * 1000 / fq.QuadPart > 6) next[w] = 0;   // a new frame: the first row again
        last[w] = now;
        int k = next[w]++;
        wst_draw(where, k, x, y);
    }
    return r;
}
static INT64 ls_native_strip(INT64 *args) {                       // op 110 (game thread)
    int where = (int)args[1], x = (int)args[3], y = (int)args[4];
    unsigned word = (unsigned)args[2];
    int row = (int)(word & 0xFFFF), outfit = (int)(word >> 16);
    int ii = -1;
    for (int i = 0; i < g_ls_img_n; i++) if (g_ls_img[i].slot == where) ii = i;
    LsImage *im = ii >= 0 ? &g_ls_img[ii] : NULL;
    int st = im ? ls_stem_index(row) : -1;
    __try {
        if (g_ls_per_outfit && im && im->mine && st >= 0 && outfit >= 0 && outfit < g_ls_cols) {
            LARGE_INTEGER now, fq; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&fq);
            if ((now.QuadPart - g_ls_last[ii].QuadPart) * 1000 / fq.QuadPart > 6) g_ls_next[ii] = 0;   // a new frame
            g_ls_last[ii] = now;
            int k = g_ls_next[ii]++;
            if (k < 8) {
                InterlockedExchange(&g_ls_want[ii][k], st * g_ls_cols + outfit);
                if (im->have[k] == st * g_ls_cols + outfit) {
                    INT64 a[4] = {where, im->first + k, x, y};
                    ((PFN_LS_NATIVE)(g_base + 0x207730))(a);
                    return 1;
                }
            }
        }
        if (where == 3) { INT64 a[4] = {3, row, x, y}; ((PFN_LS_NATIVE)(g_base + 0x207730))(a); return 1; }   // the stock strip
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

static unsigned char *ls_object(ULONG_PTR menu, ULONG_PTR mgr, const LsImage *im) {   // as so_object, for our texture
    if (!menu || !mgr || !*(ULONG_PTR *)(menu + 0x10530 + im->slot * 0x20)) return NULL;
    const unsigned *handles = *(const unsigned **)(menu + 0x10548 + im->slot * 0x20);
    if (!handles) return NULL;
    unsigned h = handles[im->first];
    int set = (h >> 16) & 0xff, tex = (h >> 8) & 0xff;
    if ((int)(h >> 24) != im->first || tex != im->tex) return NULL;
    if (*(int *)(mgr + 0x128 + set * 0x30) != im->textures || *(int *)(mgr + 0x12c + set * 0x30) != im->sprites) return NULL;
    const unsigned char *alr = *(const unsigned char **)(mgr + 0x308 + set * 8);
    if (!alr || *(const unsigned *)alr != 0x11) return NULL;
    const unsigned char *ch = alr + *(const unsigned *)(alr + 4);
    if (*(const unsigned *)ch != 0x15) return NULL;
    int n = *(const unsigned short *)(ch + 8);
    for (int i = 0; i < n && i < 256; i++) {
        const unsigned char *e = ch + 0xc + 28 * i;
        if (*(const unsigned *)(e + 0x14) != im->h0 || *(const unsigned *)(e + 0x18) != im->h1) continue;
        if (*(const int *)e != -1) return NULL;
        unsigned char *obj = *(unsigned char **)(e + 4);
        if (!obj || *(const unsigned char **)(obj + 0x18) != e) return NULL;
        unsigned packed = *(const unsigned *)(obj + 0x10);
        if ((1 << ((packed >> 20) & 15)) != LS_TW || (1 << ((packed >> 24) & 15)) != LS_TH || ((packed >> 8) & 0xff) != 0x0c) return NULL;
        return obj;
    }
    return NULL;
}
static bool ls_take(LsImage *im, unsigned char *obj) {
    ID3D11Device *dev = *(ID3D11Device **)(g_base + RVA_D3D_DEVICE);
    const unsigned char *pixels = *(const unsigned char **)(obj + 8);
    if (!dev || !pixels) return false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = LS_TW; td.Height = LS_TH; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_BC1_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {}; init.pSysMem = pixels; init.SysMemPitch = LS_TW / 4 * 8;
    ID3D11Texture2D *tex = NULL; ID3D11ShaderResourceView *srv = NULL;
    if (FAILED(dev->CreateTexture2D(&td, &init, &tex)) || !tex) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {}; vd.Format = td.Format; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(tex, &vd, &srv)) || !srv) { tex->Release(); return false; }
    IUnknown *old_tex = *(IUnknown **)(obj + 0x20), *old_srv = *(IUnknown **)(obj + 0x28);
    *(ID3D11Texture2D **)(obj + 0x20) = tex; *(ID3D11ShaderResourceView **)(obj + 0x28) = srv;
    if (old_srv) old_srv->Release();
    if (old_tex) old_tex->Release();
    im->obj = obj; im->mine = tex;
    for (int k = 0; k < 8; k++) im->have[k] = -2;
    logf("lobby strips: image slot %d texture object %p: own copy in place", im->slot, obj);
    return true;
}
static void lobstrip_step() {                                     // CwoOverlayFrame (render thread, as so_step)
    if (g_ls_img_n <= 0 || !g_ls_atlas) return;
    __try {
        ULONG_PTR menu = *(ULONG_PTR *)(g_base + RVA_MENU_MGR), mgr = *(ULONG_PTR *)(g_base + RVA_SPRITE_MGR);
        ID3D11DeviceContext *ctx = *(ID3D11DeviceContext **)(g_base + RVA_D3D_CTX);
        for (int i = 0; i < g_ls_img_n; i++) {
            LsImage *im = &g_ls_img[i];
            unsigned char *obj = ls_object(menu, mgr, im);
            if (!obj) { im->obj = NULL; im->mine = NULL; continue; }
            if (obj != im->obj || *(ID3D11Texture2D **)(obj + 0x20) != im->mine)
                if (!ls_take(im, obj)) { im->obj = obj; im->mine = NULL; continue; }
            if (!im->mine || !ctx) continue;
            for (int k = 0; k < 8; k++) {
                LONG cell = g_ls_want[i][k];
                if (cell < 0 || cell == im->have[k]) continue;
                int x0 = (cell % g_ls_cols) * g_ls_cw, y0 = (cell / g_ls_cols) * g_ls_ch;
                D3D11_BOX box = {0, (UINT)(24 * k), 0, (UINT)g_ls_cw, (UINT)(24 * k + 24), 1};
                ctx->UpdateSubresource(im->mine, 0, &box, g_ls_atlas + (size_t)(y0 / 4) * (g_ls_aw / 4) * 8 + (x0 / 4) * 8, g_ls_aw / 4 * 8, 0);
                im->have[k] = cell;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
