// ---- v5.132 quieter title screen (owner 2026-11-10). Offline and online alike (local sound level only, nothing sent).
// Game facts (Ghidra full pass 065 via pd_fn.py, clean 9ba7411b, 2026-11-10): the BGM stream level is set by FUN_14019d0a0(k):
// level = options BGM volume (int percent at 0x14052dfb4) * k / 100, clamped 0..0x1000, applied with FUN_1402aa020(0, level)
// and kept in the short 0x14058280a; FUN_14019cdf0 (track play) calls it with k = 0x1000. The menu in use = the short at
// *0x140533c58 + 0x105f4; id 7 = the title screen (MODLOG 2026-11-03, What's New).
// What this does: while the title screen is up, the BGM is held at CwoTitleVolume.txt percent of your normal music level
// (default 40; 100 = as the game has it); on leaving the title, the normal level is set back once.
typedef void(__fastcall *PFN_BGM_SETVOL)(int, int);
static const ULONG_PTR RVA_TV_SETVOL = 0x2aa020, RVA_TV_OPT = 0x52dfb4, RVA_TV_LEVEL = 0x58280a, RVA_TV_MENU = 0x533c58,
                       RVA_TV_MODE = 0x52ad0c, RVA_TV_LEVEL2 = 0x582688, RVA_TV_TARGET2 = 0x5826a0;
// v5.132.1 (in game 06:23: nothing to lower - the title plays in mode 2): FUN_14019d110 / FUN_14019d1b0 fade the BGM in two
// play modes (*0x14052ad0c): mode 1 level = the short 0x14058280a, mode 2 level = the short 0x140582688 (fade target 0x1405826a0)
// v5.155 (owner 2026-11-12): held from start-up
// (logos, intro, title) until the title has been left once - the profile's own music level applies after that; default 70.
static int g_tv_pct = -1;
static bool g_tv_on, g_tv_boot = true, g_tv_title_seen;

static void tv_load() {
    g_tv_pct = 70;
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoTitleVolume.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") == 0 && f) {
        int v = 70;
        if (fscanf_s(f, "%d", &v) == 1 && v >= 0 && v <= 100) g_tv_pct = v;
        fclose(f);
    } else if (cwo_wfopen(&f, p, L"w") == 0 && f) {
        fprintf(f, "70\n# start-up and title-screen music, percent of your normal music volume (100 = the game's own)\n");
        fclose(f);
    }
    logf("title volume: %d %% of the normal music level from start-up until the title is left", g_tv_pct);
}

static int tv_level(int pct_of_normal) {                           // v5.132.2: percent of what the game really plays (clamped first)
    int opt = *(int *)(g_base + RVA_TV_OPT);
    int lv = opt * 0x1000 / 100;
    lv = lv < 0 ? 0 : lv > 0x1000 ? 0x1000 : lv;
    return lv * pct_of_normal / 100;
}
static bool g_tv_logged;

static void tv_step() {                                            // CwoOverlayFrame, game thread
    if (g_tv_pct < 0) tv_load();
    if (g_tv_pct >= 100) return;
    __try {
        ULONG_PTR m = *(ULONG_PTR *)(g_base + RVA_TV_MENU);
        int id = m ? *(short *)(m + 0x105f4) : -1;
        PFN_BGM_SETVOL setvol = (PFN_BGM_SETVOL)(g_base + RVA_TV_SETVOL);
        int mode = *(int *)(g_base + RVA_TV_MODE);
        short *cur = (short *)(g_base + (mode == 2 ? RVA_TV_LEVEL2 : RVA_TV_LEVEL));
        if (id == 7) g_tv_title_seen = true;
        if (g_in_match || (g_tv_title_seen && id != 7)) g_tv_boot = false;   // v5.155 past the title: the profile's level
        if ((id == 7 || g_tv_boot) && !g_in_match) {
            int want = tv_level(g_tv_pct);
            if (*cur > want + 8) {
                int was = *cur;
                setvol(0, want);
                *cur = (short)want;
                if (mode == 2 && *(short *)(g_base + RVA_TV_TARGET2) > want) *(short *)(g_base + RVA_TV_TARGET2) = (short)want;
                if (!g_tv_logged) { g_tv_logged = true; logf("title volume: title screen (mode %d) - music %d -> %d", mode, was, want); }
            }
            if (!g_tv_on) logf("title volume: %s - mode %d, levels %d / %d, want %d", id == 7 ? "on the title" : "start-up", mode, *(short *)(g_base + RVA_TV_LEVEL), *(short *)(g_base + RVA_TV_LEVEL2), want);
            g_tv_on = true;
        } else if (g_tv_on) {
            int normal = tv_level(100);
            setvol(0, normal);
            *cur = (short)normal;
            if (mode == 2) *(short *)(g_base + RVA_TV_TARGET2) = (short)normal;
            g_tv_on = false; g_tv_logged = false;
            logf("title volume: left the title - music back to %d", normal);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_tv_pct = 100; logf("title volume: fault - off"); }
}
