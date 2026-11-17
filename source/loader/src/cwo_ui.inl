// ---- v5.124 Dream UI renderer (owner 2026-11-09: Edgar's Dream > CREATE as its own full-screen screen, "just make it look
// as neat as possible"). The game's debug font (FUN_14028d240) is a small rough fixed font; the CREATE editor draws with its
// own renderer instead: anti-aliased Segoe UI glyphs (CwoUiFont.bin, tools/bake_ui_font.py, Windows GDI) and filled
// rectangles, as one batch of triangles on the finished frame in the Present hook (cwo_post.inl), every state it touches
// saved and put back (the same pattern as the Visions pass). Our own HLSL, compiled with the system's d3dcompiler_47.dll.
// Layout units: a 1920 x 1080 canvas scaled to the back buffer (uniform scale, centred).
// For now it draws the CREATE frame preview while CwoDreamProbe.txt is on (one-player full-screen session), fed by the
// probe's input (cwo_dreamprobe.inl): Dream Bar buttons light under the cursor and answer clicks, the Name field shows the
// typed text, the badge shows the device routing.
struct UiGlyph { unsigned short x, y; unsigned char w, h; signed char ox, oy, adv; unsigned char pad; };
struct UiFont { int size, line, ascent; UiGlyph g[95]; };
struct UiVert { float x, y, u, v; DWORD col; };
static UiFont g_ui_font[8];
static int g_ui_nfont, g_ui_aw, g_ui_ah;
static ID3D11VertexShader *g_ui_vs;
static ID3D11PixelShader *g_ui_ps, *g_ui_ps_img;
static ID3D11Texture2D *g_ui_copy;               // v5.125 the game's finished frame, shown shrunk in the viewport
static ID3D11ShaderResourceView *g_ui_copy_srv;
static UINT g_ui_cw, g_ui_ch; static DXGI_FORMAT g_ui_cf;
// the viewport rectangle (canvas units) and the game picture's place in it - also used to map the mouse into the game
static const float UI_VP_X = 330, UI_VP_Y = 58, UI_VP_W = 1250, UI_VP_H = 742;
static float g_ui_img[4];                                          // x, y, w, h of the game picture (canvas)
static ID3D11InputLayout *g_ui_il;
static ID3D11Buffer *g_ui_vb, *g_ui_cb;
static ID3D11ShaderResourceView *g_ui_srv;
static ID3D11SamplerState *g_ui_smp;
static ID3D11BlendState *g_ui_blend;
static ID3D11RasterizerState *g_ui_rs;
static const int UI_MAXV = 60000;
static UiVert *g_ui_v;
static int g_ui_n;
static float g_ui_s = 1, g_ui_ox, g_ui_oy;                        // canvas -> back buffer
static char g_ui_status[96];
static ULONGLONG g_ui_status_t;

static const char UI_HLSL[] =
"cbuffer C : register(b0) { float4 vp; };\n"
"struct VI { float2 p : POSITION; float2 t : TEXCOORD0; float4 c : COLOR0; };\n"
"struct VO { float4 p : SV_POSITION; float2 t : TEXCOORD0; float4 c : COLOR0; };\n"
"Texture2D tx : register(t0); SamplerState sm : register(s0);\n"
"VO vs(VI i) { VO o; o.p = float4(i.p.x / vp.x * 2 - 1, 1 - i.p.y / vp.y * 2, 0, 1); o.t = i.t; o.c = i.c; return o; }\n"
"float4 ps(VO i) : SV_TARGET { float a = tx.Sample(sm, i.t).r; return float4(i.c.rgb, i.c.a * a); }\n"
"float4 psimg(VO i) : SV_TARGET { return float4(tx.Sample(sm, i.t).rgb, 1); }\n";

static bool ui_build(ID3D11Device *dev) {
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoUiFont.bin", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"rb") != 0 || !f) { logf("dream ui: CwoUiFont.bin missing - off"); return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = (unsigned char *)malloc(n);
    bool rd = d && fread(d, 1, n, f) == (size_t)n;
    fclose(f);
    if (!rd || n < 12 || memcmp(d, "CWUF", 4)) { free(d); logf("dream ui: CwoUiFont.bin unreadable - off"); return false; }
    g_ui_aw = *(unsigned short *)(d + 6); g_ui_ah = *(unsigned short *)(d + 8); g_ui_nfont = *(unsigned short *)(d + 10);
    if (g_ui_nfont > 8) g_ui_nfont = 8;
    size_t o = 12;
    for (int k = 0; k < g_ui_nfont; k++) {
        g_ui_font[k].size = *(unsigned short *)(d + o); g_ui_font[k].line = *(short *)(d + o + 2); g_ui_font[k].ascent = *(short *)(d + o + 4);
        memcpy(g_ui_font[k].g, d + o + 6, sizeof(UiGlyph) * 95);
        o += 6 + sizeof(UiGlyph) * 95;
    }
    if (o + (size_t)g_ui_aw * g_ui_ah > (size_t)n) { free(d); logf("dream ui: font atlas short - off"); return false; }
    D3D11_TEXTURE2D_DESC td = {}; td.Width = g_ui_aw; td.Height = g_ui_ah; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {d + o, (UINT)g_ui_aw, 0};
    ID3D11Texture2D *tex = NULL;
    bool ok = SUCCEEDED(dev->CreateTexture2D(&td, &sd, &tex)) && SUCCEEDED(dev->CreateShaderResourceView(tex, NULL, &g_ui_srv));
    if (tex) tex->Release();
    free(d);
    if (!ok) { logf("dream ui: atlas texture failed - off"); return false; }
    HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
    PFN_D3DCOMPILE comp = dc ? (PFN_D3DCOMPILE)GetProcAddress(dc, "D3DCompile") : NULL;
    if (!comp) { logf("dream ui: d3dcompiler_47.dll missing - off"); return false; }
    ID3DBlob *vb = NULL, *pb = NULL, *err = NULL;
    if (FAILED(comp(UI_HLSL, sizeof(UI_HLSL) - 1, "cwo_ui", NULL, NULL, "vs", "vs_4_0", 0, 0, &vb, &err)) ||
        FAILED(comp(UI_HLSL, sizeof(UI_HLSL) - 1, "cwo_ui", NULL, NULL, "ps", "ps_4_0", 0, 0, &pb, &err))) {
        logf("dream ui: shader: %s", err ? (const char *)err->GetBufferPointer() : "?"); if (err) err->Release(); if (vb) vb->Release(); return false;
    }
    D3D11_INPUT_ELEMENT_DESC il[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                     {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                     {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    ok = SUCCEEDED(dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), NULL, &g_ui_vs)) &&
         SUCCEEDED(dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), NULL, &g_ui_ps)) &&
         SUCCEEDED(dev->CreateInputLayout(il, 3, vb->GetBufferPointer(), vb->GetBufferSize(), &g_ui_il));
    vb->Release(); pb->Release();
    ID3DBlob *ib = NULL;
    if (SUCCEEDED(comp(UI_HLSL, sizeof(UI_HLSL) - 1, "cwo_ui", NULL, NULL, "psimg", "ps_4_0", 0, 0, &ib, &err))) {
        ok = ok && SUCCEEDED(dev->CreatePixelShader(ib->GetBufferPointer(), ib->GetBufferSize(), NULL, &g_ui_ps_img)); ib->Release();
    } else { ok = false; if (err) err->Release(); }
    D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = sizeof(UiVert) * UI_MAXV; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_BUFFER_DESC cd = {}; cd.ByteWidth = 16; cd.Usage = D3D11_USAGE_DYNAMIC; cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SAMPLER_DESC sm = {}; sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sm.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_BLEND_DESC bl = {}; bl.RenderTarget[0].BlendEnable = TRUE; bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_RASTERIZER_DESC rs = {}; rs.FillMode = D3D11_FILL_SOLID; rs.CullMode = D3D11_CULL_NONE; rs.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, NULL, &g_ui_vb)) && SUCCEEDED(dev->CreateBuffer(&cd, NULL, &g_ui_cb)) &&
         SUCCEEDED(dev->CreateSamplerState(&sm, &g_ui_smp)) && SUCCEEDED(dev->CreateBlendState(&bl, &g_ui_blend)) &&
         SUCCEEDED(dev->CreateRasterizerState(&rs, &g_ui_rs));
    g_ui_v = (UiVert *)malloc(sizeof(UiVert) * UI_MAXV);
    logf("dream ui: %s (%d fonts, atlas %d x %d)", ok && g_ui_v ? "ready" : "FAILED", g_ui_nfont, g_ui_aw, g_ui_ah);
    return ok && g_ui_v;
}

// ---- drawing helpers (canvas units, colours 0xAARRGGBB) ----
static DWORD ui_c(DWORD argb) { return (argb & 0xFF00FF00) | ((argb >> 16) & 0xFF) | ((argb & 0xFF) << 16); }   // -> R8G8B8A8
static void ui_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, DWORD c) {
    if (g_ui_n + 6 > UI_MAXV) return;
    x0 = g_ui_ox + x0 * g_ui_s; x1 = g_ui_ox + x1 * g_ui_s; y0 = g_ui_oy + y0 * g_ui_s; y1 = g_ui_oy + y1 * g_ui_s;
    UiVert q[6] = {{x0, y0, u0, v0, c}, {x1, y0, u1, v0, c}, {x0, y1, u0, v1, c}, {x1, y0, u1, v0, c}, {x1, y1, u1, v1, c}, {x0, y1, u0, v1, c}};
    memcpy(g_ui_v + g_ui_n, q, sizeof(q)); g_ui_n += 6;
}
static void ui_rect(float x, float y, float w, float h, DWORD argb) {
    float u = 1.5f / g_ui_aw, v = 1.5f / g_ui_ah;                 // the atlas's solid white corner
    ui_quad(x, y, x + w, y + h, u, v, u, v, ui_c(argb));
}
static void ui_frame(float x, float y, float w, float h, DWORD argb) {   // 1-unit border
    ui_rect(x, y, w, 1, argb); ui_rect(x, y + h - 1, w, 1, argb); ui_rect(x, y, 1, h, argb); ui_rect(x + w - 1, y, 1, h, argb);
}
static float ui_text_w(int font, const char *t) {
    if (font < 0 || font >= g_ui_nfont) return 0;
    float w = 0;
    for (; *t; t++) { int c = (unsigned char)*t; if (c >= 32 && c < 127) w += g_ui_font[font].g[c - 32].adv; }
    return w;
}
static float ui_text(int font, float x, float y, const char *t, DWORD argb) {   // y = top of the line
    if (font < 0 || font >= g_ui_nfont) return x;
    const UiFont &F = g_ui_font[font];
    DWORD c = ui_c(argb);
    for (; *t; t++) {
        int ch = (unsigned char)*t;
        if (ch < 32 || ch >= 127) continue;
        const UiGlyph &g = F.g[ch - 32];
        if (g.w && g.h) {
            float gx = x + g.ox, gy = y + g.oy;
            ui_quad(gx, gy, gx + g.w, gy + g.h, (float)g.x / g_ui_aw, (float)g.y / g_ui_ah, (float)(g.x + g.w) / g_ui_aw, (float)(g.y + g.h) / g_ui_ah, c);
        }
        x += g.adv;
    }
    return x;
}

// ---- the CREATE frame preview ----
static const DWORD UI_PANEL = 0xEE111318, UI_LINE = 0xFF3A352A, UI_HEAD = 0xFF1B1E25, UI_GOLD = 0xFFF0BF55, UI_TEXT = 0xFFE9E3D6,
                   UI_DIM = 0xFF9A9384, UI_BTN = 0xFF1A1D23, UI_BTN_LINE = 0xFF3D3829, UI_ON = 0xFF3A2F15, UI_ON_LINE = 0xFFC79A3B,
                   UI_SEL = 0xFF4A3A14, UI_FIELD = 0xFF0D0F13;
struct UiBtn { const char *label, *key; float x, w; int kind; };   // kind 0 normal, 1 on, 2 play
static float g_ui_px = -1, g_ui_py = -1;
static bool ui_hit(float x, float y, float w, float h) { return g_ui_px >= x && g_ui_px < x + w && g_ui_py >= y && g_ui_py < y + h; }
static void ui_button(float x, float y, float w, float h, const char *label, const char *key, int kind, bool click) {
    bool hov = ui_hit(x, y, w, h);
    DWORD bg = kind == 2 ? 0xFF1F3A24 : kind == 1 ? UI_ON : UI_BTN, ln = kind == 2 ? 0xFF4F9A5B : kind == 1 ? UI_ON_LINE : UI_BTN_LINE;
    if (hov) { bg = kind == 2 ? 0xFF2B5233 : 0xFF2C2618; ln = 0xFFF0BF55; }
    ui_rect(x, y, w, h, bg); ui_frame(x, y, w, h, ln);
    DWORD tc = kind == 2 ? 0xFFBFF0C6 : kind == 1 || hov ? 0xFFFFD77A : 0xFFDDD5C4;
    float tw = ui_text_w(2, label) + (key ? ui_text_w(2, key) + 16 : 0);
    float tx = ui_text(2, x + (w - tw) / 2, y + (h - 25) / 2, label, tc);
    if (key) { float kw = ui_text_w(2, key) + 10; ui_frame(tx + 6, y + 8, kw, h - 16, 0xFF6F6650); ui_text(2, tx + 11, y + (h - 25) / 2, key, 0xFFE6DCC4); }
    if (hov && click) { sprintf_s(g_ui_status, "%s - preview only, nothing changes yet", label); g_ui_status_t = GetTickCount64(); }
}
static void ui_panel(float x, float y, float w, float h, const char *title, const char *right) {
    ui_rect(x, y, w, h, UI_PANEL); ui_frame(x, y, w, h, UI_LINE);
    ui_rect(x + 1, y + 1, w - 2, 34, UI_HEAD); ui_rect(x + 1, y + 35, w - 2, 1, UI_LINE);
    ui_text(3, x + 14, y + 5, title, UI_GOLD);
    if (right) ui_text(2, x + w - 14 - ui_text_w(2, right), y + 6, right, 0xFF7D776A);
}
static void ui_row(float x, float y, float w, const char *label, const char *value, bool field) {
    ui_text(2, x + 14, y + 6, label, 0xFFB9B19F);
    if (field) { ui_rect(x + 160, y + 4, w - 174, 28, UI_FIELD); ui_frame(x + 160, y + 4, w - 174, 28, 0xFF34302A); }
    ui_text(2, x + 168, y + 6, value, 0xFFF2EAD8);
    ui_rect(x + 1, y + 36, w - 2, 1, 0xFF1F2126);
}
static void ui_cat(float x, float y, float w, const char *t) {
    ui_rect(x + 1, y, w - 2, 28, 0xFF191B20); ui_text(3, x + 14, y + 2, t, UI_ON_LINE);
}

static void ui_base() {
    // v5.125 (owner): vertices
    // 0-5 = the editor background, 6-11 = the game's whole finished frame (HUD, skills and all) fitted into the viewport
    ui_rect(0, 0, 1920, 1080, 0xFF0B0C0F);
    {
        float iw = UI_VP_W, ih = UI_VP_W * 9.0f / 16.0f;
        if (ih > UI_VP_H) { ih = UI_VP_H; iw = UI_VP_H * 16.0f / 9.0f; }
        float ix = UI_VP_X + (UI_VP_W - iw) / 2, iy = UI_VP_Y + 4;   // v5.125.2 top-aligned: the strip under it holds the hints + badge
        g_ui_img[0] = ix; g_ui_img[1] = iy; g_ui_img[2] = iw; g_ui_img[3] = ih;
        float v0 = 0, v1 = 1;                                     // the game draws 16:9 centred in the back buffer
        if (g_ui_cw && g_ui_ch) { float gh = g_ui_cw * 9.0f / 16.0f; if (gh < g_ui_ch) { v0 = (g_ui_ch - gh) / 2 / g_ui_ch; v1 = 1 - v0; } }
        ui_quad(ix, iy, ix + iw, iy + ih, 0, v0, 1, v1, 0xFFFFFFFF);
    }
}
static void ui_compose(bool click) {                              // v5.124 the mock-up (kept for the probe file)
    ui_base();
    // Dream Bar
    ui_rect(0, 0, 1920, 58, 0xF0121419); ui_rect(0, 56, 1920, 2, 0xFF8A6A27);
    ui_text(4, 18, 4, "CREATE", UI_GOLD);
    float x = 190;
    ui_button(x, 10, 120, 38, "Stage", NULL, 1, click); x += 132;
    ui_text(2, x, 16, "Edgar's Dream Stage", UI_DIM); x += 200;
    const UiBtn B[] = {{"Save", "Ctrl S", 0, 130, 0}, {"Undo", NULL, 0, 80, 0}, {"Redo", NULL, 0, 80, 0}, {"Select", "Q", 0, 110, 0},
                       {"Move", "W", 0, 105, 1}, {"Turn", "E", 0, 100, 0}, {"Snap 1 m", NULL, 0, 110, 0}, {"Check stage", NULL, 0, 140, 0},
                       {"Test in Dream", "Alt P", 0, 210, 2}};
    for (int k = 0; k < 9; k++) {
        if (k == 3 || k == 7) { ui_rect(x, 12, 1, 34, UI_LINE); x += 12; }
        ui_button(x, 10, B[k].w, 38, B[k].label, B[k].key, B[k].kind, click); x += B[k].w + 10;
    }
    ui_button(1800, 10, 108, 38, "Help", "F1", 0, click);
    // Ledger
    ui_panel(0, 58, 330, 742, "LEDGER", "preview");
    ui_rect(12, 102, 306, 30, UI_FIELD); ui_frame(12, 102, 306, 30, 0xFF34302A); ui_text(2, 20, 104, "Search the stage...", 0xFF6F6A5E);
    const char *tree[] = {"v  Floors (3)", "     Open floor", "     Edge walls", "v  Spawns (4)", "     Spawn 1", "     Spawn 2", "> Lifts (0)",
                          "> Breakables (0)", "> Objects in storage (360)", "> Kill zone, sky, light"};
    for (int k = 0; k < 10; k++) {
        float ty = 144 + k * 32;
        bool sel = k == 4, hov = ui_hit(0, ty, 330, 32);
        if (sel) { ui_rect(1, ty, 328, 32, UI_SEL); ui_rect(1, ty, 3, 32, UI_GOLD); }
        else if (hov) ui_rect(1, ty, 328, 32, 0xFF1F2128);
        ui_text(1, 18, ty + 1, tree[k], sel ? 0xFFFFE2A0 : (tree[k][0] == ' ' ? 0xFFBDB5A3 : 0xFFCFC6B2));
    }
    ui_text(2, 14, 760, "Click to select - double-click to fly to it", 0xFF8A8373);
    // Attributes
    ui_panel(1580, 58, 340, 742, "ATTRIBUTES", "Spawn 1");
    ui_cat(1580, 94, 340, "IDENTITY");
    char nm[64]; sprintf_s(nm, "%s%s", g_dp_name[0] ? g_dp_name : "Spawn 1", ((GetTickCount64() / 450) & 1) ? "|" : "");
    ui_row(1580, 122, 340, "Name", nm, true);
    ui_cat(1580, 160, 340, "PLACEMENT");
    ui_row(1580, 188, 340, "Position", "-11.5  20.0  11.5", true);
    ui_row(1580, 226, 340, "Facing", "90 deg", true);
    ui_row(1580, 264, 340, "Sits on floor", "yes - floor 20.0", false);
    ui_cat(1580, 302, 340, "SPAWN");
    ui_row(1580, 330, 340, "Team", "A", true);
    ui_row(1580, 368, 340, "Capsule spots", "3  (2.5 / 4 / 5.5 m)", false);
    ui_text(2, 1594, 420, "Type to rename. Changes show in the", 0xFFA79F8D);
    ui_text(2, 1594, 444, "view as you make them.", 0xFFA79F8D);
    // Vault
    ui_panel(0, 800, 1920, 280, "VAULT", "drag into the view - double-click to place");
    const char *tabs[] = {"Stage pieces", "Objects", "Breakables", "Lifts & markers", "Skies & lights", "Textures", "My pieces"};
    float tx = 12;
    for (int k = 0; k < 7; k++) {
        float tw = ui_text_w(2, tabs[k]) + 28; bool on = k == 0, hov = ui_hit(tx, 842, tw, 32);
        ui_rect(tx, 842, tw, 32, on ? 0xFF2A2316 : hov ? 0xFF1F2128 : 0xFF15171C); ui_frame(tx, 842, tw, 32, on ? 0xFF6D5626 : 0xFF2C2922);
        ui_text(2, tx + 14, 845, tabs[k], on ? 0xFFFFD77A : 0xFFA79F8D); tx += tw + 4;
    }
    const char *items[] = {"Sein Ring", "Island Rock", "Rock Bridge", "Perch Rock", "Floating Ledge", "Sein Tower", "Rubble Ridge",
                           "Floor Plate A", "Gravity Lift", "Basic Box", "Spawn Point", "Kill Zone", "Sein Sky"};
    for (int k = 0; k < 13; k++) {
        float ix = 14 + k * 146, iy = 888; bool hov = ui_hit(ix, iy, 132, 84);
        ui_rect(ix, iy, 132, 84, 0xFF22252C); ui_frame(ix, iy, 132, 84, hov ? UI_GOLD : 0xFF3D382C);
        ui_text(2, ix, iy + 90, items[k], 0xFFD8CFBB);
    }
    // the routing badge + status
    // v5.125.2 the hint line and the routing badge sit in the strip under the game picture, never over it
    float sy = g_ui_img[1] + g_ui_img[3] + 3;
    char bd[96]; sprintf_s(bd, "PAD > %s     KEYS + MOUSE > %s", g_dp_pad_editor ? "EDITOR" : "FIGHTER", g_dp_kbm_editor ? "EDITOR" : "FIGHTER");
    float bw = ui_text_w(2, bd) + 24;
    ui_rect(1574 - bw, sy, bw, 26, 0xD80A0B0E); ui_frame(1574 - bw, sy, bw, 26, 0xFF6D5626); ui_text(2, 1586 - bw, sy - 1, bd, 0xFFFFD77A);
    if (g_ui_status[0] && GetTickCount64() - g_ui_status_t < 2500) {
        float sw = ui_text_w(1, g_ui_status) + 32;
        ui_rect(960 - sw / 2, 70, sw, 40, 0xE01F3A24); ui_frame(960 - sw / 2, 70, sw, 40, 0xFF4F9A5B); ui_text(1, 976 - sw / 2, 74, g_ui_status, 0xFFDFF5E2);
    }
    ui_text(2, g_ui_img[0] + 4, sy - 1, "Hold right mouse + WASD to fly   Q / E down / up   F focus   L3 + R3 switch the pad", 0xC0CFC6B2);
    ui_frame(g_ui_img[0] - 1, g_ui_img[1] - 1, g_ui_img[2] + 2, g_ui_img[3] + 2, 0xFF3A352A);
    // cursor ring (the system arrow is drawn by Windows; this marks the click point in screenshots too)
    if (g_ui_px >= 0) { ui_frame(g_ui_px - 6, g_ui_py - 6, 12, 12, 0xFFFFD77A); ui_rect(g_ui_px - 1, g_ui_py - 1, 2, 2, 0xFFFFD77A); }
}

static void cr_compose(bool click);                               // v5.127 cwo_create.inl: the real CREATE panels
// v5.133.2 the Skill Vault drawn by this renderer (in game 06:38: the loader's debug-font pages only draw inside Edgar's
// Dream's menus, so on View Stock the vault was open but not seen)
static void vt_ui() {
    // v5.172.4 (owner 2026-11-13; in game the vault's text was about
    // 60 % of the game's menu text): the vault draws on a 1440 x 810 canvas (ui_present) - one third larger on screen
    const float X = 320, Y = 96, W = 800, ROWH = 34;              // v5.172.5 Y 96: below the menu's header line
    const int SHOW = 16;
    ui_rect(0, 0, 1440, 810, 0x99000000);
    ui_panel(X, Y, W, g_vt_n ? 110 + SHOW * ROWH : 130, g_vt_phase == 0 ? "SKILL VAULT" : "SKILL VAULT - REPLACE WHICH?",   // v5.172.5 empty: one line's box
             !g_vt_n ? "B: close" : g_vt_phase == 0 ? "A: pick   B: close" : "A: replace   B: back");   // v5.172.6 empty: nothing to pick
    float y = Y + 46;
    if (g_vt_msg[0]) { ui_text(2, X + 14, y, g_vt_msg, 0xFFA0E0FF); }
    y += 32;
    if (!g_vt_n) { ui_text(2, X + 14, y, g_vt_phase == 0 ? "The vault is empty: you own every skill." : "No stock skill is free (all are in your arsenals).", 0xFFF2EAD8); return; }
    if (g_vt_row < g_vt_top) g_vt_top = g_vt_row;
    if (g_vt_row >= g_vt_top + SHOW) g_vt_top = g_vt_row - SHOW + 1;
    char line[96];
    for (int i = g_vt_top; i < g_vt_n && i < g_vt_top + SHOW; i++, y += ROWH) {
        bool on = i == g_vt_row;
        if (on) ui_rect(X + 4, y - 2, W - 8, ROWH - 2, 0xFF3A2A12);
        sprintf_s(line, "%4d   %s", (int)g_vt_list[i], sc_skill_name(g_vt_list[i]));
        ui_text(2, X + 18, y + 2, line, on ? 0xFFFFD040 : 0xFFF2EAD8);
    }
    sprintf_s(line, "%d of %d     Up / Down: move     Left / Right: 10 at a time", g_vt_row + 1, g_vt_n);
    ui_text(2, X + 14, Y + 110 + SHOW * ROWH - 34, line, 0xFF9A948A);
}

static void ui_present(IDXGISwapChain *sc) {                      // Present hook (render thread)
    bool vault = g_ed_open && g_ed_page == EDP_VAULT;              // v5.133.2
    if (!vault && (!g_dp_on || g_dp_win_state != 1)) return;
    ID3D11Device *dev = NULL; ID3D11Texture2D *bb = NULL;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void **)&dev)) || !dev) return;
    if (!g_ui_state) g_ui_state = ui_build(dev) ? 1 : -1;
    if (g_ui_state != 1 || FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)) || !bb) { dev->Release(); return; }
    ID3D11DeviceContext *ctx = NULL; dev->GetImmediateContext(&ctx);
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
    const float CW = vault ? 1440.0f : 1920.0f, CH = vault ? 810.0f : 1080.0f;   // v5.172.4 the vault's larger canvas
    g_ui_s = (float)td.Width / CW; if ((float)td.Height / CH < g_ui_s) g_ui_s = (float)td.Height / CH;
    g_ui_ox = ((float)td.Width - CW * g_ui_s) / 2; g_ui_oy = ((float)td.Height - CH * g_ui_s) / 2;
    g_ui_px = g_ui_pfx >= 0 ? ((float)g_ui_pfx * td.Width - g_ui_ox) / g_ui_s : -1;
    g_ui_py = g_ui_pfy >= 0 ? ((float)g_ui_pfy * td.Height - g_ui_oy) / g_ui_s : -1;
    static LONG seen;
    LONG seq = g_ui_click_seq; bool click = seq != seen; seen = seq;
    if (!g_ui_copy || td.Width != g_ui_cw || td.Height != g_ui_ch || td.Format != g_ui_cf) {
        if (g_ui_copy_srv) { g_ui_copy_srv->Release(); g_ui_copy_srv = NULL; }
        if (g_ui_copy) { g_ui_copy->Release(); g_ui_copy = NULL; }
        D3D11_TEXTURE2D_DESC cd = td; cd.BindFlags = D3D11_BIND_SHADER_RESOURCE; cd.Usage = D3D11_USAGE_DEFAULT; cd.CPUAccessFlags = 0; cd.MiscFlags = 0; cd.MipLevels = 1; cd.ArraySize = 1;
        if (SUCCEEDED(dev->CreateTexture2D(&cd, NULL, &g_ui_copy)) && SUCCEEDED(dev->CreateShaderResourceView(g_ui_copy, NULL, &g_ui_copy_srv))) {
            g_ui_cw = td.Width; g_ui_ch = td.Height; g_ui_cf = td.Format;
        }
    }
    g_ui_n = 0;
    if (vault) vt_ui(); else if (g_cr_entry) cr_compose(click); else ui_compose(click);
    ID3D11RenderTargetView *rtv = NULL;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (ctx && g_ui_n && SUCCEEDED(dev->CreateRenderTargetView(bb, NULL, &rtv)) && rtv && SUCCEEDED(ctx->Map(g_ui_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, g_ui_v, sizeof(UiVert) * g_ui_n); ctx->Unmap(g_ui_vb, 0);
        if (SUCCEEDED(ctx->Map(g_ui_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { float *f = (float *)ms.pData; f[0] = (float)td.Width; f[1] = (float)td.Height; f[2] = f[3] = 0; ctx->Unmap(g_ui_cb, 0); }
        ID3D11RenderTargetView *o_rtv[8] = {}; ID3D11DepthStencilView *o_dsv = NULL; ctx->OMGetRenderTargets(8, o_rtv, &o_dsv);
        UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; D3D11_VIEWPORT o_vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; ctx->RSGetViewports(&nvp, o_vp);
        ID3D11RasterizerState *o_rs = NULL; ctx->RSGetState(&o_rs);
        ID3D11BlendState *o_bs = NULL; float o_bf[4]; UINT o_sm; ctx->OMGetBlendState(&o_bs, o_bf, &o_sm);
        ID3D11DepthStencilState *o_ds = NULL; UINT o_sr; ctx->OMGetDepthStencilState(&o_ds, &o_sr);
        ID3D11InputLayout *o_il = NULL; ctx->IAGetInputLayout(&o_il);
        D3D11_PRIMITIVE_TOPOLOGY o_tp; ctx->IAGetPrimitiveTopology(&o_tp);
        ID3D11Buffer *o_vb = NULL; UINT o_st = 0, o_of = 0; ctx->IAGetVertexBuffers(0, 1, &o_vb, &o_st, &o_of);
        ID3D11VertexShader *o_vs = NULL; ctx->VSGetShader(&o_vs, NULL, NULL);
        ID3D11Buffer *o_vcb = NULL; ctx->VSGetConstantBuffers(0, 1, &o_vcb);
        ID3D11PixelShader *o_ps = NULL; ctx->PSGetShader(&o_ps, NULL, NULL);
        ID3D11GeometryShader *o_gs = NULL; ctx->GSGetShader(&o_gs, NULL, NULL);
        ID3D11ShaderResourceView *o_srv = NULL; ctx->PSGetShaderResources(0, 1, &o_srv);
        ID3D11SamplerState *o_smp = NULL; ctx->PSGetSamplers(0, 1, &o_smp);
        D3D11_VIEWPORT vp = {0, 0, (float)td.Width, (float)td.Height, 0, 1};
        UINT stride = sizeof(UiVert), off = 0;
        ctx->OMSetRenderTargets(1, &rtv, NULL); ctx->RSSetViewports(1, &vp); ctx->RSSetState(g_ui_rs);
        float bf[4] = {0, 0, 0, 0}; ctx->OMSetBlendState(g_ui_blend, bf, 0xffffffff); ctx->OMSetDepthStencilState(NULL, 0);
        ctx->IASetInputLayout(g_ui_il); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ctx->IASetVertexBuffers(0, 1, &g_ui_vb, &stride, &off);
        ctx->VSSetShader(g_ui_vs, NULL, 0); ctx->VSSetConstantBuffers(0, 1, &g_ui_cb); ctx->GSSetShader(NULL, NULL, 0);
        ctx->PSSetShader(g_ui_ps, NULL, 0); ctx->PSSetShaderResources(0, 1, &g_ui_srv); ctx->PSSetSamplers(0, 1, &g_ui_smp);
        if (g_ui_copy_srv && g_ui_n > 12) {
            ctx->CopyResource(g_ui_copy, bb);                    // the game's finished frame
            ctx->Draw(6, 0);                                     // background
            ctx->PSSetShader(g_ui_ps_img, NULL, 0); ctx->PSSetShaderResources(0, 1, &g_ui_copy_srv);
            ctx->Draw(6, 6);                                     // the game, shrunk into the viewport
            ctx->PSSetShader(g_ui_ps, NULL, 0); ctx->PSSetShaderResources(0, 1, &g_ui_srv);
            ctx->Draw(g_ui_n - 12, 12);                          // the editor
        } else ctx->Draw(g_ui_n, 0);
        ctx->OMSetRenderTargets(8, o_rtv, o_dsv); ctx->RSSetViewports(nvp, o_vp); ctx->RSSetState(o_rs); ctx->OMSetBlendState(o_bs, o_bf, o_sm);
        ctx->OMSetDepthStencilState(o_ds, o_sr); ctx->IASetInputLayout(o_il); ctx->IASetPrimitiveTopology(o_tp); ctx->IASetVertexBuffers(0, 1, &o_vb, &o_st, &o_of);
        ctx->VSSetShader(o_vs, NULL, 0); ctx->VSSetConstantBuffers(0, 1, &o_vcb); ctx->PSSetShader(o_ps, NULL, 0); ctx->GSSetShader(o_gs, NULL, 0);
        ctx->PSSetShaderResources(0, 1, &o_srv); ctx->PSSetSamplers(0, 1, &o_smp);
        for (int k = 0; k < 8; k++) if (o_rtv[k]) o_rtv[k]->Release();
        IUnknown *rel[] = {o_dsv, o_rs, o_bs, o_ds, o_il, o_vb, o_vs, o_vcb, o_ps, o_gs, o_srv, o_smp};
        for (IUnknown *u : rel) if (u) u->Release();
        static bool logged; if (!logged) { logged = true; logf("dream ui: first frame %u x %u, %d vertices, scale %.3f", td.Width, td.Height, g_ui_n, g_ui_s); }
    }
    if (rtv) rtv->Release();
    if (ctx) ctx->Release();
    bb->Release(); dev->Release();
}
