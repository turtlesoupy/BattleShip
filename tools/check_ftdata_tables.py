#!/usr/bin/env python3
"""Check that the PORT motion tables still agree with upstream's.

Covers decomp/src/ft/ftdata.c and decomp/src/sc/scsubsys/scsubsysdata*.c.
The PORT branches are machine-derived by tools/derive_ftmotion_port_tables.py;
run that after an upstream merge, then this to prove the regeneration.

decomp/src/ft/ftdata.c carries every FTMotionDesc array twice:

    #ifdef PORT
        <file-id symbol>, <numeric offset>, <numeric flags>,
    #else
        { &<file-id symbol>, <offset symbol>, FTANIM_FLAG_* },
    #endif

The duplication is structural, not laziness. Upstream writes the file id as
`&llFooFileID`, the *address* of a linker symbol that the ROM link places at
address == id. The port has no linker script to reproduce that, so
include/reloc_data.<v>.h emits each id as a `#define` integer constant, and
`&` on an rvalue does not compile. The PORT branch is the port's re-encoding
of the same data.

The hazard: upstream edits land in the #else branch (that text is byte-identical
to upstream, which is what keeps merges clean), and nothing forces the PORT
branch to follow. This script resolves both branches to numbers and fails if
they ever disagree.

Usage:
    python3 tools/check_ftdata_tables.py [--version {us,jp}]
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECOMP = ROOT / "decomp" if (ROOT / "decomp").is_dir() else ROOT

ENTRY_FIELDS = 3
ARRAY_RE = re.compile(r"FTMotionDesc\s+(d\w+)\[\]\s*=\s*\{")
DEFINE_RE = re.compile(r"#define\s+(\w+)\s+\(\(intptr_t\)(0x[0-9A-Fa-f]+|\d+)\)")
OFFSET_RE = re.compile(r"#define\s+(d\w+)\s+(0x[0-9A-Fa-f]+)\s*$")
FLAG_RE = re.compile(r"#define\s+(FTANIM_FLAG_\w+)\s+(0x[0-9A-Fa-f]+)")
HEX_RE = re.compile(r"0x[0-9A-Fa-f]+")


def preprocess(text, port, region_us):
    """Resolve PORT / REGION_* conditionals, keeping the selected branch."""
    kept, stack = [], []
    for line in text.splitlines(True):
        stripped = line.strip()
        if stripped.startswith("#if"):
            if re.match(r"#ifdef\s+PORT\b", stripped):
                cond = port
            elif re.match(r"#ifndef\s+PORT\b", stripped):
                cond = not port
            elif "REGION_US" in stripped:
                cond = region_us
            elif "REGION_JP" in stripped:
                cond = not region_us
            else:
                cond = True
            stack.append(cond)
        elif stripped.startswith("#else"):
            if stack:
                stack[-1] = not stack[-1]
        elif stripped.startswith("#endif"):
            if stack:
                stack.pop()
        elif all(stack):
            kept.append(line)
    return "".join(kept)


def read_defines(path, pattern, base=0):
    """Collect `name -> int` from a header, honouring REGION conditionals."""
    values = {}
    if not path.is_file():
        return values
    text = path.read_text(encoding="utf-8", errors="ignore")
    for line in preprocess(text, port=True, region_us=base).splitlines():
        match = pattern.match(line.strip())
        if match:
            values[match.group(1)] = int(match.group(2), 0)
    return values


def split_entries(body):
    """Flatten an initialiser body into 3-tuples; braces are optional."""
    text = re.sub(r"//.*", "", body)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = text.replace("{", " ").replace("}", " ")
    tokens = [t.strip() for t in text.split(",") if t.strip()]
    if len(tokens) % ENTRY_FIELDS:
        return None
    return [tuple(tokens[i:i + ENTRY_FIELDS])
            for i in range(0, len(tokens), ENTRY_FIELDS)]


def array_bodies(source):
    """Yield (name, body) for every FTMotionDesc array, brace-matched."""
    for match in ARRAY_RE.finditer(source):
        start = source.index("{", match.start())
        depth, i = 0, start
        while i < len(source):
            if source[i] == "{":
                depth += 1
            elif source[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        yield match.group(1), source[start + 1:i]


def resolve(expr, table, operators="[|+]"):
    """Evaluate a symbol/number expression against a lookup table."""
    total = 0
    for part in re.split(operators, expr):
        part = part.strip().lstrip("&")
        if not part:
            continue
        if part in table:
            total += table[part]
        elif HEX_RE.fullmatch(part):
            total += int(part, 16)
        elif part.isdigit():
            total += int(part)
        else:
            return None
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", choices=("us", "jp"), default="us")
    args = parser.parse_args()
    region_us = args.version == "us"

    ids = read_defines(ROOT / "include" / f"reloc_data.{args.version}.h",
                       DEFINE_RE, base=region_us)
    offsets = read_defines(DECOMP / "include/ft/motiondesc_offsets.h",
                           OFFSET_RE, base=region_us)
    flags = read_defines(DECOMP / "src/ft/ftdef.h", FLAG_RE, base=region_us)
    if not ids or not offsets or not flags:
        print("error: could not load reloc ids, motion offsets, or FTANIM flags",
              file=sys.stderr)
        return 2

    targets = [DECOMP / "src/ft/ftdata.c"]
    targets += sorted((DECOMP / "src/sc/scsubsys").glob("scsubsysdata*.c"))

    def norm(expr):
        """Textual fallback for fields with no numeric value (RAM symbols like
        D_ovl1_*): the PORT side differs from upstream only by `&` removal and
        an (intptr_t) cast, so compare with both stripped."""
        return re.sub(r"\s+|\(intptr_t\)|&", "", expr)

    checked = failures = arrays = 0
    for path in targets:
        source = path.read_text(encoding="utf-8")
        for name, body in array_bodies(source):
            arrays += 1
            port = split_entries(preprocess(body, True, region_us))
            vanilla = split_entries(preprocess(body, False, region_us))
            if port is None or vanilla is None:
                print(f"{name}: could not parse one of the branches")
                failures += 1
                continue
            if len(port) != len(vanilla):
                print(f"{name}: entry count differs — "
                      f"PORT={len(port)} upstream={len(vanilla)}")
                failures += 1
                continue
            for index, (p, v) in enumerate(zip(port, vanilla)):
                checked += 1
                fields = (
                    ("file id", p[0], v[0], ids),
                    ("offset", p[1], v[1], offsets),
                    ("flags", p[2], v[2], flags),
                )
                for label, ptext, vtext, table in fields:
                    got, want = resolve(ptext, table), resolve(vtext, table)
                    if got is None and want is None:
                        if norm(ptext) == norm(vtext):
                            continue
                    elif got == want and got is not None:
                        continue
                    print(f"{name}[{index}] {label}: PORT={ptext!r} -> {got} "
                          f"upstream={vtext!r} -> {want}")
                    failures += 1

    print(f"{args.version}: checked {checked} motion-desc entries "
          f"across {arrays} arrays in {len(targets)} files; "
          f"{failures} disagreement(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
