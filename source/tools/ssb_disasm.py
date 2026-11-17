"""Linear disassembler / validator for SSB bytecode (research tool).

Independently authored from the Batch 006 review of the native interpreter
step 0x1402aaa30 on PDUWP 9ba7411b. Instruction = little-endian u32:
  op  = word & 0xFF          (0x00..0x1F; 0x0B, 0x0C and >= 0x20 are invalid)
  sub = (word >> 8) & 0xFF   (sub-operation / flag)
  imm = signed (word >> 16)  (s16)
Opcode 0x0F carries one extra u32 operand. Opcode 0x1F uses a signed 24-bit
operand (word >> 8). Branch targets for 0x03 / 0x07 / 0x08 are
next_instruction + imm*4 bytes; 0x1F pushes next_instruction + imm24*4.
Mnemonics are research labels, not original names.

Usage:
  python -I -B ssb_disasm.py census --from-list LIST --out OUT.json
  python -I -B ssb_disasm.py dump FILE
"""
import argparse
import collections
import json
import struct
import sys

MNEMONIC = {
    0x00: "nop", 0x01: "callnative", 0x02: "ctl", 0x03: "call", 0x04: "callext",
    0x05: "ret", 0x06: "alloc", 0x07: "jmp", 0x08: "case", 0x09: "ldloc",
    0x0A: "stloc", 0x0D: "pop", 0x0E: "push16", 0x0F: "push32", 0x10: "pushret",
    0x11: "pushstr", 0x12: "add", 0x13: "sub", 0x14: "mul", 0x15: "div",
    0x16: "mod", 0x17: "cmp", 0x18: "and", 0x19: "or", 0x1A: "xor", 0x1B: "not",
    0x1C: "neg", 0x1D: "lnot", 0x1E: "shift", 0x1F: "pushaddr",
}
BRANCH = {0x03, 0x07, 0x08}


def decode(code, base):
    """Linear sweep over `code` (bytes of the code region starting at file
    offset `base`). Returns (instructions, invalid_list)."""
    out, bad = [], []
    pos = 0
    while pos + 4 <= len(code):
        w = struct.unpack_from("<I", code, pos)[0]
        op, sub = w & 0xFF, (w >> 8) & 0xFF
        imm = struct.unpack("<h", struct.pack("<H", w >> 16))[0]
        size = 8 if op == 0x0F else 4
        ins = {"off": base + pos, "op": op, "sub": sub, "imm": imm, "size": size}
        if op not in MNEMONIC:
            bad.append(ins)
        elif op == 0x0F:
            if pos + 8 > len(code):
                bad.append(dict(ins, error="truncated push32"))
            else:
                ins["imm32"] = struct.unpack_from("<i", code, pos + 4)[0]
        elif op in BRANCH:
            ins["target"] = base + pos + 4 + imm * 4
        elif op == 0x1F:
            imm24 = w >> 8
            if imm24 & 0x800000:
                imm24 -= 1 << 24
            ins["target"] = base + pos + 4 + imm24 * 4
        out.append(ins)
        pos += size
    return out, bad


def decode_one(data, off):
    w = struct.unpack_from("<I", data, off)[0]
    op, sub = w & 0xFF, (w >> 8) & 0xFF
    imm = struct.unpack("<h", struct.pack("<H", w >> 16))[0]
    ins = {"off": off, "op": op, "sub": sub, "imm": imm, "size": 8 if op == 0x0F else 4}
    if op in BRANCH:
        ins["target"] = off + 4 + imm * 4
    elif op == 0x1F:
        imm24 = w >> 8
        if imm24 & 0x800000:
            imm24 -= 1 << 24
        ins["target"] = off + 4 + imm24 * 4
    return ins


def traverse(data):
    """Recursive-descent from export entries. Follows fall-through, branch and
    local-call targets; stops after ret (0x05), unconditional jmp (0x07 sub 0),
    and ctl sub 1/2 (halt / end, per the native interpreter). pushaddr targets
    are recorded as data references, not followed. Returns a summary."""
    table, text = struct.unpack_from("<II", data, 4)
    lo, hi = 0x20, table
    entries = [struct.unpack_from("<I", data, table + 12 * k + 8)[0] * 4
               for k in range((text - table) // 12)]
    seen, invalid, out_of_range, data_refs = {}, [], 0, set()
    work = list(entries)
    while work:
        off = work.pop()
        while True:
            if off in seen:
                break
            if not (lo <= off and off + 4 <= hi):
                out_of_range += 1
                break
            ins = decode_one(data, off)
            seen[off] = ins
            op, sub = ins["op"], ins["sub"]
            if op not in MNEMONIC:
                invalid.append(ins)
                break
            if op == 0x1F:
                data_refs.add(ins["target"])
            elif op in BRANCH:
                work.append(ins["target"])
            if op == 0x05 or (op == 0x07 and sub == 0) or (op == 0x02 and sub in (1, 2)):
                break
            off += ins["size"]
    reached_words = sum(i["size"] // 4 for i in seen.values())
    overlap = sum(1 for o in seen if any(o < p < o + seen[o]["size"] for p in (o + 4,) if p in seen))
    return {"entries": len(entries), "reached_instructions": len(seen), "reached_words": reached_words,
            "code_words": (hi - lo) // 4, "invalid_reached": len(invalid),
            "invalid_examples": invalid[:3], "targets_out_of_range": out_of_range,
            "pushaddr_targets": len(data_refs),
            "pushaddr_targets_in_reached_code": sum(1 for t in data_refs if t in seen),
            "overlapping_decodes": overlap,
            "reached_ops": collections.Counter(i["op"] for i in seen.values()),
            "reached_callext_imms": sorted({i["imm"] for i in seen.values() if i["op"] == 0x04})}


def analyze(data):
    table, text = struct.unpack_from("<II", data, 4)
    code_start, code_end = 0x20, table
    ins, bad = decode(data[code_start:code_end], code_start)
    starts = {i["off"] for i in ins}
    branch_ok = branch_bad = 0
    for i in ins:
        if "target" in i and i["op"] in BRANCH | {0x1F}:
            if code_start <= i["target"] < code_end and i["target"] in starts:
                branch_ok += 1
            else:
                branch_bad += 1
    exp_ok = exp_bad = 0
    for k in range((text - table) // 12):
        c = struct.unpack_from("<I", data, table + 12 * k + 8)[0] * 4
        if c in starts:
            exp_ok += 1
        else:
            exp_bad += 1
    return {"instructions": len(ins), "invalid": len(bad), "invalid_examples": bad[:3],
            "branch_targets_ok": branch_ok, "branch_targets_bad": branch_bad,
            "exports_on_boundary": exp_ok, "exports_off_boundary": exp_bad,
            "opcodes": collections.Counter(i["op"] for i in ins)}


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("census")
    c.add_argument("--from-list", required=True)
    c.add_argument("--out", required=True)
    d = sub.add_parser("dump")
    d.add_argument("file")
    a = ap.parse_args(argv)
    if a.cmd == "dump":
        data = open(a.file, "rb").read()
        table = struct.unpack_from("<I", data, 4)[0]
        ins, _ = decode(data[0x20:table], 0x20)
        for i in ins:
            m = MNEMONIC.get(i["op"], "INVALID")
            extra = f" -> 0x{i['target']:x}" if "target" in i else (f" {i['imm32']}" if "imm32" in i else "")
            print(f"{i['off']:06x}  {m:10s} sub={i['sub']:<3d} imm={i['imm']:<6d}{extra}")
        return 0
    files = [l.strip() for l in open(a.from_list, encoding="utf-8") if l.strip()]
    tot = collections.Counter()
    ops = collections.Counter()
    per = {}
    trav = collections.Counter()
    trav_problems = {}
    for f in files:
        data = open(f, "rb").read()
        r = analyze(data)
        ops.update(r.pop("opcodes"))
        for k in ("instructions", "invalid", "branch_targets_ok", "branch_targets_bad",
                  "exports_on_boundary", "exports_off_boundary"):
            tot[k] += r[k]
        if r["invalid"] or r["branch_targets_bad"] or r["exports_off_boundary"]:
            per[f] = r
        t = traverse(data)
        for k in ("entries", "reached_instructions", "reached_words", "code_words", "invalid_reached",
                  "targets_out_of_range", "pushaddr_targets", "pushaddr_targets_in_reached_code",
                  "overlapping_decodes"):
            trav[k] += t[k]
        if t["invalid_reached"] or t["targets_out_of_range"] or t["overlapping_decodes"]:
            trav_problems[f] = t
    result = {"files": len(files), "totals": dict(tot),
              "opcode_frequency": {MNEMONIC.get(k, hex(k)): v for k, v in sorted(ops.items())},
              "files_with_problems": len(per), "problem_examples": dict(list(per.items())[:10]),
              "traversal_totals": dict(trav), "traversal_files_with_problems": len(trav_problems),
              "traversal_problem_examples": dict(list(trav_problems.items())[:10])}
    with open(a.out, "x", encoding="utf-8") as fh:
        json.dump(result, fh, indent=1)
    print(json.dumps({"files": result["files"], "linear_totals": result["totals"],
                      "traversal_totals": result["traversal_totals"],
                      "traversal_files_with_problems": result["traversal_files_with_problems"]}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
