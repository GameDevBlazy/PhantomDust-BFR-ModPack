// ---- v5.148.1 effect size per skill (owner 2026-11-10). CwoFxSize.txt: lines "skill percent" (100-1000).
// Game facts (Ghidra full pass 065, recorded): effect instances = 256 slots of 0xF0 from the manager +0x60 (FUN_14018a040 takes
// the first slot whose +0 is 0, up to +0xF060); slot +0 = the handle, +0x30 = the loaded effect's data (the loader table
// *0x1405BB228: entries of 0x88 from +0xC0, count +0xC8, +8 request, +0x18 data); a track-0 instance (+0xDC = 1) draws with
// the rows of *(slot + 0x40) at slot +0xD0, any other with slot +0x50 (FUN_14018d440 moves it); a skill's effect request =
// FUN_PFN effect_of(profile +0x14 of its record). Each instance of that skill's effect is pinned (g.move + sfx_pin, as the
// stage effects) to its own first matrix with the three axes scaled: meshes and emitters grow, it stays where it started.
// Offline only.
static int g_fz_n = -1, g_fz_skill[16], g_fz_pct[16];
struct FzInst { unsigned h; bool keep; float m[16]; };
// v5.148.4 (owner; log 16:47: the crash drew effect 8002, the
// recoloured COPY of Photon Burst's effect): no recoloured copy any more, so Mega Photon Burst shares Photon Burst's effect;
// an instance is grown only when it first shows within 1.5 s of a new flying record carrying the sized skill's number
// (fighter + 0x4E0, 48 x 0x1F0, +0x10 state, +0x14 skill) - plain Photon Burst keeps its own size
static ULONGLONG g_fz_cast[16];
static unsigned char g_fz_seen[8][48];
static FzInst g_fz[32];

static void fz_load() {
    g_fz_n = 0;
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%sCwoFxSize.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[128];
    while (g_fz_n < 16 && fgets(line, sizeof(line), f)) {
        int sk, pc;
        if (line[0] == '#' || sscanf_s(line, "%d %d", &sk, &pc) != 2) continue;
        if (sk < 1 || sk > 752 || pc < 100 || pc > 1000) { logf("effect size: line refused: %.40s", line); continue; }
        g_fz_skill[g_fz_n] = sk; g_fz_pct[g_fz_n] = pc; g_fz_n++;
        logf("effect size: skill %d drawn at %d %%", sk, pc);
    }
    fclose(f);
}

static void fz_step() {                                             // game thread, every drawn frame (next to spx_step)
    if (g_fz_n < 0) fz_load();
    if (g_fz_n <= 0 || !g_in_match || ed_online()) return;
    FxGame g;
    if (!fx_game(&g)) return;
    __try {
        ULONG_PTR t = *(ULONG_PTR *)(g_base + RVA_EFT_TABLE);
        if (!t) return;
        for (int i = 0; i < 32; i++) if (g_fz[i].h && !fx_alive(g, g_fz[i].h)) g_fz[i].h = 0;
        ULONGLONG now = GetTickCount64();
        for (int p = 0; p < 4; p++) {
            ULONG_PTR a = cwo_actors()[p];
            if (!a || !*(int *)(a + 0x20)) continue;
            for (int q = 0; q < 48; q++) {
                ULONG_PTR sl = a + 0x4E0 + 0x1F0 * (ULONG_PTR)q;
                int st = *(int *)(sl + 0x10), sk = *(int *)(sl + 0x14);
                bool hit = false;
                for (int k = 0; k < g_fz_n; k++) if (st && sk == g_fz_skill[k]) {
                    hit = true;
                    if (!g_fz_seen[p][q]) { g_fz_cast[k] = now; logf("effect size: skill %d cast by fighter %d", sk, p + 1); }
                    // v5.148.14 (owner 17:0x: "the max photon burst did in fact make collision with the other player, even though he
                    // wasn't close, but the visual did not reflect that"; 17:06 diagnostic: no effect-manager instance of request 110 after
                    // the cast): the burst is the skill object's own visual, drawn from its 4 x 4 matrix at record +0x80 (FUN_140263ce0;
                    // the flight updates it). Its three axes set to the size every frame (normalised first, so it never grows on).
                    float *m = (float *)(sl + 0x80), sc = g_fz_pct[k] / 100.0f;
                    for (int r = 0; r < 3; r++) {
                        float l = sqrtf(m[r * 4] * m[r * 4] + m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2]);
                        if (l > 1e-4f) for (int c = 0; c < 3; c++) m[r * 4 + c] *= sc / l;
                    }
                }
                g_fz_seen[p][q] = hit;
            }
        }
        for (int k = 0; k < g_fz_n; k++) {
            int sk = g_fz_skill[k];
            short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)sk * 0x90);
            int req = prof > 0 ? g.effect_of(prof, sk) : 0;
            if (req <= 0) continue;
            char *dp[4]; int nd = 0;
            unsigned n = *(unsigned *)(t + 0xC8);
            ULONG_PTR e = *(ULONG_PTR *)(t + 0xC0);
            for (unsigned i = 0; e && i < n && i < 4096 && nd < 4; i++, e += 0x88)
                if (*(int *)(e + 8) == req && *(char **)(e + 0x18)) dp[nd++] = *(char **)(e + 0x18);
            if (!nd) continue;
            if (now - g_fz_cast[k] < 2000) {                            // v5.148.6 diagnostic: what the new instances after a cast are
                static unsigned logged[64]; static int nlog;
                for (int q = 0; q < 256 && nlog < 64; q++) {
                    unsigned char *sl = (unsigned char *)(g.mgr + 0x60 + (ULONG_PTR)q * 0xF0);
                    unsigned hh = *(unsigned *)sl;
                    if (!hh) continue;
                    bool old = false; for (int j = 0; j < nlog; j++) old |= logged[j] == hh;
                    if (old) continue;
                    logged[nlog++] = hh;
                    char hx[200]; size_t o = 0;
                    for (int j = 0; j < 0x40 && o < sizeof(hx) - 3; j += 4) o += sprintf_s(hx + o, sizeof(hx) - o, "%08X ", *(unsigned *)(sl + j));
                    logf("effect size: after cast %d - slot %d handle %08X data %p (want %p, req %d) dc %d: %s", sk, q, hh, *(void **)(sl + 0x30), dp[0], req, *(int *)(sl + 0xDC), hx);
                }
            }
            float sc = g_fz_pct[k] / 100.0f;
            for (int s = 0; s < 256; s++) {
                unsigned char *slot = (unsigned char *)(g.mgr + 0x60 + (ULONG_PTR)s * 0xF0);
                unsigned h = *(unsigned *)slot;
                if (!h) continue;
                char *d = *(char **)(slot + 0x30);
                bool mine = false;
                for (int j = 0; j < nd; j++) if (d == dp[j]) mine = true;
                if (!mine) continue;
                int at = -1, fr = -1;
                for (int i = 0; i < 32; i++) { if (g_fz[i].h == h) at = i; if (!g_fz[i].h && fr < 0) fr = i; }
                if (at < 0) {                                       // first sight: its own matrix, the axes scaled
                    if (fr < 0) continue;
                    FzInst &z = g_fz[fr];
                    z.keep = now - g_fz_cast[k] > 1500;              // not just after a cast of this skill: left at its size
                    if (z.keep) { z.h = h; continue; }
                    float *src = *(int *)(slot + 0xDC) ? *(float **)(slot + 0x40) : (float *)(slot + 0x50);
                    if (!src) continue;
                    memcpy(z.m, src, 64);
                    if (*(int *)(slot + 0xDC)) { memcpy(z.m + 12, slot + 0xD0, 12); z.m[15] = 1.0f; }
                    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) z.m[r * 4 + c] *= sc;
                    z.h = h; at = fr;
                    logf("effect size: skill %d effect %d instance %08X drawn at %d %%", sk, req, h, g_fz_pct[k]);
                }
                if (g_fz[at].keep) continue;
                g.move(h, g_fz[at].m);
                sfx_pin(g, h, g_fz[at].m);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
