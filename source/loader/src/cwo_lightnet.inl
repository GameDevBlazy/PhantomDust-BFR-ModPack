// ---- v5.126 stage light safety net (owner 2026-11-09, for the green tint on
// fighters in stages that carry no light script of their own).
// Game facts (Ghidra, PDUWP 9ba7411b; MODLOG 2026-11-01 + 2026-11-09):
// - FUN_140199b00(light, variant, script) runs the stage light script (SSB function "mapLight": native 2.0 light sets
//   0-3, 2.2 fog, 2.5). With no script given it takes the stage .dat's section 0x33 + variant, then section 0x33; when
//   both are empty it loads the loose file "maplight%d.ssb" (variant 0) from .\Map, then .\script; when that is missing
//   too, no light script runs and the light sets keep the last values - the menus' set 1 is pure green (00 ff 00), so the
//   fighters stay green.
// - The match-time caller FUN_140204860 always passes light 0, variant 0 (tail jump at 0x140204a1f, call at 0x140204acb:
//   ecx = edx = 0). Seen in game: the Create a Stage template (st20, no light section) asked for
//   "Assets/Data/.\Map\maplight0.ssb" and ".\script\maplight0.ssb", both missing (file_requests.csv).
// - A loose .ssb has the same layout as a .dat light section (u32 0x20 header size first; stock boss scripts likewise).
// What this does: at worker start (Store mode) the loader puts the game's own neutral day light - Rooftop Day, the player's
// stock Map\st12.dat section 0x34 (968 bytes, SHA-256 66d1f8fd..., the same script build_pack_lights.py gives the day
// packs) - into the override folder as Assets\Data\Map\maplight0.ssb. The game reads it only through its own fallback,
// i.e. only for a stage that has no light of its own; every stage with a light section is unchanged. Explicit manifest:
// the script must match its hash, or nothing is written. A maplight0.ssb already there with other bytes is kept (files
// are preserved; logged). No game bytes ship with the loader: the file is cut from the player's own install.
static const unsigned char LN_SHA[32] = {0x66, 0xd1, 0xf8, 0xfd, 0x1e, 0x2d, 0x3d, 0xe5, 0x36, 0x3f, 0xae, 0xc9, 0x21, 0xd0, 0xa3, 0x06,
                                         0x6b, 0x12, 0x35, 0x63, 0x8d, 0xb4, 0x0e, 0x37, 0xe2, 0xab, 0xd2, 0x53, 0xf2, 0xb7, 0x55, 0x44};
static const unsigned LN_SECTION = 0x34, LN_SIZE = 968;

static void ln_install() {                                         // worker start, after pf_build_all
    if (!g_store_mode) { logf("light safety net: loose-copy mode - not used"); return; }
    wchar_t src[MAX_PATH], dst[MAX_PATH];
    if (swprintf_s(src, MAX_PATH, L"%sAssets\\Data\\Map\\st12.dat", g_install) < 0 ||
        swprintf_s(dst, MAX_PATH, L"%sAssets\\Data\\Map\\maplight0.ssb", g_over) < 0) return;
    DWORD n = 0;
    BYTE *d = pf_read(src, &n);
    const BYTE *blob = NULL;
    if (d && n > 4 + 0x40 * 4 && memcmp(d, "ST00", 4) == 0) {
        unsigned o = pf_u32(d + 4 + LN_SECTION * 4), end = n;
        for (unsigned i = 0; i < 0x40; i++) { unsigned x = pf_u32(d + 4 + i * 4); if (x != 0xFFFFFFFFu && x > o && x < end) end = x; }
        if (o != 0xFFFFFFFFu && o < n && end - o == LN_SIZE && pf_u32(d + o) == 0x20) {
            unsigned char h[32]; cwo_sha256(d + o, LN_SIZE, h);
            if (memcmp(h, LN_SHA, 32) == 0) blob = d + o;
        }
    }
    if (!blob) { logf("light safety net: Rooftop Day's light script not found as expected in st12.dat - not installed"); free(d); return; }
    DWORD cn = 0;
    BYTE *cur = pf_read(dst, &cn);
    if (cur && cn == LN_SIZE && memcmp(cur, blob, LN_SIZE) == 0) logf("light safety net: maplight0.ssb in place (Rooftop Day light)");
    else if (cur) logf("light safety net: a different maplight0.ssb is already in the override folder - kept as it is");
    else if (pf_write(dst, blob, LN_SIZE)) logf("light safety net: maplight0.ssb written (Rooftop Day light, for stages with no light of their own)");
    else logf("light safety net: could not write maplight0.ssb");
    free(cur); free(d);
}
