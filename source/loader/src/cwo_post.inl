// ---- v5.78 post-process pass on the finished frame (owner 2026-11-02; "map the original theme's several colors ... to the holiday's palette"; "the title
// screen's logo follows the theme too"; Halloween: "Happy Halloween ... flickering ... pumpkins ... ghosts").
// Facts: the menu text / window colours are constants inside the game's draw code (0xff5cac95 normal text, 62 places;
// FUN_14024a780 and others, MODLOG 2026-11-02) and the bars are StMenu's teal strips, so the theme is applied to the
// finished picture instead. Windows documentation only (no game code): IDXGISwapChain::Present is vtable slot 8 and
// IDXGISwapChain1::Present1 slot 22; the loader learns the swap chain class's vtable by making a small composition swap
// chain of its own on the game's device (IDXGIFactory2::CreateSwapChainForComposition) and puts its hook in both slots.
// The pass (our own HLSL, compiled with the system's d3dcompiler_47.dll): copy the back buffer; a full-screen triangle
// maps teal / green pixels (hue 85-215 degrees, saturated) to the holiday's three colours by brightness (grey CRT lines
// stay); on the title screen the fire logo's hues (0-60 degrees) too; then the holiday's own drawn decorations (pumpkins
// and ghosts, snow, hearts, shamrocks, eggs, stars, fireworks, leaves) away from the menu panel. Every pipeline state the
// pass touches is saved and put back. Menus only (never in a match); off when no holiday is on or CwoTheme.txt says off.
#include <d3dcompiler.h>
#include <dxgi1_2.h>
typedef HRESULT(WINAPI *PFN_D3DCOMPILE)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
typedef HRESULT(STDMETHODCALLTYPE *PFN_PRESENT)(IDXGISwapChain *, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *PFN_PRESENT1)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
static void cb_present(IDXGISwapChain *sc);                       // v5.88 cwo_colorbar.inl
static void ui_present(IDXGISwapChain *sc);                       // v5.124 cwo_ui.inl (the CREATE editor's own UI)
static PFN_PRESENT g_pp_present;
static PFN_PRESENT1 g_pp_present1;
static int g_pp_state;                                            // 0 not tried, 1 hooked, -1 failed
static ID3D11VertexShader *g_pp_vs;
static ID3D11PixelShader *g_pp_ps;
static ID3D11Buffer *g_pp_cb;
static ID3D11SamplerState *g_pp_samp, *g_pp_lin;
static ID3D11ShaderResourceView *g_pp_spr;            // v5.78.2 holiday figures (Menu\cwo_holiday_sprites.bin)
static int g_pp_spr_cells;
static ID3D11Texture2D *g_pp_copy;
static ID3D11ShaderResourceView *g_pp_srv;
static UINT g_pp_w, g_pp_h;
static DXGI_FORMAT g_pp_fmt;
static volatile LONG g_pp_mode;                                   // game thread -> Present: 0 off, else 1 + holiday (+64 title screen)
static ULONGLONG g_pp_t0;
static unsigned g_pp_frames;

struct PpPalette { float c[3][3]; int decor; };
// per holiday (HOLIDAYS order): dark / middle / light (r g b 0-1), decoration kind (v5.78.4: all 0 - owner 2026-11-02:
// "Remove the pumpkins and ghost. It looks too kiddie-ish ... get rid of the ghost and the pumpkins and the character model")
static const PpPalette PP_PAL[] = {
    {{{0.04f, 0.08f, 0.24f}, {0.90f, 0.74f, 0.24f}, {1.00f, 1.00f, 0.94f}}, 0},   // New Year: navy, gold, white - fireworks
    {{{0.24f, 0.02f, 0.10f}, {0.94f, 0.35f, 0.59f}, {1.00f, 0.80f, 0.86f}}, 0},   // Valentine's: crimson, pink, blush - hearts
    {{{0.02f, 0.16f, 0.06f}, {0.16f, 0.75f, 0.31f}, {0.94f, 0.82f, 0.35f}}, 0},   // St. Patrick's: green, emerald, gold - shamrocks
    {{{0.27f, 0.20f, 0.43f}, {0.94f, 0.67f, 0.78f}, {0.75f, 0.94f, 0.82f}}, 0},   // Easter: lavender, pink, mint - eggs
    {{{0.06f, 0.10f, 0.31f}, {0.86f, 0.16f, 0.20f}, {0.94f, 0.94f, 1.00f}}, 0},   // 4th of July: navy, red, white - stars
    {{{0.14f, 0.04f, 0.20f}, {1.00f, 0.47f, 0.00f}, {1.00f, 0.78f, 0.24f}}, 0},   // Halloween: purple, orange, amber - pumpkins + ghosts
    {{{0.24f, 0.12f, 0.04f}, {0.82f, 0.39f, 0.12f}, {0.94f, 0.75f, 0.31f}}, 0},   // Thanksgiving: brown, burnt orange, gold - leaves
    {{{0.04f, 0.20f, 0.10f}, {0.78f, 0.12f, 0.16f}, {0.96f, 0.98f, 1.00f}}, 0},   // Christmas: pine, red, snow - snow
};

static const char PP_HLSL[] =
"cbuffer C : register(b0) { float4 c0; float4 c1; float4 c2; float4 p; float4 scr; float4 spr; float4 v1; float4 v2; float4 v3; float4 v4; };\n"
"Texture2D T : register(t0); SamplerState S : register(s0); Texture2D G : register(t1); SamplerState L : register(s1);\n"
"struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
"V vs(uint id : SV_VertexID) { V o; float2 t = float2((id << 1) & 2, id & 2); o.uv = t; o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
"float3 hsv(float3 c) { float4 K = float4(0, -1.0/3.0, 2.0/3.0, -1); float4 q = c.g < c.b ? float4(c.bg, K.wz) : float4(c.gb, K.xy);\n"
"  float4 r = c.r < q.x ? float4(q.xyw, c.r) : float4(c.r, q.yzx); float d = r.x - min(r.w, r.y);\n"
"  return float3(abs(r.z + (r.w - r.y) / (6 * d + 1e-5)), d / (r.x + 1e-5), r.x); }\n"
"float3 pal(float t) { t = saturate(t); return t < 0.5 ? lerp(c0.rgb, c1.rgb, t * 2) : lerp(c1.rgb, c2.rgb, t * 2 - 1); }\n"
"float h21(float2 v) { return frac(sin(dot(v, float2(12.9898, 78.233))) * 43758.5453); }\n"
"float ell(float2 q, float2 r) { return length(q / r) - 1; }\n"
"float4 over(float4 a, float3 c, float m) { m = saturate(m); return float4(lerp(a.rgb, c, m), max(a.a, m)); }\n"
"float aa(float d, float px) { return saturate(0.5 - d / px); }\n"
// one decoration at q (units of screen height), size s; kind k; returns colour + coverage
"float4 deco(float2 q, float s, int k, float t, float sd) { float4 o = 0; float px = 1.5 / scr.y / s; q /= s;\n"
"  if (k == 6) {\n"
"    if (sd < 0.5) {\n"   // pumpkin: three lobes, ribs, stem, glowing face
"      float d = min(ell(q - float2(0, 0), float2(0.55, 0.45)), min(ell(q - float2(-0.33, 0.02), float2(0.4, 0.42)), ell(q - float2(0.33, 0.02), float2(0.4, 0.42))));\n"
"      float rib = 0.75 + 0.25 * cos(q.x * 9);\n"
"      o = over(o, float3(1.0, 0.45, 0.02) * rib, aa(d, px));\n"
"      o = over(o, float3(0.2, 0.45, 0.1), aa(max(abs(q.x + 0.03) - 0.06, abs(q.y + 0.52) - 0.12), px));\n"
"      float fl = 0.75 + 0.25 * sin(t * 13 + sd * 40) * sin(t * 7.3);\n"
"      float2 e = float2(abs(q.x) - 0.2, q.y + 0.1); float eye = max(max(-e.y - 0.12, e.y - 0.0), abs(e.x) - (0.12 + e.y));\n"
"      float mo = max(abs(q.x) - 0.32, abs(q.y - 0.17 + 0.05 * sin(q.x * 30)) - 0.06);\n"
"      o = over(o, float3(1.0, 0.85, 0.2) * fl, aa(min(eye, mo), px) * (d < 0 ? 1 : 0));\n"
"    } else {\n"            // ghost: round head, wavy hem, two eyes
"      float hem = 0.5 + 0.06 * sin(q.x * 18 + t * 6);\n"
"      float d = min(length(q - float2(0, -0.15)) - 0.4, max(abs(q.x) - 0.4, max(q.y - hem, -0.15 - q.y)));\n"
"      o = over(o, float3(0.95, 0.95, 1.0), aa(d, px) * 0.8);\n"
"      float ey = min(ell(q - float2(-0.14, -0.2), float2(0.07, 0.1)), ell(q - float2(0.14, -0.2), float2(0.07, 0.1)));\n"
"      o = over(o, float3(0.05, 0.02, 0.1), aa(ey, px));\n"
"    } }\n"
"  else if (k == 2) { float2 h = float2(abs(q.x), -q.y + 0.1); float d = length(h - float2(0.25, 0.22)) - 0.28; d = min(d, max(h.y + h.x * 1.1 - 0.42, -h.y - 0.55 + h.x));\n"
"    o = over(o, sd < 0.5 ? float3(0.95, 0.25, 0.45) : float3(1, 0.65, 0.8), aa(d, px)); }\n"
"  else if (k == 3) { float d = 1e3; for (int i = 0; i < 3; i++) { float a = i * 2.094 + 1.57; d = min(d, length(q - 0.27 * float2(cos(a), -sin(a))) - 0.24); }\n"
"    d = min(d, max(abs(q.x - 0.05 - q.y * 0.2) - 0.04, abs(q.y - 0.45) - 0.25)); o = over(o, float3(0.15, 0.7, 0.25), aa(d, px)); }\n"
"  else if (k == 4) { float d = ell(q, float2(0.36, 0.48)); float3 col = sd < 0.33 ? float3(0.95, 0.7, 0.8) : sd < 0.66 ? float3(0.7, 0.9, 0.8) : float3(0.8, 0.75, 0.97);\n"
"    float band = step(0.5, frac(q.y * 3 + 0.25)); o = over(o, lerp(col, col * 0.75 + 0.25, band), aa(d, px)); }\n"
"  else if (k == 5 || k == 1) { float a = atan2(q.y, q.x) + t * 0.5, r = length(q); float st = 0.32 + 0.16 * cos(5 * a);\n"
"    float3 col = k == 1 ? float3(1, 0.85, 0.35) : (sd < 0.33 ? float3(0.9, 0.15, 0.2) : sd < 0.66 ? float3(0.95, 0.95, 1) : float3(0.2, 0.3, 0.9));\n"
"    o = over(o, col, aa(r - st, px));\n"
"    float ph = frac(t * 0.35 + sd * 3); float ring = abs(r - ph * 1.4) - 0.03; float rays = step(0.6, frac(a * 3.0 / 6.283 * 6));\n"
"    o = over(o, col, aa(ring, px) * rays * (1 - ph)); }\n"
"  else if (k == 7) { float a = t * 1.3 + sd * 6; float2 r = float2(cos(a) * q.x - sin(a) * q.y, sin(a) * q.x + cos(a) * q.y);\n"
"    float d = ell(r, float2(0.45, 0.22)); float3 col = sd < 0.5 ? float3(0.85, 0.35, 0.08) : float3(0.75, 0.55, 0.12);\n"
"    o = over(o, col, aa(d, px)); o = over(o, col * 0.6, aa(abs(r.y) - 0.015, px) * (d < 0 ? 1 : 0)); }\n"
"  return o; }\n"
"float4 ps(V i) : SV_Target { float4 src = T.Sample(S, i.uv); float3 c = src.rgb; float t = p.x;\n"
// Visions (v5.83; owner: graphics options like clarity, blur ... our own effects): v1 = sharpen, blur, saturation, contrast;
// v2 = vignette, warmth, brightness, on
// v5.130 (owner 2026-11-10): our own effects;
// v3 = cel bands, ink outline, bloom, film grain; v4 = chromatic shift, scanlines, filmic tone, vibrance
"  if (v2.w > 0.5) { float2 px = 1.0 / scr.xy; float3 W = float3(0.299, 0.587, 0.114);\n"
"    if (v4.x > 0) { float2 o = (i.uv - 0.5) * v4.x * 0.012; c = float3(T.Sample(L, i.uv + o).r, c.g, T.Sample(L, i.uv - o).b); }\n"
"    float3 n0 = T.Sample(S, i.uv + float2(px.x, 0)).rgb, n1 = T.Sample(S, i.uv - float2(px.x, 0)).rgb, n2 = T.Sample(S, i.uv + float2(0, px.y)).rgb, n3 = T.Sample(S, i.uv - float2(0, px.y)).rgb;\n"
"    float3 nb = (n0 + n1 + n2 + n3) * 0.25;\n"
"    c = lerp(c, nb, v1.y); c = c + (c - nb) * v1.x * 1.5;\n"
"    if (v3.z > 0) { float3 b = 0; for (int r = 1; r <= 3; r++) for (int a = 0; a < 6; a++) { float an = a * 1.0472 + r; float3 sm = T.Sample(L, i.uv + float2(cos(an), sin(an)) * px * r * 5).rgb;\n"
"        b += max(sm - 0.62, 0); } c += b / 18 * v3.z * 2.2; }\n"
"    if (v3.y > 0) { float e = abs(dot(n0 - n1, W)) + abs(dot(n2 - n3, W)); c *= 1 - v3.y * smoothstep(0.06, 0.22, e); }\n"
"    if (v3.x > 0) { float bands = lerp(24, 3, v3.x); float l0 = max(dot(c, W), 1e-4); float lq = floor(l0 * bands + 0.5) / bands; c *= lq / l0; }\n"
"    float l = dot(c, W); c = lerp(float3(l, l, l), c, v1.z);\n"
"    if (v4.w != 0) { float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)); c = lerp(float3(l, l, l), c, 1 + v4.w * (1 - (mx - mn))); }\n"
"    c = (c - 0.5) * v1.w + 0.5 + v2.z; c *= float3(1 + v2.y * 0.08, 1, 1 - v2.y * 0.08);\n"
"    if (v4.z > 0) { float3 x = max(c, 0) * (1 + v4.z * 0.6); float3 fm = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14); c = lerp(c, fm, v4.z); }\n"
"    float2 d = i.uv - 0.5; c *= 1 - v2.x * smoothstep(0.25, 0.75, length(d * float2(scr.x / scr.y, 1)) );\n"
"    if (v4.y > 0) c *= 1 - v4.y * 0.35 * (0.5 + 0.5 * sin(i.uv.y * scr.y * 3.14159));\n"
"    if (v3.w > 0) c += (h21(i.uv * scr.xy + frac(t) * 61.7) - 0.5) * v3.w * 0.16;\n"
"    c = saturate(c); }\n"
"  float3 h = hsv(c);\n"
// v5.134 minimalistic white (spr.z): dark -> near white, light -> dark grey; the red selected bar and yellow text stay
"  if (spr.z > 0.5) { float l = dot(c, float3(0.299, 0.587, 0.114));\n"
"    float keep = (h.y > 0.45 && h.z > 0.25 && (h.x < 0.06 || h.x > 0.92 || (h.x > 0.10 && h.x < 0.19))) ? 1 : 0;\n"
"    if (spr.w > 0.5) keep = max(keep, 1 - smoothstep(0.22, 0.27, h.x) * (1 - smoothstep(0.58, 0.62, h.x)) * smoothstep(0.08, 0.2, h.y));\n"
"    float3 w = lerp(float3(0.955, 0.96, 0.965), float3(0.10, 0.11, 0.12), smoothstep(0.04, 0.75, l));\n"
"    w = lerp(w, w * float3(0.93, 0.95, 0.97), 0.5 * (1 - smoothstep(0.0, 0.35, l)));\n"
"    return float4(lerp(w, c, keep), src.a); }\n"
"  if (p.w < 0.5) return float4(c, src.a);\n"
"  float teal = smoothstep(0.22, 0.27, h.x) * (1 - smoothstep(0.58, 0.62, h.x)) * smoothstep(0.10, 0.22, h.y) * smoothstep(0.03, 0.08, h.z);\n"
"  float fire = p.z > 0.5 ? (1 - smoothstep(0.15, 0.19, h.x)) * smoothstep(0.30, 0.45, h.y) * smoothstep(0.10, 0.2, h.z) : 0;\n"
"  float m = max(teal, fire);\n"
"  float3 mapped = pal(saturate((h.z * (0.45 + 1.1 * h.y) - 0.1) * 1.35));\n"
"  c = lerp(c, mapped, m * p.w);\n"
"  int k = (int)(p.y + 0.5); float asp = scr.x / scr.y; float2 q = float2(i.uv.x * asp, i.uv.y);\n"
// Gyne (Halloween, main menu): glides round an ellipse in the right-hand area, smaller and darker at the back, the
// sprite cell picked by the way she moves (cell 0 = front, 45 degrees a cell)
"  if (k == 6 && p.z < 0.5 && spr.y > 0.5) { float a = t * 0.32; float dep = sin(a);\n"
"    float2 pos = float2(asp * 0.785 + cos(a) * 0.17, 0.33 + dep * 0.035 + sin(t * 2.6) * 0.006); float sc = 0.26 * (0.82 + 0.18 * dep);\n"
"    float phi = atan2(-sin(a), cos(a)); int idx = ((int)floor(phi / 0.7853982 + 0.5) + 8) % 8;\n"
"    float2 lp = (q - pos) / sc + 0.5;\n"
"    if (all(lp >= 0) && all(lp <= 1)) { float4 g = G.Sample(L, float2((idx + lp.x) / spr.x, lp.y));\n"
"      float3 gc = g.rgb * (0.78 + 0.22 * dep) * float3(0.92, 0.88, 1.0); c = lerp(c, gc, g.a * 0.88); } }\n"
"  if (k == 8) {\n"   // snow over the whole picture, three layers
"    for (int l = 0; l < 3; l++) { float sc = 18 + l * 14; float2 g = q * sc + float2(sin(t * 0.4 + l) * 0.6, -t * (0.9 + l * 0.5));\n"
"      float2 id = floor(g), f = frac(g) - 0.5; float r = h21(id + l * 7.1);\n"
"      float d = length(f - (float2(h21(id + 3.3), h21(id + 9.1)) - 0.5) * 0.6) - (0.05 + 0.05 * r);\n"
"      c = lerp(c, float3(1, 1, 1), aa(d, 1.5 * sc / scr.y) * step(0.55, r) * 0.8); } }\n"
"  else if (k > 0) { int n = (k == 6) ? (p.z > 0.5 ? 8 : 5) : 6;\n"
"    for (int j = 0; j < n; j++) { float sd = h21(float2(j, k)); float sp = 0.25 + 0.5 * h21(float2(j * 3.1, k));\n"
"      float2 ctr; bool title = p.z > 0.5;\n"
"      float fx = frac((j + 0.5 + 0.35 * (sd - 0.5)) / n), fy = frac(sd * 9.7 + j * 0.37);\n"
"      if (k == 7 || k == 2) { float fall = frac(sd * 3.7 + t * 0.04 * sp * (k == 2 ? -1 : 1));\n"
"        ctr = title ? float2(asp * (j % 2 == 0 ? 0.03 + 0.12 * fx : 0.85 + 0.12 * fx), fall * 1.1 - 0.05)\n"
"                    : float2(asp * (0.64 + 0.30 * fx), 0.12 + 0.45 * fall);\n"
"        ctr.x += sin(t * sp + j) * 0.03; }\n"
"      else { ctr = title ? float2(asp * (0.05 + 0.9 * fx), j % 2 == 0 ? 0.08 + 0.11 * fy : 0.80 + 0.08 * fy)\n"
"                         : float2(asp * (0.66 + 0.27 * fx), 0.15 + 0.38 * fy);\n"
"        ctr += float2(sin(t * sp * 0.7 + j * 2) * 0.025, sin(t * sp * 1.3 + j) * 0.02); }\n"
"      float sdk = (k == 6) ? ((j % 3 == 2) ? 0.75 : 0.25) : sd; float s = (k == 6 && sdk > 0.5) ? 0.075 : 0.06 + 0.02 * sd;\n"
"      float4 d = deco(q - ctr, s, k, t, sdk); c = lerp(c, d.rgb, d.a * 0.92); } }\n"
"  return float4(c, src.a); }\n";

// v5.78.1 (in game 15:28): the hue came from the wrong swizzle (no teal was found); the decorations covered the title
// logo and the panels - now the title's top / bottom bands and the main menu's right-hand column
static bool pp_build(ID3D11Device *dev) {
    HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
    PFN_D3DCOMPILE comp = dc ? (PFN_D3DCOMPILE)GetProcAddress(dc, "D3DCompile") : NULL;
    if (!comp) { logf("post pass: d3dcompiler_47.dll not available - off"); return false; }
    ID3DBlob *vb = NULL, *pb = NULL, *err = NULL;
    if (FAILED(comp(PP_HLSL, sizeof(PP_HLSL) - 1, "cwo_post", NULL, NULL, "vs", "vs_4_0", 0, 0, &vb, &err))) {
        logf("post pass: vertex shader: %s", err ? (const char *)err->GetBufferPointer() : "?"); if (err) err->Release(); return false;
    }
    if (FAILED(comp(PP_HLSL, sizeof(PP_HLSL) - 1, "cwo_post", NULL, NULL, "ps", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pb, &err))) {
        logf("post pass: pixel shader: %s", err ? (const char *)err->GetBufferPointer() : "?"); if (err) err->Release(); vb->Release(); return false;
    }
    bool ok = SUCCEEDED(dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), NULL, &g_pp_vs)) &&
              SUCCEEDED(dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), NULL, &g_pp_ps));
    vb->Release(); pb->Release();
    D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 160; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, NULL, &g_pp_cb)) && SUCCEEDED(dev->CreateSamplerState(&sd, &g_pp_samp));
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    ok = ok && SUCCEEDED(dev->CreateSamplerState(&sd, &g_pp_lin));
    if (ok) {                                                     // v5.78.2 the holiday figures, when the file is there
        wchar_t exe[MAX_PATH], path[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wchar_t *sl = wcsrchr(exe, L'\\');
        if (sl) sl[1] = 0;
        swprintf_s(path, MAX_PATH, L"%sAssets\\Data\\Menu\\cwo_holiday_sprites_off.bin", exe);   // v5.78.4 the Gyne figure removed (owner)
        FILE *f = NULL;
        unsigned hdr[4] = {};
        if (cwo_wfopen(&f, path, L"rb") == 0 && f) {
            if (fread(hdr, 4, 4, f) == 4 && hdr[0] == 0x534f5743 && hdr[1] >= 16 && hdr[1] <= 512 && hdr[2] >= 16 && hdr[2] <= 512 && hdr[3] >= 1 && hdr[3] <= 16) {
                size_t w = (size_t)hdr[1] * hdr[3], h = hdr[2], n = w * h * 4;
                unsigned char *px = (unsigned char *)malloc(n);
                if (px && fread(px, 1, n, f) == n) {
                    D3D11_TEXTURE2D_DESC td = {}; td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
                    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    D3D11_SUBRESOURCE_DATA init = {px, (UINT)(w * 4), 0};
                    ID3D11Texture2D *tx = NULL;
                    if (SUCCEEDED(dev->CreateTexture2D(&td, &init, &tx)) && tx) {
                        if (SUCCEEDED(dev->CreateShaderResourceView(tx, NULL, &g_pp_spr))) g_pp_spr_cells = (int)hdr[3];
                        tx->Release();
                    }
                }
                free(px);
            }
            fclose(f);
        }
        logf("post pass: holiday figures %s (%d cells)", g_pp_spr ? "loaded" : "not there", g_pp_spr_cells);
    }
    if (!ok) logf("post pass: could not create its shaders / buffers - off");
    return ok;
}

struct PpVision { const char *name; float sharpen, blur, sat, contrast, vignette, warm, bright, poster, outline, bloom, grain, chroma, scan, tone, vib; };
static const PpVision PP_VIS[] = {          // v5.83 Visions presets (CwoVisions.txt: a name, or "custom s b sat c v w br")
    {"off", 0, 0, 1, 1, 0, 0, 0}, {"clarity", 0.6f, 0, 1.08f, 1.06f, 0, 0, 0}, {"vivid", 0.25f, 0, 1.35f, 1.12f, 0.1f, 0, 0.01f},
    {"cinematic", 0.2f, 0, 0.9f, 1.12f, 0.45f, 0.6f, -0.02f}, {"soft", 0, 0.6f, 1.05f, 0.96f, 0.15f, 0.3f, 0.02f},
    {"noir", 0.3f, 0, 0, 1.25f, 0.4f, 0, -0.02f, 0, 0.2f, 0, 0.25f},
    // v5.130 owner 2026-11-10: anime-ish and realistic looks, a retro CRT one
    {"anime", 0.3f, 0, 1.3f, 1.04f, 0, 0.1f, 0.08f, 0.4f, 0.55f, 0.4f, 0, 0, 0, 0, 0.3f},
    {"realistic", 0.35f, 0, 0.92f, 1.05f, 0.25f, 0.15f, 0, 0, 0, 0.2f, 0.12f, 0.1f, 0, 0.8f, 0.1f},
    {"retro", 0.1f, 0.2f, 1.1f, 1.06f, 0.25f, 0.2f, 0.06f, 0.3f, 0, 0.2f, 0.3f, 0.35f, 0.5f, 0, 0}};
static PpVision g_pp_vis = {"off", 0, 0, 1, 1, 0, 0, 0};
static volatile LONG g_pp_vis_on;
static void pp_vis_cfg() {                                        // game thread, every 3 s
    static ULONGLONG t;
    ULONGLONG now = GetTickCount64();
    if (now < t) return;
    t = now + 3000;
    wchar_t path[MAX_PATH]; swprintf_s(path, MAX_PATH, L"%sCwoVisions.txt", g_dir);
    FILE *f = NULL; char line[128] = "off";
    if (cwo_wfopen(&f, path, L"r") == 0 && f) { if (!fgets(line, sizeof(line), f)) strcpy_s(line, "off"); fclose(f); }
    for (char *c = line; *c; c++) if (*c == '\r' || *c == '\n') *c = 0;
    PpVision v = PP_VIS[0];
    if (!_strnicmp(line, "custom", 6)) {
        v.name = "custom";
        sscanf_s(line + 6, "%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", &v.sharpen, &v.blur, &v.sat, &v.contrast, &v.vignette, &v.warm, &v.bright,
                 &v.poster, &v.outline, &v.bloom, &v.grain, &v.chroma, &v.scan, &v.tone, &v.vib);   // v5.130: 15 values (7-value lines still read)
    } else for (const PpVision &x : PP_VIS) if (!_stricmp(line, x.name)) v = x;
    static char last[16] = "";
    if (strcmp(last, v.name)) { strcpy_s(last, v.name); logf("visions: %s", v.name); }
    g_pp_vis = v;
    InterlockedExchange(&g_pp_vis_on, strcmp(v.name, "off") != 0 && !ed_online());   // offline only (no online exception)
}
static void pp_pass(IDXGISwapChain *sc) {
    g_pp_mode = g_pp_mode_want;
    LONG mode = g_pp_mode;
    if ((!mode && !g_pp_vis_on) || !g_pp_ps) return;
    ID3D11Device *dev = NULL;
    ID3D11Texture2D *bb = NULL;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void **)&dev)) || !dev) return;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)) || !bb) { dev->Release(); return; }
    ID3D11DeviceContext *ctx = NULL;
    dev->GetImmediateContext(&ctx);
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
    if (!g_pp_copy || td.Width != g_pp_w || td.Height != g_pp_h || td.Format != g_pp_fmt) {
        if (g_pp_srv) { g_pp_srv->Release(); g_pp_srv = NULL; }
        if (g_pp_copy) { g_pp_copy->Release(); g_pp_copy = NULL; }
        D3D11_TEXTURE2D_DESC cd = td; cd.BindFlags = D3D11_BIND_SHADER_RESOURCE; cd.Usage = D3D11_USAGE_DEFAULT; cd.CPUAccessFlags = 0; cd.MiscFlags = 0; cd.MipLevels = 1; cd.ArraySize = 1;
        if (SUCCEEDED(dev->CreateTexture2D(&cd, NULL, &g_pp_copy)) && SUCCEEDED(dev->CreateShaderResourceView(g_pp_copy, NULL, &g_pp_srv))) {
            g_pp_w = td.Width; g_pp_h = td.Height; g_pp_fmt = td.Format;
            logf("post pass: back buffer %u x %u format %d", td.Width, td.Height, (int)td.Format);
        } else { if (g_pp_copy) { g_pp_copy->Release(); g_pp_copy = NULL; } }
    }
    ID3D11RenderTargetView *rtv = NULL;
    if (g_pp_srv && ctx && SUCCEEDED(dev->CreateRenderTargetView(bb, NULL, &rtv)) && rtv) {
        // save what we touch
        ID3D11RenderTargetView *o_rtv[8] = {}; ID3D11DepthStencilView *o_dsv = NULL;
        ctx->OMGetRenderTargets(8, o_rtv, &o_dsv);
        UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; D3D11_VIEWPORT o_vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ctx->RSGetViewports(&nvp, o_vp);
        ID3D11RasterizerState *o_rs = NULL; ctx->RSGetState(&o_rs);
        ID3D11BlendState *o_bs = NULL; float o_bf[4]; UINT o_sm; ctx->OMGetBlendState(&o_bs, o_bf, &o_sm);
        ID3D11DepthStencilState *o_ds = NULL; UINT o_sr; ctx->OMGetDepthStencilState(&o_ds, &o_sr);
        ID3D11InputLayout *o_il = NULL; ctx->IAGetInputLayout(&o_il);
        D3D11_PRIMITIVE_TOPOLOGY o_tp; ctx->IAGetPrimitiveTopology(&o_tp);
        ID3D11VertexShader *o_vs = NULL; ctx->VSGetShader(&o_vs, NULL, NULL);
        ID3D11PixelShader *o_ps = NULL; ctx->PSGetShader(&o_ps, NULL, NULL);
        ID3D11GeometryShader *o_gs = NULL; ctx->GSGetShader(&o_gs, NULL, NULL);
        ID3D11ShaderResourceView *o_srv[2] = {}; ctx->PSGetShaderResources(0, 2, o_srv);
        ID3D11SamplerState *o_smp[2] = {}; ctx->PSGetSamplers(0, 2, o_smp);
        ID3D11Buffer *o_cb = NULL; ctx->PSGetConstantBuffers(0, 1, &o_cb);
        // the pass
        ctx->CopyResource(g_pp_copy, bb);
        int hol = (mode & 63) - 1;
        D3D11_MAPPED_SUBRESOURCE ms;
        bool themed = mode && hol >= 0 && hol < (int)(sizeof(PP_PAL) / sizeof(PP_PAL[0]));
        bool white = g_pp_white && mode;                                   // v5.134 the white menu theme
        if ((themed || g_pp_vis_on || white) && SUCCEEDED(ctx->Map(g_pp_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            float *f = (float *)ms.pData;
            memset(f, 0, 160);
            if (themed) for (int k = 0; k < 3; k++) { f[k * 4] = PP_PAL[hol].c[k][0]; f[k * 4 + 1] = PP_PAL[hol].c[k][1]; f[k * 4 + 2] = PP_PAL[hol].c[k][2]; f[k * 4 + 3] = 1; }
            f[12] = (float)((GetTickCount64() - g_pp_t0) % 3600000) / 1000.0f; f[13] = themed ? (float)PP_PAL[hol].decor : 0.0f; f[14] = (mode & 64) ? 1.0f : 0.0f; f[15] = themed ? 1.0f : 0.0f;
            const PpVision &vv = g_pp_vis;
            f[24] = vv.sharpen; f[25] = vv.blur; f[26] = vv.sat; f[27] = vv.contrast;
            f[28] = vv.vignette; f[29] = vv.warm; f[30] = vv.bright; f[31] = g_pp_vis_on ? 1.0f : 0.0f;
            f[32] = vv.poster; f[33] = vv.outline; f[34] = vv.bloom; f[35] = vv.grain;      // v5.130
            f[36] = vv.chroma; f[37] = vv.scan; f[38] = vv.tone; f[39] = vv.vib;
            f[16] = (float)td.Width; f[17] = (float)td.Height; f[18] = f[19] = 0;
            f[20] = (float)(g_pp_spr_cells > 0 ? g_pp_spr_cells : 1); f[21] = g_pp_spr ? 1.0f : 0.0f; f[22] = white ? 1.0f : 0.0f; f[23] = white && g_pp_white_ui ? 1.0f : 0.0f;
            ctx->Unmap(g_pp_cb, 0);
            D3D11_VIEWPORT vp = {0, 0, (float)td.Width, (float)td.Height, 0, 1};
            ID3D11ShaderResourceView *nul[2] = {};
            ctx->PSSetShaderResources(0, 2, nul);
            ctx->OMSetRenderTargets(1, &rtv, NULL);
            ctx->RSSetViewports(1, &vp);
            ctx->RSSetState(NULL);
            ctx->OMSetBlendState(NULL, NULL, 0xffffffff);
            ctx->OMSetDepthStencilState(NULL, 0);
            ctx->IASetInputLayout(NULL);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(g_pp_vs, NULL, 0);
            ctx->GSSetShader(NULL, NULL, 0);
            ctx->PSSetShader(g_pp_ps, NULL, 0);
            ID3D11ShaderResourceView *use[2] = {g_pp_srv, g_pp_spr};
            ID3D11SamplerState *smp[2] = {g_pp_samp, g_pp_lin};
            ctx->PSSetShaderResources(0, 2, use);
            ctx->PSSetSamplers(0, 2, smp);
            ctx->PSSetConstantBuffers(0, 1, &g_pp_cb);
            ctx->Draw(3, 0);
            ctx->PSSetShaderResources(0, 2, nul);
            if (!g_pp_frames++) logf("post pass: first frame (%s%s)", themed ? HOLIDAYS[hol].name : "no holiday", g_pp_vis_on ? ", Visions" : "");
        }
        // restore
        ctx->OMSetRenderTargets(8, o_rtv, o_dsv);
        ctx->RSSetViewports(nvp, o_vp);
        ctx->RSSetState(o_rs); ctx->OMSetBlendState(o_bs, o_bf, o_sm); ctx->OMSetDepthStencilState(o_ds, o_sr);
        ctx->IASetInputLayout(o_il); ctx->IASetPrimitiveTopology(o_tp);
        ctx->VSSetShader(o_vs, NULL, 0); ctx->PSSetShader(o_ps, NULL, 0); ctx->GSSetShader(o_gs, NULL, 0);
        ctx->PSSetShaderResources(0, 2, o_srv); ctx->PSSetSamplers(0, 2, o_smp); ctx->PSSetConstantBuffers(0, 1, &o_cb);
        for (int k = 0; k < 8; k++) if (o_rtv[k]) o_rtv[k]->Release();
        if (o_dsv) o_dsv->Release();
        IUnknown *rel[] = {o_rs, o_bs, o_ds, o_il, o_vs, o_ps, o_gs, o_srv[0], o_srv[1], o_smp[0], o_smp[1], o_cb};
        for (IUnknown *u : rel) if (u) u->Release();
        rtv->Release();
    }
    if (ctx) ctx->Release();
    bb->Release(); dev->Release();
}

static HRESULT STDMETHODCALLTYPE pp_present(IDXGISwapChain *sc, UINT sync, UINT flags) {
    if (!(flags & DXGI_PRESENT_TEST)) __try { cb_present(sc); pp_pass(sc); ui_present(sc); } __except (EXCEPTION_EXECUTE_HANDLER) { g_pp_mode = 0; }
    return g_pp_present(sc, sync, flags);
}
static HRESULT STDMETHODCALLTYPE pp_present1(IDXGISwapChain1 *sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS *pp) {
    if (!(flags & DXGI_PRESENT_TEST)) __try { cb_present(sc); pp_pass(sc); ui_present(sc); } __except (EXCEPTION_EXECUTE_HANDLER) { g_pp_mode = 0; }
    return g_pp_present1(sc, sync, flags, pp);
}

static void pp_hook() {                                           // CwoOverlayFrame (render thread), once
    if (g_pp_state) return;
    g_pp_state = -1;
    g_pp_t0 = GetTickCount64();
    ID3D11Device *dev = *(ID3D11Device **)(g_base + RVA_D3D_DEVICE);
    if (!dev) { g_pp_state = 0; return; }
    if (!pp_build(dev)) return;
    IDXGIDevice *xd = NULL; IDXGIAdapter *ad = NULL; IDXGIFactory2 *fa = NULL; IDXGISwapChain1 *sc = NULL;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&xd)) && SUCCEEDED(xd->GetAdapter(&ad)) &&
        SUCCEEDED(ad->GetParent(__uuidof(IDXGIFactory2), (void **)&fa))) {
        DXGI_SWAP_CHAIN_DESC1 d = {};
        d.Width = 64; d.Height = 64; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2; d.Scaling = DXGI_SCALING_STRETCH; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        HRESULT hr = fa->CreateSwapChainForComposition(dev, &d, NULL, &sc);
        if (FAILED(hr) || !sc) logf("post pass: CreateSwapChainForComposition failed %08x - off", (unsigned)hr);
    } else logf("post pass: no DXGI factory - off");
    if (sc) {
        void **vt = *(void ***)sc;
        DWORD old;
        if (VirtualProtect(&vt[8], 15 * sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) {
            g_pp_present = (PFN_PRESENT)vt[8]; g_pp_present1 = (PFN_PRESENT1)vt[22];
            vt[8] = (void *)pp_present; vt[22] = (void *)pp_present1;
            VirtualProtect(&vt[8], 15 * sizeof(void *), old, &old);
            g_pp_state = 1;
            logf("post pass: Present hooked (vtable %p)", (void *)vt);
        }
        sc->Release();
    }
    if (fa) fa->Release(); if (ad) ad->Release(); if (xd) xd->Release();
}

static DWORD th_accent() {                                        // v5.79.2 the greeting's headline: the holiday's light colour
    if (!TH_COLOURS || g_th_on < 0 || g_th_on >= (int)(sizeof(PP_PAL) / sizeof(PP_PAL[0]))) return 0xFF80E0D0;   // v5.202.0 no holiday colours
    const PpPalette &pl = PP_PAL[g_th_on];
    return 0xFF000000u | (DWORD)(pl.c[2][0] * 255) << 16 | (DWORD)(pl.c[2][1] * 255) << 8 | (DWORD)(pl.c[2][2] * 255);
}
// the holiday's greeting as a banner (owner: Halloween "a flickering 'Happy Halloween' where the logo is ... spooky"):
// the title screen above the logo, the main menu over its small logo; the game's own font (as th_draw)
static void th_banner() {
    if (g_th_on < 0 || g_in_match || g_th_popup == 2) return;
    int mid = -1;
    __try { ULONG_PTR mm = *(ULONG_PTR *)(g_base + 0x533c58); mid = mm ? *(short *)(mm + 0x105f4) : -1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (mid != 7) return;                                         // v5.78.4 the title screen only (side lists cover the main menu's logo spot)
    static const char *BAN[] = {"HAPPY NEW YEAR", "HAPPY VALENTINE'S DAY", "HAPPY ST. PATRICK'S DAY", "HAPPY EASTER", "HAPPY 4TH OF JULY",
                                "HAPPY HALLOWEEN", "HAPPY THANKSGIVING", "MERRY CHRISTMAS"};
    if (g_th_on >= (int)(sizeof(BAN) / sizeof(BAN[0]))) return;
    const char *msg = BAN[g_th_on];
    ULONGLONG t = GetTickCount64();
    float lvl = 1.0f;
    if (!strcmp(HOLIDAYS[g_th_on].name, "Halloween")) {           // a failing neon sign: dips and blackouts
        unsigned hsh = (unsigned)(t / 90) * 2654435761u;
        hsh ^= hsh >> 13;
        if ((hsh & 31) < 3) return;                               // off
        if ((hsh & 31) < 8) lvl = 0.45f;
        lvl *= 0.85f + 0.15f * (float)sin((double)t * 0.02);
    }
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 200) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    bool title = mid == 7;
    int w = title ? 16 : 12, h = title ? 28 : 20, g = title ? 5 : 4;
    int len = (int)strlen(msg), px = 320 - len * (w + g) / 2, py = title ? 112 : 290;
    const PpPalette &pl = PP_PAL[g_th_on];
    auto col = [&](int k, float m) { return 0xFF000000u | (DWORD)(pl.c[k][0] * 255 * m) << 16 | (DWORD)(pl.c[k][1] * 255 * m) << 8 | (DWORD)(pl.c[k][2] * 255 * m); };
    *space = w / 2; *gap = g; *fh = h; *fw = w;
    // one letter at a time at an even step (v5.79: the font left a gap after a capital I - "VALENTI NE")
    // v5.79.2 (in game: "CHRI STMAS" even one letter at a time): the font's I is thin at the left of its cell - half a step
    auto adv = [&](char ch) { return ch == 'I' ? (w + g) / 2 : w + g; };
    int tw = 0;
    for (int i = 0; i < len; i++) tw += adv(msg[i]);
    px = 320 - tw / 2;
    auto mono = [&](int x, int y, DWORD c) { set_rgba(rgba, c); char one[2] = {0, 0}; for (int i = 0; i < len; i++) { if (msg[i] != ' ') { one[0] = msg[i]; text(x, y, one); } x += adv(msg[i]); } };
    mono(px + 2, py + 2, 0xFF000000);
    mono(px, py, TH_COLOURS ? col(2, lvl) : 0xFFFFFFFFu);        // v5.202.0 white: the words stay, no holiday colour                                    // the light colour (red on the red logo glow could not be read)
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}

// v5.188.25 title intro (owner 2026-11-16 - a fade in the game's font before PRESS START):
// once per game start, the first time the title screen (menu 7) is up: black over the screen, the edition's name and MODS
// ENABLED fade in, hold, fade out, then the title as it is. The game's own text route as th_banner (box glyph \x81\xa1 for
// the black cover, alpha in the colour); stops at once when the title is left (a menu window with rows) or a match starts.
static ULONGLONG g_ti_t0;
static int g_ti_state;                                           // 0 not yet, 1 playing, 2 done
static bool ti_active() { return g_ti_state == 1; }
static void ti_draw() {
    if (g_ti_state == 2 || g_in_match) { if (g_ti_state == 1) g_ti_state = 2; return; }
    int mid = -1;
    __try { ULONG_PTR mm = *(ULONG_PTR *)(g_base + 0x533c58); mid = mm ? *(short *)(mm + 0x105f4) : -1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (mid != 7) { if (g_ti_state == 1) g_ti_state = 2; return; }
    __try {                                                       // LOAD PROFILE shares menu 7: rows on screen = the title is gone
        ULONG_PTR m = *(ULONG_PTR *)(g_base + 0x533c58);
        for (int k = 0; m && k < 16; k++) {
            short rows = *(short *)(m + 0x40 + (ULONG_PTR)k * 0x103c + 0x16);
            if (rows > 0 && rows < 64) { g_ti_state = 2; return; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    ULONGLONG now = GetTickCount64();
    // v5.188.25 (in game 01:35: menu 7 is already up under the boot logos - the intro played unseen 2 s in): it starts 15 s after
    // the first frame the loader draws - the dark gap after the game's disclaimer (clip: disclaimer 12-15 s, logo 16-18 s, PRESS
    // ENTER 19 s) - so the game's own notice is never covered and the title's PRESS START comes right after it
    // v5.188.25 (in game BFR 01:48: the loader joins later there - launcher checks - so 15 s from its first frame fell after the
    // title): the anchor is the game process's own start (GetProcessTimes); both editions show the disclaimer's dark gap at
    // about 17.5 s and PRESS ENTER at about 22 s of the process's life (clips 01:40 MODS / 01:48 BFR)
    static LONGLONG s_age0 = -1;                                  // ms of process life at the loader's tick s_t0
    static ULONGLONG s_t0;
    if (s_age0 < 0) {
        FILETIME c, e, k, u, n;
        GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
        GetSystemTimeAsFileTime(&n);
        ULARGE_INTEGER ci, ni; ci.LowPart = c.dwLowDateTime; ci.HighPart = c.dwHighDateTime; ni.LowPart = n.dwLowDateTime; ni.HighPart = n.dwHighDateTime;
        s_age0 = ni.QuadPart > ci.QuadPart ? (LONGLONG)((ni.QuadPart - ci.QuadPart) / 10000) : 0;
        s_t0 = now;
        logf("title intro: the game process is %lld ms old at the loader's first title frame", s_age0);
    }
    if (g_ti_state == 0 && s_age0 + (LONGLONG)(now - s_t0) < 17500) return;
    if (g_ti_state == 0) { g_ti_state = 1; g_ti_t0 = now; logf("title intro: %s - MODS ENABLED", g_bfr_edition ? "BATTLE FOR REALITY" : "PHANTOM DUST"); }
    const int T_IN = 900, T_HOLD = 2600, T_OUT = 3400, T_END = 3700;
    int t = (int)(now - g_ti_t0);
    if (t >= T_END) { g_ti_state = 2; return; }
    float txt = t < T_IN ? t / (float)T_IN : t < T_HOLD ? 1.0f : t < T_OUT ? 1.0f - (t - T_HOLD) / (float)(T_OUT - T_HOLD) : 0.0f;
    float blk = 1.0f;                                             // fully black (overlapping boxes at half alpha would show a grid)
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 520) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    auto a8 = [](float f) { int v = (int)(f * 255.0f); return (DWORD)(v < 0 ? 0 : v > 255 ? 255 : v) << 24; };
    *space = 0; *gap = 0; *fh = 64; *fw = 48;                   // the black cover: box glyphs over 640 x 480
    set_rgba(rgba, a8(blk) | 0x000000);
    // in game 01:37: 64-high steps left light seams between the rows and the widescreen sides showed - the boxes now overlap
    // (40 x 36 steps of 48 x 64 glyphs, about 420 of the 1000-glyph queue) and reach past both sides of the 640-wide frame
    for (int by = -24; by < 504; by += 40)
        for (int bx = -220; bx < 860; bx += 36) text(bx, by, "\x81\xa1");
    auto line = [&](const char *msg, int w, int h, int g, int py, DWORD rgb) {
        auto adv = [&](char ch) { return ch == 'I' ? (w + g) / 2 : w + g; };
        int len = (int)strlen(msg), tw = 0;
        for (int i = 0; i < len; i++) tw += adv(msg[i]);
        int px = 320 - tw / 2;
        *space = w / 2; *gap = g; *fh = h; *fw = w;
        char one[2] = {0, 0};
        for (int pass = 0; pass < 2; pass++) {                    // a shadow, then the colour
            set_rgba(rgba, a8(txt) | (pass ? rgb : 0x000000));
            int x = px + (pass ? 0 : 2), y = py + (pass ? 0 : 2);
            for (int i = 0; i < len; i++) { if (msg[i] != ' ') { one[0] = msg[i]; text(x, y, one); } x += adv(msg[i]); }
        }
    };
    if (txt > 0.01f) {
        line(g_bfr_edition ? "BATTLE FOR REALITY" : "PHANTOM DUST", 18, 32, 6, 196, 0xF0C060);
        line("MODS ENABLED", 12, 20, 5, 252, 0xE8E8E8);
    }
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}
