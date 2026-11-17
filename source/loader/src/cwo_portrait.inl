// ---- v5.40 - v5.42 stage portraits in the stage lists (owner 2026-10-31: the highlighted-stage portrait must show
// THAT stage; "I want the level list pictures finished. They should have that gridded line like the original"; after the
// 22:04 in-game failure: "Fix it. I want those pictures added like you said").
// Game facts (PD Ghidra full pass 065 + capstone on PDUWP 9ba7411b, MODLOG 2026-10-31):
//   vsmenu SSEL draws the stage picture with 2.82(slot 0, table_0x1f38[row], 48, 261) (image VsMode); livestagesel
//   STSEL with 2.82(slot 3, table_0x1328[row], 48, 261) (image MNet). The image manager (*0x140527c78): +0x308 + set * 8
//   = the loaded .alr bytes (menu pool 0, 2.57 MB in all: a big file never loads), +0x128 + set * 0x30 / +0x12c = its
//   texture / sprite counts; the menu (*0x140533c58) image slot s: +0x10530 + s * 0x20 present, +0x10548 + s * 0x20 the
//   sprite handles (index << 24 | set << 16 | texture << 8 | index on the texture). An .alr's texture table (chunk 0x15 at
//   file + u32[1]: u16 count at +8, 28-byte entries from +0xc {i32 flag, pixels / object (8), packed, x, hash0, hash1});
//   FUN_14029a050 -> FUN_14001de70 creates each texture: entry flag = -1 and entry + 4 = a 0x60 object {+8 pixels,
//   +0x10 packed, +0x18 entry, +0x20 ID3D11Texture2D (IMMUTABLE, FUN_1400bcac0: usage 1, BC1 for format 0x0c, mips from
//   the packed word), +0x28 its shader resource view (FUN_14001d340: device->CreateShaderResourceView, view dimension
//   TEXTURE2D, all mips)}; the D3D11 device is *0x1404baff0, the immediate context *0x1404bad40.
// What this does: the menu files carry one small texture (256 x 1024 DXT1: 8 Map Slot pictures, one per 112-row cell)
// and 8 sprites on it (tools/build_stage_portraits.py); the full picture atlas is the loader's own file
// Map\mx_portraits.dxt (raw DXT1 2048 x 2048, cells of 256 x 112). Each frame, for each image named in
// Map\mx_portraits.txt whose menu slot holds it: the loader finds our texture's entry by name hash; the first time it
// sees that texture object it creates its own DEFAULT-usage copy (same pixels) and its view, and puts them in the
// object in place of the game's (released); when the pack Map Slot 7 loads changes, the Map Slot 7 cell (row 0) of the
// copy is overwritten with that pack's atlas cell (UpdateSubresource). Online only the Xbox Live LEVEL LIST image is
// touched (AGENTS.md "Level list pictures online"); nothing else is written.
#include <d3d11.h>
static const ULONG_PTR RVA_MENU_MGR = 0x533c58, RVA_SPRITE_MGR = 0x527c78, RVA_D3D_DEVICE = 0x4baff0, RVA_D3D_CTX = 0x4bad40;
static const int PT_W = 254, PT_H = 112, PT_IMAGES = 4, PT_SLOTS = 8, PT_SMALL_W = 256, PT_SMALL_H = 1024;
struct PtRow { char file[16]; int cell; };
struct PtImage { int slot, textures, sprites, tex, sprite, row; unsigned h0, h1; void *obj; ID3D11Texture2D *mine; int last; int table[30], table_n; };
static PtRow g_pt[512];
static PtImage g_pt_img[PT_IMAGES];
static int g_pt_n, g_pt_img_n, g_pt_sprite = -1, g_pt_default = -1;
static int g_pt_aw, g_pt_ah, g_pt_cw, g_pt_ch, g_pt_cols;          // the atlas: size, cell stride, cells per row
static unsigned char *g_pt_atlas;                                   // raw DXT1, g_pt_aw / 4 * 8 bytes per block row

static void portrait_add_image(int slot, int textures, int sprites, int tex, int sprite, unsigned h0, unsigned h1, int row) {
    if (g_pt_img_n >= PT_IMAGES || slot < 0 || slot >= PT_SLOTS || textures < 1 || textures > 255 || sprites < 1 ||
        sprites > 128 || tex < 0 || tex >= textures || sprite < 0 || sprite >= sprites || row < 0 || (row + 1) * PT_H > PT_SMALL_H) {
        logf("stage portraits: image line refused (slot %d, %d textures, %d sprites, texture %d, sprite %d, row %d)", slot, textures, sprites, tex, sprite, row);
        return;
    }
    PtImage *im = &g_pt_img[g_pt_img_n++];
    memset(im, 0, sizeof(*im));
    im->slot = slot; im->textures = textures; im->sprites = sprites; im->tex = tex; im->sprite = sprite; im->row = row;
    im->h0 = h0; im->h1 = h1; im->last = -2;
}

static void portrait_load() {                                  // worker start, after mx_load_list
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\');
    if (sl) sl[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Map\\mx_portraits.txt", exe);
    FILE *f = NULL;
    g_pt_n = g_pt_img_n = 0;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) { logf("stage portraits: no Map\\mx_portraits.txt - the stage lists keep their pictures"); return; }
    char line[400], key[32];
    int v, a, b, c, d, e, r;
    unsigned h0, h1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf_s(line, "#image\t%d\t%d\t%d\t%d\t%d\t%u\t%u\t%d", &a, &b, &c, &d, &e, &h0, &h1, &r) == 8) { portrait_add_image(a, b, c, d, e, h0, h1, r); continue; }
        if (sscanf_s(line, "#atlas\t%d\t%d\t%d\t%d\t%d", &a, &b, &c, &d, &e) == 5) { g_pt_aw = a; g_pt_ah = b; g_pt_cw = c; g_pt_ch = d; g_pt_cols = e; continue; }
        if (!strncmp(line, "#table\t", 7) && g_pt_img_n > 0) {   // v5.43 "#table<TAB>slot<TAB>v0 .. v29": the image's stock picture table (op 76)
            PtImage *im = NULL;
            char *ctx = NULL, *t = strtok_s(line + 7, "\t\r\n", &ctx);
            int sl = t ? atoi(t) : -1;
            for (int i = 0; i < g_pt_img_n; i++) if (g_pt_img[i].slot == sl) im = &g_pt_img[i];
            if (im) { im->table_n = 0; while ((t = strtok_s(NULL, "\t\r\n", &ctx)) && im->table_n < 30) im->table[im->table_n++] = atoi(t); }
            continue;
        }
        if (sscanf_s(line, "%31[^\t]\t%d", key, (unsigned)sizeof(key), &v) != 2) continue;
        if (!strcmp(key, "#sprite")) g_pt_sprite = v;
        else if (!strcmp(key, "#default")) g_pt_default = v;
        else if (key[0] != '#' && g_pt_n < 512) { strncpy_s(g_pt[g_pt_n].file, key, _TRUNCATE); g_pt[g_pt_n++].cell = v; }
    }
    fclose(f);
    if (g_pt_img_n == 0) { logf("stage portraits: no #image line - nothing to do"); return; }
    if (g_pt_aw < 4 || g_pt_ah < 4 || (g_pt_aw & 3) || (g_pt_ah & 3) || g_pt_cw != PT_SMALL_W || g_pt_ch != PT_H || g_pt_cols < 1 ||
        g_pt_cols * g_pt_cw > g_pt_aw) {
        logf("stage portraits: bad #atlas line (%d x %d, cells %d x %d, %d per row) - off", g_pt_aw, g_pt_ah, g_pt_cw, g_pt_ch, g_pt_cols);
        g_pt_img_n = 0; return;
    }
    size_t need = (size_t)(g_pt_aw / 4) * (g_pt_ah / 4) * 8;
    swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Map\\mx_portraits.dxt", exe);
    FILE *af = NULL;
    if (cwo_wfopen(&af, path, L"rb") != 0 || !af) { logf("stage portraits: no Map\\mx_portraits.dxt - off"); g_pt_img_n = 0; return; }
    g_pt_atlas = (unsigned char *)malloc(need);
    size_t got = g_pt_atlas ? fread(g_pt_atlas, 1, need, af) : 0;
    fclose(af);
    if (got != need) { logf("stage portraits: Map\\mx_portraits.dxt is %zu bytes, not %zu - off", got, need); free(g_pt_atlas); g_pt_atlas = NULL; g_pt_img_n = 0; return; }
    logf("stage portraits: %d packs, %d images, own stage cell %d, atlas %d x %d DXT1 (%zu bytes)", g_pt_n, g_pt_img_n, g_pt_default, g_pt_aw, g_pt_ah, need);
    for (int i = 0; i < g_pt_img_n; i++)
        logf("stage portraits: image slot %d (%d textures, %d sprites): texture %d (hash %08x %08x) row %d = Map Slot 7's picture",
             g_pt_img[i].slot, g_pt_img[i].textures, g_pt_img[i].sprites, g_pt_img[i].tex, g_pt_img[i].h0, g_pt_img[i].h1, g_pt_img[i].row);
}

static volatile LONG g_sl_hl;                                  // v5.43 cwo_stagelist.inl
static int portrait_cell() {                                   // the cell for the highlighted pack row, else what Map Slot 7 loads now
    LONG k = g_sl_hl >= 0 ? g_sl_hl : g_mx_cur;
    if (k >= 0 && k < g_mx_count)
        for (int i = 0; i < g_pt_n; i++)
            if (!strcmp(g_pt[i].file, g_mx[k].file)) return g_pt[i].cell;
    return g_pt_default;
}

// the image's texture object (the 0x60 object FUN_14001de70 made for our texture) when the image's menu slot holds it,
// else NULL. menu = *0x140533c58, mgr = *0x140527c78.
static unsigned char *portrait_object(ULONG_PTR menu, ULONG_PTR mgr, const PtImage *im) {
    if (!menu || !mgr || !*(ULONG_PTR *)(menu + 0x10530 + im->slot * 0x20)) return NULL;
    const unsigned *handles = *(const unsigned **)(menu + 0x10548 + im->slot * 0x20);
    if (!handles) return NULL;
    unsigned h = handles[im->sprite];
    int set = (h >> 16) & 0xff, tex = (h >> 8) & 0xff;
    if ((int)(h >> 24) != im->sprite || tex != im->tex) return NULL;
    // v5.157.4 (in game 2026-11-12: later menu builds grew VsMode to 8 textures / 92 sprites and the exact totals check
    // switched every picture swap off without a word): the image must HOLD our texture / sprite; the texture's name hash
    // below is what identifies it
    if (*(int *)(mgr + 0x128 + set * 0x30) <= im->tex || *(int *)(mgr + 0x12c + set * 0x30) <= im->sprite) return NULL;
    const unsigned char *alr = *(const unsigned char **)(mgr + 0x308 + set * 8);
    if (!alr || *(const unsigned *)alr != 0x11) return NULL;
    const unsigned char *ch = alr + *(const unsigned *)(alr + 4);
    if (*(const unsigned *)ch != 0x15) return NULL;
    int n = *(const unsigned short *)(ch + 8);
    for (int i = 0; i < n && i < 256; i++) {
        const unsigned char *e = ch + 0xc + 28 * i;
        if (*(const unsigned *)(e + 0x14) != im->h0 || *(const unsigned *)(e + 0x18) != im->h1) continue;
        if (*(const int *)e != -1) return NULL;                // not created yet
        unsigned char *obj = *(unsigned char **)(e + 4);
        if (!obj || *(const unsigned char **)(obj + 0x18) != e) return NULL;
        unsigned packed = *(const unsigned *)(obj + 0x10);
        if ((1 << ((packed >> 20) & 15)) != PT_SMALL_W || (1 << ((packed >> 24) & 15)) != PT_SMALL_H || ((packed >> 8) & 0xff) != 0x0c) return NULL;
        return obj;
    }
    return NULL;
}

// the loader's own DEFAULT-usage copy of the small texture, put into the object in place of the game's immutable one
static bool portrait_take(PtImage *im, unsigned char *obj) {
    ID3D11Device *dev = *(ID3D11Device **)(g_base + RVA_D3D_DEVICE);
    const unsigned char *pixels = *(const unsigned char **)(obj + 8);
    if (!dev || !pixels) return false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = PT_SMALL_W; td.Height = PT_SMALL_H; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_BC1_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = pixels; init.SysMemPitch = PT_SMALL_W / 4 * 8;
    ID3D11Texture2D *tex = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    if (FAILED(dev->CreateTexture2D(&td, &init, &tex)) || !tex) { logf("stage portraits: CreateTexture2D failed"); return false; }
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = td.Format; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(tex, &vd, &srv)) || !srv) { tex->Release(); logf("stage portraits: CreateShaderResourceView failed"); return false; }
    IUnknown *old_tex = *(IUnknown **)(obj + 0x20), *old_srv = *(IUnknown **)(obj + 0x28);
    *(ID3D11Texture2D **)(obj + 0x20) = tex;
    *(ID3D11ShaderResourceView **)(obj + 0x28) = srv;
    if (old_srv) old_srv->Release();
    if (old_tex) old_tex->Release();
    im->obj = obj; im->mine = tex; im->last = -2;
    logf("stage portraits: image slot %d texture object %p: own copy %p in place (view %p)", im->slot, obj, tex, srv);
    return true;
}

static void portrait_step() {                                  // CwoOverlayFrame
    if (g_pt_img_n <= 0 || !g_pt_atlas) return;
    bool online = ed_online();                                 // online: only the Xbox Live LEVEL LIST's image (slot 3)
    __try {
        ULONG_PTR menu = *(ULONG_PTR *)(g_base + RVA_MENU_MGR), mgr = *(ULONG_PTR *)(g_base + RVA_SPRITE_MGR);
        ID3D11DeviceContext *ctx = *(ID3D11DeviceContext **)(g_base + RVA_D3D_CTX);
        int cell = portrait_cell();
        for (int i = 0; i < g_pt_img_n; i++) {
            PtImage *im = &g_pt_img[i];
            if (online && im->slot != 3) continue;
            unsigned char *obj = portrait_object(menu, mgr, im);
            if (!obj) { im->obj = NULL; im->mine = NULL; continue; }   // image not loaded (the game released any copy with it)
            if (obj != im->obj || *(ID3D11Texture2D **)(obj + 0x20) != im->mine) {
                if (!portrait_take(im, obj)) { im->obj = obj; im->mine = NULL; continue; }
            }
            if (!im->mine || !ctx || cell < 0 || cell == im->last) continue;
            if (cell >= g_pt_cols * (g_pt_ah / g_pt_ch)) { logf("stage portraits: cell %d is outside the atlas", cell); im->last = cell; continue; }
            int x0 = (cell % g_pt_cols) * g_pt_cw, y0 = (cell / g_pt_cols) * g_pt_ch;
            D3D11_BOX box = {0, (UINT)(im->row * PT_H), 0, PT_SMALL_W, (UINT)(im->row * PT_H + PT_H), 1};
            const unsigned char *src = g_pt_atlas + (size_t)(y0 / 4) * (g_pt_aw / 4) * 8 + (x0 / 4) * 8;
            ctx->UpdateSubresource(im->mine, 0, &box, src, g_pt_aw / 4 * 8, 0);
            LONG k = g_mx_cur;
            logf("stage portraits: image slot %d Map Slot 7 picture -> cell %d (%s)", im->slot, cell, k >= 0 && k < g_mx_count ? g_mx[k].name : "its own stage");
            im->last = cell;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int portrait_self_test() {                              // the entry lookup on stand-in game structures (no D3D)
    static unsigned char menu[0x10640], mgr[0x400], alr[0x100], obj[0x60];
    static unsigned handles0[71];
    PtImage im = {0, 5, 71, 4, 63, 0, 0x11111111u, 0x22222222u, NULL, NULL, -2};
    memset(menu, 0, sizeof(menu)); memset(mgr, 0, sizeof(mgr)); memset(alr, 0, sizeof(alr)); memset(obj, 0, sizeof(obj));
    *(ULONG_PTR *)(menu + 0x10530) = 1;
    *(ULONG_PTR *)(menu + 0x10548) = (ULONG_PTR)handles0;
    handles0[63] = 63u << 24 | 2u << 16 | 4u << 8 | 0;
    *(int *)(mgr + 0x128 + 2 * 0x30) = 5; *(int *)(mgr + 0x12c + 2 * 0x30) = 71;
    *(unsigned char **)(mgr + 0x308 + 2 * 8) = alr;
    *(unsigned *)alr = 0x11; *(unsigned *)(alr + 4) = 0x20;    // header, then chunk 0x15 with 5 entries, ours the 5th
    unsigned char *ch = alr + 0x20;
    *(unsigned *)ch = 0x15; *(unsigned short *)(ch + 8) = 5;
    unsigned char *e = ch + 0xc + 28 * 4;
    *(int *)e = -1; *(unsigned char **)(e + 4) = obj; *(unsigned *)(e + 0x14) = im.h0; *(unsigned *)(e + 0x18) = im.h1;
    *(unsigned char **)(obj + 0x18) = e;
    *(unsigned *)(obj + 0x10) = (10u << 24) | (8u << 20) | (1u << 16) | (0x0cu << 8) | 0x29;   // 256 x 1024, 1 mip, DXT1
    bool found = portrait_object((ULONG_PTR)menu, (ULONG_PTR)mgr, &im) == obj;
    *(int *)e = 0x00040001;                                    // not created yet -> nothing
    bool wait = portrait_object((ULONG_PTR)menu, (ULONG_PTR)mgr, &im) == NULL;
    *(int *)e = -1; *(int *)(mgr + 0x12c + 2 * 0x30) = 63;     // the stock VsMode: counts differ -> nothing
    bool stock = portrait_object((ULONG_PTR)menu, (ULONG_PTR)mgr, &im) == NULL;
    logf("self-test: stage portraits texture lookup %s, uncreated entry skipped %s, stock image left alone %s",
         found ? "ok" : "FAILED", wait ? "ok" : "FAILED", stock ? "ok" : "FAILED");
    return found && wait && stock ? 1 : -78;
}
