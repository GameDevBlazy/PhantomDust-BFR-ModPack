// ---- v5.49 per-skill hit radius (owner 2026-10-31: a hit box per skill).
// Game facts (PDUWP 9ba7411b, Ghidra full pass 065 + capstone; recorded skill facts): FUN_140263a00 makes a
// skill object with +0x14 = its skill record index and +0x190 = the hit sphere radius, 5.0 for every object
// (`mov dword ptr [rcx + 0x190], 0x40a00000` at 0x140263a82, 10 bytes); FUN_14026c1c0 tests skill radius + body radius.
// The stock records have no radius field, so the value comes from the loader's table: a skill list's option
// `hit=<cm>` (1 PD unit = 10 cm).
// What this does (official Windows documentation only: VirtualAlloc, VirtualProtect, FlushInstructionCache): a stub page
// near the exe (tools/build_hitbox.py assembled and checked it; cwo_hitbox_expect.h holds its bytes, the self-test
// compares) does the original store, then, when the record index is below 1,200 and the table holds a non-zero float for
// it, stores that radius instead. The site becomes `call stub; nop x5` after its bytes are checked. A restart undoes it.
#include "cwo_hitbox_expect.h"
static ULONG_PTR sixf2_near_alloc(SIZE_T n, DWORD prot);          // cwo_sixf2.inl (included later)
static const ULONG_PTR HITBOX_SITE = 0x263a82;
static const int HB_N = 1200;
static ULONG_PTR g_hb_page;
static float *g_hb_table;                                       // 1,200 floats, 0 = keep the game's 5.0
static int g_hb_state;                                          // 0 off, 1 on, -1 refused

static void hb_put32(unsigned char *p, LONGLONG v) { int d = (int)v; memcpy(p, &d, 4); }

static int hb_stub(unsigned char *out, ULONG_PTR page, ULONG_PTR cell) {   // the bytes build_hitbox.py checked, laid out here
    static const unsigned char head[] = {0xc7, 0x81, 0x90, 0x01, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x40,   // mov [rcx+0x190], 5.0
                                         0x50, 0x52,                                                  // push rax; push rdx
                                         0x8b, 0x41, 0x14,                                            // mov eax, [rcx+0x14]
                                         0x3d, 0xb0, 0x04, 0x00, 0x00,                                // cmp eax, 1200
                                         0x73, 0x19,                                                  // jae done
                                         0x48, 0x8b, 0x15, 0x00, 0x00, 0x00, 0x00,                    // mov rdx, [rip + cell]
                                         0x48, 0x85, 0xd2, 0x74, 0x0d,                                // test rdx, rdx; jz done
                                         0x8b, 0x04, 0x82, 0x85, 0xc0, 0x74, 0x06,                    // mov eax, [rdx+rax*4]; test; jz
                                         0x89, 0x81, 0x90, 0x01, 0x00, 0x00,                          // mov [rcx+0x190], eax
                                         0x5a, 0x58, 0xc3};                                           // pop rdx; pop rax; ret
    memcpy(out, head, sizeof(head));
    hb_put32(out + 25, (LONGLONG)cell - (LONGLONG)(page + 29));  // the rip-relative load (ends at page + 29)
    return (int)sizeof(head);
}

// place the stub at `page`, its cell at page + 0x100, and the call at base + HITBOX_SITE; false = the site is not stock
static bool hb_apply(ULONG_PTR base, ULONG_PTR page, bool live) {
    unsigned char *site = (unsigned char *)(base + HITBOX_SITE);
    if (memcmp(site, HITBOX_SITE_OLD, sizeof(HITBOX_SITE_OLD))) return false;
    unsigned char stub[64];
    int n = hb_stub(stub, page, page + 0x100);
    memcpy((void *)page, stub, n);
    *(float **)(page + 0x100) = live ? g_hb_table : NULL;
    unsigned char call[10] = {0xe8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90};
    hb_put32(call + 1, (LONGLONG)page - (LONGLONG)(base + HITBOX_SITE + 5));
    DWORD old;
    if (live && !VirtualProtect(site, 10, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(site, call, 10);
    if (live) { VirtualProtect(site, 10, old, &old); FlushInstructionCache(GetCurrentProcess(), site, 10); FlushInstructionCache(GetCurrentProcess(), (void *)page, 0x200); }
    return true;
}

static void hb_set(int id, float r) { if (g_hb_table && id >= 0 && id < HB_N) g_hb_table[id] = r; }

static void hb_install() {                                      // worker start
    g_hb_table = (float *)VirtualAlloc(NULL, HB_N * sizeof(float), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ULONG_PTR page = sixf2_near_alloc(0x1000, PAGE_EXECUTE_READWRITE);
    if (!g_hb_table || !page) { g_hb_state = -1; logf("hit radius: no memory near the exe - off"); return; }
    if (!hb_apply(g_base, page, true)) { g_hb_state = -1; logf("hit radius: the site at %llx is not the stock store - off", (unsigned long long)(g_base + HITBOX_SITE)); return; }
    g_hb_page = page; g_hb_state = 1;
    logf("hit radius: per-skill hit sphere on (stub %p, table %p)", (void *)page, (void *)g_hb_table);
}

static int hb_self_test() {                                     // the stub and the call on a stand-in image, against the checked bytes
    const SIZE_T img = 0x300000;
    unsigned char *base = (unsigned char *)VirtualAlloc(NULL, img + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) return -84;
    memcpy(base + HITBOX_SITE, HITBOX_SITE_OLD, sizeof(HITBOX_SITE_OLD));
    ULONG_PTR page = (ULONG_PTR)base + img;
    bool ok = hb_apply((ULONG_PTR)base, page, false);
    ok = ok && !memcmp((void *)page, HITBOX_EXPECT_STUB, sizeof(HITBOX_EXPECT_STUB)) && !memcmp(base + HITBOX_SITE, HITBOX_EXPECT_SITE, sizeof(HITBOX_EXPECT_SITE));
    bool again = hb_apply((ULONG_PTR)base, page, false);
    VirtualFree(base, 0, MEM_RELEASE);
    logf("self-test: hit-radius stub %s, second apply %s", ok ? "matches the checked bytes" : "DIFFERS", again ? "WRONGLY accepted" : "refused");
    return ok && !again ? 1 : -84;
}
