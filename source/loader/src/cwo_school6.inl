// ---- v5.159 the Illusion school = school 6 (owner 2026-11-13 - a 6th school, smoke silver aura that
// flickers, the cracked-eye mark, 75 skills; design 07_Combat_and_Skills/illusion_school_2026-11-13).
// Game facts (PD Ghidra full pass 065 via pd_fn.py + capstone on PDUWP 9ba7411b, 2026-11-13; MODLOG):
//  - every reader of a skill's school (record +0x12) was checked for 1-5 assumptions:
//    * the aura pick FUN_14024d9d0 counts the 30 battle cards (0x140584f80 + player * 400) per school in an int[8] (room for 6)
//      and picks the most among 1-5 (ties to the earlier school); school 6 is counted but never picked -> the colour is set
//      here, through native 2.46 (cwo_outfitfx.inl ofx_aura_native, which already sets per-outfit colours);
//    * the Lab's stock lists FUN_140172910: type lists by +0x0E, school counts param_4[school - 1] in 6 ints (index 5 fits);
//      the school filter bit (lab + 0x4f6c >> school) & 1 - bit 6 is never set, so Illusion skills always show;
//    * the Lab's school mark: sprite = *(lab + 0x4f44 + school * 4) at 6 sites (FUN_140168410, FUN_140170330 x2,
//      FUN_140171970 x2, FUN_140173b30), each `movsxd rcx, dword [base + rax*4 + 0x4f44]` with rax = school; the table holds
//      schools 0-5 (0x0f, 0x15, 0x14, 0x12, 0x10, 0x11) and +0x4f5c after it is the Lab's player (-1..3) - so school 6
//      would draw sprite -1..3. Each site now calls a stub: school 6 -> WusMn handle slot 250, which the loader keeps = the
//      Illusion mark's handle (sprite "detail_icon_type_il" of Menu/Labo.alr, build_illusion_sprite.py) while Labo is loaded,
//      else ... (superseded by v5.159.3, below); v5.159.6: the stub always gives sprite 19 - never sprite 15, the Aura
//      Particle school's own mark (owner 2026-11-13: the Aura Particle is its own school; nothing else belongs to it);
//    * the skill-use school ban FUN_140262d80 tests status 0x28 + school: status 0x2e (the game's last, of 0x2f) = the
//      Illusion ban (Disbelief, environment status +0x54 = 46);
//    * the arsenal counts FUN_14015a8d0 / FUN_14015a9d0 / Arsenal Edit FUN_140159db0 keep [school] and [6 + type] in one
//      array: school 6 lands on [6] (the no-type count) - a count only, no memory outside the array (see the MODLOG note);
//    * the single-school flag FUN_14015b830 (an Illusion-only arsenal is single-school), the sort FUN_140172a80, the script
//      getter FUN_14024c220 case 0x28 and the controller call FUN_1400b9580 take any value.
// Offline only for the colour (AGENTS.md: created skills stay offline); the mark stub is display only.
static const ULONG_PTR S6_SITES[6] = {0x16883e, 0x17075d, 0x170945, 0x17224b, 0x1724e3, 0x174203};
static const unsigned char S6_BASE_MODRM[6] = {0x82, 0x87, 0x87, 0x86, 0x86, 0x86};   // [rdx / rdi / rsi + rax*4 + disp]
static const int S6_SPRITE = 201, S6_PLAIN = 19;   // sprite 19: the stock plain box until the mark is in it
static const ULONG_PTR S6_SPRITE_MGR = 0x527c78;                  // *0x140527c78 the sprite manager (cwo_portrait.inl)
static bool ed_online();                                        // cwo_edgar.inl
static int g_s6_state;                                          // 1 on, -1 refused
static volatile int *g_s6_sprite;                               // in the stub page: the sprite school 6 draws

static void s6_site_bytes(int i, unsigned char *b) {             // the stock instruction at site i
    unsigned char v[8] = {0x48, 0x63, 0x8c, S6_BASE_MODRM[i], 0x44, 0x4f, 0x00, 0x00};
    memcpy(b, v, 8);
}
// one stub: pushfq; cmp rax, 6; jne L; popfq; mov ecx, [rip + data]; ret; L: popfq; <the stock instruction>; ret
static int s6_write_stub(unsigned char *c, int i, ULONG_PTR data) {
    int n = 0;
    c[n++] = 0x9C;
    c[n++] = 0x48; c[n++] = 0x83; c[n++] = 0xF8; c[n++] = 0x06;
    c[n++] = 0x75; c[n++] = 0x08;
    c[n++] = 0x9D;
    c[n++] = 0x8B; c[n++] = 0x0D;
    *(int *)(c + n) = (int)((LONG_PTR)data - (LONG_PTR)(c + n + 4)); n += 4;
    c[n++] = 0xC3;
    c[n++] = 0x9D;
    s6_site_bytes(i, c + n); n += 8;
    c[n++] = 0xC3;
    return n;
}
static int s6_patch(ULONG_PTR base, ULONG_PTR page, char *why, size_t nwhy) {   // all six or none
    for (int i = 0; i < 6; i++) {
        unsigned char want[8];
        s6_site_bytes(i, want);
        if (memcmp((void *)(base + S6_SITES[i]), want, 8)) { sprintf_s(why, nwhy, "site +0x%llx differs", (unsigned long long)S6_SITES[i]); return -1; }
        LONG_PTR d = (LONG_PTR)(page + 0x40 * i) - (LONG_PTR)(base + S6_SITES[i] + 5);
        if (d < INT_MIN || d > INT_MAX) { sprintf_s(why, nwhy, "out of reach"); return -1; }
    }
    ULONG_PTR data = page + 0x800;
    *(volatile int *)data = S6_PLAIN;
    for (int i = 0; i < 6; i++) s6_write_stub((unsigned char *)(page + 0x40 * i), i, data);
    for (int i = 0; i < 6; i++) {
        unsigned char *s = (unsigned char *)(base + S6_SITES[i]);
        DWORD old;
        if (!VirtualProtect(s, 8, PAGE_EXECUTE_READWRITE, &old)) { sprintf_s(why, nwhy, "not writable"); return -1; }
        int rel = (int)((LONG_PTR)(page + 0x40 * i) - (LONG_PTR)(s + 5));
        unsigned char b[8] = {0xE8, 0, 0, 0, 0, 0x0F, 0x1F, 0x00};   // call stub; nop dword [rax]
        memcpy(b + 1, &rel, 4);
        memcpy(s, b, 8);
        VirtualProtect(s, 8, old, &old);
        FlushInstructionCache(GetCurrentProcess(), s, 8);
    }
    FlushInstructionCache(GetCurrentProcess(), (void *)page, 0x1000);
    g_s6_sprite = (volatile int *)data;
    return 1;
}
static void s6_install() {                                      // worker start (host checked)
    if (g_s6_state) return;
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    char why[64] = "";
    if (!page || s6_patch(g_base, page, why, sizeof(why)) != 1) { g_s6_state = -1; logf("school 6: the Lab's school marks left alone (%s)", page ? why : "no page"); return; }
    g_s6_state = 1;
    logf("school 6: the Lab's 6 school-mark reads go through %p (school 6 -> the Illusion mark once WusMnEn holds it)", (void *)page);
}

// ---- the Illusion mark (v5.159.3, after the in-game checks of 2026-11-13):
// Game facts (Ghidra full pass 065 + reads of the running game): WusMnEn is read at boot into a fixed buffer
// (FUN_140167df0: manager + 0x580 = *0x14052def0; the next region starts 0x6e000 bytes on, FUN_14015f650) before the loader's
// file redirect in a "late" launch, so a sprite added to the file was there in one launch and not the next; a sprite in
// another image (Labo) is drawn in that image's layer, under the Lab's rows. So the mark goes into WusMn's own texture 2
// (WusMnEn0600, 256 x 512 DXT3 - the stock marks' texture) in video memory: the loader puts a DEFAULT-usage copy of that
// texture in place of the game's immutable one (as cwo_portrait.inl does for the level-list pictures) and writes the mark
// into sprite 19 "detail_icon_type_no" (x 68, y 240, 34 x 20; not in the school table 0x4f44, not read by any Lab code by
// number), and the school-6 stub draws sprite 19. Until the copy is in place sprite 19 is the stock plain box - an Illusion row
// never shows the Aura Particle school's mark (sprite 15).
// Image data (FUN_140197750 / FUN_14001de70): the set's .alr at *(manager + 0x308 + 8 * set): chunk 0x15 at + u32[1],
// u16 count at +8, 28-byte entries from +0xc {i32 -1 when created, object +4 (8)}; object +8 pixels, +0x10 packed,
// +0x18 its entry, +0x20 ID3D11Texture2D, +0x28 its view.
#include "cwo_school6_mark.h"
static const int S6_MARK_SPR = 19, S6_TEX = 2, S6_TX = 68, S6_TY = 240;
static unsigned char *g_s6_obj;
static void *g_s6_mine;
static void s6_tex_step() {                                       // CwoOverlayFrame (the render thread, as portrait_step)
    if (g_s6_state != 1) return;
    __try {
        char *mgr = *(char **)(g_base + S6_SPRITE_MGR);
        ID3D11Device *dev = *(ID3D11Device **)(g_base + RVA_D3D_DEVICE);
        ID3D11DeviceContext *ctx = *(ID3D11DeviceContext **)(g_base + RVA_D3D_CTX);
        if (!mgr || !dev || !ctx) return;
        int set = -1;
        for (int s = 0; s < 10 && set < 0; s++) { const char *nm = mgr + 0xc * (s + 1); if (nm[0] && !_strnicmp(nm, "WusMn", 5)) set = s; }
        if (set < 0) { g_s6_obj = NULL; g_s6_mine = NULL; return; }
        const unsigned char *alr = *(const unsigned char **)(mgr + 0x308 + 8 * set);
        if (!alr || *(const unsigned *)alr != 0x11) return;
        const unsigned char *ch = alr + *(const unsigned *)(alr + 4);
        if (*(const unsigned *)ch != 0x15 || *(const unsigned short *)(ch + 8) <= S6_TEX) return;
        const unsigned char *e = ch + 0xc + 28 * S6_TEX;
        if (*(const int *)e != -1) return;
        unsigned char *obj = *(unsigned char **)(e + 4);
        if (!obj || *(const unsigned char **)(obj + 0x18) != e) return;
        unsigned packed = *(const unsigned *)(obj + 0x10);
        if ((1 << ((packed >> 20) & 15)) != 256 || (1 << ((packed >> 24) & 15)) != 512 || ((packed >> 8) & 0xff) != 0x0e) return;
        if (obj == g_s6_obj && *(void **)(obj + 0x20) == g_s6_mine) return;   // ours, mark written
        const unsigned char *pixels = *(const unsigned char **)(obj + 8);
        if (!pixels) return;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = 256; td.Height = 512; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_BC2_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = pixels; init.SysMemPitch = 256 / 4 * 16;
        ID3D11Texture2D *tex = NULL;
        ID3D11ShaderResourceView *srv = NULL;
        if (FAILED(dev->CreateTexture2D(&td, &init, &tex)) || !tex) { logf("school 6: CreateTexture2D failed - the star stays"); g_s6_state = 2; return; }
        D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
        vd.Format = td.Format; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1;
        if (FAILED(dev->CreateShaderResourceView(tex, &vd, &srv)) || !srv) { tex->Release(); logf("school 6: CreateShaderResourceView failed - the star stays"); g_s6_state = 2; return; }
        D3D11_BOX box = {(UINT)S6_TX, (UINT)S6_TY, 0, (UINT)(S6_TX + S6_MARK_W), (UINT)(S6_TY + S6_MARK_H), 1};
        ctx->UpdateSubresource(tex, 0, &box, S6_MARK_DXT3, S6_MARK_W / 4 * 16, 0);
        IUnknown *old_tex = *(IUnknown **)(obj + 0x20), *old_srv = *(IUnknown **)(obj + 0x28);
        *(ID3D11Texture2D **)(obj + 0x20) = tex;
        *(ID3D11ShaderResourceView **)(obj + 0x28) = srv;
        if (old_srv) old_srv->Release();
        if (old_tex) old_tex->Release();
        g_s6_obj = obj; g_s6_mine = tex;
        logf("school 6: WusMn texture %d (object %p) - own copy %p with the Illusion mark at sprite %d", S6_TEX, (void *)obj, (void *)tex, S6_MARK_SPR);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void s6_step() {                                            // every frame: the stub draws the mark once it is in place
    if (g_s6_state != 1 && g_s6_state != 2) return;
    int want = S6_MARK_SPR;                                       // the mark once the copy is in, else the stock plain box
    static int said = -1;
    int now = g_s6_state == 1 && g_s6_mine ? 1 : 0;
    if (*g_s6_sprite != want) *g_s6_sprite = want;
    if (said != now) {
        said = now;
        logf("school 6: the Lab draws %s for Illusion skills (sprite %d)", now ? "the Illusion mark" : "the plain box (the mark is not in place)", want);
    }
}

// ---- the aura: an arsenal with more Illusion cards than any other school's glows smoke silver, flickering
static int g_s6_said[8];
static DWORD s6_aura_rgb(int k) {
    if (k < 0 || k >= 4 || !g_in_match || ed_online()) return 0;
    __try {
        const int *deck = (const int *)(g_base + 0x584f80 + 400 * (ULONG_PTR)k);
        const char *recs = *(const char **)(g_base + RVA_SKILL_RECS);
        if (!recs) return 0;
        int c[8] = {0};
        for (int i = 0; i < 30; i++) {
            int id = deck[i];
            if (id <= 0 || id >= 1200) continue;
            short s = *(const short *)(recs + 0x90 * (size_t)id + 0x12);
            if (s >= 0 && s < 8) c[s]++;
        }
        int best = 0;
        for (int s = 1; s <= 5; s++) if (c[s] > best) best = c[s];
        bool ill = c[6] > best;                                   // the game's rule: the most cards; a tie keeps the earlier school
        if (g_s6_said[k] != (ill ? 2 : 1)) {
            g_s6_said[k] = ill ? 2 : 1;
            logf("school 6: aura slot %d - Illusion %d cards, best other school %d -> %s", k, c[6], best, ill ? "smoke silver" : "the game's colour");
        }
        if (!ill) return 0;
        // smoke silver that flickers: a slow shimmer, a faster ripple, and a short fade-out now and then
        double t = (double)GetTickCount64() / 1000.0;
        double b = 0.74 + 0.14 * sin(t * 4.3 + k) + 0.08 * sin(t * 11.7 + 1.3 * k);
        double ph = fmod(t + 0.37 * k, 1.9);
        if (ph < 0.14) b *= 0.35 + 0.65 * (ph / 0.14 < 0.5 ? 1.0 - 2.0 * ph / 0.14 : 2.0 * ph / 0.14 - 1.0);
        if (b < 0.2) b = 0.2;
        if (b > 1.0) b = 1.0;
        int r = (int)(200 * b), g = (int)(206 * b), bl = (int)(218 * b);
        return 0xFF000000u | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)bl;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
static void s6_match_reset() { for (int k = 0; k < 8; k++) g_s6_said[k] = 0; }

static int s6_self_test() {                                      // the stubs on a stand-in: school 6 -> the sprite, others -> the table
    unsigned char *img = (unsigned char *)VirtualAlloc(NULL, 0x180000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!img) return -90;
    for (int i = 0; i < 6; i++) s6_site_bytes(i, img + S6_SITES[i]);
    ULONG_PTR page = (ULONG_PTR)img + 0x178000;
    char why[64] = "";
    volatile int *keep = g_s6_sprite;
    bool ok = s6_patch((ULONG_PTR)img, page, why, sizeof(why)) == 1;
    for (int i = 0; ok && i < 6; i++) ok = img[S6_SITES[i]] == 0xE8 && (ULONG_PTR)(img + S6_SITES[i] + 5 + *(int *)(img + S6_SITES[i] + 1)) == page + 0x40 * i;
    // run stub 0 (base rdx) as a function would: rax = school, rdx = a table with 0x4f44 + 4 * s = 100 + s
    static int tab[0x5000 / 4];
    for (int s = 0; s < 6; s++) tab[(0x4f44 >> 2) + s] = 100 + s;
    tab[(0x4f44 >> 2) + 6] = -1;                                  // what the stock code would read for school 6 (the player)
    typedef INT64(*PFN_T)(void);
    // a tiny caller: mov rdx, tab; mov rax, s; call stub; mov rax, rcx; ret
    unsigned char *call = img + 0x179000;
    int got[7];
    for (int s = 0; ok && s <= 6; s++) {
        int n = 0;
        call[n++] = 0x48; call[n++] = 0xBA; *(ULONG_PTR *)(call + n) = (ULONG_PTR)tab; n += 8;
        call[n++] = 0x48; call[n++] = 0xC7; call[n++] = 0xC0; *(int *)(call + n) = s; n += 4;
        call[n++] = 0xE8; *(int *)(call + n) = (int)((LONG_PTR)page - (LONG_PTR)(call + n + 4)); n += 4;
        call[n++] = 0x48; call[n++] = 0x89; call[n++] = 0xC8;            // mov rax, rcx
        call[n++] = 0xC3;
        FlushInstructionCache(GetCurrentProcess(), call, n);
        got[s] = (int)((PFN_T)(void *)call)();
    }
    *(volatile int *)(page + 0x800) = S6_SPRITE;
    if (ok) got[6] = (int)((PFN_T)(void *)call)();
    for (int s = 0; ok && s < 6; s++) ok = got[s] == 100 + s;
    ok = ok && got[6] == S6_SPRITE;
    char why2[64] = "";
    bool refused = s6_patch((ULONG_PTR)img, page, why2, sizeof(why2)) != 1;   // the sites are ours now: refused
    VirtualFree(img, 0, MEM_RELEASE);
    g_s6_sprite = keep;
    logf("self-test: school 6 marks %s (schools 0-5 from the table, 6 -> sprite %d), changed site refused %s", ok ? "ok" : "FAILED", S6_SPRITE, refused ? "ok" : "FAILED");
    return ok && refused ? 1 : -91;
}
