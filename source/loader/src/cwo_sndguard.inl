#include <initializer_list>
// ---- v5.186.2 sound guard: a fighter with no loaded sound bank no longer crashes the game when it uses a skill (owner
// 2026-11-14: "I press dark color, it crashes" (an NPC in BFR) and "the new character ... When I go to use a move, it
// crashes" (Sammira in MODS)). Both logs fault at exe+240abd: BFR 20:33:47 (NPC 3 / 4, character rows 32 / 33) and MODS
// 20:50:53 (EXTRAS row 47).
// Game facts (Ghidra full pass 065 via pd_fn.py + capstone, PDUWP 9ba7411b, 2026-11-14):
//  - FUN_140240a00 / FUN_140240870 play a sound: bank slot = *(int *)(0x140533708 + edx * 4) (edx = the 2nd argument),
//    bank = *(QWORD *)(0x140533760 + slot * 16), then read byte [bank + sound * 6] - with a null bank that read faults
//    (exe+240abd: movzx ecx, byte ptr [rax + rcx*2], rax = 0);
//  - both start with `cmp dword ptr [rip -> 0x140533704], 0` (sound on?) - 0x140240a1c and 0x140240883, 7 bytes
//    83 3D xx xx xx xx 00 - and when it is 0 they return -1 (the game's own "no sound" path).
// So each of those compares calls a stub that answers "sound off" (ZF = 1) when the bank of that call is null, and does the
// original compare otherwise. Only a call that would have crashed changes; every other sound plays as before.
static const ULONG_PTR SG_SITES[2] = {0x240a1c, 0x240883};
static const ULONG_PTR SG_SOUND_ON = 0x533704, SG_SLOT = 0x533708, SG_BANK = 0x533760;
static int g_sg_state;
// v5.186.3 (seen 2026-11-15, MODS, Sammira row 47: rax = 0x0A at exe+240abd): the bank entry can also hold a small junk value,
// not only null - so the stub answers "sound off" for a bank slot over 0xFF or a bank pointer under 0x10000 too.
static size_t sg_stub(unsigned char *p, ULONG_PTR at) {
    size_t n = 0, jbad[3], nj = 0;
    auto b = [&](std::initializer_list<int> v) { for (int x : v) p[n++] = (unsigned char)x; };
    auto rel = [&](ULONG_PTR target, size_t after) { LONGLONG d = (LONGLONG)target - (LONGLONG)(at + n + 4 + after); put32(p + n, d); n += 4; };
    b({0x50, 0x51});                                            // push rax; push rcx
    b({0x48, 0x63, 0xCA});                                      // movsxd rcx, edx
    b({0x48, 0x8D, 0x05}); rel(g_base + SG_SLOT, 0);            // lea rax, [slot table]
    b({0x8B, 0x04, 0x88});                                      // mov eax, [rax + rcx*4]
    b({0x48, 0x63, 0xC8});                                      // movsxd rcx, eax
    b({0x48, 0x81, 0xF9, 0xFF, 0x00, 0x00, 0x00});              // cmp rcx, 0xff
    b({0x77, 0x00}); jbad[nj++] = n - 1;                        // ja bad (also a negative slot)
    b({0x48, 0xC1, 0xE1, 0x04});                                // shl rcx, 4
    b({0x48, 0x8D, 0x05}); rel(g_base + SG_BANK, 0);            // lea rax, [bank table]
    b({0x48, 0x8B, 0x04, 0x08});                                // mov rax, [rax + rcx]
    b({0x48, 0x3D, 0x00, 0x00, 0x01, 0x00});                    // cmp rax, 0x10000
    b({0x72, 0x00}); jbad[nj++] = n - 1;                        // jb bad (null or junk)
    b({0x59, 0x58});                                            // pop rcx; pop rax
    b({0x83, 0x3D}); rel(g_base + SG_SOUND_ON, 1); b({0x00});   // cmp dword ptr [sound on], 0
    b({0xC3});                                                  // ret
    size_t bad = n;
    b({0x59, 0x58, 0x39, 0xC0, 0xC3});                          // bad: pop rcx; pop rax; cmp eax, eax (ZF = 1: "sound off"); ret
    for (size_t i = 0; i < nj; i++) p[jbad[i]] = (unsigned char)(bad - (jbad[i] + 1));
    return n;
}
static void sndguard_install() {                                // DllMain (host checked): before any game code runs
    static const unsigned char OLD0[3] = {0x83, 0x3D, 0x00};
    char why[96] = "";
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    bool ok = page != 0;
    if (!ok) strcpy_s(why, "no stub page near the image");
    for (int i = 0; ok && i < 2; i++) {
        const unsigned char *s = (const unsigned char *)(g_base + SG_SITES[i]);
        int d; memcpy(&d, s + 2, 4);
        if (memcmp(s, OLD0, 2) || s[6] != 0 || g_base + SG_SITES[i] + 7 + d != g_base + SG_SOUND_ON) { ok = false; sprintf_s(why, "site %d differs", i); }
        else if (!bigstage_reach(page, g_base + SG_SITES[i])) { ok = false; strcpy_s(why, "stub page out of reach"); }
    }
    if (ok) {
        size_t n = sg_stub((unsigned char *)page, page);
        FlushInstructionCache(GetCurrentProcess(), (void *)page, n);
        for (int i = 0; i < 2; i++) {
            ULONG_PTR site = g_base + SG_SITES[i];
            DWORD old = 0;
            if (!VirtualProtect((void *)site, 7, PAGE_EXECUTE_READWRITE, &old)) { ok = false; strcpy_s(why, "code not writable"); break; }
            bigstage_call((unsigned char *)site, site, page);   // call stub; nop; nop
            VirtualProtect((void *)site, 7, old, &old);
            FlushInstructionCache(GetCurrentProcess(), (void *)site, 7);
        }
    }
    g_sg_state = ok ? 1 : -1;
    if (ok) logf("sound guard: a sound whose bank is not loaded is skipped instead of crashing (exe+240a1c / +240883, stub %p)", (void *)page);
    else logf("sound guard: refused (%s) - nothing changed", why);
}
