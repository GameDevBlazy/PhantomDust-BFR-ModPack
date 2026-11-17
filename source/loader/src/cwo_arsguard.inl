// ---- v5.28 arsenal guard (owner 2026-10-27, after a profile crashed on load with 5.28.0 - "This entire
// time, I had no idea why it crashed, so I just stayed off of it ... if it's something due to an arsenal, then I don't mind
// you replacing it with the aura particle"). The crash (loader log 2026-10-27 13:28:04, exe 14015b874): the arsenal check
// FUN_14015b830 read the skill table with the number 0x414c (16716) from arsenal 14's third slot; the table has 753 skills.
// Game facts (Ghidra full pass 065 via pcode.py, capstone 2026-10-27; tools/build_arsguard_hook.py):
//  - FUN_14015b830(?, rdx = arsenal record: +4 name, +0x14 30 skill numbers, -1 = empty) sets the record's flags; called
//    by FUN_14015a730 (every arsenal after a profile loads and before an arsenal list) and FUN_1402152d0; FUN_14015a8d0
//    reads the same numbers after it (skill table *0x1404bdc50, 0x90 a skill).
//  - every check skips -1, and a battle's capsule set fills empty slots with Aura Particles (30 - skills).
// So a number the game has no skill for becomes -1 (an empty slot: an Aura Particle in battle) before the check reads it;
// the game then saves the arsenal as fixed. Numbers 0-752 are never touched.
#include "cwo_arsguard_hook.h"
static const int ARG_SKILLS = 753;
static int g_arg_state;                                         // 1 on, -1 refused
static volatile LONG g_arg_fixed;

static int ars_guard_fix(unsigned char *rec) {                  // the slots changed
    int n = 0;
    for (int k = 0; k < 30; k++) {
        short *s = (short *)(rec + 0x14 + 2 * k);
        // v5.159 a number of the loader's own skill lists (the Illusion school 753-827) is a skill once the big tables point
        if (*s != -1 && (*s < 0 || (*s >= ARG_SKILLS && !bs_listed(*s)) || bfr_locked(*s) || bs_removed_now(*s))) {   // v5.110 removed skill numbers too   // v5.60 a locked boss skill: never in an arsenal
            if (InterlockedIncrement(&g_arg_fixed) <= 64) {
                char name[17];
                memcpy(name, rec + 4, 16); name[16] = 0;
                for (int i = 0; name[i]; i++) if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126) name[i] = '?';
                logf("arsenal guard: arsenal '%s' slot %d held %d (0x%04x), not a skill - now empty (an Aura Particle in battle)",
                     name, k + 1, (int)*s, (unsigned)(unsigned short)*s);
            }
            *s = -1;
            n++;
        }
    }
    return n;
}
static void __fastcall ars_guard(ULONG_PTR, ULONG_PTR rec) {    // the game thread, from the detour; offline only (AGENTS.md)
    __try { if (rec && !ed_online()) ars_guard_fix((unsigned char *)rec); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void arg_install() {                                     // DllMain: before any game code runs (all or nothing)
    if (memcmp((const void *)(g_base + ARG_SITE_RVA), ARG_SITE, sizeof(ARG_SITE))) {
        g_arg_state = -1;
        logf("arsenal guard: the arsenal check is not the expected code - not placed");
        return;
    }
    ULONG_PTR stub = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    long long d = (long long)stub - (long long)(g_base + ARG_SITE_RVA + 5);
    if (!stub || d < INT_MIN || d > INT_MAX) { g_arg_state = -1; logf("arsenal guard: no memory within reach for the detour"); return; }
    memcpy((void *)stub, ARG_STUB, sizeof(ARG_STUB));
    ULONG_PTR back = g_base + ARG_BACK_RVA, fn = (ULONG_PTR)&ars_guard;
    memcpy((void *)(stub + ARG_BACK_AT), &back, 8);
    memcpy((void *)(stub + ARG_FN_AT), &fn, 8);
    unsigned char j[sizeof(ARG_SITE)] = {0xE9};
    int rel = (int)d;
    memcpy(j + 1, &rel, 4);
    DWORD old;
    if (!VirtualProtect((void *)(g_base + ARG_SITE_RVA), sizeof(j), PAGE_EXECUTE_READWRITE, &old)) { g_arg_state = -1; return; }
    memcpy((void *)(g_base + ARG_SITE_RVA), j, sizeof(j));
    VirtualProtect((void *)(g_base + ARG_SITE_RVA), sizeof(j), old, &old);
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    g_arg_state = 1;
    logf("arsenal guard: the arsenal check calls the loader first (stub %p): numbers with no skill become empty slots", (void *)stub);
}

static int arg_self_test() {                                    // a stand-in record: the bad numbers go, the rest stay
    unsigned char rec[100];
    memset(rec, 0, sizeof(rec));
    strcpy_s((char *)rec + 4, 16, "TestArsenal");
    for (int k = 0; k < 30; k++) *(short *)(rec + 0x14 + 2 * k) = (short)(k < 20 ? k * 30 : -1);   // 0 .. 570, then empty
    *(short *)(rec + 0x14 + 4) = 0x414c;                       // the number from the crash
    *(short *)(rec + 0x14 + 10) = 752;                         // the last skill: kept
    *(short *)(rec + 0x14 + 12) = 753;                         // one past: goes
    *(short *)(rec + 0x14 + 50) = -7;                          // a negative number: goes
    LONG keep = g_arg_fixed;
    int n = ars_guard_fix(rec);
    g_arg_fixed = keep;
    bool ok = n == 3 && *(short *)(rec + 0x14 + 4) == -1 && *(short *)(rec + 0x14 + 10) == 752 && *(short *)(rec + 0x14 + 12) == -1 &&
              *(short *)(rec + 0x14 + 50) == -1 && *(short *)(rec + 0x14) == 0 && *(short *)(rec + 0x14 + 38) == 570 &&
              !strcmp((char *)rec + 4, "TestArsenal");
    ars_guard(0, 0);                                            // no record: nothing
    logf("self-test: arsenal guard %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -72;
}

static int bs_removed_self_test() {                             // v5.110: a removed number is cleared, an owned one kept
    int k_rm = g_bs_rm_n, k_ns = g_ns_n, k_bs = g_bs_n, k_bfr = g_bfr_n; short k0 = g_bs_rm[0], k1 = g_bs_rm[1]; int k_id = g_ns[0].id;
    g_bs_rm_n = 2; g_bs_rm[0] = 500; g_bs_rm[1] = 501; g_bs_n = 0; g_bfr_n = 0;
    g_ns_n = 1; g_ns[0].id = 501;                               // 501 is now a Skill Creator skill
    unsigned char rec[0x14 + 60]; memset(rec, 0, sizeof(rec));
    for (int k = 0; k < 30; k++) *(short *)(rec + 0x14 + 2 * k) = -1;
    *(short *)(rec + 0x14) = 500; *(short *)(rec + 0x16) = 501; *(short *)(rec + 0x18) = 61;
    int n = ars_guard_fix(rec);
    bool ok = n == 1 && *(short *)(rec + 0x14) == -1 && *(short *)(rec + 0x16) == 501 && *(short *)(rec + 0x18) == 61;
    g_bs_rm_n = k_rm; g_bs_rm[0] = k0; g_bs_rm[1] = k1; g_ns_n = k_ns; g_ns[0].id = k_id; g_bs_n = k_bs; g_bfr_n = k_bfr;
    logf("self-test: removed skills out of arsenals %s", ok ? "ok" : "FAILED");
    return ok ? 1 : -92;
}
