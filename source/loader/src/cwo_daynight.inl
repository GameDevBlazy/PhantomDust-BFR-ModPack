// ---- v5.188.58 DAY AND NIGHT during a match (owner voice memo 2026-11-21, item 4: "sky colour changing during a match: NOT
// STARTED"). Offline matches, MODS edition only; switch: CwoDayNight.txt in the loader folder (MOD LIST: L3), optional first
// line "cycle N" = seconds for a whole day (default 240).
// Game facts (Ghidra full pass 065, pd_fn.py, 2026-11-21; cwo_outfit.inl): FUN_140199510(set, brightness) turns light set N
// (0x1403e8a40 + 0x58 N: colour bytes +0..+2, +4..+6, +8..+0xa) into light 8N+0x40 (type 6, colours at +0 and +0x10, floats =
// byte x 1/255 x brightness) and 8N+0x41 (type 1); lights at 0x1405a6a40, 0x70 each. The per-frame light update
// FUN_14019a240 (a vtable function) rebuilds sets 0, 2 and 3 only while its brightness fade runs (*0x140527c90 & 1) - so the
// stage's lights otherwise keep whatever colours they hold, and the loader can tint them frame by frame.
// What this does: at the match start it keeps the stage's own colours of lights 0x40 / 0x41 / 0x50 / 0x51 / 0x58 / 0x59 (sets
// 0, 2, 3); each frame it writes those colours x a tint that runs day -> sunset -> dusk -> day over the cycle (never below 70 %
// of a channel - the owner's rule: every stage keeps adequate light); at the match end the stage's own colours come back.
// The sky (2026-11-21 15:xx, Ghidra pass 065): pdMapSky's draw FUN_1401c87f0 draws the stage's .alr group 2 with light set 2
// (FUN_140199970(2) -> lights 0x50 / 0x51, tinted here), a second sky object with set 3 - so the sky takes the tint as far as
// its shader uses the light (to be seen in game).
static const int DN_LIGHTS[6] = {0x40, 0x41, 0x50, 0x51, 0x58, 0x59};
static float g_dn_keep[6][8];
static int g_dn_state = -1, g_dn_cycle = 240, g_dn_switch = -1;
static ULONGLONG g_dn_t0, g_dn_check_t;

static void dn_tint(double f, float *t) {                         // f = 0..1 through the day
    static const float K[5][3] = {{1.00f, 1.00f, 1.00f},              // day
                                  {1.12f, 0.86f, 0.70f},              // sunset
                                  {0.74f, 0.80f, 1.00f},              // dusk / moonlight
                                  {0.90f, 0.82f, 0.95f},              // dawn
                                  {1.00f, 1.00f, 1.00f}};             // day again
    double x = f * 4.0;
    int i = (int)x; if (i > 3) i = 3;
    double u = x - i, s = u * u * (3 - 2 * u);                         // smooth step between the keys
    for (int k = 0; k < 3; k++) t[k] = (float)(K[i][k] + (K[i + 1][k] - K[i][k]) * s);
}

// v5.200.0 (owner 2026-11-22): the pause menu's row (tools/build_pause_daynight.py):
// native 2.8 op 128 = its text, op 129 = A on it - on / off at once (CwoDayNight.txt written or removed, the switch set now, so a
// running match starts or ends the cycle on the next frame; off puts the stage's own light back)
static INT64 dn_native(int op) {
    static char txt[32];
    if (op == 129) {
        wchar_t p[MAX_PATH];
        swprintf_s(p, MAX_PATH, L"%sCwoDayNight.txt", g_dir);
        int on = g_dn_switch == 1 ? 0 : 1;
        if (on && ed_online()) on = 0;
        if (on) { FILE *f = NULL; if (cwo_wfopen(&f, p, L"w") == 0 && f) { fprintf(f, "cycle %d\n", g_dn_cycle); fclose(f); } }
        else DeleteFileW(p);
        g_dn_switch = on; g_dn_check_t = GetTickCount64();
        logf("day and night: %s (pause menu)", on ? "ON" : "off");
    }
    sprintf_s(txt, "Day and Night: %s", g_dn_switch == 1 ? "ON" : "OFF");
    return (INT64)(ULONG_PTR)txt;
}
static void dn_step() {                                          // game thread, every drawn frame
    ULONGLONG now = GetTickCount64();
    if (now - g_dn_check_t > 3000 && g_dn_state < 1) {
        g_dn_check_t = now;
        wchar_t p[MAX_PATH]; FILE *f = NULL;
        swprintf_s(p, MAX_PATH, L"%sCwoDayNight.txt", g_dir);
        int on = 0;
        if (cwo_wfopen(&f, p, L"r") == 0 && f) {
            char line[64] = ""; int n = 0;
            if (fgets(line, sizeof(line), f) && sscanf_s(line, "cycle %d", &n) == 1 && n >= 30 && n <= 3600) g_dn_cycle = n;
            fclose(f); on = 1;
        }
        if (on != g_dn_switch) { logf("day and night: %s (CwoDayNight.txt, a day = %d s)", on ? "ON for offline matches" : "off", g_dn_cycle); g_dn_switch = on; }
    }
    bool want = g_dn_switch == 1 && g_in_match && !ed_online();   // v5.201.0 BFR too (owner 2026-11-22)
    __try {
        float *L = (float *)(g_base + RVA_LIGHTS);
        if (!want) {
            if (g_dn_state == 1) {                                 // put the stage's own colours back
                for (int k = 0; k < 6; k++) memcpy((char *)L + DN_LIGHTS[k] * 0x70, g_dn_keep[k], sizeof(g_dn_keep[k]));
                logf("day and night: the stage's own light is back");
            }
            g_dn_state = 0;
            return;
        }
        if (g_dn_state != 1) {
            for (int k = 0; k < 6; k++) memcpy(g_dn_keep[k], (char *)L + DN_LIGHTS[k] * 0x70, sizeof(g_dn_keep[k]));
            g_dn_t0 = now; g_dn_state = 1;
            logf("day and night: started (a day = %d s)", g_dn_cycle);
        }
        float t[3];
        dn_tint(fmod((now - g_dn_t0) / 1000.0 / g_dn_cycle, 1.0), t);
        for (int k = 0; k < 6; k++) {
            float *c = (float *)((char *)L + DN_LIGHTS[k] * 0x70);
            for (int j = 0; j < 3; j++) { c[j] = g_dn_keep[k][j] * t[j]; c[4 + j] = g_dn_keep[k][4 + j] * t[j]; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_dn_state = 0; }
}
