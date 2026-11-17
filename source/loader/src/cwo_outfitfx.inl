// ---- v5.75 outfit effects (owner 2026-11-02; "I meant literally ... a skills effect outlining the character or anywhere that I want it to
// be at ... little flames and sparks lifting off the character, not an actual texture"; "It doesn't have to be that exact
// effect"). Offline matches (display only, every fighter on this machine).
// Game facts (PD Ghidra full pass 065, AURA_FX_2026-10-24.md, SCHOOL_FX_FIX_2026-10-25.md, cwo_sfx.inl): an effect starts with
// FUN_14018a040(mgr, owner, request, track, matrix, 0) (owner = fighter + 0x14), loops with FUN_14018d410, moves with
// FUN_14018d440; the slot's +0x40 / +0x90 / +0xD0 are pinned to our matrix every frame (sfx_pin); a fighter's joints are the
// owner table mgr + 0xF070 + 0x800 * owner (+8 = entry 0's matrices; bone b = + (b + 1) * 0x40, position at +48); the
// request of a skill = FUN of its profile (g.effect_of); SFX_PARTS gives each body's bones for ground, hips, chest, head,
// right / left hand, right / left foot. A fighter's outfit = record 0x1404bc420 + 0x48 p + 8 (outfit << 16 | charaset row).
// What this does: CwoOutfitFx.txt lines "<model stem> <outfit 1-15> <preset>" (Create an Outfit's "Outfit effect" row writes
// them); in a match every fighter whose model + outfit has a preset gets that skill's effect part on each of the preset's
// body points, looped, following the joints every frame.
struct OfxPreset { const char *name; int skill, track, size; const char *points; };   // points: h hips, c chest, d head, r / l hands, f / g feet
// v5.75.1 (in game 14:24: part 0 hangs on the skill's own hand joint, so every point drew at the hand): the projectile (1)
// or hit (3) parts follow the instance's matrix, so those are used, smaller, at each point
static const OfxPreset OFX_PRESETS[] = {
    {"none", 0, 0, 0, ""},
    {"Fire (Bullet of Fire)", 61, 1, 12, "hcdrlfg"},
    {"Ice (Ice Lance)", 60, 1, 14, "hcdrlfg"},
    {"Thunder (Lightning Sword)", 8, 3, 14, "hcdrlfg"},
    {"Psychic (Psycho Wave)", 1, 3, 18, "hcdrl"},
    {"Vibration (Vibration Blaster)", 54, 1, 14, "hcdrlfg"},
    {"Flaming hands (Bullet of Fire)", 61, 1, 20, "rlfg"},
    // v5.93 (owner's list 2026-11-03: "An outfit effect chosen from the deck"): the effect of a skill in the fighter's own
    // battle deck (0x140584f80 + fighter * 400: 30 skill ids, Ghidra FUN_14024d9d0 - AURA_COLOUR notes), the first one that
    // has an effect; skill -1 = picked in the match
    {"From your deck (a skill's effect)", -1, 1, 14, "hcdrlfg"},
};
// v5.93 (owner's list: "Bigger flames (particle size)"): the particle size itself is in the effect data the game draws (not
// researched); above 150 % Effect size each point gets 2 copies, from 200 % 3, spread around it - bigger, fuller flames
static const int OFX_N = (int)(sizeof(OFX_PRESETS) / sizeof(OFX_PRESETS[0])), OFX_PTS = 7, OFX_DUP = 3, OFX_SLOTS = OFX_PTS * OFX_DUP;
// v5.101 (owner 2026-11-03): a preset value 1000 + N = skill N's own effect (picked in Create an Outfit from the arsenal in use, kept
// by skill number so a later deck change does not change the outfit); its part: projectile (1), else hit (3), else 0 / 2
static const int OFX_SKILL0 = 1000;
static bool ofx_valid(int p) { return (p >= 0 && p < OFX_N) || (p > OFX_SKILL0 && p < OFX_SKILL0 + 1000); }
static OfxPreset ofx_ps(int p) {
    if (p > OFX_SKILL0 && p < OFX_SKILL0 + 1000) return {"a deck skill's effect", p - OFX_SKILL0, 1, 14, "hcdrlfg"};
    return OFX_PRESETS[p >= 0 && p < OFX_N ? p : 0];
}
struct OfxEntry { char stem[8]; int outfit, preset, mode, dx, dy, dz, size, speed, shows, aura; };   // v5.93 aura: the outfit's aura hue (-1 the school's)   // v5.82 shows: 0 always, 1 aura 10, 2 grows   // v5.79 placement, offsets, size %, speed %
static OfxEntry g_ofx[256];
static int g_ofx_n;
static ULONGLONG g_ofx_cfg_t;
struct OfxInst { unsigned h; int fail; float tlast; __declspec(align(16)) float m[16]; };
static OfxInst g_ofx_inst[4][OFX_SLOTS];
static int g_ofx_live[4] = {-1, -1, -1, -1};                     // the preset (+ 100 x placement) running on fighter p (-1 none)

static void ofx_path(wchar_t *p) { swprintf_s(p, MAX_PATH, L"%sCwoOutfitFx.txt", g_dir); }
static void ofx_load() {
    wchar_t p[MAX_PATH]; ofx_path(p);
    FILE *f = NULL;
    g_ofx_n = 0;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[128];
    while (fgets(line, sizeof(line), f) && g_ofx_n < 256) {
        OfxEntry e = {};
        e.size = 100; e.speed = 100; e.aura = -1;
        if (line[0] == '#' || sscanf_s(line, "%7s %d %d %d %d %d %d %d %d %d %d", e.stem, (unsigned)sizeof(e.stem), &e.outfit, &e.preset,
                                       &e.mode, &e.dx, &e.dy, &e.dz, &e.size, &e.speed, &e.shows, &e.aura) < 3) continue;
        if (e.aura < -1 || e.aura > 40) e.aura = -1;
        if (e.shows < 0 || e.shows > 2) e.shows = 0;
        e.mode = e.mode ? 1 : 0;
        if (e.size < 25 || e.size > 300) e.size = 100;
        if (e.speed < 0 || e.speed > 300) e.speed = 100;
        if (e.outfit < 1 || e.outfit > 15 || !ofx_valid(e.preset)) continue;
        g_ofx[g_ofx_n++] = e;
    }
    fclose(f);
}
static void ofx_save() {
    wchar_t p[MAX_PATH]; ofx_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# outfit effects (loader): model stem, outfit 1-15, preset, placement (0 outline, 1 one spot), left/right, up/down, front/back, size %%, speed %% (0 paused)\n");
    for (int i = 0; i < g_ofx_n; i++)
        if (g_ofx[i].preset > 0 || g_ofx[i].aura >= 0) fprintf(f, "%s %d %d %d %d %d %d %d %d %d %d\n", g_ofx[i].stem, g_ofx[i].outfit, g_ofx[i].preset, g_ofx[i].mode,
                                          g_ofx[i].dx, g_ofx[i].dy, g_ofx[i].dz, g_ofx[i].size, g_ofx[i].speed, g_ofx[i].shows, g_ofx[i].aura);
    fclose(f);
}
static const OfxEntry *ofx_get(const char *stem, int outfit) {
    for (int i = 0; i < g_ofx_n; i++) if (!_stricmp(g_ofx[i].stem, stem) && g_ofx[i].outfit == outfit && g_ofx[i].preset > 0) return &g_ofx[i];
    return NULL;
}
static void ofx_set(const char *stem, int outfit, int preset) {    // Create an Outfit: Save and apply / the effect row
    ofx_load();
    for (int i = 0; i < g_ofx_n; i++) if (!_stricmp(g_ofx[i].stem, stem) && g_ofx[i].outfit == outfit) { g_ofx[i].preset = preset; ofx_save(); return; }
    if (g_ofx_n < 256 && preset > 0) { OfxEntry e = {}; strcpy_s(e.stem, stem); e.outfit = outfit; e.preset = preset; e.size = e.speed = 100; g_ofx[g_ofx_n++] = e; }
    ofx_save();
    logf("outfit effects: %s outfit %d = %s", stem, outfit, ofx_name(preset));
}
static int ofx_bodypart(char c) {                                 // SFX_PARTS column (1-based as SfxSpec.bodypart)
    switch (c) { case 'h': return 2; case 'c': return 3; case 'd': return 4; case 'r': return 5; case 'l': return 6; case 'f': return 7; case 'g': return 8; }
    return 0;
}
// v5.79 fx = Create an Outfit's v[8..15]: preset, placement, left/right, up/down, front/back, size %, speed %, shows (v5.82)
static void ofx_set_full(const char *stem, int outfit, const int *fx) {
    ofx_load();
    OfxEntry e = {};
    strcpy_s(e.stem, stem); e.outfit = outfit; e.preset = ofx_valid(fx[0]) ? fx[0] : 0; e.mode = fx[1] ? 1 : 0;
    e.dx = fx[2]; e.dy = fx[3]; e.dz = fx[4]; e.size = fx[5] >= 25 && fx[5] <= 300 ? fx[5] : 100; e.speed = fx[6] >= 0 && fx[6] <= 300 ? fx[6] : 100;
    e.shows = fx[7] >= 0 && fx[7] <= 2 ? fx[7] : 0;
    e.aura = fx[8] >= 0 && fx[8] <= 40 ? fx[8] - 1 : -1;            // v5.93 the row: 0 = the school's, 1..41 = a hue
    int i = 0;
    for (; i < g_ofx_n; i++) if (!_stricmp(g_ofx[i].stem, stem) && g_ofx[i].outfit == outfit) break;
    if (i < g_ofx_n) g_ofx[i] = e; else if (g_ofx_n < 256) g_ofx[g_ofx_n++] = e;
    ofx_save();
    g_ofx_cfg_t = 0;                                              // the match picks it up on its next frame
}
static void ofx_stock_path(const wchar_t *alr, wchar_t *out) {    // "03 Fire.alr" -> "03 Fire.fx"
    wcscpy_s(out, MAX_PATH, alr);
    wchar_t *dot = wcsrchr(out, L'.');
    if (dot) wcscpy_s(dot, MAX_PATH - (dot - out), L".fx");
}
static void ofx_stock_write(const wchar_t *alr, const int *fx) {
    wchar_t p[MAX_PATH]; ofx_stock_path(alr, p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "%d %d %d %d %d %d %d %d %d\n", fx[0], fx[1], fx[2], fx[3], fx[4], fx[5], fx[6], fx[7], fx[8]);
    fclose(f);
}
static bool ofx_stock_read(const wchar_t *alr, int *fx) {
    wchar_t p[MAX_PATH]; ofx_stock_path(alr, p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return false;
    fx[7] = 0; fx[8] = 0;
    bool ok = fscanf_s(f, "%d %d %d %d %d %d %d %d %d", &fx[0], &fx[1], &fx[2], &fx[3], &fx[4], &fx[5], &fx[6], &fx[7], &fx[8]) >= 7;
    fclose(f);
    return ok;
}
// an effect row changed on the Create an Outfit page: its slot shows it at once (in a match, within a frame)
static void ofx_live() {
    if (!InterlockedExchange(&g_ofx_dirty, 0)) return;
    const EdCreator &c = g_edc[EDP_OUTFIT];
    if (c.n <= 0) return;
    const EdItem &it = c.item[c.cur];
    char st[8]; sprintf_s(st, "pc%02da", it.v[0]);
    int slot = it.v[6] > 0 ? it.v[6] : it.v[1];
    ofx_set_full(st, slot, it.v + 8);
    char st2[64]; sprintf_s(st2, "Effect saved on slot %d (in a match it shows at once)", slot);
    set_status(st2);
}
static void ofx_stop_fighter(const FxGame &g, int p) {
    for (int k = 0; k < OFX_SLOTS; k++) { if (fx_alive(g, g_ofx_inst[p][k].h)) g.stop(g_ofx_inst[p][k].h); g_ofx_inst[p][k].h = FX_NONE; g_ofx_inst[p][k].fail = 0; }
    g_ofx_live[p] = -1;
}
// v5.89 the effect on the Create an Outfit preview (owner 2026-11-03). Game facts (Ghidra batch_102_model_viewer):
// the model viewer FUN_14015cd50 (cc_viewer) keeps its model as a fighter object at viewer + 0x110 (motion at +0x12f00) and
// its skill page loads a skill's effect with FUN_140189e80(*0x1405bb2e0 = the effect manager, FUN_1401272b0(profile,
// skill), 0) before playing it; so the preview loads the outfit's effect the same way and runs the outfit-effect code on
// that model (slot 0, the aura gate off).
static int g_ofx_prev_loaded = -1;
// v5.117.2 (owner 2026-11-08 10:33: still crashed ~3 s into Create an Outfit; log: exe+1e4ef from FUN_14011bc30 (the draw's
// texture set-up, FUN_14001e4b0 -> FUN_14001de70) on a freed texture object, 30 ms after the preview started effect 61 part 1,
// 1 s after the model had loaded): the effect's texture is not valid in that menu (the waiting of 5.117.1 did not help).
// The preview shows no outfit effect until that is found live; the rows still save it and matches show it.
static const bool OFX_PREVIEW_FX = false;
static ULONG_PTR ofx_prev_actor() {
    if (!OFX_PREVIEW_FX) return 0;
    if (g_in_match || g_co_prev != 2) return 0;
    ULONG_PTR v = cc_viewer();
    if (!v) return 0;
    // v5.117.1 (owner 2026-11-08: Create an Outfit crashed on Flaming hands and then on every entry; log 09:21:10 / 09:23:44:
    // exe+1e4ef ~17 ms after the preview opened and started the effect): since 5.112 the page opens straight into the
    // preview, so the effect was started on the viewer's model while the viewer was still loading it (state +0x48 3-5) and
    // again across each reload. The effect now runs only while the model is shown (state 2) and 1 s after its load; a
    // reload (state 3-5) makes this 0, which stops the preview's effect first (ofx_step).
    static ULONGLONG busy_t;                                       // the last time the viewer was loading
    ULONGLONG now_ = GetTickCount64();
    if (*(int *)(v + 0x48) != 2) busy_t = now_;
    if (*(int *)(v + 0x48) != 2 || !g_co_load_t || now_ - g_co_load_t < 1000 || now_ - busy_t < 800) return 0;
    ULONG_PTR a = *(ULONG_PTR *)(v + 0x110);
    return a && (a & 7) == 0 ? a : 0;
}
static void ofx_step() {                                          // CwoOverlayFrame, game thread
    FxGame g;
    ULONG_PTR prev = 0;
    __try { prev = ofx_prev_actor(); } __except (EXCEPTION_EXECUTE_HANDLER) { prev = 0; }
    if ((!g_in_match && !prev) || ed_online() || !fx_game(&g)) {
        if (!g_in_match) {
            if (g_ofx_live[0] >= 0 && fx_game(&g)) __try { ofx_stop_fighter(g, 0); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            for (int p = 0; p < 4; p++) { for (int k = 0; k < OFX_SLOTS; k++) g_ofx_inst[p][k].h = FX_NONE; g_ofx_live[p] = -1; }
            g_ofx_prev_loaded = -1;
        }
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (now >= g_ofx_cfg_t) { g_ofx_cfg_t = now + 2000; ofx_load(); }
    __try {
        ULONG_PTR *acts = cwo_actors();
        for (int p = 0; p < (prev ? 1 : 4); p++) {
            ULONG_PTR a = prev ? prev : acts[p];
            const OfxEntry *en = NULL;
            if (prev) {                                               // the preview: the outfit being edited
                const EdCreator &c = g_edc[EDP_OUTFIT];
                if (c.n > 0) {
                    const EdItem &it = c.item[c.cur];
                    char st[8]; sprintf_s(st, "pc%02da", it.v[0]);
                    en = ofx_get(st, it.v[6] > 0 ? it.v[6] : it.v[1]);
                }
            } else if (a && *(int *)(a + ACTOR_ACTIVE) != 0) {
                unsigned w = *(unsigned *)(cwo_gaddr(0x4bc420) + 0x48 * (ULONG_PTR)p + 8);
                int row = (int)(w & 0xFFFF), outfit = (int)(w >> 16) + 1;
                if (row >= 0 && row < 256 && g_hf_row_stem[row][0]) en = ofx_get(g_hf_row_stem[row], outfit);
            }
            // v5.82 (owner): battle data *(0x1404bc608 + 8p) +0x18 =
            // the aura level (FUN_1402621f0: a skill needing aura level N compares it)
            int aura = 0;
            if (en && !prev) { ULONG_PTR bd = *(ULONG_PTR *)(cwo_gaddr(0x4bc608) + 8 * (ULONG_PTR)p); if (bd) aura = *(int *)(bd + 0x18); }
            if (prev) aura = 10;                                      // the preview shows it as at full aura
            if (en && en->shows == 1 && aura < 10) en = NULL;
            int preset = en ? en->preset : 0, key = en ? en->preset + 100 * en->mode : -1;
            if (key != g_ofx_live[p] && g_ofx_live[p] >= 0) ofx_stop_fighter(g, p);
            if (preset <= 0) continue;
            const OfxPreset ps = ofx_ps(preset);
            g_ofx_live[p] = key;
            int pidx = *(int *)(a + 0x14);
            int skill = ps.skill;
            if (skill < 0) {                                          // v5.93 from the fighter's own deck
                skill = 0;
                const int *deck = prev ? NULL : (const int *)(g_base + 0x584f80 + 400 * (ULONG_PTR)p);
                int nrec = *(int *)(*(char **)(g_base + 0x4bdc40) + 4);
                for (int d = 0; deck && d < 30 && !skill; d++) {
                    int sk = deck[d];
                    if (sk <= 0 || sk >= nrec) continue;
                    short pr = *(short *)(g.skills + 0x14 + (ULONG_PTR)sk * 0x90);
                    if (pr > 0 && g.effect_of(pr, sk) > 0) skill = sk;
                }
                if (!skill) skill = 61;                                // the preview (no deck): Bullet of Fire
            }
            short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)skill * 0x90);
            int req = g.effect_of(prof, skill);
            if (req <= 0) continue;
            if (prev && g_ofx_prev_loaded != req) {                   // the viewer's own way to have the effect loaded
                typedef INT64 (__fastcall *PFN_FX_LOAD)(ULONG_PTR, int, int);
                ((PFN_FX_LOAD)(g_base + 0x189e80))(g.mgr, req, 0);
                g_ofx_prev_loaded = req;
                for (int k = 0; k < OFX_SLOTS; k++) g_ofx_inst[0][k].fail = 0;
                logf("outfit effects: the preview loads effect %d (%s)", req, ps.name);
            }
            ULONG_PTR base = *(ULONG_PTR *)(g.mgr + 0xF070 + 0x800 * (ULONG_PTR)pidx + 8);
            int body = sfx_body_of(a);
            const float *fpos = (const float *)(a + ACTOR_POS);
            float aura_k = en->shows == 2 ? 0.35f + 0.065f * (aura < 0 ? 0 : aura > 10 ? 10 : aura) : 1.0f;   // grows with the aura
            float sc = ps.size / 100.0f * en->size / 100.0f * aura_k, grow = en->size / 100.0f * aura_k;
            // v5.79 the editor: the body's middle (chest), the facing frame (as sfx_matrix), offsets x 0.5 (the body is ~34 tall)
            auto joint = [&](int part, float *out) {
                int bone = body >= 0 && body < 19 && part > 0 ? SFX_PARTS[body][part - 1] : -1;
                out[0] = fpos[0]; out[1] = fpos[1]; out[2] = fpos[2];
                if (!base || bone < 0) { if (part > 1) out[1] += 17.0f; return; }
                const float *q = (const float *)(base + (ULONG_PTR)(bone + 1) * 0x40 + 48);
                float dx = q[0] - out[0], dy = q[1] - out[1], dz = q[2] - out[2];
                if (dx * dx + dy * dy + dz * dz < 3600.0f) { out[0] = q[0]; out[1] = q[1]; out[2] = q[2]; }
            };
            float mid[3], F[16];
            joint(3, mid);
            memcpy(F, g.identity, 64);
            g.turn(F, (unsigned)(*(int *)(a + ACTOR_FACING) + 0x4000));
            float off[3];
            for (int j = 0; j < 3; j++) off[j] = en->dx * 0.5f * F[j] + en->dy * 0.5f * F[4 + j] + en->dz * 0.5f * F[8 + j];
            const char *pts = en->mode ? "c" : ps.points;
            int dup = en->size >= 200 ? 3 : en->size > 150 ? 2 : 1;
            float spread = (en->size - 100) * 0.03f;                  // PD units (a fighter is about 34 tall)
            static const float DUP_DIR[3][2] = {{0, 0}, {1, 0.6f}, {-1, 0.6f}};
            int npts = (int)strlen(pts); if (npts > OFX_PTS) npts = OFX_PTS;
            for (int kk = 0; kk < OFX_SLOTS && kk / OFX_PTS < dup; kk++) {
                int k = kk;
                if (kk % OFX_PTS >= npts) continue;                   // this point list is shorter than 7
                OfxInst &in = g_ofx_inst[p][k];
                int d = kk / OFX_PTS;
                float pos[3];
                joint(ofx_bodypart(pts[kk % OFX_PTS]), pos);
                for (int j = 0; j < 3; j++) pos[j] = mid[j] + (pos[j] - mid[j]) * grow + off[j] + spread * (DUP_DIR[d][0] * F[j] + DUP_DIR[d][1] * F[4 + j]);
                memset(in.m, 0, sizeof(in.m));
                in.m[0] = in.m[5] = in.m[10] = sc; in.m[15] = 1.0f;
                in.m[12] = pos[0]; in.m[13] = pos[1]; in.m[14] = pos[2];
                if (fx_alive(g, in.h)) {
                    g.move(in.h, in.m); sfx_pin(g, in.h, in.m);
                    // speed / pause (as sfx_step: the game's own advance since the last frame, scaled; 0 holds it)
                    float *time = (float *)(g.mgr + 0x60 + (ULONG_PTR)(in.h & 0xFFF) * 0xF0 + 0xC);
                    float cur = *time, kk = en->speed / 100.0f;
                    if (en->speed != 100 && in.tlast >= 0) *time = cur >= in.tlast ? in.tlast + (cur - in.tlast) * kk : cur * kk;
                    in.tlast = *time;
                    continue;
                }
                if (in.fail > (prev ? 600 : 120)) continue;
                unsigned h = g.start(g.mgr, pidx, req, ps.track, in.m, 0);
                if (h == FX_NONE && preset > OFX_SKILL0) {             // v5.101 a deck skill: the part it has
                    static const int OTHER[3] = {3, 0, 2};
                    for (int t = 0; t < 3 && h == FX_NONE; t++) h = g.start(g.mgr, pidx, req, OTHER[t], in.m, 0);
                }
                if (h == FX_NONE) { in.fail++; continue; }
                g.loop(h);
                sfx_pin(g, h, in.m);
                in.h = h; in.fail = 0; in.tlast = -1;
                if (k == 0) logf("outfit effects: fighter %d %s (effect %d part %d)", p + 1, ps.name, req, ps.track);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static int ofx_shown_no(int sk) {                                   // v5.115 the number the game shows (record +0x02)
    __try { return *(const short *)(*(const char **)(g_base + 0x4bdc50) + 0x90 * (ULONG_PTR)sk + 2); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return sk; }
}
static const char *ofx_name(int i) {
    if (i > OFX_SKILL0 && i < OFX_SKILL0 + 1000) {                    // v5.101 a skill (v5.115: any skill, its shown number)
        static char buf[48];
        sprintf_s(buf, "%03d %.30s", ofx_shown_no(i - OFX_SKILL0), sc_skill_name(i - OFX_SKILL0));
        return buf;
    }
    return OFX_PRESETS[i >= 0 && i < OFX_N ? i : 0].name;
}
// v5.101 Create an Outfit's "Outfit effect" row, L / R: the presets, then every skill of the arsenal in use that has an effect
// (arsenals *(*0x1404bec10 + 0x3a30), 100-byte records, +0x14 30 skill ids as shorts; the one in use *(*0x1404bec10 + 0x3b08):
// FUN_14024c220 case 0x21, cwo_lobby.inl); the old "From your deck" (7) only while it is the value
// v5.115 (owner 2026-11-08): "none", then EVERY skill that has an effect (records 1..999 below the boss / system ids 700+), in the game's own
// skill order - the number it shows (record +0x02), then the record - with the game's names; the old presets last (outfits
// saved with them keep working)
static int ofx_next(int v, int dir) {
    static int list[1100];
    static short key[1100];
    int n = 0;
    list[n++] = 0;
    FxGame g;
    bool ok = false;
    __try { ok = fx_game(&g); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    int ns = 0;
    __try {
        int nrec = *(int *)(*(char **)(g_base + 0x4bdc40) + 4);
        if (nrec > 1000) nrec = 1000;
        for (int sk = 1; ok && sk < nrec && n < 1000; sk++) {
            int no = ofx_shown_no(sk);
            if (no <= 0 || no >= 700) continue;                      // 700+: the bosses' and the system's own skills
            short pr = *(short *)(g.skills + 0x14 + (ULONG_PTR)sk * 0x90);
            if (pr <= 0 || g.effect_of(pr, sk) <= 0) continue;
            int at = n;                                              // insertion by (shown number, record)
            while (at > 1 && (key[at - 1] > no)) { list[at] = list[at - 1]; key[at] = key[at - 1]; at--; }
            list[at] = OFX_SKILL0 + sk; key[at] = (short)no; n++; ns++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    for (int i = 1; i < OFX_N && n < 1100; i++) if (i != 7 || v == 7) list[n++] = i;
    int at = -1;
    for (int j = 0; j < n; j++) if (list[j] == v) at = j;
    if (at < 0 && v > OFX_SKILL0 && n < 1100) { list[n] = v; at = n++; }   // a skill not in the list any more stays reachable
    if (at < 0) at = 0;
    static int logged;
    if (!logged++) logf("outfit effects: the effect list has %d skills in the game's order + %d presets", ns, OFX_N - 1);
    return list[(at + dir + n) % n];
}

// ---- v5.93 an aura colour per outfit (owner's list 2026-11-03: "an aura colour per outfit"). Game facts (Ghidra batch 036-041
// AURA_COLOUR notes + capstone 2026-11-03): every frame the aura script auramove sets each aura slot's colour with native 2.46
// (0x140206d00: *(mgr + 0x120 * *(mgr + 0x44) + 0xC8) = argument, mgr = *0x14052ad00, slot index = the fighter's battle-data
// number). The table entry 2.46 (0x1403eede0 + 46 * 8) goes to ofx_aura_native: a fighter whose outfit has its own aura
// colour gets it, everyone else the script's. Offline.
typedef INT64 (__fastcall *PFN_OFX_NAT)(INT64 *args);
static PFN_OFX_NAT g_ofx_aura_orig;
static DWORD g_ofx_aura_rgb[8];                                    // per aura slot: 0 = the school's
static INT64 __fastcall ofx_aura_native(INT64 *args) {
    __try {
        ULONG_PTR mgr = *(ULONG_PTR *)(g_base + 0x52ad00);
        int k = mgr ? *(int *)(mgr + 0x44) : -1;
        if (k >= 0 && k < 8 && g_ofx_aura_rgb[k] && g_in_match && !ed_online()) { *(DWORD *)(mgr + 0x120 * (ULONG_PTR)k + 0xC8) = g_ofx_aura_rgb[k]; return 0; }
        // v5.159 the Illusion school's smoke silver (cwo_school6.inl): the game's pick never takes school 6
        DWORD s6 = k >= 0 && k < 8 ? s6_aura_rgb(k) : 0;
        if (s6) { *(DWORD *)(mgr + 0x120 * (ULONG_PTR)k + 0xC8) = s6; return 0; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_ofx_aura_orig(args);
}
static void ofx_aura_install() {
    ULONG_PTR *cell = (ULONG_PTR *)(g_base + 0x3eede0 + 46 * 8);
    if (*cell != g_base + 0x206d00) { logf("outfit aura: native 2.46 is not the game's - off"); return; }
    g_ofx_aura_orig = (PFN_OFX_NAT)*cell;
    DWORD old;
    if (!VirtualProtect(cell, 8, PAGE_READWRITE, &old)) return;
    *cell = (ULONG_PTR)&ofx_aura_native;
    VirtualProtect(cell, 8, old, &old);
    logf("outfit aura: native 2.46 -> the loader (an aura colour per outfit)");
}
static void ofx_aura_step() {                                       // game thread: each fighter's outfit aura colour
    memset(g_ofx_aura_rgb, 0, sizeof(g_ofx_aura_rgb));
    if (!g_in_match || ed_online() || !g_ofx_aura_orig) return;
    __try {
        ULONG_PTR *acts = cwo_actors();
        for (int p = 0; p < 4; p++) {
            ULONG_PTR a = acts[p];
            if (!a || !*(int *)(a + ACTOR_ACTIVE)) continue;
            unsigned w = *(unsigned *)(cwo_gaddr(0x4bc420) + 0x48 * (ULONG_PTR)p + 8);
            int row = (int)(w & 0xFFFF), outfit = (int)(w >> 16) + 1;
            if (row < 0 || row >= 256 || !g_hf_row_stem[row][0]) continue;
            const OfxEntry *en = ofx_get(g_hf_row_stem[row], outfit);
            if (!en || en->aura < 0) continue;
            int k = *(int *)(a + 0x14);
            if (k >= 0 && k < 8) g_ofx_aura_rgb[k] = ed_aura_rgb(en->aura * 9, 90, 100, 176.0f);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
