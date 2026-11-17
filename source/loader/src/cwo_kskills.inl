// ---- v5.129 two support skills (owner 2026-11-09; owner 2026-11-10). Offline matches only (never online), like the Mirror.
// Game facts (recorded, no new guesses): fighter actor +0x40 = battle data, +0x4C life (battle +0x0C), +0x50 its cap,
// +0x1C team, +0x20 in the battle, +0x24 1 standing / 0 down, +0x12E40 position, +0x12E4C previous position (write both =
// a clean move, MODLOG 2026-11-08), +0x12E68 facing (angle units / 5215.18896 = radians, cwo_mirror.inl); hand slots at actor
// +0x388 + 0x50 k (+0x30 skill, +0x3c / +0x40 leave -1 while a use runs - the Mirror's use test); the game's revive
// FUN_140251f80 halves life and its cap (TAG_REVIVE_RESEARCH_2026-10-21; cwo_cpurevive.inl).
//  - "Revive Mend" (from Doppelganger 226 with its statuses taken out, as the Mirror): you and every team-mate standing within
//    6 m who has been revived (cap below the match's starting cap) get the full cap and full life back; if nobody has been
//    revived it heals you 5 life (up to your cap).
//  - "Body Pull": the nearest downed team-mate within 60 m is pulled to your side (1.2 m in front of you), ready for a revive.
static int g_ks_mend = -1, g_ks_pull = -1;
static int g_ks_cap0[8];
static int g_ks_last[8][4];
static const float KS_MEND_REACH = 60.0f, KS_PULL_REACH = 600.0f;

static int ks_make(const char *name, const char *desc) {
    for (int k = 0; k < g_ns_n; k++)
        if (!strcmp(g_ns[k].name, name) && g_ns[k].src == 226) return g_ns[k].id;
    if (g_ns_n >= NS_MAX) return -1;
    int id = ns_free_id();
    if (id < 0) { logf("support skills: no free skill number for %s", name); return -1; }
    NewSkill m;
    memset(&m, 0, sizeof(m));
    m.id = id; m.src = 226;
    strcpy_s(m.name, name);
    strcpy_s(m.desc, desc);
    m.school = m.cost = m.damage = m.fx = m.snd = m.anim = -1;
    static const int OFF[5] = {0x54, 0x56, 0x58, 0x5A, 0x5C};
    for (int i = 0; i < 5; i++) { m.fo[i] = OFF[i]; m.fv[i] = 0; }
    m.nfo = 5;
    m.dirty = 1;
    g_ns[g_ns_n++] = m;
    ns_save();
    logf("support skills: %s is skill %d", name, id);
    return id;
}

static void ks_ensure_skills() {
    g_ks_mend = ks_make("Revive Mend", "Undoes the revive penalty: you and revived team-mates near you get full life back. Else heals you 5.");
    g_ks_pull = ks_make("Body Pull", "Pulls the nearest downed team-mate to your side so you can revive them at once.");
}

// the effects, on plain fighter memory (also used by the self-test on stand-ins)
static int ks_mend(ULONG_PTR *act, int n, int me) {
    ULONG_PTR c = act[me];
    const float *pc = (const float *)(c + ACTOR_POS);
    int fixed = 0;
    for (int j = 0; j < n; j++) {
        ULONG_PTR b = act[j];
        if (!b || !*(int *)(b + 0x20) || *(int *)(b + 0x24) != 1 || *(int *)(b + 0x1C) != *(int *)(c + 0x1C)) continue;
        const float *pb = (const float *)(b + ACTOR_POS);
        float dx = pb[0] - pc[0], dy = pb[1] - pc[1], dz = pb[2] - pc[2];
        if (j != me && dx * dx + dy * dy + dz * dz > KS_MEND_REACH * KS_MEND_REACH) continue;
        int cap0 = g_ks_cap0[j];
        if (cap0 > 0 && *(int *)(b + 0x50) < cap0) {
            *(int *)(b + 0x50) = cap0;
            *(int *)(b + 0x4C) = cap0;
            fixed++;
        }
    }
    if (!fixed) {
        int cap = *(int *)(c + 0x50), life = *(int *)(c + 0x4C) + 5;
        *(int *)(c + 0x4C) = life > cap ? cap : life;
    }
    return fixed;
}

static int ks_pull(ULONG_PTR *act, int n, int me) {
    ULONG_PTR c = act[me];
    const float *pc = (const float *)(c + ACTOR_POS);
    int best = -1;
    float bd = KS_PULL_REACH * KS_PULL_REACH;
    for (int j = 0; j < n; j++) {
        ULONG_PTR b = act[j];
        if (j == me || !b || !*(int *)(b + 0x20) || *(int *)(b + 0x24) != 0 || *(int *)(b + 0x1C) != *(int *)(c + 0x1C)) continue;
        const float *pb = (const float *)(b + ACTOR_POS);
        float dx = pb[0] - pc[0], dy = pb[1] - pc[1], dz = pb[2] - pc[2], d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = j; }
    }
    if (best < 0) return -1;
    float yaw = (float)*(int *)(c + 0x12E68) / 5215.18896f + 1.5708f;
    float *pb = (float *)(act[best] + ACTOR_POS);
    float np[3] = {pc[0] + sinf(yaw) * 12.0f, pc[1], pc[2] + cosf(yaw) * 12.0f};
    memcpy(pb, np, 12);
    memcpy(pb + 3, np, 12);                                        // +0x12E4C previous position: a clean move
    return best;
}

static void ks_step(bool in_match) {                              // CwoOverlayFrame, game thread
    if (!in_match || ed_online() || (g_ks_mend <= 0 && g_ks_pull <= 0)) {
        memset(g_ks_cap0, 0, sizeof(g_ks_cap0)); memset(g_ks_last, 0, sizeof(g_ks_last));
        return;
    }
    __try {
        ULONG_PTR *act = cwo_actors();
        int n = cwo_nfighters();
        if (n > 8) n = 8;
        for (int i = 0; i < n; i++) {
            ULONG_PTR a = act[i];
            if (!a || !*(int *)(a + 0x20)) continue;
            if (!g_ks_cap0[i] && *(int *)(a + 0x50) > 0) g_ks_cap0[i] = *(int *)(a + 0x50);   // the match's starting cap
            if (*(int *)(a + 0x24) != 1) { memset(g_ks_last[i], 0, sizeof(g_ks_last[i])); continue; }
            for (int k = 0; k < 4; k++) {
                char *s = (char *)(a + 0x388 + 0x50 * k);
                int sk = *(int *)(s + 0x30);
                int busy = *(int *)(s + 0x3c) != -1 || *(int *)(s + 0x40) != -1;
                int on = busy && (sk == g_ks_mend || sk == g_ks_pull) ? sk : 0;
                if (on && !g_ks_last[i][k]) {
                    if (on == g_ks_mend) {
                        int f = ks_mend(act, n, i);
                        logf("Revive Mend: fighter %d - %d revived fighter(s) restored%s", i + 1, f, f ? "" : " (healed 5)");
                        if (i == 0) set_status(f ? "REVIVE MEND: full life restored" : "REVIVE MEND: +5 life");
                    } else {
                        int j = ks_pull(act, n, i);
                        logf("Body Pull: fighter %d pulled %s", i + 1, j >= 0 ? "a downed team-mate" : "nobody (no downed team-mate in reach)");
                        if (i == 0) set_status(j >= 0 ? "BODY PULL: your team-mate is beside you - revive them" : "BODY PULL: no downed team-mate in reach");
                    }
                }
                g_ks_last[i][k] = on;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { logf("support skills: fault - skipped this frame"); }
}

static int ks_self_test() {                                       // stand-in fighters: mend (revived / none), pull (downed / none)
    static char f[4][0x14000];
    memset(f, 0, sizeof(f));
    ULONG_PTR act[4] = {(ULONG_PTR)f[0], (ULONG_PTR)f[1], (ULONG_PTR)f[2], (ULONG_PTR)f[3]};
    for (int i = 0; i < 4; i++) {
        *(int *)(f[i] + 0x20) = 1; *(int *)(f[i] + 0x24) = 1; *(int *)(f[i] + 0x1C) = i < 2 ? 0 : 1;
        *(int *)(f[i] + 0x50) = 20; *(int *)(f[i] + 0x4C) = 20; g_ks_cap0[i] = 20;
        float *p = (float *)(f[i] + ACTOR_POS); p[0] = 10.0f * i; p[1] = 50.0f; p[2] = 0.0f;
    }
    *(int *)(f[1] + 0x50) = 10; *(int *)(f[1] + 0x4C) = 7;                 // team-mate revived (cap halved)
    *(int *)(f[3] + 0x50) = 10;                                             // opponent revived: not ours
    bool mend = ks_mend(act, 4, 0) == 1 && *(int *)(f[1] + 0x50) == 20 && *(int *)(f[1] + 0x4C) == 20 && *(int *)(f[3] + 0x50) == 10;
    *(int *)(f[0] + 0x4C) = 12;
    bool heal = ks_mend(act, 4, 0) == 0 && *(int *)(f[0] + 0x4C) == 17;
    *(int *)(f[1] + 0x24) = 0; ((float *)(f[1] + ACTOR_POS))[0] = 300.0f;   // team-mate down far away
    *(int *)(f[0] + 0x12E68) = 0;
    bool pull = ks_pull(act, 4, 0) == 1 && fabsf(((float *)(f[1] + ACTOR_POS))[0] - 12.0f) < 0.5f &&
                fabsf(((float *)(f[1] + ACTOR_POS + 12))[0] - 12.0f) < 0.5f;
    *(int *)(f[1] + 0x24) = 1;
    bool none = ks_pull(act, 4, 0) == -1;
    memset(g_ks_cap0, 0, sizeof(g_ks_cap0));
    logf("self-test: support skills - mend revived %s, mend heal %s, pull %s, pull none %s", mend ? "ok" : "FAILED", heal ? "ok" : "FAILED",
         pull ? "ok" : "FAILED", none ? "ok" : "FAILED");
    return mend && heal && pull && none ? 1 : -129;
}
