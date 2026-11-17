"""Read-only inspector for Phantom Dust stage DAT files (ST00 family).

Independently authored from neutral facts recorded in the Rebuild
STAGE_FORMAT_SPEC.md (header 0x13c, placement stride 0x24, count words at
+0x30/+0x38/+0x3c, PS01/PS02 pointers at +0x0c/+0x10, name pool +0x5c/+0x60).
No third-party parser was consulted for this implementation.

What it reports (nothing is interpreted beyond the evidence):
  * all 79 header u32 words, with a flag for each word that is an in-file
    offset and the 4-byte tag found just before / at that offset
  * a census of 4-aligned ASCII section tags of the form AA00..ZZ99
  * PS00/PS01/PS02 placement rows when their bounds validate
  * a byte-level diff summary between two files (for owner-made variants)

Usage:
  python -I -B stage_dat_inspect.py inspect FILE [FILE...] --out OUT.json
  python -I -B stage_dat_inspect.py diff BASE VARIANT --out OUT.json
"""
import argparse
import hashlib
import json
import re
import struct
import sys

HEADER_SIZE = 0x13C
PLACEMENT_STRIDE = 0x24
TAG_RE = re.compile(rb"[A-Z]{2}[0-9]{2}")


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def i32(b, o):
    return struct.unpack_from("<i", b, o)[0]


def tag_at(b, o):
    if 0 <= o and o + 4 <= len(b):
        t = b[o:o + 4]
        if TAG_RE.fullmatch(t):
            return t.decode("ascii")
    return None


def tag_census(b):
    out = []
    for o in range(0, len(b) - 3, 4):
        t = tag_at(b, o)
        if t:
            out.append({"offset": o, "tag": t})
    return out


def read_rows(b, start, count):
    """Return placement rows or an error string if bounds fail."""
    if count in (0, -1):
        return {"status": "absent", "count_word": count}
    if count < 0:
        return {"status": "invalid_negative_count", "count_word": count}
    end = start + count * PLACEMENT_STRIDE
    if start < 0 or end > len(b):
        return {"status": "out_of_bounds", "start": start, "count": count, "file_size": len(b)}
    rows = []
    for k in range(count):
        o = start + k * PLACEMENT_STRIDE
        oid = i32(b, o)
        opaque = b[o + 4:o + 12].hex()
        pos = struct.unpack_from("<3f", b, o + 12)
        rot = struct.unpack_from("<3f", b, o + 24)
        rows.append({"row": k, "offset": o, "object_id": oid, "opaque_04_0b": opaque,
                     "pos": [round(v, 6) for v in pos], "rot": [round(v, 6) for v in rot]})
    return {"status": "ok", "start": start, "count": count, "rows": rows}


def section_map(b):
    """Order header words 0x5c..0x138 that are in-file offsets; span = distance
    to the next such offset (or EOF). Pure arithmetic; no section semantics."""
    ptrs = []
    for o in range(0x5C, HEADER_SIZE, 4):
        if o == 0x60:  # name count word, not an offset
            continue
        v = u32(b, o)
        if 0x140 <= v < len(b):
            ptrs.append((v, o))
    ptrs.sort()
    out = []
    for k, (v, o) in enumerate(ptrs):
        end = ptrs[k + 1][0] if k + 1 < len(ptrs) else len(b)
        sec = {"field": f"0x{o:02x}", "offset": v, "span": end - v,
               "tag_before": tag_at(b, v - 4), "tag_at": tag_at(b, v),
               "first16": b[v:v + 16].hex()}
        if sec["tag_at"] == "CP00" and v + 8 <= len(b):
            sec["cp_u16_a"], sec["cp_u16_b"] = struct.unpack_from("<HH", b, v + 4)
        if v + 16 <= len(b) and u32(b, v) == 0x20:
            w = struct.unpack_from("<4I", b, v)
            sec["ssb_like_header"] = list(w)
            sec["ssb_like_plausible"] = w[1] <= w[2] == w[3] <= end - v
        out.append(sec)
    return out


CP_ENTRY = 80


def parse_cp00(b, off):
    """Decode one CP00 block at `off` (tag position). Layout measured in
    Batch 003: u16 count, u16 payload bytes (= count*80), then 80-byte
    entries. Field names are positional only; semantics are hypotheses."""
    if b[off:off + 4] != b"CP00" or off + 8 > len(b):
        return None
    count, nbytes = struct.unpack_from("<HH", b, off + 4)
    blk = {"offset": off, "count": count, "payload_bytes": nbytes,
           "size_rule_count_x80": nbytes == count * CP_ENTRY, "entries": []}
    if not blk["size_rule_count_x80"] or off + 8 + nbytes > len(b):
        return blk
    for k in range(count):
        o = off + 8 + k * CP_ENTRY
        f = struct.unpack_from("<20f", b, o)
        w = struct.unpack_from("<20I", b, o)
        blk["entries"].append({
            "p0": [round(v, 4) for v in f[0:3]], "f0c": round(f[3], 6), "u10": f"0x{w[4]:08x}",
            "p1": [round(v, 4) for v in f[5:8]], "p2": [round(v, 4) for v in f[8:11]],
            "p3": [round(v, 4) for v in f[11:14]],
            "u38_u4c": [f"0x{x:08x}" for x in w[14:20]]})
    return blk


def inspect_bytes(b, name="<bytes>"):
    rep = {"name": name, "size": len(b), "sha256": hashlib.sha256(b).hexdigest(),
           "signature": b[:4].decode("latin-1")}
    if len(b) < HEADER_SIZE + 4:
        rep["status"] = "too_small"
        return rep
    rep["status"] = "ST00" if b[:4] == b"ST00" else "not_ST00"
    words = []
    for o in range(0, HEADER_SIZE, 4):
        v = u32(b, o)
        w = {"off": o, "u32": v, "i32": i32(b, o)}
        if 0 < v < len(b):
            w["in_file"] = True
            w["tag_at"] = tag_at(b, v)
            w["tag_before"] = tag_at(b, v - 4)
        words.append(w)
    rep["header_words"] = words
    rep["marker_at_0x13c"] = tag_at(b, 0x13C)
    rep["tags"] = tag_census(b)
    rep["sections"] = section_map(b)
    rep["CP00_blocks"] = [dict(parse_cp00(b, s["offset"]), field=s["field"])
                          for s in rep["sections"] if s["tag_at"] == "CP00"]
    ps00 = read_rows(b, 0x140, i32(b, 0x30))
    ps01 = read_rows(b, i32(b, 0x0C), i32(b, 0x38))
    ps02 = read_rows(b, i32(b, 0x10), i32(b, 0x3C))
    rep["PS00"], rep["PS01"], rep["PS02"] = ps00, ps01, ps02
    rep["name_pool"] = {"pointer": i32(b, 0x5C), "count_word": i32(b, 0x60)}
    ids = [r["object_id"] for t in (ps00, ps01, ps02) if t.get("status") == "ok" for r in t["rows"]]
    if ids:
        rep["object_id_range"] = [min(ids), max(ids)]
        cnt = rep["name_pool"]["count_word"]
        rep["ids_within_name_count"] = cnt > 0 and all(0 <= i < cnt for i in ids)
    return rep


def diff_bytes(a, b):
    n = min(len(a), len(b))
    runs = []
    k = 0
    while k < n:
        if a[k] != b[k]:
            s = k
            while k < n and a[k] != b[k]:
                k += 1
            runs.append([s, k])
        else:
            k += 1
    return {"size_a": len(a), "size_b": len(b), "common": n,
            "diff_runs": len(runs), "diff_bytes": sum(e - s for s, e in runs),
            "runs": runs[:2000], "runs_truncated": len(runs) > 2000}


def locate(rep, off):
    """Name the region of an inspected file that contains offset `off`."""
    if off < HEADER_SIZE:
        return f"header+0x{off:x}"
    for key in ("PS00", "PS01", "PS02"):
        t = rep.get(key, {})
        if t.get("status") == "ok":
            s = t["start"]
            if s <= off < s + t["count"] * PLACEMENT_STRIDE:
                r, f = divmod(off - s, PLACEMENT_STRIDE)
                return f"{key}[{r}]+0x{f:02x}"
    for sec in rep.get("sections", []):
        if sec["offset"] <= off < sec["offset"] + sec["span"]:
            label = sec["tag_at"] or sec["tag_before"] or "untagged"
            return f"section {sec['field']} ({label})+0x{off - sec['offset']:x}"
    prev = None
    for tg in rep.get("tags", []):
        if tg["offset"] <= off:
            prev = tg
        else:
            break
    return f"after_tag {prev['tag']}@0x{prev['offset']:x}" if prev else "unknown"


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p1 = sub.add_parser("inspect")
    p1.add_argument("files", nargs="+")
    p1.add_argument("--out", required=True)
    p1.add_argument("--no-rows", action="store_true")
    p2 = sub.add_parser("diff")
    p2.add_argument("base")
    p2.add_argument("variant")
    p2.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "inspect":
        reports = []
        for f in a.files:
            with open(f, "rb") as fh:
                r = inspect_bytes(fh.read(), f)
            if a.no_rows:
                for k in ("PS00", "PS01", "PS02"):
                    r.get(k, {}).pop("rows", None)
            reports.append(r)
        result = reports
    else:
        with open(a.base, "rb") as fh:
            ba = fh.read()
        with open(a.variant, "rb") as fh:
            bb = fh.read()
        ra = inspect_bytes(ba, a.base)
        d = diff_bytes(ba, bb)
        d["base"], d["variant"] = a.base, a.variant
        d["base_sha256"], d["variant_sha256"] = ra["sha256"], hashlib.sha256(bb).hexdigest()
        regions = {}
        for s, e in d["runs"]:
            regions.setdefault(locate(ra, s), []).append([s, e])
        d["regions"] = regions
        result = d
    with open(a.out, "x", encoding="utf-8") as fh:
        json.dump(result, fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
