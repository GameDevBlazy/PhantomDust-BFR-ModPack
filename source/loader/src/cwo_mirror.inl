// ---- v5.34 the Mirror skill (owner 2026-10-27; the owner's choice: "Mirror fighter with my hand, gone
// on first hit"). Offline matches only (local versus / Single Play), never online, never in a creator's test.
// How (the game cannot add a fighter in the middle of a battle without its quest reinforcement path, FUN_14012d200, which
// works on the 4-fighter tables the six-fighter patch moved - so not used here):
//  - the skill: a new skill "Mirror" made from Doppelganger (record 226, a Status skill: its motion SPE_RESET and its own
//    effect on you), with its statuses taken out (+0x54 target = 0 you, status ids +0x56 .. +0x5C = 0 none), 3 in stock;
//  - at the battle's set-up (create hook, as Boss Raid's allies): when player 1's arsenal holds the Mirror and a fighter
//    record is free, a fighter of player 1's own character, outfit and arsenal joins player 1's team as a CPU - parked;
//  - parked: not drawn (the player draw wrapper, cwo_forge.inl), can't be locked on (status flag 8), takes no damage (flag 33),
//    its slots can't be pressed (the per-fighter rule table 0x1404bbd70, 9 ints a fighter, read by FUN_140256920) and it is
//    held at its start point;
//  - player 1 uses the Mirror: the double stands beside player 1, drawn, lockable, with 1 life and player 1's hand at that
//    moment (the game's capture, FUN_140261a00, into its 4 slots), and fights (its CPU program);
//  - its first hit takes it down: it disappears at once, and when its fall is over the game's own revive (FUN_140251f80)
//    stands it up parked again for the next Mirror;
//  - player 1 down: the double goes too (it is taken out, so a 1 vs 1 still ends when you are out).
// Game facts (Ghidra full pass 065, 2026-10-27/28): status flag n of a fighter = battle data (fighter + 0x40) + 0x61b8 + 12 n
// (+4 timed, +8 time left; FUN_14024d4d0 sums them); flag 8 = can't be locked on (FUN_140270b00 case 0xc), flag 33 = no
// damage taken (case 0xe); fighter + 0x12f04 state (0x1B down), + 0x12f08 the count before a downed fighter is removed,
// + 0x4c life, + 0x50 its cap, + 0x14 index, + 0x1c team, + 0x20 in the battle, + 0x24 standing.
static int g_mr_id = -1;                                         // the Mirror's skill number (from CwoNewSkills.txt)
// v5.148.19 Spiritual Projection (owner 2026-11-10, after the parked double fought invisibly: "we can create a skill that does
// exactly this ... Maybe it can be called spiritual projection ... fits the KI school"; spec: "cost 5 and last for 15 seconds ...
// the skills that spiritual projection will have will be hyperkick and pursuit. Whatever amount of aura the user has, that's how
// much ... spiritual projection has"). It shares the Mirror's parked double: on its cast the double comes out unseen (not drawn,
// can't be locked on to) with Hyperkick (18) + Pursuit (22) in hand and the caster's aura, for 15 s or until it is hit.
static int g_sp_id = -1;
static int g_mr_mode;                                             // 0 the Mirror, 1 Spiritual Projection (while out)
static unsigned char g_sp_hand5[5], g_sp_proj[48];
static const int SP_SKILLS[2] = {18, 22};
static int g_mr_idx = -1;                                        // the double's fighter index in this battle (-1 none)
static int g_mr_state;                                           // 0 parked, 1 out, 2 down (hidden, waiting to stand), 3 gone
static float g_mr_park[3];
static bool g_mr_have_park;
static char g_mr_cpu[0x60];
static int g_mr_last_uses[4];
static ULONGLONG g_mr_out_at;                                    // v5.129 (owner 2026-11-09 redesign): out 30 s at most
static const ULONG_PTR RVA_MR_RULES = 0x4bbd70;
static const int MR_FLAG_NOLOCK = 8, MR_FLAG_NODAMAGE = 33, MR_DOWN = 0x1B;
static const char *MR_NAME = "Mirror";

// v5.137.1 offline test switch: CwoTestDoubles.txt in the loader data folder = the Mirror's and the Hologram's doubles join every
// offline battle even when the arsenal does not hold the skills (for the agent's in-game checks; delete the file to end it)
static bool mr_test_doubles() {
    if (ed_online()) return false;
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoTestDoubles.txt", g_dir);
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}
// v5.148.4 (owner 2026-11-10; log: a cast from hand
// slot 4 - "projectile: player 1 slot 4 state 2 skill 470" - and the use test looked at slots 0-3 only): a cast is seen when a
// hand slot 0-4 holding the skill starts its use (+0x3c / +0x40 leave -1), or when a new flying record of the fighter
// (fighter + 0x4E0, 48 x 0x1F0, +0x10 state, +0x14 skill; cwo_newskills.inl) carries the skill number
static bool mr_cast_seen(ULONG_PTR me, int id, unsigned char *last_hand, unsigned char *last_proj) {
    bool used = false;
    for (int k = 0; k < 5; k++) {
        char *s = (char *)(me + 0x388 + 0x50 * k);
        // v5.148.7 (in game 17:1x, slot bytes read every 50 ms across a cast of 471): a use of this kind starts with +0x34 = the
        // skill in use and +0x24 = its cost (about 1 s); +0x3c / +0x40 stay -1. (5.148.7-15 read +0x20 by mistake: never seen.)
        bool now = ((*(int *)(s + 0x3c) != -1 || *(int *)(s + 0x40) != -1) && *(int *)(s + 0x30) == id) ||
                   (*(int *)(s + 0x30) == id && *(int *)(s + 0x34) == id && *(int *)(s + 0x24) != 0);   // 5.148.18 +0x30 too (a 2nd 'out' at 19:50 without a cast)
        if (now && !last_hand[k]) used = true;
        last_hand[k] = now;
    }
    for (int k = 0; k < 48; k++) {
        ULONG_PTR sl = me + 0x4E0 + 0x1F0 * (ULONG_PTR)k;
        bool now = *(int *)(sl + 0x10) != 0 && *(int *)(sl + 0x14) == id;
        if (now && !last_proj[k]) used = true;
        last_proj[k] = now;
    }
    return used;
}
static unsigned char g_mr_hand5[5], g_mr_proj[48], g_ho_hand5[5], g_ho_proj[48];
static int *mr_flag(ULONG_PTR actor, int n) { return (int *)(actor + 0x40 + 0x61b8 + 12 * (ULONG_PTR)n); }
static void mr_rules(int idx, int v) {                           // the double's 4 slot buttons (FUN_140256920's rule)
    if (idx < 0 || idx > 3) return;
    int *r = (int *)(g_base + RVA_MR_RULES) + 9 * idx;
    for (int k = 0; k < 4; k++) r[k] = v;
}

// the skill: made once, kept in CwoNewSkills.txt like the Skill Creator's
static void sp_ensure_skill() {
    for (int k = 0; k < g_ns_n; k++)
        if (!strcmp(g_ns[k].name, "Spiritual Projection") && g_ns[k].src == 226) { g_sp_id = g_ns[k].id; return; }
    if (g_ns_n >= NS_MAX) return;
    int id = ns_free_id();
    if (id < 0) { logf("spiritual projection: no free skill number"); return; }
    NewSkill m;
    memset(&m, 0, sizeof(m));
    m.id = id; m.src = 226;
    strcpy_s(m.name, "Spiritual Projection");
    strcpy_s(m.desc, "A projection of you fights for 15 sec with Hyperkick and Pursuit and your aura. Gone if hit.");
    m.school = 4; m.cost = 5; m.damage = m.fx = m.snd = m.anim = -1;
    static const int OFF[5] = {0x54, 0x56, 0x58, 0x5A, 0x5C};
    for (int i = 0; i < 5; i++) { m.fo[i] = OFF[i]; m.fv[i] = 0; }
    m.nfo = 5;
    m.dirty = 1;
    g_ns[g_ns_n++] = m;
    g_sp_id = id;
    ns_save();
    logf("spiritual projection: the skill is skill %d (Ki, cost 5)", id);
}
static void mr_ensure_skill() {
    sp_ensure_skill();
    for (int k = 0; k < g_ns_n; k++)
        if (!strcmp(g_ns[k].name, MR_NAME) && g_ns[k].src == 226) { g_mr_id = g_ns[k].id; return; }
    if (g_ns_n >= NS_MAX) return;
    int id = ns_free_id();
    if (id < 0) { logf("mirror: no free skill number - the Mirror skill is not made"); return; }
    NewSkill m;
    memset(&m, 0, sizeof(m));
    m.id = id; m.src = 226;
    strcpy_s(m.name, MR_NAME);
    strcpy_s(m.desc, "A double of you with your hand joins the fight. It is gone on the first hit it takes.");
    m.school = m.cost = m.damage = m.fx = m.snd = m.anim = -1;
    static const int OFF[5] = {0x54, 0x56, 0x58, 0x5A, 0x5C};     // target you, no status of its own
    for (int i = 0; i < 5; i++) { m.fo[i] = OFF[i]; m.fv[i] = 0; }
    m.nfo = 5;
    m.dirty = 1;
    g_ns[g_ns_n++] = m;
    g_mr_id = id;
    ns_save();
    logf("mirror: the Mirror skill is skill %d (Doppelganger's motion and effect, no status)", id);
}

// set-up (game thread, sixf_before_create_main): the double into a free record of player 1's team
static int mr_fill(char *rec, ULONG_PTR *ptrs, int nskills, float *aura) {
    g_mr_idx = -1; g_mr_state = 0; g_mr_have_park = false; g_mr_hide_actor = 0;
    memset(g_mr_last_uses, 0, sizeof(g_mr_last_uses));
    memset(g_mr_hand5, 0, sizeof(g_mr_hand5)); memset(g_mr_proj, 0, sizeof(g_mr_proj));
    if (g_mr_id <= 0 || !*(int *)rec || !ptrs || !ptrs[0] || nskills <= g_mr_id) return 0;
    int *mine = (int *)ptrs[0];
    if (mine[g_mr_id] <= 0 && !(g_sp_id > 0 && g_sp_id < nskills && mine[g_sp_id] > 0) && !mr_test_doubles()) return 0;   // no Mirror / Spiritual Projection (5.148.19) in player 1's arsenal
    int free_i = -1, used = 0;
    for (int p = 0; p < 4; p++) {
        char *r = rec + 0x48 * p;
        int s = *(int *)(r + 0x18);
        if (*(int *)r && s >= 0 && s < 4) used |= 1 << s;
        if (!*(int *)r && free_i < 0 && p > 0) free_i = p;
    }
    if (free_i < 0 || !ptrs[free_i]) { logf("mirror: no free fighter in this battle - no double"); return 0; }
    char *r = rec + 0x48 * free_i, *c = g_mr_cpu;
    int slot = 0;
    for (int k = 0; k < 4; k++) if (!(used & (1 << k))) { slot = k; break; }
    int row = *(int *)(rec + 8) & 0xFFFF;
    memset(c, 0, 0x60);
    *(int *)c = 1; *(int *)(c + 8) = 1; *(int *)(c + 0xc) = row; *(int *)(c + 0x10) = slot;
    *(int *)(c + 0x14) = 20; *(int *)(c + 0x18) = 20;
    strncpy_s(c + 0x20, 0x11, "Mirror", _TRUNCATE);
    lob_cpu_program(c);
    memcpy(r, rec, 0x48);                                         // player 1's record: character, outfit, ...
    *(int *)r = 1;
    *(int *)(r + 0xc) = -1; *(int *)(r + 0x10) = 0; *(int *)(r + 0x14) = 1; *(int *)(r + 0x18) = slot;
    *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)c;
    memcpy((void *)ptrs[free_i], (void *)ptrs[0], (size_t)(nskills - 1) * 4);   // player 1's arsenal
    if (aura) aura[free_i] = aura[0];
    g_mr_idx = free_i;
    logf("mirror: the double of player 1 (character row %d) is fighter %d, start %d - parked until the Mirror is used", row, free_i + 1, slot);
    return 1;
}

// v5.137 (memory read 2026-11-10 07:3x, offline split-screen match, 4 fighters): fighter +0x13F20 = the index of the
// fighter it is locked on to (player 1 -> 1, player 2 -> 0, each double -> an enemy of its team): the lock target.
static const ULONG_PTR FT_LOCK = 0x13F20;
// offline test helper: CwoTestCast.txt "mirror" / "hologram" in the loader data folder fires that skill's use once
static bool mr_test_cast(const char *what) {
    static ULONGLONG t; static char seen[16];
    ULONGLONG now = GetTickCount64();
    if (now - t < 500) { if (seen[0] && !_stricmp(seen, what)) { seen[0] = 0; return true; } return false; }
    t = now; seen[0] = 0;
    if (ed_online()) return false;
    wchar_t p[MAX_PATH]; swprintf_s(p, MAX_PATH, L"%sCwoTestCast.txt", g_dir);
    FILE *f = NULL; char line[16] = "";
    if (cwo_wfopen(&f, p, L"r") != 0 || !f) return false;
    if (!fgets(line, sizeof(line), f)) line[0] = 0;
    fclose(f); DeleteFileW(p);
    for (char *c = line; *c; c++) if (*c == '\r' || *c == '\n') *c = 0;
    logf("test cast: %s", line);
    if (!_stricmp(line, what)) return true;
    strcpy_s(seen, line);
    return false;
}
// v5.148.15 (owner 17:1x: "those invisible characters are running around ... they have their own spawns ... picking up capsules"):
// a parked double is INERT - its CPU brain (fighter +0x142D0; FUN_14024ec80 runs the CPU step only while it is set and the pad
// driver +0x30 is 0) is off while parked and given back when it comes out - and it is parked 300 m above its start, away from
// capsules and fighters (its position + previous position written every frame).
static int mr_brain_keep(ULONG_PTR a, bool park) {
    static ULONG_PTR who[4]; static int val[4];
    int slot = -1;
    for (int i = 0; i < 4; i++) if (who[i] == a) slot = i;
    if (park) {
        if (slot < 0) for (int i = 0; i < 4 && slot < 0; i++) if (!who[i]) { slot = i; who[i] = a; val[i] = *(int *)(a + 0x142d0); }
        *(int *)(a + 0x142d0) = 0;
    } else if (slot >= 0) {
        *(int *)(a + 0x142d0) = val[slot] ? val[slot] : 1; who[slot] = 0;
    }
    return 0;
}
static void mr_park(ULONG_PTR a, bool on) {
    mr_brain_keep(a, on);
    *mr_flag(a, MR_FLAG_NOLOCK) = on ? 1 : 0;
    *mr_flag(a, MR_FLAG_NODAMAGE) = on ? 1 : 0;
    mr_rules(g_mr_idx, on ? 1 : 0);
    g_mr_hide_actor = on ? a : 0;
}

// every frame (render thread, CwoOverlayFrame)
static bool g_mr_started;                                       // v5.137.2: the match the double was made for has begun
static void mr_step(bool in_match) {
    if (g_mr_idx < 0) { g_mr_started = false; return; }
    if (in_match) g_mr_started = true;
    // v5.137.2 (in game 07:32: the double is made at the battle's creation, before the match counts as started, and the
    // first frame's 'not in a match' dropped it - the owner's 'I used mirror, nothing happened'): only a started match ends it
    if (!in_match && !g_mr_started) return;
    if (!in_match) {
        g_mr_started = false;                                              // the battle is over: nothing left behind
        __try { mr_rules(g_mr_idx, 0); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_mr_idx = -1; g_mr_hide_actor = 0;
        return;
    }
    __try {
        ULONG_PTR *act = cwo_actors();
        ULONG_PTR me = act[0], a = act[g_mr_idx];
        if (!me || !a || !*(int *)(a + 0x20)) return;
        float *pos = (float *)(a + 0x12E40);
        if (!g_mr_have_park) { memcpy(g_mr_park, pos, 12); g_mr_park[1] += 3000.0f; g_mr_have_park = true; mr_park(a, true); }
        int st = *(int *)(a + 0x12F04);
        bool me_down = *(int *)(me + 0x12F04) == MR_DOWN || !*(int *)(me + 0x24);
        if (me_down && g_mr_state != 3) {                         // player 1 down: the double goes as well
            mr_park(a, false);
            g_mr_hide_actor = a;
            *(int *)(a + 0x4C) = 0;
            g_mr_state = 3;
            logf("mirror: player 1 is down - the double leaves the battle");
            return;
        }
        if (g_mr_state == 3) { g_mr_hide_actor = a; return; }
        if (g_mr_state == 0) {                                     // parked: hidden, held, waiting for the Mirror
            if (st != MR_DOWN) { memcpy(pos, g_mr_park, 12); memcpy(pos + 3, g_mr_park, 12); *mr_flag(a, MR_FLAG_NOLOCK) = 1; *mr_flag(a, MR_FLAG_NODAMAGE) = 1; *(int *)(a + 0x142d0) = 0; }
            mr_rules(g_mr_idx, 1);   // v5.148.4: every frame - the game clears the rule table at each match start (in game 16:56 the parked double cast your skills)
            // player 1 used the Mirror? (a slot with it starts its use: +0x3c / +0x40 leave -1)
            bool used = mr_cast_seen(me, g_mr_id, g_mr_hand5, g_mr_proj);
            bool sp = g_sp_id > 0 && mr_cast_seen(me, g_sp_id, g_sp_hand5, g_sp_proj);
            if (!used && !sp && mr_test_cast("mirror")) used = true;
            if (!used && !sp && mr_test_cast("projection")) sp = true;
            if (!used && !sp) return;
            g_mr_mode = sp && !used ? 1 : 0;
            float yaw = (float)*(int *)(me + 0x12E68) / 5215.18896f + 1.5708f;
            const float *mp = (const float *)(me + 0x12E40);
            pos[0] = mp[0] + sinf(yaw) * 14.0f; pos[1] = mp[1]; pos[2] = mp[2] + cosf(yaw) * 14.0f;
            *(int *)(a + 0x12E68) = *(int *)(me + 0x12E68);
            mr_park(a, false);
            *(int *)(a + 0x4C) = 1;                                // gone on the first hit
            int copied = 0;
            if (g_mr_mode == 1) {                                  // Spiritual Projection: Hyperkick + Pursuit, the caster's aura (visible from 5.149.1)
                for (int k = 0; k < 2; k++) if (sc_capture((char *)(a + 0x388 + 0x50 * k), SP_SKILLS[k])) copied++;
                *(int *)(a + 0x58) = *(int *)(me + 0x58);          // status block (fighter + 0x40) +0x18 max aura / +0x1C aura
                *(float *)(a + 0x5C) = *(float *)(me + 0x5C);
                *mr_flag(a, MR_FLAG_NOLOCK) = 1;
                // v5.149.1 (owner 2026-11-11): drawn (mr_park(a,
                // false) above already cleared the hide); still not lockable, gone on its first hit.
                g_mr_state = 1;
                g_mr_out_at = GetTickCount64();
                set_status("SPIRITUAL PROJECTION: your projection strikes for 15 s");
                logf("spiritual projection: out beside player 1 (Hyperkick + Pursuit: %d, aura %.1f of %d)", copied, *(float *)(a + 0x5C), *(int *)(a + 0x58));
                return;
            }
            for (int k = 0; k < 4; k++) {                          // player 1's hand at this moment
                char *ms = (char *)(me + 0x388 + 0x50 * k), *ds = (char *)(a + 0x388 + 0x50 * k);
                int sk = *(int *)(ms + 0x30);
                if (*(int *)(ms + 8) && sk > 0 && sk != g_mr_id && sk != g_sp_id && sc_capture(ds, sk)) copied++;
            }
            g_mr_state = 1;
            g_mr_out_at = GetTickCount64();
            set_status("MIRROR: your double joins the fight - 30 s or one hit");
            logf("mirror: out beside player 1 with %d of your hand's skills", copied);
            return;
        }
        if (g_mr_state == 1) {                                     // out: its first hit sends it away
            *(int *)(a + FT_LOCK) = *(int *)(me + FT_LOCK);        // v5.137: it fights whoever player 1 is locked on to
            if (g_mr_mode == 1) *mr_flag(a, MR_FLAG_NOLOCK) = 1;           // v5.148.19 not lockable; v5.149.1 visible (no hide)
            if (st != MR_DOWN && *(int *)(a + 0x4C) > 0 && GetTickCount64() - g_mr_out_at > (g_mr_mode == 1 ? 15000u : 30000u)) {   // v5.129: 30 s up (projection 15 s)
                int cap = *(int *)(a + 0x50) > 0 ? *(int *)(a + 0x50) : 20;
                *(int *)(a + 0x4C) = cap;
                memcpy(pos, g_mr_park, 12);
                mr_park(a, true);
                g_mr_state = 0;
                logf("mirror: 30 s up - the double is parked again");
                return;
            }
            if (st == MR_DOWN || *(int *)(a + 0x4C) <= 0) {
                g_mr_hide_actor = a;
                memcpy(pos, g_mr_park, 12); memcpy(pos + 3, g_mr_park, 12);   // v5.188.16 out of the stage at once (owner: it stood there invisible)
                g_mr_state = 2;
                logf("mirror: hit - the double is gone (moved out of the stage)");
            }
            return;
        }
        if (g_mr_state == 2) {                                     // down, hidden: stand it up parked once its fall is over
            g_mr_hide_actor = a;
            memcpy(pos, g_mr_park, 12); memcpy(pos + 3, g_mr_park, 12);   // v5.188.16 kept out of the stage while down
            int *count = (int *)(a + 0x12F08);
            if (*count < 3) *count = 3;                            // the game would take it out at 0
            if (st != MR_DOWN) return;
            if (*(int *)(a + 0x1396C) != -1 || *(int *)(a + 0x13968) != 0) return;   // still falling
            PFN_REVIVE revive = (PFN_REVIVE)(g_base + RVA_REVIVE);
            revive(a);
            int cap = *(int *)(a + 0x50) > 0 ? *(int *)(a + 0x50) : 20;
            *(int *)(a + 0x4C) = cap;
            memcpy(pos, g_mr_park, 12);
            mr_park(a, true);
            g_mr_state = 0;
            logf("mirror: the double is parked again (life %d) - ready for the next Mirror", cap);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_mr_idx = -1; g_mr_hide_actor = 0; logf("mirror: fault - the double is left alone"); }
}

static int mr_self_test() {                                      // the set-up on stand-ins: no Mirror, Mirror, no free record
    char rec[4 * 0x48];
    static int cnt[4][400];
    ULONG_PTR ptrs[4] = {(ULONG_PTR)cnt[0], (ULONG_PTR)cnt[1], (ULONG_PTR)cnt[2], (ULONG_PTR)cnt[3]};
    float aura[4] = {1.5f, 0, 0, 0};
    int keep_id = g_mr_id;
    memset(rec, 0, sizeof(rec)); memset(cnt, 0, sizeof(cnt));
    *(int *)rec = 1; *(int *)(rec + 8) = 0x30005; *(int *)(rec + 0x10) = 1; *(int *)(rec + 0x18) = 0;
    *(int *)(rec + 0x48) = 1; *(int *)(rec + 0x48 + 4) = 1; *(int *)(rec + 0x48 + 0x18) = 1;
    g_mr_id = 381;
    bool none = mr_fill(rec, ptrs, 400, aura) == 0 && g_mr_idx == -1;
    cnt[0][381] = 3; cnt[0][61] = 2;
    bool made = mr_fill(rec, ptrs, 400, aura) == 1 && g_mr_idx == 2 && *(int *)(rec + 0x48 * 2) == 1 &&
                (*(int *)(rec + 0x48 * 2 + 8) & 0xFFFF) == 5 && *(int *)(rec + 0x48 * 2 + 0x10) == 0 &&
                *(int *)(rec + 0x48 * 2 + 0x18) == 2 && cnt[2][381] == 3 && cnt[2][61] == 2 && aura[2] == 1.5f;
    *(int *)(rec + 0x48 * 2) = 1; *(int *)(rec + 0x48 * 3) = 1;
    bool full = mr_fill(rec, ptrs, 400, aura) == 0 && g_mr_idx == -1;
    g_mr_id = keep_id; g_mr_idx = -1;
    logf("self-test: Mirror set-up - no Mirror %s, the double %s, no free record %s", none ? "ok" : "FAILED", made ? "ok" : "FAILED", full ? "ok" : "FAILED");
    return none && made && full ? 1 : -78;
}
