// ---- v5.185.0 CPUs think in Single Play (owner 2026-11-14: CPUs stand still in Single Play - "I think it could be because
// of the ENG program ... Enemy program"). Checked first: the original eneprog006.ssb made no difference (all five CPUs still).
// Game facts (Ghidra via REA + capstone on PDUWP 9ba7411b, 2026-11-14):
//  - FUN_14024ec80 (a fighter's frame) runs the CPU brain FUN_1401775b0 when the frame's fighter mask bit, fighter +0x20,
//    +0x24, *0x140533744 == 3, driver +0x30 == 0, brain +0x142d0 and CPU stage data *0x1404c0180 are set - all were, in game;
//  - FUN_1401775b0 calls the program step FUN_14017b220, which starts with `cmp dword ptr [0x1404bc9d4], 1` (0x14017b226,
//    bytes 83 3D A7 17 34 00 01) and runs the CPU program only when the battle type is NOT 1;
//  - the battle type (2 quest, 4 / 5 online, 6 split screen) read in a MULTIPLAYER > Single Play match: 1 - so every CPU
//    program was skipped (slots 20-29 stayed 0, positions never changed).
// So: while an OFFLINE match of type 1 has CPU fighters (driver 0 with a brain), the compare's 1 becomes 0x7F (a type that
// never occurs) and the programs run; outside such a match the byte goes back to 1. Online sessions are never touched.
static const ULONG_PTR CT1_SITE = 0x17b226;
static const unsigned char CT1_ORIG[7] = {0x83, 0x3D, 0xA7, 0x17, 0x34, 0x00, 0x01};
static int g_ct1_state;                                          // 0 not checked, 1 ready, -1 bytes not the game's
static bool g_ct1_on;
static void ct1_write(unsigned char v) {
    unsigned char *p = (unsigned char *)(g_base + CT1_SITE + 6);
    DWORD old;
    if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) return;
    *p = v;
    VirtualProtect(p, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
}
static void cputype1_step() {
    if (g_ct1_state == 0) {
        g_ct1_state = memcmp((void *)(g_base + CT1_SITE), CT1_ORIG, 6) == 0 &&
                      (*(unsigned char *)(g_base + CT1_SITE + 6) == 0x01 || *(unsigned char *)(g_base + CT1_SITE + 6) == 0x7F) ? 1 : -1;
        if (g_ct1_state < 0) { logf("CPU think: the program step's check is not the expected bytes - left alone"); return; }
    }
    if (g_ct1_state < 0) return;
    bool want = false;
    __try {
        if (g_in_match && !ed_online() && *(int *)(g_base + 0x4bc9d4) == 1) {
            ULONG_PTR *act = cwo_actors();
            for (int i = 0; i < SIXF_MAX && !want; i++) {
                ULONG_PTR a = act[i];
                if (a && *(int *)(a + 0x30) == 0 && *(int *)(a + 0x142d0) != 0) want = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { want = false; }
    if (want == g_ct1_on) return;
    ct1_write(want ? 0x7F : 0x01);
    g_ct1_on = want;
    logf(want ? "CPU think: an offline type-1 match (Single Play) with CPUs - their programs run (exe+0x17b22c 1 -> 0x7f)"
              : "CPU think: the program step's check back to the game's (type 1)");
}
