// ---- v5.77 select portraits follow the highlighted outfit (owner 2026-11-02; "Complete number 8"; AGENTS.md "Character select portraits online": display only, this machine only).
// Game facts (MODLOG 2026-10-27 window record, 2026-11-02 COLORS probe; cwo_portrait.inl for the texture objects):
//   the menu *0x140533c58 keeps window k at +0x40 + k * 0x103c: +0x12 the cursor row (2.116), +0x16 rows, +0x1a the first
//   row shown, +0x1034 its row-draw routine; the COLORS window (split screen and Xbox Live) draws each row through the
//   script routine _draw_plcolo, which now tells the loader it ran (op 79, tools/build_select_outfit_probe.py); op 44 gives
//   the COLORS character (g_col_char, cwo_outfitnames.inl). The grid asks op 77 / 78 for each cell's sprite.
// What this does: the menu image carries one extra 256 x 128 DXT1 cell and its sprite (tools/build_select_outfits.py);
// while a COLORS list is up, the loader puts the highlighted outfit's face (its own atlas Menu\cwo_select_outfits.dxt)
// into its own copy of that cell, and op 77 / 78 hand the grid that sprite for the COLORS character's cell.
struct SoImage { int slot, textures, sprites, tex, sprite, first; unsigned h0, h1; void *obj; ID3D11Texture2D *mine; int last; };
static SoImage g_so_img[2];
static int g_so_img_n, g_so_aw, g_so_ah, g_so_cw, g_so_ch, g_so_cols, g_so_nstem;
static char g_so_stems[32][12];                                 // v5.86 32: players + monsters
static unsigned char *g_so_atlas;
// g_so_seen (the last COLORS row draw, op 79) is declared in cwo_loader.cpp (v5.86.4: cwo_holiday.inl reads it too)
static volatile LONG g_so_want = -1;                              // atlas cell wanted (game thread) -> render thread
static int g_so_win = -1;                                         // the COLORS window index found
static const int SO_TW = 256, SO_TH = 128;

static void so_load() {                                           // worker start
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\');
    if (sl) sl[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Menu\\cwo_select_outfits.txt", exe);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) { logf("select outfits: no Menu\\cwo_select_outfits.txt - the select keeps one face per character"); return; }
    char line[400];
    while (fgets(line, sizeof(line), f)) {
        int a, b, c, d, e, r; unsigned h0, h1;
        if (sscanf_s(line, "#atlas\t%d\t%d\t%d\t%d\t%d", &a, &b, &c, &d, &e) == 5) { g_so_aw = a; g_so_ah = b; g_so_cw = c; g_so_ch = d; g_so_cols = e; continue; }
        if (sscanf_s(line, "#image\t%d\t%d\t%d\t%d\t%d\t%u\t%u\t%d", &a, &b, &c, &d, &e, &h0, &h1, &r) == 8 && g_so_img_n < 2) {
            if (a < 0 || a > 7 || b < 1 || b > 255 || c < 1 || c > 128 || d < 0 || d >= b || e < 0 || e >= c) { logf("select outfits: image line refused"); continue; }
            SoImage *im = &g_so_img[g_so_img_n++];
            memset(im, 0, sizeof(*im));
            im->slot = a; im->textures = b; im->sprites = c; im->tex = d; im->sprite = e; im->h0 = h0; im->h1 = h1; im->first = r; im->last = -2;
            continue;
        }
        if (!strncmp(line, "#stems\t", 7)) {
            char *ctx = NULL, *t = strtok_s(line + 7, "\t\r\n", &ctx);
            for (g_so_nstem = 0; t && g_so_nstem < 32; t = strtok_s(NULL, "\t\r\n", &ctx)) strncpy_s(g_so_stems[g_so_nstem++], t, _TRUNCATE);
        }
    }
    fclose(f);
    if (g_so_img_n < 1 || g_so_nstem < 1 || g_so_cw < 4 || g_so_ch < 4 || (g_so_cw & 3) || (g_so_ch & 3) || g_so_cw > SO_TW || g_so_ch > SO_TH ||
        g_so_cols < 1 || g_so_cols * g_so_cw > g_so_aw || g_so_nstem * g_so_ch > g_so_ah || (g_so_aw & 3) || (g_so_ah & 3)) {
        logf("select outfits: Menu\\cwo_select_outfits.txt incomplete - off"); g_so_img_n = 0; return;
    }
    size_t need = (size_t)(g_so_aw / 4) * (g_so_ah / 4) * 8;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Menu\\cwo_select_outfits.dxt", exe);
    FILE *af = NULL;
    if (cwo_wfopen(&af, path, L"rb") != 0 || !af) { logf("select outfits: no Menu\\cwo_select_outfits.dxt - off"); g_so_img_n = 0; return; }
    g_so_atlas = (unsigned char *)malloc(need);
    size_t got = g_so_atlas ? fread(g_so_atlas, 1, need, af) : 0;
    fclose(af);
    if (got != need) { logf("select outfits: atlas is %zu bytes, not %zu - off", got, need); free(g_so_atlas); g_so_atlas = NULL; g_so_img_n = 0; return; }
    logf("select outfits: %d characters x %d outfits, %d images", g_so_nstem, g_so_cols, g_so_img_n);
}

static int so_stem_index(const char *stem) {
    for (int i = 0; stem && stem[0] && i < g_so_nstem; i++) if (!_stricmp(g_so_stems[i], stem)) return i;
    return -1;
}

// the COLORS window's cursor (game thread, while op 79 runs): the window whose row-draw name is _DRAW_PLCOLO
static int so_cursor() {
    ULONG_PTR menu = *(ULONG_PTR *)(g_base + RVA_MENU_MGR);
    if (!menu) return -1;
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < 16; k++) {
            if (pass == 0 && g_so_win >= 0 && k != g_so_win) continue;
            unsigned char *w = (unsigned char *)(menu + 0x40 + (ULONG_PTR)k * 0x103c);
            const char *nm = (const char *)(w + 0x1034);
            bool hit = !_strnicmp(nm, "_DRAW_PL", 8);
            if (!hit) {                                           // a pointer to the name
                const char *p = *(const char **)(w + 0x1034);
                __try { hit = p && (ULONG_PTR)p > 0x10000 && !_strnicmp(p, "_DRAW_PLCOLO", 12); } __except (EXCEPTION_EXECUTE_HANDLER) { hit = false; }
            }
            if (!hit) continue;
            if (k != g_so_win) { g_so_win = k; logf("select outfits: COLORS = window %d (cursor %d, rows %d, first shown %d)", k, *(short *)(w + 0x12), *(short *)(w + 0x16), *(short *)(w + 0x1a)); }
            int cur = *(short *)(w + 0x12), rows = *(short *)(w + 0x16);
            return cur >= 0 && cur < rows ? cur : -1;
        }
    return -1;
}

static void so_row_drawn() {                                      // op 79 (game thread): a COLORS row is being drawn
    g_so_seen = GetTickCount64();
    if (g_so_img_n <= 0) return;
    int st = so_stem_index(on_stem(g_col_char)), cur = -1;
    __try { cur = so_cursor(); } __except (EXCEPTION_EXECUTE_HANDLER) { cur = -1; }
    LONG cell = st >= 0 && cur >= 0 && cur < g_so_cols ? st * g_so_cols + cur : -1;
    if (cell != g_so_want) { InterlockedExchange(&g_so_want, cell); logf("select outfits: %s outfit %d highlighted", st >= 0 ? g_so_stems[st] : "?", cur + 1); }
}

// op 77 / 78: the grid's sprite for a cell (stock = the sprite it would draw, k = the cell's atlas row); the COLORS
// character's cell shows our cell. v5.86 (owner 2026-11-03): the monsters are atlas rows too.
static int so_cell(int stock, int k, bool live) {
    if (g_so_img_n <= 0 || GetTickCount64() - g_so_seen > 200 || g_so_want < 0) return stock;
    for (int i = 0; i < g_so_img_n; i++) {
        const SoImage &im = g_so_img[i];
        if (live ? im.slot != 3 : im.slot == 3) continue;
        if (k < 0 || k >= g_so_nstem || k != (int)g_so_want / g_so_cols || !im.mine || im.last != (int)g_so_want) return stock;
        return im.sprite;
    }
    return stock;
}
// v5.100 the NPC page's cell (owner 2026-11-03): the
// split-screen grid's slot 3 on the players page (stock sprite base + 3, build_monster_portraits.py) shows atlas row npc00a -
// the highlighted NPC while its COLORS list is up, else colour 1 (Spokesman)
static int so_npc_cell(int stock) {
    int st = so_stem_index("npc00a");
    if (st < 0 || g_so_img_n <= 0 || g_so_cols <= 0) return stock;
    ULONGLONG now = GetTickCount64();
    if (now - g_so_seen > 200) { LONG c = st * g_so_cols; if (g_so_want != c) InterlockedExchange(&g_so_want, c); }
    LONG w = g_so_want;
    if (w < 0 || w / g_so_cols != st) return stock;
    for (int i = 0; i < g_so_img_n; i++) {
        const SoImage &im = g_so_img[i];
        if (im.slot == 3) continue;
        return im.mine && im.last == (int)w ? im.sprite : stock;
    }
    return stock;
}
static int so_sprite(int stock, int first, bool live) { return so_cell(stock, stock - first, live); }   // the player page
static int so_sprite_mon(int sprite, int slot, bool live) {     // the monster page: the grid slot's monster (its row)
    // the pages' character tables (build_monster_portraits.py: vsmenu 0x6b88 / livecharasel 0x480)
    static const int MON_ROWS[12] = {13, 15, 11, 28, 23, 12, 24, 14, 18, 20, 17, 16}, LIVE_ROWS[11] = {13, 15, 11, 23, 12, 24, 14, 18, 20, 17, 16};
    int row = live ? (slot >= 0 && slot < 11 ? LIVE_ROWS[slot] : -1) : (slot >= 0 && slot < 12 ? MON_ROWS[slot] : -1);
    if (row < 0) return sprite;
    return so_cell(sprite, so_stem_index(on_stem(row)), live);
}

static unsigned char *so_object(ULONG_PTR menu, ULONG_PTR mgr, const SoImage *im) {   // as portrait_object, for our cell
    if (!menu || !mgr || !*(ULONG_PTR *)(menu + 0x10530 + im->slot * 0x20)) return NULL;
    const unsigned *handles = *(const unsigned **)(menu + 0x10548 + im->slot * 0x20);
    if (!handles) return NULL;
    unsigned h = handles[im->sprite];
    int set = (h >> 16) & 0xff, tex = (h >> 8) & 0xff;
    if ((int)(h >> 24) != im->sprite || tex != im->tex) return NULL;
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
        if ((1 << ((packed >> 20) & 15)) != SO_TW || (1 << ((packed >> 24) & 15)) != SO_TH || ((packed >> 8) & 0xff) != 0x0c) return NULL;
        return obj;
    }
    return NULL;
}

static bool so_take(SoImage *im, unsigned char *obj) {             // as portrait_take: our DEFAULT-usage copy in the object
    ID3D11Device *dev = *(ID3D11Device **)(g_base + RVA_D3D_DEVICE);
    const unsigned char *pixels = *(const unsigned char **)(obj + 8);
    if (!dev || !pixels) return false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = SO_TW; td.Height = SO_TH; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_BC1_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = pixels; init.SysMemPitch = SO_TW / 4 * 8;
    ID3D11Texture2D *tex = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    if (FAILED(dev->CreateTexture2D(&td, &init, &tex)) || !tex) { logf("select outfits: CreateTexture2D failed"); return false; }
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = td.Format; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(tex, &vd, &srv)) || !srv) { tex->Release(); logf("select outfits: CreateShaderResourceView failed"); return false; }
    IUnknown *old_tex = *(IUnknown **)(obj + 0x20), *old_srv = *(IUnknown **)(obj + 0x28);
    *(ID3D11Texture2D **)(obj + 0x20) = tex;
    *(ID3D11ShaderResourceView **)(obj + 0x28) = srv;
    if (old_srv) old_srv->Release();
    if (old_tex) old_tex->Release();
    im->obj = obj; im->mine = tex; im->last = -2;
    logf("select outfits: image slot %d texture object %p: own copy %p in place", im->slot, obj, tex);
    return true;
}

static void so_step() {                                           // CwoOverlayFrame (render thread, as portrait_step)
    if (g_so_img_n <= 0 || !g_so_atlas) return;
    __try {
        ULONG_PTR menu = *(ULONG_PTR *)(g_base + RVA_MENU_MGR), mgr = *(ULONG_PTR *)(g_base + RVA_SPRITE_MGR);
        ID3D11DeviceContext *ctx = *(ID3D11DeviceContext **)(g_base + RVA_D3D_CTX);
        LONG cell = g_so_want;
        for (int i = 0; i < g_so_img_n; i++) {
            SoImage *im = &g_so_img[i];
            unsigned char *obj = so_object(menu, mgr, im);
            if (!obj) { im->obj = NULL; im->mine = NULL; continue; }
            if (obj != im->obj || *(ID3D11Texture2D **)(obj + 0x20) != im->mine)
                if (!so_take(im, obj)) { im->obj = obj; im->mine = NULL; continue; }
            if (!im->mine || !ctx || cell < 0 || cell == im->last) continue;
            int x0 = (cell % g_so_cols) * g_so_cw, y0 = (cell / g_so_cols) * g_so_ch;
            D3D11_BOX box = {0, 0, 0, (UINT)g_so_cw, (UINT)g_so_ch, 1};
            ctx->UpdateSubresource(im->mine, 0, &box, g_so_atlas + (size_t)(y0 / 4) * (g_so_aw / 4) * 8 + (x0 / 4) * 8, g_so_aw / 4 * 8, 0);
            im->last = cell;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
