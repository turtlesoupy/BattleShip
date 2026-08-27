#!/usr/bin/env python3
"""Regenerate the PORT branch of every dual-branch FTMotionDesc table.

decomp/src/ft/ftdata.c and decomp/src/sc/scsubsys/scsubsysdata*.c carry each
FTMotionDesc array twice:

    #ifdef PORT
        llFooFileID, <offset>, <flags>,        <- derived, this script owns it
    #else
        { &llFooFileID, <offset>, <flags> },   <- upstream's text, verbatim
    #endif

The #else branch must stay byte-identical to upstream so upstream merges apply
cleanly. The PORT branch exists because upstream's `&llFooFileID` is the
address of a linker symbol the ROM link places at address == id; the port's
include/reloc_data.<v>.h emits ids as #define integer constants (they must
also serve as plain integers in the FTData initialisers), and `&` on an
rvalue does not compile.

The PORT branch carries no information of its own — it is upstream's branch
with `&` and the entry braces stripped. This script performs exactly that
transform, so after merging upstream you re-run it instead of hand-editing:

    python3 tools/derive_ftmotion_port_tables.py          # rewrite
    python3 tools/check_ftdata_tables.py --version us     # prove equivalence
    python3 tools/check_ftdata_tables.py --version jp

It is idempotent, preserves REGION_US/REGION_JP structure inside the tables,
and touches nothing outside the FTMotionDesc array bodies.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECOMP = ROOT / "decomp" if (ROOT / "decomp").is_dir() else ROOT

ARRAY_RE = re.compile(r"FTMotionDesc\s+(d\w+)\[\]\s*=\s*\{")
ENTRY_RE = re.compile(r"\{\s*&\s*(\w+)\s*,\s*([^,]+?)\s*,\s*([^}]+?)\s*\}")
ELIDED_AMP_RE = re.compile(r"&(?=ll\w*FileID\b)")
DATA_SYM_RE = re.compile(r"(,\s*)(&?D_\w+)(\s*,)")


def array_spans(source):
    """Yield (name, open_idx, close_idx) for each FTMotionDesc initialiser."""
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
        yield match.group(1), start, i


def split_port(body):
    """Split a table body on PORT conditionals only.

    Returns (port_text, vanilla_text). Non-PORT directives (REGION_* etc.) are
    routed to whichever half they textually live in: a REGION block at the
    shared level goes to both halves, but one nested inside a PORT-only (or
    vanilla-only) branch goes only to that half — otherwise each rewrite would
    leak empty REGION skeletons into the other half and the transform would
    not be idempotent.
    """
    port, vanilla, stack = [], [], []

    def dest_now():
        port_flags = [flag for kind, flag in stack if kind == "port"]
        if not port_flags:
            return "both"
        return "port" if all(port_flags) else "vanilla"

    def emit(line, dest):
        if dest in ("both", "port"):
            port.append(line)
        if dest in ("both", "vanilla"):
            vanilla.append(line)

    for line in body.splitlines(True):
        stripped = line.strip()
        if re.match(r"#ifdef\s+PORT\b", stripped):
            stack.append(["port", True])
            continue
        if re.match(r"#ifndef\s+PORT\b", stripped):
            stack.append(["port", False])
            continue
        if stripped.startswith("#if"):
            dest = dest_now()
            stack.append(["other", dest])
            emit(line, dest)
            continue
        if stripped.startswith("#else"):
            if stack and stack[-1][0] == "port":
                stack[-1][1] = not stack[-1][1]
                continue
            emit(line, stack[-1][1] if stack else "both")
            continue
        if stripped.startswith("#endif"):
            if stack and stack[-1][0] == "port":
                stack.pop()
                continue
            dest = stack.pop()[1] if stack else "both"
            emit(line, dest)
            continue
        emit(line, dest_now())
    return "".join(port), "".join(vanilla)


def derive(vanilla):
    """Upstream entry text -> PORT entry text.

    Drop `&` and the entry braces; additionally, an override entry whose
    second field is a RAM address rather than a file offset — a raw data
    symbol (`D_ovl1_...`) or the address of a local script array
    (`&D_ovl1_...`) — needs an `(intptr_t)` cast because the PORT struct
    stores that field as an integer.
    """
    derived = ENTRY_RE.sub(lambda m: f"{m.group(1)}, {m.group(2)}, {m.group(3)}",
                           vanilla)
    # Brace-elided entries keep their `&` through ENTRY_RE; strip it.
    derived = ELIDED_AMP_RE.sub("", derived)
    return DATA_SYM_RE.sub(r"\1(intptr_t)\2\3", derived)


def rewrite(path):
    source = path.read_text(encoding="utf-8")
    out, changed = source, 0
    for name, start, end in reversed(list(array_spans(source))):
        body = source[start + 1:end]
        _, vanilla = split_port(body)
        rebuilt = ("\n#ifdef PORT\n" + derive(vanilla).strip("\n")
                   + "\n#else\n" + vanilla.strip("\n") + "\n#endif\n")
        out = out[:start + 1] + rebuilt + out[end:]
        changed += 1
    if out != source:
        path.write_text(out, encoding="utf-8")
    return changed, out != source


def main():
    targets = [DECOMP / "src/ft/ftdata.c"]
    targets += sorted((DECOMP / "src/sc/scsubsys").glob("scsubsysdata*.c"))
    total, touched = 0, 0
    for path in targets:
        arrays, wrote = rewrite(path)
        total += arrays
        touched += wrote
        if wrote:
            print(f"  {path.relative_to(DECOMP)}: {arrays} array(s) rewritten")
    print(f"{total} arrays across {len(targets)} files; {touched} file(s) changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
