// v5.188.22 STR "charge + value" (owner 2026-11-16 skill review: Charged Ray 447 "Hold button to charge. STR = charge Level +2").
// Independently authored. Game facts (capstone of the clean exe 9ba7411b, 2026-11-16; recorded in MODLOG):
// - an attack's damage FUN_140272400(obj, skill): for family 0 (+0x52) and formula +0x56 != 31 it calls the formula table
//   0x1402dd0a0[+0x56](obj, skill), then adds FUN_14024d4d0(owner, 0) and +0x5A x uses;
// - formula 21 = 0x140274790: STR = record +0x58 x obj +0x1c (the aura spent on the charge) - Gungnir, Ki Cannon, Ki Palm ...
// The stock formulas have no "spent + value". The loader puts its own function in table slot 21: for the skills listed below
// (the BFR skill pack applied) it returns obj +0x1c + record +0x58; every other skill goes to the game's own formula 21
// unchanged. The slot must hold the stock function or nothing is changed.
static const ULONG_PTR RVA_STR_TABLE = 0x2dd0a0, RVA_STR_F21 = 0x274790;
typedef int (*PFN_STRF)(ULONG_PTR obj, int skill);
static PFN_STRF g_sp_f21;
static int sp_plus_skill(int skill) { return skill == 447 && g_bfr_applied; }   // Charged Ray (BFR skill pack)
static int sp_formula21(ULONG_PTR obj, int skill) {
    __try {
        if (sp_plus_skill(skill)) {
            const unsigned char *rec = *(const unsigned char **)(g_base + 0x4bdc50) + (ULONG_PTR)skill * 0x90;
            if (*(const short *)(rec + 0x52) == 0) {
                int spent = *(const int *)(obj + 0x1c), v = *(const short *)(rec + 0x58);
                static int s_last = -1;                       // logged when the charge changes (the game asks for STR often)
                if (spent != s_last) { s_last = spent; logf("STR formula 21: Charged Ray - charge %d + %d = STR %d (the game's own would be %d)", spent, v, spent + v, spent * v); }
                return spent + v;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_sp_f21(obj, skill);
}
static bool sp_install() {
    void **slot = (void **)(g_base + RVA_STR_TABLE + 21 * 8);
    if ((ULONG_PTR)*slot == (ULONG_PTR)sp_formula21) return true;
    if ((ULONG_PTR)*slot != g_base + RVA_STR_F21) { logf("STR formula 21: refused - the slot holds %p, not the stock %p", *slot, (void *)(g_base + RVA_STR_F21)); return false; }
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return false;
    g_sp_f21 = (PFN_STRF)*slot;
    *slot = (void *)sp_formula21;
    VirtualProtect(slot, sizeof(void *), old, &old);
    logf("STR formula 21: Charged Ray (447) = charge + its STR value; every other skill the game's own formula");
    return true;
}
