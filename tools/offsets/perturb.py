"""Fakes an MCC update and checks that the offsets are still found where they moved - or reported missing, never
found somewhere wrong.

    python tools/offsets/perturb.py [--module NAME ...] [--test build/Release/offset_test.exe] [--seed N]

For each module it changes the installed game's file and runs offset_test.exe (the DLL's own lookup) over it:
  - the code grows: blocks of int3 go in between functions all through the code section, and every call, jump and
    RIP-relative operand across them is fixed up, so nearly every function moves, and the sections after the code
    move with it (every global moves too);
  - one of the two patterns of a fifth of the offsets that have two is broken (a byte only it pins down is
    changed), so those must be found through the other;
  - the only pattern of a tenth of the single-pattern offsets is broken, so those must be reported missing;
  - the first pattern of some offsets that have two gets a second match (a copy of its bytes in an inserted block),
    so it's ambiguous and they must be found through the other;
  - with --churn F, three bytes change in a fraction F of all functions besides (as a recompile would): offsets
    whose patterns that hits may go missing, but none may be found anywhere but where it moved.
Writes build/perturb/<module>.img (laid out as loaded) and .expect, the address each offset moved to.
"""
import argparse
import bisect
import os
import random
import re
import struct
import subprocess
import sys

import capstone
import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_patterns as gp  # noqa: E402
from common import DEFAULT_MCC, MODULES, ROOT, inc_dir  # noqa: E402

lite = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
detail = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
detail.detail = True


def read_table(module):
    """[(name, known, [pattern text])] from the checked-in patterns_<module>.inc."""
    path = os.path.join(inc_dir(), f"patterns_{module}.inc")
    out = []
    for block in re.finditer(r"\{&(OFFSET_\w+), (0x[0-9a-f]+), \w+, \{(.*?)\}\}", open(path).read(), re.S):
        pats = re.findall(r'(?:At|Rel)\("([^"]*)"', block.group(3))
        out.append((block.group(1), int(block.group(2), 16), pats))
    return out


def parse(text):
    tokens = text.split()
    return [0 if t == "??" else int(t, 16) for t in tokens], [0 if t == "??" else 1 for t in tokens]


def relative_fields(img, a, b):
    """(field, width, instruction end, target) of every call, jump and RIP-relative operand in the code section,
    decoded function by function (from each unwind entry, and each gap between them) so that data between
    functions can't throw the decoding off for long."""
    bounds = sorted({a, b} | {x for x in img.fbegin + img.fend if a < x < b})
    out = []
    for s, e in zip(bounds, bounds[1:]):
        p = s
        while p < e:
            last = p
            for address, size, mnemonic, op_str in lite.disasm_lite(img.image[p:e], p):
                last = end = address + size
                if op_str.startswith("0x") and (mnemonic in ("call", "jmp", "jrcxz", "jecxz") or
                                                mnemonic.startswith("j") or mnemonic.startswith("loop")):
                    width = 1 if size <= 3 else 4
                    out.append((end - width, width, end, int(op_str, 16)))
                elif "rip" in op_str:
                    insn = next(detail.disasm(img.image[address:end], address))
                    if insn.disp_size == 4:
                        disp = struct.unpack_from("<i", insn.bytes, insn.disp_offset)[0]
                        out.append((address + insn.disp_offset, 4, end, end + disp))
            p = last if last > p else p + 1  # a byte that doesn't decode
    return out


def perturb(module, mcc, outdir, seed, churn, log):
    rng = random.Random(seed)
    img = gp.Image(os.path.join(mcc, MODULES[module]))
    table = read_table(module)
    text_a, text_b = img.code[0]

    placed = {}  # name -> [(start, end, data, mask)] of each pattern's match
    for name, known, pats in table:
        for text in pats:
            data, mask = parse(text)
            m = img.matches(data, mask, 2)
            assert m and len(m) == 1, (name, text)
            placed.setdefault(name, []).append((m[0], m[0] + len(data), data, mask))

    fields = relative_fields(img, text_a, text_b)
    field_bytes = bytearray(img.size)  # 1 where a relative field is
    for field, width, end, target in fields:
        field_bytes[field:field + width] = b"\x01" * width

    def others(name, index):
        used = set()
        for other, entries in placed.items():
            for j, (s, e, d, m) in enumerate(entries):
                if (other, j) != (name, index):
                    used.update(range(s, e))
        return used

    # 1. break patterns: flip a byte only that pattern pins down
    image = bytearray(img.image)
    two = sorted(n for n, k, p in table if len(p) == 2)
    one = sorted(n for n, k, p in table if len(p) == 1)
    rng.shuffle(two)
    rng.shuffle(one)
    broken, missing = [], set()

    def break_one(name, index):
        s, e, data, mask = placed[name][index]
        used = others(name, index)
        for k in range(len(data) - 1, -1, -1):
            if mask[k] and s + k not in used and not field_bytes[s + k]:
                image[s + k] ^= 0xFF
                return True
        return False

    for name in two[:max(1, len(two) // 5)]:
        index = rng.randrange(2)
        if break_one(name, index):
            broken.append((name, index))
    for name in one[:max(1, len(one) // 10)]:
        if break_one(name, 0):
            missing.add(name)

    churned = 0
    for a, b in zip(img.fbegin, img.fend):
        if churn and text_a <= a < text_b and rng.random() < churn:
            for _ in range(3):
                image[rng.randrange(a, b)] ^= 0xFF
            churned += 1

    # 2. blocks inserted before functions; the middle one also holds copies of some first patterns
    copies = []
    for name in two[len(two) // 5:]:
        if len(copies) == max(1, len(two) // 10):
            break
        s, e, data, mask = placed[name][0]
        used = others(name, 0)
        if not any(x in used for x in range(s, e)):
            copies.append((name, bytes(image[s:e])))
    starts = sorted(x for x in img.fbegin if text_a < x < text_b)
    points = sorted(rng.sample(starts, min(60, len(starts))))
    sizes = {p: rng.randrange(1, 64) * 16 for p in points}
    host = points[len(points) // 2]
    sizes[host] = max(sizes[host], sum(len(c) + 32 for n, c in copies) + 32)
    cumulative, total = [], 0
    for p in points:
        cumulative.append(total)
        total += sizes[p]
    data_shift = (total + 0xFFF) & ~0xFFF

    def new(rva):
        if rva >= text_b:
            return rva + data_shift
        k = bisect.bisect_right(points, rva)
        return rva + (cumulative[k - 1] + sizes[points[k - 1]] if k else 0)

    # 3. every relative field for the new layout
    unfixable = 0
    for field, width, end, target in fields:
        if not 0 <= target < img.size:
            continue  # not an instruction: data between functions decoded as code
        value = new(target) - new(end)
        if width == 4:
            struct.pack_into("<i", image, field, value)
        elif -128 <= value < 128:
            struct.pack_into("<b", image, field, value)
        else:
            unfixable += 1

    # 4. the new layout
    out = bytearray(img.size + data_shift)
    out[:text_a] = image[:text_a]
    chunks, prev = [], text_a
    for p in points + [text_b]:
        chunks.append(bytes(image[prev:p]))
        if p in sizes:
            block = bytearray(b"\xCC" * sizes[p])
            if p == host:
                q = 32
                for name, c in copies:
                    block[q:q + len(c)] = c
                    q += len(c) + 32
            chunks.append(bytes(block))
        prev = p
    code = b"".join(chunks)
    out[text_a:text_a + len(code)] = code
    for a, b, x in img.sections:
        if a >= text_b:
            out[a + data_shift:b + data_shift] = image[a:b]
    headers = pefile.PE(data=bytes(out[:text_a]), fast_load=True)
    headers.FILE_HEADER.TimeDateStamp ^= 0x5A5A  # another build
    headers.OPTIONAL_HEADER.SizeOfImage += data_shift
    for section in headers.sections:
        if section.VirtualAddress == text_a:
            section.Misc_VirtualSize += total
        elif section.VirtualAddress >= text_b:
            section.VirtualAddress += data_shift
    out[:text_a] = headers.write()

    log(f"{module}: {len(points)} blocks ({total:#x} bytes) inserted between functions, data moved by "
        f"{data_shift:#x}; {len(broken)} of two patterns broken, {len(missing)} only patterns broken, "
        f"{len(copies)} first patterns duplicated" + (f", {churned} functions churned" if churn else "") +
        (f"; {unfixable} short jumps out of reach" if unfixable else ""))

    os.makedirs(outdir, exist_ok=True)
    image_path = os.path.join(outdir, f"{module}.img")
    expect_path = os.path.join(outdir, f"{module}.expect")
    with open(image_path, "wb") as f:
        f.write(out)
    with open(expect_path, "w") as f:
        for name, known, pats in table:
            if pats:
                f.write(f"{name} {'-' if name in missing else hex(new(known))}\n")
    return image_path, expect_path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mcc", default=DEFAULT_MCC)
    ap.add_argument("--module", action="append", choices=sorted(MODULES))
    ap.add_argument("--test", default=os.path.join(ROOT, "build", "Release", "offset_test.exe"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "perturb"))
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--churn", type=float, default=0.0, help="fraction of functions to change a few bytes in")
    args = ap.parse_args()
    failed = 0
    for module in args.module or list(MODULES):
        image, expect = perturb(module, args.mcc, args.out, args.seed, args.churn, print)
        run = subprocess.run([args.test, "--module", module, "--image", image, "--flat", "--expect", expect],
                             capture_output=True, text=True)
        lines = run.stdout.strip().splitlines()
        if args.churn:  # misses are expected there; show the summary and anything wrong
            lines = [line for line in lines if not line.lstrip().startswith("MISS")]
        print("  " + "\n  ".join(lines))
        failed += run.returncode != 0
    print("FAILED" if failed else "OK")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
