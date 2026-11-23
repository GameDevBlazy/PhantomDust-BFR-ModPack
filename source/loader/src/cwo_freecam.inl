// ---- v5.9 free camera (owner 2026-10-25). Included by cwo_edgar.inl after the spectator code.
// Game facts (Ghidra full-pass batch_065, pcode.py):
//   FUN_14012bfc0 (battle draw, every frame): while *0x1404bc630 == 0 the view matrix *0x140584f40 is copied from the
//     view fighter's camera (fighter + 0x13f50, built by its controller fighter + 0x13f10: eye +0x13f2c, target +0x13f38)
//     and the eye *0x14058bdf0 from +0x13f2c. While it is not 0 (the event camera) the view is FUN_1402912d0(0x140584f40,
//     target *0x14059e130, eye *0x14059e150) - a look-at from eye to target, y up - and *0x14058bdf0 = that eye.
//   Nothing in the game's code writes 0x1404bc630 by name (no pointer to it in the image either); the event camera's
//     target / eye are written only by FUN_140275c40 / FUN_140275de0 / FUN_140276010 and cleared by FUN_140274e10.
// So the free camera sets the flag, writes its eye and target every frame, and puts the flag back when it ends. Player 1 is
// handed to the game's CPU while it runs (cwo_edgar.inl spectator), so the fight goes on while the pad flies the camera;
// the input filter gives the game only Start then (cwo_loader.cpp filtered_read).
static const ULONG_PTR RVA_EVCAM_ON = 0x4bc630, RVA_EVCAM_TGT = 0x59e130, RVA_EVCAM_EYE = 0x59e150, RVA_VIEW_EYE = 0x58bdf0;
static volatile LONG g_fc_req, g_fc_next, g_fc_hud;             // controller thread: on / off, next fighter, HUD
static float g_fc_pos[3], g_fc_yaw, g_fc_pitch;
static int g_fc_keep_flag, g_fc_follow = -1;
static bool g_fc_made_cpu, g_fc_keep_hud;
static LARGE_INTEGER g_fc_last;
static unsigned char *g_fc_test_mem;                            // self-test: stand-in for the flag, target and eye

static int *fc_flag() { return g_fc_test_mem ? (int *)g_fc_test_mem : (int *)(g_base + RVA_EVCAM_ON); }
static float *fc_tgt() { return g_fc_test_mem ? (float *)(g_fc_test_mem + 0x10) : (float *)(g_base + RVA_EVCAM_TGT); }
static float *fc_eye() { return g_fc_test_mem ? (float *)(g_fc_test_mem + 0x20) : (float *)(g_base + RVA_EVCAM_EYE); }
static const float *fc_view_eye() { return g_fc_test_mem ? (const float *)(g_fc_test_mem + 0x30) : (const float *)(g_base + RVA_VIEW_EYE); }

static float fc_dz(float v) { return fabsf(v) < 0.15f ? 0.0f : v; }
// one step: right stick turns (yaw around y, heading x = sin, z = cos as the game's), left stick moves on the ground
// plane along the view, the triggers sink / climb; A held = 3x
static void fc_move(float *pos, float *yaw, float *pitch, const float *ax, bool fast, float dt) {
    *yaw += fc_dz(ax[2]) * 2.2f * dt;
    *pitch += fc_dz(ax[3]) * 1.6f * dt;
    if (*pitch > 1.40f) *pitch = 1.40f;
    if (*pitch < -1.40f) *pitch = -1.40f;
    float sp = (fast ? 360.0f : 120.0f) * dt, lx = fc_dz(ax[0]), ly = fc_dz(ax[1]);
    float f[3] = {sinf(*yaw), 0, cosf(*yaw)}, r[3] = {cosf(*yaw), 0, -sinf(*yaw)};
    for (int j = 0; j < 3; j++) pos[j] += (f[j] * ly + r[j] * lx) * sp;
    pos[1] += (ax[5] - ax[4]) * sp;
}
static void fc_write() {
    float d[3] = {sinf(g_fc_yaw) * cosf(g_fc_pitch), sinf(g_fc_pitch), cosf(g_fc_yaw) * cosf(g_fc_pitch)};
    float *e = fc_eye(), *t = fc_tgt();
    for (int j = 0; j < 3; j++) { e[j] = g_fc_pos[j]; t[j] = g_fc_pos[j] + d[j] * 10.0f; }
    *fc_flag() = 1;
}
static void fc_look_at(const float *p) {                        // turn toward a point
    float dx = p[0] - g_fc_pos[0], dy = p[1] - g_fc_pos[1], dz = p[2] - g_fc_pos[2];
    g_fc_yaw = atan2f(dx, dz);
    g_fc_pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
}
// ---- v5.20 photo mode (owner 2026-10-27; "go ahead
// and work on adding all 8 of those ideas"). Inside the free camera: LB pauses / resumes the fight, D-pad Up / Down sets
// its speed (100, 50, 25, 10 %), X hides the HUD (as before). The picture keeps drawing every frame, so the camera flies
// smoothly while the fight stands still or crawls.
// Game facts (Ghidra batch_065, pcode.py, 2026-10-27):
//  - FUN_1401c85c0 (the frame): up to 3 logic steps FUN_1401c7630 at a fixed 30 per second (QPC period *0x1405bb820),
//    then the draw FUN_1401c8110 - drawn only on a frame that ran a step;
//  - FUN_1401c7630 runs the world's task groups (1, 2, 3, 6, 8, 0xe ... 0x1f: fighters, skills, effects) only while bits
//    0x20001 of *0x140530034 are clear; the battle draw (FUN_14012bfc0, the camera) and the HUD groups run either way;
//  - the battle's state machine FUN_140167580(*0x1404c0130, state) sets bit 0 itself for the states whose flag word
//    (*(u16 *)(0x1403e7b80 + state * 0x18)) has bit 0 (the pause menu and the like); the state is battle + 0x50.
// So the loader sets bit 0 for the steps the fight must skip, and only while the game's own state does not want it set;
// a slower speed skips steps (25 %: the fight moves on every 4th step), never the drawing.
static const ULONG_PTR RVA_WORLD_FLAGS = 0x530034, RVA_BATTLE_OBJ = 0x4c0130, RVA_STATE_FLAGS = 0x3e7b80;
static const int PH_SPEEDS = 4;
static const int PH_PCT[PH_SPEEDS] = {100, 50, 25, 10};
static int g_ph_speed;                                          // index into PH_PCT
static bool g_ph_pause, g_ph_set;                               // paused; bit 0 set by the loader
static float g_ph_acc;
static unsigned char *g_ph_test_mem;                            // self-test: flags word +0, battle object +0x10, state table +0x100

static unsigned *ph_flags() { return g_ph_test_mem ? (unsigned *)g_ph_test_mem : (unsigned *)(g_base + RVA_WORLD_FLAGS); }
static bool ph_game_pauses() {                                  // the game's own state holds the world (or no battle)
    __try {
        ULONG_PTR b = g_ph_test_mem ? (ULONG_PTR)(g_ph_test_mem + 0x10) : *(ULONG_PTR *)(g_base + RVA_BATTLE_OBJ);
        if (!b) return true;
        int st = *(int *)(b + 0x50);
        if (st < 0 || st > 63) return true;
        const unsigned char *tab = g_ph_test_mem ? g_ph_test_mem + 0x100 : (const unsigned char *)(g_base + RVA_STATE_FLAGS);
        return (*(const unsigned short *)(tab + st * 0x18) & 1) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}
static void ph_release() {                                      // bit 0 back to the game's, if the loader set it
    if (g_ph_set && !ph_game_pauses()) *ph_flags() &= ~1u;
    g_ph_set = false;
}
static void ph_end() {
    ph_release();
    g_ph_pause = false; g_ph_speed = 0; g_ph_acc = 0;
}
static void ph_want(bool pause, int speed) {                    // v5.36 the Skill Creator's loop sets the pace directly
    g_ph_pause = pause;
    if (speed < 0) speed = 0; if (speed >= PH_SPEEDS) speed = PH_SPEEDS - 1;
    if (g_ph_speed != speed) { g_ph_speed = speed; g_ph_acc = 0; }
}
static void ph_status() {
    char msg[96];
    if (g_ph_pause) sprintf_s(msg, "PHOTO: paused (LB plays, D-pad speed %d %%, X HUD)", PH_PCT[g_ph_speed]);
    else sprintf_s(msg, "PHOTO: speed %d %% (D-pad Up / Down, LB pauses, X HUD)", PH_PCT[g_ph_speed]);
    set_status(msg);
}
static void ph_step(bool toggle, int dspeed) {                  // game thread, every drawn frame while the free camera is on
    if (toggle) { g_ph_pause = !g_ph_pause; ph_status(); logf("photo mode: %s", g_ph_pause ? "paused" : "playing"); }
    if (dspeed) {
        int s = g_ph_speed - dspeed;                            // Up = faster (a lower index)
        s = s < 0 ? 0 : s >= PH_SPEEDS ? PH_SPEEDS - 1 : s;
        if (s != g_ph_speed) { g_ph_speed = s; g_ph_acc = 0; ph_status(); logf("photo mode: speed %d %%", PH_PCT[s]); }
    }
    if (ph_game_pauses()) { g_ph_set = false; return; }        // the game's own pause: hands off
    bool hold;
    if (g_ph_pause) hold = true;
    else if (g_ph_speed == 0) hold = false;
    else {
        g_ph_acc += PH_PCT[g_ph_speed] / 100.0f;
        hold = g_ph_acc < 1.0f;
        if (!hold) g_ph_acc -= 1.0f;
    }
    if (hold) { *ph_flags() |= 1u; g_ph_set = true; }
    else if (g_ph_set) { *ph_flags() &= ~1u; g_ph_set = false; }
}
static int ph_self_test() {                                     // the steps on stand-ins (no game memory touched)
    static unsigned char mem[0x100 + 0x18 * 4];
    memset(mem, 0, sizeof(mem));
    g_ph_test_mem = mem;
    *(unsigned short *)(mem + 0x100 + 0x18 * 2) = 1;            // state 2 = a state the game pauses in
    unsigned *fl = (unsigned *)mem;
    *fl = 0x800;
    ph_end();
    ph_step(true, 0);                                           // paused: bit 0 on, the other bits kept
    bool pause = *fl == 0x801 && g_ph_set;
    ph_step(true, 0);                                           // playing: bit 0 off
    bool play = *fl == 0x800 && !g_ph_set;
    ph_step(false, -2);                                         // two steps slower: 25 %
    int held = 0;
    for (int i = 0; i < 4; i++) { ph_step(false, 0); held += *fl & 1; }
    bool slow = g_ph_speed == 2 && held == 3;                   // 3 steps held, 1 runs
    ph_step(false, -5);                                         // never below 10 %
    bool floor_ = g_ph_speed == 3;
    *(int *)(mem + 0x10 + 0x50) = 2;                            // the game pauses (its menu): hands off, its bit kept
    *fl = 0x801;
    ph_step(true, 0);
    ph_end();
    bool game = *fl == 0x801;
    *(int *)(mem + 0x10 + 0x50) = 0;                            // ended while the loader held it: released
    *fl = 0x800;
    ph_step(true, 0);
    ph_end();
    bool released = (*fl & 1) == 0 && !g_ph_pause && g_ph_speed == 0;
    g_ph_test_mem = NULL;
    logf("self-test: photo pause %s, play %s, slow %s, floor %s, game's pause kept %s, released %s", pause ? "ok" : "FAILED",
         play ? "ok" : "FAILED", slow ? "ok" : "FAILED", floor_ ? "ok" : "FAILED", game ? "ok" : "FAILED", released ? "ok" : "FAILED");
    return pause && play && slow && floor_ && game && released ? 1 : -63;
}

static void fc_end(const char *why, bool in_match) {
    __try { *fc_flag() = g_fc_keep_flag; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try { ph_end(); } __except (EXCEPTION_EXECUTE_HANDLER) {}            // v5.20 the fight runs on at full speed
    InterlockedExchange(&g_fc_on, 0);
    g_hide_hud = g_fc_keep_hud;
    if (g_fc_made_cpu && in_match) {                            // player 1 was yours when it started: yours again
        ULONG_PTR pl = ed_player(0);
        if (pl) ed_spec_cpu(pl, false);
    }
    g_fc_made_cpu = false;
    logf("free camera: off (%s)", why);
}
static bool g_db_match;                                           // cwo_dbtl.inl (v5.97)
static void fc_start() {
    if (ed_online() && !g_db_match) { set_status("Online: no free camera (Download Battle: yes)"); return; }
    if (g_forge_active || g_forge_session) { set_status("Free camera: not in the creators' tests"); return; }
    ULONG_PTR pl = ed_player(0);
    if (!pl || !*(int *)(pl + 0x20)) { set_status("Free camera: no fighter to watch yet"); return; }
    g_fc_made_cpu = false;
    if (*(int *)(pl + 0x30) != 0 && !g_el) {                    // you are playing: the CPU fights on for you (v5.203.0 not in the Effect Lab: you stand)
        if (!ed_spec_cpu(pl, true)) return;                     // (it says why on screen)
        g_fc_made_cpu = true;
    }
    memcpy(g_fc_pos, fc_view_eye(), 12);
    float p[3];
    memcpy(p, (const float *)(pl + ACTOR_POS), 12);
    p[1] += 15.0f;
    if (fabsf(g_fc_pos[0] - p[0]) + fabsf(g_fc_pos[1] - p[1]) + fabsf(g_fc_pos[2] - p[2]) < 1.0f) {   // no eye yet
        g_fc_pos[0] = p[0]; g_fc_pos[1] = p[1] + 30.0f; g_fc_pos[2] = p[2] - 70.0f;
    }
    fc_look_at(p);
    g_fc_keep_flag = *fc_flag();
    g_fc_keep_hud = g_hide_hud;
    g_fc_follow = 0;
    QueryPerformanceCounter(&g_fc_last);
    fc_write();
    InterlockedExchange(&g_fc_on, 1);
    if (!g_el) ph_end();                                        // v5.203.0 the Effect Lab's freeze / slow carries into the camera
    if (!g_el) set_status("FREE CAMERA: sticks fly, LT/RT down/up, A fast, Y next fighter, X HUD, LB pause, D-pad speed, B back");   // v5.204.2 the Effect Lab shows its own control lines (the status ran off the screen)
    logf("free camera: on at (%.1f, %.1f, %.1f)%s", g_fc_pos[0], g_fc_pos[1], g_fc_pos[2], g_fc_made_cpu ? ", player 1 handed to the CPU" : "");
}
static void ed_freecam_step() {                                 // game thread, every flush
    bool req = InterlockedExchange(&g_fc_req, 0) != 0;
    if (!g_in_match) {
        if (g_fc_on) fc_end("the match ended", false);
        InterlockedExchange(&g_fc_next, 0); InterlockedExchange(&g_fc_hud, 0);
        return;
    }
    __try {
        if (req) { if (g_fc_on) fc_end("B / View + RB", true); else fc_start(); }
        LONG sc = InterlockedExchange(&g_ed_spec_cam, 0);       // v5.188.27 View + LB / Spectator: the camera with the CPU
        if (sc == 1 && !g_fc_on) { fc_start(); if (g_fc_on) set_status("CPU playing - X: HUD on / off   B: camera off   View + LB: you again"); }   // v5.188.30 flight not shown yet (MODLOG)
        if (sc == 2 && g_fc_on) fc_end("View + LB: player 1 is yours", true);
        if (!g_fc_on) return;
        if (ed_online() && !g_db_match) { fc_end("online", true); return; }
        LARGE_INTEGER now, f;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&f);
        float dt = (float)(now.QuadPart - g_fc_last.QuadPart) / (float)f.QuadPart;
        g_fc_last = now;
        if (dt < 0 || dt > 0.1f) dt = 0.1f;
        float ax[6];
        for (int j = 0; j < 6; j++) ax[j] = g_fc_axis[j];
        fc_move(g_fc_pos, &g_fc_yaw, &g_fc_pitch, ax, (g_fc_btn & 0x4) != 0, dt);
        if (InterlockedExchange(&g_fc_next, 0)) {               // Y: behind the next fighter, looking at them
            ULONG_PTR *act = cwo_actors();
            for (int k = 1; k <= 8; k++) {
                int i = (g_fc_follow + k) % 8;
                ULONG_PTR a = act[i];
                if (!a || !*(int *)(a + 0x20)) continue;
                float p[3];
                memcpy(p, (const float *)(a + ACTOR_POS), 12);
                p[1] += 15.0f;
                g_fc_pos[0] = p[0] - sinf(g_fc_yaw) * 70.0f; g_fc_pos[1] = p[1] + 30.0f; g_fc_pos[2] = p[2] - cosf(g_fc_yaw) * 70.0f;
                fc_look_at(p);
                g_fc_follow = i;
                char msg[64]; sprintf_s(msg, "Free camera: fighter %d", i + 1); set_status(msg);
                break;
            }
        }
        if (InterlockedExchange(&g_fc_hud, 0)) { g_hide_hud = !g_hide_hud; set_status(g_hide_hud ? "Free camera: HUD hidden (X)" : "Free camera: HUD shown (X)"); }
        fc_write();
        ph_step(InterlockedExchange(&g_ph_pause_req, 0) != 0, (int)InterlockedExchange(&g_ph_speed_req, 0));   // v5.20 photo mode
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("free camera: fault - off"); InterlockedExchange(&g_fc_on, 0); }
}

static int fc_self_test() {                                     // the steps on stand-ins (no game memory touched)
    static unsigned char mem[0x40];
    memset(mem, 0, sizeof(mem));
    g_fc_test_mem = mem;
    float pos[3] = {0, 0, 0}, yaw = 0, pitch = 0;
    float fwd[6] = {0, 1, 0, 0, 0, 0}, right[6] = {1, 0, 0, 0, 0, 0}, up[6] = {0, 0, 0, 0, 0, 1}, look[6] = {0, 0, 1, 0, 0, 0};
    fc_move(pos, &yaw, &pitch, fwd, false, 1.0f);              // 1 s forward: +z 120
    bool f = fabsf(pos[0]) < 1e-3f && fabsf(pos[2] - 120.0f) < 1e-3f;
    fc_move(pos, &yaw, &pitch, right, false, 0.5f);            // half a second right: +x 60
    f = f && fabsf(pos[0] - 60.0f) < 1e-3f;
    fc_move(pos, &yaw, &pitch, up, true, 0.1f);                // fast climb: +y 36
    f = f && fabsf(pos[1] - 36.0f) < 1e-3f;
    float y0 = yaw;
    fc_move(pos, &yaw, &pitch, look, false, 0.5f);             // the right stick turns, does not move
    f = f && fabsf(yaw - y0 - 1.1f) < 1e-3f && fabsf(pos[2] - 120.0f) < 1e-3f;
    float dead[6] = {0.1f, -0.1f, 0.12f, 0, 0, 0};
    float p0[3] = {pos[0], pos[1], pos[2]};
    fc_move(pos, &yaw, &pitch, dead, false, 1.0f);             // inside the dead zone: nothing
    f = f && !memcmp(p0, pos, 12);
    g_fc_pos[0] = 1; g_fc_pos[1] = 2; g_fc_pos[2] = 3; g_fc_yaw = 0; g_fc_pitch = 0;
    fc_write();
    const float *e = (const float *)(mem + 0x20), *t = (const float *)(mem + 0x10);
    bool w = *(int *)mem == 1 && e[0] == 1 && e[1] == 2 && e[2] == 3 && fabsf(t[2] - 13.0f) < 1e-4f && fabsf(t[0] - 1.0f) < 1e-4f;
    float tgt[3] = {11, 2, 3};
    fc_look_at(tgt);                                            // +x: yaw 90 degrees, level
    w = w && fabsf(g_fc_yaw - 1.5707963f) < 1e-4f && fabsf(g_fc_pitch) < 1e-4f;
    g_fc_test_mem = NULL;
    logf("self-test: free camera moves %s, writes %s", f ? "ok" : "FAILED", w ? "ok" : "FAILED");
    return f && w ? 1 : -34;
}
