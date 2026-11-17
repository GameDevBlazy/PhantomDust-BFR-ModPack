// ---- v5.45 the bigger skill table + skill lists (owner 2026-10-31: extend the skill list where it works correctly, never
// overwrite a skill that is in the game, at least 10 copies of each new skill).
// Game facts (PD Ghidra full pass 065, MODLOG 2026-10-31): the skill data image is the static array 0x1404c5240
// (0x44004 bytes; FUN_14017c690 sets *0x1404c5228 to it); FUN_14017dcb0 installs it and is the ONLY code that uses the
// layout offsets: *0x1404bdc40 = image + 4, *0x1404bdc50 = the records (image + 0x18, 0x90 each, 753 of them: the count at
// image + 8; 60 readers use this pointer), *0x1404bcc98 = the profiles (image + 0x1B00C, 0x72 each, 896 slots; 24 readers),
// *0x1404bdc48 = the text rows (image + 0x34018, 12 bytes {9001, name offset, description offset} self-relative, the
// count at image + 0x34008), *0x1404bdc58 = image + 0x2B00C (the per-skill side tables; 29 readers use image + 0x33xxx
// directly, so the image itself stays where it is); the displayed-id maps exe + 0x4BCCA0 / + 0x4BDC60 hold 1,016 ints;
// the arsenal side block's stock bytes (side + 0x644 + id) hold 1,000 ids.
// What this does: the loader allocates its own record, profile and text-row tables (bigger copies, near the exe so the
// text rows' 32-bit offsets reach the image's strings) and points the game's three pointers at them; records 753-999,
// profiles 896-1999 and text rows 3000-4999 are then real to the game. Whenever the game installs its skill data again
// (the pointers go back to the image), the copies are refreshed and re-pointed on the next frame. The skills of a list
// file (the Illusion school's CwoIllusion.txt: id, source, name, description, options) are written into empty
// records as the Skill Creator's skills are (ns_put rules: only empty places), with one profile each, their text in the
// loader's own string arena, and 10 copies in stock. Never records 0-374 or 700-752.
static const ULONG_PTR RVA_GS_IMAGE = 0x4c5240, RVA_P_HDR = 0x4bdc40, RVA_P_REC = 0x4bdc50, RVA_P_PROF = 0x4bcc98, RVA_P_ROWS = 0x4bdc48;
static const int BS_RECS = 1200, BS_PROFS = 2000, BS_ROWS = 5000, BS_ROW0 = 3000, BS_PROF0 = 896, BS_ARENA = 0x80000;
static const int BS_MAX = 700, BS_ID_LO = 381, BS_ID_HI = 1199;
static const int BS_FX_OWNER = 9006;                            // v5.159 the owner number of the lists' recoloured effects
struct ListSkill { int id, src, school, cost, damage, copies; int fo[12], fv[12], nfo; char name[40], desc[200]; int said; int prof, row, stro; int hit; int snd; int fx;
                    int sat, val; };                            // v5.159 the effect drawn recoloured (sat 1 = no colour; val brightness %)
static ListSkill g_bs[BS_MAX];
static bool bfr_owns(int id);
static int bfr_count();                                         // v5.58 cwo_bfr.inl: the pack's list alone also needs the big tables
static bool g_bs_dead;                                          // v5.58 a fault: off for the session                                  // v5.56 cwo_bfr.inl: the Battle for Reality pack holds this id now
static int g_bs_n;
static unsigned char *g_bs_mem;                                 // one block: records | profiles | rows | strings
static char *g_bs_rec, *g_bs_prof, *g_bs_rows, *g_bs_str;
static int g_bs_str_used, g_bs_prof_next = BS_PROF0, g_bs_row_next = BS_ROW0, g_bs_repoints, g_bs_refresh;
static bool g_bs_on;
static char *g_bs_test_img;                                     // self-test stand-in image
static ULONG_PTR g_bs_t_ptr[4];                                 // self-test stand-in pointer cells

static ULONG_PTR bs_cell(ULONG_PTR rva) { return g_bs_test_img ? (ULONG_PTR)&g_bs_t_ptr[rva == RVA_P_HDR ? 0 : rva == RVA_P_REC ? 1 : rva == RVA_P_PROF ? 2 : 3] : g_base + rva; }
static char *bs_image() { return g_bs_test_img ? g_bs_test_img : (char *)(g_base + RVA_GS_IMAGE); }

static bool bs_alloc() {                                        // near the exe: the rows' offsets must reach the image's strings
    size_t n = (size_t)BS_RECS * NS_REC + (size_t)BS_PROFS * NS_PROF + (size_t)BS_ROWS * 12 + BS_ARENA;
    // v5.52.1 (owner 2026-11-01, Aura Particle crash on a pack stage: CRASH at exe+0x24fc07): FUN_14024fb90 (the combo
    // check) reads record (fighter+0x12f5c) = -1 when the fighter has used no skill yet - 0x5e bytes before the table. In
    // the stock game that is inside the gsdata image; our table started a fresh allocation, so the page before it was
    // unmapped. The table now starts one page into its block; the lead page is 0xFF (shorts of -1 match no type id).
    size_t lead = g_bs_test_img ? 0 : 0x1000;
    if (g_bs_test_img) { g_bs_mem = (unsigned char *)g_bs_test_img + 0x44004; }      // the stand-in block follows the stand-in image
    else {
        for (int k = 1; k <= 32 && !g_bs_mem; k++) {
            ULONG_PTR hint = (g_base + 0x4000000 * (ULONG_PTR)k) & ~(ULONG_PTR)0xffff;
            g_bs_mem = (unsigned char *)VirtualAlloc((void *)hint, n + lead, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        }
        if (!g_bs_mem) g_bs_mem = (unsigned char *)VirtualAlloc(NULL, n + lead, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    if (!g_bs_mem) return false;
    if (lead) memset(g_bs_mem, 0xFF, lead);
    g_bs_rec = (char *)g_bs_mem + lead;
    g_bs_prof = g_bs_rec + (size_t)BS_RECS * NS_REC;
    g_bs_rows = g_bs_prof + (size_t)BS_PROFS * NS_PROF;
    g_bs_str = g_bs_rows + (size_t)BS_ROWS * 12;
    LONGLONG d = g_bs_test_img ? 0 : (LONGLONG)(bs_image() - g_bs_rows);
    if (d > 0x70000000LL || d < -0x70000000LL) { logf("big skills: table block %p is too far from the skill data image %p", (void *)g_bs_mem, (void *)bs_image()); return false; }
    return true;
}

// copy the game's tables into ours (records 0-752, profiles 0-895, rows 0..count-1 with the offsets re-aimed at the
// same strings) and point the game at ours
static void bs_refresh_and_point() {
    char *img = bs_image();
    int recs = *(int *)(img + 8), rows = *(int *)(img + 0x34008);
    if (recs < 1 || recs > 753) recs = 753;
    memcpy(g_bs_rec, img + 0x18, (size_t)recs * NS_REC);
    memcpy(g_bs_prof, img + NS_PROF_BASE, (size_t)896 * NS_PROF);
    if (rows > 0 && rows <= 4000) {
        for (int r = 0; r < rows; r++) {
            const char *o = img + NS_TEXT + 12 * r;
            char *e = g_bs_rows + 12 * r;
            int kind = *(const int *)o, a = *(const int *)(o + 4), b = *(const int *)(o + 8);
            *(int *)e = kind;
            *(int *)(e + 4) = a ? (int)((o + a) - e) : 0;       // the same string, from our entry
            *(int *)(e + 8) = b ? (int)((o + b) - e) : 0;
        }
    }
    // v5.85 (owner 2026-11-02): records 750 /
    // 751 are the game's hidden system copies of Jump (257) and Dash (294) - shown ids 257 / 294, Ki, text rows 750 / 751
    // left empty (an empty row reads as row 1, Psycho Wave). Acquire All Skills = native 5.41 FUN_140216ff0: for every
    // record 1 .. count-1 whose +0x04 is below 100 it adds +0x04 to the stock (*(*0x1404bec10 + 0x3a38) + id) and sets the
    // owned pair (*(... + 0x3a40) + id * 4). Their +0x04 = 100 keeps them out; their rows point at Jump's / Dash's names.
    for (int k = 0; k < 2; k++) {
        int sys = 750 + k, src = k ? 294 : 257;
        if (sys >= recs) continue;
        *(short *)(g_bs_rec + (size_t)NS_REC * sys + 4) = 100;
        if (sys < BS_ROWS && src < rows) {
            char *e = g_bs_rows + 12 * sys, *s = g_bs_rows + 12 * src;
            *(int *)e = *(int *)s;
            *(int *)(e + 4) = *(int *)(s + 4) ? (int)((s + *(int *)(s + 4)) - e) : 0;
            *(int *)(e + 8) = *(int *)(s + 8) ? (int)((s + *(int *)(s + 8)) - e) : 0;
        }
    }
    *(ULONG_PTR *)bs_cell(RVA_P_REC) = (ULONG_PTR)g_bs_rec;
    *(ULONG_PTR *)bs_cell(RVA_P_PROF) = (ULONG_PTR)g_bs_prof;
    *(ULONG_PTR *)bs_cell(RVA_P_ROWS) = (ULONG_PTR)g_bs_rows;
    if (*(int *)(img + 8) < BS_RECS) *(int *)(img + 8) = BS_RECS;          // the record count: our ids are real
    if (*(int *)(img + 0x34008) < BS_ROWS) *(int *)(img + 0x34008) = BS_ROWS;
    if (*(unsigned *)(img + 0x14) < (unsigned)BS_ID_HI) *(unsigned *)(img + 0x14) = BS_ID_HI;   // deck / reference limit
    g_bs_repoints++;
    for (int k = 0; k < g_bs_n; k++) g_bs[k].said = -9;                     // every list skill checked again
}

static bool bs_pointed() { return *(ULONG_PTR *)bs_cell(RVA_P_REC) == (ULONG_PTR)g_bs_rec; }

// v5.111 (owner 2026-11-07: Create a Skill "creates it as cycle wave, as an aura particle, no matter what"): while the
// loader's bigger tables are pointed (the BFR pack or list skills on), the game reads records / profiles / text rows
// through them (*0x1404bdc50 / *0x1404bcc98 / *0x1404bdc48), but the Skill Creator writes its skills into the game's own
// image (gsdata + 0x18 ...), which the tables copy only at a refresh. A skill made or changed after the copy was an
// empty record to the game. Each pass now copies the skill's record, its profiles and its text row (string offsets
// re-aimed from our row, as bs_refresh_and_point does) into the tables.
static void bs_mirror_ns(const char *gs, int id, int row, int prof0, int nprof) {
    __try {
        if (!g_bs_mem || !gs || !bs_pointed() || gs != bs_image()) return;
        if (id > 0 && id < BS_RECS) memcpy(g_bs_rec + (size_t)NS_REC * id, gs + 0x18 + (size_t)NS_REC * id, NS_REC);
        for (int j = 0; j < nprof; j++) {
            int p = prof0 + j;
            if (p >= 0 && p < 896) memcpy(g_bs_prof + (size_t)NS_PROF * p, gs + NS_PROF_BASE + (size_t)NS_PROF * p, NS_PROF);
        }
        if (row > 0 && row < BS_ROWS && row < 4000) {
            const char *o = gs + NS_TEXT + 12 * row;
            char *e = g_bs_rows + 12 * row;
            int kind = *(const int *)o, a = *(const int *)(o + 4), b = *(const int *)(o + 8);
            *(int *)e = kind;
            *(int *)(e + 4) = a ? (int)((o + a) - e) : 0;
            *(int *)(e + 8) = b ? (int)((o + b) - e) : 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int bs_parse(const wchar_t *path) {
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) return 0;
    char line[640];
    int n = 0;
    while (g_bs_n < BS_MAX && fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        ListSkill s = {};
        s.school = s.cost = s.damage = -1; s.copies = 10; s.said = -9; s.prof = s.row = s.stro = -1;
        char opts[200] = "";
        int got = sscanf_s(line, "%d\t%d\t%39[^\t]\t%199[^\t]\t%199[^\r\n]", &s.id, &s.src, s.name, (unsigned)sizeof(s.name), s.desc, (unsigned)sizeof(s.desc), opts, (unsigned)sizeof(opts));
        if (got < 3 || s.id < BS_ID_LO || s.id > BS_ID_HI || (s.id >= 700 && s.id <= 741) || s.id == 750 || s.id == 751 || s.id == 752 ||
            s.src < 1 || s.src > 374) { logf("skill lists: line refused: %.60s", line); continue; }
        if (got < 4 || !strcmp(s.desc, "-")) s.desc[0] = 0;
        char *ctx = NULL;
        for (char *t = strtok_s(opts, " ", &ctx); t; t = strtok_s(NULL, " ", &ctx)) {
            int a, b;
            // v5.159 school 6 = the Illusion school (owner 2026-11-13; the game's school readers checked, cwo_school6.inl)
            if (sscanf_s(t, "school=%d", &a) == 1 && a >= 1 && a <= 6) s.school = a;
            else if (sscanf_s(t, "cost=%d", &a) == 1 && a >= 0 && a <= 10) s.cost = a;
            else if (sscanf_s(t, "damage=%d", &a) == 1 && a >= 0 && a <= 999) s.damage = a;
            else if (sscanf_s(t, "copies=%d", &a) == 1 && a >= 1 && a <= 99) s.copies = a;
            else if (sscanf_s(t, "sat=%d", &a) == 1 && a >= 1 && a <= 300) s.sat = a;          // v5.159 effect colour
            else if (sscanf_s(t, "val=%d", &a) == 1 && a >= 10 && a <= 300) s.val = a;
            else if (t[0] == 'r' && sscanf_s(t + 1, "%x=%d", &a, &b) == 2 && a >= 0x0E && a < 0x90 && !(a & 1) && s.nfo < 12) { s.fo[s.nfo] = a; s.fv[s.nfo] = b; s.nfo++; }
            else if (sscanf_s(t, "hit=%d", &a) == 1 && a > 0 && a <= 2000) s.hit = a;   // v5.49 hit sphere radius in cm
            else if (sscanf_s(t, "snd=%d", &a) == 1 && a > 0 && a < 4000) s.snd = a;    // v5.50 common.bin sound (profile +0x06)
            else if (sscanf_s(t, "fx=%d", &a) == 1 && a > 0 && a < 65000) s.fx = a;     // v5.51 effect request (profile +0x04)
        }
        bool twice = false;
        for (int k = 0; k < g_bs_n; k++) twice |= g_bs[k].id == s.id;
        for (int k = 0; k < g_ns_n; k++) twice |= g_ns[k].id == s.id;
        if (twice) { logf("skill lists: id %d is taken - line refused", s.id); continue; }
        g_bs[g_bs_n++] = s;
        n++;
    }
    fclose(f);
    return n;
}

// v5.110 (owner 2026-11-07: an imported skill list was taken out of both editions): <data>\CwoRemovedSkills.txt names
// the numbers it used. Those records are empty again, so every second their stock
// copies go to 0 and the arsenal guard empties arsenal slots that hold them - unless the number now belongs to a Skill
// Creator skill, a fusion or the Battle for Reality pack. (Both editions share one save, so both get the list.)
static short g_bs_rm[1200];
static int g_bs_rm_n;
static bool bfr_uses(int id);                                     // cwo_bfr.inl
static bool fu_uses(int id);                                      // cwo_fusion.inl
static bool bs_removed_now(int id) {
    bool hit = false;
    for (int i = 0; i < g_bs_rm_n && !hit; i++) hit = g_bs_rm[i] == id;
    if (!hit) return false;
    for (int k = 0; k < g_ns_n; k++) if (g_ns[k].id == id) return false;
    for (int k = 0; k < g_bs_n; k++) if (g_bs[k].id == id) return false;
    return !fu_uses(id) && !bfr_uses(id);
}
static void bs_removed_load() {
    wchar_t path[MAX_PATH];
    swprintf_s(path, MAX_PATH, L"%sCwoRemovedSkills.txt", g_dir);
    g_bs_rm_n = 0;
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) {
        f = NULL;
        if (!f) return;
    }
    char line[64];
    while (g_bs_rm_n < 1200 && fgets(line, sizeof(line), f)) {
        int id = atoi(line);
        if (id >= 375 && id < 1200 && (id < 700 || id > 752)) g_bs_rm[g_bs_rm_n++] = (short)id;
    }
    fclose(f);
    logf("removed skills: %d numbers (%ls) - their stock and arsenal slots are cleared", g_bs_rm_n, path);
}
static void bs_unstock_removed(ULONG_PTR pr) {
    if (!g_bs_rm_n || !pr) return;
    unsigned char *stock = *(unsigned char **)(pr + 0x3a38);
    short *owned = *(short **)(pr + 0x3a40);
    if (!stock || !owned) return;
    bool wide = stock == g_bi_stock[0];                           // our 1,200-wide arrays (else the game's 1,000)
    int cleared = 0;
    for (int i = 0; i < g_bs_rm_n; i++) {
        int id = g_bs_rm[i];
        if (id >= 1000 && !wide) continue;
        if ((stock[id] || owned[id * 2] || owned[id * 2 + 1]) && bs_removed_now(id)) {
            stock[id] = 0; owned[id * 2] = 0; owned[id * 2 + 1] = 0; cleared++;
        }
    }
    if (cleared) logf("skill lists: %d removed skills taken out of the stock", cleared);
}
static void bs_load() {                                         // worker start, after ns_load
    wchar_t path[MAX_PATH];
    g_bs_n = 0;
    // v5.159 the Illusion school (owner 2026-11-13): records 753-827, school 6, the same line format
    swprintf_s(path, MAX_PATH, L"%sCwoIllusion.txt", g_dir);
    int m = bs_parse(path);
    logf("illusion school: %d skills from CwoIllusion.txt", m);
    bs_removed_load();
}
// v5.159 a number one of the loader's lists (the Illusion school's) writes into the big tables: real to the game once they point at
// them (the arsenal guard keeps it, cwo_arsguard.inl)
static bool bs_listed(int id) {
    if (id < 753 || id >= BS_RECS || g_bs_dead) return false;
    for (int k = 0; k < g_bs_n; k++) if (g_bs[k].id == id) return bs_pointed();
    return false;
}

static char *bs_record(int id) { return g_bs_rec + (size_t)NS_REC * id; }

// one list skill into our tables (the game's rules as ns_apply_one: only empty places, record last)
static int bs_apply_one(int k) {
    ListSkill &s = g_bs[k];
    char *rec = bs_record(s.id);
    // v5.159.5 (in game 2026-11-13: with the Battle for Reality pack on, Place of Invisibility in our tables is the pack's
    // Environment version): a list skill is a copy of the GAME's skill - its record and profile come from the game's own
    // skill data image (0x1404c5240; the pack and the Skill Creator write into our tables / records 376+, never there)
    const char *src = g_bs_test_img ? bs_record(s.src) : bs_image() + 0x18 + (size_t)NS_REC * s.src;
    if (*(short *)(rec + 2) == (short)s.id && *(short *)rec >= BS_ROW0) return 0;   // ours already
    if (!ns_zero(rec + 2, NS_REC - 2)) { if (s.said != -1) logf("list skill %d '%s': record is not empty - left out", s.id, s.name); return -1; }
    if (*(short *)(src + 2) == 0) { if (s.said != -1) logf("list skill %d: source %d is empty - left out", s.id, s.src); return -1; }
    // v5.159 a recoloured effect needs the effect index in first (as the Skill Creator's skills, cwo_newskills.inl)
    bool colour = (s.sat || s.val) && !g_bs_test_img && g_fx_state == 1;
    if (colour && !fx_ei_ready()) return -2;
    if (s.row < 0 && (g_bs_prof_next >= BS_PROFS || g_bs_row_next >= BS_ROWS || g_bs_str_used + 260 > BS_ARENA)) { if (s.said != -1) logf("list skill %d: the tables are full", s.id); return -1; }
    // its own slots, given once and kept (a refresh copies only the game's records / profiles / rows, so they survive)
    if (s.row < 0) { s.prof = g_bs_prof_next++; s.row = g_bs_row_next++; s.stro = g_bs_str_used; g_bs_str_used += (int)((strlen(s.name) + strlen(s.desc) + 2 + 3) & ~3); }
    char r[NS_REC];
    memcpy(r, src, NS_REC);
    short gp = *(short *)(src + 0x14);                         // one profile: the source's ground profile, copied
    if (gp > 0) {
        char *np = g_bs_prof + (size_t)NS_PROF * s.prof;
        const char *sp = g_bs_test_img || gp >= 896 ? g_bs_prof + (size_t)NS_PROF * gp : bs_image() + NS_PROF_BASE + (size_t)NS_PROF * gp;
        memcpy(np, sp, NS_PROF);
        if (s.snd) *(short *)(np + 0x06) = (short)s.snd;          // v5.50 the skill's own common.bin sound (FUN_14025a3e0: profile +0x06)
        if (s.fx) *(short *)(np + 0x04) = (short)s.fx;            // v5.51 the skill's own effect request (FUN_1401272b0: profile +0x04)
        // v5.159 (the Illusion school's smoke silver): the effect through a recoloured number of the loader's (cwo_fxcolour.inl);
        // one owner for the whole list, so skills built from the same effect share one recoloured copy
        short *req = (short *)(np + 0x04);
        if (colour && *req > 0 && *req < FX_REQ0) {
            FxColour c = {0, s.sat == 1 ? 0 : (s.sat ? s.sat : 100), s.val ? s.val : 100, 0};
            int cr = fx_colour_req(BS_FX_OWNER, *req, c);
            if (cr > 0) *req = (short)cr;
        }
        *(short *)(r + 0x14) = (short)s.prof;
    }
    *(short *)(r + 0x16) = 0;                                   // no air version, no combo steps
    for (int o = 0x18; o <= 0x1E; o += 2) *(short *)(r + o) = 0;
    int row = s.row;
    char *str = g_bs_str + s.stro;
    size_t nl = strlen(s.name), dl = strlen(s.desc);
    memcpy(str, s.name, nl + 1);
    memcpy(str + nl + 1, s.desc, dl + 1);
    char *entry = g_bs_rows + 12 * row;
    *(int *)entry = 9001; *(int *)(entry + 4) = (int)(str - entry); *(int *)(entry + 8) = (int)(str + nl + 1 - entry);
    *(short *)(r + 0x00) = (short)row;
    *(short *)(r + 0x02) = (short)s.id;
    *(short *)(r + 0x06) = (short)s.id;
    if (*(short *)(src + 0x32) == (short)s.src) *(short *)(r + 0x32) = (short)s.id;
    if (s.school > 0) *(short *)(r + 0x12) = (short)s.school;
    if (s.cost >= 0) *(short *)(r + 0x28) = (short)s.cost;
    if (s.damage >= 0) *(short *)(r + 0x58) = (short)s.damage;
    for (int i = 0; i < s.nfo; i++) if (s.fo[i] != 0x32) *(short *)(r + s.fo[i]) = (short)s.fv[i];
    memcpy(rec + 2, r + 2, NS_REC - 2);
    memcpy(rec, r, 2);
    if (s.hit) hb_set(s.id, (float)s.hit / 10.0f);              // v5.49 (record index = id for our records)
    return 1;
}

static void bs_stock(int id, int copies) {                      // the id maps and the stock (at least `copies`)
    if (id >= 1000 && g_bi_state != 1) return;                  // v5.47 ids above 999 need the big-id patches
    int *first = (g_bi_state == 1 ? g_bi_mapA : (int *)(g_base + RVA_FIRST_MAP)) + id;
    int *last = (g_bi_state == 1 ? g_bi_mapB : (int *)(g_base + RVA_LAST_MAP)) + id;
    if (*first == -1) *first = id;
    if (*last == -1) *last = id;
    ULONG_PTR mgr = *(ULONG_PTR *)(g_base + RVA_ARSENAL);
    if (!mgr) return;
    unsigned char *stock = *(unsigned char **)(mgr + 0x3a38);   // player 1's stock pointer (ours once mirrored)
    unsigned char *supp = *(unsigned char **)(mgr + 0x3a40);
    if (!stock || !supp) return;
    if (id >= 1000 && stock != g_bi_stock[0]) return;           // not yet switched to our 1,200-wide arrays
    stock += id;
    if (*stock == 0 || *stock == 0xFF || *stock < copies) {
        *stock = (unsigned char)copies;
        short *sup = (short *)(supp + 4 * id);
        if (sup[0] == 0 && sup[1] == 0) sup[1] = 2;
    }
}

// every frame (game thread): the tables pointed at ours; once a second the list skills written when missing
static void bs_unstock_system() {                                 // v5.85 records 750 / 751 out of the profile's stock
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 1000;
    __try {
        ULONG_PTR pr = *(ULONG_PTR *)(g_base + 0x4bec10);
        if (!pr) return;
        unsigned char *stock = *(unsigned char **)(pr + 0x3a38);
        short *owned = *(short **)(pr + 0x3a40);
        if (!stock || !owned) return;
        bs_unstock_removed(pr);                                   // v5.110 the removed list skills
        for (int id = 750; id <= 751; id++) {
            if (stock[id] || owned[id * 2 + 1]) {
                logf("big skills: record %d (the system copy of %s) taken out of the stock (had %d)", id, id == 750 ? "Jump" : "Dash", stock[id]);
                stock[id] = 0; owned[id * 2] = 0; owned[id * 2 + 1] = 0;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void bs_step() {
    bs_unstock_system();
    if (g_bs_dead || (g_bs_n <= 0 && !g_bs_test_img && bfr_count() <= 0)) return;   // v5.58 the BFR edition has no skill list
    char *img = bs_image();
    if (!img || *(unsigned *)img != 0x44004) return;
    __try {
        if (!g_bs_mem && !bs_alloc()) { if (!g_bs_on) logf("big skills: no memory - off"); g_bs_on = true; return; }
        if (!bs_pointed()) {
            bs_refresh_and_point();
            if (!g_bs_on) logf("big skills: records / profiles / text rows at %p / %p / %p (ids to %d, profiles to %d, rows to %d)", (void *)g_bs_rec, (void *)g_bs_prof, (void *)g_bs_rows, BS_RECS - 1, BS_PROFS - 1, BS_ROWS - 1);
            else logf("big skills: the game installed its skill data again - tables refreshed and pointed back (%d)", g_bs_repoints);
            g_bs_on = true;
            // our own writes are in the copies: the list skills the refresh cleared (ids below 753) are written again
            // below into their own kept slots; the Skill Creator's live in the image
        }
        static ULONGLONG next;
        ULONGLONG now = GetTickCount64();
        if (!g_bs_test_img && now < next) return;
        next = now + 1000;
        int wrote = 0;
        for (int k = 0; k < g_bs_n; k++) {
            if (!g_bs_test_img && bfr_owns(g_bs[k].id)) continue;   // v5.56 the Battle for Reality skill there while the pack is on
            int r = bs_apply_one(k);
            if (r > 0) wrote++;
            g_bs[k].said = r;
            if (r >= 0 && !g_bs_test_img) bs_stock(g_bs[k].id, g_bs[k].copies);
        }
        if (wrote) logf("skill lists: %d written (%d in all, profiles %d-%d, rows %d-%d, %d KB of text)", wrote, g_bs_n, BS_PROF0, g_bs_prof_next - 1, BS_ROW0, g_bs_row_next - 1, g_bs_str_used / 1024);
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("big skills: fault - off"); g_bs_n = 0; if (!g_bs_test_img) g_bs_dead = true; }
}

static int bs_self_test() {                                     // the relocation and one skill on a stand-in image
    size_t tables = (size_t)BS_RECS * NS_REC + (size_t)BS_PROFS * NS_PROF + (size_t)BS_ROWS * 12 + BS_ARENA;
    char *img = (char *)calloc(1, 0x44004 + tables);           // image + tables in one block: the rows' offsets stay 32-bit
    if (!img) return -80;
    *(unsigned *)img = 0x44004; *(int *)(img + 8) = 753; *(unsigned *)(img + 0x14) = 374; *(int *)(img + 0x34008) = 2;
    char *src = img + 0x18 + NS_REC * 61;
    *(short *)(src + 2) = 61; *(short *)(src + 0x12) = 3; *(short *)(src + 0x14) = 61; *(short *)(src + 0x28) = 2;
    *(short *)(img + NS_PROF_BASE + NS_PROF * 61 + 4) = 61;
    strcpy_s(img + 0x40000, 32, "Bullet of Fire");             // row 1 names it from the image
    *(int *)(img + NS_TEXT + 12) = 9001; *(int *)(img + NS_TEXT + 16) = (int)((img + 0x40000) - (img + NS_TEXT + 12));
    int keep_n = g_bs_n;
    ListSkill s = {}; s.prof = s.row = s.stro = -1; s.id = 800; s.src = 61; s.school = 2; s.cost = 3; s.damage = 5; s.copies = 10; s.fo[0] = 0x0E; s.fv[0] = 1; s.nfo = 1;
    strcpy_s(s.name, "Test Skill"); strcpy_s(s.desc, "Test description");
    g_bs[0] = s; g_bs_n = 1;
    g_bs_test_img = img; g_bs_mem = NULL; g_bs_on = false;
    bs_step();
    bool pointed = bs_pointed() && *(int *)(img + 8) == BS_RECS && *(unsigned *)(img + 0x14) == BS_ID_HI;
    char *e1 = g_bs_rows + 12;
    bool rows_ok = pointed && !strcmp(e1 + *(int *)(e1 + 4), "Bullet of Fire");
    char *r = g_bs_rec + NS_REC * 800;
    short row = *(short *)r, pr = *(short *)(r + 0x14);
    char *e = g_bs_rows + 12 * row;
    bool rec_ok = *(short *)(r + 2) == 800 && *(short *)(r + 0x12) == 2 && *(short *)(r + 0x28) == 3 && *(short *)(r + 0x58) == 5 && *(short *)(r + 0x0E) == 1 &&
                  pr == BS_PROF0 && *(short *)(g_bs_prof + NS_PROF * pr + 4) == 61 && row == BS_ROW0 &&
                  !strcmp(e + *(int *)(e + 4), "Test Skill") && !strcmp(e + *(int *)(e + 8), "Test description");
    bs_step();                                                 // again: nothing more
    bool once = g_bs_prof_next == BS_PROF0 + 1 && g_bs_row_next == BS_ROW0 + 1;
    *(ULONG_PTR *)bs_cell(RVA_P_REC) = 0;                      // the game installed its data again
    bs_step();
    bool again = bs_pointed() && *(short *)(g_bs_rec + NS_REC * 800 + 2) == 800 && g_bs_repoints == 2;
    free(img); g_bs_mem = NULL; g_bs_test_img = NULL; g_bs_on = false; g_bs_n = keep_n; g_bs_repoints = 0;
    g_bs_prof_next = BS_PROF0; g_bs_row_next = BS_ROW0; g_bs_str_used = 0;
    for (int k = 0; k < BS_MAX; k++) { g_bs[k].prof = g_bs[k].row = g_bs[k].stro = -1; }
    logf("self-test: big skills pointed %s, rows re-aimed %s, list skill %s, written once %s, re-pointed %s", pointed ? "ok" : "FAILED",
         rows_ok ? "ok" : "FAILED", rec_ok ? "ok" : "FAILED", once ? "ok" : "FAILED", again ? "ok" : "FAILED");
    return pointed && rows_ok && rec_ok && once && again ? 1 : -80;
}
