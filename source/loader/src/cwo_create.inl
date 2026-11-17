// ---- v5.127 CREATE: the Edgar's Dream editor (owner 2026-11-09;
// "it must include create a stage, create a skill, create an outfit, create a character"; one player, full screen).
// What it is: the real panels on the CREATE screen (cwo_ui.inl draws them in the Present hook). Every tab drives the
// creators that already exist - nothing new is written into the game:
//   Stage      Stage Creator (cwo_forge.inl): categories, objects, markers, the Forge menu's rows, place / copy / delete
//   Skill      Skill Creator (cwo_newskills.inl): every row of the skill, save, into your hand, capsule
//   Outfit, Character, School, Quest, Mode, Spectator   the Edgar's Dream creators (cwo_edgar.inl): items, name, fields, actions
// Threads: the panels are composed on the render thread and only READ state; a click or key becomes a command in a small
// queue that the worker thread runs (cr_input, before forge_input) with the creators' own functions - never from Present.
// Entry (MODS edition): Edgar's Dream's first entry is CREATE; it goes into the offline one-player Single Play session on
// the Create a Stage default template (stage 20), the same session Create a Stage used, and the screen comes on by itself
// (no CwoDreamProbe.txt). Online it never turns on (ed_online()). The BFR edition keeps Create a Stage as it was.
// Not in this session: "Try it now", "Preview live" and "3 VS 3" go through Single Play from the main menu, so on the
// CREATE screen they say so instead of starting a second session.
// g_cr_entry (cwo_ui.inl): the session was started from Edgar's Dream > CREATE
// v5.152 (owner 2026-11-12, "Do all of these"): Spectator moved to the SINGLE PLAY lobby - CREATE holds only the creators
enum { CR_T_STAGE, CR_T_SKILL, CR_T_OUTFIT, CR_T_CHAR, CR_T_SCHOOL, CR_T_QUEST, CR_T_MODE, CR_T_N, CR_T_SPECT = 99 };
static const char *CR_TAB_NAMES[CR_T_N] = {"Stage", "Skill", "Outfit", "Character", "School", "Quest", "Mode"};
static volatile LONG g_cr_tab;
static int cr_page(int t) {
    switch (t) {
    case CR_T_OUTFIT: return EDP_OUTFIT; case CR_T_CHAR: return EDP_CHAR; case CR_T_SCHOOL: return EDP_SCHOOL;
    case CR_T_QUEST: return EDP_QUEST; case CR_T_MODE: return EDP_MODE; case CR_T_SPECT: return EDP_SPECT;
    }
    return -1;
}
static bool cr_enabled() { return true; }   // v5.150.0 (owner 2026-11-11): BFR too
static bool cr_wanted() { return g_cr_entry && g_forge_session && g_in_match && !g_fexit_phase && !ed_online(); }   // v5.127.1 off while leaving

// Edgar's Dream > CREATE (game thread, the menu's op 51): the session on the default template (stage 20), as Create a
// Stage's A on its first row did (cs_build: Single Play, the loader presses through the lists, black until the editor is on)
static bool cr_start() {
    cs_reset_list();
    g_cs_row = 0;
    // v5.188.18 (BFR player feedback 2026-11-16: "they clicked on full screen editor and it immediately took it to a stage
    // creator ... pick any deck ... it would just stay there"): the Store game has no stage 20 (the default template lives in
    // MODS only), so BFR fell back to the stage list. Now the editor starts on Dawn City (05) - solid world floor, seen in
    // game 2026-11-16 22:25 (editor on, the fighter stands) - or the first ordinary stage
    if (g_cs_n > 0 && g_cs[0].kind != 0) {
        int pick = -1;
        for (int i = 0; i < g_cs_n && pick < 0; i++) if (g_cs[i].kind == 1 && g_cs[i].stage == 5) pick = i;
        for (int i = 0; i < g_cs_n && pick < 0; i++) if (g_cs[i].kind == 1) pick = i;
        if (pick >= 0) { g_cs_row = pick; logf("Edgar's Dream: CREATE - no default template (stage 20); starting on '%s'", g_cs[pick].name); }
    }
    if (g_cs_n <= 0 || g_cs[g_cs_row].kind == 2 || !cs_build()) { logf("Edgar's Dream: CREATE - the default template was not found; the stage list instead"); return false; }
    InterlockedExchange(&g_cr_entry, 1);
    InterlockedExchange(&g_cr_tab, CR_T_STAGE);
    logf("Edgar's Dream: CREATE - into the editor session ('%s')", g_cs[g_cs_row].name);
    return true;
}

// ---- the command queue (render thread -> worker thread) ----
enum { CR_NONE, CR_F_CHOOSE, CR_F_TURN, CR_F_SPAWN, CR_F_MARK, CR_F_GOTO, CR_F_GOTO_SP, CR_F_OP, CR_SC_OPEN, CR_SC_TURN,
       CR_SC_CHOOSE, CR_ED_PICK, CR_ED_NEW, CR_ED_CHANGE, CR_ED_DEFAULT, CR_ED_NAME, CR_ED_ACTION, CR_SAVE, CR_UNDO, CR_REDO,
       CR_TEST, CR_EXIT };
struct CrCmd { int op, a, b, c; };
static CrCmd g_crq[64];
static volatile LONG g_crq_w, g_crq_r;
static char g_cr_namebuf[24];
static void cr_post(int op, int a = 0, int b = 0, int c = 0) {
    LONG w = g_crq_w;
    if (w - g_crq_r >= 64) return;
    g_crq[w & 63] = {op, a, b, c};
    MemoryBarrier();
    InterlockedExchange(&g_crq_w, w + 1);
}

// ---- UI state (render thread) ----
static int g_cr_cat = 1;                                           // Stage: the Vault's category (0 = Markers)
static int g_cr_scroll[3];                                         // Ledger, Attributes, Vault first row
static bool g_cr_typing;                                           // the Name field is being typed (g_dp_name)
static int g_cr_sel = -1;                                          // the pad's row in Attributes (-1 none)
static volatile LONG g_cr_sc_opened;
static volatile LONG g_cr_key_save, g_cr_key_undo, g_cr_key_redo, g_cr_key_test, g_cr_key_enter, g_cr_key_esc;

// the Forge menu's rows that turn with L / R (the rest are A: toggles and actions)
static bool cr_f_turns(int r) { return r == 5 || r == 15 || r == 16 || r == 20 || (r >= 22 && r <= 27) || r == 29; }
static void cr_f_val(int r, char *val, size_t n) {                // the Forge menu's value text (as forge_draw shows it)
    val[0] = 0;
    switch (r) {
    case 3: sprintf_s(val, n, "%d step%s", g_hcur, g_hcur == 1 ? "" : "s"); break;
    case 4: sprintf_s(val, n, "%d step%s", g_hn - 1 - g_hcur, g_hn - 1 - g_hcur == 1 ? "" : "s"); break;
    case 5: if (F_SNAPS[g_fsnap]) sprintf_s(val, n, "%d deg", F_SNAPS[g_fsnap]); else strcpy_s(val, n, "free"); break;
    case 6: strcpy_s(val, n, g_fprecise ? "on" : "off"); break;
    case 7: strcpy_s(val, n, g_fly_landed ? "off (walk)" : "on"); break;
    case 8: strcpy_s(val, n, g_fplay ? "on" : "off"); break;
    case 9: strcpy_s(val, n, g_ffreeze ? "on" : "off"); break;
    case 10: strcpy_s(val, n, g_hide_hud ? "on" : "off"); break;
    case 11: strcpy_s(val, n, g_fhide_player ? (o_player_draw ? "on" : "not available") : "off"); break;
    case 12: strcpy_s(val, n, g_fbob ? "on" : "off"); break;
    case 13: strcpy_s(val, n, g_fgod ? "on" : "off"); break;
    case 14: strcpy_s(val, n, g_fmagnet ? "on" : "off"); break;
    case 15: case 16: case 23: {
        int id = r == 23 ? g_fsfx_skill : g_fmk_skill[r - 15];
        const char *nm = "?";
        for (int i = 0; i < g_sk_count; i++) if (g_sk[i].id == id) nm = g_sk[i].name;
        sprintf_s(val, n, "%.20s", nm);
        break;
    }
    case 17: strcpy_s(val, n, g_fmk_fx ? "on" : "off"); break;
    case 18: strcpy_s(val, n, g_fcaps ? "on" : "off"); break;
    case 19: strcpy_s(val, n, g_fcol ? "on" : "off"); break;
    case 20: if (g_sk_count > 0) sprintf_s(val, n, "%.20s", g_sk[g_sk_sel].name); break;
    case 21: sprintf_s(val, n, "%s (%d made)", g_fcopies ? "on" : "off", g_fmade); break;
    case 22: strcpy_s(val, n, g_fbox_alpha == 2 ? "gray box" : g_fbox_alpha == 1 ? "black box" : "none"); break;
    case 24: sprintf_s(val, n, "x%.2g", F_SFX_SIZES[g_fsfx_size]); break;
    case 25: sprintf_s(val, n, "%.20s", F_SFX_PART[g_fsfx_part]); break;
    case 26: { float sp = F_SFX_SPEEDS[g_fsfx_speed]; if (sp == 0.0f) strcpy_s(val, n, "frozen"); else sprintf_s(val, n, "x%.2g", sp); break; }
    case 27: if (g_fsfx_hold < 0) strcpy_s(val, n, "off"); else sprintf_s(val, n, "step %ld", g_fsfx_hold); break;
    case 29:
        strcpy_s(val, n, "-");
        for (int k = 0; k < g_fd_n; k++)
            if (g_fd[k].placed && g_fd[k].cat >= 0 && g_fd[k].cat < g_fcat_n && !strcmp(g_fcat[g_fd[k].cat], "Skies") && g_fd[k].disp)
                sprintf_s(val, n, "%.20s", g_fd[k].disp + (strncmp(g_fd[k].disp, "Sky: ", 5) ? 0 : 5));
        break;
    }
}
// the Stage tab's rows in Attributes: the Forge menu without Save / Load / Undo / Redo (Dream Bar), Skill Creator and Close
static int cr_stage_rows(int *out) {
    int n = 0;
    for (int r = 5; r <= 29; r++) out[n++] = r;
    out[n++] = 2;                                                  // Reset map to the stage file
    out[n++] = 1;                                                  // Load saved map
    return n;
}

// ---- worker thread ----
static void cr_with_menu(int r, int dir, bool turn) {             // a Forge menu row, as if it were chosen in the menu
    LONG fm = g_fmenu, fs = g_fmenu_sel;
    InterlockedExchange(&g_fmenu, 2);
    InterlockedExchange(&g_fmenu_sel, r);
    if (turn) f_menu_turn(dir); else f_forge_menu_choose(r);
    if (g_fmenu == 2) { InterlockedExchange(&g_fmenu, fm == 2 ? 0 : fm); InterlockedExchange(&g_fmenu_sel, fs); }
}
static void cr_test_toggle() {                                     // Test in Dream (the Stage Creator pause menu's rows 0 / 1)
    if (g_ftesting) {
        InterlockedExchange(&g_ftesting, 0);
        g_fly_landed = g_ftest_keep_fly;
        g_hide_hud = g_ftest_keep_hud;
        memcpy(g_ghost, (const float *)(f_actor(0) + ACTOR_POS), 12);
        set_status("Back to building");
    } else if (g_hold >= 0) set_status("Place what you are holding first");
    else {
        g_ftest_keep_fly = g_fly_landed;
        g_ftest_keep_hud = g_hide_hud;
        g_fly_landed = true;
        g_hide_hud = false;
        InterlockedExchange(&g_ftesting, 1);
        InterlockedExchange(&g_ftest_start, 1);
        set_status("Test in Dream: your map plays now (Test again to go back to building)");
    }
}
static void cr_ed_new(int page) {                                  // A on an Edgar's Dream creator's first row: a new item
    EdCreator &c = g_edc[page];
    if (c.n >= ed_cap(page)) { set_status("No room for another one - delete one first"); return; }
    EdItem &it = c.item[c.n];
    if (c.n > 0) it = c.item[c.cur];
    else { memset(&it, 0, sizeof(it)); for (int fi = 0; fi < c.nf && fi < 40; fi++) it.v[fi] = c.f[fi].def; }
    sprintf_s(it.name, "My %s %d", c.word ? c.word : "item", c.n + 1);
    c.cur = c.n++;
    ed_save(page);
    set_status("New one made");
}
static void cr_run(const CrCmd &q) {
    char msg[96];
    switch (q.op) {
    case CR_F_CHOOSE:
        if (q.a == 30) { InterlockedExchange(&g_cr_tab, CR_T_SKILL); break; }
        cr_with_menu(q.a, 0, false);
        break;
    case CR_F_TURN: cr_with_menu(q.a, q.b, true); break;
    case CR_F_SPAWN: f_post(F_SPAWN, q.a); break;
    case CR_F_MARK: f_post(F_MARK, q.a); break;
    case CR_F_GOTO: f_post(F_GOTO, q.a); break;
    case CR_F_GOTO_SP: f_post(F_GOTO_SP, q.a); break;
    case CR_F_OP: f_post(q.a); break;
    case CR_SC_OPEN:
        if (!g_cr_sc_opened) { sc_open(); sc_snapshot(); InterlockedExchange(&g_cr_sc_opened, 1); }
        break;
    case CR_SC_TURN: sc_turn(q.a, q.b); break;
    case CR_SC_CHOOSE: {
        int id = sc_id(q.a);
        if (id == 15) break;                                       // "Back": there is nowhere to go back to here
        if (id == 13) { sc_choose(q.a); InterlockedExchange(&g_fmenu, 0); break; }   // the capsule: drop it, no menu over the view
        sc_choose(q.a);
        if (g_fmenu == 2 || g_fmenu == 5 || g_fmenu == 7) InterlockedExchange(&g_fmenu, 0);
        break;
    }
    case CR_ED_PICK: if (q.a >= 0 && q.a < EDP_N && q.b >= 0 && q.b < g_edc[q.a].n) { g_edc[q.a].cur = q.b; ed_state_save(); } break;
    case CR_ED_NEW: if (q.a >= 0 && q.a < EDP_N) cr_ed_new(q.a); break;
    case CR_ED_CHANGE: if (q.a >= 0 && q.a < EDP_N && g_edc[q.a].n > 0) ed_change(q.a, q.b, q.c); break;
    case CR_ED_DEFAULT: {
        EdCreator &c = g_edc[q.a];
        int fi = ed_fld(q.a, q.b);
        if (c.n > 0 && fi >= 0 && fi < c.nf) { c.item[c.cur].v[fi] = c.f[fi].def; ed_save(q.a); set_status("Back to its default"); }
        break;
    }
    case CR_ED_NAME: {
        EdCreator &c = g_edc[q.a];
        if (c.n > 0 && g_cr_namebuf[0]) { strncpy_s(c.item[c.cur].name, g_cr_namebuf, _TRUNCATE); ed_save(q.a); set_status("Name saved"); }
        break;
    }
    case CR_ED_ACTION: {
        EdCreator &c = g_edc[q.a];
        if (q.b < 0 || q.b >= c.na) break;
        const char *s = c.actions[q.b];
        if (!strncmp(s, "Try it now", 10) || !strncmp(s, "Preview live", 12) || !strncmp(s, "3 VS 3", 6)) {
            sprintf_s(msg, "%.24s starts its own match: Exit CREATE, then use it on the Edgar's Dream page", s);
            set_status(msg);
            break;
        }
        if (!strncmp(s, "Back", 4)) break;
        if (c.n <= 0 && strncmp(s, "New", 3)) { set_status("Make one first (New)"); break; }
        ed_action(q.a, q.b);
        break;
    }
    case CR_SAVE:
        if (q.a == CR_T_STAGE) f_post(F_SAVE);
        else if (q.a == CR_T_SKILL) { if (g_cr_sc_opened) sc_save_now(); }
        else if (cr_page(q.a) >= 0) { ed_save(cr_page(q.a)); set_status("Saved"); }
        break;
    case CR_UNDO: if (q.a == CR_T_SKILL) { if (g_cr_sc_opened) sc_undo(); } else f_post(F_UNDO); break;
    case CR_REDO: if (q.a == CR_T_SKILL) { if (g_cr_sc_opened) sc_redo(); } else f_post(F_REDO); break;
    case CR_TEST: cr_test_toggle(); break;
    case CR_EXIT:                                                  // back to Edgar's Dream (as the pause menu's exits do)
        if (q.a) f_post(F_SAVE);
        if (q.a && g_cr_sc_opened) sc_save_now();
        InterlockedExchange(&g_fexit_save, 0);
        InterlockedExchange(&g_ed_try_page, EDP_CHOOSER);
        InterlockedExchange(&g_fexit_to_ed, 1);
        InterlockedExchange(&g_fexit_phase, 1);
        logf("CREATE: exit (%s)", q.a ? "saved" : "not saved");
        break;
    }
}

// the pad, while L3 + R3 gave it to the editor: LB / RB tabs, Up / Down rows, Left / Right change, A choose, X default,
// B hands the pad back to the fighter
static int cr_rows_now();
static void cr_pad_row(int dir);
static void cr_pad_act(int what);
static bool cr_input(WORD b, WORD pressed, bool in_match) {        // worker thread, before forge_input
    (void)b; (void)pressed;
    if (!g_forge_session) {
        if (g_cr_entry) { InterlockedExchange(&g_cr_entry, 0); InterlockedExchange(&g_cr_sc_opened, 0); logf("CREATE: session over"); }
        g_crq_r = g_crq_w;
        return false;
    }
    while (g_crq_r != g_crq_w) {
        CrCmd q = g_crq[g_crq_r & 63];
        InterlockedIncrement(&g_crq_r);
        __try { cr_run(q); } __except (EXCEPTION_EXECUTE_HANDLER) { logf("CREATE: command %d faulted - skipped", q.op); }
    }
    if (!(g_dp_on && g_ui_state == 1 && in_match && g_dp_pad_editor)) return false;
    static UINT32 prev;
    UINT32 pb = g_dp_pad_buttons, pp = pb & ~prev;
    prev = pb;
    // Windows.Gaming.Input buttons: A 4, B 8, X 0x10, Y 0x20, Up 0x40, Down 0x80, Left 0x100, Right 0x200, LB 0x400, RB 0x800
    if (pp & 0x400) InterlockedExchange(&g_cr_tab, (g_cr_tab + CR_T_N - 1) % CR_T_N);
    if (pp & 0x800) InterlockedExchange(&g_cr_tab, (g_cr_tab + 1) % CR_T_N);
    if (pp & 0x40) cr_pad_row(-1);
    if (pp & 0x80) cr_pad_row(1);
    if (pp & 0x100) cr_pad_act(-1);
    if (pp & 0x200) cr_pad_act(1);
    if (pp & 0x4) cr_pad_act(0);
    if (pp & 0x10) cr_pad_act(2);
    if (pp & 0x8) InterlockedExchange(&g_dp_pad_editor, 0);
    return true;
}

// ---- keys (render thread, from dp_frame): Ctrl S save, Ctrl Z undo, Ctrl Y redo, F5 test, Enter / Esc for the name ----
static void cr_keys() {
    if (!g_cr_entry || !g_dp_kbm_editor) return;
    static bool was[6];
    bool ctrl = dp_key(17);
    bool now[6] = {ctrl && dp_key('S'), ctrl && dp_key('Z'), ctrl && dp_key('Y'), dp_key(116), dp_key(13), dp_key(27)};
    volatile LONG *flag[6] = {&g_cr_key_save, &g_cr_key_undo, &g_cr_key_redo, &g_cr_key_test, &g_cr_key_enter, &g_cr_key_esc};
    for (int k = 0; k < 6; k++) { if (now[k] && !was[k]) InterlockedExchange(flag[k], 1); was[k] = now[k]; }
}

// ---- drawing (render thread) ----
static bool cr_btn(float x, float y, float w, float h, const char *label, int kind, bool click) {   // kind 0, 1 on, 2 play, 3 danger
    bool hov = ui_hit(x, y, w, h);
    DWORD bg = kind == 2 ? 0xFF1F3A24 : kind == 1 ? UI_ON : kind == 3 ? 0xFF3A1A1A : UI_BTN;
    DWORD ln = kind == 2 ? 0xFF4F9A5B : kind == 1 ? UI_ON_LINE : kind == 3 ? 0xFF9A4F4F : UI_BTN_LINE;
    if (hov) { bg = kind == 2 ? 0xFF2B5233 : kind == 3 ? 0xFF522B2B : 0xFF2C2618; ln = UI_GOLD; }
    ui_rect(x, y, w, h, bg); ui_frame(x, y, w, h, ln);
    DWORD tc = kind == 2 ? 0xFFBFF0C6 : kind == 3 ? 0xFFF0C6C6 : kind == 1 || hov ? 0xFFFFD77A : 0xFFDDD5C4;
    float tw = ui_text_w(2, label);
    if (tw > w - 8) tw = w - 8;
    ui_text(2, x + (w - tw) / 2, y + (h - 25) / 2, label, tc);
    return hov && click;
}
static void cr_clip(char *s, size_t n, int font, float w) {       // shorten to fit w, ending in ".."
    (void)n;
    if (ui_text_w(font, s) <= w) return;
    size_t l = strlen(s);
    while (l > 3) { s[--l] = 0; if (ui_text_w(font, s) + ui_text_w(font, "..") <= w) break; }
    strcat_s(s, n, "..");
}
// v5.172 (owner 2026-11-13; in game: "Play this ma..", "Collision foll..", "Photon Barr.."):
// the Attributes panel uses short names (the Stage Creator's own menu keeps the long ones; hovering a row shows its full
// description), and the - / + rows give the value more room
static const char *CR_SHORT[] = {NULL, NULL, "Reset map", "Undo", NULL, "Snap", "Precision", NULL, "In matches", "Freeze CPUs",
    "Hide HUD", "Hide me", "Highlight", NULL, NULL, "Spawn fx", "Zone fx", "Markers", "Capsules", "Collision", "Capsule",
    "More copies", "Menu box", "FX skill", "FX size", "FX part", "FX speed", "FX hold", "Place effect", NULL, NULL, NULL};
static_assert(sizeof(CR_SHORT) / sizeof(CR_SHORT[0]) == 32, "one short name per Stage Creator row");
// v5.172.7 (owner 2026-11-13; in game the Skill / Outfit / Character tabs
// cut a dozen labels and values with ".."): a label or value that does not fit at the row font (Segoe UI 22) gets its
// short name, then a simpler form (no brackets, no "from", "Kind value N" -> "Kind N"), then the 18 px font; only then a cut.
// Returns the font used (1 or 2).
static const char *CR_ALIAS[][2] = {
    {"Find: school / kind", "Filter"}, {"Find: name starts with", "Name filter"}, {"Closest skill", "Closest"},
    {"Projectile extra", "Extra shot"}, {"Color (hue)", "Hue"}, {"Capsule type", "Capsule"}, {"Aura level on use", "Level cost"},
    {"Cost change per use", "Cost / use"}, {"Health on use", "Health cost"}, {"Where it works", "Where"},
    {"Reset to the base skill", "Reset to base"}, {"Skill family", "Family"}, {"Color brightness", "Brightness"},
    {"Color strength", "Strength"}, {"Replace the aura", "Replace aura"}, {"Aura effect part", "Effect part"},
    {"Aura effect shows", "Effect shows"}, {"Skill motions from", "Skill motions"}, {"Select-screen face", "Select face"},
    {"Head accessory", "Accessory"}, {"Effect placement", "Placement"}, {"Replaces quest", "Replaces"},
    {"Stage version", "Version"}, {"Mission picture", "Picture"}, {"Starting capsules", "Capsules"},
    {"Game rule it plays on", "Game rule"}, {"a new one every", "Every"}, {"Opponents' life", "Their life"},
    {"Capsules refill after", "Refill"}, {"Win rules from quest", "Win rules"}, {"Your start point", "Start point"},
    {"First-clear credits", "Credits"}, {"Opponents in all", "In all"}, {"Reinforcements", "Reinforce"},
};
static void cr_simplify(char *s, size_t n) {
    while (s[0] == ' ') memmove(s, s + 1, strlen(s));
    for (auto &a : CR_ALIAS) if (!_stricmp(s, a[0])) { strcpy_s(s, n, a[1]); return; }
    char *br = strstr(s, " ("); if (br && br > s + 2) *br = 0;
    size_t l = strlen(s); if (l > 5 && !strcmp(s + l - 5, " from")) s[l - 5] = 0;
    char *kv = strstr(s, "Kind value "); if (kv) memmove(kv + 5, kv + 11, strlen(kv + 11) + 1);
    // "Effect left / right", "move up / down": the pair alone ("Left / right")
    char *sl = strstr(s, " / ");
    if (sl) { char *sp = strchr(s, ' '); if (sp && sp < sl) { memmove(s, sp + 1, strlen(sp + 1) + 1); if (s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; } }
    static const char *PRE[][2] = {{"environment ", "Env "}, {"Environment ", "Env "}, {"Banned skill ", "Ban "}, {"opponent ", "Opp "},
                                   {"Opponent ", "Opp "}, {"Walk turns", "Turns"}};
    for (auto &p : PRE) { size_t pl = strlen(p[0]); if (!_strnicmp(s, p[0], pl)) { char t[64]; sprintf_s(t, "%s%s", p[1], s + pl); strcpy_s(s, n, t); } }
}
// whole words only: the last word goes until it fits (never ending on a stop word), ".." when a value lost words
static void cr_words(char *s, size_t n, int font, float room, bool dots) {
    static const char *STOP[] = {"the", "of", "a", "to", "and", "in", "on", "with", "-", "/", "+"};
    bool cut = false;
    for (;;) {
        char t[96]; sprintf_s(t, "%s%s", s, cut && dots ? ".." : "");
        if (ui_text_w(font, t) <= room) break;
        char *sp = strrchr(s, ' ');
        if (!sp || sp == s) { cr_clip(s, n, font, room); return; }
        *sp = 0; cut = true;
        for (char *q = strrchr(s, ' '); q; q = strrchr(s, ' ')) {
            bool stop = false;
            for (const char *w : STOP) if (!_stricmp(q + 1, w)) stop = true;
            if (!stop) break;
            *q = 0;
        }
    }
    if (cut && dots) strcat_s(s, n, "..");
}
static int cr_fit(char *s, size_t n, float room, bool label) {
    if (ui_text_w(1, s) <= room) return 1;
    if (label) { cr_simplify(s, n); if (ui_text_w(1, s) <= room) return 1; }
    else {                                                            // a value: its part before " (" / " - " first
        char *c = strstr(s, " ("); if (!c) c = strstr(s, " - ");
        if (c && c - s >= 4) { char t[96]; strncpy_s(t, s, (size_t)(c - s)); if (ui_text_w(2, t) <= room) { strcpy_s(s, n, t); return ui_text_w(1, s) <= room ? 1 : 2; } }
    }
    if (ui_text_w(2, s) <= room) return 2;
    cr_words(s, n, 2, room, !label);
    return 2;
}
// one Attributes row: label, value, - / + (turn), a click on the row = choose
struct CrRow { char label[48], value[64]; bool turns, choose, sel; };
static int cr_attr_row(float y, const CrRow &r, bool click) {      // 1 = -, 2 = +, 3 = the row itself
    const float X = 1580, W = 340, H = 34;
    int hit = 0;
    bool hov = ui_hit(X, y, W, H);
    if (r.sel) { ui_rect(X + 1, y, W - 2, H, UI_SEL); ui_rect(X + 1, y, 3, H, UI_GOLD); }
    else if (hov) ui_rect(X + 1, y, W - 2, H, 0xFF1F2128);
    float room = r.turns ? 104.0f : r.value[0] ? 132.0f : 300.0f;   // v5.172 - / + rows: 104
    { char lb[48]; strcpy_s(lb, r.label); int f = cr_fit(lb, sizeof(lb), room, true); ui_text(f, X + 12, y + (f == 1 ? 3 : 6), lb, r.choose ? 0xFFFFE2A0 : 0xFFB9B19F); }
    if (r.turns) {                                               // v5.172 '-' at +120: the value gets W - 186 (was W - 214)
        if (cr_btn(X + 120, y + 4, 24, 26, "-", 0, click)) hit = 1;
        if (cr_btn(X + W - 32, y + 4, 24, 26, "+", 0, click)) hit = 2;
        char v[64]; strcpy_s(v, r.value); int f = cr_fit(v, sizeof(v), W - 186, false);
        float vw = ui_text_w(f, v);
        ui_text(f, X + 146 + (W - 186 - vw) / 2, y + (f == 1 ? 3 : 6), v, 0xFFF2EAD8);
    } else if (r.value[0]) {
        char v[64]; strcpy_s(v, r.value); int f = cr_fit(v, sizeof(v), 180, false);
        ui_text(f, X + W - 12 - ui_text_w(f, v), y + (f == 1 ? 3 : 6), v, 0xFFF2EAD8);
    }
    ui_rect(X + 1, y + H - 1, W - 2, 1, 0xFF1F2126);
    if (!hit && hov && click) hit = 3;
    return hit;
}
static bool cr_scroll_btns(int pane, float x, float y, int total, int shown, bool click) {   // ^ v at a panel's head
    if (total <= shown) { g_cr_scroll[pane] = 0; return false; }
    if (cr_btn(x, y, 30, 26, "^", 0, click)) g_cr_scroll[pane] -= shown - 1;
    if (cr_btn(x + 34, y, 30, 26, "v", 0, click)) g_cr_scroll[pane] += shown - 1;
    if (g_cr_scroll[pane] > total - shown) g_cr_scroll[pane] = total - shown;
    if (g_cr_scroll[pane] < 0) g_cr_scroll[pane] = 0;
    return true;
}

// Attributes rows of the current tab (also used by the pad)
static int cr_rows_now() {
    int t = g_cr_tab;
    if (t == CR_T_STAGE) { int o[40]; return cr_stage_rows(o); }
    if (t == CR_T_SKILL) return g_cr_sc_opened ? sc_rows_n() : 0;
    int pg = cr_page(t);
    return pg >= 0 ? ed_rows(pg) - 1 : 0;                          // the item row is the Ledger's; Name, fields, actions here
}
static void cr_pad_row(int dir) {
    int n = cr_rows_now();
    if (n <= 0) return;
    g_cr_sel = g_cr_sel < 0 ? 0 : (g_cr_sel + dir + n) % n;
    if (g_cr_sel < g_cr_scroll[1]) g_cr_scroll[1] = g_cr_sel;
    if (g_cr_sel >= g_cr_scroll[1] + 19) g_cr_scroll[1] = g_cr_sel - 18;
}
static void cr_pad_act(int what) {                                 // -1 / 1 change, 0 choose, 2 default (worker thread)
    int t = g_cr_tab, r = g_cr_sel;
    if (r < 0) return;
    CrCmd q = {CR_NONE, 0, 0, 0};
    if (t == CR_T_STAGE) {
        int o[40], n = cr_stage_rows(o);
        if (r >= n) return;
        q = what == 0 || !cr_f_turns(o[r]) ? CrCmd{CR_F_CHOOSE, o[r], 0, 0} : CrCmd{CR_F_TURN, o[r], what, 0};
        if (what == 2) return;
    } else if (t == CR_T_SKILL) {
        if (!g_cr_sc_opened) return;
        q = what == 0 ? CrCmd{CR_SC_CHOOSE, r, 0, 0} : what == 2 ? CrCmd{CR_NONE, 0, 0, 0} : CrCmd{CR_SC_TURN, r, what, 0};
    } else {
        int pg = cr_page(t), row = r + 1;                          // ed rows: 1 name, 2.. fields, then actions
        if (pg < 0) return;
        const EdCreator &c = g_edc[pg];
        if (row >= 2 + c.nf) { if (what == 0) q = {CR_ED_ACTION, pg, row - 2 - c.nf, 0}; }
        else if (row >= 2) q = what == 2 ? CrCmd{CR_ED_DEFAULT, pg, row, 0} : what == 0 ? CrCmd{CR_NONE, 0, 0, 0} : CrCmd{CR_ED_CHANGE, pg, row, what};
    }
    if (q.op != CR_NONE) cr_run(q);
}

static void cr_dream_bar(bool click) {
    int t = g_cr_tab;
    ui_rect(0, 0, 1920, 58, 0xF0121419); ui_rect(0, 56, 1920, 2, 0xFF8A6A27);
    ui_text(4, 18, 4, "CREATE", UI_GOLD);
    float x = 190;
    for (int k = 0; k < CR_T_N; k++) {
        float w = ui_text_w(2, CR_TAB_NAMES[k]) + 30;
        if (cr_btn(x, 10, w, 38, CR_TAB_NAMES[k], k == t ? 1 : 0, click) && k != t) {
            InterlockedExchange(&g_cr_tab, k); g_cr_sel = -1; g_cr_typing = false; g_cr_scroll[0] = g_cr_scroll[1] = g_cr_scroll[2] = 0;
            if (k == CR_T_SKILL && !g_cr_sc_opened) cr_post(CR_SC_OPEN);
        }
        x += w + 6;
    }
    x += 10; ui_rect(x, 12, 1, 34, UI_LINE); x += 12;
    if (cr_btn(x, 10, 96, 38, "Save", 0, click) || InterlockedExchange(&g_cr_key_save, 0)) cr_post(CR_SAVE, t);
    x += 102;
    bool ur = t == CR_T_STAGE || t == CR_T_SKILL;
    if (ur) {
        if (cr_btn(x, 10, 80, 38, "Undo", 0, click) || InterlockedExchange(&g_cr_key_undo, 0)) cr_post(CR_UNDO, t);
        x += 86;
        if (cr_btn(x, 10, 80, 38, "Redo", 0, click) || InterlockedExchange(&g_cr_key_redo, 0)) cr_post(CR_REDO, t);
        x += 86;
    } else { InterlockedExchange(&g_cr_key_undo, 0); InterlockedExchange(&g_cr_key_redo, 0); }
    if (cr_btn(x, 10, 230, 38, g_ftesting ? "Back to building" : "Test in Dream  F5", 2, click) || InterlockedExchange(&g_cr_key_test, 0)) cr_post(CR_TEST);
    x += 236;
    if (cr_btn(1690, 10, 100, 38, "Exit", 3, click)) cr_post(CR_EXIT, 0);
    if (cr_btn(1796, 10, 116, 38, "Save + exit", 3, click)) cr_post(CR_EXIT, 1);
}

static void cr_stage(bool click) {
    // Ledger: categories (Markers first), then the stage's markers (click = fly there)
    ui_panel(0, 58, 330, 742, "LEDGER", "Stage");
    float y = 98;
    ui_text(2, 14, y, "Categories", UI_ON_LINE); y += 30;
    for (int c = 0; c < g_fcat_n && y < 520; c++) {
        bool sel = c == g_cr_cat, hov = ui_hit(0, y, 330, 30);
        if (sel) { ui_rect(1, y, 328, 30, UI_SEL); ui_rect(1, y, 3, 30, UI_GOLD); } else if (hov) ui_rect(1, y, 328, 30, 0xFF1F2128);
        char s[48]; sprintf_s(s, "%.30s", g_fcat[c]);
        ui_text(1, 18, y + 1, s, sel ? 0xFFFFE2A0 : 0xFFCFC6B2);
        if (hov && click) { g_cr_cat = c; g_cr_scroll[2] = 0; }
        y += 30;
    }
    y += 8; ui_text(2, 14, y, "Markers and starts", UI_ON_LINE); y += 30;
    int nsp = f_stage_spawn_n();
    char s[64];
    for (int i = 0; i < g_mk_n && y < 734; i++) {
        bool hov = ui_hit(0, y, 330, 28);
        if (hov) ui_rect(1, y, 328, 28, 0xFF1F2128);
        sprintf_s(s, "%.10s %d   (%.0f, %.0f, %.0f)", g_mk[i].type, i + 1, g_mk[i].p[0] / 10, g_mk[i].p[1] / 10, g_mk[i].p[2] / 10);
        ui_text(1, 18, y, s, 0xFFBDB5A3);
        if (hov && click) cr_post(CR_F_GOTO, i);
        y += 28;
    }
    for (int i = 0; i < nsp && y < 734; i++) {
        bool hov = ui_hit(0, y, 330, 28);
        if (hov) ui_rect(1, y, 328, 28, 0xFF1F2128);
        sprintf_s(s, "Stage start %d", i + 1);
        ui_text(1, 18, y, s, 0xFFBDB5A3);
        if (hov && click) cr_post(CR_F_GOTO_SP, i);
        y += 28;
    }
    ui_text(1, 14, 770, "Click a marker or start to fly to it", 0xFF8A8373);
    // Attributes: the Forge menu's rows
    int o[40], n = cr_stage_rows(o);
    ui_panel(1580, 58, 340, 742, "ATTRIBUTES", NULL);
    cr_scroll_btns(1, 1580 + 340 - 72, 62, n, 19, click);
    for (int i = 0; i < 19 && g_cr_scroll[1] + i < n; i++) {
        int k = g_cr_scroll[1] + i, r = o[k];
        CrRow row = {};
        strcpy_s(row.label, CR_SHORT[r] ? CR_SHORT[r] : F_MENU_NAMES[r]);   // v5.172 names that fit the panel (hover: the full text)
        cr_f_val(r, row.value, sizeof(row.value));
        row.turns = cr_f_turns(r); row.choose = !row.turns; row.sel = k == g_cr_sel;
        int h = cr_attr_row(96 + i * 34.0f, row, click);
        if (h == 1 || h == 2) cr_post(CR_F_TURN, r, h == 1 ? -1 : 1);
        else if (h == 3) cr_post(row.turns ? CR_F_TURN : CR_F_CHOOSE, r, 1);
        if (ui_hit(1580, 96 + i * 34.0f, 340, 34)) { ui_rect(1580, 744, 340, 54, 0xFF15171C); ui_text(1, 1590, 748, F_MENU_DESC[r], 0xFFA79F8D); }
    }
    // Vault: the chosen category's objects (click = one in your hands), or the four markers to place
    ui_panel(0, 800, 1920, 280, "VAULT", g_hold >= 0 ? "holding one: Place, Lock, Copy or Delete" : "click to take one - it appears in front of you");
    float bx = 1180;
    if (cr_btn(bx, 838, 120, 34, "Place", 2, click)) cr_post(CR_F_OP, F_PLACE); bx += 126;
    if (cr_btn(bx, 838, 150, 34, "Place + lock", 0, click)) cr_post(CR_F_OP, F_PLACE_LOCK); bx += 156;
    if (cr_btn(bx, 838, 110, 34, "Copy", 0, click)) cr_post(CR_F_OP, F_COPY); bx += 116;
    if (cr_btn(bx, 838, 110, 34, "Delete", 3, click)) cr_post(CR_F_OP, F_DELETE); bx += 116;
    if (cr_btn(bx, 838, 110, 34, "Lock", 0, click)) cr_post(CR_F_OP, F_LOCK);
    if (g_cr_cat == 0) {
        for (int k = 0; k < 4; k++) {
            float ix = 14 + k * 230, iy = 884;
            char l[40]; sprintf_s(l, "Place %s", F_MARK_TYPES[k]);
            if (cr_btn(ix, iy, 220, 60, l, k == 0 ? 1 : 0, click)) cr_post(CR_F_MARK, k);
            sprintf_s(l, "%d placed", f_marks_of(k));
            ui_text(1, ix + 4, iy + 66, l, 0xFF9A9384);
        }
        ui_text(1, 14, 1046, "Markers go where you stand. Stage creator spawn markers are the match's starts (6 = all six players).", 0xFF8A8373);
        return;
    }
    int ids[256], m = 0;
    for (int k = 0; k < g_fd_n && m < 256; k++) if (g_fd[k].cat == g_cr_cat) ids[m++] = k;
    const int PER = 26;                                            // 13 x 2
    cr_scroll_btns(2, 1110, 840, (m + 12) / 13, 2, click);
    for (int i = 0; i < PER; i++) {
        int j = g_cr_scroll[2] * 13 + i;
        if (j >= m) break;
        const FDesign &d = g_fd[ids[j]];
        float ix = 14 + (i % 13) * 146, iy = 884 + (i / 13) * 92;
        bool hov = ui_hit(ix, iy, 138, 84), none = d.placed >= d.total && !g_fcopies;
        ui_rect(ix, iy, 138, 84, hov ? 0xFF2C2618 : 0xFF22252C); ui_frame(ix, iy, 138, 84, hov ? UI_GOLD : 0xFF3D382C);
        // v5.172 (owner 2026-11-13; in game every tile read "Edgar's: Wal.."): the name on
        // two lines - the stage before the ": " on the first (dim), the piece on the second - each cut only if still too long
        char nm[48]; sprintf_s(nm, "%.40s", d.disp ? d.disp : "Object");
        char *cut = strstr(nm, ": ");
        if (cut && ui_text_w(1, nm) > 128) {
            *cut = 0; char piece[48]; strcpy_s(piece, cut + 2);
            cr_clip(nm, sizeof(nm), 1, 128); cr_clip(piece, sizeof(piece), 1, 128);
            ui_text(1, ix + 5, iy + 4, nm, none ? 0xFF6F6A5E : 0xFF8A8373);
            ui_text(1, ix + 5, iy + 26, piece, none ? 0xFF6F6A5E : 0xFFD8CFBB);
        } else { cr_clip(nm, sizeof(nm), 1, 128); ui_text(1, ix + 5, iy + 6, nm, none ? 0xFF6F6A5E : 0xFFD8CFBB); }
        char c[32]; sprintf_s(c, "%d / %d placed", d.placed, d.total);
        ui_text(1, ix + 5, iy + 54, c, 0xFF8A8373);
        if (hov && click) { if (none) set_status("Every copy is placed - turn on More copies on demand"); else cr_post(CR_F_SPAWN, d.obj); }
    }
}

static void cr_skill(bool click) {
    ui_panel(0, 58, 330, 742, "LEDGER", "Skill");
    if (!g_cr_sc_opened) { ui_text(2, 14, 100, "Opening the Skill Creator...", UI_DIM); ui_panel(1580, 58, 340, 742, "ATTRIBUTES", NULL); ui_panel(0, 800, 1920, 280, "VAULT", NULL); return; }
    char card[5][96] = {};
    sc_card(card);
    for (int k = 0; k < 5; k++) { char l[96]; strcpy_s(l, card[k]); cr_clip(l, sizeof(l), 1, 300); ui_text(1, 14, 100 + k * 30.0f, l, k ? 0xFFCFC6B2 : UI_GOLD); }
    ui_text(1, 14, 270, "The card as the game will show it.", 0xFF8A8373);
    ui_text(1, 14, 296, "Damage cost stays the default until", 0xFF8A8373);
    ui_text(1, 14, 320, "you set one.", 0xFF8A8373);
    int n = sc_rows_n();
    ui_panel(1580, 58, 340, 742, "ATTRIBUTES", NULL);
    cr_scroll_btns(1, 1580 + 340 - 72, 62, n, 19, click);
    const char *hover_desc = NULL;
    for (int i = 0; i < 19 && g_cr_scroll[1] + i < n; i++) {
        int r = g_cr_scroll[1] + i, id = sc_id(r);
        CrRow row = {};
        const char *desc = NULL;
        strcpy_s(row.label, sc_name(r) ? sc_name(r) : "?");
        sc_row(r, row.value, sizeof(row.value), &desc);
        bool action = id == 12 || id == 13 || id == 14 || id == 33 || id == 0;
        if (id == 15) continue;
        row.turns = !action; row.choose = action; row.sel = r == g_cr_sel;
        float y = 96 + i * 34.0f;
        int h = cr_attr_row(y, row, click);
        if (h == 1 || h == 2) cr_post(CR_SC_TURN, r, h == 1 ? -1 : 1);
        else if (h == 3) cr_post(action ? CR_SC_CHOOSE : CR_SC_TURN, r, 1);
        if (ui_hit(1580, y, 340, 34) && desc) hover_desc = desc;
    }
    if (hover_desc) { char l[96]; sprintf_s(l, "%.90s", hover_desc); ui_rect(1580, 744, 340, 54, 0xFF15171C); cr_clip(l, sizeof(l), 1, 320); ui_text(1, 1590, 748, l, 0xFFA79F8D); }
    ui_panel(0, 800, 1920, 280, "VAULT", "the skill's actions");
    struct { const char *l; int id; int kind; } A[] = {{"Save skill", 12, 2}, {"Put in my hand", 14, 1}, {"Drop a capsule", 13, 0}, {"Reset to the source", 33, 3}};
    for (int k = 0; k < 4; k++) {
        if (cr_btn(14 + k * 250, 850, 240, 60, A[k].l, A[k].kind, click)) {
            int r = sc_row_of(A[k].id);
            if (r >= 0) cr_post(CR_SC_CHOOSE, r);
        }
    }
    ui_text(1, 14, 930, "Test in Dream plays the stage; Put in my hand gives you the skill to try it at once.", 0xFF8A8373);
}

static void cr_creator(bool click) {
    int pg = cr_page(g_cr_tab);
    if (pg < 0) return;
    EdCreator &c = g_edc[pg];
    char s[96];
    // Ledger: the items (click = edit that one), + New
    sprintf_s(s, "%d / %d", c.n, ed_cap(pg));
    ui_panel(0, 58, 330, 742, "LEDGER", s);
    if (cr_btn(14, 98, 302, 34, c.n ? "New (a copy of this one)" : "New", 2, click)) cr_post(CR_ED_NEW, pg);
    for (int i = 0; i < c.n && i < 18; i++) {
        float y = 140 + i * 32.0f;
        bool sel = i == c.cur, hov = ui_hit(0, y, 330, 32);
        if (sel) { ui_rect(1, y, 328, 32, UI_SEL); ui_rect(1, y, 3, 32, UI_GOLD); } else if (hov) ui_rect(1, y, 328, 32, 0xFF1F2128);
        ui_text(1, 18, y + 2, c.item[i].name, sel ? 0xFFFFE2A0 : 0xFFCFC6B2);
        if (hov && click && !sel) { cr_post(CR_ED_PICK, pg, i); g_cr_typing = false; }
    }
    if (!c.n) ui_text(1, 14, 150, "Nothing made yet: click New.", UI_DIM);
    // Attributes: Name (type it), the fields with - / +
    ui_panel(1580, 58, 340, 742, "ATTRIBUTES", NULL);
    if (c.n) {                                                        // v5.176.1 the item's name between the title and ^ v (it ran under them)
        char nm[48]; strcpy_s(nm, c.item[c.cur].name);
        float x0 = 1580 + 14 + ui_text_w(3, "ATTRIBUTES") + 12, x1 = 1580 + 340 - 80;
        cr_clip(nm, sizeof(nm), 2, x1 - x0);
        ui_text(2, x1 - ui_text_w(2, nm), 64, nm, 0xFF7D776A);
    }
    int nrow = 1 + c.nf;                                           // Name + fields (actions go to the Vault)
    cr_scroll_btns(1, 1580 + 340 - 72, 62, nrow, 19, click);
    const char *hover_help = NULL;
    for (int i = 0; i < 19 && g_cr_scroll[1] + i < nrow; i++) {
        int k = g_cr_scroll[1] + i, row = k + 1;                   // ed row (1 = name)
        float y = 96 + i * 34.0f;
        CrRow r = {};
        r.sel = k == g_cr_sel;
        if (row == 1) {
            strcpy_s(r.label, "Name");
            if (g_cr_typing) sprintf_s(r.value, "%s%s", g_dp_name, ((GetTickCount64() / 450) & 1) ? "|" : "");
            else strcpy_s(r.value, c.n ? c.item[c.cur].name : "-");
            r.choose = true;
            if (cr_attr_row(y, r, click) == 3 && c.n && g_dp_kbm_editor) { g_cr_typing = true; strcpy_s(g_dp_name, c.item[c.cur].name); }
            continue;
        }
        int fi = ed_fld(pg, row);
        if (fi < 0 || fi >= c.nf) continue;
        strcpy_s(r.label, c.f[fi].label ? c.f[fi].label : "?");
        if (c.n) ed_value(pg, fi, c.item[c.cur].v[fi], r.value, sizeof(r.value)); else strcpy_s(r.value, "-");
        r.turns = c.n > 0;
        int h = cr_attr_row(y, r, click);
        if (h == 1 || h == 2) cr_post(CR_ED_CHANGE, pg, row, h == 1 ? -1 : 1);
        else if (h == 3 && c.n) cr_post(CR_ED_CHANGE, pg, row, 1);
        if (ui_hit(1580, y, 340, 34) && c.f[fi].help) hover_help = c.f[fi].help;
    }
    if (g_cr_typing) {
        ui_rect(1580, 744, 340, 54, 0xFF15171C); ui_text(1, 1590, 748, "Type the name - Enter keeps it, Esc cancels", 0xFFFFD77A);
        if (InterlockedExchange(&g_cr_key_enter, 0)) { strcpy_s(g_cr_namebuf, g_dp_name); cr_post(CR_ED_NAME, pg); g_cr_typing = false; }
        if (InterlockedExchange(&g_cr_key_esc, 0)) g_cr_typing = false;
    } else if (hover_help) { char l[96]; sprintf_s(l, "%.90s", hover_help); cr_clip(l, sizeof(l), 1, 320); ui_rect(1580, 744, 340, 54, 0xFF15171C); ui_text(1, 1590, 748, l, 0xFFA79F8D); }
    // Vault: the creator's actions, and how to use it
    ui_panel(0, 800, 1920, 280, "VAULT", c.title);
    float x = 14, y = 846;
    for (int a = 0; a < c.na; a++) {
        const char *l = c.actions[a];
        if (!l || !strncmp(l, "Back", 4)) continue;
        float w = ui_text_w(2, l) + 34;
        if (w > 460) w = 460;
        if (x + w > 1906) { x = 14; y += 44; }
        if (y + 38 > 996) break;                                   // v5.127.2 never into the how-to lines
        bool away = !strncmp(l, "Try it now", 10) || !strncmp(l, "Preview live", 12) || !strncmp(l, "3 VS 3", 6);
        bool del = !strncmp(l, "Delete", 6);
        if (cr_btn(x, y, w, 38, l, away ? 0 : del ? 3 : 1, click)) cr_post(CR_ED_ACTION, pg, a);
        x += w + 8;
    }
    if (c.howto) for (int k = 0; k < 3 && c.howto[k]; k++) { char l[120]; sprintf_s(l, "%.110s", c.howto[k]); cr_clip(l, sizeof(l), 1, 1880); ui_text(1, 14, 1000 + k * 24.0f, l, 0xFF8A8373); }
}

static void cr_compose(bool click) {
    ui_base();
    int t = g_cr_tab;
    if (t == CR_T_SKILL && !g_cr_sc_opened) { static ULONGLONG asked; ULONGLONG now = GetTickCount64(); if (now - asked > 1000) { asked = now; cr_post(CR_SC_OPEN); } }
    if (!g_cr_typing) { g_dp_name[0] = 0; InterlockedExchange(&g_cr_key_enter, 0); InterlockedExchange(&g_cr_key_esc, 0); }
    // a click inside the game picture belongs to the game view, not to a panel
    cr_dream_bar(click);
    if (t == CR_T_STAGE) cr_stage(click);
    else if (t == CR_T_SKILL) cr_skill(click);
    else cr_creator(click);
    // the strip under the picture: what drives what, the hints
    float sy = g_ui_img[1] + g_ui_img[3] + 3;
    char bd[96]; sprintf_s(bd, "PAD > %s     KEYS + MOUSE > %s", g_dp_pad_editor ? "PANELS" : "FIGHTER", g_dp_kbm_editor ? "EDITOR" : "FIGHTER");
    float bw = ui_text_w(2, bd) + 24;
    ui_rect(1574 - bw, sy, bw, 26, 0xD80A0B0E); ui_frame(1574 - bw, sy, bw, 26, 0xFF6D5626); ui_text(2, 1586 - bw, sy - 1, bd, 0xFFFFD77A);
    {   // v5.127.2 (owner): the hint is cut to the room left of the badge
        char hint[160];
        strcpy_s(hint, g_dp_pad_editor ? "Pad: LB/RB tabs  Up/Down rows  Left/Right change  A choose  X default  B back to the fighter"
                                       : "Pad: the fighter (View = menu)   L3 + R3: pad to the panels   F10: keys to the fighter");
        cr_clip(hint, sizeof(hint), 1, 1574 - bw - 16 - (g_ui_img[0] + 4));
        ui_text(1, g_ui_img[0] + 4, sy + 1, hint, 0xC0CFC6B2);
    }
    ui_frame(g_ui_img[0] - 1, g_ui_img[1] - 1, g_ui_img[2] + 2, g_ui_img[3] + 2, 0xFF3A352A);
    {   // the loader's status line (set_status) as a toast under the Dream Bar
        char st[sizeof(g_status)] = "";
        EnterCriticalSection(&g_ui_lock);
        if (GetTickCount64() < g_status_until) strcpy_s(st, g_status);
        LeaveCriticalSection(&g_ui_lock);
        if (st[0]) {
            cr_clip(st, sizeof(st), 1, 1200);
            float sw = ui_text_w(1, st) + 32;
            ui_rect(960 - sw / 2, 70, sw, 40, 0xE01F3A24); ui_frame(960 - sw / 2, 70, sw, 40, 0xFF4F9A5B); ui_text(1, 976 - sw / 2, 74, st, 0xFFDFF5E2);
        }
    }
    if (g_ui_px >= 0) { ui_frame(g_ui_px - 6, g_ui_py - 6, 12, 12, 0xFFFFD77A); ui_rect(g_ui_px - 1, g_ui_py - 1, 2, 2, 0xFFFFD77A); }
}
