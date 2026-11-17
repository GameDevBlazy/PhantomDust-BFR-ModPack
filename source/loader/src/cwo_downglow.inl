// ---- v5.103 a glow on downed fighters (owner 2026-11-03 - "a glow on each downed fighter that stays until revived" - and
// "let that be a toggle option from the host online in the BFR option in the multiplayer lobby"; AGENTS.md "Downed-body glow
// online": display only, each machine's own drawing).
// Game facts (MODLOG; cwo_realistic.inl, cwo_outfitfx.inl): a fighter is downed while +0x12F04 == 0x1B (FUN_14025c670 takes it
// out when +0x12F08 runs out); effects are the game's own instances (start / loop / move / stop, cwo_sfx.inl), the effect of
// skill N = g.effect_of(its profile, N), loaded on demand with FUN_140189e80(effect manager, request, 0) as the model viewer does.
// The glow = Psycho Wave's hit effect (skill 1, part 3 - Create an Outfit's "Psychic" preset) on the body, looped, following it.
// The switch: offline this machine's own (g_dg_on); online the host's - its bit 0x4000 in the BFR player-info bytes (cwo_bfrnet.inl).
static unsigned g_dg_h[4] = {FX_NONE, FX_NONE, FX_NONE, FX_NONE};
static int g_dg_fail[4];
static bool g_dg_loaded;
static bool dg_active() {
    if (!ed_online() || bn_host()) return g_dg_on;
    return (bn_member_value(0) & 0x4000u) != 0;                  // a guest follows the host
}
// v5.112 (owner 2026-11-07; "right now it's just showing a purple ball that's flickering in and out"). Game facts (Ghidra full pass 065 +
// capstone, clean PDUWP 9ba7411b): FUN_14024f2f0 (every frame, each fighter) asks FUN_14024d4d0(fighter + 0x40, 8) - status
// flag 8, "can't be locked on", which a downed fighter carries - and then sets (0) or clears (else) bit 0x20 of the
// fighter's aura entry (*0x1404bbd50 + 0xc0 + fighter * 0x120); FUN_14019b520 draws a fighter's aura only with that bit.
// The call at 0x14024f33b (e8 90 e1 ff ff) now goes to dk_check: the game's own answer, except "not hidden" for a downed
// fighter (+0x24 == 0, still in: +0x20) whose max life (+0x50) is above 1 while the Downed Glow switch is on. Only this
// call changes; the status itself (lock-on) is the game's.
static int g_dk_state;                                            // 1 placed, -1 refused
static const ULONG_PTR DK_SITE = 0x24f33b, DK_CHECK = 0x24d4d0;
typedef int(__fastcall *PFN_DK)(ULONG_PTR, int);
static int __fastcall dk_check(ULONG_PTR bd, int flag) {
    int r = ((PFN_DK)(g_base + DK_CHECK))(bd, flag);
    if (!r || flag != 8) return r;
    __try {
        ULONG_PTR f = bd - 0x40;
        if (g_in_match && dg_active() && *(int *)(f + 0x20) && *(int *)(f + 0x24) == 0 && *(int *)(f + 0x50) > 1) return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}
static void dk_install() {                                        // DllMain, with the other code patches (all or nothing)
    static const unsigned char want[5] = {0xE8, 0x90, 0xE1, 0xFF, 0xFF};
    unsigned char *site = (unsigned char *)(g_base + DK_SITE);
    if (memcmp(site, want, 5)) { g_dk_state = -1; logf("downed aura: the aura check is not the expected code - not placed"); return; }
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    long long d = (long long)page - (long long)(g_base + DK_SITE + 5);
    if (!page || d < INT_MIN || d > INT_MAX) { g_dk_state = -1; logf("downed aura: no memory within reach"); return; }
    unsigned char th[14] = {0xFF, 0x25, 0, 0, 0, 0};
    ULONG_PTR fn = (ULONG_PTR)&dk_check;
    memcpy(th + 6, &fn, 8);
    memcpy((void *)page, th, sizeof(th));
    unsigned char c[5] = {0xE8};
    int rel = (int)d;
    memcpy(c + 1, &rel, 4);
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) { g_dk_state = -1; return; }
    memcpy(site, c, 5);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    g_dk_state = 1;
    logf("downed aura: a downed fighter keeps its own aura while its max life is above 1 (call at %p -> %p)", (void *)site, (void *)page);
}
static void dg_step() {                                           // game thread, every drawn frame
    FxGame g;
    if (!fx_game(&g)) return;
    bool on = g_in_match && dg_active() && g_dk_state != 1;          // v5.112 with the aura kept, no effect ball
    if (!g_in_match) g_dg_loaded = false;
    __try {
        ULONG_PTR *acts = cwo_actors();
        for (int p = 0; p < 4; p++) {
            ULONG_PTR a = acts[p];
            bool down = on && a && *(int *)(a + 0x20) && *(int *)(a + 0x24) == 0;   // v5.104 downed = +0x24 0 (standing 1), as cwo_cpurevive.inl
            if (!down) {
                if (g_dg_h[p] != FX_NONE && fx_alive(g, g_dg_h[p])) g.stop(g_dg_h[p]);
                g_dg_h[p] = FX_NONE;
                if (!g_in_match || (a && *(int *)(a + 0x20) && *(int *)(a + 0x24) != 0)) g_dg_fail[p] = 0;
                continue;
            }
            const float *pos = (const float *)(a + ACTOR_POS);
            // v5.179.4: the game keeps a pointer to the pinned matrix (sfx_pin, slot +0x40) - one per fighter that lives on
            static __declspec(align(16)) float s_dg_m[4][16];
            float *m = s_dg_m[p];
            memset(m, 0, 64);
            m[0] = m[5] = m[10] = 0.32f; m[15] = 1.0f;
            m[12] = pos[0]; m[13] = pos[1] + 5.0f; m[14] = pos[2];
            if (fx_alive(g, g_dg_h[p])) { g.move(g_dg_h[p], m); sfx_pin(g, g_dg_h[p], m); continue; }
            if (g_dg_fail[p] > 300) continue;
            short prof = *(short *)(g.skills + 0x14 + 1 * 0x90);
            int req = prof > 0 ? g.effect_of(prof, 1) : 0;
            if (req <= 0) { g_dg_fail[p] = 999; continue; }
            unsigned h = g.start(g.mgr, *(int *)(a + 0x14), req, 3, m, 0);
            if (h == FX_NONE) {
                if (++g_dg_fail[p] == 30 && !g_dg_loaded) {           // not loaded in this match: load it as the viewer does
                    typedef INT64 (__fastcall *PFN_FX_LOAD)(ULONG_PTR, int, int);
                    ((PFN_FX_LOAD)(g_base + 0x189e80))(g.mgr, req, 0);
                    g_dg_loaded = true;
                    logf("downed glow: effect %d loaded", req);
                }
                continue;
            }
            g.loop(h);
            sfx_pin(g, h, m);
            g_dg_h[p] = h; g_dg_fail[p] = 0;
            logf("downed glow: fighter %d is down", p + 1);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
