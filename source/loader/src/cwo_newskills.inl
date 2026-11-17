// ---- v2.6 Skill Creator: new skills from CwoNewSkills.txt (owner 2026-10-24; first step: any skill in a
// new colour). Included by cwo_loader.cpp.
//
// A new skill never replaces anything: it goes into an EMPTY record (375-699 are empty, SKILL_SLOT_MAP_2026-10-21),
// its profiles into EMPTY 0x72-byte profiles, its name into a text row in the zero padding after the text pool; each
// place is checked to be empty (or already ours) before it is written. The same writes as our first in-game skill
// test (record 375, passed in game 2026-10-22), from inside the game instead of an outside watcher:
//   GSDATA image *0x1404c5228 (loaded when its u32 +0 = 0x44004): record id at +0x18 + 0x90*id, profile p at
//   +0x1B00C + 0x72*p, text row r's entry at +0x34018 + 12*r = {9001, name - entry, description - entry} (entries are
//   rebased at load, 0x14017dcb0), deck reference limit at +0x14; displayed-id maps exe+0x4BCCA0 / +0x4BDC60 [id] = id;
//   stock: arsenal side [[exe+0x4BEC10] + 0x33A0]: +0x644 + id copies, +0xA2C + 4*id supplemental (0, 2).
//   Record fields: +0x00 text row, +0x02/+0x06 id, +0x12 school (1 Psycho, 2 Optical, 3 Nature, 4 Ki, 5 Faith),
//   +0x14 ground profile, +0x16 air, +0x18..+0x1E combo, +0x28 aura cost, +0x32 combo link (= its own id),
//   +0x58 damage; profile +0x04 = its effect request (0x1401272b0), +0x06 its sound (200 + record, batch 071).
// The game re-reads its skill data now and then (LIVE-003 saw the server copy arrive): the loader checks once a
// second and writes again whenever a new skill is missing.
//
// CwoNewSkills.txt (next to PDUWP.exe), one skill per line, tab separated:
//   new id <TAB> source id <TAB> name <TAB> description <TAB> options (space separated)
//   options: OLD=NEW effect request swaps (made by skill_colour.py), school=1..5, cost=N, damage=N
static const ULONG_PTR RVA_GSDATA = 0x4C5228, RVA_FIRST_MAP = 0x4BCCA0, RVA_LAST_MAP = 0x4BDC60, RVA_ARSENAL = 0x4BEC10;
static const int NS_REC = 0x90, NS_PROF = 0x72, NS_PROF_BASE = 0x1B00C, NS_TEXT = 0x34018, NS_ROW0 = 2200,
                 NS_STR0 = 0x3B000, NS_STR = 0x100, NS_PROF0 = 410, NS_MAX = 100;
struct NewSkill {
    int id, src;
    char name[40], desc[180];
    int from[8], to[8], nmap;
    int school, cost, damage;
    int fx, snd, anim;                                      // v2.9: effect / sound / animation of another skill (-1 = its own)
    int hue;                                                // v3.0: colour turn in degrees (0 = as it is); built into its own effect copy
    int sat, val;                                           // v3.1: saturation / brightness in percent (0 = 100 %)
    int trail;                                              // v3.1: another skill's flying effect rides on its projectiles (0 = none)
    int trail_part;                                         // v5.188.22 trail=N:P - stored P + 1 (0 = part 1, the flying part)
    int pal, pal_req;                                       // v3.2: its 12-colour palette: requests pal .. pal+11 recolour effect pal_req
    int dirty;                                              // v2.9: changed in the Skill Creator: written again
    int unsaved;                                            // v4.2: made live in the Skill Creator, not in the file yet
    int comm;                                               // v5.186: a community skill (CwoCommunitySkills.txt): the pack's, read only
    int fo[24], fv[24], nfo;                                // v4.4: record fields set in the Skill Creator (offset, value)
    int fxmode;                                             // v4.7: 1 = Effect from brings its whole behaviour (+0x50..+0x8E)
    int voice;                                              // v5.30: 0 its own, 1-6 the voice value (profile +0x08), 7 no voice
    int cmode;                                              // v5.32: colour mode 0 Auto, 1 Turn, 2 Paint (cwo_fxcolour.inl)
};
static NewSkill g_ns[NS_MAX];
static int g_ns_n;
// v5.186.0 (owner 2026-11-14 "go ahead and work on A, D"; AGENTS.md "Community skills and created-content safety online"):
// - community skills: players' skills the pack's author approved, shipped in the pack's own CwoCommunitySkills.txt (same line
//   format) with fixed numbers 600-639 - a block neither the Skill Creator (counts up from 467) nor the Skill Fuser (counts
//   down from 699) hands out; every player of that pack has them, so they may be used online;
// - the pack's own skills (name AND number below) and community skills cannot be changed in the Skill Creator of the BFR
//   edition (a changed copy on one PC would differ online); a new skill can still be based on them.
static const int NS_COMM_LO = 600, NS_COMM_HI = 639;
struct NsPackSkill { const char *name; int id; };
static const NsPackSkill NS_PACK[] = {{"Mirror", 467}, {"Mirror", 471}, {"Revive Mend", 468}, {"Body Pull", 469},
                                      {"Spiritual Projection", 470}, {"Hologram", 472}, {"Thunder Punch", 473}, {"Ice Punch", 474},
                                      {"Fiery Tornado", 475}, {"Mega Photon Burst", 476}, {"Giant Meteor", 477}};
static bool ns_comm_range(int id) { return id >= NS_COMM_LO && id <= NS_COMM_HI; }
static bool ns_is_pack(const NewSkill &s) {
    for (const NsPackSkill &p : NS_PACK) if (p.id == s.id && !strcmp(p.name, s.name)) return true;
    return false;
}
static bool ns_locked(const NewSkill &s) { return s.comm || (g_bfr_edition && ns_is_pack(s)); }
static bool g_ns_comm_parse;                                    // ns_parse_file is reading CwoCommunitySkills.txt
static char *g_ns_test;                                     // self-test: a stand-in GSDATA image
static ULONGLONG g_ns_next;
static int g_ns_said[NS_MAX];                       // last result logged per skill (no repeats every second)

static int ns_parse_file(const wchar_t *path, bool saved);
// v3.2 (owner's skill 379 was lost: "cannot write CwoNewSkills.tmp"): the game may not write next to its exe, so the
// Skill Creator saves in the loader's data folder; the installed list is read first, the saved one is merged over it
static bool ns_dedup_save;                                      // v5.111 cwo_bfr.inl ns_off_pack saves when set
static void ns_load() {
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) slash[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sCwoNewSkills.txt", exe);
    g_ns_n = 0;
    int a = ns_parse_file(path, false);
    swprintf_s(path, MAX_PATH, L"%sCwoNewSkills.txt", g_dir);
    int b = ns_parse_file(path, true);
    swprintf_s(path, MAX_PATH, L"%sCwoCommunitySkills.txt", g_dir);   // v5.186.0 the pack's community skills (600-639)
    g_ns_comm_parse = true;
    int c = ns_parse_file(path, true);
    g_ns_comm_parse = false;
    logf("new skills: %d installed, %d saved in the Skill Creator, %d community skills, %d in all", a, b, c, g_ns_n);
    // v5.111: the same skill (name + source) listed more than once (the game-folder list and the saved list, or copies the
    // 5.109 renumbering wrote at each start) - the last one is kept
    int w = 0;
    for (int k = 0; k < g_ns_n; k++) {
        bool later = false;
        for (int j = k + 1; j < g_ns_n && !later; j++) later = g_ns[j].src == g_ns[k].src && !strcmp(g_ns[j].name, g_ns[k].name) &&
                                                            !g_ns[j].comm && !g_ns[k].comm;   // v5.186 a community copy is its own
        if (later) { logf("new skills: '%s' (%d) listed again later - this copy dropped", g_ns[k].name, g_ns[k].id); continue; }
        if (w != k) g_ns[w] = g_ns[k];
        w++;
    }
    if (w != g_ns_n) { g_ns_n = w; ns_dedup_save = true; }
    static const char *SCHOOL[6] = {"?", "Psycho", "Optical", "Nature", "Ki", "Faith"};
    for (int k = 0; k < g_ns_n && g_sk_count < 800; k++) {
        SkillRow &r = g_sk[g_sk_count++];
        r.id = g_ns[k].id;
        sprintf_s(r.name, "%.31s", g_ns[k].name);
        strcpy_s(r.school, g_ns[k].school > 0 ? SCHOOL[g_ns[k].school] : "as src");
        if (g_ns[k].cost >= 0) sprintf_s(r.cost, "%d", g_ns[k].cost); else strcpy_s(r.cost, "-");
    }
}

static int ns_parse_file(const wchar_t *path, bool saved) {
    FILE *f = NULL;
    int got_n = 0;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) return 0;
    char line[512];
    while (g_ns_n < NS_MAX && fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        NewSkill s = {};
        s.school = s.cost = s.damage = -1;
        s.fx = s.snd = s.anim = -1;
        char opts[200] = "";
        int got = sscanf_s(line, "%d\t%d\t%39[^\t]\t%179[^\t]\t%199[^\r\n]", &s.id, &s.src, s.name, (unsigned)sizeof(s.name),
                           s.desc, (unsigned)sizeof(s.desc), opts, (unsigned)sizeof(opts));
        if (got < 3 || s.id < 376 || s.id > 699 || s.src < 1 || s.src > 699) { logf("new skills: line refused: %.60s", line); continue; }
        if (!g_ns_comm_parse && ns_comm_range(s.id)) { logf("new skills: %d is kept for the pack's community skills - line refused: %.40s", s.id, line); continue; }
        if (g_ns_comm_parse && !ns_comm_range(s.id)) { logf("community skills: %d is outside %d-%d - refused", s.id, NS_COMM_LO, NS_COMM_HI); continue; }
        s.comm = g_ns_comm_parse ? 1 : 0;
        if (got < 4 || !strcmp(s.desc, "-")) s.desc[0] = 0;
        char *ctx = NULL;
        for (char *t = strtok_s(opts, " ", &ctx); t; t = strtok_s(NULL, " ", &ctx)) {
            int a, b;
            if (sscanf_s(t, "school=%d", &a) == 1 && a >= 1 && a <= 5) s.school = a;
            else if (sscanf_s(t, "cost=%d", &a) == 1 && a >= 0 && a <= 10) s.cost = a;
            else if (sscanf_s(t, "damage=%d", &a) == 1 && a >= 0 && a <= 999) s.damage = a;
            else if (sscanf_s(t, "fx=%d", &a) == 1 && a > 0 && a < 753) s.fx = a;
            else if (sscanf_s(t, "snd=%d", &a) == 1 && a > 0 && a < 753) s.snd = a;
            else if (sscanf_s(t, "anim=%d", &a) == 1 && a > 0 && a < 753) s.anim = a;
            else if (sscanf_s(t, "voice=%d", &a) == 1 && a >= 1 && a <= 7) s.voice = a;
            else if (sscanf_s(t, "hue=%d", &a) == 1 && a > 0 && a < 360) s.hue = a;
            else if (sscanf_s(t, "fxm=%d", &a) == 1 && (a == 0 || a == 1)) s.fxmode = a;
            else if (sscanf_s(t, "cm=%d", &a) == 1 && a >= 0 && a <= 2) s.cmode = a;   // v5.32 colour mode
            else if (sscanf_s(t, "sat=%d", &a) == 1 && a >= 0 && a <= 300) s.sat = a;
            else if (sscanf_s(t, "val=%d", &a) == 1 && a >= 10 && a <= 300) s.val = a;
            else if (sscanf_s(t, "trail=%d:%d", &a, &b) == 2 && a > 0 && a < 753 && b >= 0 && b < 8) { s.trail = a; s.trail_part = b + 1; }   // v5.188.22
            else if (sscanf_s(t, "trail=%d", &a) == 1 && a > 0 && a < 753) { s.trail = a; s.trail_part = 0; }
            else if (sscanf_s(t, "pal=%d:%d", &a, &b) == 2 && a > 0 && a < 32000 && b > 0) { s.pal = a; s.pal_req = b; }
            else if (t[0] == 'r' && sscanf_s(t + 1, "%x=%d", &a, &b) == 2 && a >= 0x0E && a < 0x90 && !(a & 1) && s.nfo < 24) {
                s.fo[s.nfo] = a; s.fv[s.nfo] = b; s.nfo++;   // v4.4 a record field
            }
            else if (sscanf_s(t, "%d=%d", &a, &b) == 2 && s.nmap < 8) { s.from[s.nmap] = a; s.to[s.nmap] = b; s.nmap++; }
        }
        int at = -1;
        for (int k = 0; k < g_ns_n; k++) if (g_ns[k].id == s.id) at = k;
        if (at >= 0 && g_ns_comm_parse) { logf("community skills: %d is already a skill - refused", s.id); continue; }
        if (at >= 0 && !saved) { logf("new skills: id %d twice, second refused", s.id); continue; }
        if (at >= 0) {                                       // a saved version: it wins, keeping the installed palette / colour copies
            NewSkill &o = g_ns[at];
            if (!s.pal) { s.pal = o.pal; s.pal_req = o.pal_req; }
            for (int m = 0; m < o.nmap && s.nmap < 8; m++) {
                bool has = false;
                for (int q = 0; q < s.nmap; q++) has |= s.from[q] == o.from[m];
                if (!has) { s.from[s.nmap] = o.from[m]; s.to[s.nmap] = o.to[m]; s.nmap++; }
            }
            o = s;
        } else g_ns[g_ns_n++] = s;
        got_n++;
    }
    fclose(f);
    return got_n;
}

static bool ns_quiet(int id) {                             // refused last time too: say it once
    for (int k = 0; k < g_ns_n; k++) if (g_ns[k].id == id) return g_ns_said[k] < 0;
    return false;
}

static bool ns_zero(const char *p, int n) {
    for (int i = 0; i < n; i++) if (p[i]) return false;
    return true;
}

// write `n` bytes only if the place is empty or already holds exactly them
static int ns_put(char *at, const void *v, int n, const char *what, int id) {
    if (!memcmp(at, v, n)) return 0;
    if (!ns_zero(at, n)) { if (!ns_quiet(id)) logf("new skill %d: %s at %p is not empty - skill left out", id, what, (void *)at); return -1; }
    memcpy(at, v, n);
    return 1;
}

// v5.32 the skill's colour as the effect recolour takes it (sat 0 = 100 %, 1 = 0 %; val 0 = 100 %)
static FxColour ns_colour(const NewSkill &s) {
    FxColour c = {((s.hue % 360) + 360) % 360, s.sat == 0 ? 100 : s.sat == 1 ? 0 : s.sat, s.val ? s.val : 100, s.cmode};
    return c;
}
static bool ns_coloured(const NewSkill &s) { FxColour c = ns_colour(s); return c.hue || c.sat != 100 || c.val != 100 || c.mode == 2; }

static bool bfr_lists(int id);                                  // v5.59 cwo_bfr.inl: the Battle for Reality pack is on and holds this id
static int ns_apply_one(char *gs, int k) {
    NewSkill &s = g_ns[k];
    if (!g_ns_test && bfr_lists(s.id)) return -3;                  // v5.59 the pack's skill there (Mirror wrote over BFR 376 at each refresh)
    if (!g_ns_test && ns_coloured(s) && g_fx_state == 1 && !fx_ei_ready()) return -2;   // v5.32 the effect index is not in yet
    char *rec = gs + 0x18 + NS_REC * s.id;
    const char *src = gs + 0x18 + NS_REC * s.src;
    int row = NS_ROW0 + k;
    if (s.dirty && *(short *)rec == (short)row && *(short *)(rec + 2) == (short)s.id) {   // changed: our places cleared, written again
        memset(rec + 2, 0, NS_REC - 2);
        for (int j = 0; j < 6; j++) memset(gs + NS_PROF_BASE + NS_PROF * (NS_PROF0 + 6 * k + j), 0, NS_PROF);
        memset(gs + NS_STR0 + NS_STR * k, 0, NS_STR);
        memset(gs + NS_TEXT + 12 * row, 0, 12);
    }
    s.dirty = 0;
    // already in place? (record carries our row and id)
    if (*(short *)rec == (short)row && *(short *)(rec + 2) == (short)s.id) return 0;
    if (!ns_zero(rec + 2, NS_REC - 2)) { if (!ns_quiet(s.id)) logf("new skill %d: record is not empty - left out", s.id); return -1; }
    if (*(short *)(src + 2) == 0) { if (!ns_quiet(s.id)) logf("new skill %d: source %d is empty - left out", s.id, s.src); return -1; }
    char r[NS_REC];
    memcpy(r, src, NS_REC);
    // profiles: copies with the effect swaps, in empty profile slots 410 + 6k ..
    static const int PF[6] = {0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E};
    for (int j = 0; j < 6; j++) {
        short p = *(short *)(src + PF[j]);
        if (p <= 0) continue;
        int np = NS_PROF0 + 6 * k + j;
        char pr[NS_PROF];
        memcpy(pr, gs + NS_PROF_BASE + NS_PROF * p, NS_PROF);
        short *req = (short *)(pr + 4);                      // the effect; +0x06 is the sound (batch 071), kept
        // v2.9 Skill Creator: another skill's effect (+0x04), sound (+0x06) or animation (motion slot +0x4C, LIVE-003)
        auto ground = [&](int id) -> const char * {
            short gp = *(short *)(gs + 0x18 + NS_REC * id + 0x14);
            return gp > 0 ? gs + NS_PROF_BASE + NS_PROF * gp : NULL;
        };
        if (s.fx > 0) {
            const char *o = ground(s.fx);
            *req = o ? *(const short *)(o + 4) : *(short *)(gs + 0x18 + NS_REC * s.fx + 0x20);
        }
        if (s.snd > 0 && ground(s.snd)) *(short *)(pr + 6) = *(const short *)(ground(s.snd) + 6);
        if (s.anim > 0 && ground(s.anim)) *(short *)(pr + 0x4C) = *(const short *)(ground(s.anim) + 0x4C);
        // v5.30 (owner 2026-10-27): the
        // voice value v = profile +0x08; the move start says a line of group (v - 1) % 6 + 10 (FUN_14025a3e0, gsdata +0x33004)
        if (s.voice >= 1 && s.voice <= 6) *(short *)(pr + 8) = (short)s.voice;
        else if (s.voice == 7) *(short *)(pr + 8) = 0;
        // colour copies last (v3.0): a recoloured copy of the effect it ends up with (its own, or Effect from's)
        // v5.32 (owner: every hue, every skill): a coloured skill shows its effect through a number of its own that the
        // loader recolours as it loads (cwo_fxcolour.inl); the old 30-degree palette copies are no longer used for it
        bool colour = !g_ns_test && ns_coloured(s) && g_fx_state == 1;
        for (int m = 0; m < s.nmap; m++)
            if (*req == s.from[m]) { if (!(colour && s.pal > 0 && s.from[m] == s.pal_req)) *req = (short)s.to[m]; break; }
        if (colour && *req > 0 && *req < FX_REQ0) {
            int cr = fx_colour_req(s.id, *req, ns_colour(s));
            if (cr > 0) *req = (short)cr;
        }
        if (ns_put(gs + NS_PROF_BASE + NS_PROF * np, pr, NS_PROF, "profile", s.id) < 0) return -1;
        *(short *)(r + PF[j]) = (short)np;
    }
    // text: entry + strings
    char *entry = gs + NS_TEXT + 12 * row, *str = gs + NS_STR0 + NS_STR * k;
    char strs[NS_STR] = {};
    memcpy(strs, s.name, strlen(s.name));
    memcpy(strs + 0x30, s.desc, strlen(s.desc) < NS_STR - 0x31 ? strlen(s.desc) : NS_STR - 0x31);
    int ent[3] = {9001, (int)(str - entry), (int)(str + 0x30 - entry)};
    if (ns_put(str, strs, NS_STR, "text", s.id) < 0 || ns_put(entry, ent, 12, "text row", s.id) < 0) return -1;
    // v4.7 (owner): a skill's visuals are placed by its family and
    // flight code (+0x52 family, +0x54.. family values, +0x7A kind, +0x7C.. kind values; SKILL_BEHAVIOUR_FAMILIES /
    // SKILL_KIND_PARAMETERS docs), so 'everything' also takes +0x50..+0x8E from the Effect-from skill
    if (s.fx > 0 && s.fx < 753 && s.fxmode) {
        const char *fr = gs + 0x18 + NS_REC * s.fx;
        if (*(short *)(fr + 2) != 0) memcpy(r + 0x50, fr + 0x50, NS_REC - 0x50);
    }
    // record last, so the game never sees half a skill
    *(short *)(r + 0x00) = (short)row;
    *(short *)(r + 0x02) = (short)s.id;
    *(short *)(r + 0x06) = (short)s.id;
    if (*(short *)(src + 0x32) == (short)s.src) *(short *)(r + 0x32) = (short)s.id;
    if (s.school > 0) *(short *)(r + 0x12) = (short)s.school;
    if (s.cost >= 0) *(short *)(r + 0x28) = (short)s.cost;
    if (s.damage >= 0) *(short *)(r + 0x58) = (short)s.damage;
    for (int i = 0; i < s.nfo; i++)                         // v4.4 the record fields set in the Skill Creator
        if (s.fo[i] >= 0x0E && s.fo[i] < 0x90 && s.fo[i] != 0x32) *(short *)(r + s.fo[i]) = (short)s.fv[i];
    char keep[2];
    memcpy(keep, rec, 2);                                   // empty records keep a leftover text id at +0x00
    memcpy(rec + 2, r + 2, NS_REC - 2);
    memcpy(rec, r, 2);
    if (*(unsigned *)(gs + 0x14) < (unsigned)s.id) *(unsigned *)(gs + 0x14) = s.id;
    logf("new skill %d '%s' from %d written: record %p (was text id %d), text row %d, %d effect swap(s)", s.id, s.name, s.src,
         (void *)rec, *(short *)keep, row, s.nmap);
    return 1;
}

static void ns_big_stock(int id);                          // v5.148.9 cwo_bigids.inl (included later)
static void ns_maps_and_stock(int id) {
    int *first = (int *)(g_base + RVA_FIRST_MAP) + id, *last = (int *)(g_base + RVA_LAST_MAP) + id;
    if (*first == -1) *first = id;
    if (*last == -1) *last = id;
    ULONG_PTR mgr = *(ULONG_PTR *)(g_base + RVA_ARSENAL);
    ULONG_PTR side = mgr ? *(ULONG_PTR *)(mgr + 0x33A0) : 0;
    if (!side) return;
    unsigned char *stock = (unsigned char *)(side + 0x644 + id);
    if (*stock == 0 || *stock == 0xFF) {
        *stock = 3;
        short *sup = (short *)(side + 0xA2C + 4 * id);
        if (sup[0] == 0 && sup[1] == 0) sup[1] = 2;
        static unsigned char said[1200];
        if (id >= 0 && id < 1200 && !said[id]) { said[id] = 1; logf("new skill %d: 3 copies in stock", id); }   // v5.148.9 once
    }
    ns_big_stock(id);
}

// every second (game thread): new skills into the skill data when it is loaded and they are missing
static void sc_pending_step();                              // v5.32 below: a kept unsaved skill leaves after the match
static bool sc_skill_busy(int id);                          // v5.33 below: the looped / kept skill is in use
static volatile LONG g_scp_stop_req, g_scp_resume;
static int g_scp_on, g_scp_skill;
static void bs_mirror_ns(const char *gs, int id, int row, int prof0, int nprof);   // v5.111 cwo_bigskills.inl
static void ns_step() {
    if (!g_ns_test) { sc_pending_step(); fx_ei_keep(); }
    if (g_ns_n <= 0) return;
    ULONGLONG now = GetTickCount64();
    if (!g_ns_test && now < g_ns_next) return;
    g_ns_next = now + 1000;
    char *gs = g_ns_test ? g_ns_test : *(char **)(g_base + RVA_GSDATA);
    if (!gs || *(unsigned *)gs != 0x44004) return;
    for (int k = 0; k < g_ns_n; k++) {
        if (!g_ns_test && g_ns[k].dirty && sc_skill_busy(g_ns[k].id)) {   // v5.33 its last use finishes first
            if (g_scp_on && g_scp_skill == g_ns[k].id) { InterlockedExchange(&g_scp_stop_req, 1); InterlockedExchange(&g_scp_resume, 1); }
            g_ns_next = now + 250;
            continue;
        }
        int r = ns_apply_one(gs, k);
        g_ns_said[k] = r;
        if (r >= 0 && !g_ns_test) { bs_mirror_ns(gs, g_ns[k].id, NS_ROW0 + k, NS_PROF0 + 6 * k, 6); ns_maps_and_stock(g_ns[k].id); }
    }
}

static bool sc_self_test(char *gs);
// self-test on a copy-shaped stand-in: a source record + profile; the new skill lands in empty places, swaps its
// effect, and a busy record is refused
static bool ns_self_test() {
    static char gs[0x44004 + 16];
    memset(gs, 0, sizeof(gs));
    *(unsigned *)gs = 0x44004;
    *(unsigned *)(gs + 0x14) = 374;
    char *src = gs + 0x18 + NS_REC * 61;
    *(short *)(src + 2) = 61; *(short *)(src + 0x12) = 3; *(short *)(src + 0x14) = 61; *(short *)(src + 0x28) = 2;
    *(short *)(gs + NS_PROF_BASE + NS_PROF * 61 + 4) = 61;
    *(short *)(gs + NS_PROF_BASE + NS_PROF * 61 + 6) = 261;
    NewSkill keep[2];
    int keep_n = g_ns_n;
    memcpy(keep, g_ns, sizeof(keep));
    NewSkill a = {376, 61, "Blue Test", "desc", {61, 261}, {6004, 6005}, 2, 2, 5, -1};
    NewSkill b = {377, 61, "Busy", "", {}, {}, 0, -1, -1, -1};
    a.voice = 2;                                               // v5.30: the chant line on its profile
    g_ns[0] = a; g_ns[1] = b; g_ns_n = 2;
    g_ns_said[0] = g_ns_said[1] = 0;
    *(short *)(gs + 0x18 + NS_REC * 377 + 0x40) = 7;          // record 377 not empty
    g_ns_test = gs;
    ns_step();
    ns_step();                                                 // second pass: nothing more to write
    char *r = gs + 0x18 + NS_REC * 376;
    short np = *(short *)(r + 0x14);
    char *pr = gs + NS_PROF_BASE + NS_PROF * np;
    char *entry = gs + NS_TEXT + 12 * NS_ROW0;
    bool ok = *(short *)(r + 2) == 376 && *(short *)r == NS_ROW0 && np == NS_PROF0 && *(short *)(pr + 4) == 6004 &&
              *(short *)(pr + 6) == 261 && *(short *)(r + 0x12) == 2 && *(short *)(r + 0x28) == 5 &&
              !strcmp(entry + *(int *)(entry + 4), "Blue Test") && !strcmp(entry + *(int *)(entry + 8), "desc") &&
              *(unsigned *)(gs + 0x14) == 376 && *(short *)(gs + 0x18 + NS_REC * 377 + 2) == 0 &&
              *(short *)(gs + NS_PROF_BASE + NS_PROF * 61 + 4) == 61 &&
              *(short *)(pr + 8) == 2 && *(short *)(gs + NS_PROF_BASE + NS_PROF * 61 + 8) == 0;
    bool scok = ok && sc_self_test(gs);                       // v2.9 Skill Creator options and a change in place
    {   // v3.2: an installed line with a palette, then the in-game saved version of the same skill: merged
        wchar_t pa[MAX_PATH], pb[MAX_PATH];
        swprintf_s(pa, MAX_PATH, L"%sns_test_a.txt", g_dir);
        swprintf_s(pb, MAX_PATH, L"%sns_test_b.txt", g_dir);
        FILE *f = NULL;
        if (cwo_wfopen(&f, pa, L"w") == 0 && f) { fprintf(f, "390\t61\tInstalled\t-\t61=6004 pal=7000:61\n"); fclose(f); }
        if (cwo_wfopen(&f, pb, L"w") == 0 && f) { fprintf(f, "390\t61\tSaved\t-\thue=30 damage=4 voice=7\n"); fclose(f); }
        NewSkill keep2[2];
        memcpy(keep2, g_ns, sizeof(keep2));
        int keep_n2 = g_ns_n;
        g_ns_n = 0;
        int a1 = ns_parse_file(pa, false), b1 = ns_parse_file(pb, true);
        bool mok = a1 == 1 && b1 == 1 && g_ns_n == 1 && !strcmp(g_ns[0].name, "Saved") && g_ns[0].pal == 7000 && g_ns[0].pal_req == 61 &&
                   g_ns[0].nmap == 1 && g_ns[0].to[0] == 6004 && g_ns[0].damage == 4 && g_ns[0].hue == 30 && g_ns[0].voice == 7;
        logf("self-test: saved skills merge %s", mok ? "ok" : "FAILED");
        memcpy(g_ns, keep2, sizeof(keep2));
        g_ns_n = keep_n2;
        DeleteFileW(pa); DeleteFileW(pb);
        scok = scok && mok;
    }
    unsigned sc_turn_rgb(unsigned, int);
    bool hueok = sc_turn_rgb(0xFFFF0000u, 120) == 0xFF00FF00u && sc_turn_rgb(0x80FFFFFFu, 90) == 0x80FFFFFFu &&
                 sc_turn_rgb(0xFF0000FFu, 120) == 0xFFFF0000u;   // v3.0 colour turn: red->green, white stays, blue->red
    unsigned sc_tint(unsigned, int, int, int);
    hueok = hueok && sc_tint(0xFFFF0000u, 0, 0, 100) == 0xFFFFFFFFu && sc_tint(0xFFFF0000u, 0, 100, 50) == 0xFF800000u;   // v3.1 grey, half bright
    logf("self-test: color turn %s", hueok ? "ok" : "FAILED");
    scok = scok && hueok;
    logf("self-test: Skill Creator %s", scok ? "ok" : "FAILED");
    ok = scok;
    g_ns_test = NULL;
    g_ns_said[0] = g_ns_said[1] = 0;
    memcpy(g_ns, keep, sizeof(keep));
    g_ns_n = keep_n;
    logf("self-test: new skills %s (record 376 profile %d effects %d/%d, busy 377 refused, source untouched)", ok ? "ok" : "FAILED", np,
         *(short *)(pr + 4), *(short *)(pr + 6));
    return ok;
}

// ---- v2.9 Skill Creator menu (Stage Creator menu > Skill Creator) ----
// A new skill = a copy of any skill with changes: school, aura cost, damage, and the effect / sound / animation of
// other skills. Saved to CwoNewSkills.txt (the same file the colour tool writes) and written into the game's skill
// data within a second (ns_step), so it can be dropped as a capsule and used at once.
// v5.186.0 a skill's options as saved in CwoNewSkills.txt (ns_save) and sent by FEEDBACK > Skill to share
static void ns_opts(const NewSkill &s, char *o, size_t n) {
    size_t at = 0;
    o[0] = 0;
    auto add = [&](const char *fmt, int x, int y) {
        if (at + 24 >= n) return;
        int w = sprintf_s(o + at, n - at, fmt, x, y);
        if (w > 0) at += (size_t)w;
    };
    for (int m = 0; m < s.nmap; m++) add("%d=%d ", s.from[m], s.to[m]);
    if (s.school > 0) add("school=%d ", s.school, 0);
    if (s.cost >= 0) add("cost=%d ", s.cost, 0);
    if (s.damage >= 0) add("damage=%d ", s.damage, 0);
    if (s.fx > 0) add("fx=%d ", s.fx, 0);
    if (s.snd > 0) add("snd=%d ", s.snd, 0);
    if (s.anim > 0) add("anim=%d ", s.anim, 0);
    if (s.voice > 0) add("voice=%d ", s.voice, 0);
    if (s.hue > 0) add("hue=%d ", s.hue, 0);
    if (s.sat > 0 && s.sat != 100) add("sat=%d ", s.sat, 0);
    if (s.val > 0 && s.val != 100) add("val=%d ", s.val, 0);
    if (s.trail > 0 && s.trail_part > 0) add("trail=%d:%d ", s.trail, s.trail_part - 1);
    else if (s.trail > 0) add("trail=%d ", s.trail, 0);
    if (s.pal > 0) add("pal=%d:%d ", s.pal, s.pal_req);
    for (int i = 0; i < s.nfo; i++) add("r%x=%d ", s.fo[i], s.fv[i]);   // v4.4 record fields
    if (s.fxmode) add("fxm=1 ", 0, 0);                                // v4.7 Effect from brings its behaviour
    if (s.cmode) add("cm=%d ", s.cmode, 0);                           // v5.32 colour mode
}

static void ns_save() {
    wchar_t exe[MAX_PATH], path[MAX_PATH], tmp[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) slash[1] = 0;
    swprintf_s(path, MAX_PATH, L"%sCwoNewSkills.txt", g_dir);       // v3.2: the loader's data folder (writable)
    swprintf_s(tmp, MAX_PATH, L"%sCwoNewSkills.tmp", g_dir);
    (void)exe;
    FILE *f = NULL;
    if (cwo_wfopen(&f, tmp, L"w") != 0 || !f) { logf("new skills: cannot write %ls", tmp); set_status("Could not save the skill (see the log)"); return; }
    fprintf(f, "# new id\tsource\tname\tdescription\toptions (OLD=NEW effect swaps, school=, cost=, damage=, fx=, snd=, anim=)\n");
    for (int k = 0; k < g_ns_n; k++) {
        const NewSkill &s = g_ns[k];
        if (s.unsaved || s.comm) continue;                   // v4.2: a live skill not saved yet; v5.186 community: the pack's file
        fprintf(f, "%d\t%d\t%s\t%s\t", s.id, s.src, s.name, s.desc[0] ? s.desc : "-");
        { char o[700]; ns_opts(s, o, sizeof(o)); fputs(o, f); }   // v5.186 the same text the FEEDBACK page sends
        fprintf(f, "\n");
    }
    fclose(f);
    if (!g_ns_test) MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
    else DeleteFileW(tmp);
}

// v4.4 (owner): the record fields read by the game's code (GSDATA_FIELDS_RECONCILED_2026-10-21.md, Ghidra
// batches 051-062b; ground / air values from 0x14024fce0; attack kinds SKILL_BEHAVIOUR_FAMILIES doc).
static const int SC_ROWS = 57;                                  // v5.112 + 54 / 55 the skill finder; v5.160 + 56 closest skill
static const char *SC_NAMES[SC_ROWS] = {"Skill", "Based on", "School", "Aura cost", "Damage", "Effect from", "Sound from",
                                       "Animation from", "Projectile extra", "Color (hue)", "Saturation", "Brightness",
                                       "Save + use now", "Drop a capsule", "Put in hand", "Back",
                                       "Capsule type", "Uses", "Aura level on use", "Cost change per use", "Health on use",
                                       "Where it works", "Attack kind", "Kind value 1", "Kind value 2", "Kind value 3",
                                       "Kind value 4", "Kind value 5", "Kind value 6", "Kind value 7", "Kind value 8",
                                       "Kind value 9", "Kind value 10", "Reset to the base skill", "  Flight from", "Voice",
                                       "Color mode", "Skill family", "f54", "f56", "f58", "f5A", "f5C", "f5E", "f60", "f62", "f64",
                                       "f66", "f68", "f6A", "f6C", "Needs", "  needs amount", "-",
                                       "Find: school / kind", "Find: name starts with", "Closest skill"};
// v5.32 (owner 2026-10-27): the rows follow the skill - each
// family (+0x52) shows its own fields by name, the attack kind only the values it reads, nothing "not used"
static int sc_order(int *ids);
static int sc_id(int row) {
    int ids[SC_ROWS];
    int n = sc_order(ids);
    return row >= 0 && row < n ? ids[row] : 15;
}
static int sc_row_of(int id) {
    int ids[SC_ROWS];
    int n = sc_order(ids);
    for (int r = 0; r < n; r++) if (ids[r] == id) return r;
    return 0;
}
static int sc_rows_n() { int ids[SC_ROWS]; return sc_order(ids); }
struct ScFld { int off, lo, hi; const char *desc; };
static const ScFld SC_FLD[17] = {
    {0x0E, 0, 6, "The capsule it comes in: attack, defence, erase, status, special, environment"},
    {0x30, 0, 99, "How many times it can be used before it is gone (0 = unlimited)"},
    {0x2A, -1, 10, "Aura level when used: N needs level N and lowers it by N; -1 costs all your aura"},
    {0x2C, -10, 10, "The aura cost rises (or falls) by this much each time it is used (max 99)"},
    {0x2E, -99, 99, "Health when used: N takes N health; -1 takes half; negative gives health"},
    {0x4C, 0, 3, "Where it can be used: on the ground, in the air, or both"},
    {0x7A, 0, 20, "How it travels and where it hits (the game's 21 attack kinds)"},
    {0x7C, -999, 9999, ""}, {0x7E, -999, 9999, ""}, {0x80, -999, 9999, ""}, {0x82, -999, 9999, ""}, {0x84, -999, 9999, ""},
    {0x86, -999, 9999, ""}, {0x88, -999, 9999, ""}, {0x8A, -999, 9999, ""}, {0x8C, -999, 9999, ""}, {0x8E, -999, 9999, ""}};
static int g_sc_hand;                                        // v3.0: hand slot 0..3 for "Put in hand"
static void sc_preview();
static NewSkill g_sc;                                        // the draft
static int g_sc_k = -1;                                      // -1 = a new skill, else g_ns index

// v5.115 (owner 2026-11-08, BFR: the outfit effect list showed "20 ?, 21 ?, 22 ?"; the BFR data folder has no CwoSkills.txt):
// the name the game itself shows - record (*0x1404bdc50 + 0x90 id) +0x00 = its text row, rows *0x1404bdc48 are 12 bytes
// {9001, name offset, description offset} relative to the row (cwo_bigskills.inl / cwo_bfr.inl); the list file is the fallback
static const char *sc_live_name(int id) {
    static char buf[4][40];
    static int k;
    __try {
        int nrec = *(int *)(*(char **)(g_base + 0x4bdc40) + 4);
        const char *recs = *(const char **)(g_base + 0x4bdc50), *rows = *(const char **)(g_base + 0x4bdc48);
        if (id <= 0 || id >= nrec || !recs || !rows) return NULL;
        short row = *(const short *)(recs + 0x90 * (ULONG_PTR)id);
        if (row < 0) return NULL;
        const char *e = rows + 12 * (ULONG_PTR)row;
        if (*(const int *)e != 9001) return NULL;
        const char *nm = e + *(const int *)(e + 4);
        char *out = buf[k++ & 3];
        int n = 0;
        while (n < 39 && nm[n] && (unsigned char)nm[n] >= 32) { out[n] = nm[n]; n++; }
        out[n] = 0;
        return n ? out : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}
static const char *sc_skill_name(int id) {
    const char *live = sc_live_name(id);
    if (live) return live;
    for (int i = 0; i < g_sk_count; i++) if (g_sk[i].id == id) return g_sk[i].name;
    return "?";
}

static void sc_draft_new(int src) {
    memset(&g_sc, 0, sizeof(g_sc));
    g_sc.src = src;
    g_sc.school = g_sc.cost = g_sc.damage = g_sc.fx = g_sc.snd = g_sc.anim = -1;
    g_sc.fxmode = 0;                                         // v5.32 (owner): flight from the base skill
    g_sc_k = -1;
}

static void sc_open() { if (g_sc.src <= 0) sc_draft_new(61); }

// v5.112 (owner 2026-11-07): two finder rows under "Skill"; Based on / Effect from /
// Sound from / Animation from / Projectile extra step only through the skills that match (school = record +0x12, capsule
// type = record +0x0E: 1 attack, 2 defence, 3 erase, 4 status, 5 special, 6 environment - GSDATA_FIELDS_RECONCILED).
static int g_sc_fcat, g_sc_flet;                                // 0 = all / any letter
static const char *SC_FCAT[12] = {"all skills", "Psycho", "Optical", "Nature", "Ki", "Faith", "attack", "defence", "erase",
                                  "status", "special", "environment"};
static bool sc_match(int id) {
    if (!g_sc_fcat && !g_sc_flet) return true;
    if (g_sc_flet) {
        const char *nm = sc_skill_name(id);
        if (toupper((unsigned char)nm[0]) != 'A' + g_sc_flet - 1) return false;
    }
    if (g_sc_fcat) {
        short sch = 0, typ = 0;
        __try {
            char *gs = *(char **)(g_base + RVA_GSDATA);
            char *rec = NULL;
            ULONG_PTR live = *(ULONG_PTR *)(g_base + 0x4bdc50);   // the records the game reads (ours while the big tables are on)
            if (live && id > 0 && id < 1200) rec = (char *)live + NS_REC * id;
            else if (gs && *(unsigned *)gs == 0x44004 && id > 0 && id < 753) rec = gs + 0x18 + NS_REC * id;
            if (rec) { sch = *(short *)(rec + 0x12); typ = *(short *)(rec + 0x0E); }
        } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
        if (g_sc_fcat <= 5 ? sch != g_sc_fcat : typ != g_sc_fcat - 5) return false;
    }
    return true;
}
static int sc_cycle_all(int id, int dir, bool allow_none);
static int sc_cycle(int id, int dir, bool allow_none) {         // v5.112 the finder: only matching skills
    int v = id;
    for (int step = 0; step < g_sk_count + 1; step++) {
        v = sc_cycle_all(v, dir, allow_none);
        if (v < 0 || sc_match(v)) return v;
    }
    return id;                                                   // nothing matches: stays
}
static int sc_cycle_all(int id, int dir, bool allow_none) {      // next / previous skill in the list (stock and new)
    int at = -1;
    for (int i = 0; i < g_sk_count; i++) if (g_sk[i].id == id) at = i;
    if (at < 0) return g_sk_count ? g_sk[dir > 0 ? 0 : g_sk_count - 1].id : -1;
    int n = at + dir;
    if (allow_none && (n < 0 || n >= g_sk_count)) return -1;
    return g_sk[(n % g_sk_count + g_sk_count) % g_sk_count].id;
}

static int sc_step(int v, int dir, int lo, int hi) {         // lo-1 means "as the base skill"
    v += dir;
    if (v < lo - 1) v = hi;
    if (v > hi) v = lo - 1;
    return v;
}

static int sc_field(int off, bool *set);
static int sc_base_field(int off) {                         // v4.4 the base skill's value of a record field
    __try {
        char *gs = *(char **)(g_base + RVA_GSDATA);
        int from = (off >= 0x50 && g_sc.fx > 0 && g_sc.fx < 753 && g_sc.fxmode) ? g_sc.fx : g_sc.src;   // v4.7
        if (gs && *(unsigned *)gs == 0x44004 && from > 0 && from < 753) return *(short *)(gs + 0x18 + NS_REC * from + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
static int sc_field(int off, bool *set) {
    for (int i = 0; i < g_sc.nfo; i++) if (g_sc.fo[i] == off) { if (set) *set = true; return g_sc.fv[i]; }
    if (set) *set = false;
    return sc_base_field(off);
}
static void sc_field_turn(const ScFld &fd, int dir) {
    int v = sc_field(fd.off, NULL), step = (fd.hi - fd.lo) > 200 ? 5 : 1;
    v += dir * step;
    if (v < fd.lo) v = fd.hi;
    if (v > fd.hi) v = fd.lo;
    int at = -1;
    for (int i = 0; i < g_sc.nfo; i++) if (g_sc.fo[i] == fd.off) at = i;
    if (v == sc_base_field(fd.off)) {                        // back to the base value: no longer set
        if (at >= 0) { g_sc.fo[at] = g_sc.fo[g_sc.nfo - 1]; g_sc.fv[at] = g_sc.fv[g_sc.nfo - 1]; g_sc.nfo--; }
        return;
    }
    if (at < 0) { if (g_sc.nfo >= 24) return; at = g_sc.nfo++; g_sc.fo[at] = fd.off; }
    g_sc.fv[at] = v;
}


// v4.6 (owner 2026-10-25 photo: "capsule type needs to be specific ... Attack kind ... kind value 1 through 10, I don't know
// what none of that is"). v5.32 (owner 2026-10-27). All from the game's
// code (Ghidra full pass 065, 2026-10-27 - the kind handler table 0x1403fb240 + kind x 0x18):
//   3 and 14 share one handler pair (0x140265d60 / 0x140265e00): a hit sphere of the reach on your own body, checked once
//     against the locked target; the movement code dashes for kind 14 only (Pursuit), so 3 is the same hit without the dash.
//   15 and 16 share 0x14026a510 / 0x14026a7f0 and are put on the ground under the target (0x14026e410 mode 1); at release
//     kind 16 makes one under EVERY standing fighter (0x1402626d0) and has no reach check (0x140273d40) - Earthquake.
//   18 and 19: both handlers are an empty function (0x140004660, ret 0): nothing moves and nothing hits on its own; the boss
//     attacks of kind 18 get their hits from the boss's own program (0x140263ce0 with a radius of its own).
//   4: no flight and no sphere - after +0x7E frames it hits the locked target (mode 2: every fighter) (0x140265f50).
static const char *SC_CAPS[7] = {"none", "attack", "defence", "erase", "status", "special", "environment"};
static const char *SC_KINDS[21] = {"shot (straight, can home)", "lobbed (bomb arc)", "falls from the sky", "hit around your body",
                                   "area: hits the target", "crawls along the floor", "laser beams", "lightning strike",
                                   "trap / mine / gas", "wave around you", "forward pull", "throws a nearby object", "sword swing",
                                   "close-range hit", "dash and hit", "press under the target", "quake under every fighter",
                                   "bouncing shot", "boss body (no flight)", "empty (no code of its own)", "rain / storm"};   // v5.36 no skill names (owner)
static const char *SC_KIND_DESC[21] = {
    "Flies straight from you; can turn toward the target (homing)", "Thrown in an arc; falls under gravity",
    "Starts above and behind the target, falls onto it", "A hit sphere around your own body, once (no dash)",
    "No flight: after a delay it hits the locked target (or every fighter)", "Runs along the ground toward the target",
    "One or more beams from you (2, 3 or 8 beams)", "Strikes the target from above for a time",
    "Placed where you stand: goes off by time, by sensor, as gas or by remote", "A ring that grows around you",
    "Moves forward, pulling fighters in and hitting them on the way", "Lifts a stage object near you and throws it", "A blade swing in front of you",
    "A close hit in front of you", "Dashes to a knocked-down target and hits", "A shock area on the ground under the target",
    "A shock area under every standing fighter (no reach limit)", "A shot that bounces off walls and keeps going",
    "Moves and hits only through a boss's own moves", "Empty: no movement, no hit. The game's attack code has no case for 19, so the loader can give it new behaviour later",
    "Drops over an area for a time"};
struct ScKv { const char *name, *desc; };
static const ScKv SC_KV_NONE = {NULL, NULL};
static ScKv sc_kv(int kind, int i) {                           // value i (0-9 = +0x7C .. +0x8E) of attack kind `kind`; name NULL = not read
    static const ScKv LIFE = {"Range / life", "Negative: a range of -value x 10 units; positive: frames it lives (0x1402636f0)"};
    switch (kind) {
    case 0: { static const ScKv K[5] = {LIFE, {"Start speed", "Speed at the start (value / 10.8 units a frame)"},
              {"End speed", "Speed after the ramp"}, {"Speed ramp", "Frames from start to end speed (0 = start speed all the way)"},
              {"Homing", "Turn toward the target, 0.1 degree a frame (0 = straight)"}}; return i < 5 ? K[i] : SC_KV_NONE; }
    case 1: { static const ScKv K[4] = {{"Throw speed", "Forward speed (0 = the next value is used)"}, {"Other speed", "Used when Throw speed is 0"},
              {"Value 3", "1-10 in the game; read first by the lob code"}, {"Upward speed", "How high it is thrown"}}; return i < 4 ? K[i] : SC_KV_NONE; }
    case 2: { static const ScKv K[4] = {{"Start height", "Above the target, x 10 units (6 = 60)"}, {"Start back", "Behind along your facing, x 10 units"},
              {"Fall speed", "Value / 10.8 units a frame"}, {"Second speed", "Value / 10.8 units a frame"}}; return i < 4 ? K[i] : SC_KV_NONE; }
    case 3: case 12: case 13: case 14: { static const ScKv K[3] = {{"Reach", "The hit sphere's radius, x 0.1 units (130 = 13)"},
              {"Frames", "8 in the game"}, {"Arc", "Swing width, 0.1 degree"}}; return i < 3 ? K[i] : SC_KV_NONE; }
    case 4: { static const ScKv K[2] = {{"Mode", "0: the target must stay in reach; 2: every fighter; else the locked target"},
              {"Delay", "Frames before the hit (30 a second)"}}; return i < 2 ? K[i] : SC_KV_NONE; }
    case 5: { static const ScKv K[6] = {LIFE, {"Start speed", "Value / 10.8 units a frame"}, {"End speed", "After the ramp"},
              {"Speed ramp", "Frames to the end speed"}, {"Spread", "0.01 degree (500 = 5 degrees)"}, {"Value 6", "100 in the game"}};
              return i < 6 ? K[i] : SC_KV_NONE; }
    case 6: { static const ScKv K[8] = {{"Range", "Negative: range x 10 units (-80 = 800)"}, {"Beam speed", "Value / 10.8 units a frame"},
              {"Beam timing", "Frames (8-20 in the game)"}, {"Angle 1", "Degrees: beam k leaves at Angle 1 + k x Step 1"},
              {"Angle 2", "Degrees: beam k at Angle 2 + k x Step 2"}, {"Beam count", "Double 2, Triple 3, Octo 8"},
              {"Step 1", "Degrees between beams (angle 1)"}, {"Step 2", "Degrees between beams (angle 2)"}}; return i < 8 ? K[i] : SC_KV_NONE; }
    case 7: { static const ScKv K[5] = {{"Time 1", "Frames (added to Time 2)"}, {"Time 2", "Frames"}, {"Value 3", "x 10"},
              {"Value 4", "x 10"}, {"Hit radius", "x 0.1 units (300 = 30)"}}; return i < 5 ? K[i] : SC_KV_NONE; }
    case 8: { static const ScKv K[4] = {{"Mode", "0 goes off at the end of its time, 1 gas cloud, 2 remote"}, {"Trigger radius", "Gas: x 0.1 units"},
              {"Fuse", "Frames before it goes off (Timer Mine 150)"}, {"Sensor range", "x 0.1 units"}}; return i < 4 ? K[i] : SC_KV_NONE; }
    case 9: { static const ScKv K[1] = {{"Reach", "Hit radius, x 0.1 units (400 = 40); grows over 30 frames"}}; return i < 1 ? K[i] : SC_KV_NONE; }
    case 10: { static const ScKv K[6] = {{"Time", "Frames"}, {"Hit radius", "x 0.1 units"}, {"Value 3", "10-12 in the game"},
               {"Forward speed", "Value / 10.8 units a frame"}, {"Second speed", "Value / 10.8 units a frame"}, {"Time 2", "Frames"}};
               return i < 6 ? K[i] : SC_KV_NONE; }
    case 11: { static const ScKv K[8] = {LIFE, {"Speed before", "Before the switch"}, {"Speed after", "After the switch"},
               {"Switch after", "Frames"}, {"Value 5", "x 0.1"}, {"Value 6", "Read by the code"}, SC_KV_NONE, {"Value 8", "Read by the code"}};
               return i < 8 ? K[i] : SC_KV_NONE; }
    case 15: case 16: { static const ScKv K[3] = {{"Time 1", "Frames; also how long the area shrinks at the end"}, {"Time 2", "Frames"},
               {"Hit radius", "x 0.1 units (300 = 30)"}}; return i < 3 ? K[i] : SC_KV_NONE; }
    case 17: { static const ScKv K[10] = {LIFE, {"Speed", "Value / 10.8 units a frame"}, {"End speed", "After the ramp"}, {"Ramp", "Frames"},
               SC_KV_NONE, {"Angle 1", "Degrees"}, {"Count", "More than 1 = several beams"}, SC_KV_NONE, {"Angle 2", "Degrees"},
               {"Bounces", "Copied to the projectile +0x1C8"}}; return K[i]; }
    case 20: { static const ScKv K[9] = {{"Time 1", "Frames (added to Time 2)"}, {"Time 2", "Frames"}, {"Value 3", "6 in the game"},
               {"Value 4", "5 in the game"}, SC_KV_NONE, {"Area radius", "x 0.1 units (1000 = 100)"}, {"Hit radius", "x 0.1 units (300 = 30)"},
               {"Drop speed", "Value / 10.8 units a frame"}, {"Spread angle", "Degrees (90 in the game)"}}; return i < 9 ? K[i] : SC_KV_NONE; }
    }
    return SC_KV_NONE;                                        // 18, 19: this kind reads no values
}
// v5.32 the per-family fields (+0x54 .. +0x6C), named from the game's code and the stock skills that use each value
static const char *SC_FAMILY[7] = {"attack", "erase", "defence", "movement", "status", "environment", "copy"};
static const char *sc_formula(int v) {
    static const char *F[32] = {"STR as set", "(target health - yours) x STR", "(your health - target's) x STR",
        "STR x your used attacks", "STR x your used defences", "STR x your used capsule-3 skills", "STR x your used capsule-4 skills",
        "STR x your used capsule-5 skills", "STR x your used capsule-0 skills", "your used attacks / STR", "STR x attacks in your hand",
        "STR x attacks in your hand", "STR x attacks in your hand", "STR x attacks in your hand", "STR x attacks in your hand",
        "STR x Aura Particles in hand", "STR x other skills in hand", "STR x target's level", "STR x target's aura",
        "target's level / STR", "target's aura / STR", "STR x aura spent", "aura spent / STR", "STR x target's empty buttons",
        "STR x your empty buttons", "sum of STR in hand (same id)", "sum of STR in hand (same group)", "STR x random 0-1",
        "STR x random 0-2", "STR x random 0-3", "STR x a fighter count", "no STR"};
    return v >= 0 && v < 32 ? F[v] : "?";
}
static const char *sc_onhit(int v) {
    switch (v) {
    case 0: return "none"; case 1: return "aura to 0"; case 2: return "level - value"; case 3: return "aura x or / value";
    case 4: return "level / value"; case 5: return "explodes (blast radius)"; case 6: return "can't move"; case 7: return "buttons frozen";
    case 8: return "presses cost health"; case 9: return "drain health"; case 10: return "drain aura"; case 11: return "drain level";
    case 12: return "damage dealt + value"; case 13: return "move speed + value %"; case 14: return "homing + value %";
    case 15: return "aura recovery + value %"; case 16: return "same button blocked"; case 17: return "status reset";
    case 18: return "auto-capture"; case 37: return "controls inverted"; case 38: return "environment crystals erased";
    case 39: return "environment cancelled"; case 40: return "decoy (locks only on you)"; case 41: return "buttons scrambled";
    case 42: return "forced to use the button"; case 43: return "health + value"; case 44: return "level + value";
    case 45: return "teleported to your start"; case 46: return "health averaged"; case 47: return "level to the lowest";
    case 48: return "health - count x value"; case 49: return "level - value per aura particle"; case 50: return "made to capture yours";
    case 51: return "attributes reversed"; case 52: return "timed flag 0x2E";
    }
    return v >= 19 && v <= 36 ? "timed flag (no stock skill)" : "?";
}
static const char *sc_status(int v) {
    switch (v) {
    case 0: return "none"; case 1: return "level + value"; case 2: return "aura + value (99 = full)"; case 3: return "health + value";
    case 4: return "damage dealt + value"; case 5: return "move speed + value %"; case 6: return "homing + value %";
    case 7: return "aura recovery + value %"; case 8: return "status reset"; case 10: return "level x value";
    case 11: return "health back while still"; case 12: return "can't be locked on"; case 13: return "floats (value = height)";
    case 14: return "no damage taken"; case 15: return "no damage while still"; case 17: return "hear the target's team";
    case 18: return "copies the target's attributes"; case 19: return "captures your last used skill";
    case 20: return "captures a skill from your arsenal"; case 21: return "value used skills back"; case 22: return "aura particles out of your arsenal";
    }
    return "(no stock skill)";
}
static const char *sc_envprop(int v) {
    switch (v) {
    case 0: return "damage + value"; case 1: return "move speed + value %"; case 2: return "homing + value %";
    case 3: return "aura recovery + value %"; case 17: return "health - value per skill use"; case 18: return "health - value per capsule";
    case 19: return "health + value per aura particle"; case 20: return "every attack pierces"; case 21: return "no attack over STR value";
    case 22: return "no attack under STR value"; case 25: return "no Erase skills"; case 29: return "no aura back while moving";
    case 30: return "aura cost x2 (1) or / 2 (-1)"; case 31: return "attacks lose bonuses / penalties"; case 32: return "every skill single-use";
    case 34: return "capsules slower (+) / faster (-)"; case 35: return "controls inverted"; case 38: return "no skills costing value or more";
    case 39: return "no skills costing value or less"; case 41: return "no Psycho skills"; case 42: return "no Optical skills";
    case 43: return "no Nature skills"; case 44: return "no Ki skills"; case 45: return "no Faith skills";
    }
    return "(no stock skill)";
}
// a family field row: its offset, range, name, what it does; false = this family does not use it (the row is not shown)
static bool sc_ffield(int fam, int off, ScFld *fd, const char **name) {
    auto set = [&](int lo, int hi, const char *nm, const char *ds) { fd->off = off; fd->lo = lo; fd->hi = hi; fd->desc = ds; *name = nm; return true; };
    int v5c = sc_field(0x5C, NULL), v62 = sc_field(0x62, NULL);
    switch (fam) {
    case 0:                                                   // attack (FUN_140272400, 140272510, 140272bc0, 1402723b0, 14026c1c0)
        switch (off) {
        case 0x56: return set(0, 31, "STR formula", "How STR is worked out (0 = the Damage value as it is; table 0x1402dd0a0)");
        case 0x5A: return set(-9, 9, "STR per use", "Added to STR for each time it was used before");
        case 0x5C: return set(0, 9, "Erase from hand", "Takes skills out of the target's hand on hit (0 none; 9 every filled slot)");
        case 0x5E: if (!v5c) return false; return set(0, 4, "  erase order", "0 random, 1 / 2 sorted, 3 random skipping limited ones, 4 the same slot");
        case 0x60: if (!v5c) return false; return set(0, 4, "  how many", "How many skills it takes out");
        case 0x62: return set(0, 52, "Extra on hit", "A second effect on the target when it hits");
        case 0x64: if (!v62) return false;
                   return set(-999, 9999, "  extra value", v62 == 5 ? "Blast radius, x 0.1 units (300 = 30)" :
                              (v62 >= 6 && v62 <= 8) || v62 >= 19 ? "How long, in frames (30 a second; 300 = 10 s)" : "How much");
        case 0x66: if (v62 != 0x33) return false; return set(-999, 9999, "  extra value 2", "Second value for 'attributes reversed'");
        case 0x68: return set(0, 3, "Against shields", "1 goes on after breaking one, 2 passes most, 3 can't be bent or turned");
        case 0x6A: return set(0, 3, "Through walls", "0 or 1: the hit stops at walls; 2 or 3: no wall check (the game reads 1 as 0 and 3 as 2)");   // v5.36 FUN_14026c1c0: == 2 || == 3
        }
        return false;
    case 1:                                                   // erase (0x14026fcb0, 0x14026ff20, 0x140270150)
        if (off == 0x54) return set(0, 5, "Erases", "What it takes away");
        if (off == 0x56) return set(0, 9, "Then", "Particle Convert 3: capsules become aura particles; Change the World 2");
        return false;
    case 2:                                                   // defence (0x140264085, 0x140271760, 0x140271ed0, 0x140272400)
        switch (off) {
        case 0x54: return set(0, 7, "Defence type", "How it stops attacks");
        case 0x56: return set(0, 2000, "Barrier size", "Radius x 0.1 units (130 = 13 shields, 800 = 80 bends)");
        case 0x58: return set(2, 999, "Barrier time", "Frames it stands (+2): Block 18, Wall 23, Shell 28, Auto 10");
        case 0x5A: return set(0, 99, "DEF", "How much it blocks");
        case 0x5C: return set(-9, 9, "DEF per use", "Added for each time it was used before");
        case 0x5E: return set(0, 1, "Stops even if broken", "1: the attack ends even when the barrier breaks");
        case 0x60: return set(0, 1, "Reacts to melee only", "1: the reaction below only answers close-range hits");
        case 0x62: return set(0, 7, "Reaction", "What happens to the attacker when it blocks");
        case 0x64: { int r = sc_field(0x62, NULL); if (r < 4 || r > 6) return false; return set(0, 9999, "  reaction time", "Frames (30 a second; 90 = 3 s)"); }
        }
        return false;
    case 3:                                                   // movement (0x14020e020, 0x140270200)
        if (off == 0x54) return set(0, 8, "Move", "Which movement");
        if (off == 0x56) return set(-999, 9999, "Move value", "Speed or height (Jump 10, Skywalk 5)");
        if (off == 0x58) return set(0, 9999, "Move time", "Frames (Skywalk 450, Levitate 90)");
        return false;
    case 4: {                                                 // status (0x140270890 / 0x140270a00: up to 4 statuses)
        if (off == 0x54) return set(0, 2, "Who", "0 you, 1 the locked target, 2 every fighter");
        static const char *SN[4] = {"Status 1", "Status 2", "Status 3", "Status 4"};
        static const char *VN[4] = {"  status 1 value", "  status 2 value", "  status 3 value", "  status 4 value"};
        static const char *TN[4] = {"  status 1 time", "  status 2 time", "  status 3 time", "  status 4 time"};
        for (int k = 0; k < 4; k++) {
            int id = sc_field(0x56 + 2 * k, NULL);
            if (off == 0x56 + 2 * k) { if (k > 0 && !sc_field(0x56 + 2 * (k - 1), NULL)) return false; return set(0, 22, SN[k], "Which status (0 none)"); }
            if (off == 0x5E + 2 * k) { if (!id) return false; return set(-999, 9999, VN[k], "How much"); }
            if (off == 0x66 + 2 * k) { if (!id) return false; return set(0, 9999, TN[k], "Frames (30 a second; 450 = 15 s; 0 = until reset)"); }
        }
        return false;
    }
    case 5:                                                   // environment (0x140271660, 0x14024d4d0, 0x140210be0)
        if (off == 0x54) return set(0, 45, "Property", "What it changes for everyone");
        if (off == 0x56) return set(0, 9999, "Settles after", "Frames (450 in every stock one; 0x140210be0)");
        if (off == 0x58) return set(-999, 999, "Value", "How much (clamped per property, table 0x1402dcf30)");
        if (off == 0x5A) return set(0, 9999, "Lasts", "Frames (1800 = 1 min; 0 = until replaced)");
        return false;
    case 6:                                                   // copy (0x14015d980)
        if (off == 0x54) return set(0, 1, "Copies", "0 your own last skill, 1 the opponent's");
        return false;
    }
    return false;
}
static void sc_ffield_words(int fam, int off, int v, char *out, size_t n) {
    out[0] = 0;
    if (fam == 0) {
        if (off == 0x56) sprintf_s(out, n, "%s", sc_formula(v));
        else if (off == 0x5C) sprintf_s(out, n, "%s", v == 0 ? "none" : v <= 5 ? "picked slots" : v == 6 ? "slots with Aura Particles" :
                                                     v == 7 ? "slots with other skills" : v == 8 ? "non-attack / defence" : "every filled slot");
        else if (off == 0x62) sprintf_s(out, n, "%s", sc_onhit(v));
        else if (off == 0x68) sprintf_s(out, n, "%s", v == 1 ? "goes on after a break" : v == 2 ? "passes most" : v == 3 ? "can't be bent" : "normal");
        else if (off == 0x6A) sprintf_s(out, n, "%s", v == 0 ? "stops at walls" : v == 1 ? "stops at walls (same as 0)" : v == 2 ? "through walls" : "through walls (same as 2)");   // v5.36
    } else if (fam == 1 && off == 0x54) {
        static const char *E[6] = {"the capsule you touch", "the particle you touch", "every particle on the field", "the capsule (not aura)", "?", "every capsule on the field"};
        sprintf_s(out, n, "%s", v >= 0 && v <= 5 ? E[v] : "?");
    } else if (fam == 2) {
        static const char *D[8] = {"wall", "shell", "bend", "block", "reflect", "absorb", "turn around", "auto"};
        static const char *R[8] = {"none", "health from damage", "aura from cost", "erase the attack", "attacker can't move",
                                   "attacker's buttons frozen", "attacker's presses hurt", "capture the skill"};
        if (off == 0x54) sprintf_s(out, n, "%s", v >= 0 && v <= 7 ? D[v] : "?");
        else if (off == 0x62) sprintf_s(out, n, "%s", v >= 0 && v <= 7 ? R[v] : "?");
        else if (off == 0x5E || off == 0x60) sprintf_s(out, n, "%s", v ? "yes" : "no");
    } else if (fam == 3 && off == 0x54) {
        static const char *M[9] = {"jump", "?", "retreat", "?", "call", "float", "return", "charge", "dash"};
        sprintf_s(out, n, "%s", v >= 0 && v <= 8 ? M[v] : "?");
    } else if (fam == 4) {
        if (off == 0x54) sprintf_s(out, n, "%s", v == 1 ? "the locked target" : v == 2 ? "every fighter" : "you");
        else if (off >= 0x56 && off <= 0x5C) sprintf_s(out, n, "%s", sc_status(v));
    } else if (fam == 5 && off == 0x54) sprintf_s(out, n, "%s", sc_envprop(v));
    else if (fam == 6 && off == 0x54) sprintf_s(out, n, "%s", v ? "the opponent's last" : "your own last");
}
static void sc_field_words(int off, int v, char *out, size_t n) {   // the value in words
    out[0] = 0;
    switch (off) {
    case 0x0E: sprintf_s(out, n, "%s", v >= 0 && v <= 6 ? SC_CAPS[v] : "?"); break;
    case 0x30: if (v == 0) sprintf_s(out, n, "unlimited"); else sprintf_s(out, n, "%d use%s", v, v == 1 ? "" : "s"); break;
    case 0x2A: if (v < 0) sprintf_s(out, n, "costs all your aura"); else if (v == 0) sprintf_s(out, n, "no change");
               else sprintf_s(out, n, "needs level %d, lowers it by %d", v, v); break;
    case 0x2C: if (v == 0) sprintf_s(out, n, "stays the same"); else sprintf_s(out, n, "%s by %d each use", v > 0 ? "rises" : "falls", v > 0 ? v : -v); break;
    case 0x2E: if (v == 0) sprintf_s(out, n, "none"); else if (v == -1) sprintf_s(out, n, "takes half your health");
               else if (v < 0) sprintf_s(out, n, "gives %d health", -v); else sprintf_s(out, n, "takes %d health", v); break;
    case 0x4C: { static const char *GA[4] = {"on the ground", "in the air", "ground and air", "ground and air (extra rule)"}; sprintf_s(out, n, "%s", GA[v & 3]); break; }
    case 0x7A: sprintf_s(out, n, "%s", v >= 0 && v <= 20 ? SC_KINDS[v] : "?"); break;
    case 0x52: sprintf_s(out, n, "%s", v >= 0 && v <= 6 ? SC_FAMILY[v] : "?"); break;
    case 0x48: { static const char *Q[10] = {"nothing", "health <= amount", "health >= amount", "aura <= amount", "aura >= amount",
                 "skills left <= amount", "skills left >= amount", "aura full", "level <= amount", "level >= amount"};
                 sprintf_s(out, n, "%s", v >= 0 && v <= 9 ? Q[v] : "?"); break; }
    }
}
// the field a row id stands for (ids 16..32: SC_FLD; 37 family; 38..50 the family's +0x54..+0x6C; 51 / 52 the requirement)
static bool sc_fdef(int id, ScFld *fd, const char **name) {
    if (id >= 16 && id <= 32) {
        *fd = SC_FLD[id - 16];
        *name = SC_NAMES[id];
        if (id == 22) fd->desc = "How it travels and where it hits (the game's 21 attack kinds)";
        if (id >= 23) { ScKv kv = sc_kv(sc_field(0x7A, NULL), id - 23); if (!kv.name) return false; *name = kv.name; fd->desc = kv.desc; }
        return true;
    }
    if (id == 37) { *fd = {0x52, 0, 6, "Its family: how the game treats it (changing it changes every field below)"}; *name = SC_NAMES[37]; return true; }
    if (id >= 38 && id <= 50) return sc_ffield(sc_field(0x52, NULL), 0x54 + 2 * (id - 38), fd, name);
    if (id == 51) { *fd = {0x48, 0, 9, "What it needs before it can be used (0x140262470)"}; *name = SC_NAMES[51]; return true; }
    if (id == 52) { if (!sc_field(0x48, NULL)) return false; *fd = {0x4A, -99, 999, "The amount for Needs"}; *name = SC_NAMES[52]; return true; }
    return false;
}
// the rows shown now, in order
static int sc_order(int *ids) {
    int n = 0, fam = sc_field(0x52, NULL);
    auto add = [&](int id) { if (n < SC_ROWS) ids[n++] = id; };
    auto addf = [&](int id) { ScFld fd; const char *nm; if (sc_fdef(id, &fd, &nm)) add(id); };
    add(0); add(54); add(55); add(1); add(56); add(2); add(3);  // v5.112 the finder under Skill; v5.160 closest skill
    if (fam == 0) add(4);                                     // Damage = STR (+0x58): attacks only
    add(5);
    if (g_sc.fx > 0) add(34);
    add(6); add(35); add(7);
    if (fam == 0) add(8);
    add(9); add(10); add(11); add(36);
    for (int id = 16; id <= 21; id++) add(id);
    addf(51); addf(52);
    add(37);
    for (int id = 38; id <= 50; id++) addf(id);
    if (fam == 0) { add(22); for (int id = 23; id <= 32; id++) addf(id); }
    add(33); add(12); add(13); add(14); add(15);
    return n;
}
static const char *sc_name(int row) {                          // the row's name on screen
    int id = sc_id(row);
    ScFld fd;
    const char *nm;
    if (((id >= 16 && id <= 32) || (id >= 37 && id <= 52)) && sc_fdef(id, &fd, &nm)) return nm;
    return SC_NAMES[id];
}

static ULONGLONG g_sc_held_ms;                               // v5.32 how long L / R has been held (cwo_forge.inl's repeat)
static void sc_turn_value(int row, int dir) {
    ScFld fd;
    const char *fnm;
    if (((row >= 16 && row <= 32) || (row >= 37 && row <= 52)) && sc_fdef(row, &fd, &fnm)) { sc_field_turn(fd, dir); return; }
    switch (row) {
    case 0: {                                                // which skill: new, or one of the saved new skills
        int k = g_sc_k;
        for (int tries = 0; tries <= g_ns_n + 1; tries++) {      // v5.186 the pack's own / community skills are skipped (BFR)
            k += dir;
            if (k < -1) k = g_ns_n - 1;
            if (k >= g_ns_n) k = -1;
            if (k < 0 || !ns_locked(g_ns[k])) break;
        }
        if (k < 0) sc_draft_new(g_sc.src > 0 ? g_sc.src : 61); else { g_sc = g_ns[k]; g_sc_k = k; }
        return;
    }
    case 1: { int v = sc_cycle(g_sc.src, dir, false); if (v > 0) g_sc.src = v; return; }
    case 2: g_sc.school = sc_step(g_sc.school < 1 ? 0 : g_sc.school, dir, 1, 5); if (g_sc.school < 1) g_sc.school = -1; return;
    case 3: g_sc.cost = sc_step(g_sc.cost, dir, 0, 10); return;
    case 4: g_sc.damage = sc_step(g_sc.damage, dir, 0, 99); return;
    case 5: g_sc.fx = sc_cycle(g_sc.fx, dir, true); return;
    case 6: g_sc.snd = sc_cycle(g_sc.snd, dir, true); return;
    case 7: g_sc.anim = sc_cycle(g_sc.anim, dir, true); return;
    case 35: g_sc.voice = ((g_sc.voice + dir) % 8 + 8) % 8; return;   // v5.30 own, 1-6, none
    case 54: g_sc_fcat = ((g_sc_fcat + dir) % 12 + 12) % 12; return;   // v5.112 the finder
    case 55: g_sc_flet = ((g_sc_flet + dir) % 27 + 27) % 27; return;
    case 8: g_sc.trail = sc_cycle(g_sc.trail > 0 ? g_sc.trail : -1, dir, true); if (g_sc.trail < 0) g_sc.trail = 0; return;
    case 9: {
        // v5.32 (owner):
        // every degree, 1 a press, 5 a step after L / R has been held 1.5 s; the colour is laid on as the effect loads
        // (cwo_fxcolour.inl), so the old 30-degree palette copy is set back to the source's own colour
        int step = g_sc_held_ms > 1500 ? 5 : 1;
        g_sc.hue = ((g_sc.hue + step * dir) % 360 + 360) % 360;
        for (int m = 0; m < g_sc.nmap; m++) if (g_sc.pal > 0 && g_sc.from[m] == g_sc.pal_req) g_sc.to[m] = g_sc.pal;
        sc_preview();
        return;
    }
    case 36: g_sc.cmode = ((g_sc.cmode + dir) % 3 + 3) % 3; sc_preview(); return;   // v5.32 Auto / Turn / Paint
    case 10: { int v = (g_sc.sat > 1 ? g_sc.sat : g_sc.sat == 1 ? 0 : 100) + 5 * dir; g_sc.sat = v < 0 ? 0 : v > 200 ? 200 : v; if (g_sc.sat == 0) g_sc.sat = 1; else if (g_sc.sat == 100) g_sc.sat = 0; sc_preview(); return; }
    case 11: { int v = (g_sc.val ? g_sc.val : 100) + 5 * dir; g_sc.val = v < 30 ? 30 : v > 200 ? 200 : v; sc_preview(); return; }
    case 14: g_sc_hand = ((g_sc_hand + dir) % 5 + 5) % 5; return;   // v5.33 slot 1-4 or all slots
    case 34: g_sc.fxmode = !g_sc.fxmode; return;           // v4.7
    }
}

// ---- v4.2 the Skill Creator live (owner 2026-10-25): every change is written into the game at once (the same writes as Save,
// ns_apply_one, without the file); a new skill gets its free number at its first change and is marked unsaved. Leaving
// without saving puts everything back: an unsaved skill's places are emptied again (record, profiles, text, the deck
// limit, the id maps and the stock it was given), a saved skill gets the values it had when the test began.
static int ns_free_id();
static void sc_list_update(const NewSkill &n);
static NewSkill g_sc_snap;                                   // the skill as it was when the test began (or it was chosen)
static int g_sc_snap_k = -1;
static volatile LONG g_scp_restart;                          // the preview starts again with the new values
static ULONGLONG g_sc_tint_at;                               // the colour is laid on again once the new effect is loaded
struct ScUndo { int id; short rec0; unsigned lim; int first, last; unsigned char stock; short sup[2]; bool have_stock; };
static ScUndo g_sc_undo = {-1};

static void sc_snapshot() { g_sc_snap = g_sc; g_sc_snap_k = g_sc_k; }

static int g_sc_pend_k = -1;                                // v5.32 an unsaved skill kept until the match is over (below)
static void sc_live() {
    if (g_sc_k < 0) {                                         // first change of a new skill: its number now, unsaved
        if (g_ns_n >= NS_MAX) { set_status("Skill Creator: 100 new skills is the limit"); return; }
        if (g_sc_pend_k >= 0 && g_sc_pend_k == g_ns_n - 1 && g_ns[g_sc_pend_k].unsaved) {   // v5.32 the kept one's number again
            int k = g_sc_pend_k;
            g_sc_pend_k = -1;
            g_sc.id = g_ns[k].id;
            g_sc.unsaved = 1;
            sprintf_s(g_sc.name, "New skill (not saved)");
            sprintf_s(g_sc.desc, "Being made in the Skill Creator from %.30s.", sc_skill_name(g_sc.src));
            g_sc_k = k;
            g_ns[k] = g_sc;
            g_ns[k].dirty = 1;
            sc_list_update(g_sc);
            g_ns_said[k] = 0;
            g_ns_next = 0;
            InterlockedExchange(&g_scp_restart, 1);
            g_sc_tint_at = GetTickCount64() + 700;
            logf("Skill Creator: live skill %d (unsaved) from %d - the kept number again", g_sc.id, g_sc.src);
            return;
        }
        int id = ns_free_id();
        if (id < 0) { set_status("Skill Creator: no free skill number"); return; }
        g_sc_undo = {-1};
        __try {                                              // what its places held, for leaving without saving
            char *gs = *(char **)(g_base + RVA_GSDATA);
            if (gs && *(unsigned *)gs == 0x44004) {
                g_sc_undo.id = id;
                g_sc_undo.rec0 = *(short *)(gs + 0x18 + NS_REC * id);
                g_sc_undo.lim = *(unsigned *)(gs + 0x14);
                g_sc_undo.first = ((int *)(g_base + RVA_FIRST_MAP))[id];
                g_sc_undo.last = ((int *)(g_base + RVA_LAST_MAP))[id];
                ULONG_PTR mgr = *(ULONG_PTR *)(g_base + RVA_ARSENAL);
                ULONG_PTR side = mgr ? *(ULONG_PTR *)(mgr + 0x33A0) : 0;
                if (side) {
                    g_sc_undo.stock = *(unsigned char *)(side + 0x644 + id);
                    memcpy(g_sc_undo.sup, (void *)(side + 0xA2C + 4 * id), 4);
                    g_sc_undo.have_stock = true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { g_sc_undo.id = -1; }
        g_sc.id = id;
        g_sc.unsaved = 1;
        sprintf_s(g_sc.name, "New skill (not saved)");
        sprintf_s(g_sc.desc, "Being made in the Skill Creator from %.30s.", sc_skill_name(g_sc.src));
        g_sc_k = g_ns_n;
        g_ns[g_ns_n++] = g_sc;
        sc_list_update(g_sc);
        logf("Skill Creator: live skill %d (unsaved) from %d", id, g_sc.src);
    } else {
        g_sc.dirty = 1;
        g_ns[g_sc_k] = g_sc;
    }
    g_ns_said[g_sc_k] = 0;
    g_ns_next = 0;                                           // written within the next frame
    InterlockedExchange(&g_scp_restart, 1);
    g_sc_tint_at = GetTickCount64() + 700;
}

static void sc_tint_restore();
// v5.32 (owner 2026-10-27; the log: the unsaved
// skill 380 was taken out - record, profiles and text zeroed - while it was still in hand slot 1 and in a capsule, and the
// game crashed 28 s later at exe 1402937e9 reading address -1 in the effect / model unload FUN_1402934e0): in a match an
// unsaved skill is only marked; it leaves the game once the match is over (sc_pending_step), and a new draft made in the
// meantime takes its number back, so the unsaved skill is always the last one
static void ns_discard_k(int k) {                            // the unsaved skill k (always the last one) leaves the game
    if (k < 0 || k != g_ns_n - 1 || !g_ns[k].unsaved) return;
    int id = g_ns[k].id, row = NS_ROW0 + k;
    __try {
        char *gs = *(char **)(g_base + RVA_GSDATA);
        if (gs && *(unsigned *)gs == 0x44004) {
            char *rec = gs + 0x18 + NS_REC * id;
            if (*(short *)rec == (short)row && *(short *)(rec + 2) == (short)id) {
                memset(rec + 2, 0, NS_REC - 2);
                for (int j = 0; j < 6; j++) memset(gs + NS_PROF_BASE + NS_PROF * (NS_PROF0 + 6 * k + j), 0, NS_PROF);
                memset(gs + NS_STR0 + NS_STR * k, 0, NS_STR);
                memset(gs + NS_TEXT + 12 * row, 0, 12);
                if (g_sc_undo.id == id) {
                    *(short *)rec = g_sc_undo.rec0;
                    *(unsigned *)(gs + 0x14) = g_sc_undo.lim;
                }
            }
        }
        if (g_sc_undo.id == id) {
            ((int *)(g_base + RVA_FIRST_MAP))[id] = g_sc_undo.first;
            ((int *)(g_base + RVA_LAST_MAP))[id] = g_sc_undo.last;
            ULONG_PTR mgr = *(ULONG_PTR *)(g_base + RVA_ARSENAL);
            ULONG_PTR side = mgr ? *(ULONG_PTR *)(mgr + 0x33A0) : 0;
            if (side && g_sc_undo.have_stock) {
                *(unsigned char *)(side + 0x644 + id) = g_sc_undo.stock;
                memcpy((void *)(side + 0xA2C + 4 * id), g_sc_undo.sup, 4);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    for (int i = 0; i < g_sk_count; i++)
        if (g_sk[i].id == id) { for (int j = i; j + 1 < g_sk_count; j++) g_sk[j] = g_sk[j + 1]; g_sk_count--; break; }
    g_ns_n--;
    g_sc_undo = {-1};
    logf("Skill Creator: unsaved skill %d taken out again", id);
}
static void sc_discard_unsaved() {                           // the unsaved skill (always the last one) leaves the game
    if (!g_sc.unsaved || g_sc_k != g_ns_n - 1) return;
    int src = g_sc.src;
    sc_tint_restore();
    if (g_in_match) {
        g_sc_pend_k = g_sc_k;
        logf("Skill Creator: unsaved skill %d stays in the game until the match is over (it may be in a hand or a capsule)", g_sc.id);
    } else {
        ns_discard_k(g_sc_k);
    }
    sc_draft_new(src);
}
static void sc_pending_step() {                              // game thread: the match is over - the kept unsaved skill goes
    if (g_sc_pend_k < 0 || g_in_match) return;
    int k = g_sc_pend_k;
    g_sc_pend_k = -1;
    if (k == g_sc_k) return;                                 // it became the draft again
    ns_discard_k(k);
}

static void sc_revert() {                                    // leaving without saving: back to the test's start
    sc_discard_unsaved();
    sc_tint_restore();
    if (g_sc_snap_k >= 0 && g_sc_snap_k < g_ns_n && g_ns[g_sc_snap_k].id == g_sc_snap.id) {
        g_ns[g_sc_snap_k] = g_sc_snap;
        g_ns[g_sc_snap_k].dirty = 1;
        g_ns_said[g_sc_snap_k] = 0;
        g_ns_next = 0;
        ns_save();                                           // a palette colour is kept at once: put the file back too
    }
    g_sc = g_sc_snap; g_sc_k = g_sc_snap_k;
}

static void sc_save_now() {                                  // Save (row 12 and the pause menu)
    char msg[96];
    if (g_sc_k >= 0 && g_sc_k < g_ns_n && ns_locked(g_ns[g_sc_k])) {   // v5.186 never a changed copy of the pack's skill
        set_status("This skill comes with the pack - make your own from it (Based on)");
        return;
    }
    bool made = g_sc_k < 0 || g_sc.unsaved;
    if (g_sc_k < 0) sc_live();
    if (g_sc_k < 0) return;
    if (made) {
        int same = 0;
        for (int k = 0; k < g_ns_n; k++) same += k != g_sc_k && g_ns[k].src == g_sc.src;
        sprintf_s(g_sc.name, "%.16s %s", sc_skill_name(g_sc.src), same ? "Custom+" : "Custom");
        sprintf_s(g_sc.desc, "Made in the Skill Creator from %.30s.", sc_skill_name(g_sc.src));
        g_sc.unsaved = 0;
        g_sc_undo = {-1};
    }
    g_sc.dirty = 1;
    g_ns[g_sc_k] = g_sc;
    g_ns_said[g_sc_k] = 0;
    g_ns_next = 0;                                           // written within the next frame
    ns_save();
    sc_list_update(g_sc);
    sc_snapshot();                                           // leaving without saving now comes back to this
    sprintf_s(msg, "Saved skill %d '%.30s' (rename it in CwoNewSkills.txt)", g_sc.id, g_sc.name);
    set_status(msg);
    logf("Skill Creator: %s skill %d from %d (school %d cost %d damage %d fx %d snd %d anim %d trail %d hue %d)", made ? "made" : "changed",
         g_sc.id, g_sc.src, g_sc.school, g_sc.cost, g_sc.damage, g_sc.fx, g_sc.snd, g_sc.anim, g_sc.trail, g_sc.hue);
    rumble(2);
}

// v4.4 undo / redo (owner): every change keeps the values before it (32 steps)
static NewSkill g_sc_undo_st[32], g_sc_redo_st[32];
static int g_sc_undo_n, g_sc_redo_n;
static void sc_hist_push() {
    if (g_sc_undo_n == 32) { memmove(g_sc_undo_st, g_sc_undo_st + 1, sizeof(NewSkill) * 31); g_sc_undo_n = 31; }
    g_sc_undo_st[g_sc_undo_n++] = g_sc;
    g_sc_redo_n = 0;
}
static void sc_hist_take(const NewSkill &from) {              // the values of 'from', this skill's identity kept
    NewSkill cur = g_sc;
    g_sc = from;
    g_sc.id = cur.id; g_sc.unsaved = cur.unsaved; g_sc.pal = cur.pal; g_sc.pal_req = cur.pal_req;
    memcpy(g_sc.name, cur.name, sizeof(g_sc.name)); memcpy(g_sc.desc, cur.desc, sizeof(g_sc.desc));
    sc_live();
}
static void sc_undo() {
    if (!g_sc_undo_n) { set_status("Nothing to undo"); return; }
    if (g_sc_redo_n < 32) g_sc_redo_st[g_sc_redo_n++] = g_sc;
    sc_hist_take(g_sc_undo_st[--g_sc_undo_n]);
    set_status("Undone");
}
static void sc_redo() {
    if (!g_sc_redo_n) { set_status("Nothing to redo"); return; }
    if (g_sc_undo_n < 32) g_sc_undo_st[g_sc_undo_n++] = g_sc;
    sc_hist_take(g_sc_redo_st[--g_sc_redo_n]);
    set_status("Redone");
}
static void sc_reset() {                                     // back to the base skill (its identity kept)
    sc_hist_push();
    g_sc.school = g_sc.cost = g_sc.damage = g_sc.fx = g_sc.snd = g_sc.anim = -1;
    g_sc.hue = g_sc.sat = g_sc.val = g_sc.trail = 0;
    g_sc.nfo = 0;
    g_sc.fxmode = 0;
    g_sc.cmode = 0;
    for (int m = 0; m < g_sc.nmap; m++) if (g_sc.pal > 0 && g_sc.from[m] == g_sc.pal_req) g_sc.to[m] = g_sc.pal;
    sc_tint_restore();
    sc_live();
    set_status("Reset to the base skill (X: undo)");
}

static void sc_turn_id(int row, int dir) {
    if (row == 0 && g_sc.unsaved) { sc_discard_unsaved(); g_sc_k = -1; }   // v4.2: another skill: the unsaved one goes
    bool change = (row >= 1 && row <= 11) || (row >= 16 && row <= 32) || (row >= 34 && row <= 52);
    if (change) sc_hist_push();
    sc_turn_value(row, dir);
    if (row == 0) { sc_snapshot(); g_sc_undo_n = g_sc_redo_n = 0; InterlockedExchange(&g_scp_restart, 1); g_sc_tint_at = GetTickCount64() + 700; }
    if (change) sc_live();                                   // v4.2: every value shows at once
    int rows = sc_rows_n();                                  // v5.32 the rows follow the skill: the cursor stays on one
    if (g_fmenu_sel >= rows) InterlockedExchange(&g_fmenu_sel, rows - 1);
}
static void sc_turn(int row, int dir) { sc_turn_id(sc_id(row), dir); }   // v4.4: row on screen -> its id

// v5.160 (owner 2026-11-13):
// the skill being made, compared with every skill the game has now, by what it does and how it looks - the same tiers as
// 07_Combat_and_Skills/skill_checks_2026-11-13/skill_similarity.py, where the original game is the yardstick (none of its
// 374 skills is a DUPLICATE or NEAR COPY of another). Behaviour = every record field except text row / ids / uses / profiles /
// combo link, school, cost and STR; same behaviour = same capsule type, family and attack kind and >= 97% of those fields;
// look = the ground profile's effect (a loader recolour counts as its source effect).
static const ULONG_PTR SC_RVA_RECS = 0x4bdc50, SC_RVA_PROFS = 0x4bcc98;
static int sc_look_of(const char *recs, const char *profs, int id) {
    short gp = *(const short *)(recs + 0x90 * (size_t)id + 0x14);
    if (gp <= 0 || gp >= 2000) return 0;
    int fx = *(const short *)(profs + 0x72 * (size_t)gp + 4);
    const FxMapEnt *m = fx >= FX_REQ0 ? fx_map_of(fx) : NULL;
    return m ? m->src : fx;
}
static int sc_similar(char *out, size_t n) {               // 3 DUPLICATE, 2 NEAR COPY, 1 SAME MECHANIC, 0 unique, -1 unknown
    out[0] = 0;
    __try {
        const char *recs = *(const char **)(g_base + SC_RVA_RECS), *profs = *(const char **)(g_base + SC_RVA_PROFS);
        char *gs = *(char **)(g_base + RVA_GSDATA);
        if (!recs || !profs || !gs || g_sc.src <= 0 || g_sc.src >= 1200) return -1;
        char me[NS_REC];                                      // the draft: its base (the game's original when <= 752) + changes
        memcpy(me, g_sc.src <= 752 ? gs + 0x18 + NS_REC * g_sc.src : recs + NS_REC * g_sc.src, NS_REC);
        if (g_sc.fx > 0 && g_sc.fxmode) memcpy(me + 0x50, recs + NS_REC * g_sc.fx + 0x50, NS_REC - 0x50);
        if (g_sc.school > 0) *(short *)(me + 0x12) = (short)g_sc.school;
        if (g_sc.cost >= 0) *(short *)(me + 0x28) = (short)g_sc.cost;
        if (g_sc.damage >= 0) *(short *)(me + 0x58) = (short)g_sc.damage;
        for (int i = 0; i < g_sc.nfo && i < 24; i++) if (g_sc.fo[i] >= 0x0E && g_sc.fo[i] < 0x90) *(short *)(me + g_sc.fo[i]) = (short)g_sc.fv[i];
        int look = sc_look_of(recs, profs, g_sc.fx > 0 ? g_sc.fx : g_sc.src);
        static const int SKIP[] = {0x00, 0x02, 0x04, 0x06, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x28, 0x32, 0x58};
        auto skip = [&](int o) { for (int s2 : SKIP) if (s2 == o) return true; return false; };
        int best = 0, who = -1;
        for (int id = 1; id < 1200; id++) {
            if (id == g_sc.id || id == 750 || id == 751) continue;
            const char *r = recs + NS_REC * id;
            if (*(const short *)(r + 2) == 0) continue;
            if (*(const short *)(r + 0x0E) != *(const short *)(me + 0x0E) || *(const short *)(r + 0x52) != *(const short *)(me + 0x52) ||
                *(const short *)(r + 0x7A) != *(const short *)(me + 0x7A)) continue;
            int eq = 0, all = 0;
            for (int o = 0x08; o < 0x90; o += 2) { if (skip(o)) continue; all++; if (*(const short *)(r + o) == *(const short *)(me + o)) eq++; }
            if (eq * 100 < all * 97) continue;
            bool same_look = look > 0 && sc_look_of(recs, profs, id) == look;
            int tier = !same_look ? 1 : (*(const short *)(r + 0x28) == *(const short *)(me + 0x28) && *(const short *)(r + 0x58) == *(const short *)(me + 0x58)) ? 3 : 2;
            if (tier > best) { best = tier; who = id; }
        }
        static const char *T[4] = {"unique", "same move, new look", "near copy of", "DUPLICATE of"};
        if (best == 0) sprintf_s(out, n, "unique - no skill like it");
        else if (best == 1) sprintf_s(out, n, "%s %.20s", T[1], sc_skill_name(who));
        else sprintf_s(out, n, "%s %.20s", T[best], sc_skill_name(who));
        return best;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

static void sc_row(int r, char *val, size_t n, const char **desc) {
    static const char *SCH[6] = {"?", "Psycho", "Optical", "Nature", "Ki", "Faith"};
    val[0] = 0;
    r = sc_id(r);                                             // v4.4: row on screen -> its id
    ScFld fd;
    const char *fnm;
    if (((r >= 16 && r <= 32) || (r >= 37 && r <= 52)) && sc_fdef(r, &fd, &fnm)) {
        bool set = false;
        int v = sc_field(fd.off, &set);
        char words[48];
        if (r >= 38 && r <= 50) sc_ffield_words(sc_field(0x52, NULL), fd.off, v, words, sizeof(words));
        else sc_field_words(fd.off, v, words, sizeof(words));
        if (words[0]) sprintf_s(val, n, set ? "%d  %s" : "%d  %s (base)", v, words);
        else sprintf_s(val, n, set ? "%d" : "%d (base)", v);
        *desc = fd.desc;
        if (r == 22) { int k = sc_field(0x7A, NULL); if (k >= 0 && k <= 20) *desc = SC_KIND_DESC[k]; }
        return;
    }
    if (r == 33) { *desc = "Every value back to the base skill's (X undoes it)"; return; }
    if (r == 34) {                                            // v5.32 (owner): the flight is the base skill's unless asked
        sprintf_s(val, n, "%s", g_sc.fxmode ? "the effect's skill" : "the base skill");
        *desc = g_sc.fxmode ? "Flies, falls or spreads like Effect from's skill; your damage and cost stay"
                            : "Effect from's look, placed where the base skill's own flight puts it";
        return;
    }
    if (r == 36) {                                            // v5.32 colour mode
        static const char *M[3] = {"Auto", "Turn", "Paint"};
        sprintf_s(val, n, "%s", M[g_sc.cmode >= 0 && g_sc.cmode <= 2 ? g_sc.cmode : 0]);
        *desc = g_sc.cmode == 1 ? "Every color turns by the hue; white and gray stay" :
                g_sc.cmode == 2 ? "Every color takes the hue (white and gray too)" :
                                  "Colors turn by the hue; white and gray parts take it";
        return;
    }
    switch (r) {
    case 0: if (g_sc_k < 0) sprintf_s(val, n, "NEW (not saved yet)");
            else if (g_sc.unsaved) sprintf_s(val, n, "%d NEW - live, not saved", g_sc.id);
            else sprintf_s(val, n, "%d %.24s", g_sc.id, g_sc.name);
            *desc = "L/R: a new skill, or one you made before"; break;
    case 1: sprintf_s(val, n, "%.26s", sc_skill_name(g_sc.src)); *desc = "L/R: the skill it starts from (kind, timing, everything)"; break;
    case 54: sprintf_s(val, n, "%s", SC_FCAT[g_sc_fcat]); *desc = "L/R: the skill rows below show only this school or kind"; break;
    case 56: {                                               // v5.160 the similarity check
        int t = sc_similar(val, n);
        *desc = t >= 2 ? "Too close: it plays and looks like that skill - change its kind, effect or values to make it new"
              : t == 1 ? "Same move as that skill with its own look - fine; a new kind or values makes it more unique"
              : t == 0 ? "No skill plays like this one - a new skill"
                       : "Shows how close this skill is to every other skill";
        if (t < 0) sprintf_s(val, n, "-");
        break;
    }
    case 55: if (g_sc_flet) sprintf_s(val, n, "%c", 'A' + g_sc_flet - 1); else sprintf_s(val, n, "any letter");
             *desc = "L/R: the skill rows below show only names starting with this letter"; break;
    case 2: sprintf_s(val, n, "%s", g_sc.school > 0 ? SCH[g_sc.school] : "as the base skill"); *desc = "L/R: Psycho, Optical, Nature, Ki, Faith"; break;
    case 3: if (g_sc.cost >= 0) sprintf_s(val, n, "%d", g_sc.cost); else sprintf_s(val, n, "as the base skill"); *desc = "L/R: aura it costs"; break;
    case 4: if (g_sc.damage >= 0) sprintf_s(val, n, "%d", g_sc.damage); else sprintf_s(val, n, "as the base skill"); *desc = "L/R: damage per hit"; break;
    case 5: sprintf_s(val, n, "%.26s", g_sc.fx > 0 ? sc_skill_name(g_sc.fx) : "its own");
            *desc = "L/R: look like another skill (its effect, every part: hand, flight, impact ...)"; break;
    case 6: sprintf_s(val, n, "%.26s", g_sc.snd > 0 ? sc_skill_name(g_sc.snd) : "its own"); *desc = "L/R: sound like another skill"; break;
    case 7: sprintf_s(val, n, "%.26s", g_sc.anim > 0 ? sc_skill_name(g_sc.anim) : "its own"); *desc = "L/R: move like another skill (character animation)"; break;
    case 35: {                                                // v5.30 the voice line the move starts with
        static const char *V[8] = {"its own", "quick attack shout", "chant / support call", "power attack shout",
                                   "unheard lines 40-42", "unheard lines 50-52", "unheard lines 60-62", "no voice"};
        sprintf_s(val, n, "%s", V[g_sc.voice >= 0 && g_sc.voice <= 7 ? g_sc.voice : 0]);
        *desc = "L/R: what the fighter says as the move starts (each in their own voice)"; break;
    }
    case 8: sprintf_s(val, n, "%.26s", g_sc.trail > 0 ? sc_skill_name(g_sc.trail) : "none");
            *desc = g_sc.trail > 0 ? "Its flying effect rides on every projectile (the preview shot shows it)"
                                   : "L/R: another skill's flying effect rides on its projectiles"; break;
    case 9: if (g_sc.hue || g_sc.cmode == 2) sprintf_s(val, n, "%d deg", g_sc.hue); else sprintf_s(val, n, "as it is");
            *desc = "L/R: every degree (hold: faster); its next use shows it - textures, sparks, all"; break;
    case 10: sprintf_s(val, n, "%d %%", g_sc.sat > 1 ? g_sc.sat : g_sc.sat == 1 ? 0 : 100); *desc = "L/R: 0 % = gray, 200 % = vivid"; break;
    case 11: sprintf_s(val, n, "%d %%", g_sc.val ? g_sc.val : 100); *desc = "L/R: darker or brighter"; break;
    case 12: *desc = g_sc.unsaved ? "Every change is already live; this keeps it (CwoNewSkills.txt)" : "Saves to CwoNewSkills.txt"; break;
    case 13: *desc = "A capsule of it in front of you (or in a capsule already lying there)"; break;
    case 14:                                                 // v5.33 (owner)
        if (g_sc_hand == 4) sprintf_s(val, n, "all slots"); else sprintf_s(val, n, "slot %d", g_sc_hand + 1);
        *desc = "L/R: one hand slot or all of them; A: the skill goes there and stays (use it as often as you like)"; break;
    default: *desc = ""; break;
    }
}

static bool fu_uses(int id);                                // v5.17 cwo_fusion.inl: a fusion's number is not free
// v5.109 (owner 2026-11-07): a new skill took 376, a Battle for Reality pack record (the pack writes 375-465
// back every second while it is on), so the test showed the pack's skill. Now skipped: every record the BFR list names
// (on or off), and any record that already holds a skill in the game's table (as the Skill Fuser's search does).
static bool bfr_uses(int id);                                    // cwo_bfr.inl
// v5.111 (owner 2026-11-07): new
// skills start at 467 (the Battle for Reality pack uses records 11-465; its locked boss skills 700+; the Skill Creator never
// goes past 699)
static const int NS_FIRST_FREE = 467;
static int ns_free_id() {
    char *gs = NULL;
    __try { gs = g_ns_test ? g_ns_test : *(char **)(g_base + RVA_GSDATA); } __except (EXCEPTION_EXECUTE_HANDLER) { gs = NULL; }
    for (int id = NS_FIRST_FREE; id < 700; id++) {
        bool used = fu_uses(id) || bfr_uses(id) || ns_comm_range(id);   // v5.186 600-639: the pack's community skills
        for (int k = 0; k < g_ns_n; k++) used |= g_ns[k].id == id;
        if (!used && gs && *(unsigned *)gs == 0x44004 && !ns_zero(gs + 0x18 + NS_REC * id + 2, NS_REC - 2)) used = true;
        if (!used) return id;
    }
    return -1;
}

static void sc_list_update(const NewSkill &n) {             // the capsule / showcase list knows it
    static const char *SCHOOL[6] = {"?", "Psycho", "Optical", "Nature", "Ki", "Faith"};
    int at = -1;
    for (int i = 0; i < g_sk_count; i++) if (g_sk[i].id == n.id) at = i;
    if (at < 0 && g_sk_count < 800) at = g_sk_count++;
    if (at < 0) return;
    g_sk[at].id = n.id;
    sprintf_s(g_sk[at].name, "%.31s", n.name);
    strcpy_s(g_sk[at].school, n.school > 0 ? SCHOOL[n.school] : "as src");
    if (n.cost >= 0) sprintf_s(g_sk[at].cost, "%d", n.cost); else strcpy_s(g_sk[at].cost, "-");
    InterlockedExchange(&g_sk_sel, at);
}

static void sc_put_in_hand(int skill, char *msg, size_t n);  // v5.33 below
static void sc_choose(int row) {
    char msg[96];
    row = sc_id(row);                                         // v4.4: row on screen -> its id
    if (row == 33) { sc_reset(); return; }
    if ((row >= 16 && row <= 32) || (row >= 36 && row <= 52)) { sc_turn_id(row, 1); return; }
    if (row == 12) {
        sc_save_now();
        if (g_ed_mode == 1) sc_choose(sc_row_of(14));         // v4.4 (owner): a saved skill goes straight into the hand
        return;
    }
    if (row == 13) {
        if (g_sc_k < 0) sc_live();                            // v4.2: a live skill can be tried before it is saved
        if (g_sc_k < 0) return;
        for (int i = 0; i < g_sk_count; i++) if (g_sk[i].id == g_sc.id) InterlockedExchange(&g_sk_sel, i);
        InterlockedExchange(&g_drop_request, 1);
        InterlockedExchange(&g_fmenu, 0);
        rumble(1);
        return;
    }
    if (row == 14) {                                         // v3.0: straight into a hand slot (owner)
        if (g_sc_k < 0) sc_live();
        if (g_sc_k < 0) return;
        // v5.33: the game's capture into one slot or all four (sc_put_in_hand below); in Create a Skill the slots stay filled
        __try { sc_put_in_hand(g_sc.id, msg, sizeof(msg)); } __except (EXCEPTION_EXECUTE_HANDLER) { sprintf_s(msg, "Not in a match"); }
        set_status(msg);
        if (g_ed_mode == 1) InterlockedExchange(&g_fmenu, 0);   // v4.2: the menu closes: fire it
        rumble(1);
        return;
    }
    if (row == 15) { InterlockedExchange(&g_fmenu, g_ed_mode == 1 ? 7 : 2); return; }   // v4.2: Edgar's Dream: the Skill Creator pause   // the menu puts the cursor back on Skill Creator
    sc_turn_id(row, 1);
}

// self-test: a Skill Creator skill on the stand-in image: school / cost / damage and another skill's effect, sound and
// animation land in the new record and its profile; a change rewrites it in place
static bool sc_self_test(char *gs) {
    char *src = gs + 0x18 + NS_REC * 10;                     // a second source: skill 10 with its own profile 10
    *(short *)(src + 2) = 10; *(short *)(src + 0x14) = 10;
    char *p10 = gs + NS_PROF_BASE + NS_PROF * 10;
    *(short *)(p10 + 4) = 10; *(short *)(p10 + 6) = 210; *(short *)(p10 + 0x4C) = 64;
    for (int j = 0; j < 6; j++) memset(gs + NS_PROF_BASE + NS_PROF * (NS_PROF0 + j), 0, NS_PROF);   // stand-in: slot 0 free again
    memset(gs + NS_STR0, 0, NS_STR);
    memset(gs + NS_TEXT + 12 * NS_ROW0, 0, 12);
    NewSkill a = {};
    a.id = 380; a.src = 61; strcpy_s(a.name, "SC Test");
    a.school = 4; a.cost = 7; a.damage = 9; a.fx = 10; a.snd = 10; a.anim = 10;
    g_ns[0] = a; g_ns_n = 1; g_ns_said[0] = 0;
    g_ns_test = gs;
    ns_step();
    char *r = gs + 0x18 + NS_REC * 380;
    char *pr = gs + NS_PROF_BASE + NS_PROF * *(short *)(r + 0x14);
    bool ok = *(short *)(r + 2) == 380 && *(short *)(r + 0x12) == 4 && *(short *)(r + 0x28) == 7 && *(short *)(r + 0x58) == 9 &&
              *(short *)(pr + 4) == 10 && *(short *)(pr + 6) == 210 && *(short *)(pr + 0x4C) == 64;
    g_ns[0].damage = 3; g_ns[0].dirty = 1;                   // changed in the menu
    ns_step();
    ok = ok && *(short *)(r + 0x58) == 3 && *(short *)(r + 2) == 380;
    *(short *)(src + 0x7A) = 6; *(short *)(src + 0x86) = 2;   // v4.7: Effect from brings its flight; our damage stays
    g_ns[0].fxmode = 1; g_ns[0].dirty = 1;
    ns_step();
    ok = ok && *(short *)(r + 0x7A) == 6 && *(short *)(r + 0x86) == 2 && *(short *)(r + 0x58) == 3;
    g_ns_test = NULL;
    return ok;
}

// ---- v3.0 skill card and colour preview ----
// The card: record fields (+0x12 school, +0x28 aura cost, +0x58 damage) and the text row +0x00, whose entry holds the
// name and description at entry + [entry+4] / [entry+8] (rebased at load, 0x14017dcb0).
static int sc_card(char out[5][96]) {
    static const char *SCH[6] = {"?", "Psycho", "Optical", "Nature", "Ki", "Faith"};
    int id = g_sc_k >= 0 ? g_sc.id : g_sc.src;
    int n = 0;
    __try {
        char *gs = *(char **)(g_base + RVA_GSDATA);
        if (!gs || *(unsigned *)gs != 0x44004 || id <= 0 || id >= 753) return 0;
        const char *r = gs + 0x18 + NS_REC * id;
        short row = *(const short *)r, sch = *(const short *)(r + 0x12);
        const char *entry = gs + NS_TEXT + 12 * (int)row;
        const char *name = row > 0 ? entry + *(const int *)(entry + 4) : "?";
        const char *desc = row > 0 ? entry + *(const int *)(entry + 8) : "";
        sprintf_s(out[n++], 96, "CARD  %.40s  (skill %d)", name, id);
        sprintf_s(out[n++], 96, "%s   Aura %d   Damage %d", sch >= 1 && sch <= 5 ? SCH[sch] : "?", *(const short *)(r + 0x28),
                  *(const short *)(r + 0x58));
        size_t L = strnlen(desc, 400), at = 0;
        while (at < L && n < 5) {                            // description in lines of up to 58 characters
            size_t take = L - at < 58 ? L - at : 58;
            if (at + take < L) { size_t sp = take; while (sp > 20 && desc[at + sp] != ' ') sp--; if (sp > 20) take = sp; }
            sprintf_s(out[n++], 96, "%.*s", (int)take, desc + at);
            at += take;
            while (at < L && desc[at] == ' ') at++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return n; }
    return n;
}

// v5.8 (owner): the card is the game's own skill card, drawn in the HUD pass by the
// function View Stock's skill window uses (cwo_sixhud.inl, cwo_card_draw); the overlay only says which skill and where.
// Returns true when the game's card was drawn lately (else the overlay shows the text card above as before).
static bool sc_card_want(int x, int y) {
    int id = g_sc_k >= 0 ? g_sc.id : g_sc.src;
    InterlockedExchange(&g_card_id, id);
    InterlockedExchange(&g_card_x, x);
    InterlockedExchange(&g_card_y, y);
    g_card_want = GetTickCount64();
    return g_card_want - g_card_drawn < 300;
}

// Colour preview: a loaded effect span's particle colours change on the spot (node colours at 0x13 chunk +0x38 /
// +0x3C, second pair +0x40 / +0x44, read every frame by FUN_1401a9ae0); textures and vertex colours are on the
// graphics card and change when the game restarts with the built copy. Only the skill's OWN effect copies
// (requests 6000+, made by the colour build) are touched, never a stock skill's effect. Loaded spans: effect loader
// *0x1405bb228: entries of 0x88 at +0xC0, count +0xC8; entry +8 request, +4 loaded, +0x18 data, +0x20 its tail
// (FUN_14018a040 / FUN_14018d0b0).
struct ScOrig { unsigned *at; unsigned v; };
static ScOrig g_sc_orig[4096];
static int g_sc_orig_n;

unsigned sc_tint(unsigned w, int hue, int sat, int val);
unsigned sc_turn_rgb(unsigned w, int hue) { return sc_tint(w, hue, 100, 100); }
unsigned sc_tint(unsigned w, int hue, int satp, int valp) {
    float r = ((w >> 16) & 255) / 255.0f, g = ((w >> 8) & 255) / 255.0f, b = (w & 255) / 255.0f;
    float mx = max(r, max(g, b)), mn = min(r, min(g, b)), d = mx - mn, h = 0;
    if (d > 0) {
        if (mx == r) h = fmodf((g - b) / d, 6.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h *= 60.0f;
        if (h < 0) h += 360.0f;
    }
    float sat = mx > 0 ? d / mx : 0, v = mx;
    h = fmodf(h + (float)hue, 360.0f);
    sat = sat * satp / 100.0f; if (sat > 1) sat = 1;                    // v3.1 saturation / brightness sliders
    v = v * valp / 100.0f; if (v > 1) v = 1;
    float c = v * sat, x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1)), m = v - c, R = 0, G = 0, B = 0;
    if (h < 60) { R = c; G = x; } else if (h < 120) { R = x; G = c; } else if (h < 180) { G = c; B = x; }
    else if (h < 240) { G = x; B = c; } else if (h < 300) { R = x; B = c; } else { R = c; B = x; }
    unsigned ri = (unsigned)((R + m) * 255 + 0.5f), gi = (unsigned)((G + m) * 255 + 0.5f), bi = (unsigned)((B + m) * 255 + 0.5f);
    return (w & 0xFF000000) | (ri << 16) | (gi << 8) | bi;
}

static int sc_effect_req() {                                 // v4.2 the effect request the live skill uses now
    __try {
        char *gs = *(char **)(g_base + RVA_GSDATA);
        if (!gs || *(unsigned *)gs != 0x44004 || g_sc_k < 0) return 0;
        short p = *(short *)(gs + 0x18 + NS_REC * g_sc.id + 0x14);
        return p > 0 ? *(short *)(gs + NS_PROF_BASE + NS_PROF * p + 4) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int g_sc_tint_req;                                    // v4.2: a stock effect coloured for the preview (0 none)
static void sc_tint_restore() {                              // every colour word changed by the preview gets its value back
    if (!g_sc_orig_n) return;
    __try {
        ULONG_PTR ld = *(ULONG_PTR *)(g_base + 0x5bb228);
        char *ent = ld ? *(char **)(ld + 0xC0) : NULL;
        unsigned cnt = ld ? *(unsigned *)(ld + 0xC8) : 0;
        for (unsigned e = 0; ent && e < cnt && e < 1024; e++) {   // only spans still loaded are touched
            char *en = ent + 0x88 * e;
            if (*(int *)(en + 4) == 0) continue;
            char *data = *(char **)(en + 0x18), *tail = *(char **)(en + 0x20);
            if (!data || tail <= data) continue;
            for (int k = 0; k < g_sc_orig_n; k++)
                if ((char *)g_sc_orig[k].at >= data && (char *)g_sc_orig[k].at < tail) *g_sc_orig[k].at = g_sc_orig[k].v;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_sc_orig_n = 0;
    g_sc_tint_req = 0;
}

static volatile LONG g_scp_fire_now;                         // below: the R3 loop uses the skill again at once
static void sc_preview() {                                  // v5.32: a colour change shows on the skill's next use
    if (g_sc_k < 0) { set_status("Color: change any value first (the skill goes live)"); return; }
    if (g_fx_state != 1) { set_status("Color: the effect load hook is off (see the log)"); return; }
    InterlockedExchange(&g_scp_fire_now, 1);
    set_status("Color: its next use shows it - textures, sparks, all (R3: on a loop)");
}

// ---- v3.1 projectile extra (owner) ----
// Each player's flying projectiles: player + 0x4E0, 48 slots of 0x1F0 (FUN_14020f4f0); slot +0x10 state (0 free,
// set to 2 when fired), +0x14 the skill, +0x80 its 4x4 matrix (FUN_140263ce0 fills them; the flight updates the
// matrix). For a new skill with trail=N, skill N's flying effect (part 1, looping) starts on each of its projectiles,
// is moved to the projectile's matrix every frame (FUN_14018d440) and stops when the projectile is gone.
struct NsTrail { ULONG_PTR slot; int skill; unsigned h; int fails; unsigned h2; ULONGLONG next; };   // v5.188.22 h2 / next: overlapped part trails
static NsTrail g_ntr[64];
static int g_ntr_n;

// v3.3 Skill Creator preview (owner). v5.32 (owner 2026-10-27): R3 puts the skill in the chosen hand slot with the game's own capture and has the fighter use it
// each time it is free again; the game does the whole skill - motion, effect, projectile, hit. The fighter does not walk
// (the input filter keeps the sticks) and the sticks turn a camera round it. R3 again, or leaving the Skill Creator, ends it.
// v5.33 (owner): 5.32 pressed the slot's button through the pad, which never
// reached the fighter; now the loader asks for the slot the way the game's own combo code does.
// Game facts (Ghidra full pass 065, 2026-10-27/28):
//  - FUN_1402517c0 (the fighter's hand, every frame; param = fighter): hand slot k = fighter + 0x388 + 0x50 k (= battle data
//    *(0x1404bc608) + 0x348 + 0x50 k); slot +8 a card is there, +0x24 / +0x3c / +0x40 busy (0 / -1 / -1 = free), +0x30 the
//    skill. A slot is used when its button is new (FUN_140256920) OR fighter + 0x61f4 == k - the queued use FUN_14024fb90
//    (the combo check) writes; FUN_1402517c0 sets it back to -1 once the use has started. The use itself goes through
//    FUN_140261e90(slot, 0) (the game's own aura / cost check) and FUN_140258180 (the motion).
//  - the fighter's state fighter + 0x12f04: a slot is used only in states whose bits are 0, 1 or 8 (not 9).
//  - FUN_140261a00(slot, skill, 0, 0) = the game's capture into a slot (it needs +0x3c = +0x40 = -1, and puts the old card
//    away first).
// The camera: the event camera (flag 0x1404bc630, target 0x14059e130, eye 0x14059e150), as the free camera (cwo_freecam.inl).
static volatile LONG g_scp_req;
static int g_scp_user_off;
static ULONGLONG g_scp_anim_t, g_scp_fire_t, g_scp_off_t;
static float g_scp_yaw, g_scp_pitch = 0.3f, g_scp_dist = 70.0f;
static int g_scp_cam_keep;
static const ULONG_PTR RVA_SCP_CAM_ON = 0x4bc630, RVA_SCP_CAM_TGT = 0x59e130, RVA_SCP_CAM_EYE = 0x59e150, FN_SCP_CAPTURE = 0x261a00;
static const int SCP_USE_REQ = 0x61f4, SCP_STATE = 0x12f04, SC_HAND_ALL = 4;
static int g_sc_keep_skill = -1, g_sc_keep_mask;            // v5.33 Put in hand: these slots keep the skill (unlimited uses)
static ULONGLONG g_sc_keep_t;
static void sc_preview_toggle() { InterlockedExchange(&g_scp_req, 1); }

static char *sc_hand_slot(ULONG_PTR actor, int k) { return (char *)(actor + 0x388 + 0x50 * (ULONG_PTR)k); }
static bool sc_slot_free(const char *slot) {
    return *(const int *)(slot + 0x24) == 0 && *(const int *)(slot + 0x3c) == -1 && *(const int *)(slot + 0x40) == -1;
}
static bool sc_slot_has(const char *slot, int skill) { return *(const int *)(slot + 8) != 0 && *(const int *)(slot + 0x30) == skill; }
static bool sc_capture(char *slot, int skill) {                // the game's capture; true when the card is there now
    if (sc_slot_has(slot, skill)) return true;
    if (*(int *)(slot + 0x3c) != -1 || *(int *)(slot + 0x40) != -1) return false;
    typedef void(__fastcall *PFN_SCP_CAPTURE)(void *, int, unsigned, int);
    ((PFN_SCP_CAPTURE)(g_base + FN_SCP_CAPTURE))(slot, skill, 0, 0);
    return sc_slot_has(slot, skill);
}
static bool sc_fighter_free(ULONG_PTR actor) {                 // FUN_1402517c0's own test
    unsigned st = *(unsigned *)(actor + SCP_STATE);
    return (st & 0xfffffff6u) == 0 && st != 9 && *(int *)(actor + SCP_USE_REQ) == -1;
}
// v5.33 is the looped / kept skill in use (or just used)? A rewrite of its record waits until it has finished:
// the game drops the old effect when the record changes, and a trail still drawn from it crashed (owner; FUN_1401a1c00 -> FUN_14001e4b0 on a freed texture)
static bool sc_skill_busy(int id) {
    ULONGLONG now = GetTickCount64();
    if (g_scp_on && g_scp_skill == id) return true;
    if (g_scp_off_t && now - g_scp_off_t < 4000 && g_scp_skill == id) return true;
    return g_sc_keep_t && now - g_sc_keep_t < 4000 && g_sc_keep_skill == id;
}

static void ph_step(bool toggle, int dspeed);               // cwo_freecam.inl (included later): the world's pace
static void ph_end();
static void ph_want(bool pause, int speed);
static int g_scp_k;                                         // v5.36 the loop's hand slot (the first free one)
// v5.167.0 test casts (the agent's offline tests only: CwoTestCast.txt in the loader folder = skill numbers): in a match,
// player 1 gets each listed skill in a free hand slot by the game's capture (0x140261a00, as the preview loop) and uses it
// with the game's queued use (actor +0x61f4) when the fighter is free (sc_fighter_free = FUN_1402517c0's own test), one cast
// every 4 s, each logged - to see created skills (the Illusion school 753+) cast in the game. Never online.
static bool ed_online();                                        // cwo_edgar.inl
static int g_tc_ids[64], g_tc_n, g_tc_k;
static bool g_tc_was_in;
static ULONGLONG g_tc_t;
static void tc_step(bool in_match) {
    if (in_match && !g_tc_was_in) {                             // a match began: read the list
        g_tc_n = 0; g_tc_k = 0; g_tc_t = GetTickCount64() + 12000;
        wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoTestCast.txt", g_dir);
        FILE *f = NULL;
        if (!ed_online() && _wfopen_s(&f, p, L"r") == 0 && f) {
            int v;
            while (g_tc_n < 64 && fscanf_s(f, "%d", &v) == 1) if (v >= 0 && v < 1128) g_tc_ids[g_tc_n++] = v;
            fclose(f);
            logf("test cast: %d skill(s) from CwoTestCast.txt for player 1", g_tc_n);
        }
    }
    g_tc_was_in = in_match;
    if (!in_match || g_tc_n <= 0 || ed_online()) return;
    ULONGLONG now = GetTickCount64();
    if (now < g_tc_t) return;
    __try {
        ULONG_PTR actor = cwo_actors()[0];
        if (!actor || !sc_fighter_free(actor)) return;
        int skill = g_tc_ids[g_tc_k % g_tc_n], k = -1;
        for (int i = 0; i < 4 && k < 0; i++) if (sc_slot_has(sc_hand_slot(actor, i), skill)) k = i;
        for (int i = 0; i < 4 && k < 0; i++) { char *sl = sc_hand_slot(actor, i); if (sc_slot_free(sl) && sc_capture(sl, skill)) k = i; }
        for (int i = 0; i < 4 && k < 0; i++) {                  // v5.167.1 no free slot (the arsenal's cards): the game's capture over a card not in use
            char *sl = sc_hand_slot(actor, i);
            if (*(int *)(sl + 0x3c) == -1 && *(int *)(sl + 0x40) == -1 && sc_capture(sl, skill)) k = i;
        }
        if (k < 0) { g_tc_t = now + 500; return; }              // no slot free yet
        float au = *(float *)(actor + 0x5C); int mx = *(int *)(actor + 0x58);   // v5.167.2 the status block (fighter +0x40): +0x18 max aura, +0x1C aura
        // v5.167.4 (in game: a match starts with max aura 0 until Aura Particles are taken - every cast was refused): the test
        // gives player 1 aura level 10 and full aura before each cast (the cost is not the test)
        *(int *)(actor + 0x58) = 10; *(float *)(actor + 0x5C) = 10.0f;
        static bool dumped;
        if (!dumped) {                                          // v5.167.3 once: the status block as the game has it here
            dumped = true;
            char hx[200] = ""; size_t o = 0;
            for (int i = 0; i < 0x40; i += 4) o += sprintf_s(hx + o, sizeof(hx) - o, " %08x", *(unsigned *)(actor + 0x40 + i));
            logf("test cast: actor %p, status block +0x40..+0x7f:%s", (void *)actor, hx);
        }
        *(int *)(actor + SCP_USE_REQ) = k;
        logf("test cast: skill %d from hand slot %d (%d of %d), aura %.1f / %d -> 10 / 10", skill, k + 1, g_tc_k % g_tc_n + 1, g_tc_n, au, mx);
        g_tc_k++;
        g_tc_t = now + 4000;
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_tc_t = now + 2000; }
}

static void sc_loop_end(bool in_match) {                    // the camera back to the game's
    if (!g_scp_on) return;
    g_scp_on = 0;
    g_scp_off_t = GetTickCount64();
    InterlockedExchange(&g_scp_loop, 0);
    ph_end();                                               // v5.36 full speed again
    if (in_match) { __try { *(int *)(g_base + RVA_SCP_CAM_ON) = g_scp_cam_keep; } __except (EXCEPTION_EXECUTE_HANDLER) {} }
}

// Put in hand (row 14): the skill into one slot or all four, with the game's capture; in Create a Skill they stay filled
// v5.36 (owner; log 19:02:44: four captures from the controller
// thread, then FUN_1401a1c00 -> FUN_14001e4b0 on a freed texture 48 ms later): the captures now run on the game thread
// (sc_preview_step), one slot a frame, when the slot is free and the fighter is idle - the game's own capture timing
static volatile LONG g_sc_put_mask;                          // slots still to fill
static void sc_put_in_hand(int skill, char *msg, size_t n) {
    ULONG_PTR actor = cwo_actors()[0];
    if (!actor) { sprintf_s(msg, n, "Not in a match"); return; }
    int mask = g_sc_hand == SC_HAND_ALL ? 15 : 1 << g_sc_hand;
    g_sc_keep_skill = skill;
    g_sc_keep_mask = g_ed_mode == 1 ? mask : 0;
    g_sc_keep_t = GetTickCount64();
    InterlockedExchange(&g_sc_put_mask, mask);
    if (g_sc_hand == SC_HAND_ALL) sprintf_s(msg, n, "All slots: it goes in as each slot is free%s", g_ed_mode == 1 ? " (they stay filled)" : "");
    else sprintf_s(msg, n, "Hand slot %d: it goes in when the slot is free%s", g_sc_hand + 1,
                   g_ed_mode == 1 ? " (it stays there: use it as often as you like)" : "");
    logf("Skill Creator: skill %d put in hand (%s) asked - filled on the game thread", skill, g_sc_hand == SC_HAND_ALL ? "all slots" : "one slot");
}

static void sc_preview_step(FxGame &g, bool in_match) {
    (void)g;
    bool req = InterlockedExchange(&g_scp_req, 0) != 0;
    InterlockedExchange(&g_scp_restart, 0);                  // (the loop uses every change by itself)
    ULONGLONG now = GetTickCount64();
    if (g_sc_tint_at && now >= g_sc_tint_at) g_sc_tint_at = 0;
    if (!in_match) { g_sc_keep_mask = 0; g_sc_keep_skill = -1; InterlockedExchange(&g_scp_resume, 0); InterlockedExchange(&g_sc_put_mask, 0); }
    if (InterlockedExchange(&g_scp_stop_req, 0) && g_scp_on) {
        sc_loop_end(in_match);
        set_status("Preview: the change applies when the last use has finished (a few seconds), then the loop goes on");
    }
    if (g_scp_on && (req || !in_match || g_fmenu != 5)) {
        sc_loop_end(in_match);
        InterlockedExchange(&g_scp_resume, 0);
        if (req) set_status("Skill preview off");
        return;
    }
    if (in_match && g_fmenu == 5 && !g_scp_on && sc_id(g_fmenu_sel) == 7 && g_ed_mode == 1 && now - g_scp_anim_t > 2600) {   // 'Animation from'
        g_scp_anim_t = now;
        __try {
            ULONG_PTR pl = cwo_actors()[0];
            char *gs = *(char **)(g_base + RVA_GSDATA);
            int from = g_sc.anim > 0 ? g_sc.anim : g_sc.src;
            short p = gs && *(unsigned *)gs == 0x44004 ? *(short *)(gs + 0x18 + NS_REC * from + 0x14) : 0;
            int slot = p > 0 ? *(short *)(gs + NS_PROF_BASE + NS_PROF * p + 0x4C) : -1;
            if (pl && slot > 0 && slot < 130 && *(int *)(pl + 0x12f04) == 0 && *(int *)(pl + 0x1396c) == -1 && *(int *)(pl + 0x13968) == 0) {
                typedef void(__fastcall *PFN_SC_PLAY)(ULONG_PTR, int, int, float, int, float, float);
                ((PFN_SC_PLAY)(g_base + 0x257310))(pl + 0x12f00, slot, 0, *(float *)(g_base + 0x2f7f18), 1, -1.0f, -1.0f);
                char msg[96];
                sprintf_s(msg, "Animation: %.30s (motion %d)", sc_skill_name(from), slot);
                set_status(msg);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    ULONG_PTR actor = in_match ? cwo_actors()[0] : 0;
    // v5.36 Put in hand: the asked slots, one capture a frame on this (the game's) thread
    LONG pm = g_sc_put_mask;
    if (actor && pm && g_sc_keep_skill > 0) {
        __try {
            for (int k = 0; k < 4; k++) {
                if (!(pm & (1 << k))) continue;
                char *slot = sc_hand_slot(actor, k);
                if (sc_slot_has(slot, g_sc_keep_skill)) { InterlockedAnd(&g_sc_put_mask, ~(1 << k)); continue; }
                if (sc_slot_free(slot) && sc_fighter_free(actor)) {
                    bool ok = sc_capture(slot, g_sc_keep_skill);
                    InterlockedAnd(&g_sc_put_mask, ~(1 << k));
                    g_sc_keep_t = now;
                    logf("Skill Creator: skill %d into hand slot %d %s", g_sc_keep_skill, k + 1, ok ? "(captured)" : "(the capture did not take)");
                    break;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { InterlockedExchange(&g_sc_put_mask, 0); }
    }
    // v5.33 Put in hand keeps its slots filled in Create a Skill (owner);
    // v5.36 one capture a frame here too
    if (actor && !g_sc_put_mask && g_sc_keep_mask && g_sc_keep_skill > 0 && now - g_sc_keep_t > 300) {
        __try {
            for (int k = 0; k < 4; k++) {
                if (!(g_sc_keep_mask & (1 << k))) continue;
                char *slot = sc_hand_slot(actor, k);
                if (!sc_slot_free(slot)) { g_sc_keep_t = now; continue; }   // in use now: the record waits (sc_skill_busy)
                if (!sc_slot_has(slot, g_sc_keep_skill) && sc_fighter_free(actor)) { sc_capture(slot, g_sc_keep_skill); g_sc_keep_t = now; break; }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { g_sc_keep_mask = 0; }
    }
    if (!req && !g_scp_on && g_scp_resume && in_match && g_fmenu == 5 && !sc_skill_busy(g_sc_k >= 0 ? g_sc.id : g_sc.src)) {
        InterlockedExchange(&g_scp_resume, 0);                 // the held change is written: the loop again
        bool written = g_sc_k < 0 || g_sc_k >= g_ns_n || !g_ns[g_sc_k].dirty;
        if (written) req = true; else InterlockedExchange(&g_scp_resume, 1);
    }
    if (req && in_match && !g_scp_on && actor && g_fmenu == 5) {   // R3: the loop starts
        g_scp_skill = g_sc_k >= 0 ? g_sc.id : g_sc.src;
        __try {
            g_scp_cam_keep = *(int *)(g_base + RVA_SCP_CAM_ON);
            g_scp_yaw = (float)*(int *)(actor + 0x12E68) / 5215.18896f + 2.4f;   // in front of the fighter, a little to the side
        } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
        g_scp_pitch = 0.3f; g_scp_dist = 70.0f;
        // v5.36 (owner): the loop
        // waited for hand slot 1 to be free, and the arsenal's starting cards sit in slots 1-3. Now it takes the first slot
        // that is free (or already holds the skill); only when all four hold other cards does it wait for one
        g_scp_k = -1;
        __try {
            for (int k = 0; k < 4 && g_scp_k < 0; k++) { char *s = sc_hand_slot(actor, k); if (sc_slot_has(s, g_scp_skill) || sc_slot_free(s)) g_scp_k = k; }
        } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
        g_scp_on = 1;
        InterlockedExchange(&g_scp_loop, 1);
        g_scp_fire_t = 0;
        char msg[160];
        if (g_scp_k < 0) { g_scp_k = 0; sprintf_s(msg, "Preview: all four hand slots hold cards - use one, then the loop starts (R3: stop)"); }
        else sprintf_s(msg, "Preview: %.30s on a loop - sticks: camera, LT: slow, LT full + RT: a frame at a time, R3: stop", g_sc.unsaved ? "your new skill" : sc_skill_name(g_scp_skill));
        set_status(msg);
        logf("Skill Creator: preview loop of skill %d in hand slot %d", g_scp_skill, g_scp_k + 1);
    }
    if (!g_scp_on || !actor) return;
    __try {
        // the camera: right stick turns, left stick up / down comes closer / goes back
        float dt = 1.0f / 30.0f, rx = g_scp_axis[2], ry = g_scp_axis[3], ly = g_scp_axis[1];
        if (fabsf(rx) > 0.15f) g_scp_yaw -= rx * 2.2f * dt;
        if (fabsf(ry) > 0.15f) g_scp_pitch += ry * 1.6f * dt;
        if (fabsf(ly) > 0.15f) g_scp_dist -= ly * 90.0f * dt;
        if (g_scp_pitch > 1.3f) g_scp_pitch = 1.3f;
        if (g_scp_pitch < -0.4f) g_scp_pitch = -0.4f;
        if (g_scp_dist < 25.0f) g_scp_dist = 25.0f;
        if (g_scp_dist > 220.0f) g_scp_dist = 220.0f;
        const float *pp = (const float *)(actor + 0x12E40);
        float *t = (float *)(g_base + RVA_SCP_CAM_TGT), *e = (float *)(g_base + RVA_SCP_CAM_EYE);
        t[0] = pp[0]; t[1] = pp[1] + 10.0f; t[2] = pp[2];
        e[0] = t[0] + sinf(g_scp_yaw) * cosf(g_scp_pitch) * g_scp_dist;
        e[1] = t[1] + sinf(g_scp_pitch) * g_scp_dist;
        e[2] = t[2] + cosf(g_scp_yaw) * cosf(g_scp_pitch) * g_scp_dist;
        *(int *)(g_base + RVA_SCP_CAM_ON) = 1;
        // the skill in its slot (the game's capture whenever the card has been used up), then the game's queued use
        int k = g_scp_k;
        char *slot = sc_hand_slot(actor, k);
        if (InterlockedExchange(&g_scp_fire_now, 0)) g_scp_fire_t = 0;
        if (sc_fighter_free(actor) && sc_slot_free(slot)) {
            if (!sc_slot_has(slot, g_scp_skill)) sc_capture(slot, g_scp_skill);
            else if (now - g_scp_fire_t > 900) { *(int *)(actor + SCP_USE_REQ) = k; g_scp_fire_t = now; }
        }
        // v5.36 (owner): LT half = 25 %
        // speed, LT full = frozen, each RT press lets one step through (the photo mode's world-pace flag, ph_step)
        float lt = g_scp_axis[4], rt = g_scp_axis[5];
        static bool s_rt_was;
        bool rt_new = rt > 0.5f && !s_rt_was;
        s_rt_was = rt > 0.5f;
        if (lt > 0.9f) ph_want(!rt_new, 0);
        else if (lt > 0.3f) ph_want(false, 2);
        else ph_want(false, 0);
        ph_step(false, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { sc_loop_end(in_match); set_status("Preview stopped (fault)"); }
}

static void ns_trail_step(bool in_match) {
    FxGame g;
    if (!fx_game(&g)) return;
    sc_preview_step(g, in_match);
    bool any = false;
    for (int k = 0; k < g_ns_n; k++) any |= g_ns[k].trail > 0;
    if (!in_match || !any) {
        for (int i = 0; i < g_ntr_n; i++) if (in_match && fx_alive(g, g_ntr[i].h)) g.stop(g_ntr[i].h);
        g_ntr_n = 0;
        return;
    }
    __try {
        for (int i = 0; i < g_ntr_n; i++) {                  // finished projectiles: their extra stops
            NsTrail &t = g_ntr[i];
            if (*(int *)(t.slot + 0x10) == 0 || *(int *)(t.slot + 0x14) != t.skill) {
                if (fx_alive(g, t.h)) g.stop(t.h);
                if (t.h2 != FX_NONE && fx_alive(g, t.h2)) g.stop(t.h2);
                g_ntr[i--] = g_ntr[--g_ntr_n];
            }
        }
        for (int p = 0; p < 4; p++) {
            ULONG_PTR actor = cwo_actors()[p];
            if (!actor || !*(int *)(actor + 0x20)) continue;
            for (int k = 0; k < 48; k++) {
                ULONG_PTR slot = actor + 0x4E0 + 0x1F0 * (ULONG_PTR)k;
                if (*(int *)(slot + 0x10) == 0) continue;
                int sk = *(int *)(slot + 0x14), trail = 0, tpart = 1;
                static int seen[16], seen_n;                 // v3.3: which skill numbers the projectiles carry (once each)
                bool known = false;
                for (int q = 0; q < seen_n; q++) known |= seen[q] == sk;
                if (!known && seen_n < 16) {
                    seen[seen_n++] = sk;
                    logf("projectile: player %d slot %d state %d skill %d (+0x18 %d, +0x60 %d)", p + 1, k, *(int *)(slot + 0x10), sk,
                         *(int *)(slot + 0x18), *(int *)(slot + 0x60));
                }
                for (int n = 0; n < g_ns_n && !trail; n++) if (g_ns[n].id == sk) { trail = g_ns[n].trail; tpart = g_ns[n].trail_part > 0 ? g_ns[n].trail_part - 1 : 1; }
                if (trail <= 0) continue;
                int at = -1;
                for (int i = 0; i < g_ntr_n; i++) if (g_ntr[i].slot == slot) at = i;
                if (at < 0) {
                    if (g_ntr_n >= 64) continue;
                    at = g_ntr_n++;
                    g_ntr[at].slot = slot; g_ntr[at].skill = sk; g_ntr[at].h = FX_NONE; g_ntr[at].fails = 0; g_ntr[at].h2 = FX_NONE; g_ntr[at].next = 0;
                }
                NsTrail &t = g_ntr[at];
                // v5.188.22 (owner: Fiery Tornado "just looks like a little flame trail"): a named part (trail=N:P, e.g. Wall of Fire's
                // flame body, which does not loop) is started again every 0.4 s on a second handle, so one is always burning
                bool overlap = tpart != 1;
                if (overlap) {
                    if (t.h2 != FX_NONE && fx_alive(g, t.h2)) g.move(t.h2, (const float *)(slot + 0x80));
                    ULONGLONG nowt = GetTickCount64();
                    if (fx_alive(g, t.h)) { g.move(t.h, (const float *)(slot + 0x80)); if (nowt < t.next) continue; }
                    else if (nowt < t.next && t.h2 != FX_NONE && fx_alive(g, t.h2)) continue;
                    t.next = nowt + 400;
                    if (t.h != FX_NONE && fx_alive(g, t.h)) { if (t.h2 != FX_NONE && fx_alive(g, t.h2)) g.stop(t.h2); t.h2 = t.h; }
                    t.h = FX_NONE;
                } else if (fx_alive(g, t.h)) { g.move(t.h, (const float *)(slot + 0x80)); continue; }
                if (t.fails > 120) continue;
                short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)trail * 0x90);
                int req = g.effect_of(prof, trail);
                if (req <= 0) { t.fails = 121; continue; }
                unsigned h = g.start(g.mgr, *(int *)(actor + 0x14), req, tpart, (const float *)(slot + 0x80), 0);
                if (h == FX_NONE) { t.fails++; continue; }
                g.loop(h);
                t.h = h;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_ntr_n = 0; }
}
