// ---- v5.67 HUD faces (owner 2026-11-02; "the character's face will be literally to the left of the character's name ... to the
// left of the name and the health bar"; AGENTS.md "Scoreboard line-ups and HUD faces": display only, this machine only,
// Xbox Live / scoreboard matches too).
// Game facts (PD Ghidra full pass 065 via pd_fn.py + the clean exe's tables, MODLOG 2026-11-02):
//   The battle HUD (FUN_140150c30) draws every fighter's panel (name, life box, capsules, status icons) with
//   FUN_140155310(hud, panel index, player, ?, flag, x, y, ?) - the one call at 0x1401516c8; x, y = row 0 of the panel
//   table 0x1402f1250 (HUD presets), the panel's own y offset = hud + 0x2a4 (reset for panel 0, +0x1c / +0x29 / +0x38 per
//   panel); its name is drawn 14 px above y. Every HUD picture is queued with FUN_1401983e0(.., handle, x, y, 0, x scale*,
//   y scale*, colour*, 0, 0) from the handle buffer *(hud + 0x268) (4 bytes a sprite, in the image's record order) and
//   drawn with the panels by FUN_140198be0. The image is Menu\BtMenuEn.alr (3 DXT5 textures, 180 sprites). Status icons
//   (FUN_140157860): FUN_14024d4d0(battle data *(0x1404bc608 + 8p), n) = the sum of status n; table 0x1402f1200
//   {0 power, 1 speed, 3 recovery, 2 target} (> 0 up, < 0 down) and 0x1402f1298 {4 stop, 5 freeze, 6 poison, 33 special,
//   35 reverse, 36 confusion}; flag 8 = can't be locked on (cwo_mirror.inl). Fighter *(0x1404bc9d8 + 8p): +0x4c life,
//   +0x50 its cap, +0x12f04 state (0x1B down). Record 0x1404bc420 + 0x48 p: +8 = outfit << 16 | charaset row; charaset
//   image *0x140590190 rows: +0 the model stem, +4 the shown name (pool offsets).
// What this does: BtMenuEn.alr carries one more texture (256 x 64 DXT5: 4 cells of 64 x 64, one per panel) and 4 sprites
// on it (tools/build_hud_faces.py); the faces are the loader's own file Menu\cwo_faces.dxt (64 x 64 DXT5 cells, one per
// character and state, order in Menu\cwo_faces.txt). The panel call goes through hf_panel: it picks the face for that
// panel's fighter, puts it into the panel's cell of the loader's DEFAULT-usage copy of the texture (UpdateSubresource,
// only when it changes), queues the cell's sprite left of the name and life box, and draws the panel moved right by the
// face's width. Nothing in the game's data is written.
static const ULONG_PTR RVA_HF_CALL = 0x1516c8, RVA_HF_PANEL = 0x155310, RVA_HF_SPRITE = 0x1983e0, RVA_HF_STATUS = 0x24d4d0,
                       RVA_HF_BD = 0x4bc608, RVA_HF_FIGHTERS = 0x4bc9d8, RVA_HF_RECS = 0x4bc420, RVA_HF_CHARA = 0x590190;
static const int HF_CELL = 64, HF_CELLS = 6,   // v5.181: 6 cells (4 + 2 for fighters 5-6 of a 3 vs 3: a second row under the first)
    HF_TEX_W = 256, HF_TEX_H = 64, HF_CELL_BYTES = HF_CELL / 4 * HF_CELL / 4 * 16;
typedef void(__fastcall *PFN_HF_PANEL)(INT64, int, unsigned, int, int, int, int, unsigned);
typedef INT64(__fastcall *PFN_HF_SPRITE)(INT64, unsigned, int, int, unsigned, const float *, const float *, const unsigned *, const int *, const float *);
typedef int(__fastcall *PFN_HF_STATUS)(INT64, int);
static PFN_HF_PANEL g_hf_orig;
static const int SP_PANEL_DY = 46;                                // v5.107 split screen: one panel's height in 640 x 480 units
static int g_hf_state = 0;                                       // 1 hooked, -1 off
static char g_hf_stems[64][12];
static char g_hf_states[32][28];
static int g_hf_nstem, g_hf_nstate, g_hf_tex = -1, g_hf_base = -1;
static int g_hf_tw = 256, g_hf_th = 64, g_hf_cx, g_hf_cy;               // the host texture and the cells' origin (#cells; v5.67.10:
                                                                    // a stock page - an added page is not drawn in battle)
static unsigned g_hf_h0, g_hf_h1;
static unsigned char *g_hf_data;                                 // g_hf_nstem * g_hf_nstate cells
static void *g_hf_obj;
static ID3D11Texture2D *g_hf_mine;
static int g_hf_cell_now[HF_CELLS];                              // the face index in each cell (-1 none)
// placement (HUD units, 640 x 480 stretched to the window): the panel's art starts ~50 px right of its x, so the panels move
// right by g_hf_shift and the face sits at x + shift + dx; x scale < y scale keeps the face square on a 16:10 / 16:9 screen
static int g_hf_dx = -31, g_hf_dy = -14, g_hf_shift = 8;          // CwoFaces.txt "dx dy xscale yscale shift" (read each second)
static float g_hf_sx = 0.55f, g_hf_sy = 0.62f;
static volatile LONG g_hf_want[HF_CELLS] = {-1, -1, -1, -1, -1, -1};      // the face each cell should show (set by the HUD, uploaded by the render hook)
static int g_hf_test_sprite = -1;                                 // CwoFaces.txt 6th value: draw that stock HUD sprite instead (test)
static int g_hf_last_life[8];
static ULONGLONG g_hf_hit_until[8];                                // real time (ms): the panel is drawn once per fighter a frame
static unsigned g_hf_frame;

static char g_hf_row_stem[256][12];
static int g_hf_rows_cached;
static int hf_state_index(const char *name) {
    for (int i = 0; i < g_hf_nstate; i++) if (!strcmp(g_hf_states[i], name)) return i;
    return -1;
}
static void hf_load() {                                          // worker start
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\');
    if (sl) sl[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Menu\\cwo_faces.txt", exe);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) { logf("HUD faces: no Menu\\cwo_faces.txt - off"); return; }
    char line[600];
    while (fgets(line, sizeof(line), f)) {
        unsigned a, b; int t, s;
        if (sscanf_s(line, "#texture\t%d\t%d\t%u\t%u", &t, &s, &a, &b) == 4) { g_hf_tex = t; g_hf_base = s; g_hf_h0 = a; g_hf_h1 = b; continue; }
        int cw, ch, cx, cy;
        if (sscanf_s(line, "#cells\t%d\t%d\t%d\t%d", &cw, &ch, &cx, &cy) == 4 && cw >= 64 && cw <= 4096 && ch >= 64 && ch <= 4096 &&
            cx >= 0 && cy >= 0 && cx + HF_CELL * 4 <= cw && cy + HF_CELL * 2 <= ch && !(cx & 3) && !(cy & 3)) { g_hf_tw = cw; g_hf_th = ch; g_hf_cx = cx; g_hf_cy = cy; continue; }
        char *ctx = NULL, *tok;
        if (!strncmp(line, "#stems\t", 7)) {
            for (tok = strtok_s(line + 7, "\t\r\n", &ctx); tok && g_hf_nstem < 64; tok = strtok_s(NULL, "\t\r\n", &ctx)) strncpy_s(g_hf_stems[g_hf_nstem++], tok, _TRUNCATE);
        } else if (!strncmp(line, "#states\t", 8)) {
            for (tok = strtok_s(line + 8, "\t\r\n", &ctx); tok && g_hf_nstate < 32; tok = strtok_s(NULL, "\t\r\n", &ctx)) strncpy_s(g_hf_states[g_hf_nstate++], tok, _TRUNCATE);
        }
    }
    fclose(f);
    if (g_hf_tex < 0 || g_hf_base < 0 || g_hf_nstem < 1 || g_hf_nstate < 1 || hf_state_index("health_20_full") < 0) {
        logf("HUD faces: Menu\\cwo_faces.txt incomplete (texture %d, sprite %d, %d characters, %d states) - off", g_hf_tex, g_hf_base, g_hf_nstem, g_hf_nstate);
        g_hf_nstem = 0; return;
    }
    size_t need = (size_t)g_hf_nstem * g_hf_nstate * HF_CELL_BYTES;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Menu\\cwo_faces.dxt", exe);
    FILE *df = NULL;
    if (cwo_wfopen(&df, path, L"rb") != 0 || !df) { logf("HUD faces: no Menu\\cwo_faces.dxt - off"); g_hf_nstem = 0; return; }
    g_hf_data = (unsigned char *)malloc(need);
    size_t got = g_hf_data ? fread(g_hf_data, 1, need, df) : 0;
    fclose(df);
    if (got != need) { logf("HUD faces: Menu\\cwo_faces.dxt is %zu bytes, not %zu - off", got, need); free(g_hf_data); g_hf_data = NULL; g_hf_nstem = 0; return; }
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\setup\\charaset\\charaset_en.dat", exe);
    FILE *cf = NULL;
    if (cwo_wfopen(&cf, path, L"rb") == 0 && cf) {
        static unsigned char cb[0x20000];
        size_t n = fread(cb, 1, sizeof(cb), cf);
        fclose(cf);
        int rows = n >= 16 ? *(int *)(cb + 4) : 0, pool = n >= 16 ? *(int *)(cb + 8) : 0, rs = rows > 0 ? (pool - 16) / rows : 0;
        if (rows > 0 && rows <= 256 && rs >= 0x3C && rs <= 0x100 && (size_t)pool < n) {
            for (int r = 0; r < rows; r++) {
                int o = *(int *)(cb + 16 + rs * r);
                if (o < 0 || (size_t)(pool + o) >= n) continue;
                const char *stem = (const char *)cb + pool + o;
                for (int k = 0; k < 11 && (size_t)(pool + o + k) < n && stem[k] > 0x20 && stem[k] < 0x7f; k++) g_hf_row_stem[r][k] = stem[k];
            }
            g_hf_rows_cached = rows;
            logf("HUD faces: %d character rows from charaset_en.dat (row 0 '%s', 1 '%s', 23 '%s')", rows, g_hf_row_stem[0], g_hf_row_stem[1], g_hf_row_stem[23]);
        } else logf("HUD faces: charaset_en.dat not as expected (%zu bytes, %d rows, pool %d)", n, rows, pool);
    } else logf("HUD faces: charaset_en.dat not readable - faces only from the menus' copy");
    for (int k = 0; k < HF_CELLS; k++) g_hf_cell_now[k] = -1;
    logf("HUD faces: %d characters x %d states (%zu bytes), texture %d, sprites %d-%d", g_hf_nstem, g_hf_nstate, need, g_hf_tex, g_hf_base, g_hf_base + HF_CELLS - 1);
}

static void hf_placement() {                                     // CwoFaces.txt, once a second (game thread)
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 1000;
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%sCwoFaces.txt", g_dir);
    FILE *pf = NULL;
    if (cwo_wfopen(&pf, path, L"r") != 0 || !pf) return;
    int dx, dy, sh, ts = -1; float sx, sy;
    int got = fscanf_s(pf, "%d %d %f %f %d %d", &dx, &dy, &sx, &sy, &sh, &ts);
    if (got >= 5 && ts != g_hf_test_sprite) { g_hf_test_sprite = ts; logf("HUD faces: test sprite %d", ts); }
    if (got >= 5 && sx > 0.1f && sx < 4.0f && sy > 0.1f && sy < 4.0f &&
        (dx != g_hf_dx || dy != g_hf_dy || sx != g_hf_sx || sy != g_hf_sy || sh != g_hf_shift)) {
        g_hf_dx = dx; g_hf_dy = dy; g_hf_sx = sx; g_hf_sy = sy; g_hf_shift = sh;
        logf("HUD faces: placement from CwoFaces.txt: face at panel x %+d (after the %d px move), y %+d, scale %.2f x %.2f", dx, sh, dy, sx, sy);
    }
    fclose(pf);
}

static void hf_why(int p, const char *what, long long v) {       // once per fighter and reason: why no face
    static unsigned done[8];
    unsigned bit = 1u << ((unsigned)(what[0] + what[1]) & 31);
    if (p < 0 || p >= 8 || (done[p] & bit)) return;
    done[p] |= bit;
    logf("HUD faces: fighter %d: %s (%lld) - no face", p, what, v);
}
// the character table: read from the game's own setup\charaset\charaset_en.dat at start (hf_load; its header +4 rows, +8
// the string pool, rows from +16, row +0 the model stem); the in-memory image (*0x140590190) as a second source while readable
static void hf_cache_step() {                                    // CwoOverlayFrame, once a second (menus and battles)
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (now < next || !g_hf_data) return;
    next = now + 1000;
    __try {
        char *b = *(char **)(g_base + RVA_HF_CHARA);
        if (!b) return;
        int rows = *(int *)(b + 4), pool = *(int *)(b + 8), rs = rows > 0 ? (pool - 16) / rows : 0;
        if (rows <= 0 || rows > 256 || rs < 0x3C || rs > 0x100) return;
        for (int r = 0; r < rows; r++) {
            const char *stem = b + pool + *(int *)(b + 16 + rs * r);
            char t[12] = {0};
            for (int k = 0; k < 11 && stem[k] > 0x20 && stem[k] < 0x7f; k++) t[k] = stem[k];
            memcpy(g_hf_row_stem[r], t, sizeof(t));
        }
        if (rows != g_hf_rows_cached) {
            g_hf_rows_cached = rows;
            logf("HUD faces: %d character rows copied (row 0 '%s', 1 '%s', 3 '%s')", rows, g_hf_row_stem[0], g_hf_row_stem[1], g_hf_row_stem[3]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static int hf_stem_of(int p) {                                    // the pack index of fighter p's model (-1 none)
    __try {
        unsigned w = *(unsigned *)(cwo_gaddr(RVA_HF_RECS) + 0x48 * (ULONG_PTR)p + 8);   // v5.179.4: the moved table when six fighters are on (scoreboard faces were all fighter 0)
        int row = (int)(w & 0xFFFF);
        if (row < 0 || row >= 256 || !g_hf_row_stem[row][0]) { hf_why(p, "character row not copied from the menus", row); return -1; }
        for (int i = 0; i < g_hf_nstem; i++) if (!_stricmp(g_hf_stems[i], g_hf_row_stem[row])) return i;
        static unsigned logged;
        if (!(logged & (1u << (p & 31)))) { logged |= 1u << (p & 31); logf("HUD faces: fighter %d: model '%s' (row %d) has no face set", p, g_hf_row_stem[row], row); }
    } __except (EXCEPTION_EXECUTE_HANDLER) { hf_why(p, "exception reading the fighter record", 0); }
    return -1;
}

static const char *hf_pick(int p) {                               // the face's state for fighter p (game thread)
    ULONG_PTR a = 0, bd = 0;
    __try { a = p < cwo_nfighters() ? cwo_actors()[p] : 0; bd = p < cwo_nfighters() ? cwo_bdata()[p] : 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}   // v5.146 moved tables (six fighters)
    if (!a || !bd) return "health_20_full";
    int life = 20, cap = 20, state = 0;
    __try { life = *(int *)(a + 0x4c); cap = *(int *)(a + 0x50); state = *(int *)(a + 0x12f04); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (life < 1) return "health_0_ko";
    if (p >= 0 && p < 8) {                                       // a hit: life went down -> the hit face for half a second
        ULONGLONG now = GetTickCount64();
        if (life < g_hf_last_life[p]) g_hf_hit_until[p] = now + 700;   // the hit face for 0.7 s
        g_hf_last_life[p] = life;
        if (now < g_hf_hit_until[p]) return "damaged";
    }
    PFN_HF_STATUS st = (PFN_HF_STATUS)(g_base + RVA_HF_STATUS);
    auto has = [&](int n) { int v = 0; __try { v = st((INT64)bd, n); } __except (EXCEPTION_EXECUTE_HANDLER) {} return v; };
    if (has(8) > 0) return "invisible";
    if (has(5) > 0) return "frozen_hand";
    if (has(4) > 0) return "paralysis";
    if (has(6) > 0) return "poison";
    if (has(35) > 0 || has(36) > 0) return "inverted_controls";
    if (state == 0x1B) return "knocked_up";
    if (has(0) > 0) return "strength_up";
    if (has(1) > 0) return "speed_up";
    if (has(3) > 0) return "aura_recovery";
    int l20 = cap > 0 ? (life * 20 + cap - 1) / cap : life;     // the owner's steps are for a life of 20
    if (l20 >= 11) return "health_20_full";
    if (l20 >= 6) return "health_10_hurt";
    if (l20 >= 4) return "health_5_pretty_hurt";
    if (l20 >= 2) return "health_3_seriously_hurt";
    return (GetTickCount64() / 500) & 1 ? "low_health" : "health_1_critical";   // the low-life pulse, twice a second
}

// our texture's object in the HUD image (its set from our sprite's handle), else NULL
static unsigned char *hf_object(ULONG_PTR mgr, unsigned h) {
    int set = (h >> 16) & 0xff;
    const unsigned char *alr = *(const unsigned char **)(mgr + 0x308 + set * 8);
    if (!alr || *(const unsigned *)alr != 0x11) return NULL;
    const unsigned char *ch = alr + *(const unsigned *)(alr + 4);
    if (*(const unsigned *)ch != 0x15) return NULL;
    int n = *(const unsigned short *)(ch + 8);
    for (int i = 0; i < n && i < 256; i++) {
        const unsigned char *e = ch + 0xc + 28 * i;
        if (*(const unsigned *)(e + 0x14) != g_hf_h0 || *(const unsigned *)(e + 0x18) != g_hf_h1) continue;
        if (*(const int *)e != -1) return NULL;
        unsigned char *obj = *(unsigned char **)(e + 4);
        if (!obj || *(const unsigned char **)(obj + 0x18) != e) return NULL;
        unsigned packed = *(const unsigned *)(obj + 0x10);
        if ((1 << ((packed >> 20) & 15)) != g_hf_tw || (1 << ((packed >> 24) & 15)) != g_hf_th || ((packed >> 8) & 0xff) != 0x0f) return NULL;
        return obj;
    }
    return NULL;
}
static bool hf_take(unsigned char *obj) {                         // our DEFAULT-usage copy in place of the immutable one
    ID3D11Device *dev = *(ID3D11Device **)(g_base + RVA_D3D_DEVICE);
    const unsigned char *pixels = *(const unsigned char **)(obj + 8);
    if (!dev || !pixels) return false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = g_hf_tw; td.Height = g_hf_th; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_BC3_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = pixels; init.SysMemPitch = g_hf_tw / 4 * 16;
    ID3D11Texture2D *tex = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    if (FAILED(dev->CreateTexture2D(&td, &init, &tex)) || !tex) { logf("HUD faces: CreateTexture2D failed"); return false; }
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = td.Format; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(tex, &vd, &srv)) || !srv) { tex->Release(); logf("HUD faces: CreateShaderResourceView failed"); return false; }
    IUnknown *old_tex = *(IUnknown **)(obj + 0x20), *old_srv = *(IUnknown **)(obj + 0x28);
    *(ID3D11Texture2D **)(obj + 0x20) = tex;
    *(ID3D11ShaderResourceView **)(obj + 0x28) = srv;
    if (old_srv) old_srv->Release();
    if (old_tex) old_tex->Release();
    g_hf_obj = obj; g_hf_mine = tex;
    for (int k = 0; k < HF_CELLS; k++) g_hf_cell_now[k] = -1;
    logf("HUD faces: texture object %p: own copy %p in place", obj, tex);
    return true;
}

// v5.109 (owner 2026-11-07): <data>\CwoFacesOff.txt present = no faces (this machine only; display
// only). Switched by the lobby's BFR Options row "HUD Faces" (cwo_bfrnet.inl, op 89 value 205).
static int g_hf_show = -1;
static bool hf_show() {
    if (g_hf_show < 0) {
        wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoFacesOff.txt", g_dir);
        g_hf_show = GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES ? 1 : 0;
    }
    return g_hf_show == 1;
}
static void hf_set_show(bool on) {
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoFacesOff.txt", g_dir);
    if (on) DeleteFileW(p); else { FILE *f = NULL; if (_wfopen_s(&f, p, L"w") == 0 && f) { fputs("off", f); fclose(f); } }
    g_hf_show = on ? 1 : 0;
    logf("HUD faces: %s (lobby BFR Options)", on ? "shown" : "hidden");
}
static void __fastcall hf_panel(INT64 hud, int idx, unsigned player, int p4, int flag, int x, int y, unsigned p8) {
    int shift = 0;
    __try {
        // v5.68 split screen too (hud + 0x2a0 != 0: FUN_140150c30 draws one panel a half, panel index = the half, rows 1 / 2 of
        // the panel table, and FUN_140155310 resets the stack offset hud + 0x2a4 for every split panel)
        bool split = *(int *)(hud + 0x2a0) != 0;
        if (hf_show() && g_hf_data && hud && idx >= 0 && idx < HF_CELLS && player < 8) {
            g_hf_frame++;
            hf_placement();
            const unsigned *handles = *(const unsigned **)(hud + 0x268);
            unsigned h = handles ? handles[g_hf_base + idx] : 0;
            if (!(handles && (int)(h >> 24) == g_hf_base + idx && (int)((h >> 8) & 0xff) == g_hf_tex)) hf_why((int)player, "sprite handle not ours", h);
            else {
                ULONG_PTR mgr = *(ULONG_PTR *)(g_base + RVA_SPRITE_MGR);
                unsigned char *obj = mgr ? hf_object(mgr, h) : NULL;
                if (obj && (obj != g_hf_obj || *(ID3D11Texture2D **)(obj + 0x20) != g_hf_mine)) { if (!hf_take(obj)) { g_hf_obj = obj; g_hf_mine = NULL; } }
                int stem = hf_stem_of((int)player);
                if (!obj) hf_why((int)player, "texture object not found", 0);
                else if (!g_hf_mine) hf_why((int)player, "no own texture copy", 0);
                if (obj && g_hf_mine && stem >= 0) {
                    int s = hf_state_index(hf_pick((int)player));
                    if (s < 0) s = hf_state_index("health_20_full");
                    int face = stem * g_hf_nstate + s;
                    static int last_s[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
                    if (s != last_s[player]) {                                // each change of a fighter's face, for the log
                        last_s[player] = s;
                        int lf = 0; __try { ULONG_PTR fa = player >= 0 && player < cwo_nfighters() ? cwo_actors()[player] : 0; lf = fa ? *(int *)(fa + 0x4c) : -1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
                        logf("HUD faces: fighter %u (%s) -> %s (life %d)", player, g_hf_stems[stem], g_hf_states[s], lf);
                    }
                    InterlockedExchange(&g_hf_want[idx], face);           // the render hook (hf_upload_step) puts it into the cell
                    int oy = idx == 0 || split ? 0 : *(int *)(hud + 0x2a4);
                    float sx = g_hf_sx, sy = g_hf_sy;
                    unsigned colour = 0xffffffffu;                         // opaque white (the game's icons pass their colour too)
                    int fx = x + g_hf_shift + g_hf_dx, fy = y + oy + g_hf_dy;
                    unsigned hd = g_hf_test_sprite >= 0 && g_hf_test_sprite < g_hf_base ? handles[g_hf_test_sprite] : h;
                    ((PFN_HF_SPRITE)(g_base + RVA_HF_SPRITE))(0, hd, fx, fy, 0, &sx, &sy, &colour, NULL, NULL);
                    static unsigned drawn_log;
                    if (!drawn_log) {                                       // the draw objects of texture 0 (stock) and ours
                        int set = (h >> 16) & 0xff;
                        ULONG_PTR *dobj = *(ULONG_PTR **)(mgr + 0x358 + set * 8);
                        for (int t = 0; dobj && t <= g_hf_tex; t += g_hf_tex) {
                            ULONG_PTR d = dobj[t];
                            logf("HUD faces: set %d texture %d draw object %p: +0x28 %p, +0x30 %d x %d, +0x38 %08x (our texture object %p, its view %p)",
                                 set, t, (void *)d, d ? *(void **)(d + 0x28) : NULL, d ? *(int *)(d + 0x30) : 0, d ? *(int *)(d + 0x34) : 0,
                                 d ? *(unsigned *)(d + 0x38) : 0, obj, *(void **)(obj + 0x28));
                        }
                    }
                    if (!(drawn_log & (1u << idx))) { drawn_log |= 1u << idx; logf("HUD faces: panel %d fighter %u: face %d (%s %s) at %d, %d (panel x %d y %d +%d), handle %08x", idx, player, face, g_hf_stems[stem], g_hf_states[s], fx, fy, x, y, oy, h); }
                    shift = g_hf_shift;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { shift = 0; }
    g_hf_orig(hud, idx, player, p4, flag, x + shift, y, p8);
    // v5.107 every fighter's own panel in split screen (owner 2026-11-04; "I do not want to see these low budget, low tier looking health name display"):
    // FUN_140150c30 draws one panel a half (its own player); after it the other fighters' panels - the game's own panel
    // (FUN_140155310: name, life bar, hand, deck, aura) with their faces - stack under it. Each fighter keeps its own slot
    // (slot = fighter: the life bar's drain state +0x2ac / +0x2bc + slot * 4 stays that fighter's), p8 = the half's locked
    // target (that panel shows the lock as the game's). Offline only; split panels reset HUD +0x2a4, so y is given.
    static bool in_extra;
    __try {
        // v5.109 (owner 2026-11-07): off by default - each half shows only its own player's
        // panel, as the game does; CwoSplitPanels.txt "all" brings the 5.107 stack back
        static int sp_all = -1;
        if (sp_all < 0) {
            sp_all = 0;
            wchar_t pth[MAX_PATH]; swprintf_s(pth, MAX_PATH, L"%sCwoSplitPanels.txt", g_dir);
            FILE *f = NULL; char w[16] = "";
            if (_wfopen_s(&f, pth, L"r") == 0 && f) { if (fgets(w, sizeof(w), f) && !strncmp(w, "all", 3)) sp_all = 1; fclose(f); }
            logf("split screen panels: %s", sp_all ? "every fighter in each half (CwoSplitPanels.txt all)" : "each half its own player only");
        }
        if (sp_all && !in_extra && *(int *)(hud + 0x2a0) != 0 && !ed_online() && player < 4) {
            in_extra = true;
            ULONG_PTR *bd = cwo_bdata();
            int n = 0;
            for (int k = 0; k < 4; k++) {
                if (k == (int)player || !bd[k] || !*(int *)bd[k]) continue;
                n++;
                hf_panel(hud, k, (unsigned)k, k, 0, x, y + SP_PANEL_DY * n, p8);
            }
            in_extra = false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { in_extra = false; }
}

static void hf_upload_step() {                                    // CwoOverlayFrame (the render thread, as portrait_step)
    if (!g_hf_data || !g_hf_mine) return;
    __try {
        ID3D11DeviceContext *ctx = *(ID3D11DeviceContext **)(g_base + RVA_D3D_CTX);
        if (!ctx || !g_hf_obj || *(ID3D11Texture2D **)((unsigned char *)g_hf_obj + 0x20) != g_hf_mine) return;
        for (int k = 0; k < HF_CELLS; k++) {
            LONG face = g_hf_want[k];
            if (face < 0 || face >= g_hf_nstem * g_hf_nstate || face == g_hf_cell_now[k]) continue;
            UINT bx = (UINT)(g_hf_cx + (k % 4) * HF_CELL), by = (UINT)(g_hf_cy + (k / 4) * HF_CELL);   // v5.181 cells 4-5 under 0-1
            D3D11_BOX box = {bx, by, 0, bx + HF_CELL, by + HF_CELL, 1};
            ctx->UpdateSubresource(g_hf_mine, 0, &box, g_hf_data + (size_t)face * HF_CELL_BYTES, HF_CELL / 4 * 16, 0);
            g_hf_cell_now[k] = face;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void hf_install() {                                        // worker start: the panel call -> hf_panel
    if (!g_hf_data) return;
    unsigned char *site = (unsigned char *)(g_base + RVA_HF_CALL);
    if (site[0] != 0xE8 || (ULONG_PTR)(site + 5 + *(int *)(site + 1)) != g_base + RVA_HF_PANEL) {
        logf("HUD faces: the panel call at %p is not the game's - off", (void *)site);
        g_hf_state = -1; return;
    }
    ULONG_PTR stub = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!stub) { g_hf_state = -1; return; }
    unsigned char *c = (unsigned char *)stub;
    c[0] = 0x48; c[1] = 0xB8; *(ULONGLONG *)(c + 2) = (ULONGLONG)(ULONG_PTR)hf_panel; c[10] = 0xFF; c[11] = 0xE0;
    FlushInstructionCache(GetCurrentProcess(), c, 12);
    g_hf_orig = (PFN_HF_PANEL)(g_base + RVA_HF_PANEL);
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) { g_hf_state = -1; return; }
    *(int *)(site + 1) = (int)(stub - (ULONG_PTR)(site + 5));
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    g_hf_state = 1;
    logf("HUD faces: the panel call hooked (stub %p)", (void *)stub);
}

static bool hf_ready() { return g_hf_orig != NULL; }            // v5.181 the six-fighter HUD (cwo_sixhud.inl) draws through hf_panel

static int hf_self_test() {                                       // the texture lookup and the state names on stand-ins
    static unsigned char mgr[0x400], alr[0x100], obj[0x60];
    memset(mgr, 0, sizeof(mgr)); memset(alr, 0, sizeof(alr)); memset(obj, 0, sizeof(obj));
    unsigned sh0 = g_hf_h0, sh1 = g_hf_h1;
    g_hf_h0 = 0x4de8b79f; g_hf_h1 = 0x5f40ca69;
    *(unsigned char **)(mgr + 0x308 + 5 * 8) = alr;
    *(unsigned *)alr = 0x11; *(unsigned *)(alr + 4) = 0x20;
    unsigned char *ch = alr + 0x20;
    *(unsigned *)ch = 0x15; *(unsigned short *)(ch + 8) = 4;
    unsigned char *e = ch + 0xc + 28 * 3;
    *(int *)e = -1; *(unsigned char **)(e + 4) = obj; *(unsigned *)(e + 0x14) = g_hf_h0; *(unsigned *)(e + 0x18) = g_hf_h1;
    *(unsigned char **)(obj + 0x18) = e;
    int stw = g_hf_tw, sth = g_hf_th; g_hf_tw = 256; g_hf_th = 64;
    *(unsigned *)(obj + 0x10) = (6u << 24) | (8u << 20) | (1u << 16) | (0x0fu << 8) | 0x29;   // 256 x 64, 1 mip, DXT5
    unsigned h = 183u << 24 | 5u << 16 | 3u << 8 | 3;
    bool found = hf_object((ULONG_PTR)mgr, h) == obj;
    *(unsigned *)(obj + 0x10) = (6u << 24) | (8u << 20) | (1u << 16) | (0x0cu << 8) | 0x29;   // DXT1: not ours
    bool fmt = hf_object((ULONG_PTR)mgr, h) == NULL;
    g_hf_h0 = sh0; g_hf_h1 = sh1; g_hf_tw = stw; g_hf_th = sth;
    logf("self-test: HUD faces texture lookup %s, wrong format skipped %s", found ? "ok" : "FAILED", fmt ? "ok" : "FAILED");
    return found && fmt ? 1 : -79;
}
