// ---- v5.180.0 body shape (owner 2026-11-13).
// Independently authored: the C++ port of our own 13_Independent_Research_Tools/fem_shape.py (same arithmetic, in the same
// order; the offline test CwoBodyShapeTest is checked byte for byte against that tool's files).
//
// Game facts used:
// - the vertex declarations (exe table 0x1402ddbc0, one pointer per vertex code, Xbox-style tokens STREAM / REG(register,
//   type) / END; read from the clean PDUWP.exe 2026-11-13, MODLOG): 0x11 = FLOAT3 position, NORMSHORT2 +12, D3DCOLOR +16,
//   SHORT2 +20 (24 bytes); 0x0b = FLOAT3 position, D3DCOLOR +12 (16); 0x07 = FLOAT3 position, NORMPACKED3 +12 (16);
// - skinned vertices (0x11) are stored in model space (pdchar2gltf.py, MODLOG): a moved position reshapes the bind pose and
//   every motion follows through the same weights; file size, counts, indices, weights and bones stay as they are;
// - a model group's chunk 0x16 lists its vertex buffers: u32 count +8, 28-byte entries from +12 (code +0, stride +1, vertex
//   count +8, offset into the resource +0x14); the resource offset is the file's u32 +0x0C (cwo_charbuild.inl).
//
// What it does: CwoBodyShape.txt names a model stem and its shape (band, radius, amount [, cx] - fractions of the body
// height). From the player's own clean game files (stem.alr, stem0-3.alr, never an override) the loader works out where each
// chest point moves (old position -> new position) and puts that on every file of the stem in its own game folder: the
// game's files as new override files, the loader's outfits 4-15 and worn / saved outfits as they are opened. Only exact old
// positions move, so a second pass changes nothing. Removing a line moves every point back (new -> old) and drops the
// overrides that are the game's own file again; CwoBodyShape_applied.txt remembers what this machine's files carry (it is
// this machine's state and never shipped). Files the pack builds from patches are left alone (they would be rebuilt).
#include <unordered_map>
#include <map>
#include <string>

struct BshSet { char stem[8]; double band, radius, amount, cx; bool has_cx; };
struct BshKey {
    unsigned v[3];
    bool operator==(const BshKey &o) const { return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2]; }
};
struct BshKeyHash { size_t operator()(const BshKey &k) const { return (size_t)k.v[0] * 0x9E3779B1u ^ (size_t)k.v[1] * 0x85EBCA77u ^ (size_t)k.v[2] * 0xC2B2AE3Du; } };
typedef std::unordered_map<BshKey, BshKey, BshKeyHash> BshMap;
struct BshBuf { size_t pos; unsigned code, stride, count; bool body; };

static SRWLOCK g_bsh_lock = SRWLOCK_INIT;
static volatile LONG g_bsh_ready;
static std::vector<BshSet> g_bsh_sets;                              // the active lines
static std::map<std::string, BshMap> g_bsh_maps;                    // stem -> old -> new (built once from the game's files)
static std::map<std::wstring, ULONGLONG> g_bsh_seen;               // override path -> size ^ write time already checked

static unsigned bs32(const BYTE *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

// "stem band radius amount [cx]" (# starts a comment)
static bool bsh_parse_line(const char *line, BshSet &s) {
    char st[16] = "";
    double v[4] = {0, 0, 0, 0};
    int n = sscanf_s(line, " %15s %lf %lf %lf %lf", st, (unsigned)sizeof(st), &v[0], &v[1], &v[2], &v[3]);
    if (n < 4 || st[0] == '#') return false;
    size_t sl = strlen(st);
    for (size_t i = 0; i < sl; i++) st[i] = (char)tolower((unsigned char)st[i]);
    bool pc = sl == 5 && !strncmp(st, "pc", 2) && isdigit((unsigned char)st[2]) && isdigit((unsigned char)st[3]) && st[4] == 'a';
    bool en = sl == 6 && !strncmp(st, "enm", 3) && isdigit((unsigned char)st[3]) && isdigit((unsigned char)st[4]) && st[5] == 'a';
    if (!pc && !en) return false;
    if (!(v[0] > 0.3 && v[0] < 0.95) || !(v[1] > 0.0 && v[1] <= 0.15) || !(v[2] > 0.0 && v[2] <= 0.06)) return false;   // a modest shape only
    if (n >= 5 && !(v[3] > 0.0 && v[3] < 0.1)) return false;
    strcpy_s(s.stem, st);
    s.band = v[0]; s.radius = v[1]; s.amount = v[2]; s.cx = n >= 5 ? v[3] : 0; s.has_cx = n >= 5;
    return true;
}
static bool bsh_same(const BshSet &a, const BshSet &b) {
    return !strcmp(a.stem, b.stem) && a.band == b.band && a.radius == b.radius && a.amount == b.amount && a.has_cx == b.has_cx && a.cx == b.cx;
}
static void bsh_read_sets(const wchar_t *path, std::vector<BshSet> &out) {
    out.clear();
    FILE *f = NULL;
    if (_wfopen_s(&f, path, L"r") || !f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        BshSet s;
        if (!bsh_parse_line(line, s)) continue;
        bool dup = false;
        for (const BshSet &o : out) dup |= !strcmp(o.stem, s.stem);
        if (!dup) out.push_back(s);
    }
    fclose(f);
}

// <...>\<stem>\<stem>[k].alr -> stem, k (-1 = no number)
static bool bsh_parse_name(LPCWSTR f, char *stem, int *k) {
    if (!f) return false;
    size_t n = wcslen(f);
    if (n < 9 || _wcsicmp(f + n - 4, L".alr")) return false;
    const wchar_t *e = f + n - 4, *d = e;
    while (d > f && iswdigit(d[-1])) d--;
    if (e - d > 2) return false;
    const wchar_t *s = d;
    while (s > f && s[-1] != L'\\' && s[-1] != L'/') s--;
    size_t sl = (size_t)(d - s);
    if (sl < 5 || sl > 6) return false;
    char st[8] = "";
    for (size_t i = 0; i < sl; i++) { if (s[i] > 127) return false; st[i] = (char)towlower(s[i]); }
    bool pc = sl == 5 && !strncmp(st, "pc", 2) && isdigit((unsigned char)st[2]) && isdigit((unsigned char)st[3]) && st[4] == 'a';
    bool en = sl == 6 && !strncmp(st, "enm", 3) && isdigit((unsigned char)st[3]) && isdigit((unsigned char)st[4]) && st[5] == 'a';
    if (!pc && !en) return false;
    int kk = d == e ? -1 : _wtoi(d);
    if (kk > 14) return false;
    strcpy_s(stem, 8, st);
    *k = kk;
    return true;
}

// every vertex buffer of the file's model groups, in group order; the first group with buffers is the body
static bool bsh_buffers(const BYTE *b, size_t n, std::vector<BshBuf> &out) {
    out.clear();
    if (n < 0x20 || bs32(b) != 0x11) return false;
    unsigned ro = bs32(b + 0x0C), cnt = bs32(b + 0x10);
    if (!ro || ro >= n || 0x20 + 4ull * cnt > n) return false;
    std::vector<unsigned> seen;
    bool body_done = false;
    for (unsigned k = 0; k < cnt; k++) {
        int go = (int)bs32(b + 0x20 + 4 * k);
        if (go <= 0 || (size_t)go >= n || std::find(seen.begin(), seen.end(), (unsigned)go) != seen.end()) continue;
        seen.push_back((unsigned)go);
        size_t first = out.size();
        bool has = false;
        for (size_t i = (size_t)go;;) {
            if (i + 8 > n) return false;
            unsigned id = bs32(b + i), sp = bs32(b + i + 4);
            if (id == 0) break;
            if (sp < 8 || i + sp > n) return false;
            if (id == 0x16) {
                unsigned nb = bs32(b + i + 8);
                if (12 + 28ull * nb > sp) return false;
                for (unsigned q = 0; q < nb; q++) {
                    const BYTE *d = b + i + 12 + 28 * q;
                    BshBuf v;
                    v.code = d[0]; v.stride = d[1]; v.count = bs32(d + 8); v.pos = (size_t)ro + bs32(d + 0x14); v.body = false;
                    if (v.stride < 12 || v.pos + (size_t)v.stride * v.count > n) return false;
                    out.push_back(v);
                }
                has = true;
            }
            i += sp;
        }
        if (has && !body_done) { for (size_t q = first; q < out.size(); q++) out[q].body = true; body_done = true; }
    }
    return body_done;
}
static void bsh_xyz(const BYTE *p, double *o) { float f[3]; memcpy(f, p, 12); o[0] = f[0]; o[1] = f[1]; o[2] = f[2]; }
static BshKey bsh_key(const BYTE *p) { BshKey k; memcpy(k.v, p, 12); return k; }
static bool bsh_shaped_code(unsigned c) { return c == 0x11 || c == 0x0b || c == 0x07; }

// the body's bounds (the one skinned buffer of the body group) and whether a buffer shares its model space
static bool bsh_body(const BYTE *b, const std::vector<BshBuf> &bufs, const BshBuf **body, double *mn, double *mx) {
    int nb = 0;
    for (const BshBuf &v : bufs) if (v.body && v.code == 0x11) { *body = &v; nb++; }
    if (nb != 1 || !(*body)->count) return false;
    for (int a = 0; a < 3; a++) { mn[a] = 1e300; mx[a] = -1e300; }
    for (unsigned k = 0; k < (*body)->count; k++) {
        double q[3];
        bsh_xyz(b + (*body)->pos + (size_t)(*body)->stride * k, q);
        for (int a = 0; a < 3; a++) { if (q[a] < mn[a]) mn[a] = q[a]; if (q[a] > mx[a]) mx[a] = q[a]; }
    }
    return true;
}
static bool bsh_same_space(const BYTE *b, const BshBuf &v, const double *bmn, const double *bmx) {
    double lo = 1e300, hi = -1e300, H = bmx[1] - bmn[1];
    for (unsigned k = 0; k < v.count; k++) {
        double q[3];
        bsh_xyz(b + v.pos + (size_t)v.stride * k, q);
        if (q[1] < lo) lo = q[1];
        if (q[1] > hi) hi = q[1];
    }
    return v.count && lo > bmn[1] - 0.15 * H && hi < bmx[1] + 0.15 * H;
}

// fem_shape.shape on one file: every moved point's old -> new position into m (the first file to name a point wins)
static int bsh_make(const BYTE *b, size_t n, const BshSet &p, BshMap &m) {
    std::vector<BshBuf> bufs;
    const BshBuf *body = NULL;
    double bmn[3], bmx[3];
    if (!bsh_buffers(b, n, bufs) || !bsh_body(b, bufs, &body, bmn, bmx)) return -1;
    double y0 = bmn[1], H = bmx[1] - bmn[1];
    double cy = y0 + p.band * H;
    double zf = 1e300, zb = -1e300;
    int nband = 0;
    for (unsigned k = 0; k < body->count; k++) {
        double q[3];
        bsh_xyz(b + body->pos + (size_t)body->stride * k, q);
        if (fabs(q[1] - cy) < 0.04 * H && fabs(q[0]) < 0.10 * H) { if (q[2] < zf) zf = q[2]; if (q[2] > zb) zb = q[2]; nband++; }
    }
    if (!nband) return -2;
    double cx;
    if (p.has_cx) cx = p.cx * H;
    else {
        double s = 0;
        int nf = 0;
        for (unsigned k = 0; k < body->count; k++) {
            double q[3];
            bsh_xyz(b + body->pos + (size_t)body->stride * k, q);
            if (fabs(q[1] - cy) < 0.04 * H && fabs(q[0]) < 0.10 * H && q[2] < zf + 0.25 * (zb - zf)) { s += fabs(q[0]); nf++; }
        }
        double avg = s / nf;
        cx = 0.035 * H >= avg ? 0.035 * H : avg;
    }
    double R = p.radius * H, A = p.amount * H, cz = zf + 0.3 * R, split = zf + 0.5 * (zb - zf);
    double ux[2] = {-cx, cx};
    int moved = 0;
    for (const BshBuf &v : bufs) {
        if (!bsh_shaped_code(v.code) || !bsh_same_space(b, v, bmn, bmx)) continue;
        for (unsigned k = 0; k < v.count; k++) {
            const BYTE *at = b + v.pos + (size_t)v.stride * k;
            double q[3];
            bsh_xyz(at, q);
            double x = q[0], y = q[1], z = q[2];
            if (z > split) continue;
            double dx = 0, dy = 0, dz = 0;
            for (int c = 0; c < 2; c++) {
                double ex = x - ux[c], ey = y - cy, ez = z - cz;
                double d = sqrt(ex * ex + ey * ey + ez * ez);
                if (d >= R) continue;
                double u = d / R, w = 1.0 - u * u;
                w = w * w;
                double t = fabs(x) / cx;
                if (!(t < 1.0)) t = 1.0;
                w *= 0.4 + 0.6 * t * t * (3 - 2 * t);
                dz -= A * w;
                dy -= 0.20 * A * w;
                dx += copysign(0.10 * A * w, x != 0.0 ? x : ux[c]);
            }
            if (dx != 0.0 || dy != 0.0 || dz != 0.0) {
                float nf3[3] = {(float)(x + dx), (float)(y + dy), (float)(z + dz)};
                BshKey o = bsh_key(at), nw;
                memcpy(nw.v, nf3, 12);
                if (m.find(o) == m.end()) m[o] = nw;
                moved++;
            }
        }
    }
    return moved;
}

// the map on a file: points at an exact key move; returns how many moved (-1 not a model with one skinned body)
static int bsh_apply(BYTE *b, size_t n, const BshMap &m) {
    std::vector<BshBuf> bufs;
    const BshBuf *body = NULL;
    double bmn[3], bmx[3];
    if (!bsh_buffers(b, n, bufs) || !bsh_body(b, bufs, &body, bmn, bmx)) return -1;
    std::vector<bool> use(bufs.size());
    for (size_t i = 0; i < bufs.size(); i++) use[i] = bsh_shaped_code(bufs[i].code) && bsh_same_space(b, bufs[i], bmn, bmx);
    int moved = 0;
    for (size_t i = 0; i < bufs.size(); i++) {
        if (!use[i]) continue;
        for (unsigned k = 0; k < bufs[i].count; k++) {
            BYTE *at = b + bufs[i].pos + (size_t)bufs[i].stride * k;
            auto it = m.find(bsh_key(at));
            if (it == m.end()) continue;
            memcpy(at, it->second.v, 12);
            moved++;
        }
    }
    return moved;
}

static void bsh_game_path(const char *stem, int k, wchar_t *out) {             // the game's own file (never an override)
    if (k < 0) swprintf_s(out, MAX_PATH, L"%sAssets\\Data\\Player\\%S\\%S.alr", g_install, stem, stem);
    else swprintf_s(out, MAX_PATH, L"%sAssets\\Data\\Player\\%S\\%S%d.alr", g_install, stem, stem, k);
}
// the old -> new map of a shape, from the game's own stem.alr and stem0-3.alr; refused when a new point is an old one
// (a second pass would move it again)
static bool bsh_build_map(const BshSet &p, BshMap &m) {
    m.clear();
    int files = 0;
    for (int k = -1; k <= 3; k++) {
        wchar_t gp[MAX_PATH];
        bsh_game_path(p.stem, k, gp);
        DWORD n = 0;
        BYTE *b = pf_read(gp, &n);
        if (!b) continue;
        int r = bsh_make(b, n, p, m);
        free(b);
        if (r > 0) files++;
    }
    for (const auto &e : m) if (m.find(e.second) != m.end()) { logf("body shape: %s refused (a moved point lands on another point)", p.stem); m.clear(); return false; }
    if (!files) logf("body shape: %s - none of the game's files of this model is readable", p.stem);
    return files > 0;
}
static void bsh_inverse(const BshMap &f, BshMap &inv) {
    inv.clear();
    for (const auto &e : f) inv[e.second] = e.first;
}

static bool bsh_has_patch(const wchar_t *rel) {                    // a file the pack builds from a patch: not ours to change
    wchar_t pp[MAX_PATH];
    if (swprintf_s(pp, MAX_PATH, L"%spatches\\%s.cwopatch", g_dir, rel) < 0) return true;
    return GetFileAttributesW(pp) != INVALID_FILE_ATTRIBUTES;
}
static ULONGLONG bsh_stamp(const wchar_t *path) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a)) return 0;
    return ((ULONGLONG)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime) ^ ((ULONGLONG)a.nFileSizeLow << 7);
}
// the map on one override file (written only when something moved); 1 changed, 0 nothing to do, < 0 failed
static int bsh_fix_file(const wchar_t *path, const BshMap &m, int *pts) {
    *pts = 0;
    DWORD n = 0;
    BYTE *b = pf_read(path, &n);
    if (!b) return -1;
    int r = bsh_apply(b, n, m);
    int out = r < 0 ? -2 : r == 0 ? 0 : pf_write(path, b, n) ? 1 : -3;
    free(b);
    if (r > 0) *pts = r;
    return out;
}
static bool bsh_equal_files(const wchar_t *a, const wchar_t *b) {
    DWORD na = 0, nb = 0;
    BYTE *x = pf_read(a, &na), *y = pf_read(b, &nb);
    bool same = x && y && na == nb && !memcmp(x, y, na);
    free(x); free(y);
    return same;
}

// one stem's folder: back (inverse map) or on (forward map, plus the game's own files as new shaped overrides)
static int bsh_sweep(const char *stem, const BshMap &m, bool forward) {
    wchar_t dir[MAX_PATH], pat[MAX_PATH];
    swprintf_s(dir, MAX_PATH, L"%sAssets\\Data\\Player\\%S\\", g_over, stem);
    swprintf_s(pat, MAX_PATH, L"%s*.alr", dir);
    int changed = 0, pts_all = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    std::vector<std::wstring> names;
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names.push_back(fd.cFileName); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (const std::wstring &nm : names) {
        char st[8]; int k;
        if (!bsh_parse_name(nm.c_str(), st, &k) || strcmp(st, stem)) continue;      // pc02a1 - Copy.alr, .bak ... left alone
        wchar_t path[MAX_PATH], rel[MAX_PATH];
        swprintf_s(rel, MAX_PATH, L"Assets\\Data\\Player\\%S\\%s", stem, nm.c_str());
        swprintf_s(path, MAX_PATH, L"%s%s", g_over, rel);
        if (bsh_has_patch(rel)) { logf("body shape: %ls is built from a pack patch - left as it is", rel); continue; }
        int pts = 0, r = bsh_fix_file(path, m, &pts);
        if (r == 1) { changed++; pts_all += pts; }
        if (r < 0) logf("body shape: %ls not changed (%s)", rel, r == -1 ? "unreadable" : r == -2 ? "not a model with one skinned body" : "write failed");
        if (!forward && k <= 3) {                                  // back to the game's own file: the override goes
            wchar_t gp[MAX_PATH];
            bsh_game_path(stem, k, gp);
            if (bsh_equal_files(path, gp) && DeleteFileW(path)) logf("body shape: %ls is the game's file again - override removed", rel);
        }
    }
    if (forward) {
        for (int k = -1; k <= 3; k++) {                            // the game's own files: shaped overrides beside them
            wchar_t gp[MAX_PATH], ov[MAX_PATH], rel[MAX_PATH];
            bsh_game_path(stem, k, gp);
            if (k < 0) swprintf_s(rel, MAX_PATH, L"Assets\\Data\\Player\\%S\\%S.alr", stem, stem);
            else swprintf_s(rel, MAX_PATH, L"Assets\\Data\\Player\\%S\\%S%d.alr", stem, stem, k);
            swprintf_s(ov, MAX_PATH, L"%s%s", g_over, rel);
            if (GetFileAttributesW(ov) != INVALID_FILE_ATTRIBUTES || bsh_has_patch(rel)) continue;
            DWORD n = 0;
            BYTE *b = pf_read(gp, &n);
            if (!b) continue;
            int r = bsh_apply(b, n, m);
            if (r > 0 && pf_write(ov, b, n)) { changed++; pts_all += r; }
            free(b);
        }
    }
    logf("body shape: %s %s - %d files changed, %d points", stem, forward ? "on" : "back to the original", changed, pts_all);
    return changed;
}

static void bsh_path(wchar_t *out, const wchar_t *name) { swprintf_s(out, MAX_PATH, L"%s%s", g_dir, name); }
static void bsh_startup() {                                         // worker start, after the pack's patches are built
    if (!g_store_mode || !g_over[0]) return;
    AcquireSRWLockExclusive(&g_bsh_lock);
    wchar_t sp[MAX_PATH], ap[MAX_PATH];
    bsh_path(sp, L"CwoBodyShape.txt");
    bsh_path(ap, L"CwoBodyShape_applied.txt");
    std::vector<BshSet> want, had, now;
    bsh_read_sets(sp, want);
    bsh_read_sets(ap, had);
    for (const BshSet &o : had) {                                   // shapes this machine's files carry that changed or went
        bool keep = false;
        for (const BshSet &w : want) keep |= bsh_same(o, w);
        if (keep) continue;
        BshMap f, inv;
        if (!bsh_build_map(o, f)) continue;
        bsh_inverse(f, inv);
        bsh_sweep(o.stem, inv, false);
    }
    for (const BshSet &w : want) {
        BshMap f;
        if (!bsh_build_map(w, f)) continue;
        bsh_sweep(w.stem, f, true);
        g_bsh_maps[w.stem] = f;
        now.push_back(w);
    }
    g_bsh_sets = now;
    FILE *f = NULL;
    if (!_wfopen_s(&f, ap, L"w") && f) {
        fprintf(f, "# what this machine's model files carry (the loader writes this file; it is not a setting)\n");
        for (const BshSet &w : now) {
            if (w.has_cx) fprintf(f, "%s %.17g %.17g %.17g %.17g\n", w.stem, w.band, w.radius, w.amount, w.cx);
            else fprintf(f, "%s %.17g %.17g %.17g\n", w.stem, w.band, w.radius, w.amount);
        }
        fclose(f);
    }
    InterlockedExchange(&g_bsh_ready, 1);
    ReleaseSRWLockExclusive(&g_bsh_lock);
}

// the game is about to open f (after of_ensure built a missing outfit): a shaped stem's file gets the shape first
static void bsh_on_open(LPCWSTR f) {
    if (!g_bsh_ready) return;
    char stem[8]; int k;
    if (!bsh_parse_name(f, stem, &k)) return;
    AcquireSRWLockExclusive(&g_bsh_lock);
    auto mi = g_bsh_maps.find(stem);
    wchar_t rel[MAX_PATH], ov[MAX_PATH];
    if (mi != g_bsh_maps.end() && sm_relative(f, rel, MAX_PATH) && !bsh_has_patch(rel) && swprintf_s(ov, MAX_PATH, L"%s%s", g_over, rel) > 0) {
        ULONGLONG st = bsh_stamp(ov);
        if (st) {
            auto si = g_bsh_seen.find(ov);
            if (si == g_bsh_seen.end() || si->second != st) {
                int pts = 0, r = bsh_fix_file(ov, mi->second, &pts);
                if (r == 1) logf("body shape: %ls shaped when opened (%d points)", rel, pts);
                g_bsh_seen[ov] = bsh_stamp(ov);
            }
        } else if (k <= 3) {                                       // a game file with no override yet
            wchar_t gp[MAX_PATH];
            bsh_game_path(stem, k, gp);
            DWORD n = 0;
            BYTE *b = pf_read(gp, &n);
            if (b && bsh_apply(b, n, mi->second) > 0 && sm_redirect(f, ov, MAX_PATH, true) && pf_write(ov, b, n)) {
                logf("body shape: %ls written as a shaped override", rel);
                g_bsh_seen[ov] = bsh_stamp(ov);
            }
            free(b);
        }
    }
    ReleaseSRWLockExclusive(&g_bsh_lock);
}

static int bsh_self_test() {
    BshSet s;
    bool p = bsh_parse_line("pc02a 0.80 0.070 0.034 0.026\n", s) && !strcmp(s.stem, "pc02a") && s.has_cx && s.amount == 0.034 &&
             bsh_parse_line("ENM05A 0.68 0.068 0.024", s) && !strcmp(s.stem, "enm05a") && !s.has_cx &&
             !bsh_parse_line("# pc02a 0.8 0.07 0.03", s) && !bsh_parse_line("pc02a 0.8 0.07 0.5", s) && !bsh_parse_line("npc02a 0.8 0.07 0.03", s);
    char st[8]; int k = 0;
    bool nm = bsh_parse_name(L"Assets/Data/Player\\pc02a\\pc02a7.alr", st, &k) && !strcmp(st, "pc02a") && k == 7 &&
              bsh_parse_name(L"x\\enm05a\\ENM05A.alr", st, &k) && !strcmp(st, "enm05a") && k == -1 &&
              bsh_parse_name(L"x\\pc02a\\pc02a0.alr", st, &k) && k == 0 &&
              !bsh_parse_name(L"x\\pc02a\\pc02a1 - Copy.alr", st, &k) && !bsh_parse_name(L"x\\pc02a\\pc02a0.alr.bak", st, &k) &&
              !bsh_parse_name(L"x\\npc02a\\npc02a0.alr", st, &k) && !bsh_parse_name(L"x\\pc02a\\pc02a15.alr", st, &k);
    // a map moves an exact point once: a second pass changes nothing, the inverse puts it back
    BshMap m, inv;
    BshKey a, b2;
    float fa[3] = {1.5f, 14.0f, -2.0f}, fb[3] = {1.6f, 13.9f, -2.4f};
    memcpy(a.v, fa, 12); memcpy(b2.v, fb, 12);
    m[a] = b2;
    bsh_inverse(m, inv);
    bool once = m.find(m[a]) == m.end() && inv.find(b2) != inv.end() && inv[b2] == a;
    logf("self-test: body shape lines %s, names %s, map %s", p ? "ok" : "FAILED", nm ? "ok" : "FAILED", once ? "ok" : "FAILED");
    return p && nm && once ? 1 : -64;
}

// offline test (tests/bsh_test.cpp): the map of a shape line, built from a folder of clean game files, put on one file
// (inverse = 1: the way back); returns the points moved
extern "C" __declspec(dllexport) int CwoBodyShapeTest(const wchar_t *install, const char *line, const wchar_t *in_path, const wchar_t *out_path, int inverse) {
    wcscpy_s(g_install, install);
    BshSet s;
    if (!bsh_parse_line(line, s)) return -10;
    BshMap f, inv;
    if (!bsh_build_map(s, f)) return -11;
    bsh_inverse(f, inv);
    DWORD n = 0;
    BYTE *b = pf_read(in_path, &n);
    if (!b) return -12;
    int r = bsh_apply(b, n, inverse ? inv : f);
    bool ok = r >= 0 && pf_write(out_path, b, n);
    free(b);
    return ok ? r : -13;
}
