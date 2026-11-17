"""SSB export-name codec (base 40, two 6-character halves).

Independently authored from Batch 004/005 evidence on PDUWP 9ba7411b:
function 0x1402847c0 maps each character through the 128-byte table at VA
0x1402f76a0 (file offset 0x2f60a0) and accumulates two words of six base-40
digits each; NUL or '.' ends the name and the remaining digits are 0. A
character whose table entry is 0 makes that word -1 (0xffffffff).

Table (read from the executable): '0'-'9' -> 1..10, 'A'-'Z'/'a'-'z' -> 11..36,
'_' and '=' -> 37. Codes 38 and 39 never occur. Decoding is lossy for case and
for '_' vs '=' (both 37); we decode 37 as '_'.

Usage:
  python -I -B ssb_names.py census --from-list LIST --out OUT.json
"""
import argparse
import collections
import json
import struct
import sys

DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz_"   # index + 1 == code
CODE = {c: i + 1 for i, c in enumerate(DIGITS)}
CODE.update({c.upper(): CODE[c] for c in "abcdefghijklmnopqrstuvwxyz"})
CODE["="] = 37
INVALID = 0xFFFFFFFF


def _half(chars):
    v = 0
    for i in range(6):
        if i < len(chars):
            c = CODE.get(chars[i])
            if c is None:
                return INVALID
        else:
            c = 0
        v = v * 40 + c
    return v


def encode(name):
    """Return (word0, word1) as the game's encoder would for `name`."""
    name = name.split(".", 1)[0]
    return _half(name[:6]), _half(name[6:12])


def decode_word(w):
    """Decode one word; returns None if it is not a valid base-40 name word."""
    if w == INVALID or w >= 40 ** 6:
        return None
    digits = []
    for _ in range(6):
        digits.append(w % 40)
        w //= 40
    digits.reverse()
    out, ended = [], False
    for d in digits:
        if d == 0:
            ended = True
            continue
        if ended or d > 37:
            return None  # non-zero after padding, or unused code 38/39
        out.append(DIGITS[d - 1])
    return "".join(out)


def decode(w0, w1):
    a, b = decode_word(w0), decode_word(w1)
    if a is None or b is None:
        return None
    if b and len(a) < 6:
        return None  # second half only exists when the first is full
    return a + b


def exports(data):
    table, text = struct.unpack_from("<II", data, 4)
    n = (text - table) // 12
    return [struct.unpack_from("<3I", data, table + 12 * k) for k in range(n)]


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("census")
    c.add_argument("--from-list", required=True)
    c.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    files = [l.strip() for l in open(a.from_list, encoding="utf-8") if l.strip()]
    total = ok = 0
    names = collections.Counter()
    bad = []
    per_file = {}
    for f in files:
        data = open(f, "rb").read()
        decoded = []
        for w0, w1, code in exports(data):
            total += 1
            n = decode(w0, w1)
            if n is None:
                bad.append({"file": f, "w0": w0, "w1": w1})
            else:
                ok += 1
                names[n] += 1
                if encode(n) != (w0, w1):
                    bad.append({"file": f, "w0": w0, "w1": w1, "reencode_mismatch": n})
            decoded.append(n)
        per_file[f] = decoded
    result = {"files": len(files), "export_records": total, "decoded": ok,
              "undecodable_or_mismatch": len(bad), "examples_bad": bad[:20],
              "distinct_names": len(names), "most_common": names.most_common(40),
              "per_file": per_file}
    with open(a.out, "x", encoding="utf-8") as fh:
        json.dump(result, fh, indent=1)
    print(json.dumps({k: result[k] for k in ("files", "export_records", "decoded",
                                               "undecodable_or_mismatch", "distinct_names")}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
