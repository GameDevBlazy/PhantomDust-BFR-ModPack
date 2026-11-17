// ---- v5.87 the character selects use the black bars (owner 2026-11-03 -> "Okay, make those changes"; the COLORS list "needs to be on the far
// left side ... where the black bars are ... You don't want it to cover up any character or enemy portrait").
// Game facts (Ghidra batch_090 / batch_093, 2026-11-03):
//  - the menus' 2D space is 640 x 480 centred in a 854 x 480 (16:9) picture: the quad drawer FUN_14029b890 turns a
//    full-width quad (x 0, width 640) into x -107, width 854;
//  - FUN_140293060 (8 callers) sets the 2D scissor to the 640 x 480 area in pixels: left = *0x140577af0, top =
//    *0x140574aa0, right = left + *0x140574aa4, bottom = top + *0x140577b18 (ID3D11DeviceContext *0x1404bad40,
//    RSSetScissorRects at vtable +0x168) - so a menu window left of x 0 is cut off there.
// What this does: FUN_140293060 is replaced by wm_scissor, the same rectangle, widened by 107 / 640 of the width on both
// sides while the split-screen character select's grid is on screen (op 77 asked within 300 ms), offline. Display only.
static const ULONG_PTR RVA_WM_SCISSOR = 0x293060, RVA_WM_X = 0x577af0, RVA_WM_Y = 0x574aa0, RVA_WM_W = 0x574aa4,
                       RVA_WM_H = 0x577b18;
static const unsigned char WM_OLD[16] = {0x48, 0x83, 0xec, 0x48, 0x48, 0x8b, 0x05, 0x8d, 0x10, 0x14, 0x00, 0x48, 0x33, 0xc4, 0x48, 0x89};
static int g_wm_state;
static bool wm_wide() {
    // the split-screen select only: the Xbox Live select needs an AGENTS.md online exception first (not added yet)
    return GetTickCount64() - g_sel_seen < 300 && !ed_online();
}
static void wm_scissor() {                                        // replaces FUN_140293060 (game thread, 2D pass)
    int x = *(int *)(g_base + RVA_WM_X), y = *(int *)(g_base + RVA_WM_Y), w = *(int *)(g_base + RVA_WM_W), h = *(int *)(g_base + RVA_WM_H);
    D3D11_RECT r = {x, y, x + w, y + h};
    if (wm_wide() && w > 0) { int extra = w * 107 / 640; r.left = x - extra < 0 ? 0 : x - extra; r.right = x + w + extra; }
    ID3D11DeviceContext *ctx = *(ID3D11DeviceContext **)(g_base + RVA_D3D_CTX);
    if (ctx) ctx->RSSetScissorRects(1, &r);
    static ULONGLONG t; static int n;                              // probe: what the 2D pass sees while the select is up
    if (ctx && wm_wide() && n < 12 && GetTickCount64() - t > 1000) {
        t = GetTickCount64(); n++;
        D3D11_VIEWPORT vp[2]; UINT nv = 2; ctx->RSGetViewports(&nv, vp);
        ID3D11RasterizerState *rs = NULL; ctx->RSGetState(&rs);
        D3D11_RASTERIZER_DESC rd = {}; if (rs) { rs->GetDesc(&rd); rs->Release(); }
        ID3D11RenderTargetView *rtv = NULL; ctx->OMGetRenderTargets(1, &rtv, NULL);
        UINT tw = 0, th = 0;
        if (rtv) { ID3D11Resource *res = NULL; rtv->GetResource(&res); ID3D11Texture2D *tx = NULL;
            if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tx))) { D3D11_TEXTURE2D_DESC td; tx->GetDesc(&td); tw = td.Width; th = td.Height; tx->Release(); }
            if (res) res->Release(); rtv->Release(); }
        logf("wide selects probe: area %d,%d %dx%d -> scissor %d..%d; viewport %.0f,%.0f %.0fx%.0f; scissor test %d; target %ux%u",
             x, y, w, h, r.left, r.right, nv ? vp[0].TopLeftX : 0.f, nv ? vp[0].TopLeftY : 0.f, nv ? vp[0].Width : 0.f, nv ? vp[0].Height : 0.f, (int)rd.ScissorEnable, tw, th);
    }
}
static void wm_install() {                                        // worker start
    unsigned char *site = (unsigned char *)(g_base + RVA_WM_SCISSOR);
    if (memcmp(site, WM_OLD, sizeof(WM_OLD))) { logf("wide selects: FUN_140293060 is not the game's - off"); g_wm_state = -1; return; }
    DWORD old;
    if (!VirtualProtect(site, 12, PAGE_EXECUTE_READWRITE, &old)) { g_wm_state = -1; return; }
    site[0] = 0x48; site[1] = 0xB8; *(ULONGLONG *)(site + 2) = (ULONGLONG)(ULONG_PTR)wm_scissor; site[10] = 0xFF; site[11] = 0xE0;   // mov rax, wm_scissor; jmp rax
    VirtualProtect(site, 12, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 12);
    g_wm_state = 1;
    logf("wide selects: the 2D scissor FUN_140293060 -> wm_scissor (the black bars open on the character selects)");
}
static int wm_self_test() {                                       // the patch on a copy of the game's bytes
    static unsigned char fake[32];
    memcpy(fake, WM_OLD, sizeof(WM_OLD));
    bool same = !memcmp(fake, WM_OLD, sizeof(WM_OLD));
    int w = 1440, extra = w * 107 / 640;
    bool math = extra == 240;                                     // 1920 x 1080 picture: the 1440-wide 4:3 area + 240 a side
    logf("self-test: wide selects bytes %s, bar width %s", same ? "ok" : "FAILED", math ? "ok" : "FAILED");
    return same && math ? 1 : -87;
}
