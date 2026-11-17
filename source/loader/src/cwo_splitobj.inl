// ---- v5.119 pack stages keep their stage objects in split screen (owner 2026-11-09 - Riftwalk Plaza's benches, lamps, glass railings, slabs were missing in every split-screen check).
// Game facts (Ghidra full pass 065 + capstone on PDUWP 9ba7411b; RTTI vftables of pdStageObjRep01 / Rep03; live read of the
// object list 2026-11-09, MODLOG):
//   - the stage object spawner FUN_140203dc0 builds every placed object (factory DAT_1402dc4f0[class][subclass]) and then,
//     when *0x14052ad0f == 1, calls each object's slot 64 (+0x200) with 0x30000: an object whose flags hold 0x10000 or
//     0x20000 deletes itself (FUN_1401e9420 / FUN_1401ed2b0 -> FUN_1401fdbb0; its destructor clears its list slot).
//   - *0x14052ad0f is set by FUN_1401d1b90 from FUN_140129ac0: 1 when the match origin *0x1404bca04 == 4 = split screen.
//   - the crumble part pdStageObjACrm sets 0x20000 in its constructor (FUN_1401dc790, class (2,3) = pdStageObjRep03), so in
//     split screen every crumble prop (benches, tables, lamps, glass railings, signs, slabs) is removed - the game's own
//     stages included. Seen live: 52 of Riftwalk's 96 list slots NULL in a split-screen match.
//   - the test is one instruction at 0x140204503: cmp byte ptr [rip -> 0x14052ad0f], 1 (80 3d disp32 01), then
//     mov r14, [rsp + 0xb0] (no flag change) and jne 0x140204575 (skips the deletion loop).
// What this does (VirtualProtect, FlushInstructionCache - official Windows documentation): the compare becomes a call to a
// stub that does the same compare, except while the stage being loaded (*0x140530264, as cwo_bigstage) is Map Slot 7
// (stage 10, the loader's packs): then it compares against 0xff, which the byte never holds, so the spawner keeps the
// objects. The game's own stages are unchanged. Nothing else reads the patched instruction; the byte itself is untouched
// (FUN_1401c9c00 / FUN_1401c9fb0 still see split screen).
static const ULONG_PTR SPLITOBJ_SITE = 0x204503, SPLITOBJ_FLAG = 0x52ad0f;
static const unsigned char SPLITOBJ_OLD[7] = {0x80, 0x3d, 0x05, 0x68, 0x32, 0x00, 0x01};   // cmp byte ptr [rip+0x326805], 1
static int g_splitobj_state;                                    // 1 on, -1 refused

// stub: cmp dword ptr [rip -> stage], 10; je packs; cmp dword ptr [rip -> stage], 30; je packs; cmp byte ptr [rip -> flag], 1;
// ret; packs: cmp byte ptr [rip -> flag], 0xff; ret
// v5.186.1 (owner 2026-11-14): stage 30 (the Underground, our six breakable campaign doors) keeps its objects too -
// the spawner had deleted the doors (class (2,3) crumble flag 0x20000) and left their collision as an invisible wall
static const int UG_STAGE = 30;
static size_t splitobj_stub(unsigned char *p, ULONG_PTR at, ULONG_PTR stage, ULONG_PTR flag) {
    p[0] = 0x83; p[1] = 0x3d; put32(p + 2, (LONGLONG)stage - (LONGLONG)(at + 7)); p[6] = (unsigned char)LIST_STAGE;
    p[7] = 0x74; p[8] = 0x11;                                   // je packs (at + 26)
    p[9] = 0x83; p[10] = 0x3d; put32(p + 11, (LONGLONG)stage - (LONGLONG)(at + 16)); p[15] = (unsigned char)UG_STAGE;
    p[16] = 0x74; p[17] = 0x08;                                 // je packs (at + 26)
    p[18] = 0x80; p[19] = 0x3d; put32(p + 20, (LONGLONG)flag - (LONGLONG)(at + 25)); p[24] = 0x01;
    p[25] = 0xc3;
    p[26] = 0x80; p[27] = 0x3d; put32(p + 28, (LONGLONG)flag - (LONGLONG)(at + 33)); p[32] = 0xff;
    p[33] = 0xc3;
    return 34;
}

static void splitobj_install() {                                // DllMain (host checked): before any game code runs
    char why[96] = "";
    ULONG_PTR site = g_base + SPLITOBJ_SITE;
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    bool ok = false;
    if (!page) strcpy_s(why, "no stub page near the image");
    else if (memcmp((void *)site, SPLITOBJ_OLD, 7)) strcpy_s(why, "site differs");
    else if (!bigstage_reach(page, site) || !bigstage_reach(g_base + SPLITOBJ_FLAG, page) || !bigstage_reach(g_base + RVA_REQ_STAGE, page))
        strcpy_s(why, "stub page out of reach");
    else {
        splitobj_stub((unsigned char *)page, page, g_base + RVA_REQ_STAGE, g_base + SPLITOBJ_FLAG);
        FlushInstructionCache(GetCurrentProcess(), (void *)page, 0x1000);
        DWORD old = 0;
        if (VirtualProtect((void *)site, 7, PAGE_EXECUTE_READWRITE, &old)) {
            bigstage_call((unsigned char *)site, site, page);   // call stub; nop; nop
            VirtualProtect((void *)site, 7, old, &old);
            FlushInstructionCache(GetCurrentProcess(), (void *)site, 7);
            ok = true;
        } else strcpy_s(why, "code not writable");
    }
    g_splitobj_state = ok ? 1 : -1;
    if (ok) logf("split-screen objects: Map Slot 7 packs and the Underground (stage 30) keep their stage objects in split screen (stub %p)", (void *)page);
    else logf("split-screen objects: refused (%s) - nothing changed", why);
}
