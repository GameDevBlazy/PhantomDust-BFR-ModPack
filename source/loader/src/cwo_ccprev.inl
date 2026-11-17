// ---- v3.7 Create a Character: live preview (owner 2026-10-24; 2026-10-25: "Make it exist"). The game's own Character viewer (debug CHARACTER) shows the
// created character's row while the Create a Character page stays on screen; every change rebuilds its file in the loader
// (cwo_charbuild.inl, byte-identical to build_character.py) and the viewer reloads it; the viewer plays the animation of the
// row being edited.
// Game facts (Ghidra full-pass export batch_065 via pcode.py + capstone, 2026-10-24, MODLOG):
//   FUN_140188b70(*0x1404c01c8, mode word): 0x103 = mode 3 (mode table 0x1403e8450: init 0x140188700 starts the viewer on
//     the character word *0x1404c5238; update FUN_140188810; B or A leaves, A also sets *0x1404c5238).
//   viewer object = *(*0x1404c01c8 + 0x58) while *(mode + 0x7c) == 3 (FUN_140188810): +0x44 page (0 turns the model),
//     +0x48 load state (3 -> reload, 2 = shown; +0x4c its frame counter), +0x84 character word, +0x110 the fighter.
//   FUN_140257310(fighter + 0x12f00, slot, flag, -1.0f, 0, -1.0f, -1.0f) plays animation slot (FUN_14015d6a0, the viewer's
//     own animation page; -1.0f = *0x1402f82e0).
//   The viewer reads player\npc00a\npc00a0.alr for the slot's charaset row 28 (colour count 1); the loader's CreateFile2
//     wrapper opens the preview file instead while the preview runs (h_CreateFile2).
static const ULONG_PTR RVA_CHAR_WORD = 0x4c5238, RVA_MODE_SWITCH = 0x188b70, RVA_PLAY_ANIM = 0x257310;   // RVA_MODE_OBJ: cwo_forge.inl
typedef void(__fastcall *PFN_MODE)(ULONG_PTR, int);
typedef void(__fastcall *PFN_PLAYANIM)(ULONG_PTR, int, int, float, int, float, float);
static const int CC_ROW = 28;                               // charaset row of the created character (the select slot)
static wchar_t g_cc_file[MAX_PATH], g_cc_dir[MAX_PATH];
static int g_cc_old_char = -1, g_cc_shown = -2;
static ULONGLONG g_cc_t;
static char g_cc_note[100];
static unsigned g_cc_sig;                                   // v5.10 the look the preview file holds (0 = none yet)
static ULONGLONG g_cc_load_t;                               // v5.10 when the viewer was asked to load it (0 = shown)

static bool cc_is_slot_file(LPCWSTR f) {                    // ...\npc00a0.alr or .../npc00a0.alr, any case (h_CreateFile2)
    size_t n = f ? wcslen(f) : 0;
    static const wchar_t tail[] = L"npc00a0.alr";
    const size_t t = sizeof(tail) / sizeof(wchar_t) - 1;
    return n > t && _wcsicmp(f + n - t, tail) == 0 && (f[n - t - 1] == L'\\' || f[n - t - 1] == L'/');
}

static const int CC_FIRST[CH_GN] = {0, 7, 17, 12, 21, 25, 33, 37, 42, 45, 64, 102}, CC_LAST[CH_GN] = {7, 12, 21, 17, 25, 33, 37, 42, 45, 64, 102, 130};
static int cc_row_anim(int row, ULONGLONG ms) {             // the animation slot the row being edited is about
    // v3.8 (owner): a group
    // row plays each of its animations in turn (walk turns: right, left, back-right, back-left), 2.5 s each
    int fi = row >= 2 ? ed_fld(EDP_CHAR, row) : -1;
    const EdItem &it = g_edc[EDP_CHAR].item[g_edc[EDP_CHAR].cur];
    if (fi >= CH_G0 && fi < CH_G0 + CH_GN) {
        int g = fi - CH_G0, n = CC_LAST[g] - CC_FIRST[g];
        return CC_FIRST[g] + (int)((ms / 2500) % (ULONGLONG)n);
    }
    if (fi >= CH_S0 && fi < CH_S0 + 6) return it.v[CH_S0 + ((fi - CH_S0) & ~1)] & 127;
    if (fi == CH_MOVES) return 7;                           // the base moves: walking
    if (fi == CH_EVADE) return 33 + (int)((ms / 2500) % 4); // the evade set
    return 0;                                               // everything else: the stance (WAIT0)
}

// v4.5 (owner): NPC bodies, the
// head and the middle / lower parts are built in the preview too (cwo_charbuild2.inl, byte-identical to build_character.py)
static void cc_spec(const EdItem &it, CbSpec2 &s2, char *note, size_t n) {
    note[0] = 0;
    CbSpec &sp = s2.base;
    sp.body = it.v[CH_BODY]; sp.colour = sp.body <= 10 ? it.v[CH_COLOUR] & 3 : 0; sp.moves = it.v[CH_MOVES]; sp.size_fit = it.v[CH_SIZE];
    s2.head = it.v[CH_HEAD]; s2.mid = it.v[CH_MID]; s2.mstyle = it.v[CH_MIDSTYLE]; s2.low = it.v[CH_LOW]; s2.lstyle = it.v[CH_LOWSTYLE];
    s2.headx = it.v[CH_HEADX];
    for (int *pv : {&s2.head, &s2.headx, &s2.mid, &s2.low})   // v4.9: a part the rules forbid (an older save) is left out
        if (*pv >= 0 && *pv != sp.body && !cb_part_allowed(sp.body, *pv)) { sprintf_s(note, n, "A part is not allowed on this body: left out"); *pv = -1; }
    if (sp.body <= 10 && (it.v[CH_COLOUR] & 3) != it.v[CH_COLOUR]) sprintf_s(note, n, "Outfit color %d is shown as color %d here", it.v[CH_COLOUR] + 1, (it.v[CH_COLOUR] & 3) + 1);
    if (!cb_moves_allowed(sp.body, sp.moves)) {              // v5.10 owner HARD RULE: an older save's moves of the other half
        sprintf_s(note, n, "%s's moves: not allowed on this body (male / female rule)", ED_MOVES[sp.moves]);
        sp.moves = sp.body <= 10 ? sp.body : cb_female(sp.body) ? 2 : 0;
    }
    if (!ed_move_fits(sp.moves, sp.body)) {                 // v3.8: what does not fit is left out of the preview, not refused
        sprintf_s(note, n, "%s's moves do not fit this body: shown with its own", ED_MOVES[sp.moves]);
        sp.moves = sp.body;
    }
    for (int s = 0; s < 130; s++) sp.from[s] = -1;
    for (int g = 0; g < CH_GN; g++) {                        // groups first, then the single animations (bake_edgar order)
        int ch = it.v[CH_G0 + g];
        if (!cb_moves_allowed(sp.body, ch)) { sprintf_s(note, n, "%s from %s: male / female rule, left out", CHAR_F[CH_G0 + g].label, ED_MOVES[ch]); continue; }
        if (ch >= 0 && !ed_move_fits(ch, sp.body)) { sprintf_s(note, n, "%s from %s does not fit: left out", CHAR_F[CH_G0 + g].label, ED_MOVES[ch]); continue; }
        if (ch >= 0 && ch != sp.moves) for (int s = CC_FIRST[g]; s < CC_LAST[g]; s++) sp.from[s] = ch;
    }
    // v5.32 (owner 2026-10-27):
    // the cartwheel / roll characters (charaset +0x36 = 751 Dash: Edgar, Cuff Button, pH, JD, Sammah) have the cartwheel in
    // their JUMP_* slots too; a jumping character takes JUMP_* from a jumper of its own half (its body, else Player / Freia),
    // a dashing one DASH_* from a dasher (its body, else Edgar) - the same rule as bake_edgar.py at install
    static const bool DASHER[11] = {false, true, false, false, false, true, true, false, false, true, true};
    auto row_of = [](int src) { return src <= 10 ? src : src - 11; };
    auto dasher = [&](int src) { int r = row_of(src); return r >= 0 && r <= 10 && DASHER[r]; };
    int ev = it.v[CH_EVADE];
    bool female = cb_female(sp.body);
    bool jumps = ev == 1 || (ev != 2 && !dasher(sp.moves));
    int g = jumps ? 7 : 6;                                    // group 7 JUMP_* (37-41), 6 DASH_* (33-36)
    int src = sp.from[CC_FIRST[g]] >= 0 ? sp.from[CC_FIRST[g]] : sp.moves;
    int donor = -1;
    if (jumps && dasher(src)) donor = (sp.body <= 10 && !DASHER[sp.body]) ? sp.body : (female ? 2 : 0);
    if (!jumps && !female && !dasher(src)) donor = (sp.body <= 10 && DASHER[sp.body]) ? sp.body : 1;
    if (donor >= 0 && ed_move_fits(donor, sp.body) && cb_moves_allowed(sp.body, donor)) {
        for (int s = CC_FIRST[g]; s < CC_LAST[g]; s++) sp.from[s] = donor == sp.moves ? -1 : donor;
        sprintf_s(note, n, "%s from %s (%s's moves have no real one)", jumps ? "Jump" : "Dash", ED_MOVES[donor], ED_MOVES[src]);
    }
    for (int k = 0; k < 3; k++) {
        int ch = it.v[CH_S0 + 2 * k + 1];
        if (ch >= 0 && ed_move_fits(ch, sp.body) && cb_moves_allowed(sp.body, ch)) sp.from[it.v[CH_S0 + 2 * k] & 127] = ch;
    }
}

static unsigned cc_sig(const CbSpec2 &sp, int theme) {      // v5.10 FNV-1a over everything the file is built from
    unsigned h = 2166136261u;
    auto mix = [&](const void *p, size_t n) { for (size_t i = 0; i < n; i++) { h ^= ((const unsigned char *)p)[i]; h *= 16777619u; } };
    mix(&sp, sizeof(sp));
    mix(&theme, sizeof(theme));
    return h ? h : 1;
}

static int cc_build(bool force) {                           // the preview file (written beside, then swapped in)
    // v5.10: 2 = the look did not change (name, school, voice, evade, outfit slot, face: nothing to reload), 1 built, 0 failed
    const EdItem &it = g_edc[EDP_CHAR].item[g_edc[EDP_CHAR].cur];
    CbSpec2 sp;
    memset(&sp, 0, sizeof(sp));
    char why[160] = "";
    cc_spec(it, sp, g_cc_note, sizeof(g_cc_note));
    unsigned sig = cc_sig(sp, it.v[CH_THEME]);
    if (!force && sig == g_cc_sig) return 2;
    Bytes out;
    bool ok_b = cb_build2(sp, out, why, sizeof(why));
    // a part that does not fit is left out of the preview (lower, then middle, then head), with a note; the rest shows
    static const char *PART[4] = {"lower part", "middle part", "head extras", "head"};
    for (int k = 0; !ok_b && k < 4; k++) {
        int *v = k == 0 ? &sp.low : k == 1 ? &sp.mid : k == 2 ? &sp.headx : &sp.head;
        if (*v < 0) continue;
        char first[160];
        strcpy_s(first, why);
        *v = -1;
        ok_b = cb_build2(sp, out, why, sizeof(why));
        if (ok_b) sprintf_s(g_cc_note, "The %s does not fit: %.70s", PART[k], first);
    }
    if (!ok_b) {
        set_status(why[0] ? why : "This character cannot be built");
        logf("preview: not built (%s)", why);
        return 0;
    }
    if (it.v[CH_THEME] > 0 && !th_apply(out, it.v[CH_THEME])) logf("preview: theme %d not applied (no 512 x 512 DXT1 outfit)", it.v[CH_THEME]);
    wchar_t tmp[MAX_PATH];
    swprintf_s(tmp, L"%s.new", g_cc_file);
    FILE *f = NULL;
    if (cwo_wfopen(&f, tmp, L"wb") != 0 || !f) { set_status("Could not write the preview file"); return 0; }
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    ok = ok && MoveFileExW(tmp, g_cc_file, MOVEFILE_REPLACE_EXISTING);
    if (ok) g_cc_sig = sig;
    logf("preview: built %s (%zu bytes)%s%s", ok ? "ok" : "FAILED", out.size(), g_cc_note[0] ? " - " : "", g_cc_note);
    return ok ? 1 : 0;
}

// v5.35 (owner 2026-10-28; it came right after a Create a Stage match). Stage loads / unloads suspend the world's task groups 9-13 and
// 29-31 (bit 0x20000, scheduler *0x14052dfe8 +0xa0, the stage view's facts in cwo_stageview.inl) and after a battle the
// game leaves them suspended in the menus, so the viewer's fighter never ran or drew. While the preview shows they run;
// the suspension the game had comes back when it closes.
static const int CC_GROUPS[8] = {0x98, 0xa8, 0xb8, 0xc8, 0xd8, 0x1d8, 0x1e8, 0x1f8};   // = LV_GROUPS
static int g_cc_groups_was = -1;                            // suspended groups before the preview (-1: not lifted)
static unsigned *cc_group_flags(int k) {
    ULONG_PTR s = *(ULONG_PTR *)(g_base + 0x52dfe8);
    if (!s) return NULL;
    ULONG_PTR g = *(ULONG_PTR *)(s + 0xa0);
    return g ? (unsigned *)(g + CC_GROUPS[k]) : NULL;
}
static void cc_groups_lift() {
    int was = 0;
    for (int k = 0; k < 8; k++) { unsigned *f = cc_group_flags(k); if (f) { if (*f & 0x20000) was |= 1 << k; *f &= ~0x20000u; } }
    if (g_cc_groups_was < 0) {
        g_cc_groups_was = was;
        logf("preview: world groups suspended when it opened: mask %02x (run while it shows); fade flag %d", was,
             *(unsigned char *)(g_base + 0x3dd3d5));
    } else g_cc_groups_was |= was;
}
static void cc_groups_back() {
    if (g_cc_groups_was < 0) return;
    for (int k = 0; k < 8; k++) {
        unsigned *f = cc_group_flags(k);
        if (f) *f = (g_cc_groups_was & (1 << k)) ? (*f | 0x20000) : (*f & ~0x20000u);
    }
    g_cc_groups_was = -1;
}

// v5.35 (owner 2026-10-28): 'Use this character now' only
// remembered the choice ("body at the next install"). Now the character in use is built here (the preview's own builder,
// byte-identical to build_character.py) and h_CreateFile2 serves it for the created-character cell's file (charaset row 28,
// ...\npc00a0.alr) from then on, so the select screen plays it with no install. The charaset row itself (voice, evade
// skill) is still the installed one.
static wchar_t g_cu_file[MAX_PATH], g_cu_src[MAX_PATH];
static unsigned g_cu_sig;
static volatile LONG g_cu_ready;
static int g_cu_k = -2;
static void cu_step() {                                     // game thread (CwoOverlayFrame), outside matches
    if (g_cc_prev || g_in_match || ed_online()) return;
    LONG k = g_ed_char_on;
    if (k < 0 || k >= g_edc[EDP_CHAR].n) {
        if (g_cu_ready) { InterlockedExchange(&g_cu_ready, 0); logf("in use: no character - the cell's installed file again"); }
        g_cu_k = k;
        return;
    }
    static ULONGLONG last;
    ULONGLONG now = GetTickCount64();
    if (now - last < 1000) return;
    last = now;
    const EdItem &it = g_edc[EDP_CHAR].item[k];
    CbSpec2 sp;
    memset(&sp, 0, sizeof(sp));
    char note[100] = "", why[160] = "";
    cc_spec(it, sp, note, sizeof(note));
    unsigned sig = cc_sig(sp, it.v[CH_THEME]);
    if (g_cu_ready && sig == g_cu_sig && k == g_cu_k) return;
    if (sig == g_cu_sig && k == g_cu_k) return;                // tried and failed for this look: not again
    swprintf_s(g_cu_src, L"%scharsrc\\", g_dir);
    swprintf_s(g_cu_file, L"%sinuse_npc00a0.alr", g_dir);
    wchar_t probe[MAX_PATH];
    swprintf_s(probe, L"%spc00a0.alr", g_cu_src);
    g_cu_sig = sig; g_cu_k = k;
    if (GetFileAttributesW(probe) == INVALID_FILE_ATTRIBUTES) { logf("in use: the builder's files are missing (charsrc)"); return; }
    g_cb_src = g_cu_src;
    Bytes out;
    bool ok_b = cb_build2(sp, out, why, sizeof(why));
    for (int p = 0; !ok_b && p < 4; p++) {                   // as the preview: a part that does not fit is left out
        int *v = p == 0 ? &sp.low : p == 1 ? &sp.mid : p == 2 ? &sp.headx : &sp.head;
        if (*v < 0) continue;
        *v = -1;
        ok_b = cb_build2(sp, out, why, sizeof(why));
    }
    if (!ok_b) { InterlockedExchange(&g_cu_ready, 0); logf("in use: '%s' not built (%s)", it.name, why); return; }
    if (it.v[CH_THEME] > 0) th_apply(out, it.v[CH_THEME]);
    wchar_t tmp[MAX_PATH];
    swprintf_s(tmp, L"%s.new", g_cu_file);
    FILE *f = NULL;
    if (cwo_wfopen(&f, tmp, L"wb") != 0 || !f) { logf("in use: could not write the file"); return; }
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    InterlockedExchange(&g_cu_ready, 0);                       // no reader on a half-swapped file
    ok = ok && MoveFileExW(tmp, g_cu_file, MOVEFILE_REPLACE_EXISTING);
    InterlockedExchange(&g_cu_ready, ok ? 1 : 0);
    logf("in use: '%s' built for the created-character cell (%zu bytes)%s", it.name, out.size(), ok ? "" : " - FAILED to write");
}

static ULONG_PTR cc_viewer() {
    ULONG_PTR mode = *(ULONG_PTR *)(g_base + RVA_MODE_OBJ);
    if (!mode || *(int *)(mode + 0x7c) != 3) return 0;
    return *(ULONG_PTR *)(mode + 0x58);
}

static void cc_prev_request() {                             // A on "Preview live" (controller thread)
    if (ed_online()) { set_status("Online: no preview"); return; }
    swprintf_s(g_cc_dir, L"%scharsrc\\", g_dir);
    swprintf_s(g_cc_file, L"%spreview_npc00a0.alr", g_dir);
    wchar_t probe[MAX_PATH];
    swprintf_s(probe, L"%spc00a0.alr", g_cc_dir);
    if (GetFileAttributesW(probe) == INVALID_FILE_ATTRIBUTES) { set_status("Preview files missing (install 3.8 again)"); return; }
    g_cb_src = g_cc_dir;
    InterlockedExchange(&g_cc_dirty, 0);
    InterlockedExchange(&g_cc_prev, 1);
}

static void co_light_neutral(bool on);
static const int CO_SLIDE_MAX = 41, CO_SKIN_MAX = 15;          // v5.70 the outfit sliders (cwo_outfit.inl)
static void co_rgb(int v, int *o);
static void co_skin_rgb(int v, int *o);                    // v5.69 cwo_outfit.inl: no green light on the preview
static void cc_prev_step() {                                // game thread (CwoOverlayFrame)
    LONG st = g_cc_prev;
    if (!st) return;
    __try {
        ULONGLONG now = GetTickCount64();
        if (st == 1) {                                      // build, then open the viewer on row 28
            if (!cc_build(true)) { InterlockedExchange(&g_cc_prev, 0); return; }
            InterlockedExchange(&g_cc_redirect, 1);
            g_cc_old_char = *(int *)(g_base + RVA_CHAR_WORD);
            *(int *)(g_base + RVA_CHAR_WORD) = CC_ROW;
            InterlockedExchange(&g_ed_open, 0);
            ((PFN_MODE)(g_base + RVA_MODE_SWITCH))(*(ULONG_PTR *)(g_base + RVA_MODE_OBJ), 0x103);
            logf("preview: Character viewer opened on row %d (player's character word was %d)", CC_ROW, g_cc_old_char);
            g_cc_t = now; g_cc_shown = -2;
            InterlockedExchange(&g_cc_prev, 2);
            set_status("PREVIEW: change any row, the model follows. B: leave");
            return;
        }
        ULONG_PTR v = cc_viewer();
        if (st == 2) {
            if (!v) {
                if (now - g_cc_t > 10000) { logf("preview: the viewer did not start - leaving"); InterlockedExchange(&g_cc_prev, 3); }
                return;
            }
            cc_groups_lift();                                // v5.35 the world groups run while the model shows
            co_light_neutral(true);
            if (*(int *)(v + 0x44) != 0) *(int *)(v + 0x44) = 0;   // the turning page
            *(int *)(v + 0x54) = 0;                          // v4.0 (owner screenshot): the viewer's own list is not drawn
            if (g_cc_shown == -2 && *(int *)(v + 0x48) != 3 && *(int *)(v + 0x48) != 4 && *(int *)(v + 0x48) != 5 && now - g_cc_t > 600) {
                *(int *)(v + 0x84) = CC_ROW;                 // v4.0: load the model, as the viewer's own D-pad does
                *(int *)(v + 0x48) = 3;
                *(int *)(v + 0x4c) = 10;
                g_cc_shown = -1;
                g_cc_load_t = now;
                logf("preview: model load asked (state was %d)", *(int *)(v + 0x48));
            }
            // v5.10: rebuild 0.3 s after the last change (scrolling through heads loads only the one you stop on), and reload
            // the model only when its look changed
            if (g_cc_dirty && now - g_cc_change_t >= 300 && *(int *)(v + 0x48) == 2) {
                InterlockedExchange(&g_cc_dirty, 0);
                if (cc_build(false) == 1) {
                    *(int *)(v + 0x84) = CC_ROW;            // reload, as the viewer's own D-pad does
                    *(int *)(v + 0x48) = 3;
                    *(int *)(v + 0x4c) = 10;
                    g_cc_shown = -1;
                    g_cc_load_t = now;
                }
            }
            if (g_cc_load_t && *(int *)(v + 0x48) == 2) {   // v5.10 how long the game took to show it (for the next tuning)
                logf("preview: shown %llu ms after the load was asked", now - g_cc_load_t);
                g_cc_load_t = 0;
            }
            ULONG_PTR fighter = *(ULONG_PTR *)(v + 0x110);
            int want = cc_row_anim(g_ed_row, now);
            if (fighter && *(int *)(v + 0x48) == 2 && want != g_cc_shown) {
                ((PFN_PLAYANIM)(g_base + RVA_PLAY_ANIM))(fighter + 0x12f00, want, 1, -1.0f, 0, -1.0f, -1.0f);
                g_cc_shown = want;
            }
            return;
        }
        if (st == 3) {                                      // leave: the viewer's own B
            cc_groups_back();                                // v5.35 the game's own suspension again
            if (g_cc_old_char >= 0) *(int *)(g_base + RVA_CHAR_WORD) = g_cc_old_char;
            if (v) { *(int *)(v + 0x44) = 0; f_press(0x8, 150); }
            g_cc_t = now;
            InterlockedExchange(&g_cc_prev, 4);
            logf("preview: leaving");
            return;
        }
        if (st == 4 && (!v || now - g_cc_t > 4000)) {
            co_light_neutral(false);
            InterlockedExchange(&g_cc_redirect, 0);
            InterlockedExchange(&g_cc_prev, 0);
            g_cc_sig = 0; g_cc_load_t = 0;
            if (InterlockedExchange(&g_cc_back, 0)) {        // v5.10 B / Back: Edgar's Dream opens on the chooser next time
                InterlockedExchange(&g_ed_page, EDP_CHOOSER);
                ed_state_save();
            }
            set_status("Preview closed - Edgar's Dream is on the main menu");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("preview: fault - closed");
        co_light_neutral(false);
        __try { cc_groups_back(); } __except (EXCEPTION_EXECUTE_HANDLER) { g_cc_groups_was = -1; }
        InterlockedExchange(&g_cc_redirect, 0);
        InterlockedExchange(&g_cc_prev, 0);
    }
}

static int cc_self_test() {                                 // the design -> build mapping (the builder itself: tests/cb_test)
    EdItem it; memset(&it, 0, sizeof(it));
    for (int i = 0; i < CH_NF; i++) it.v[i] = CHAR_F[i].def;
    it.v[CH_G0 + 6] = 1;                                    // Edgar's evade
    it.v[CH_G0 + 8] = 9;                                    // JD's float
    it.v[CH_S0] = 42; it.v[CH_S0 + 1] = 4;                  // FLOAT_IN from Chunky (a single one wins over its group)
    CbSpec2 s2; char note[100];
    CbSpec &sp = s2.base;
    cc_spec(it, s2, note, sizeof(note));
    bool map = sp.body == 0 && sp.moves == 0 && sp.from[33] == 1 && sp.from[36] == 1 && sp.from[32] == -1 && sp.from[37] == -1 &&
               sp.from[42] == 4 && sp.from[43] == 9 && sp.from[44] == 9 && sp.from[45] == -1 && !note[0];
    it.v[CH_BODY] = 14; it.v[CH_HEAD] = 1; it.v[CH_MID] = 4; it.v[CH_LOW] = 3; cc_spec(it, s2, note, sizeof(note));
    bool npc = sp.body == 14 && sp.colour == 0 && s2.head == 1 && s2.mid == 4 && s2.low == 3 && !note[0];   // v4.5: built as it is
    it.v[CH_MID] = 2; cc_spec(it, s2, note, sizeof(note));   // v4.9: Freia's torso on Mac (male) is left out, with a note
    npc = npc && s2.mid == -1 && note[0];
    {   // v5.10 the look signature: school and voice keep it (no reload), a head changes it
        CbSpec2 sa, sb; memset(&sa, 0, sizeof(sa)); memset(&sb, 0, sizeof(sb));
        EdItem i2 = it;
        cc_spec(it, sa, note, sizeof(note));
        i2.v[CH_SCHOOL] ^= 1; i2.v[CH_VOICE] = 3;
        cc_spec(i2, sb, note, sizeof(note));
        bool same = cc_sig(sa, it.v[CH_THEME]) == cc_sig(sb, i2.v[CH_THEME]);
        i2.v[CH_HEAD] = 3; memset(&sb, 0, sizeof(sb));
        cc_spec(i2, sb, note, sizeof(note));
        npc = npc && same && cc_sig(sa, it.v[CH_THEME]) != cc_sig(sb, i2.v[CH_THEME]);
    }
    auto row_of = [](int field) { for (int r = 2; r < ed_rows(EDP_CHAR); r++) if (ed_fld(EDP_CHAR, r) == field) return r; return -1; };
    int r_evade = cc_row_anim(row_of(CH_G0 + 6), 0), r_float = cc_row_anim(row_of(CH_G0 + 8), 0), r_name = cc_row_anim(1, 0);
    int r_turn2 = cc_row_anim(row_of(CH_G0 + 2), 2600), r_turn4 = cc_row_anim(row_of(CH_G0 + 2), 4 * 2500 + 10);
    bool rows = r_evade == 33 && r_float == 42 && r_name == 0 && r_turn2 == 18 && r_turn4 == 17;
    bool path = cc_is_slot_file(L"Assets/Data/player\\npc00a\\npc00a0.alr") && cc_is_slot_file(L"x/NPC00A0.ALR") &&
                !cc_is_slot_file(L"x\\xnpc00a0.alr") && !cc_is_slot_file(L"npc00a0.alr");
    logf("self-test: preview mapping %s, NPC note + look signature %s, row animations %s, slot path %s", map ? "ok" : "FAILED", npc ? "ok" : "FAILED",
         rows ? "ok" : "FAILED", path ? "ok" : "FAILED");
    return map && npc && rows && path ? 1 : -31;
}

static void cc_prev_draw() {                                // the Create a Character page beside the model
    if (g_cc_prev != 2 && g_co_prev != 2) return;                // v5.69 Create an Outfit's page beside its model too
    int glyphs = *(int *)(g_base + RVA_GLYPHS), cap = *(int *)(g_base + RVA_GLYPH_CAP);
    if (glyphs > cap - 700) return;
    if (*(int *)(g_base + RVA_OFS_X) != 0 || *(int *)(g_base + RVA_OFS_Y) != 0) return;
    int *space = (int *)(g_base + RVA_FONT_SPACE), *gap = (int *)(g_base + RVA_FONT_GAP);
    int *fh = (int *)(g_base + RVA_FONT_H), *fw = (int *)(g_base + RVA_FONT_W), *x0 = (int *)(g_base + RVA_FONT_X0);
    DWORD *rgba = (DWORD *)(g_base + RVA_FONT_RGBA);
    int s_space = *space, s_gap = *gap, s_h = *fh, s_w = *fw, s_x0 = *x0;
    DWORD s_rgba[4];
    memcpy(s_rgba, rgba, sizeof(s_rgba));
    PFN_TEXT text = (PFN_TEXT)(g_base + RVA_TEXT);
    char st[96] = {0};
    bool status = false;
    if (TryEnterCriticalSection(&g_ui_lock)) {
        if (GetTickCount64() < g_status_until) { strcpy_s(st, sizeof(st), g_status); status = true; }
        LeaveCriticalSection(&g_ui_lock);
    }
    *space = 0; *gap = 1; *fh = 11; *fw = 5;                // v4.0: smaller text, the model keeps the screen
    auto put = [&](int px, int py, const char *s, DWORD col) {
        set_rgba(rgba, 0xFF000000);
        text(px + 1, py + 1, s);
        set_rgba(rgba, col);
        text(px, py, s);
    };
    auto box = [&](int, int, DWORD, int, int) {};
    if (g_co_prev == 2)                                        // v5.112 (owner 2026-11-07): the page beside the turning model again, and the help line
        put(20, 448, status ? st : "PREVIEW: the model turns   D-pad: rows / values   Right stick: turn   B: back to Edgar's Dream", 0xFFE6D296);
    ed_draw(put, box, 4, 24, status, st);
    if (g_co_prev == 2 && g_ed_page == EDP_OUTFIT) {           // v5.70 the color / skin slider under the page
        int fi = ed_fld(EDP_OUTFIT, g_ed_row), kind = fi >= 0 ? g_edc[EDP_OUTFIT].f[fi].kind : -1;
        if ((kind == FK_OCOL || kind == FK_SKINT) && g_edc[EDP_OUTFIT].n > 0) {
            int n = kind == FK_OCOL ? CO_SLIDE_MAX : CO_SKIN_MAX, cur = g_edc[EDP_OUTFIT].item[g_edc[EDP_OUTFIT].cur].v[fi];
            int bw = kind == FK_OCOL ? 7 : 18, x0 = 8, y0 = 236;
            *gap = 0; *space = 0;
            for (int k = 1; k <= n; k++) {
                int rgb[3];
                if (kind == FK_OCOL) co_rgb(k, rgb); else co_skin_rgb(k, rgb);
                *fh = k == cur ? 22 : 14; *fw = bw - 1;
                set_rgba(rgba, 0xFF000000u | (DWORD)rgb[0] << 16 | (DWORD)rgb[1] << 8 | (DWORD)rgb[2]);
                text(x0 + (k - 1) * bw, k == cur ? y0 - 4 : y0, "\x81\xa1");
            }
            *fh = 11; *fw = 5; *gap = 1;
            put(x0, y0 + 22, cur > 0 ? "^ the slider: hold left / right" : "the outfit's own - right: onto the slider", 0xFFC8E6FF);
        }
    }
    if (g_cc_prev == 2 && g_cc_shown >= 0 && g_cc_shown < 130) {              // v3.8: what the model is doing now
        char line[64];
        sprintf_s(line, "PLAYING: %d %s", g_cc_shown, ED_ANIM[g_cc_shown]);
        put(400, 24, line, 0xFF80FF80);
    }
    if (g_cc_note[0]) put(4, 452, g_cc_note, 0xFFFFD040);
    memcpy(rgba, s_rgba, sizeof(s_rgba));
    *space = s_space; *gap = s_gap; *fh = s_h; *fw = s_w; *x0 = s_x0;
}
