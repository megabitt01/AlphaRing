"""Checks that code reaching into a game through an offset stays off when the offset isn't in the MCC build.

    python tools/offsets/check_gates.py

On a build the offsets weren't written for, an offset that isn't found reads as 0 (src/offsets/Offsets.h), so what
uses it has to be gated on it. Every OFFSET_ a source file uses must be gated there in one of these ways:
  - a hook's own target (Halo1Entry(name, OFFSET_X, ...), PreservedEntry, ::Entry name(Set(), OFFSET_X, ...));
  - listed in an EntryFeature(...) or a Feature name(...) / {...};
  - checked with Found({...}) or OFFSET_X.found();
  - a {Steam, Windows Store} pair handed to Hook::Detour / Hook::Offset, which skip a missing one;
  - an embed patch's offset (CPatch captures, and so writes, only a found one);
  - INVOKE / DefPtr / DefPPtr's (lib/game/src/ICNative.h: nothing, or null, for a missing one);
or be in REVIEWED under its file, for gates this can't read. The check is per file, not per function: a use can
lean on a gate elsewhere in the file. It also fails when a hook comes before its file's EntryFeature, which it then
isn't part of.
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT, inc_dir, read_offsets  # noqa: E402

# file -> (how its gate works, the offsets it was reviewed for); a use of any other fails
REVIEWED = {
    "src/hook/Hook.cpp": ("named as optional (compared, never used)", {
        "OFFSET_MCC_PF_GET_INDEX_BY_XUID", "OFFSET_MCC_PF_GET_PROFILE", "OFFSET_MCC_PV_WINDOWFOCUSED"}),
}

HOOK_TARGETS = [
    r"\w+Entry\(\s*\w+\s*,\s*(OFFSET_\w+)",                                  # Halo1Entry(name, OFFSET_X
    r"PreservedEntry\(\s*\w+\s*,\s*[\w:]+\(\)\s*,\s*(OFFSET_\w+)",           # PreservedEntry(name, Set(), OFFSET_X
    r"::Entry\s+\w+\(\s*\w+\(\)\s*,\s*(OFFSET_\w+)",                         # ::Entry name(Set(), OFFSET_X
]
GATED_LISTS = [
    r"EntryFeature\((.*?)\);",
    r"Feature\s+\w+\s*[({](.*?)[)}];",
    r"Found\(\{(.*?)\}\)",
]
GATED_SINGLE = [
    r"(OFFSET_\w+)\.found\(\)",
    r"\{\s*(OFFSET_MCC_\w+)\s*,\s*OFFSET_MCC_WS_\w+\s*,",                   # {steam, ws, ...}: Hook::Detour / Offset
    r"\{\s*OFFSET_MCC_\w+\s*,\s*(OFFSET_MCC_WS_\w+)\s*,",
    r"\{\s*\"[^\"]*\"\s*,\s*(?:\"[^\"]*\"\s*)+,\s*(OFFSET_\w+)",            # {"name", "desc", OFFSET_X: an embed patch
    r"INVOKE<[^>]*>\(\s*(OFFSET_\w+)",
    r"Def\w*Ptr\(\s*\w+\s*,\s*(OFFSET_\w+)",
]


def strip_comments(text):
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)


def names(text):
    return set(re.findall(r"\bOFFSET_[A-Z0-9_]+", text))


def main():
    defined = {name for header in glob.glob(os.path.join(inc_dir(), "offset_*.h")) for name, value in read_offsets(header)}
    problems = []
    files = glob.glob(os.path.join(ROOT, "src", "**", "*.*"), recursive=True) + \
        glob.glob(os.path.join(ROOT, "lib", "game", "src", "**", "*.*"), recursive=True)
    for path in sorted(files):
        rel = os.path.relpath(path, ROOT).replace("\\", "/")
        if rel.startswith("src/offsets/") or not rel.endswith((".cpp", ".h")):
            continue
        text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
        used = names(text) & defined
        if not used:
            continue
        gated = set(REVIEWED.get(rel, ("", set()))[1])
        for pattern in HOOK_TARGETS + GATED_SINGLE:
            gated |= set(re.findall(pattern, text))
        for pattern in GATED_LISTS:
            for block in re.findall(pattern, text, re.S):
                gated |= names(block)
        for name in sorted(used - gated):
            problems.append(f"{rel}: {name} isn't gated")

        feature = text.find("EntryFeature(")
        if feature >= 0:
            for pattern in HOOK_TARGETS:
                for m in re.finditer(pattern, text):
                    if m.start() < feature:
                        problems.append(f"{rel}: the hook on {m.group(1)} comes before the EntryFeature")

    for problem in problems:
        print(problem)
    print(f"{len(problems)} problems" if problems else "every offset use is gated")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
