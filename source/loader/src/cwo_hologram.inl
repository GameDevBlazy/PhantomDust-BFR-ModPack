// ---- v5.131 the Hologram skill (owner 2026-11-09 redesign: "a clone with no collision and no
// skills in hand; it copies the caster's model and current animation, walks ... toward where the caster is looking ... While
// it's out, no one can lock on to the caster (not even a team-mate) - opponents lock onto the hologram"). Offline only.
// Built like the Mirror (cwo_mirror.inl: a parked double of player 1 in a free record of player 1's team, set up at the
// battle's creation), with these differences - every one on recorded facts:
//  - its hand stays EMPTY (no capture) and its slot buttons stay blocked (rule table 0x1404bbd70, FUN_140256920);
//  - while it is out it carries status 40 "decoy (lock only on you)" (the game's own lure, recorded status list)
//    and player 1 carries status 8 "can't be locked on" (FUN_140270b00 case 0xc) - so locks land on the hologram;
//  - it appears 1.4 m in front of player 1, facing where player 1 looks (+0x12E68), and every motion request player 1's
//    scripts make (FUN_140252510, slot < 0x82 = the fighter's own motions) is made for it too (the MODS edition's motion hook);
//  - it is gone on its first hit (life 1) or after 8 s; then it is parked again (hidden, revived like the Mirror's double).
// Limits (v1, said plainly): plain running is the double's own CPU movement (locomotion is not a FUN_140252510 request, so
// it is not copied); it still has a body (no "no collision" flag is known yet).
static int g_ho_id = -1, g_ho_idx = -1, g_ho_state;                // state 0 parked, 1 out, 2 down, 3 gone
static float g_ho_park[3];
static bool g_ho_have_park;
static char g_ho_cpu[0x60];
static int g_ho_last_uses[4];
static ULONGLONG g_ho_out_at;
static float g_ho_dir[2];                                         // v5.137 the straight line it walks (player 1's facing at the cast)
// g_ho_actor / g_ho_me (declared in cwo_loader.cpp, read by the motion hook for the motion copy); g_ho_hide_actor in cwo_forge.inl
static const int HO_FLAG_DECOY = 40;
static const char *HO_NAME = "Hologram";

static void ho_ensure_skill() {
    for (int k = 0; k < g_ns_n; k++)
        if (!strcmp(g_ns[k].name, HO_NAME) && g_ns[k].src == 226) { g_ho_id = g_ns[k].id; return; }
    if (g_ns_n >= NS_MAX) return;
    int id = ns_free_id();
    if (id < 0) { logf("hologram: no free skill number - not made"); return; }
    NewSkill m;
    memset(&m, 0, sizeof(m));
    m.id = id; m.src = 226;
    strcpy_s(m.name, HO_NAME);
    strcpy_s(m.desc, "A hologram of you runs out where you look. Foes can lock only onto it. Gone on a hit or after 8 s.");
    m.school = m.cost = m.damage = m.fx = m.snd = m.anim = -1;
    static const int OFF[5] = {0x54, 0x56, 0x58, 0x5A, 0x5C};
    for (int i = 0; i < 5; i++) { m.fo[i] = OFF[i]; m.fv[i] = 0; }
    m.nfo = 5;
    m.dirty = 1;
    g_ns[g_ns_n++] = m;
    g_ho_id = id;
    ns_save();
    logf("hologram: the Hologram skill is skill %d", id);
}

// set-up (game thread, right after mr_fill): the hologram's double into another free record of player 1's team
static int ho_fill(char *rec, ULONG_PTR *ptrs, int nskills, float *aura) {
    g_ho_idx = -1; g_ho_state = 0; g_ho_have_park = false; g_ho_actor = 0; g_ho_me = 0;
    memset(g_ho_last_uses, 0, sizeof(g_ho_last_uses));
    memset(g_ho_hand5, 0, sizeof(g_ho_hand5)); memset(g_ho_proj, 0, sizeof(g_ho_proj));
    if (g_ho_id <= 0 || !*(int *)rec || !ptrs || !ptrs[0] || nskills <= g_ho_id) return 0;
    if (((int *)ptrs[0])[g_ho_id] <= 0 && !mr_test_doubles()) return 0;   // no Hologram in player 1's arsenal (or the test switch)
    int free_i = -1, used = 0;
    for (int p = 0; p < 4; p++) {
        char *r = rec + 0x48 * p;
        int s = *(int *)(r + 0x18);
        if (*(int *)r && s >= 0 && s < 4) used |= 1 << s;
        if (!*(int *)r && free_i < 0 && p > 0) free_i = p;
    }
    if (free_i < 0 || !ptrs[free_i]) { logf("hologram: no free fighter in this battle - no hologram"); return 0; }
    char *r = rec + 0x48 * free_i, *c = g_ho_cpu;
    int slot = 0;
    for (int k = 0; k < 4; k++) if (!(used & (1 << k))) { slot = k; break; }
    int row = *(int *)(rec + 8) & 0xFFFF;
    memset(c, 0, 0x60);
    *(int *)c = 1; *(int *)(c + 8) = 1; *(int *)(c + 0xc) = row; *(int *)(c + 0x10) = slot;
    *(int *)(c + 0x14) = 20; *(int *)(c + 0x18) = 20;
    strncpy_s(c + 0x20, 0x11, "Hologram", _TRUNCATE);
    lob_cpu_program(c);
    memcpy(r, rec, 0x48);
    *(int *)r = 1;
    *(int *)(r + 0xc) = -1; *(int *)(r + 0x10) = 0; *(int *)(r + 0x14) = 1; *(int *)(r + 0x18) = slot;
    *(ULONG_PTR *)(r + 0x20) = (ULONG_PTR)c;
    memset((void *)ptrs[free_i], 0, (size_t)(nskills - 1) * 4);     // an empty arsenal: nothing to draw
    if (aura) aura[free_i] = 0.0f;
    g_ho_idx = free_i;
    logf("hologram: player 1's hologram is fighter %d, start %d - parked until the Hologram is used", free_i + 1, slot);
    return 1;
}

static void ho_park(ULONG_PTR a, bool on) {
    mr_brain_keep(a, on);                                           // v5.148.15 inert while parked
    *mr_flag(a, MR_FLAG_NOLOCK) = on ? 1 : 0;
    *mr_flag(a, MR_FLAG_NODAMAGE) = on ? 1 : 0;
    *mr_flag(a, HO_FLAG_DECOY) = on ? 0 : 1;
    mr_rules(g_ho_idx, 1);                                          // never presses a slot
    g_ho_hide_actor = on ? a : 0;
}

static void ho_end(ULONG_PTR me) {                                  // player 1 lockable again
    if (me) *mr_flag(me, MR_FLAG_NOLOCK) = 0;
    g_ho_actor = 0; g_ho_me = 0;
}

static bool g_ho_started;
static float g_ho_sink_y;                                         // v5.188.16 the sink after its time
static ULONGLONG g_ho_sink_at;
static void ho_step(bool in_match) {
    if (in_match && g_ho_idx >= 0) __try { mr_rules(g_ho_idx, 1); } __except (EXCEPTION_EXECUTE_HANDLER) {}   // v5.148.4 never presses a slot: every frame of its match (the game clears the table at each match start)
                               // CwoOverlayFrame, game thread
    if (g_ho_idx < 0) { g_ho_started = false; return; }
    if (in_match) g_ho_started = true;
    if (!in_match && !g_ho_started) return;                       // v5.137.2: made before the match starts (as the Mirror)
    if (!in_match) {
        g_ho_started = false;
        __try { mr_rules(g_ho_idx, 0); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_ho_idx = -1; g_ho_hide_actor = 0; g_ho_actor = 0; g_ho_me = 0;
        return;
    }
    __try {
        ULONG_PTR *act = cwo_actors();
        ULONG_PTR me = act[0], a = act[g_ho_idx];
        if (!me || !a || !*(int *)(a + 0x20)) return;
        float *pos = (float *)(a + 0x12E40);
        if (!g_ho_have_park) { memcpy(g_ho_park, pos, 12); g_ho_park[1] += 3000.0f; g_ho_have_park = true; ho_park(a, true); }   // v5.148.15 parked high up
        int st = *(int *)(a + 0x12F04);
        bool me_down = *(int *)(me + 0x12F04) == MR_DOWN || !*(int *)(me + 0x24);
        if (me_down && g_ho_state != 3) { ho_end(me); ho_park(a, false); g_ho_hide_actor = a; *(int *)(a + 0x4C) = 0; g_ho_state = 3; return; }
        if (g_ho_state == 3) { g_ho_hide_actor = a; return; }
        if (g_ho_state == 0) {
            if (st != MR_DOWN) { memcpy(pos, g_ho_park, 12); memcpy(pos + 3, g_ho_park, 12); *mr_flag(a, MR_FLAG_NOLOCK) = 1; *mr_flag(a, MR_FLAG_NODAMAGE) = 1; *(int *)(a + 0x142d0) = 0; }   // v5.148.15 brain off while parked
            bool used = mr_cast_seen(me, g_ho_id, g_ho_hand5, g_ho_proj);
            if (!used && mr_test_cast("hologram")) used = true;
            if (!used) return;
            float yaw = (float)*(int *)(me + 0x12E68) / 5215.18896f + 1.5708f;
            g_ho_dir[0] = sinf(yaw); g_ho_dir[1] = cosf(yaw);
            const float *mp = (const float *)(me + 0x12E40);
            float np[3] = {mp[0] + sinf(yaw) * 14.0f, mp[1], mp[2] + cosf(yaw) * 14.0f};
            memcpy(pos, np, 12); memcpy(pos + 3, np, 12);
            *(int *)(a + 0x12E68) = *(int *)(me + 0x12E68);
            ho_park(a, false);
            *(int *)(a + 0x4C) = 1;
            *mr_flag(me, MR_FLAG_NOLOCK) = 1;                       // nobody can lock onto player 1 while it is out
            g_ho_me = me; g_ho_actor = a;
            g_ho_out_at = GetTickCount64();
            g_ho_state = 1;
            set_status("HOLOGRAM: foes can only lock onto your hologram (8 s or one hit)");
            logf("hologram: out in front of player 1");
            return;
        }
        if (g_ho_state == 1) {
            *mr_flag(me, MR_FLAG_NOLOCK) = 1;
            // v5.137: it walks the line it was cast on (x / z each frame; y is the game's, so it falls off edges), facing that way,
            // and every fighter of the other team is locked on to it (fighter +0x13F20 = the lock target)
            pos[0] += g_ho_dir[0] * 2.4f; pos[2] += g_ho_dir[1] * 2.4f;
            *(int *)(a + 0x12E68) = *(int *)(me + 0x12E68);
            {   ULONG_PTR *all = cwo_actors(); int n = cwo_nfighters(); if (n > 8) n = 8;
                for (int j = 0; j < n; j++) if (all[j] && j != g_ho_idx && *(int *)(all[j] + 0x20) && *(int *)(all[j] + 0x1c) != *(int *)(me + 0x1c))
                    *(int *)(all[j] + FT_LOCK) = g_ho_idx; }
            bool hit = st == MR_DOWN || *(int *)(a + 0x4C) <= 0, timeout = GetTickCount64() - g_ho_out_at > 8000;
            if (hit || timeout) {
                ho_end(me);
                if (timeout && !hit) {
                    // v5.188.16 (owner 2026-11-16): it sinks first (state 4)
                    *mr_flag(a, MR_FLAG_NOLOCK) = 1; *mr_flag(a, MR_FLAG_NODAMAGE) = 1;
                    g_ho_sink_y = pos[1]; g_ho_sink_at = GetTickCount64(); g_ho_state = 4;
                    logf("hologram: 8 s up - sinking through the floor");
                } else {                                            // v5.188.16 hit: out of the stage at once (nothing left standing there)
                    g_ho_hide_actor = a; memcpy(pos, g_ho_park, 12); memcpy(pos + 3, g_ho_park, 12);
                    g_ho_state = 2; logf("hologram: hit - gone (moved out of the stage)");
                }
            }
            return;
        }
        if (g_ho_state == 4) {                                     // v5.188.16 sinking through the floor, then parked out of sight
            *mr_flag(a, MR_FLAG_NOLOCK) = 1; *mr_flag(a, MR_FLAG_NODAMAGE) = 1; *(int *)(a + 0x142d0) = 0;
            ULONGLONG t = GetTickCount64() - g_ho_sink_at;
            float y = g_ho_sink_y - (float)t * 0.03f;               // 30 units (3 m) a second
            pos[1] = y; pos[4] = y;
            if (t > 1000) {
                *(int *)(a + 0x4C) = *(int *)(a + 0x50) > 0 ? *(int *)(a + 0x50) : 20;
                memcpy(pos, g_ho_park, 12); memcpy(pos + 3, g_ho_park, 12);
                ho_park(a, true); g_ho_state = 0;
                logf("hologram: sunk - parked out of the stage until the next Hologram");
            }
            return;
        }
        if (g_ho_state == 2) {
            g_ho_hide_actor = a;
            memcpy(pos, g_ho_park, 12); memcpy(pos + 3, g_ho_park, 12);   // v5.188.16 out of the stage while it is down
            int *count = (int *)(a + 0x12F08);
            if (*count < 3) *count = 3;
            if (st != MR_DOWN) return;
            if (*(int *)(a + 0x1396C) != -1 || *(int *)(a + 0x13968) != 0) return;
            ((PFN_REVIVE)(g_base + RVA_REVIVE))(a);
            *(int *)(a + 0x4C) = *(int *)(a + 0x50) > 0 ? *(int *)(a + 0x50) : 20;
            memcpy(pos, g_ho_park, 12); memcpy(pos + 3, g_ho_park, 12);
            ho_park(a, true);
            g_ho_state = 0;
            logf("hologram: parked again - ready for the next Hologram");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_ho_idx = -1; g_ho_hide_actor = 0; g_ho_actor = 0; logf("hologram: fault - left alone"); }
}

static int ho_self_test() {                                        // set-up on stand-ins: none / hologram (empty arsenal) / no free record
    char rec[4 * 0x48];
    static int cnt[4][400];
    ULONG_PTR ptrs[4] = {(ULONG_PTR)cnt[0], (ULONG_PTR)cnt[1], (ULONG_PTR)cnt[2], (ULONG_PTR)cnt[3]};
    float aura[4] = {1.5f, 0, 0.7f, 0};
    int keep = g_ho_id;
    memset(rec, 0, sizeof(rec)); memset(cnt, 0, sizeof(cnt));
    *(int *)rec = 1; *(int *)(rec + 8) = 0x30005; *(int *)(rec + 0x18) = 0;
    *(int *)(rec + 0x48) = 1; *(int *)(rec + 0x48 + 0x18) = 1;
    g_ho_id = 382;
    bool none = ho_fill(rec, ptrs, 400, aura) == 0 && g_ho_idx == -1;
    cnt[0][382] = 2; cnt[0][61] = 3; cnt[2][61] = 9;
    bool made = ho_fill(rec, ptrs, 400, aura) == 1 && g_ho_idx == 2 && *(int *)(rec + 0x48 * 2) == 1 && cnt[2][61] == 0 && cnt[2][382] == 0 &&
                aura[2] == 0.0f && *(int *)(rec + 0x48 * 2 + 0x18) == 2;
    *(int *)(rec + 0x48 * 3) = 1;
    bool full = ho_fill(rec, ptrs, 400, aura) == 0 && g_ho_idx == -1;
    g_ho_id = keep; g_ho_idx = -1;
    logf("self-test: Hologram set-up - none %s, hologram %s, no free record %s", none ? "ok" : "FAILED", made ? "ok" : "FAILED", full ? "ok" : "FAILED");
    return none && made && full ? 1 : -131;
}
