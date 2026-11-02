#!/usr/bin/env python3
"""Disassemble an ELF (or a raw image) without the ARM toolchain installed.

`arm-none-eabi-objdump -d` is the real answer and this is not a replacement for
it. But the toolchain is not vendored yet (see the README), which leaves anyone
with a fresh clone unable to look at the very listings the Part 0 slides quote.
This reads the ELF directly and disassembles it with capstone, which is a 3 MB
package in the course's uv environment rather than a 335 MB download:

    uv sync                      (once, at the repository root)
    python _build/disasm.py _spike/c031c6/Main.elf
    python _build/disasm.py _spike/c031c6/Main.elf -f _write
    python _build/disasm.py _spike/c031c6/Main.hex --base 0x08000000

Two details make the difference between output you can trust and output that
quietly lies:

* **Mapping symbols.** ARM ELFs carry `$t` and `$d` symbols marking where code
  stops and a literal pool begins. A disassembler that ignores them walks
  straight into the pool and renders `0x40021000` as instructions - plausible
  looking nonsense. Those regions are printed as `.word` here, as objdump does.
* **The Thumb bit.** Function symbols are odd (lesson 01, slide 19), so every
  address has to be masked before it is compared with anything.

A `.hex` or `.bin` has no symbols and no mapping symbols, so it is disassembled
as one run of Thumb from `--base`. Expect literal pools to come out as garbage
instructions; that is the cost of throwing away the ELF.
"""
import argparse
import re
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_LITTLE_ENDIAN
except ImportError:
    sys.exit("capstone is not installed: run  uv sync  at the repository root")

STT_FUNC = 2
SHF_EXECINSTR = 0x4


def read_elf(data):
    """Return (sections, symbols) from a little-endian 32-bit ELF."""
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        sys.exit("not a little-endian 32-bit ELF")
    e_shoff, = struct.unpack_from("<I", data, 32)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 46)

    raw = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, typ, flags, addr, offset, size, link, info, align, entsize = \
            struct.unpack_from("<10I", data, off)
        raw.append(dict(name=name, type=typ, flags=flags, addr=addr,
                        offset=offset, size=size, link=link, entsize=entsize))

    def strtab(idx, off):
        base = raw[idx]["offset"] + off
        end = data.index(b"\0", base)
        return data[base:end].decode("utf-8", "replace")

    for s in raw:
        s["sname"] = strtab(e_shstrndx, s["name"])

    syms = []
    for s in raw:
        if s["type"] != 2:            # SHT_SYMTAB
            continue
        count = s["size"] // (s["entsize"] or 16)
        for i in range(count):
            off = s["offset"] + i * 16
            st_name, st_value, st_size, st_info, _, st_shndx = \
                struct.unpack_from("<IIIBBH", data, off)
            if not st_name:
                continue
            syms.append(dict(name=strtab(s["link"], st_name),
                             value=st_value, size=st_size,
                             type=st_info & 0xF, shndx=st_shndx))
    return raw, syms


def regions(section, syms, sections):
    """Split a section into ('t'|'d', start, end) runs using mapping symbols.

    Mapping symbols are `$t` (Thumb code), `$d` (data), `$a` (ARM code), and
    may carry a suffix such as `$d.1`.  Without them every literal pool is
    disassembled as if it were code.
    """
    idx = sections.index(section)
    marks = sorted((s["value"] & ~1, s["name"][:2])
                   for s in syms
                   if s["shndx"] == idx and s["name"][:1] == "$")
    start, end = section["addr"], section["addr"] + section["size"]
    if not marks or marks[0][0] > start:
        marks.insert(0, (start, "$t"))

    out = []
    for i, (addr, kind) in enumerate(marks):
        stop = marks[i + 1][0] if i + 1 < len(marks) else end
        if addr < stop:
            out.append((kind[1], addr, stop))
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("image", help="an .elf, or a .hex / .bin with --base")
    ap.add_argument("-f", "--function", help="disassemble only this symbol")
    ap.add_argument("-s", "--section", help="limit to one section, e.g. .text")
    ap.add_argument("--base", type=lambda v: int(v, 0),
                    help="load address for a raw .bin (.hex carries its own)")
    args = ap.parse_args(argv)

    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    blob = open(args.image, "rb").read()

    funcs, runs, image, syms = {}, [], {}, []
    if blob[:4] == b"\x7fELF":
        sections, syms = read_elf(blob)
        for s in syms:
            if s["type"] == STT_FUNC and s["name"]:
                funcs[s["value"] & ~1] = s["name"]
        for sec in sections:
            if not (sec["flags"] & SHF_EXECINSTR) or sec["type"] == 8:
                continue                      # skip .bss and non-code
            if args.section and sec["sname"] != args.section:
                continue
            image[sec["addr"]] = blob[sec["offset"]:sec["offset"] + sec["size"]]
            runs += [(k, a, b, sec["addr"]) for k, a, b in
                     regions(sec, syms, sections)]
    else:
        if args.image.lower().endswith(".hex"):
            base, buf, hi = None, bytearray(), 0
            for line in open(args.image):
                line = line.strip()
                if not line.startswith(":"):
                    continue
                n = int(line[1:3], 16)
                addr = int(line[3:7], 16)
                typ = int(line[7:9], 16)
                body = bytes.fromhex(line[9:9 + n * 2])
                if typ == 4:
                    hi = int.from_bytes(body, "big") << 16
                elif typ == 0:
                    a = hi + addr
                    if base is None:
                        base = a
                    buf.extend(b"\0" * (a - base - len(buf)))
                    buf.extend(body)
            data, base = bytes(buf), (args.base if args.base else base)
        else:
            if args.base is None:
                sys.exit("a raw .bin needs --base, e.g. --base 0x08000000")
            data, base = blob, args.base
        image[base] = data
        runs = [("t", base, base + len(data), base)]

    if args.function:
        if not syms:
            sys.exit("--function needs an .elf; a .hex or .bin has no symbols")
        hit = [s for s in syms
               if s["type"] == STT_FUNC and s["name"] == args.function]
        if not hit:
            sys.exit("no function symbol %r in %s" % (args.function, args.image))
        lo = hit[0]["value"] & ~1
        hi = lo + (hit[0]["size"] or 0x40)   # a sizeless symbol gets 64 bytes
        runs = [(k, max(a, lo), min(b, hi), base) for k, a, b, base in runs
                if a < hi and b > lo]

    for kind, start, stop, secbase in runs:
        if start >= stop:
            continue
        data = image[secbase][start - secbase:stop - secbase]
        if kind == "d":
            for off in range(0, len(data) - 3, 4):
                word, = struct.unpack_from("<I", data, off)
                print(" %7x:\t%08x \t.word\t0x%08x"
                      % (start + off, word, word))
            continue
        pos = 0
        while pos < len(data):
            used = emit(md, data[pos:], start + pos, funcs)
            pos += used
            if pos < len(data):
                word, = struct.unpack_from("<H", data, pos)
                print(" %7x:	%04x      	.short	0x%04x"
                      % (start + pos, word, word))
                pos += 2


def emit(md, data, addr, funcs):
    """Print one Thumb run; return how many bytes capstone managed to decode.

    capstone stops dead at the first halfword it cannot decode, and a raw .hex
    is full of those - its vector table is addresses, not code - so the caller
    steps over the offender and calls again rather than truncating the dump.
    """
    used = 0
    for ins in md.disasm(data, addr):
        if ins.address in funcs:
            print("\n%08x <%s>:" % (ins.address, funcs[ins.address]))
        # objdump prints each halfword as a little-endian 16-bit value, so
        # `b5f0` - not the bytes in the order they sit in the file.
        raw = " ".join("%04x" % struct.unpack_from("<H", ins.bytes, i)[0]
                       for i in range(0, len(ins.bytes), 2))
        text = "%-7s %s" % (ins.mnemonic, ins.op_str)
        for tok in re.findall(r"#0x[0-9a-f]+\b", ins.op_str):
            target = int(tok[1:], 16) & ~1
            if target in funcs:
                text += "  <%s>" % funcs[target]
        print(" %7x:\t%-9s \t%s" % (ins.address, raw, text.rstrip()))
        used += ins.size
    return used


if __name__ == "__main__":
    main(sys.argv[1:])
