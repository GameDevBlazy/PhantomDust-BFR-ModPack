// ---- v5.86 Edgar's Dream > Visions (owner 2026-11-02 - graphics options like clarity, blur ... in Edgar's Dream;
// our own effects only). The page sets the post pass's Visions (cwo_post.inl, CwoVisions.txt): a preset, or Custom with
// each value on its own row. Offline only (the pass is off online).
static const char *VI_PRE[] = {"off", "clarity", "vivid", "cinematic", "soft", "noir", "anime", "realistic", "retro", "custom"};
static const int VI_NPRE = 10;
static const int VI_N = 15;                                         // v5.130: 15 values
struct ViRow { const char *label; float lo, hi, step; };
static const ViRow VI_ROWS[15] = {{"Sharpen", 0, 2, 0.1f}, {"Blur", 0, 1, 0.1f}, {"Saturation", 0, 2, 0.05f}, {"Contrast", 0.5f, 1.5f, 0.02f},
                                 {"Vignette", 0, 1, 0.05f}, {"Warmth", -1, 1, 0.1f}, {"Brightness", -0.2f, 0.2f, 0.02f},
                                 {"Cel bands", 0, 1, 0.05f}, {"Ink outline", 0, 1, 0.05f}, {"Bloom", 0, 1, 0.05f}, {"Film grain", 0, 1, 0.05f},
                                 {"Chromatic shift", 0, 1, 0.05f}, {"Scanlines", 0, 1, 0.05f}, {"Filmic tone", 0, 1, 0.05f}, {"Vibrance", -1, 1, 0.05f}};
static int g_vi_row, g_vi_pre = -1;
static float g_vi_val[15] = {0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static void vi_from_preset(int k) {
    if (k <= 0 || k >= VI_NPRE - 1) { if (k <= 0) { float d[15] = {0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; memcpy(g_vi_val, d, sizeof(d)); } return; }
    for (const PpVision &v : PP_VIS) if (!_stricmp(v.name, VI_PRE[k])) {
        const float a[15] = {v.sharpen, v.blur, v.sat, v.contrast, v.vignette, v.warm, v.bright, v.poster, v.outline, v.bloom, v.grain, v.chroma, v.scan, v.tone, v.vib};
        memcpy(g_vi_val, a, sizeof(a));
    }
}
static void vi_load() {                                            // what CwoVisions.txt holds now
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoVisions.txt", g_dir);
    FILE *f = NULL; char line[128] = "off";
    if (cwo_wfopen(&f, p, L"r") == 0 && f) { if (!fgets(line, sizeof(line), f)) strcpy_s(line, "off"); fclose(f); }
    g_vi_pre = 0;
    if (!_strnicmp(line, "custom", 6)) {
        g_vi_pre = VI_NPRE - 1;
        float *q = g_vi_val;
        sscanf_s(line + 6, "%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5], &q[6], &q[7], &q[8], &q[9], &q[10], &q[11], &q[12], &q[13], &q[14]);
        return;
    }
    for (int k = 0; k < VI_NPRE - 1; k++) if (!_strnicmp(line, VI_PRE[k], strlen(VI_PRE[k]))) g_vi_pre = k;
    vi_from_preset(g_vi_pre);
}
static void vi_save() {
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoVisions.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) { set_status("Could not save Visions"); return; }
    if (g_vi_pre == VI_NPRE - 1)
    { fprintf(f, "custom");
        for (int i = 0; i < VI_N; i++) fprintf(f, " %.2f", g_vi_val[i]);
        fprintf(f, "\n"); }
    else fprintf(f, "%s\n", VI_PRE[g_vi_pre]);
    fclose(f);
}
static void vi_input(WORD pressed) {
    if (g_vi_pre < 0) vi_load();
    if (pressed & BTN_B) { InterlockedExchange(&g_ed_page, EDP_CHOOSER); return; }
    const int NR = 1 + VI_N;                                              // row 0 the preset, rows 1-7 the values
    if (pressed & BTN_DOWN) g_vi_row = (g_vi_row + 1) % NR;
    if (pressed & BTN_UP) g_vi_row = (g_vi_row + NR - 1) % NR;
    int dir = (pressed & BTN_RIGHT) ? 1 : (pressed & BTN_LEFT) ? -1 : 0;
    if (pressed & BTN_X) { g_vi_pre = 0; vi_from_preset(0); vi_save(); set_status("Visions off"); return; }
    if (!dir) return;
    if (g_vi_row == 0) { g_vi_pre = ((g_vi_pre + dir) % VI_NPRE + VI_NPRE) % VI_NPRE; if (g_vi_pre != VI_NPRE - 1) vi_from_preset(g_vi_pre); }
    else {
        const ViRow &r = VI_ROWS[g_vi_row - 1];
        float &v = g_vi_val[g_vi_row - 1];
        v += dir * r.step;
        if (v < r.lo) v = r.lo;
        if (v > r.hi) v = r.hi;
        g_vi_pre = VI_NPRE - 1;                                    // a changed value makes it Custom
    }
    vi_save();
}
static const char *VI_H[] = {"VISIONS - how to use", "L / R on Look: off ... noir, anime, realistic, retro, custom",
                             "The rows below fine-tune it (any change makes it Custom)", "It shows at once, in menus and matches (offline)",
                             "X: off   B: back", NULL};
template <class PUT, class BOX> static void vi_draw(PUT &put, BOX &box, int x, int y) {
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF;
    const int LINE = 16;
    (void)box;
    if (g_vi_pre < 0) vi_load();
    if (g_ed_help) {
        put(x, y, VI_H[0], GOLD); y += LINE + 4;
        for (int i = 1; VI_H[i]; i++, y += LINE) put(x, y, VI_H[i], WHITE);
        put(x, y + 4, "Y / A / B: close this help", BLUE);
        return;
    }
    put(x, y, "VISIONS                (B: back   Y: help)", GOLD);
    y += LINE + 4;
    char line[96];
    sprintf_s(line, "%sLook                %s%s%s", g_vi_row == 0 ? "> " : "  ", g_vi_row == 0 ? "< " : "", VI_PRE[g_vi_pre], g_vi_row == 0 ? " >" : "");
    put(x, y, line, g_vi_row == 0 ? GOLD : WHITE); y += LINE;
    for (int i = 0; i < VI_N; i++, y += LINE) {
        bool on = g_vi_row == i + 1;
        sprintf_s(line, "%s%-18s  %s%.2f%s", on ? "> " : "  ", VI_ROWS[i].label, on ? "< " : "", g_vi_val[i], on ? " >" : "");
        put(x, y, line, on ? GOLD : WHITE);
    }
    put(x, y + 4, "L/R: change   X: off   (offline matches and menus)", BLUE);
}
