"""Byte patterns for AlphaRing's offsets, so the DLL can find them again after an MCC update.

    python tools/offsets/gen_patterns.py [--mcc DIR] [--module NAME ...] [--check]

Reads every DefOffset in lib/game/inc/<VERSION>/offset_<module>.h (VERSION from CMakeLists.txt), finds
it in that module's binary of the installed MCC - which must be exactly that version - and writes
lib/game/inc/<VERSION>/patterns_<module>.inc: for each offset up to two byte patterns that each match
once in the module's code, and how to get from the match to the offset. src/offsets/Offsets.cpp scans
for them when a module isn't the build the offsets were written for. --check only compares the tables
it would write with the checked-in ones (exit 1 if they differ).

A pattern is a run of whole instructions with the bytes that move between builds left out (??):
RIP-relative displacements, call/jump targets, image-relative addresses and relocated words. A
function is found by its own first instructions, or through a call to it; a global through an
instruction that addresses it. Patterns stay inside one function and are the shortest unique ones
found. Run it after changing an offset header, then rebuild and run build/Release/offset_test.exe
(every pattern must find its offset exactly) and tools/offsets/check_gates.py.
"""
import argparse
import bisect
import os
import re
import struct
import sys

import capstone
import numpy as np
import pefile
from capstone import x86

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import DEFAULT_MCC, MODULES, inc_dir, read_offsets, version  # noqa: E402

MAX_PATTERNS = 2    # per offset (src/offsets/Pattern.h kMaxPatterns)
MIN_LITERAL = 16    # bytes a pattern must pin down besides its wildcards, so that another build is unlikely to
                    # have it once somewhere else
MAX_LENGTH = 96     # bytes
MAX_BACK = 6        # instructions a pattern may start before the one it's about
MAX_REFS = 24       # referencing instructions tried per offset

# Sites whose patch assumes the code after it: the pattern pins down this many bytes from the offset, short jump
# distances included (CModule.cpp's embed patches). 4PLAYERS' "EB 18" jumps over the three stores after it.
COVER = {
    "OFFSET_HALO1_PF_4PLAYERS": 0x1A,
    "OFFSET_HALO1_PF_PAUSE": 2,
    "OFFSET_HALO1_PF_IDK": 6,
    "OFFSET_HALO1_FIRST_PERSON_SYNC_SLOT_LIMIT": 6,
    "OFFSET_HALO2_PF_PLAYER_COUNT1": 1,
    "OFFSET_HALO2_PF_PLAYER_COUNT2": 1,
    "OFFSET_HALO2_TWO_PLAYERS_TEST_HAND_OVER": 5,
    "OFFSET_HALO2_PF_QUIT_TO_LOBBY_TEST": 9,
    "OFFSET_HALO2_PF_HUD_ASPECT_LOCK": 2,
    "OFFSET_HALO4_PF_COOP_PLAYER_LIMIT": 6,
    "OFFSET_HALO4_PF_COOP_REJOIN": 2,
    "OFFSET_HALO4_VIEWMODEL_ASPECT_DIVIDE": 4,
    "OFFSET_GROUNDHOG_PF_REJOIN": 2,
    "OFFSET_HALOREACH_PF_COOP_REJOIN": 2,
    "OFFSET_HALOREACH_PF_RENDER_THROTTLE_COUNT_CALL": 5,
}

# Mid-function hooks whose detours rewrite what the code before them set up: the pattern starts at least this many
# bytes earlier (halo1/splitscreen.cpp ScopeGrid: rsp+0x40..0x48 and 0x64/xmm15; halo1/anniversary.cpp
# OverlayRect: the rect in r11d, r9d, r10d, eax). An offset right after a call - a return address - always takes
# the call as well.
COVER_BEFORE = {
    "OFFSET_HALO1_SCOPE_GRID_SET": 0x45,
    "OFFSET_HALO1_SABER_VIEW_OVERLAY_RECT": 0x32,
}

# Offsets with no binary here to pattern from.
SKIP = {
    r"OFFSET_MCC_WS_\w+": "Windows Store build",
}

IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_WRITE = 0x80000000

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


class Image:
    """A binary as the loader maps it, and what the generator needs to know about its code."""

    def __init__(self, path):
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXCEPTION"],
                                               pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"]])
        self.path = path
        self.timestamp = pe.FILE_HEADER.TimeDateStamp
        self.size = pe.OPTIONAL_HEADER.SizeOfImage
        image = bytearray(self.size)
        headers = pe.OPTIONAL_HEADER.SizeOfHeaders
        image[:headers] = pe.__data__[:headers]
        self.sections = []
        self.writable = []
        for s in pe.sections:
            raw = pe.__data__[s.PointerToRawData:s.PointerToRawData + min(s.SizeOfRawData, s.Misc_VirtualSize)]
            image[s.VirtualAddress:s.VirtualAddress + len(raw)] = raw
            self.sections.append((s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize,
                                  bool(s.Characteristics & IMAGE_SCN_MEM_EXECUTE)))
            if s.Characteristics & IMAGE_SCN_MEM_WRITE:
                self.writable.append((s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize))
        self.image = bytes(image)
        self.np = np.frombuffer(self.image, dtype=np.uint8)
        self.code = [(a, b) for a, b, x in self.sections if x]
        # u32 at every byte of the code, and its positions sorted by value (for anchor searches)
        self.u32, self.by_value = {}, {}
        for a, b in self.code:
            c = self.np[a:b].astype(np.uint32)
            u = c[:-3] | (c[1:-2] << 8) | (c[2:-1] << 16) | (c[3:] << 24)
            order = np.argsort(u, kind="stable").astype(np.uint32)
            self.u32[a] = u
            self.by_value[a] = (u[order], order)
        # functions with unwind data; an entry chained to the one before it (UNW_FLAG_CHAININFO, the rest of a
        # function whose prologue is shrink-wrapped) is part of that function
        fn = sorted((e.struct.BeginAddress, e.struct.EndAddress, e.struct.UnwindData)
                    for e in getattr(pe, "DIRECTORY_ENTRY_EXCEPTION", []))
        self.fbegin, self.fend = [], []
        for a, b, unwind in fn:
            chained = unwind < self.size and (self.image[unwind] >> 3) & 4
            if chained and self.fend and self.fend[-1] == a:
                self.fend[-1] = b
            else:
                self.fbegin.append(a)
                self.fend.append(b)
        # relocated qwords (IMAGE_REL_BASED_DIR64)
        self.relocated = np.zeros(self.size + 8, dtype=bool)
        for block in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []):
            for e in block.entries:
                if e.type == 10:
                    self.relocated[e.rva:e.rva + 8] = True
        self.decoded = {}  # function start -> (instructions, their addresses)

    def is_code(self, rva):
        return any(a <= rva < b for a, b in self.code)

    def code_range(self, rva):
        return next((a, b) for a, b in self.code if a <= rva < b)

    # ---- instructions

    def function(self, rva):
        """(start, end, leaf) of the function around rva: its unwind entry, or for a leaf without one (leaves sit
        between the entries, several in a row) from the last ret, jmp or int3 before rva."""
        i = bisect.bisect_right(self.fbegin, rva) - 1
        if i >= 0 and rva < self.fend[i]:
            return self.fbegin[i], self.fend[i], False
        lo = self.fend[i] if i >= 0 else self.code_range(rva)[0]
        hi = self.fbegin[i + 1] if i + 1 < len(self.fbegin) else self.code_range(rva)[1]
        start = lo
        for address, size, mnemonic, _ in md.disasm_lite(self.image[lo:hi], lo):
            if address + size > rva:
                break
            if mnemonic in ("ret", "jmp", "int3"):
                start = address + size
        while start < rva and self.image[start] == 0xCC:
            start += 1
        return start, hi, True

    def instructions(self, rva):
        """The function around rva, decoded: (start, end, [instructions]), cut at int3 (and a leaf at its ret)."""
        start, end, leaf = self.function(rva)
        key = start
        if key not in self.decoded:
            out = []
            for insn in md.disasm(self.image[start:end], start):
                if insn.mnemonic == "int3" and insn.address > rva:
                    break
                out.append(insn)
                if leaf and insn.address >= rva and insn.mnemonic in ("ret", "jmp"):
                    break
            self.decoded[key] = (out, [i.address for i in out])
        insns = self.decoded[key][0]
        return start, (insns[-1].address + insns[-1].size if insns else end), insns

    def containing(self, insns, rva):
        """Index of the instruction (of those instructions() returned) that holds byte rva, or None."""
        addrs = self.decoded[insns[0].address][1] if insns else []
        k = bisect.bisect_right(addrs, rva) - 1
        if k >= 0 and rva < insns[k].address + insns[k].size:
            return k
        return None

    def mask(self, insn, strict_from=None, strict_to=None):
        """Which bytes of the instruction a pattern pins down (1) or leaves out (0)."""
        m = [1] * insn.size
        rip = any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP for op in insn.operands)
        if insn.disp_size and rip:
            m[insn.disp_offset:insn.disp_offset + insn.disp_size] = [0] * insn.disp_size
        elif insn.disp_size == 4:
            disp = struct.unpack_from("<I", insn.bytes, insn.disp_offset)[0]
            if 0x10000 <= disp < self.size:  # an image-relative address (switch tables, arrays off __ImageBase)
                m[insn.disp_offset:insn.disp_offset + 4] = [0] * 4
        if insn.group(capstone.CS_GRP_BRANCH_RELATIVE) and insn.imm_size:
            keep = insn.imm_size == 1 and strict_from is not None and strict_from <= insn.address < strict_to
            if not keep:
                m[insn.imm_offset:insn.imm_offset + insn.imm_size] = [0] * insn.imm_size
        for j in np.flatnonzero(self.relocated[insn.address:insn.address + insn.size]):
            m[j] = 0
        return m

    # ---- matching

    def anchor_positions(self, value):
        """[(section start, sorted positions in it where the u32 `value` is)]"""
        out = []
        for a, (values, order) in self.by_value.items():
            lo, hi = np.searchsorted(values, value, "left"), np.searchsorted(values, value, "right")
            out.append((a, order[lo:hi].astype(np.int64)))  # stable sort: already in address order
        return out

    def matches(self, pat, mask, limit=2):
        """RVAs where the pattern matches wholly inside one code section (at most `limit`: enough to tell unique)."""
        n = len(pat)
        j = next((j for j in range(n - 3) if all(mask[j:j + 4])), None)
        if j is None:
            return None  # not searchable
        value = pat[j] | pat[j + 1] << 8 | pat[j + 2] << 16 | pat[j + 3] << 24
        out = []
        literal = [k for k in range(n) if mask[k] and not j <= k < j + 4]
        for a, pos in self.anchor_positions(value):
            b = next(e for s, e in self.code if s == a)
            cand = pos - j
            cand = cand[(cand >= 0) & (cand + n <= b - a)] + a
            for k in literal:
                if len(cand) == 0:
                    break
                cand = cand[self.np[cand + k] == pat[k]]
            out.extend(int(c) for c in cand[:limit])
        return out[:limit]

    # ---- references

    def rip_refs(self, targets):
        """{target: [instruction address]} of instructions whose RIP-relative operand addresses a target."""
        found = {t: [] for t in targets}
        wanted = np.array(sorted(targets), dtype=np.int64)
        for a, u in self.u32.items():
            disp = u.view(np.int32).astype(np.int64)
            field = np.arange(len(u), dtype=np.int64) + a
            for imm in (0, 1, 2, 4):
                ea = field + 4 + imm + disp
                hit = np.flatnonzero(np.isin(ea, wanted))
                for h in hit:
                    self._confirm_rip(int(field[h]), int(ea[h]), found)
        for t in found:
            found[t] = sorted(set(found[t]))
        return found

    def _confirm_rip(self, field, ea, found):
        start, end, insns = self.instructions(field)
        k = self.containing(insns, field)
        if k is None:
            return
        insn = insns[k]
        if insn.address + insn.disp_offset != field or insn.disp_size != 4:
            return
        if not any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP for op in insn.operands):
            return
        if insn.address + insn.size + struct.unpack_from("<i", insn.bytes, insn.disp_offset)[0] == ea:
            found[ea].append(insn.address)

    def call_refs(self, targets):
        """{target: [call/jmp rel32 address]} of direct calls and jumps to a target."""
        found = {t: [] for t in targets}
        wanted = np.array(sorted(targets), dtype=np.int64)
        for a, u in self.u32.items():
            rel = u.view(np.int32).astype(np.int64)
            pos = np.arange(len(u), dtype=np.int64) + a  # the rel32 field
            dest = pos + 4 + rel
            hit = np.flatnonzero(np.isin(dest, wanted))
            for h in hit:
                field = int(pos[h])
                op = self.image[field - 1]
                if op not in (0xE8, 0xE9):
                    continue
                start, end, insns = self.instructions(field)
                k = self.containing(insns, field - 1)
                if k is None or insns[k].address != field - 1:
                    continue
                found[int(dest[h])].append(field - 1)
        return found


class Pattern:
    def __init__(self, start, data, mask, kind, a=0, b=0, addend=0):
        self.start, self.data, self.mask = start, data, mask
        self.kind, self.a, self.b, self.addend = kind, a, b, addend

    def text(self):
        return " ".join(f"{x:02X}" if m else "??" for x, m in zip(self.data, self.mask))

    def resolve(self, img, at):
        if self.kind == "At":
            return at + self.a
        rel = struct.unpack_from("<i", img.image, at + self.a)[0]
        return at + self.b + rel + self.addend

    def cpp(self):
        if self.kind == "At":
            return f'At("{self.text()}", {self.a:#x})'
        return f'Rel("{self.text()}", {self.a:#x}, {self.b:#x}, {self.addend:#x})'


def grow(img, insns, must_from, must_to, make, strict=None):
    """The shortest unique pattern of whole instructions covering [must_from, must_to), or None. It runs on past an
    unconditional jmp or ret (into code another build may lay out elsewhere) only when nothing shorter is unique.
    `make(start, data, mask)` turns the bytes into a Pattern."""
    k = img.containing(insns, must_from)
    if k is None:
        return None
    for cross in (False, True):
        best = None
        for back in range(0, MAX_BACK + 1):
            s = k - back
            if s < 0:
                break
            start = insns[s].address
            data, mask = [], []
            for e in range(s, len(insns)):
                insn = insns[e]
                if not cross and e > s and start + len(data) >= must_to and insns[e - 1].mnemonic in ("jmp", "ret"):
                    break
                data += list(insn.bytes)
                mask += img.mask(insn, *(strict or (None, None)))
                end = start + len(data)
                if len(data) > MAX_LENGTH:
                    break
                if end < must_to or sum(mask) < MIN_LITERAL:
                    continue
                m = img.matches(data, mask, 2)
                if m is None:
                    continue
                if len(m) == 1 and m[0] == start:
                    keep = len(data)  # trailing wildcards say nothing (past what it must cover)
                    while keep > must_to - start and not mask[keep - 1]:
                        keep -= 1
                    p = make(start, bytes(data[:keep]), mask[:keep])
                    if best is None or len(p.data) < len(best.data):
                        best = p
                    break
            if best is not None and len(best.data) <= 32:
                break
        if best is not None:
            return best
    return None


def code_patterns(img, name, target, callers, rip_refs):
    """Patterns for a place in the code: its own instructions, then calls to it / references to its address."""
    out = []
    start, end, insns = img.instructions(target)
    k = img.containing(insns, target)
    if k is not None:
        cover = COVER.get(name, 8 if insns[k].address == target else 1)
        strict = (target, target + cover)
        must_from = max(start, target - COVER_BEFORE.get(name, 0))
        if k > 0 and insns[k].address == target and insns[k - 1].mnemonic == "call":
            must_from = min(must_from, insns[k - 1].address)  # a return address: its call too
        p = grow(img, insns, must_from, min(target + cover, end),
                 lambda s, d, m: Pattern(s, d, m, "At", target - s), strict=strict)
        if p:
            out.append(p)
    # a second way in, through a caller or an instruction taking its address (functions only)
    if k is not None and insns[k].address == target and target == start:
        alt = []
        for site in (callers + rip_refs)[:MAX_REFS]:
            p = ref_pattern(img, site, target)
            if p and (not out or img.function(site)[0] != img.function(out[0].start)[0]):
                alt.append(p)
        alt.sort(key=lambda p: len(p.data))
        out += alt[:MAX_PATTERNS - len(out)]
    return out[:MAX_PATTERNS]


def ref_pattern(img, site, target):
    """A pattern around the instruction at `site` that addresses `target` (a call, jump or RIP-relative operand)."""
    start, end, insns = img.instructions(site)
    k = img.containing(insns, site)
    if k is None or insns[k].address != site:
        return None
    insn = insns[k]
    if insn.mnemonic in ("call", "jmp") and insn.size == 5 and insn.bytes[0] in (0xE8, 0xE9):
        field = site + 1
    else:
        field = site + insn.disp_offset
    nxt = site + insn.size
    ea = nxt + struct.unpack_from("<i", img.image, field)[0]
    addend = target - ea

    def make(s, d, m):
        return Pattern(s, d, m, "Rel", field - s, nxt - s, addend)

    return grow(img, insns, site, nxt, make)


def data_patterns(img, target, exact, near):
    """Patterns for a global: instructions addressing it in different functions, or else a field of it close by."""
    out, funcs = [], set()
    for refs in (exact, near):
        if len(out) == MAX_PATTERNS:
            break
        cands = [p for p in (ref_pattern(img, site, target) for site in refs[:MAX_REFS]) if p]
        cands.sort(key=lambda p: (abs(p.addend), len(p.data)))
        for p in cands:
            f = img.function(p.start)[0]
            if f in funcs or len(out) == MAX_PATTERNS:
                continue
            out.append(p)
            funcs.add(f)
    return out


def generate(module, ver, mcc_dir, log):
    offsets = read_offsets(os.path.join(inc_dir(ver), f"offset_{module}.h"))
    img = Image(os.path.join(mcc_dir, MODULES[module]))
    log(f"{module}: {len(offsets)} offsets, {os.path.basename(img.path)} timestamp {img.timestamp:#x} size {img.size:#x}")

    # why an offset gets no patterns before any are looked for, or None
    skip = {name: next((why for p, why in SKIP.items() if re.fullmatch(p, name)), None) or
                  (None if 0 < value < img.size else "outside the image") for name, value in offsets}
    wanted = {value for name, value in offsets if skip[name] is None}
    # globals may be reached through a field of them as well
    data_targets = {t for v in wanted if not img.is_code(v) for t in range(v, v + 0x41)}
    code_targets = {v for v in wanted if img.is_code(v)}
    rip = img.rip_refs(data_targets | code_targets)
    calls = img.call_refs(code_targets)

    results = []
    for name, value in offsets:
        reason = skip[name]
        pats = []
        if reason is None:
            if img.is_code(value):
                pats = code_patterns(img, name, value, calls.get(value, []), rip.get(value, []))
            else:
                exact = rip.get(value, [])
                # a field of it: an address past its start (one before it would be another object), in writable
                # data (the linker pools read-only constants, so a neighbour there is anyone's)
                writable = any(a <= value < b for a, b in img.writable)
                near = [site for d in range(1, 0x41) for site in rip.get(value + d, [])] if writable else []
                pats = data_patterns(img, value, exact, near)
            for p in pats:  # belt and braces: each resolves back to the offset, once
                m = img.matches(p.data, p.mask, 2)
                assert m == [p.start] and p.resolve(img, p.start) == value, (name, p.text())
            if not pats:
                reason = "no unique pattern found"
        results.append((name, value, img.is_code(value) if 0 < value < img.size else True, pats, reason))
        log(f"  {name} {value:#x}: " + (", ".join(f"{p.kind}/{len(p.data)}" for p in pats) or reason))
    return img, results


def emit(module, ver, img, results):
    title = module[0].upper() + module[1:]
    lines = [
        f"// Generated by tools/offsets/gen_patterns.py from {os.path.basename(img.path)} {ver} - rerun it, don't edit.",
        "// At(bytes, delta): the match plus delta. Rel(bytes, field, next, addend): the rel32 at match + field,",
        "// counted from match + next (the end of its instruction), plus addend. ?? is any byte.",
        f"constexpr ModuleBuild k{title}Build{{{img.timestamp:#010x}, {img.size:#x}}};",
        f"const OffsetPatterns k{title}Patterns[] = {{",
    ]
    for name, value, code, pats, reason in results:
        kind = "Code" if code else "Data"
        if not pats:
            lines.append(f"    {{&{name}, {value:#x}, {kind}, {{}}}}, // {reason}: known build only")
            continue
        lines.append(f"    {{&{name}, {value:#x}, {kind}, {{")
        for i, p in enumerate(pats):
            lines.append(f"        {p.cpp()}" + ("}}," if i == len(pats) - 1 else ","))
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mcc", default=DEFAULT_MCC, help="MCC install folder")
    ap.add_argument("--module", action="append", choices=sorted(MODULES), help="only these modules")
    ap.add_argument("--check", action="store_true", help="compare with the checked-in tables, write nothing")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args()
    log = (lambda s: None) if args.quiet else print
    ver = version()
    defined = {name for module in MODULES for name, value in read_offsets(os.path.join(inc_dir(ver), f"offset_{module}.h"))}
    unknown = (set(COVER) | set(COVER_BEFORE)) - defined
    assert not unknown, f"COVER / COVER_BEFORE name offsets the headers don't define: {sorted(unknown)}"
    stale = []
    total = covered = 0
    for module in args.module or MODULES:
        img, results = generate(module, ver, args.mcc, log)
        text = emit(module, ver, img, results)
        del img  # before the next module's is built
        total += len(results)
        covered += sum(1 for r in results if r[3])
        path = os.path.join(inc_dir(ver), f"patterns_{module}.inc")
        old = open(path).read() if os.path.exists(path) else None
        if args.check:
            if old != text:
                stale.append(path)
        elif old != text:
            with open(path, "w", newline="\n") as f:
                f.write(text)
    print(f"{covered}/{total} offsets have patterns")
    if stale:
        print("out of date: " + ", ".join(stale))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
