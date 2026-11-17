// ---- v5.87 the character select grid at 82 %, moved right, so COLORS fits on the far left over no portrait (owner
// 2026-11-03: "the colors option, when you select the character, needs to be on the far left side ... You don't want it
// to cover up any character or enemy portrait"; option 1 "smaller portraits": "1").
// Game facts (Ghidra batch_095 / batch_096, 2026-11-03; native table group 2 at 0x1403eede0):
//  - 2.82 (thunk 0x140207730) = FUN_1402469d0(slot, sprite, x, y): the sprite at its own size;
//  - 2.83 (thunk 0x140207760) = FUN_140246b10(slot, sprite, x, y, p, scale x, scale y) -> FUN_1401983e0(menu, handle, x, y,
//    p, &sx, &sy, 0, 0, 0) - the drawer multiplies the sprite's width / height by *sx / *sy;
//  - 2.159 (thunk 0x1402077a0) = FUN_140246c70(slot, sprite, x, y, colour) -> FUN_1401983e0(.., x, y, 0, 0, 0, &colour, 0, 0);
//    the handle = *(menu + 0x10548 + (slot & 0x3f) * 0x20)[sprite], valid while *(menu + 0x10530 + ..) != 0;
//  - 2.93 (thunk 0x140207990) = FUN_14029bb60(x, y, w, h, colour) a box; 2.87 (thunk 0x140207840) text, args[0] x, [1] y.
// The patched select scripts (tools/build_select_grid.py) send the grid's cell draws (routines 0x10f0 / 0x14a4 split screen)
// here instead: ops 106 (2.82), 107 (2.159), 108 (2.93), 109 (2.87); each moves the point (x, y) of the game's 640 x 480
// grid to GX + (x - 49) * S, GY + (y - 63) * S and scales the sprite / box, then calls the game's own draw.
static const float SG_S = 0.82f;
static const int SG_GX = 180, SG_GY = 63;
typedef INT64 (__fastcall *PFN_NATIVE)(INT64 *args);
typedef void (__fastcall *PFN_SPRDRAW)(ULONG_PTR menu, unsigned handle, int x, int y, int p, float *sx, float *sy, unsigned *colour, void *p9, void *p10);
static int sg_x(INT64 x) { return SG_GX + (int)(((int)x - 49) * SG_S + ((int)x >= 49 ? 0.5f : -0.5f)); }
static int sg_y(INT64 y) { return SG_GY + (int)(((int)y - 63) * SG_S + ((int)y >= 63 ? 0.5f : -0.5f)); }
static INT64 sg_native(int op, INT64 *args) {                    // args[0] = op, args[1..] = the game native's own args
    static bool logged;
    if (!logged) { logged = true; logf("select grid: the cells at %.0f %% from x %d (op %d first)", SG_S * 100, SG_GX, op); }
    __try {
        if (op == 106) {                                          // 2.82(slot, sprite, x, y) -> 2.83 scaled
            INT64 a[7] = {args[1], args[2], sg_x(args[3]), sg_y(args[4]), 0, 0, 0};
            float s = SG_S; memcpy(&a[5], &s, 4); memcpy(&a[6], &s, 4);
            return ((PFN_NATIVE)(g_base + 0x207760))(a);
        }
        if (op == 107) {                                          // 2.159(slot, sprite, x, y, colour) scaled
            ULONG_PTR menu = *(ULONG_PTR *)(g_base + 0x533c58);
            int slot = (int)args[1] & 0x3f, sprite = (int)args[2];
            if (!menu || !*(ULONG_PTR *)(menu + 0x10530 + slot * 0x20)) return 0;
            const unsigned *handles = *(const unsigned **)(menu + 0x10548 + slot * 0x20);
            if (!handles || sprite < 0 || sprite >= 128) return 0;
            float sx = SG_S, sy = SG_S; unsigned colour = (unsigned)args[5];
            ((PFN_SPRDRAW)(g_base + 0x1983e0))(menu, handles[sprite], sg_x(args[3]), sg_y(args[4]), 0, &sx, &sy, &colour, NULL, NULL);
            return 1;
        }
        if (op == 108) {                                          // 2.93(x, y, w, h, colour)
            INT64 a[5] = {sg_x(args[1]), sg_y(args[2]), (INT64)((int)args[3] * SG_S + 0.5f), (INT64)((int)args[4] * SG_S + 0.5f), args[5]};
            return ((PFN_NATIVE)(g_base + 0x207990))(a);
        }
        if (op == 109) {                                          // 2.87(x, y, ...) text: only moved
            INT64 a[8]; memcpy(a, args + 1, sizeof(a));
            a[0] = sg_x(a[0]); a[1] = sg_y(a[1]);
            return ((PFN_NATIVE)(g_base + 0x207840))(a);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
static int sg_self_test() {
    bool ok = sg_x(49) == SG_GX && sg_y(63) == SG_GY && sg_x(457) + (int)(134 * SG_S) <= 636 && sg_x(49) >= 4 + 170 + 4;
    logf("self-test: select grid %s (columns %d %d %d %d)", ok ? "ok" : "FAILED", sg_x(49), sg_x(185), sg_x(321), sg_x(457));
    return ok ? 1 : -88;
}
