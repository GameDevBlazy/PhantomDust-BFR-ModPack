// ---- v5.148.9 map rating (owner list 2026-11-10 B: "Map rating: players rate maps (1-3 stars or good / bad / needs improvement),
// shown in the lobby when a stage is chosen"). Offline only. Independently authored.
// Facts used (recorded): the match state *0x1404bc950 (RVA_IN_PHASE: 6 = the fight, 9 / 10 / 11 = the end of the match, 0 after);
// Map Slot 7 = stage LIST_STAGE holds the chosen pack g_mx_cur; the lobby's Level line shows row LIST_STAGE's name pointer
// (mx_name_step, native 2.151).
// At the end of a match on a pack a 12 s window opens: player 1's d-pad Up = good (3), Right = needs improvement (2), Down =
// bad (1), one vote a match. Votes per pack in <data>\CwoMapRatings.txt ("mxNNN good needs bad"); the lobby's Level line
// shows "Name (2.7/3, 5 votes)".
struct RtRow { char pack[16]; int good, mid, bad; };
static RtRow g_rt[256];
static int g_rt_n = -1;
static volatile ULONGLONG g_rt_open_until;
static char g_rt_pack[16];

static void rt_path(wchar_t *p) { swprintf_s(p, MAX_PATH, L"%sCwoMapRatings.txt", g_dir); }
static void rt_load() {
    g_rt_n = 0;
    wchar_t p[MAX_PATH]; rt_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return;
    char line[128];
    while (g_rt_n < 256 && fgets(line, sizeof(line), f)) {
        RtRow r; memset(&r, 0, sizeof(r));
        if (_strnicmp(line, "mx", 2) || sscanf_s(line, "%15s %d %d %d", r.pack, (unsigned)sizeof(r.pack), &r.good, &r.mid, &r.bad) != 4) continue;
        g_rt[g_rt_n++] = r;
    }
    fclose(f);
}
static void rt_save() {
    wchar_t p[MAX_PATH]; rt_path(p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, p, L"w") != 0 || !f) return;
    fprintf(f, "# map ratings (loader 5.148.9): pack good needs-improvement bad\n");
    for (int i = 0; i < g_rt_n; i++) fprintf(f, "%s %d %d %d\n", g_rt[i].pack, g_rt[i].good, g_rt[i].mid, g_rt[i].bad);
    fclose(f);
}
static RtRow *rt_row(const char *pack, bool make) {
    if (g_rt_n < 0) rt_load();
    for (int i = 0; i < g_rt_n; i++) if (!_stricmp(g_rt[i].pack, pack)) return &g_rt[i];
    if (!make || g_rt_n >= 256) return NULL;
    RtRow &r = g_rt[g_rt_n++]; memset(&r, 0, sizeof(r)); strcpy_s(r.pack, pack);
    return &r;
}
// the Level line's rating for a pack ("" when it has no votes)
static void rt_suffix(const char *pack, char *out, size_t n) {
    out[0] = 0;
    if (ed_online()) return;
    RtRow *r = rt_row(pack, false);
    int v = r ? r->good + r->mid + r->bad : 0;
    if (!v) return;
    sprintf_s(out, n, " (%.1f/3, %d vote%s)", (3.0 * r->good + 2.0 * r->mid + r->bad) / v, v, v == 1 ? "" : "s");
}
// game thread, every frame: the end of a match on a pack opens the window
static void rt_step() {
    static int last = -1;
    __try {
        int ph = *(int *)(g_base + RVA_IN_PHASE);
        if (ph == 9 && last != 9 && !ed_online() && *(int *)(g_base + RVA_CUR_STAGE) == LIST_STAGE && g_mx_cur >= 0 && g_mx_cur < g_mx_count) {
            strcpy_s(g_rt_pack, g_mx[g_mx_cur].file);
            g_rt_open_until = GetTickCount64() + 12000;
            logf("map rating: the window is open for %s (%s)", g_mx[g_mx_cur].file, g_mx[g_mx_cur].name);
        }
        last = ph;
        static ULONGLONG nag;
        ULONGLONG now = GetTickCount64();
        if (now < g_rt_open_until && now >= nag) {
            nag = now + 3000;
            set_status("RATE THIS MAP:  Up = good    Right = needs improvement    Down = bad");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// worker thread: player 1's new presses
static void rt_press(WORD pressed) {
    if (GetTickCount64() >= g_rt_open_until || !(pressed & (BTN_UP | BTN_RIGHT | BTN_DOWN))) return;
    g_rt_open_until = 0;
    RtRow *r = rt_row(g_rt_pack, true);
    if (!r) return;
    const char *what = "good";
    if (pressed & BTN_UP) r->good++;
    else if (pressed & BTN_RIGHT) { r->mid++; what = "needs improvement"; }
    else { r->bad++; what = "bad"; }
    rt_save();
    char msg[96]; sprintf_s(msg, "Map rated: %s - thank you", what); set_status(msg);
    logf("map rating: %s rated %s (now good %d, needs %d, bad %d)", r->pack, what, r->good, r->mid, r->bad);
}
