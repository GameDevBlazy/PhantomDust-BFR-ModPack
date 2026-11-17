// ---- v5.56 the Battle for Reality skill pack (owner 2026-11-01; "this will be installed you have my permission"; AGENTS.md "BFR mod pack and the Battle for Reality skill pack
// online"). v5.59: <data>\CwoBFR.txt format 2 (13_Independent_Research_Tools/bfr_from_pack.py): whole skill records by
// record number with their own name and description, and the profiles the pack changes.
// Game facts used (Ghidra full pass 065; cwo_bigskills.inl, cwo_newskills.inl): the game reads skill records through
// *0x1404bdc50 (0x90 each), profiles through *0x1404bcc98 (0x72 each), text rows through *0x1404bdc48 (12 bytes {9001, name,
// description} self-relative); the loader's bigger tables are pointed there (bs_*); record +0x00 text row, +0x02 / +0x06 id,
// +0x08 rarity (stars - 1), +0x0E capsule type, +0x12 school, +0x14 ground profile, +0x28 aura cost, +0x30 uses, +0x32
// combo link, +0x58 damage.
// What this does: with the pack on (<data>\CwoBFR_state.txt "on"; the BFR mod pack installs it on), each second while the
// loader's tables are pointed: every record the pack touches is saved once, then written (revisions: the fields and a new
// text row that keeps the original name; additions: the source record + one profile copy, its own text row, 10 in stock).
// Off: every saved record goes back exactly. A refresh of the tables (the game installed its skill data again) starts
// from the game's records, so the saves are dropped and the pack is written again. Rows 4400-4799, profiles 1700-1799 and
// the string arena's upper half (0x40000..) are the pack's own; nothing else writes there.
static const int BFR_VERSION = 146;   // v5.188.23 Red Moon 434 (no STR / on-hit, the loader moon); v5.188.15 skill fixes 422 / 442 / 445 / 451 / 452                             // the pack's skill update version while it is on (v5.188.5: 142 -> 144, the
                                                                // pack with the original 1-374 + 2-minute environments - players on the older pack stay apart)
static const int BFR_VERSION_OG = 143;                          // v5.58 the BFR edition with the OG skills
static int g_bfr_ver_orig = -1;
static const int BFR_MAX = 520, BFR_ROW0 = 4400, BFR_PROF0 = 1700, BFR_STR0 = 0x40000, BFR_PMAX = 64;
// v5.59 format 2 (owner 2026-11-02: the skills were on the wrong records - "Rock shot ... says slide laser ... Double laser
// ... Finisher"): the list holds whole records by RECORD number (+0x02 is the displayed id: Rock Shot is record 38,
// displayed id 8; the v1 list used displayed ids as record numbers) and the profiles the pack changes. Every record of
// the list carries its own name and description; its +0x00 becomes the loader's text row.
struct BfrSkill { bool add; int id; char name[48], desc[400]; unsigned char rec[0x90]; int row, stro; };
struct BfrProf { int idx; unsigned char data[0x72], saved[0x72]; bool has_saved; };
static BfrSkill g_bfr[BFR_MAX];
static BfrProf g_bfrp[BFR_PMAX];
static int g_bfr_n, g_bfrp_n, g_bfr_on = -1, g_bfr_applied, g_bfr_epoch = -1, g_bfr_str_used;
static unsigned char g_bfr_saved[BFR_MAX][0x90];
// v5.60 (owner 2026-11-02): the list's "locked" records
static short g_bfr_lock[64];
static int g_bfr_lock_n;
static bool bfr_locked(int r) { for (int i = 0; i < g_bfr_lock_n; i++) if (g_bfr_lock[i] == r) return true; return false; }
// v5.109: a record the pack names (on or off) is never a free number for the Skill Creator / Skill Fuser
static bool bfr_uses(int id) { for (int i = 0; i < g_bfr_n; i++) if (g_bfr[i].id == id) return true; return bfr_locked(id); }
static bool g_bfr_has_saved[BFR_MAX];

static void bfr_state_write() {
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%sCwoBFR_state.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"w") != 0 || !f) return;
    fprintf(f, "%s\n", g_bfr_on > 0 ? "on" : "off");
    fclose(f);
}

static int bfr_hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static bool bfr_hex(const char *h, unsigned char *out, int n) {
    for (int i = 0; i < n; i++) {
        int a = bfr_hexval(h[2 * i]), b = a < 0 ? -1 : bfr_hexval(h[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = (unsigned char)(a * 16 + b);
    }
    return bfr_hexval(h[2 * n]) < 0;
}
static void bfr_unescape(char *t) {                               // the two characters \n are a line break in the game's text
    char *w = t;
    for (char *r = t; *r; r++) {
        if (r[0] == '\\' && r[1] == 'n') { *w++ = '\n'; r++; }
        else *w++ = *r;
    }
    *w = 0;
}

static void bfr_load() {                                         // worker start, after bs_load
    wchar_t path[MAX_PATH];
    g_bfr_n = 0; g_bfrp_n = 0; g_bfr_str_used = 0; g_bfr_lock_n = 0;
    swprintf_s(path, MAX_PATH, L"%sCwoBFR.txt", g_dir);
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) { g_bfr_on = 0; return; }
    static char line[2048];
    int fmt = 0, refused = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        if (!strncmp(line, "format ", 7)) { fmt = atoi(line + 7); continue; }
        if (fmt != 2) { refused++; continue; }                    // the old field list put skills on the wrong records
        if (!strncmp(line, "locked ", 7)) {
            char *ctx = NULL;
            for (char *t = strtok_s(line + 7, " ", &ctx); t && g_bfr_lock_n < 64; t = strtok_s(NULL, " ", &ctx)) {
                int r = atoi(t);
                if (r >= 375 && r < 1000) g_bfr_lock[g_bfr_lock_n++] = (short)r;
            }
            continue;
        }
        if (!strncmp(line, "prof ", 5) && g_bfrp_n < BFR_PMAX) {
            BfrProf &q = g_bfrp[g_bfrp_n];
            char *h = strchr(line + 5, ' ');
            q.idx = atoi(line + 5); q.has_saved = false;
            // v5.188.5: also the pack's own profile copies 1700-1799 (owner 2026-11-16: the originals 1-374 back - the pack's new
            // skills that used profiles the old pack changed on original skills use copies of them there)
            bool own = q.idx >= BFR_PROF0 && q.idx < BFR_PROF0 + 100 && q.idx < BS_PROFS;
            if (!h || q.idx <= 0 || (q.idx >= 896 && !own) || !bfr_hex(h + 1, q.data, 0x72)) { logf("BFR: profile line refused (%d)", q.idx); continue; }
            g_bfrp_n++;
            continue;
        }
        if (!strncmp(line, "rec ", 4) && g_bfr_n < BFR_MAX) {
            BfrSkill &s = g_bfr[g_bfr_n];
            memset(&s, 0, sizeof(s));
            char *tok[3] = {};
            int nt = 0;
            for (char *p = line + 4; nt < 3; ) {
                tok[nt++] = p;
                char *tab = strchr(p, '\t');
                if (!tab) break;
                *tab = 0; p = tab + 1;
            }
            char *h = strchr(tok[0], ' ');
            s.id = atoi(tok[0]);
            if (nt < 3 || !h || s.id < 1 || s.id >= 700 || !bfr_hex(h + 1, s.rec, 0x90)) { logf("BFR: record line refused (%d)", s.id); continue; }
            strncpy_s(s.name, tok[1], _TRUNCATE); bfr_unescape(s.name);
            strncpy_s(s.desc, tok[2], _TRUNCATE); bfr_unescape(s.desc);
            s.add = s.id >= 375;                                  // the pack's new skills: records 375-465 (displayed ids the same)
            s.row = BFR_ROW0 + g_bfr_n; s.stro = -1;
            g_bfr_n++;
        }
    }
    fclose(f);
    if (refused) logf("BFR: %d lines of an old list format refused - install the current pack", refused);
    swprintf_s(path, MAX_PATH, L"%sCwoBFR_state.txt", g_dir);
    g_bfr_on = 1;                                                // installed = on, unless the state file says off
    if (cwo_wfopen(&f, path, L"r") == 0 && f) { char st[8] = ""; if (fgets(st, sizeof(st), f) && !_strnicmp(st, "off", 3)) g_bfr_on = 0; fclose(f); }
    int adds = 0; for (int k = 0; k < g_bfr_n; k++) adds += g_bfr[k].add;
    logf("BFR: %d changed skills, %d new skills, %d profiles, %d boss skills locked, from CwoBFR.txt - %s", g_bfr_n - adds, adds,
         g_bfrp_n, g_bfr_lock_n, g_bfr_on ? "ON" : "off (OG skills)");
}

static int bfr_count() { return g_bfr_n; }
static bool bfr_lists(int id) {                                 // v5.59 the pack is on and holds this record (new skills / Mirror keep out)
    if (g_bfr_on <= 0) return false;
    for (int k = 0; k < g_bfr_n; k++) if (g_bfr[k].id == id) return true;
    return false;
}
static int g_bfr_wn[BFR_MAX], g_bfr_diag;                       // v5.59 writes per skill this epoch; diagnostics logged
static bool bfr_owns(int id) {                                   // the pack has this record written now (list / new skills stay out)
    if (g_bfr_on <= 0 || !g_bfr_applied) return false;
    for (int k = 0; k < g_bfr_n; k++) if (g_bfr[k].id == id) return true;
    return false;
}

static int bfr_text(BfrSkill &s) {                               // its own text row: name and description
    char *entry = g_bs_rows + 12 * s.row;
    if (s.stro < 0) {
        size_t need = (strlen(s.name) + strlen(s.desc) + 2 + 3) & ~3;
        if (BFR_STR0 + g_bfr_str_used + (int)need > BS_ARENA) return -1;
        s.stro = BFR_STR0 + g_bfr_str_used; g_bfr_str_used += (int)need;
    }
    char *str = g_bs_str + s.stro;
    size_t nl = strlen(s.name);
    memmove(str, s.name, nl + 1);
    memmove(str + nl + 1, s.desc, strlen(s.desc) + 1);
    *(int *)entry = 9001; *(int *)(entry + 4) = (int)(str - entry); *(int *)(entry + 8) = (int)(str + nl + 1 - entry);
    return s.row;
}

static int bfr_apply_one(int k) {
    BfrSkill &s = g_bfr[k];
    char *rec = bs_record(s.id);
    char r[NS_REC];
    memcpy(r, s.rec, NS_REC);
    *(short *)r = (short)s.row;
    if (!memcmp(rec, r, NS_REC)) return 0;                       // ours already
    if (!g_bfr_has_saved[k]) { memcpy(g_bfr_saved[k], rec, NS_REC); g_bfr_has_saved[k] = true; }
    if (bfr_text(s) < 0) return -1;
    if (g_bfr_wn[k]++ >= 1 && g_bfr_diag < 6) {                 // written again: what changed it since (the next test tells)
        char d[160] = ""; size_t l = 0;
        for (int o = 0; o < NS_REC && l + 16 < sizeof(d); o += 2)
            if (*(short *)(rec + o) != *(short *)(r + o)) l += sprintf_s(d + l, sizeof(d) - l, " +%02x:%d>%d", o, *(short *)(rec + o), *(short *)(r + o));
        logf("BFR: record %d written again (%d times) - fields%s", s.id, g_bfr_wn[k], d);
        g_bfr_diag++;
    }
    memcpy(rec + 2, r + 2, NS_REC - 2);
    memcpy(rec, r, 2);
    return 1;
}
static int bfr_apply_profiles() {
    int n = 0;
    for (int i = 0; i < g_bfrp_n; i++) {
        BfrProf &q = g_bfrp[i];
        char *p = g_bs_prof + (size_t)NS_PROF * q.idx;
        if (!memcmp(p, q.data, NS_PROF)) continue;
        if (!q.has_saved) { memcpy(q.saved, p, NS_PROF); q.has_saved = true; }
        memcpy(p, q.data, NS_PROF);
        n++;
    }
    return n;
}

static void bfr_revert_all() {
    int n = 0;
    for (int k = 0; k < g_bfr_n; k++) {
        if (!g_bfr_has_saved[k]) continue;
        char *rec = bs_record(g_bfr[k].id);
        memcpy(rec + 2, g_bfr_saved[k] + 2, NS_REC - 2);
        memcpy(rec, g_bfr_saved[k], 2);
        g_bfr_has_saved[k] = false;
        n++;
    }
    for (int i = 0; i < g_bfrp_n; i++)
        if (g_bfrp[i].has_saved) { memcpy(g_bs_prof + (size_t)NS_PROF * g_bfrp[i].idx, g_bfrp[i].saved, NS_PROF); g_bfrp[i].has_saved = false; n++; }
    // v5.86 (owner 2026-11-03): the new skills (375-465) leave the
    // stock too - their records are empty again, and the pack gives them back (10 each) when it is switched on
    int gone = 0;
    __try {
        ULONG_PTR mgr = *(ULONG_PTR *)(g_base + RVA_ARSENAL);
        unsigned char *stock = mgr ? *(unsigned char **)(mgr + 0x3a38) : NULL;
        short *supp = mgr ? *(short **)(mgr + 0x3a40) : NULL;
        if (stock && supp)
            for (int k = 0; k < g_bfr_n; k++) {
                int id = g_bfr[k].id;
                if (!g_bfr[k].add || id < 1 || id >= 1000) continue;
                if (stock[id] || supp[id * 2 + 1]) { stock[id] = 0; supp[id * 2] = 0; supp[id * 2 + 1] = 0; gone++; }
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_bfr_applied = 0;
    logf("BFR: %d records and profiles back to the original skills, %d new skills out of the stock", n, gone);
}

static void bfr_set(bool on) {                                   // the Lab rows / the pack's switch (any thread: applied in bfr_step)
    g_bfr_on = on ? 1 : 0;
    bfr_state_write();
    set_status(on ? "Battle for Reality skill pack: ENABLED (skills 375-466, environments 2 min)" : "Battle for Reality skill pack: DISABLED - original skills (0-374)");
    logf("BFR: switched %s", on ? "ON" : "off");
}

// every second from bs_step's slot (game thread, the loader's tables pointed)
static void bfr_step() {
    if (g_bfr_n <= 0 || !g_bs_on || !bs_pointed()) return;
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 1000;
    __try {
        if (g_bfr_epoch != g_bs_repoints) {                       // a refresh: the records are the game's again
            g_bfr_epoch = g_bs_repoints;
            memset(g_bfr_has_saved, 0, sizeof(g_bfr_has_saved));
            memset(g_bfr_wn, 0, sizeof(g_bfr_wn));
            for (int i = 0; i < g_bfrp_n; i++) g_bfrp[i].has_saved = false;
            g_bfr_applied = 0;
        }
        // the skill update version (the gsdata header int at +0x10: native 4.490 for the main menu's "Skill Update" and
        // "MODDED" lines, and the Xbox Live session attribute "version" - sessions only match equal values): 1.42 while the
        // pack is on, so BFR players only meet BFR players; the game's own number back when it is off
        {
            // v5.58 the BFR edition with the OG skills: 1.43, so BFR players never meet the stock game (the pack's lobby
            // features differ) and the two skill choices stay apart
            int *ver = (int *)(bs_image() + 0x10);
            int want = g_bfr_on > 0 ? BFR_VERSION : g_bfr_edition ? BFR_VERSION_OG : -1;
            if (want > 0 && *ver != want) {
                if (*ver != BFR_VERSION && *ver != BFR_VERSION_OG) g_bfr_ver_orig = *ver;
                *ver = want;
                logf("BFR: skill update version %d -> %d", g_bfr_ver_orig, want);
            } else if (want < 0 && g_bfr_ver_orig >= 0 && (*ver == BFR_VERSION || *ver == BFR_VERSION_OG)) {
                *ver = g_bfr_ver_orig;
                logf("BFR: skill update version back to %d", g_bfr_ver_orig);
            }
        }
        if (g_bfr_on <= 0) { if (g_bfr_applied) bfr_revert_all(); return; }
        int wrote = bfr_apply_profiles(), failed = 0;
        for (int k = 0; k < g_bfr_n; k++) {
            int r = bfr_apply_one(k);
            if (r > 0) wrote++; else if (r < 0) failed++;
            if (r >= 0 && g_bfr[k].add) bs_stock(g_bfr[k].id, 10);
        }
        g_bfr_applied = 1;
        static int last_w = -1, last_f = -1; static ULONGLONG last_t;   // v5.59 the same count every second is said once a minute
        if ((wrote || failed) && (wrote != last_w || failed != last_f || now - last_t > 60000)) {
            logf("BFR: %d skills written, %d refused (%d in the pack)", wrote, failed, g_bfr_n);
            last_w = wrote; last_f = failed; last_t = now;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("BFR: fault - off"); g_bfr_n = 0; }
}

// v5.56 the Lab's rows (tools/build_lab_bfr.py): native 2.8 op 80 = the switch (1 on), 81 = on, 82 = off (back to the OG skills)
static INT64 bfr_native(int op, INT64 *) {
    if (op == 80) return g_bfr_on > 0;
    if (g_bfr_n <= 0) { set_status("No Battle for Reality skill pack installed"); return 0; }
    if (op == 81) { bfr_set(true); return 1; }
    if (op == 82) { bfr_set(false); return 1; }
    return 0;
}

static int bfr_self_test() {                                     // a changed record, a new one and a profile on stand-ins, then revert
    static char rec[BS_RECS * NS_REC], prof[BS_PROFS * NS_PROF], rows[BS_ROWS * 12], str[BS_ARENA];
    char *k_rec = g_bs_rec, *k_prof = g_bs_prof, *k_rows = g_bs_rows, *k_str = g_bs_str;
    int k_n = g_bfr_n, k_pn = g_bfrp_n, k_on = g_bfr_on, k_ap = g_bfr_applied, k_su = g_bfr_str_used;
    static BfrSkill k_s[2]; memcpy(k_s, g_bfr, sizeof(k_s));
    static BfrProf k_p; memcpy(&k_p, g_bfrp, sizeof(k_p));
    bool k_hs[2] = {g_bfr_has_saved[0], g_bfr_has_saved[1]};
    memset(rec, 0, sizeof(rec)); memset(rows, 0, sizeof(rows)); memset(prof, 0, sizeof(prof));
    g_bs_rec = rec; g_bs_prof = prof; g_bs_rows = rows; g_bs_str = str;
    // record 38 (displayed id 8, text row 38), cost 3; profile 55 = 0x11 bytes
    char *r38 = rec + NS_REC * 38, *r400 = rec + NS_REC * 400;
    *(short *)r38 = 38; *(short *)(r38 + 2) = 8; *(short *)(r38 + 6) = 38; *(short *)(r38 + 0x28) = 3;
    memset(prof + NS_PROF * 55, 0x11, NS_PROF);
    unsigned char before38[NS_REC], before400[NS_REC], beforep[NS_PROF];
    memcpy(before38, r38, NS_REC); memcpy(before400, r400, NS_REC); memcpy(beforep, prof + NS_PROF * 55, NS_PROF);
    memset(g_bfr, 0, sizeof(BfrSkill) * 2); memset(g_bfr_has_saved, 0, 2);
    g_bfr_n = 2; g_bfrp_n = 1; g_bfr_str_used = 0;
    BfrSkill &a = g_bfr[0], &b = g_bfr[1];
    memcpy(a.rec, r38, NS_REC); *(short *)(a.rec + 0x28) = 5; a.id = 38; strcpy_s(a.name, "Rock Shot"); strcpy_s(a.desc, "two\\nlines");
    bfr_unescape(a.desc); a.row = BFR_ROW0; a.stro = -1;
    *(short *)(b.rec + 2) = 400; *(short *)(b.rec + 6) = 400; *(short *)(b.rec + 0x14) = 55; b.id = 400; b.add = true;
    strcpy_s(b.name, "Spirit Catch"); strcpy_s(b.desc, "x"); b.row = BFR_ROW0 + 1; b.stro = -1;
    g_bfrp[0].idx = 55; memset(g_bfrp[0].data, 0x22, NS_PROF); g_bfrp[0].has_saved = false;
    int np = bfr_apply_profiles(), ra = bfr_apply_one(0), rb = bfr_apply_one(1);
    const char *ne = rows + 12 * BFR_ROW0;
    bool chg = ra == 1 && *(short *)(r38 + 0x28) == 5 && *(short *)r38 == BFR_ROW0 && *(short *)(r38 + 2) == 8 &&
               !strcmp(ne + *(const int *)(ne + 4), "Rock Shot") && !strcmp(ne + *(const int *)(ne + 8), "two\nlines");
    bool add = rb == 1 && *(short *)(r400 + 2) == 400 && *(short *)(r400 + 0x14) == 55 && *(short *)r400 == BFR_ROW0 + 1;
    bool pr = np == 1 && (unsigned char)prof[NS_PROF * 55] == 0x22;
    bool again = bfr_apply_one(0) == 0 && bfr_apply_one(1) == 0 && bfr_apply_profiles() == 0;
    g_bfr_applied = 1;
    bfr_revert_all();
    bool back = !memcmp(r38, before38, NS_REC) && !memcmp(r400, before400, NS_REC) && !memcmp(prof + NS_PROF * 55, beforep, NS_PROF);
    g_bs_rec = k_rec; g_bs_prof = k_prof; g_bs_rows = k_rows; g_bs_str = k_str;
    memcpy(g_bfr, k_s, sizeof(k_s)); memcpy(g_bfrp, &k_p, sizeof(k_p)); g_bfr_has_saved[0] = k_hs[0]; g_bfr_has_saved[1] = k_hs[1];
    g_bfr_n = k_n; g_bfrp_n = k_pn; g_bfr_on = k_on; g_bfr_applied = k_ap; g_bfr_str_used = k_su;
    bool ok = chg && add && pr && again && back;
    logf("self-test: BFR changed record %s, new record %s, profile %s, written once %s, revert exact %s", chg ? "ok" : "FAILED",
         add ? "ok" : "FAILED", pr ? "ok" : "FAILED", again ? "ok" : "FAILED", back ? "ok" : "FAILED");
    return ok ? 1 : -89;
}

// v5.109 (owner 2026-11-07): saved Skill Creator skills on a record the pack names (the old free-number search started at 376, inside
// the pack's 375-465) move once to a free number (ns_free_id skips the pack now) and the file is saved. Runs at start-up
// after bfr_load, before the Mirror looks for its skill by name. Arsenals that held the old number keep the old number.
static void ns_off_pack() {
    int moved = 0;
    for (int k = 0; k < g_ns_n; k++) {
        if (!bfr_uses(g_ns[k].id) && g_ns[k].id >= NS_FIRST_FREE) continue;   // v5.111 below 467 moves too
        int old = g_ns[k].id;
        g_ns[k].id = -1;                                         // not "used" by itself during the search
        int id = ns_free_id();
        if (id < 0) { g_ns[k].id = old; logf("new skills: no free number for '%s' (%d) - left on the pack's record", g_ns[k].name, old); continue; }
        g_ns[k].id = id;
        logf("new skills: '%s' moved from %d (a Battle for Reality pack record) to %d", g_ns[k].name, old, id);
        moved++;
    }
    if (moved || ns_dedup_save) ns_save();
}

static int bfr_free_id_self_test() {                             // v5.109: pack records are never free numbers
    int k_n = g_bfr_n, k_ns = g_ns_n; int k_id0 = g_bfr[0].id, k_id1 = g_bfr[1].id;
    static char gs[0x44004 + 16]; memset(gs, 0, sizeof(gs)); *(unsigned *)gs = 0x44004;
    char *k_t = g_ns_test; g_ns_test = gs;
    g_bfr_n = 2; g_bfr[0].id = 467; g_bfr[1].id = 468; g_ns_n = 0;
    *(short *)(gs + 0x18 + NS_REC * 469 + 2) = 1;               // 469 holds a skill in the game's table
    int id = ns_free_id();
    g_bfr_n = k_n; g_ns_n = k_ns; g_bfr[0].id = k_id0; g_bfr[1].id = k_id1; g_ns_test = k_t;
    logf("self-test: new skill numbers skip the BFR pack and used records %s (got %d)", id == 470 ? "ok" : "FAILED", id);
    return id == 470 ? 1 : -91;
}
