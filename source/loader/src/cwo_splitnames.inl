// ---- v5.92 names of the other fighters in split screen (owner 2026-11-03). Game facts (Ghidra batch_073c FUN_140150c30, the battle HUD): in split screen (HUD +0x2a0 != 0) each half
// draws one panel, its own player's (the full screen draws all four), so the other player and the CPUs have no plate.
// What this does: in a split-screen battle (type *0x1404bc9d4 == 6) each half gets, in the black bar under its own panel,
// one line per other fighter still in: "PLAYER 2" / "CPU <character>", its life, teammates in green, others in red.
// Display only, offline (the split screen is offline).
static void sn_draw() {
    if (!g_in_match || ed_online()) return;
    return;   // v5.105 (owner 2026-11-04): off until real life bars
    __try {
        if (*(int *)(g_base + 0x4bc9d4) != 6) return;
        int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
        if (glyphs > cap - 200 || *(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
        ULONG_PTR *acts = cwo_actors();
        int nf = cwo_nfighters(); if (nf > 8) nf = 8;
        int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
        int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
        DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
        int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
        DWORD s_rgba[4]; memcpy(s_rgba, rgba, sizeof(s_rgba));
        PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
        auto put = [&](int px, int py, const char *s, DWORD col) { *space = 0; *gap = 1; *fh = 11; *fw = 6; set_rgba(rgba, 0xFF000000); text(px + 1, py + 1, s); set_rgba(rgba, col); text(px, py, s); };
        for (int view = 0; view < 2; view++) {                    // the two halves: player 1 left, player 2 right
            ULONG_PTR me = acts[view];
            if (!me) continue;
            int my_team = *(int *)(me + 0x1c), line = 0;
            int X = view == 0 ? 20 : 340, Y = 128;   // v5.104 below the item icons (62 sat on the HUD panel)
            for (int p = 0; p < nf && line < 4; p++) {
                ULONG_PTR a = acts[p];
                if (p == view || !a || !*(int *)(a + 0x20)) continue;
                char name[40];
                unsigned w = *(unsigned *)(cwo_gaddr(0x4bc420) + 0x48 * (ULONG_PTR)p + 8);
                int row = (int)(w & 0xFFFF);
                const char *cn = "";
                for (int i = 0; i < g_lob_nch && i < 40; i++) if (g_lob_ch[i].row == row) { cn = g_lob_ch[i].name; break; }
                bool human = *(int *)(cwo_gaddr(0x4bc420) + 0x48 * (ULONG_PTR)p + 0x10) != 0;
                int life = *(int *)(a + 0x4c);
                bool down = *(int *)(a + 0x24) != 1;
                if (human) sprintf_s(name, "PLAYER %d  %d%s", p + 1, life < 0 ? 0 : life, down ? "  DOWN" : "");
                else sprintf_s(name, "CPU %s  %d%s", cn[0] ? cn : "", life < 0 ? 0 : life, down ? "  DOWN" : "");
                bool mate = *(int *)(a + 0x1c) == my_team;
                put(X, Y + 11 * line, name, down ? 0xFFA0A0A0 : mate ? 0xFF70E070 : 0xFFF07060);
                line++;
            }
        }
        memcpy(rgba, s_rgba, sizeof(s_rgba));
        *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
