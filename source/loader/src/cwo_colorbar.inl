// ---- v5.88 COLORS in the left black bar (owner 2026-11-03; "DONT STOP. JUST BUILD AND THEN
// INSTALL EVERYTHING"). The game cannot draw past its 640 x 480 menu space (Ghidra batch_090-101, MODLOG 2026-11-03: two
// widened-viewport tests stretched the whole menu), so the loader moves the game's own finished COLORS window at Present:
//  - the select scripts open COLORS (140 wide, tools/build_colors_side.py) in the column away from the highlighted face;
//  - while the grid is up without COLORS, the frame is kept (CopyResource);
//  - while COLORS is up (op 79 within 200 ms), its rectangle (window record +0 x, +2 y, +4 w, +6 h, in menu units ->
//    pixels by the 4:3 area *0x1404baf90 left, *0x1404baf94 top, *0x1404baf98 width, *0x1404baf9c height, Ghidra
//    batch_100 FUN_140021e70) is copied into the left bar and the kept frame is put back where it was drawn.
// Only pixels the game drew are moved. Display only, this machine only (AGENTS.md "Xbox Live character select layout").
static volatile LONG g_cb_state;                                  // 0 off, 1 keep the frame, 2 move COLORS
static volatile LONG g_cb_rect[11];                               // + restore left/bottom, + the help band (7-10)                                // + restore left (6: restore bottom)                                // left, top, right, bottom, destination left (pixels)
static ID3D11Texture2D *g_cb_cache, *g_cb_tmp, *g_cb_pend;        // v5.88.3 + a pending frame
static UINT g_cb_w, g_cb_h;
static DXGI_FORMAT g_cb_fmt;
static bool g_cb_valid, g_cb_pend_ok;
// v5.88.4 the help line (the game's full-width window at y 374, routine 0x178) is drawn over COLORS when COLORS opens on the
// left; while COLORS is open that window is moved off screen (its record's x), and the band is put back from the kept frame
static int g_cb_help = -1; static short g_cb_help_x;
static void cb_help(ULONG_PTR menu, bool hide) {
    if (hide && g_cb_help < 0) {
        for (int k = 0; k < 16; k++) {
            short *w = (short *)(menu + 0x40 + (ULONG_PTR)k * 0x103c);
            if (w[1] == 374 && w[2] == 640 && w[0] == 0) { g_cb_help = k; g_cb_help_x = w[0]; w[0] = 2000; break; }
        }
    } else if (!hide && g_cb_help >= 0) {
        short *w = (short *)(menu + 0x40 + (ULONG_PTR)g_cb_help * 0x103c);
        if (w[0] == 2000) w[0] = g_cb_help_x;
        g_cb_help = -1;
    }
}
static void cb_step() {                                           // CwoOverlayFrame, game thread
    ULONGLONG now = GetTickCount64();
    bool grid = now - g_sel_seen < 300 || now - g_sel_live_seen < 300;
    ULONG_PTR mm = 0; __try { mm = *(ULONG_PTR *)(g_base + 0x533c58); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    bool open = grid && !g_in_match && now - g_so_seen <= 200 && g_so_win >= 0;
    if (!open) { g_cb_cx = -1; __try { if (mm) cb_help(mm, false); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    if (!grid || g_in_match) { InterlockedExchange(&g_cb_state, 0); return; }
    if (!open) { InterlockedExchange(&g_cb_state, 1); return; }
    __try {
        ULONG_PTR menu = *(ULONG_PTR *)(g_base + 0x533c58);
        if (!menu) return;
        const short *w = (const short *)(menu + 0x40 + (ULONG_PTR)g_so_win * 0x103c);
        float L = *(float *)(g_base + 0x4baf90), T = *(float *)(g_base + 0x4baf94), W = *(float *)(g_base + 0x4baf98), H = *(float *)(g_base + 0x4baf9c);
        if (W < 64 || H < 48 || w[2] <= 0 || w[3] <= 0) return;
        // (seen 2026-11-03 11:48: here the area is the whole 16:9 picture - 0, 60, 1920 x 1080 - and the menu's 640 units are
        // centred in it at the height's scale, so: scale = height / 480, left = area left + (width - 640 * scale) / 2)
        float sy = H / 480.f, sx = sy;
        L += (W - 640.f * sx) * 0.5f;
        int x0 = w[0] - 4, y0 = w[1] - 4, x1 = w[0] + w[2] + 4, y1 = w[1] + w[3] + 8;
        if (y1 > 418) y1 = 418;                                   // not the button bar under it (seen 11:52)
        g_cb_cx = w[0];
        bool left = w[0] < 320;                                   // COLORS opened over the left columns
        cb_help(menu, left);
        int rx0 = left ? 0 : x0, ry1 = y1;                        // the restore covers the help band left of COLORS too
        g_cb_rect[5] = (LONG)(L + rx0 * sx); g_cb_rect[6] = (LONG)(T + ry1 * sy);
        // the help line's band (y 366 .. 396, the whole 640) when it was moved away (below the grid, so no portrait)
        g_cb_rect[7] = left ? (LONG)L : 0; g_cb_rect[8] = (LONG)(T + 366 * sy); g_cb_rect[9] = left ? (LONG)(L + 640 * sx) : 0; g_cb_rect[10] = (LONG)(T + 396 * sy);
        int dst = (int)(L + (46 - (x1 - x0)) * sx);              // its right edge just left of the grid (x 49)
        if (dst < 0) dst = 0;
        g_cb_rect[0] = (LONG)(L + x0 * sx); g_cb_rect[1] = (LONG)(T + y0 * sy); g_cb_rect[2] = (LONG)(L + x1 * sx); g_cb_rect[3] = (LONG)(T + y1 * sy);
        g_cb_rect[4] = dst;
        InterlockedExchange(&g_cb_state, 2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void cb_present(IDXGISwapChain *sc) {                      // Present, render thread
    LONG st = g_cb_state;
    if (!st) { g_cb_valid = g_cb_pend_ok = false; return; }
    ID3D11Device *dev = NULL; ID3D11Texture2D *bb = NULL;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void **)&dev)) || !dev) return;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)) || !bb) { dev->Release(); return; }
    ID3D11DeviceContext *ctx = NULL; dev->GetImmediateContext(&ctx);
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
    if (td.SampleDesc.Count == 1 && ctx) {
        if (!g_cb_cache || td.Width != g_cb_w || td.Height != g_cb_h || td.Format != g_cb_fmt) {
            if (g_cb_cache) { g_cb_cache->Release(); g_cb_cache = NULL; }
            if (g_cb_tmp) { g_cb_tmp->Release(); g_cb_tmp = NULL; }
            if (g_cb_pend) { g_cb_pend->Release(); g_cb_pend = NULL; }
            D3D11_TEXTURE2D_DESC cd = td; cd.BindFlags = 0; cd.Usage = D3D11_USAGE_DEFAULT; cd.CPUAccessFlags = 0; cd.MiscFlags = 0; cd.MipLevels = 1; cd.ArraySize = 1;
            if (SUCCEEDED(dev->CreateTexture2D(&cd, NULL, &g_cb_cache)) && SUCCEEDED(dev->CreateTexture2D(&cd, NULL, &g_cb_tmp)) && SUCCEEDED(dev->CreateTexture2D(&cd, NULL, &g_cb_pend))) {
                g_cb_w = td.Width; g_cb_h = td.Height; g_cb_fmt = td.Format; g_cb_valid = g_cb_pend_ok = false;
                logf("colors bar: frame copies %u x %u", td.Width, td.Height);
            }
        }
        if (g_cb_cache && g_cb_tmp && g_cb_pend) {
            // (owner 2026-11-03: COLORS also showed on the right) the frame COLORS first appears in can still be reported
            // as "closed", so a kept frame only becomes the clean copy when the next frame is closed too
            if (st == 1) { if (g_cb_pend_ok) { ctx->CopyResource(g_cb_cache, g_cb_pend); g_cb_valid = true; } ctx->CopyResource(g_cb_pend, bb); g_cb_pend_ok = true; }
            else if (g_cb_valid) {
                g_cb_pend_ok = false;
                LONG l = g_cb_rect[0], t = g_cb_rect[1], r = g_cb_rect[2], b = g_cb_rect[3], d = g_cb_rect[4];
                if (l < 0) l = 0; if (t < 0) t = 0; if (r > (LONG)td.Width) r = td.Width; if (b > (LONG)td.Height) b = td.Height;
                if (r > l && b > t && d >= 0 && d + (r - l) <= (LONG)td.Width) {
                    D3D11_BOX box = {(UINT)l, (UINT)t, 0, (UINT)r, (UINT)b, 1};
                    ctx->CopySubresourceRegion(g_cb_tmp, 0, l, t, 0, bb, 0, &box);       // the game's COLORS
                    LONG rl = g_cb_rect[5] < 0 ? 0 : g_cb_rect[5];
                    D3D11_BOX rbox = {(UINT)rl, (UINT)t, 0, (UINT)r, (UINT)b, 1};
                    ctx->CopySubresourceRegion(bb, 0, rl, t, 0, g_cb_cache, 0, &rbox);   // the portraits (and the help line) back
                    LONG hl = g_cb_rect[7], ht = g_cb_rect[8], hr = g_cb_rect[9], hb = g_cb_rect[10];
                    if (hr > hl && hb > ht && hr <= (LONG)td.Width && hb <= (LONG)td.Height) {
                        D3D11_BOX hbox = {(UINT)hl, (UINT)ht, 0, (UINT)hr, (UINT)hb, 1};
                        ctx->CopySubresourceRegion(bb, 0, hl, ht, 0, g_cb_cache, 0, &hbox);
                    }
                    ctx->CopySubresourceRegion(bb, 0, d, t, 0, g_cb_tmp, 0, &box);       // COLORS in the bar
                    static bool said; if (!said) { said = true; logf("colors bar: COLORS %ld,%ld-%ld,%ld moved to x %ld", l, t, r, b, d); }
                }
            }
        }
    }
    if (ctx) ctx->Release();
    bb->Release(); dev->Release();
}
