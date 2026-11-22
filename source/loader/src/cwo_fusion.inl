// ---- v5.17 Skill Fuser (owner 2026-10-27). Included by cwo_loader.cpp after cwo_bigstage.inl.
//
// A fusion = the base skill's record (its move, hit, school, capsule type and its own effect) under a new number, plus the
// added skill's effect started on the same move, so both play at once. Cost = the higher + 1 (10 at most), damage = the
// higher + 1 (the Fuser can change both). Close range = attack family (+0x52 = 0) with attack kind (+0x7A) 12 (sword /
// blade) or 13 (close-range melee): 48 skills, 1,128 pairs (07_Combat_and_Skills/skill_fusion_2026-10-27).
// Game facts (Ghidra full-pass batch_065 via pcode.py, capstone; tools/build_fusion_hook.py):
//   FUN_14025a3e0(action, state) starts a skill move: skill *(int *)(action + 0x5c); its effect starts in
//   FUN_140259140(action, profile +0x04, 0, 0) = FUN_14018d2f0(owner *(*(action + 0xbd0) + 0x14), request, part 0, matrix
//   action + 0xbe0) -> FUN_14018a040(*0x1405bb2e0, ...), handle kept at action + 0xc20 (the previous one is stopped
//   there first, so the added effect is started beside it, not through it). The detour at its entry hands the action to
//   fus_move_start; the next frame starts the added skill's part 0 the same way and keeps it with the base's effect.
//   Places in the skill image (SKILL_SLOT_MAP_2026-10-21.md; measured on gsdata_en.dat 2026-10-27): records 375-699 are
//   empty; text rows 0-2113 are used, so fusions take rows 2385 .. 2300 and 2199 .. 2130 (the Skill Creator keeps 2200 +
//   k, the first new-skill test used 2120); their names live at +0x41800 (32 bytes each), one shared description at +0x43800,
//   in the zero padding after the Skill Creator's strings (+0x3B000 + 0x100 k, k < 100) and before the file's last bytes
//   (+0x43F30). Every place is written only when it is empty or already ours (ns_put). Profiles are the base skill's own.
//   Online (*0x1404bc9d4 = 4 or 5): the detour does nothing.
#include "cwo_fusion_hook.h"

static const int FU_MAX = 156, FU_STR0 = 0x41800, FU_STR = 0x20, FU_DESC = 0x43800, FU_RECS = 753;
static const char FU_DESC_TEXT[] = "A fusion: this move carries both skills' effects (Skill Fuser).";
static int fu_row(int k) { return k < 86 ? 2385 - k : 2199 - (k - 86); }
struct Fusion { int id, base, add, cost, damage; char name[32]; int dirty, said; };
static Fusion g_fu[FU_MAX];
static int g_fu_n;
static bool g_fu_loaded;
static unsigned char g_fu_is[FU_RECS];                          // record -> 1 when it is one of our fusions (the detour reads it)
static short g_fu_add[FU_RECS];                                 // record -> its added skill
static ULONGLONG g_fu_next;
static char *g_fu_test_gs;                                      // self-test: stand-in skill image

static void fu_path(wchar_t *p, bool installed) {
    if (installed) {
        GetModuleFileNameW(NULL, p, MAX_PATH);
        wchar_t *slash = wcsrchr(p, L'\\');
        if (slash) slash[1] = 0;
        wcscat_s(p, MAX_PATH, L"CwoFusions.txt");
    } else swprintf_s(p, MAX_PATH, L"%sCwoFusions.txt", g_dir);
}
static bool fu_uses(int id) { for (int k = 0; k < g_fu_n; k++) if (g_fu[k].id == id) return true; return false; }
static void fu_index() {
    memset(g_fu_is, 0, sizeof(g_fu_is));
    for (int k = 0; k < g_fu_n; k++) { g_fu_is[g_fu[k].id] = 1; g_fu_add[g_fu[k].id] = (short)g_fu[k].add; }
}
static int fu_read(const wchar_t *path) {
    FILE *f = NULL;
    if (cwo_wfopen(&f, path, L"r") != 0 || !f) return -1;
    char line[256];
    int got = 0;
    while (g_fu_n < FU_MAX && fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        Fusion u = {};
        if (sscanf_s(line, "%d\t%d\t%d\t%31[^\t]\t%d\t%d", &u.id, &u.base, &u.add, u.name, (unsigned)sizeof(u.name), &u.cost, &u.damage) != 6 ||
            u.id < 376 || u.id > 699 || u.base < 1 || u.base > 699 || u.add < 1 || u.add > 699 || u.base == u.add ||
            u.cost < 0 || u.cost > 10 || u.damage < 0 || u.damage > 99) { logf("fusions: line refused: %.60s", line); continue; }
        bool twice = false;
        for (int k = 0; k < g_fu_n; k++) twice |= g_fu[k].id == u.id;
        for (int k = 0; k < g_ns_n; k++) twice |= g_ns[k].id == u.id;
        if (twice) { logf("fusions: skill %d is taken - line refused", u.id); continue; }
        g_fu[g_fu_n++] = u;
        got++;
    }
    fclose(f);
    return got;
}
static void fu_save() {
    wchar_t p[MAX_PATH], t[MAX_PATH];
    fu_path(p, false);
    swprintf_s(t, MAX_PATH, L"%s.tmp", p);
    FILE *f = NULL;
    if (cwo_wfopen(&f, t, L"w") != 0 || !f) { logf("fusions: cannot write CwoFusions.tmp"); return; }
    fprintf(f, "# fusion id\tbase\tadded\tname\tcost\tdamage (the Skill Fuser writes this file)\n");
    for (int k = 0; k < g_fu_n; k++)
        fprintf(f, "%d\t%d\t%d\t%s\t%d\t%d\n", g_fu[k].id, g_fu[k].base, g_fu[k].add, g_fu[k].name, g_fu[k].cost, g_fu[k].damage);
    bool ok = fclose(f) == 0;
    if (ok) MoveFileExW(t, p, MOVEFILE_REPLACE_EXISTING); else DeleteFileW(t);
}
// the owner's list (data folder) when there is one; otherwise the installed starter set, copied there
static void fu_load() {
    g_fu_loaded = true;
    g_fu_n = 0;
    wchar_t p[MAX_PATH];
    fu_path(p, false);
    int n = fu_read(p);
    if (n < 0) {
        fu_path(p, true);
        n = fu_read(p);
        if (n > 0) fu_save();
        logf("fusions: %d from the installed starter set", n < 0 ? 0 : n);
    } else logf("fusions: %d in the Skill Fuser's list", n);
    fu_index();
    static const char *SCHOOL[7] = {"Aura Particle", "Psycho", "Optical", "Nature", "Ki", "Faith", "Illusion"};   // v5.168 school 0 = the Aura Particle school, 6 = Illusion
    for (int k = 0; k < g_fu_n && g_sk_count < SK_MAX; k++) {     // the Stage Creator's capsule list knows them
        SkillRow &r = g_sk[g_sk_count++];
        r.id = g_fu[k].id;
        sprintf_s(r.name, "%.31s", g_fu[k].name);
        strcpy_s(r.school, "fusion");
        sprintf_s(r.cost, "%d", g_fu[k].cost);
        (void)SCHOOL;
    }
}

static void fu_clear(char *gs, int k) {                          // our places of fusion k emptied (record keeps its +0x00)
    Fusion &u = g_fu[k];
    int row = fu_row(k);
    char *rec = gs + 0x18 + NS_REC * u.id;
    if (*(short *)rec == (short)row && *(short *)(rec + 2) == (short)u.id) memset(rec + 2, 0, NS_REC - 2);
    char *entry = gs + NS_TEXT + 12 * row, *str = gs + FU_STR0 + FU_STR * k;
    if (*(int *)entry == 9001 && entry + *(int *)(entry + 4) == str) memset(entry, 0, 12);
    memset(str, 0, FU_STR);
}
// v5.116 (owner 2026-11-08): a skill
// as the game reads it - the record table *0x1404bdc50 (the loader's big table while the BFR pack or a skill list is on, else
// the image's +0x18), so the Battle for Reality skills (375-465) and their revisions fuse too
static const char *fu_live_rec(char *gs, int id) {
    if (g_fu_test_gs) return gs + 0x18 + NS_REC * id;
    __try {
        const char *t = *(const char **)(g_base + 0x4bdc50);
        const char *hdr = *(const char **)(g_base + 0x4bdc40);           // v5.148.15: 0 at start-up (it read 0x4: a fault each launch)
        int nrec = hdr ? *(const int *)(hdr + 4) : 0;
        if (t && id > 0 && id < nrec) return t + NS_REC * (ULONG_PTR)id;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return gs + 0x18 + NS_REC * id;
}
static int fu_apply_one(char *gs, int k) {
    Fusion &u = g_fu[k];
    int row = fu_row(k);
    char *rec = gs + 0x18 + NS_REC * u.id;
    const char *src = fu_live_rec(gs, u.base);
    if (u.dirty) { fu_clear(gs, k); u.dirty = 0; }
    if (*(short *)rec == (short)row && *(short *)(rec + 2) == (short)u.id) return 0;   // in place
    if (!ns_zero(rec + 2, NS_REC - 2)) { if (u.said >= 0) logf("fusion %d: record is not empty - left out", u.id); return -1; }
    if (*(short *)(src + 2) == 0) { if (u.said >= 0) logf("fusion %d: base %d is empty - left out", u.id, u.base); return -1; }
    char desc[0x80] = {}, name[FU_STR] = {};
    memcpy(desc, FU_DESC_TEXT, sizeof(FU_DESC_TEXT));
    memcpy(name, u.name, strlen(u.name) < FU_STR - 1 ? strlen(u.name) : FU_STR - 1);
    char *entry = gs + NS_TEXT + 12 * row, *str = gs + FU_STR0 + FU_STR * k, *dsc = gs + FU_DESC;
    int ent[3] = {9001, (int)(str - entry), (int)(dsc - entry)};
    if (ns_put(dsc, desc, sizeof(desc), "description", u.id) < 0 || ns_put(str, name, FU_STR, "name", u.id) < 0 ||
        ns_put(entry, ent, 12, "text row", u.id) < 0) return -1;
    char r[NS_REC];
    memcpy(r, src, NS_REC);                                    // the base skill: move, hit, school, capsule, its profiles
    *(short *)(r + 0x00) = (short)row;
    *(short *)(r + 0x02) = (short)u.id;
    *(short *)(r + 0x06) = (short)u.id;
    if (*(short *)(src + 0x32) == (short)u.base) *(short *)(r + 0x32) = (short)u.id;   // a combo links to itself
    *(short *)(r + 0x28) = (short)u.cost;
    *(short *)(r + 0x58) = (short)u.damage;
    memcpy(rec + 2, r + 2, NS_REC - 2);                        // the record last: the game never sees half a skill
    memcpy(rec, r, 2);
    if (*(unsigned *)(gs + 0x14) < (unsigned)u.id) *(unsigned *)(gs + 0x14) = u.id;
    logf("fusion %d '%s' (%d + %d's effect) written: text row %d", u.id, u.name, u.base, u.add, row);
    return 1;
}
static void fu_records_step() {                                 // every second, like the new skills (the game re-reads its data)
    if (!g_fu_loaded) fu_load();
    if (g_fu_n <= 0) return;
    ULONGLONG now = GetTickCount64();
    if (!g_fu_test_gs && now < g_fu_next) return;
    g_fu_next = now + 1000;
    char *gs = g_fu_test_gs ? g_fu_test_gs : *(char **)(g_base + RVA_GSDATA);
    if (!gs || *(unsigned *)gs != 0x44004) return;
    for (int k = 0; k < g_fu_n; k++) {
        int r = fu_apply_one(gs, k);
        g_fu[k].said = r;
        if (r >= 0 && !g_fu_test_gs) { bs_mirror_ns(gs, g_fu[k].id, fu_row(k), 0, 0); ns_maps_and_stock(g_fu[k].id); }   // v5.112 into the big tables too (BFR on)
    }
}

// ---- the move start: the detour's call (game thread, inside the skill engine) and the next frame's effect
struct FuPend { ULONG_PTR action; int id, tries; };
struct FuLive { ULONG_PTR action; int id, frames; unsigned base_h, add_h; };
static FuPend g_fu_pend[16];
static FuLive g_fu_live[16];
static int g_fu_pend_n, g_fu_live_n;
static volatile LONG g_fu_seen;                                 // moves of fusions seen (log / self-test)
extern "C" void __fastcall fus_move_start(ULONG_PTR action, int state) {
    (void)state;
    __try {
        int id = *(int *)(action + 0x5c);
        rc_on_skill(action, id);                                // v5.21 Records: your skills
        if (id <= 0 || id >= FU_RECS || !g_fu_is[id]) return;
        if (!g_fu_test_gs) { int nm = *(int *)(g_base + RVA_NETMODE); if (nm == 4 || nm == 5) return; }   // online: nothing
        InterlockedIncrement(&g_fu_seen);
        if (g_fu_pend_n < 16) g_fu_pend[g_fu_pend_n++] = {action, id, 0};
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void fu_live_step(FxGame &g, bool in_match) {
    if (!in_match) {
        for (int i = 0; i < g_fu_live_n; i++) if (fx_alive(g, g_fu_live[i].add_h)) g.stop(g_fu_live[i].add_h);
        g_fu_live_n = g_fu_pend_n = 0;
        return;
    }
    __try {
        for (int i = 0; i < g_fu_pend_n; i++) {                // start the added skill's part 0 beside the base's effect
            FuPend &p = g_fu_pend[i];
            bool drop = ++p.tries > 90;                          // ~1.5 s of loading at most
            if (!drop) {
                int add = g_fu_add[p.id];
                short prof = *(short *)(g.skills + 0x14 + (ULONG_PTR)add * 0x90);
                int req = prof > 0 ? g.effect_of(prof, add) : 0;
                ULONG_PTR fighter = *(ULONG_PTR *)(p.action + 0xbd0);
                if (req <= 0 || !fighter || g_fu_live_n >= 16) drop = true;
                else if (!g_fu_test_gs && !fx_loaded(req)) {      // v5.194.2 asked for at the use (as the game does for a card)
                    typedef INT64(__fastcall * PFN_FX_LOAD)(ULONG_PTR, int, int);
                    if (p.tries == 1) { ((PFN_FX_LOAD)(g_base + 0x189e80))(g.mgr, req, 0); logf("Skill Fuser: skill %d - the added effect %d asked for at its use", p.id, req); }
                }
                else {
                    // v5.116 any skill: part 0 (hand / body during the move); a skill without one (a shot, a status) its
                    // projectile part (1), then its hit part (3) - each tried ~0.5 s (the pack may still be loading)
                    int part = p.tries < 30 ? 0 : p.tries < 60 ? 1 : 3;
                    unsigned h = g.start(g.mgr, *(int *)(fighter + 0x14), req, part, (const float *)(p.action + 0xbe0), 0);
                    if (h != FX_NONE) {
                        g_fu_live[g_fu_live_n++] = {p.action, p.id, 0, *(unsigned *)(p.action + 0xc20), h};
                        drop = true;
                    }
                }
            }
            if (drop) g_fu_pend[i--] = g_fu_pend[--g_fu_pend_n];
        }
        for (int i = 0; i < g_fu_live_n; i++) {                // it lasts as long as the base's own effect
            FuLive &l = g_fu_live[i];
            l.frames++;
            bool base_gone = l.base_h == FX_NONE ? l.frames > 90 : (*(unsigned *)(l.action + 0xc20) != l.base_h || !fx_alive(g, l.base_h));
            bool end = *(int *)(l.action + 0x5c) != l.id || base_gone || !fx_alive(g, l.add_h);
            if (end) {
                if (fx_alive(g, l.add_h)) g.stop(l.add_h);
                g_fu_live[i--] = g_fu_live[--g_fu_live_n];
            } else g.move(l.add_h, (const float *)(l.action + 0xbe0));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_fu_live_n = g_fu_pend_n = 0; }
}
// v5.192.2 (owner 2026-11-21; his
// log 22:36:28: "effect load during the fight: request 101" then thousands of "crash shield: a freed effect texture"): the added
// skill's effect was first loaded when the fusion was cast, mid-fight. The game's effect load FUN_140189e80 makes room by
// evicting the least-recently-used resident effect (FUN_14018cf80; recorded 2026-11-13, cwo_stagefx.inl v5.148.11) - here the
// fusion's own base effect, whose projectile was flying: its textures were freed and drawn blank. Now, as for the stage
// effects: when a match starts (match state *0x1404bc950 0 -> 1..5, before the fight, no particles yet) every fusion's added
// effect is loaded at once (those in an arsenal first, 12 at most), so nothing is loaded while the fight is on.
static bool fu_in_an_arsenal(int id);
static void fu_preload(FxGame &g) { (void)g; }
// v5.194.2 (in game 2026-11-22, Effect Lab match): an effect loaded AHEAD of its use (5.192.2-5.194.1: at the match start, at
// the fight's first frame, three at once or one at a time) drew freed textures at its first use - Double Lasers' effect 45,
// loaded 30 s before, 4 runs out of 4 - while effects asked for right at the use (Laser 42, Auto Shell 46: asked, loaded in
// ~30 ms, used) were clean, 3 out of 3. So nothing is loaded ahead any more: the added effect is asked for when the fusion's
// move starts (fu_live_step) and started once the game's table shows it loaded.
// v5.192.5 diagnosis (in game 2026-11-22: 698 in the deck, fresh game - the 2nd cast still frees effect textures, no mid-fight
// load logged): while the fight is on (*0x1404bc950 == 6) log every change of the game's effect loader table (entries 0x88 at
// *(loader + 0xC0), count +0xC8: +0 kind (short), +8 request, +0x14 last use, +0x18 data) - read only, 80 lines a session
static void fu_fxtable_watch() {
    static int last_req[2048], last_kind[2048]; static ULONG_PTR last_data[2048]; static unsigned last_cnt; static bool armed; static int said;
    int ph = 0; __try { ph = *(int *)(g_base + 0x4bc950); } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (ph != 6) { armed = false; return; }
    __try {
        ULONG_PTR ld = *(ULONG_PTR *)(g_base + RVA_FX_LOADER);
        char *ent = ld ? *(char **)(ld + 0xC0) : NULL;
        unsigned cnt = ld ? *(unsigned *)(ld + 0xC8) : 0;
        if (!ent || cnt > 2048) return;
        for (unsigned e = 0; e < cnt; e++) {
            int k = *(short *)(ent + 0x88 * e), r = *(int *)(ent + 0x88 * e + 8); ULONG_PTR dt = *(ULONG_PTR *)(ent + 0x88 * e + 0x18);
            if (armed && (e >= last_cnt || k != last_kind[e] || r != last_req[e] || dt != last_data[e]) && said < 80) {
                said++;
                logf("effect table (fight): slot %u request %d -> %d, kind %d -> %d, data %p -> %p", e, e < last_cnt ? last_req[e] : -1, r,
                     e < last_cnt ? last_kind[e] : -1, k, (void *)(e < last_cnt ? last_data[e] : 0), (void *)dt);
            }
            last_req[e] = r; last_kind[e] = k; last_data[e] = dt;
        }
        if (armed && cnt < last_cnt && said < 80) { said++; logf("effect table (fight): count %u -> %u", last_cnt, cnt); }
        last_cnt = cnt; armed = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void fu_step(bool in_match) {                            // CwoOverlayFrame
    fu_records_step();
    fu_fxtable_watch();
    FxGame g;
    if (fx_game(&g)) { fu_preload(g); fu_live_step(g, in_match); }
}

// ---- the detour: placed at load, before any game code runs (all or nothing)
static int g_fu_hook_state;                                     // 1 on, -1 refused
static void fu_install() {
    if (memcmp((const void *)(g_base + FUS_SITE_RVA), FUS_SITE, sizeof(FUS_SITE))) {
        g_fu_hook_state = -1;
        logf("Skill Fuser: the skill move start is not the expected code - fusions show the base skill's effect only");
        return;
    }
    ULONG_PTR stub = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    long long d = (long long)stub - (long long)(g_base + FUS_SITE_RVA + 5);
    if (!stub || d < INT_MIN || d > INT_MAX) { g_fu_hook_state = -1; logf("Skill Fuser: no memory within reach for the detour"); return; }
    memcpy((void *)stub, FUS_STUB, sizeof(FUS_STUB));
    ULONG_PTR back = g_base + FUS_BACK_RVA, fn = (ULONG_PTR)&fus_move_start;
    memcpy((void *)(stub + FUS_BACK_AT), &back, 8);
    memcpy((void *)(stub + FUS_FN_AT), &fn, 8);
    unsigned char j[sizeof(FUS_SITE)] = {0xE9};
    int rel = (int)d;
    memcpy(j + 1, &rel, 4);
    for (size_t i = 5; i < sizeof(j); i++) j[i] = 0x90;
    DWORD old;
    if (!VirtualProtect((void *)(g_base + FUS_SITE_RVA), sizeof(j), PAGE_EXECUTE_READWRITE, &old)) { g_fu_hook_state = -1; return; }
    memcpy((void *)(g_base + FUS_SITE_RVA), j, sizeof(j));
    VirtualProtect((void *)(g_base + FUS_SITE_RVA), sizeof(j), old, &old);
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    g_fu_hook_state = 1;
    logf("Skill Fuser: the skill move start calls the loader (stub %p)", (void *)stub);
}

// ---------------------------------------------------------------- the Skill Fuser page (Edgar's Dream)
static int g_fu_close[1000], g_fu_close_n;                       // v5.116 every skill (was: the 48 close-range skills)
static ULONGLONG g_fu_list_t;
static int g_fu_row, g_fu_bi, g_fu_ai = 1, g_fu_cost = -1, g_fu_dmg = -1, g_fu_sel, g_fu_del;
static bool fu_is_close(const char *gs, int id) {
    const char *r = gs + 0x18 + NS_REC * id;
    short kind = *(const short *)(r + 0x7A);
    return *(const short *)(r + 2) != 0 && *(const short *)(r + 0x52) == 0 && (kind == 12 || kind == 13);
}
static void fu_close_list() {
    // v5.116 every skill the game has (any school, any kind: attack, defense, status, environment, special, erase), sorted by
    // the number the game shows (record +0x02; 700+ = the bosses' / system's own), fusions themselves left out; rebuilt every
    // 3 s while the page is used (the BFR pack switch changes the table)
    ULONGLONG now = GetTickCount64();
    if (g_fu_close_n && !g_fu_test_gs && now - g_fu_list_t < 3000) return;
    g_fu_list_t = now;
    char *gs = g_fu_test_gs ? g_fu_test_gs : *(char **)(g_base + RVA_GSDATA);
    if (!gs || *(unsigned *)gs != 0x44004) return;
    int keep_b = g_fu_close_n ? g_fu_close[g_fu_bi % g_fu_close_n] : 0, keep_a = g_fu_close_n ? g_fu_close[g_fu_ai % g_fu_close_n] : 0;
    int nrec = g_fu_test_gs ? 375 : 0;
    if (!g_fu_test_gs) __try { nrec = *(int *)(*(char **)(g_base + 0x4bdc40) + 4); } __except (EXCEPTION_EXECUTE_HANDLER) { nrec = 375; }
    if (nrec > 1000) nrec = 1000;
    static short key[1000];
    int n = 0;
    for (int id = 1; id < nrec && n < 1000; id++) {
        if (id < FU_RECS && g_fu_is[id]) continue;
        const char *r = fu_live_rec(gs, id);
        short no = *(const short *)(r + 2);
        if (no <= 0 || (no >= 700 && (id < 753 || no != id || !sc_live_name(id)))) continue;   // v5.188.46 + 753 and up (Illusion ...)
        int at = n;
        while (at > 0 && key[at - 1] > no) { g_fu_close[at] = g_fu_close[at - 1]; key[at] = key[at - 1]; at--; }
        g_fu_close[at] = id; key[at] = no; n++;
    }
    bool first = !g_fu_close_n;
    g_fu_close_n = n;
    for (int i = 0; i < n; i++) { if (g_fu_close[i] == keep_b) g_fu_bi = i; if (g_fu_close[i] == keep_a) g_fu_ai = i; }
    if (first) logf("Skill Fuser: %d skills to fuse (every school and kind)", n);
}
static int fu_stat(int id, int off) {
    char *gs = g_fu_test_gs ? g_fu_test_gs : *(char **)(g_base + RVA_GSDATA);
    return gs && *(unsigned *)gs == 0x44004 ? *(const short *)(fu_live_rec(gs, id) + off) : 0;
}
static int fu_auto_cost(int a, int b) { int c = (fu_stat(a, 0x28) > fu_stat(b, 0x28) ? fu_stat(a, 0x28) : fu_stat(b, 0x28)) + 1; return c > 10 ? 10 : c; }
static int fu_auto_dmg(int a, int b) { return (fu_stat(a, 0x58) > fu_stat(b, 0x58) ? fu_stat(a, 0x58) : fu_stat(b, 0x58)) + 1; }
static int fu_find(int base, int add) { for (int k = 0; k < g_fu_n; k++) if (g_fu[k].base == base && g_fu[k].add == add) return k; return -1; }
static void fu_make_name(int a, int b, char *out, size_t n) {
    char fa[32], fb[32], na[40], nb[40];
    sprintf_s(na, "%.39s", sc_skill_name(a)); sprintf_s(nb, "%.39s", sc_skill_name(b));
    for (char *s = na + strlen(na); s > na && s[-1] == ' '; ) *--s = 0;   // "Life-Giving Sword " has a trailing space
    for (char *s = nb + strlen(nb); s > nb && s[-1] == ' '; ) *--s = 0;
    sscanf_s(na, "%31s", fa, (unsigned)sizeof(fa)); sscanf_s(nb, "%31s", fb, (unsigned)sizeof(fb));
    const char *A[4] = {na, na, fa, fa}, *B[4] = {nb, fb, nb, fb};
    for (int i = 0; i < 4; i++) {
        char t[80];
        sprintf_s(t, "%s + %s", A[i], B[i]);
        bool taken = false;
        for (int k = 0; k < g_fu_n; k++) taken |= !strcmp(g_fu[k].name, t);
        if (strlen(t) <= 31 && !taken) { sprintf_s(out, n, "%s", t); return; }
    }
    sprintf_s(out, n, "%.14s+%.14s", fa, fb);
}
static int fu_free_id() {                                       // from 699 down (the Skill Creator counts up from 376)
    char *gs = g_fu_test_gs ? g_fu_test_gs : *(char **)(g_base + RVA_GSDATA);
    for (int id = 699; id >= 376; id--) {
        bool used = bfr_uses(id) || ns_comm_range(id);           // v5.109 never a Battle for Reality pack record; v5.186 600-639 community
        for (int k = 0; k < g_fu_n; k++) used |= g_fu[k].id == id;
        for (int k = 0; k < g_ns_n; k++) used |= g_ns[k].id == id;
        if (!used && gs && *(unsigned *)gs == 0x44004 && !ns_zero(gs + 0x18 + NS_REC * id + 2, NS_REC - 2)) used = true;
        if (!used) return id;
    }
    return -1;
}
static bool fu_in_an_arsenal(int id) {                          // the game's arsenals in use and the loader's other lists
    __try {
        ULONG_PTR prof = ars_prof();
        unsigned char *r = prof ? ars_records(prof, 0) : NULL;
        for (int a = 0; r && a < ARS_N; a++)
            for (int c = 0; c < 30; c++) if (*(short *)(r + a * ARS_REC + 0x14 + 2 * c) == id) return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    for (int s = 0; s < 2; s++)
        for (int l = 0; l < ARS_LISTS; l++)
            for (int a = 0; g_ars.used[s][l] && a < ARS_N; a++)
                for (int c = 0; c < 30; c++) if (*(short *)(g_ars.rec[s][l] + a * ARS_REC + 0x14 + 2 * c) == id) return true;
    return false;
}
static int fu_base() { return g_fu_close_n ? g_fu_close[g_fu_bi % g_fu_close_n] : 0; }
static int fu_add() { return g_fu_close_n ? g_fu_close[g_fu_ai % g_fu_close_n] : 0; }
static void fu_fuse() {
    int a = fu_base(), b = fu_add();
    char msg[96];
    if (!a || !b || a == b) { set_status("Pick two different skills"); return; }
    int cost = g_fu_cost >= 0 ? g_fu_cost : fu_auto_cost(a, b), dmg = g_fu_dmg >= 0 ? g_fu_dmg : fu_auto_dmg(a, b);
    int k = fu_find(a, b);
    if (k >= 0) {                                               // the same pair: its cost / damage change
        g_fu[k].cost = cost; g_fu[k].damage = dmg; g_fu[k].dirty = 1;
        g_fu_sel = k;
        fu_save();
        g_fu_next = 0;
        sprintf_s(msg, "Changed: %.31s (cost %d, damage %d)", g_fu[k].name, cost, dmg);
        set_status(msg); rumble(1);
        return;
    }
    if (g_fu_n >= FU_MAX) { set_status("The fusion stock is full (156): delete one first"); return; }
    int id = fu_free_id();
    if (id < 0) { set_status("No free skill number left"); return; }
    Fusion u = {id, a, b, cost, dmg};
    fu_make_name(a, b, u.name, sizeof(u.name));
    g_fu_sel = g_fu_n;
    g_fu[g_fu_n++] = u;
    fu_index();
    if (g_sk_count < SK_MAX) { SkillRow &r = g_sk[g_sk_count++]; r.id = id; sprintf_s(r.name, "%.31s", u.name); strcpy_s(r.school, "fusion"); sprintf_s(r.cost, "%d", cost); }
    fu_save();
    g_fu_next = 0;                                              // written within the next frame
    sprintf_s(msg, "Fused: %.31s (skill %d) - 3 in your stock", u.name, id);
    set_status(msg);
    logf("Skill Fuser: %s = %d + %d's effect, skill %d, cost %d, damage %d", u.name, a, b, id, cost, dmg);
    rumble(2);
}
static void fu_delete() {
    if (g_fu_n <= 0) return;
    int k = g_fu_sel % g_fu_n;
    char msg[96];
    if (fu_in_an_arsenal(g_fu[k].id)) {
        sprintf_s(msg, "%.31s is in an arsenal: take it out first", g_fu[k].name);
        set_status(msg); g_fu_del = 0;
        return;
    }
    if (!g_fu_del) { g_fu_del = 1; set_status("Press A again to delete it"); return; }
    g_fu_del = 0;
    __try {                                                     // its places in the skill image and its stock go
        char *gs = g_fu_test_gs ? g_fu_test_gs : *(char **)(g_base + RVA_GSDATA);
        if (gs && *(unsigned *)gs == 0x44004) {
            for (int i = 0; i < g_fu_n; i++) fu_clear(gs, i);   // rows follow the list order: all are written again
        }
        ULONG_PTR mgr = g_fu_test_gs ? 0 : *(ULONG_PTR *)(g_base + RVA_ARSENAL);
        ULONG_PTR side = mgr ? *(ULONG_PTR *)(mgr + 0x33A0) : 0;
        if (side) *(unsigned char *)(side + 0x644 + g_fu[k].id) = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    sprintf_s(msg, "Deleted: %.31s", g_fu[k].name);
    for (int i = 0; i < g_sk_count; i++)
        if (g_sk[i].id == g_fu[k].id) { for (int j = i; j + 1 < g_sk_count; j++) g_sk[j] = g_sk[j + 1]; g_sk_count--; break; }
    for (int i = k; i + 1 < g_fu_n; i++) g_fu[i] = g_fu[i + 1];
    g_fu_n--;
    if (g_fu_sel >= g_fu_n) g_fu_sel = g_fu_n ? g_fu_n - 1 : 0;
    fu_index();
    fu_save();
    g_fu_next = 0;
    set_status(msg); rumble(1);
}
static const int FU_ROWS = 8;
static void fu_input(WORD pressed) {
    if (!g_fu_loaded) fu_load();
    fu_close_list();
    if (pressed & BTN_DOWN) { g_fu_row = (g_fu_row + 1) % FU_ROWS; g_fu_del = 0; }
    if (pressed & BTN_UP) { g_fu_row = (g_fu_row + FU_ROWS - 1) % FU_ROWS; g_fu_del = 0; }
    if (pressed & BTN_B) { InterlockedExchange(&g_ed_page, EDP_CHOOSER); return; }
    int n = g_fu_close_n ? g_fu_close_n : 1, dir = (pressed & BTN_RIGHT) ? 1 : (pressed & BTN_LEFT) ? -1 : 0;
    if (pressed & (BTN_LB | BTN_RB)) dir = (pressed & BTN_RB) ? 10 : -10;   // 10 at a time
    if (dir) {
        switch (g_fu_row) {
        case 0: g_fu_bi = ((g_fu_bi + dir) % n + n) % n; if (g_fu_ai % n == g_fu_bi) g_fu_ai = (g_fu_ai + 1) % n; g_fu_cost = g_fu_dmg = -1; break;
        case 1: g_fu_ai = ((g_fu_ai + dir) % n + n) % n; if (g_fu_ai == g_fu_bi % n) g_fu_ai = ((g_fu_ai + (dir > 0 ? 1 : -1)) % n + n) % n;
                g_fu_cost = g_fu_dmg = -1; break;
        case 2: { int c = g_fu_cost >= 0 ? g_fu_cost : fu_auto_cost(fu_base(), fu_add()); c += dir > 0 ? 1 : -1; g_fu_cost = c < 0 ? 0 : c > 10 ? 10 : c; break; }
        case 3: { int d = g_fu_dmg >= 0 ? g_fu_dmg : fu_auto_dmg(fu_base(), fu_add()); d += dir > 0 ? 1 : -1; g_fu_dmg = d < 0 ? 0 : d > 99 ? 99 : d; break; }
        case 5: if (g_fu_n) g_fu_sel = ((g_fu_sel + (dir > 0 ? 1 : -1)) % g_fu_n + g_fu_n) % g_fu_n; g_fu_del = 0; break;
        }
    }
    if ((pressed & BTN_X) && (g_fu_row == 2 || g_fu_row == 3)) { if (g_fu_row == 2) g_fu_cost = -1; else g_fu_dmg = -1; set_status("Back to the fusion rule"); }
    if (!(pressed & BTN_A)) return;
    if (g_fu_row == 4) fu_fuse();
    else if (g_fu_row == 5 && g_fu_n) {                         // that fusion into the rows above, to change it
        Fusion &u = g_fu[g_fu_sel % g_fu_n];
        for (int i = 0; i < g_fu_close_n; i++) { if (g_fu_close[i] == u.base) g_fu_bi = i; if (g_fu_close[i] == u.add) g_fu_ai = i; }
        g_fu_cost = u.cost; g_fu_dmg = u.damage; g_fu_row = 2;
        set_status("Change its cost / damage, then FUSE IT keeps the change");
    } else if (g_fu_row == 6) fu_delete();
    else if (g_fu_row == 7) InterlockedExchange(&g_ed_page, EDP_CHOOSER);
}
static const char *FU_H[] = {"SKILL FUSER - how to use", "1  Base skill: its move, hit, school and capsule",
                             "2  Add effects of: its effect plays on the same move", "   (both effects at once; any skill of any school)",
                             "3  Cost / damage: the higher + 1, or set your own (X: rule)", "4  FUSE IT: the new skill, 3 copies in your stock",
                             "   Arsenal Edit and View Stock list it like any skill", "5  Fusion stock: your fusions (A: change one)",
                             "6  Test one: Create a Skill's stage, drop its capsule", "   Offline only; the full list: CLOSE_RANGE_FUSIONS.md", NULL};
template <class PUT, class BOX> static void fu_draw(PUT &put, BOX &box, int x, int y) {
    const DWORD GOLD = 0xFFFFD040, WHITE = 0xFFFFFFFF, BLUE = 0xFFA0E0FF, GREY = 0xFFB0B0B0;
    const int LINE = 16;
    (void)box;
    if (!g_fu_loaded) fu_load();
    fu_close_list();
    if (g_ed_help) {
        put(x, y, FU_H[0], GOLD);
        y += LINE + 4;
        for (int i = 1; FU_H[i]; i++, y += LINE) put(x, y, FU_H[i], WHITE);
        put(x, y + 4, "Y / A / B: close this help", BLUE);
        return;
    }
    put(x, y, "SKILL FUSER                  (B: back   Y: help)", GOLD);
    y += LINE + 4;
    int a = fu_base(), b = fu_add();
    static const char *SCH[7] = {"Aura Particle", "Psycho", "Optical", "Nature", "Ki", "Faith", "Illusion"};   // v5.168 school 0 = the Aura Particle school (owner rule), 6 = Illusion
    char line[128], name[32];
    fu_make_name(a, b, name, sizeof(name));
    int k = fu_find(a, b);
    const char *desc = "";
    for (int r = 0; r < FU_ROWS; r++, y += LINE) {
        const char *lab = "";
        char val[96] = "";
        switch (r) {
        case 0: { int s = fu_stat(a, 0x12); lab = "Base skill";
                  sprintf_s(val, "< %.24s >  (%s) cost %d, damage %d", a ? sc_skill_name(a) : "-", SCH[s >= 0 && s <= 6 ? s : 0], fu_stat(a, 0x28), fu_stat(a, 0x58));   // v5.168 "(school)": the row read "Psycho Wave Psycho"
                  desc = "L/R (LB/RB: 10): it gives the move, hit, school, capsule"; break; }
        case 1: lab = "Add effects of"; sprintf_s(val, "< %.24s >  cost %d, damage %d", b ? sc_skill_name(b) : "-", fu_stat(b, 0x28), fu_stat(b, 0x58));
                desc = "L/R: its effect plays on the same move (game order)"; break;
        case 2: lab = "Cost"; if (g_fu_cost >= 0) sprintf_s(val, "%d", g_fu_cost); else sprintf_s(val, "%d (the higher + 1)", fu_auto_cost(a, b));
                desc = "L/R: aura it costs (X: back to the rule)"; break;
        case 3: lab = "Damage"; if (g_fu_dmg >= 0) sprintf_s(val, "%d", g_fu_dmg); else sprintf_s(val, "%d (the higher + 1)", fu_auto_dmg(a, b));
                desc = "L/R: damage per hit (X: back to the rule)"; break;
        case 4: lab = k >= 0 ? "FUSE IT (keeps the change)" : "FUSE IT"; sprintf_s(val, "%.31s", k >= 0 ? g_fu[k].name : name);
                desc = k >= 0 ? "This pair is in your fusion stock: A keeps the cost / damage above" : "A: the new skill goes into your fusion stock (3 copies)"; break;
        case 5: lab = "Fusion stock";
                if (g_fu_n) { Fusion &u = g_fu[g_fu_sel % g_fu_n]; sprintf_s(val, "< %d/%d  %.31s >  (skill %d)", g_fu_sel % g_fu_n + 1, g_fu_n, u.name, u.id); }
                else sprintf_s(val, "empty");
                desc = "L/R: your fusions; A: load it into the rows above"; break;
        case 6: lab = g_fu_del ? "Delete it - A again" : "Delete it (A twice)"; desc = "The fusion in Fusion stock (not while in an arsenal)"; break;
        case 7: lab = "Back"; desc = "To Edgar's Dream"; break;
        }
        sprintf_s(line, "%s %-26s %s", r == g_fu_row ? ">" : " ", lab, val);
        put(x, y, line, r == g_fu_row ? GOLD : (r == 4 ? BLUE : WHITE));
        if (r == g_fu_row) put(x, y + (FU_ROWS - r) * LINE + 6, desc, BLUE);
    }
    sprintf_s(line, "Fusion stock: %d of %d  -  in Arsenal Edit and View Stock like any skill (numbers 376-699)", g_fu_n, FU_MAX);
    put(x, y + LINE + 10, line, GREY);
}

// ---- self-test (stand-in skill image): a fusion is written in empty places only, carries the base's record with its own
// number, row, name, cost and damage; the detour's call queues its move and the next frame starts the added effect
// the self-test's stand-in effect calls
struct FuTestFx {
    static unsigned __fastcall start(ULONG_PTR, int o, int q, int t, const float *m, int) { s_o = o; s_q = q; s_t = t; s_m = m; return 0x2007; }
    static void __fastcall stop(unsigned h) { s_stop = h; }
    static void __fastcall move(unsigned, const float *) { s_moves++; }
    static short __fastcall eff(int p, int) { return (short)(p == 9 ? 909 : 0); }
    static int s_o, s_q, s_t, s_moves;
    static unsigned s_stop;
    static const float *s_m;
};
int FuTestFx::s_o, FuTestFx::s_q, FuTestFx::s_t, FuTestFx::s_moves;
unsigned FuTestFx::s_stop;
const float *FuTestFx::s_m;
static int fu_self_test() {
    static char gs[0x44004 + 16];
    memset(gs, 0, sizeof(gs));
    *(unsigned *)gs = 0x44004;
    *(unsigned *)(gs + 0x14) = 374;
    char *b10 = gs + 0x18 + NS_REC * 10, *b9 = gs + 0x18 + NS_REC * 9;
    *(short *)(b10 + 2) = 10; *(short *)(b10 + 0x12) = 3; *(short *)(b10 + 0x14) = 10; *(short *)(b10 + 0x28) = 3; *(short *)(b10 + 0x58) = 3;
    *(short *)(b10 + 0x7A) = 12;
    *(short *)(b9 + 2) = 9; *(short *)(b9 + 0x14) = 9; *(short *)(b9 + 0x28) = 2; *(short *)(b9 + 0x58) = 2; *(short *)(b9 + 0x7A) = 12;
    *(short *)(gs + 0x18 + NS_REC * 601 + 0x40) = 1;                // record 601 busy
    Fusion keep[2];
    int keep_n = g_fu_n;
    bool keep_loaded = g_fu_loaded;
    memcpy(keep, g_fu, sizeof(keep));
    g_fu_loaded = true;
    g_fu[0] = {600, 10, 9, 4, 4, "Flame Sword + Ice Sword"};
    g_fu[1] = {601, 10, 9, 4, 4, "Busy"};
    g_fu_n = 2;
    fu_index();
    g_fu_test_gs = gs;
    fu_records_step();
    fu_records_step();
    char *r = gs + 0x18 + NS_REC * 600, *entry = gs + NS_TEXT + 12 * fu_row(0);
    bool rec_ok = *(short *)r == fu_row(0) && *(short *)(r + 2) == 600 && *(short *)(r + 6) == 600 && *(short *)(r + 0x12) == 3 &&
                  *(short *)(r + 0x14) == 10 && *(short *)(r + 0x28) == 4 && *(short *)(r + 0x58) == 4 &&
                  !strcmp(entry + *(int *)(entry + 4), "Flame Sword + Ice Sword") && !strcmp(entry + *(int *)(entry + 8), FU_DESC_TEXT) &&
                  *(unsigned *)(gs + 0x14) == 600 && *(short *)(gs + 0x18 + NS_REC * 601 + 2) == 0 && g_fu[1].said == -1;
    // a change rewrites it in place
    g_fu[0].damage = 9; g_fu[0].dirty = 1; g_fu_next = 0;
    fu_records_step();
    bool change_ok = *(short *)(r + 0x58) == 9 && *(short *)(r + 2) == 600;
    // the move start: a fusion is queued (a stock skill is not), the next frame starts skill 9's part 0 at the action's matrix
    static unsigned char action[0xd00];
    static unsigned char fighter[0x40];
    memset(action, 0, sizeof(action)); memset(fighter, 0, sizeof(fighter));
    *(int *)(action + 0x5c) = 10;
    LONG seen0 = g_fu_seen;
    fus_move_start((ULONG_PTR)action, 0);
    bool stock_skipped = g_fu_seen == seen0 && g_fu_pend_n == 0;
    *(int *)(action + 0x5c) = 600;
    *(ULONG_PTR *)(action + 0xbd0) = (ULONG_PTR)fighter;
    *(int *)(fighter + 0x14) = 2;
    *(unsigned *)(action + 0xc20) = 0x1005;                         // the base's own effect
    fus_move_start((ULONG_PTR)action, 0);
    bool queued = g_fu_seen == seen0 + 1 && g_fu_pend_n == 1;
    static unsigned char mgr[0x60 + 0xF0 * 8];
    memset(mgr, 0, sizeof(mgr));
    *(unsigned *)(mgr + 0x60 + 5 * 0xF0) = 0x1005;                  // both instances alive
    *(unsigned *)(mgr + 0x60 + 7 * 0xF0) = 0x2007;
    FxGame g = {};
    g.mgr = (ULONG_PTR)mgr; g.skills = (ULONG_PTR)(gs + 0x18);
    g.start = FuTestFx::start; g.stop = FuTestFx::stop; g.move = FuTestFx::move; g.effect_of = FuTestFx::eff;
    FuTestFx::s_o = FuTestFx::s_q = FuTestFx::s_t = FuTestFx::s_moves = 0; FuTestFx::s_stop = 0;
    fu_live_step(g, true);
    bool started = FuTestFx::s_q == 909 && FuTestFx::s_o == 2 && FuTestFx::s_t == 0 && FuTestFx::s_m == (const float *)(action + 0xbe0) && g_fu_live_n == 1 && g_fu_pend_n == 0;
    int moved_first = FuTestFx::s_moves;                            // it follows from its first frame on
    fu_live_step(g, true);
    bool follows = moved_first == 1 && FuTestFx::s_moves == 2;
    *(unsigned *)(action + 0xc20) = 0x1009;                         // the base's effect was replaced: the added one stops
    fu_live_step(g, true);
    bool stops = FuTestFx::s_stop == 0x2007 && g_fu_live_n == 0;
    g_fu_test_gs = NULL;
    memcpy(g_fu, keep, sizeof(keep));
    g_fu_n = keep_n; g_fu_loaded = keep_loaded;
    fu_index();
    g_fu_next = 0;
    logf("self-test: Skill Fuser record %s, change %s, stock skill skipped %s, fusion queued %s, added effect starts %s, follows %s, stops with the base %s",
         rec_ok ? "ok" : "FAILED", change_ok ? "ok" : "FAILED", stock_skipped ? "ok" : "FAILED", queued ? "ok" : "FAILED",
         started ? "ok" : "FAILED", follows ? "ok" : "FAILED", stops ? "ok" : "FAILED");
    return rec_ok && change_ok && stock_skipped && queued && started && follows && stops ? 1 : -60;
}

// v5.188.46 (owner 2026-11-17: Create a Skill and the Skill Fuser "not displaying all of the skills in the game ... question
// marks"): the skill list came from CwoSkills.txt only - 374 stock skills in MODS, none at all in BFR (no such file). Now it
// is the game's live table (*0x1404bdc50, count at *0x1404bdc40 + 4; ours while the big tables are on): every record with a
// shown number 1-699, and 753+ that the loader's lists wrote (Illusion ...), named as the game names it; kept sorted by number.
static const char *sk_list_name(int id) {
    for (int i = 0; i < g_bfr_n; i++) if (g_bfr[i].id == id && g_bfr[i].name[0]) return g_bfr[i].name;
    for (int i = 0; i < g_bs_n; i++) if (g_bs[i].id == id && g_bs[i].name[0]) return g_bs[i].name;
    for (int i = 0; i < g_ns_n; i++) if (g_ns[i].id == id && g_ns[i].name[0]) return g_ns[i].name;
    for (int i = 0; i < g_fu_n; i++) if (g_fu[i].id == id && g_fu[i].name[0]) return g_fu[i].name;
    return NULL;
}
static short sk_shown(const char *recs, int nrec, int id) {    // v5.188.47 the number the game shows for a record
    __try { if (recs && id > 0 && id < nrec) { short no = *(const short *)(recs + NS_REC * (ULONG_PTR)id + 2); if (no > 0) return no; } }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return (short)(id < 32000 ? id : 32000);
}
static void sk_live_list() {
    static ULONGLONG t;
    static int last_nrec = -1, last_count = -1;
    ULONGLONG now = GetTickCount64();
    if (now - t < 3000 && last_count == g_sk_count) return;
    t = now;
    static const char *SCH[7] = {"Aura Part", "Psycho", "Optical", "Nature", "Ki", "Faith", "Illusion"};
    int nrec = 0;
    const char *recs = NULL;
    __try {
        const char *hdr = *(const char **)(g_base + 0x4bdc40);
        recs = *(const char **)(g_base + 0x4bdc50);
        nrec = hdr ? *(const int *)(hdr + 4) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { nrec = 0; }
    if (!recs || nrec <= 1) return;
    if (nrec > 1200) nrec = 1200;
    int keep = g_sk_count > 0 && g_sk_sel >= 0 && g_sk_sel < g_sk_count ? g_sk[g_sk_sel].id : -1, added = 0;
    for (int id = 1; id < nrec && g_sk_count < SK_MAX; id++) {
        short no = 0, sch = 0, cost = 0;
        __try { const char *r = recs + NS_REC * (ULONG_PTR)id; no = *(const short *)(r + 2); sch = *(const short *)(r + 0x12); cost = *(const short *)(r + 0x28); }
        __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        if (no <= 0 || (no >= 700 && (id < 753 || no != id))) continue;
        bool have = false;
        for (int i = 0; i < g_sk_count && !have; i++) have = g_sk[i].id == id;
        if (have) continue;
        const char *nm = sc_live_name(id);
        if (!nm) nm = sk_list_name(id);
        if (!nm) continue;
        SkillRow &r = g_sk[g_sk_count++];
        r.id = id;
        sprintf_s(r.name, "%.31s", nm);
        strcpy_s(r.school, sch >= 0 && sch <= 6 ? SCH[sch] : "-");
        sprintf_s(r.cost, "%d", cost >= 0 && cost <= 99 ? cost : 0);
        added++;
    }
    for (int i = 0; i < g_sk_count; i++)                         // names the list file has as "?" / empty: the game's own
        { const char *nm = sc_live_name(g_sk[i].id); if (nm) sprintf_s(g_sk[i].name, "%.31s", nm); }   // v5.188.47 the game's own name always
    if (added || last_count < 0) {                               // v5.188.47 sorted by the number the game shows (record +0x02), then record
        static short key[SK_MAX];
        for (int i = 0; i < g_sk_count; i++) key[i] = sk_shown(recs, nrec, g_sk[i].id);
        for (int i = 1; i < g_sk_count; i++) { SkillRow x = g_sk[i]; short kx = key[i]; int j = i - 1;
            while (j >= 0 && (key[j] > kx || (key[j] == kx && g_sk[j].id > x.id))) { g_sk[j + 1] = g_sk[j]; key[j + 1] = key[j]; j--; }
            g_sk[j + 1] = x; key[j + 1] = kx; }
        for (int i = 0; i < g_sk_count; i++) if (g_sk[i].id == keep) InterlockedExchange(&g_sk_sel, i);
    }
    if (added || nrec != last_nrec) logf("skill list: %d skills (%d added from the game's live table of %d records)", g_sk_count, added, nrec);
    last_nrec = nrec; last_count = g_sk_count;
}
