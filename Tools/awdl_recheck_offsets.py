#!/usr/bin/env python3
"""Re-check the version-sensitive instruction patterns of the AWDL stack against
a kernelcache (fileset Mach-O) or a single kext binary.

Companion of Tools/awdl_offsets_25G241.tsv and of the tables in
Docs/AWDL_VERSION_SENSITIVE_OFFSETS.md (sections 1-7): one row per access,
with a byte pattern that locates it inside the owning function. The pattern
carries '??' wildcards over the modrm/sib/register bytes (the compiler may
reassign registers between versions) and keeps the displacement literal: a
primary match proves the offset is unchanged. When the primary pattern does
not match, a second pass wildcards the displacement bytes and, on a hit,
reports the measured displacement (offset = measured + bias) so a new build's
table row can be regenerated.

Usage:
    python3 Tools/awdl_recheck_offsets.py <Mach-O file> [--tsv PATH]
                                         [--arch x86_64] [--va-hint]

    <Mach-O file>  the boot kernel collection
                   (/System/Library/KernelCollections/BootKernelExtensions.kc
                   or a copy) or any single-fileset/kext Mach-O. Fat binaries
                   are accepted; --arch selects the slice (only x86_64 today).
    --tsv          row file (default: Tools/awdl_offsets_25G241.tsv next to
                   this script). TAB-separated, '#' lines are comments:
                     group, structure, field, offset (hex), owner symbol
                     (mangled; sub_* = no symbol), hex pattern ('??' wildcards),
                     displacement length in bytes (0/1/4), bias, risk,
                     25G241 site VA (hint column for --va-hint).
    --va-hint      functions whose owner is unnamed (sub_FFFFFF...) carry no
                   symbol in the kernelcache symtab; without this flag those
                   rows are SKIPPED, with it the 25G241 site VA is used as the
                   search range seed (valid against the same build only).

Parsing: the container Mach-O supplies com.apple.kernel itself (its own
LC_SEGMENT_64 / LC_SYMTAB) and one inner Mach-O per LC_FILESET_ENTRY
(0x80000035); inside every module the segments and the symtab offsets are
container-absolute, so symbol VAs map to file bytes with
file = va - seg.vmaddr + seg.fileoff. A function range runs from the owner
symbol address to the next higher defined symbol address.

Verdicts: OK (primary pattern found), MOVED (only the second pass matched;
measured displacement reported), NOT-FOUND (neither pass matched inside the
owner range), SYMBOL-MISSING (owner not defined in any module of the file),
SKIPPED (unnamed owner without --va-hint, or empty owner). Exit status is
0 with no drift, 1 when any checked row is MOVED/NOT-FOUND/SYMBOL-MISSING,
2 on usage or input errors.
"""

import argparse
import re
import struct
import sys
from pathlib import Path

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_X86_64 = 0x01000007
FAT_MAGICS = (0xCAFEBABE, 0xCAFEBABF)  # big-endian on disk
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_FILESET_ENTRY = 0x80000035
N_TYPE_MASK = 0x0E
N_SECT = 0x0E

DEFAULT_TSV = Path(__file__).resolve().parent / "awdl_offsets_25G241.tsv"


def fail(msg):
    print(msg, file=sys.stderr)
    sys.exit(2)


class Module(object):
    """One Mach-O image inside the file: the kernel or a fileset entry."""

    def __init__(self, name, base):
        self.name = name
        self.base = base
        self.segments = []  # (vmaddr, fileoff, filesize)

    def va_to_file(self, va):
        for vmaddr, fileoff, filesize in self.segments:
            if vmaddr <= va < vmaddr + filesize:
                return fileoff + (va - vmaddr)
        return None

    def file_to_va(self, off):
        for vmaddr, fileoff, filesize in self.segments:
            if fileoff <= off < fileoff + filesize:
                return vmaddr + (off - fileoff)
        return None


def load_commands(data, off):
    """Yield (cmd, payload offset) for the 64-bit Mach-O header at off."""
    if len(data) < off + 32:
        fail("error: truncated Mach-O header at 0x%x" % off)
    magic, _cpu, _sub, _ft, ncmds, _sz, _fl, _res = struct.unpack_from("<IiiIIIII", data, off)
    if magic != MH_MAGIC_64:
        fail("error: not a 64-bit little-endian Mach-O at 0x%x" % off)
    p = off + 32
    for _ in range(ncmds):
        if p + 8 > len(data):
            break
        cmd, cmdsize = struct.unpack_from("<II", data, p)
        yield cmd, p
        p += cmdsize


def parse_module(data, base, name, modules, symtab, entry_vm=None):
    """Collect segments and defined symbols of one 64-bit Mach-O image.

    Inside a kernel collection the inner modules carry container-absolute
    symoff/stroff/fileoff (the convention Tools/re/kcsyms.py relies on); a
    standalone kext binary is file-relative. When the absolute reading falls
    outside the file and a base exists, the base-relative one is used. The
    inner segments of a fileset entry are module-relative (their vmaddr starts
    at 0) while the symtab carries absolute VAs: when every segment sits below
    the entry's own vmaddr, the entry vmaddr is added so symbol VAs map.
    """
    mod = Module(name, base)
    modules.append(mod)
    saw_seg = False
    for cmd, p in load_commands(data, base):
        if cmd == LC_SEGMENT_64:
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", data, p + 24)
            if fileoff + filesize > len(data) and base and fileoff >= base:
                fileoff -= base
            if filesize:
                mod.segments.append((vmaddr, fileoff, filesize))
                saw_seg = True
        elif cmd == LC_SYMTAB:
            symoff, nsyms, stroff, _strsize = struct.unpack_from("<IIII", data, p + 8)
            if symoff + 16 * nsyms > len(data) and base and symoff >= base:
                symoff -= base
                stroff -= base
            for i in range(nsyms):
                rec = symoff + 16 * i
                if rec + 16 > len(data):
                    break
                strx, n_type, n_sect, _desc, n_value = struct.unpack_from("<IBBHQ", data, rec)
                if (n_type & N_TYPE_MASK) != N_SECT or n_sect == 0 or n_value == 0:
                    continue
                end = data.find(b"\0", stroff + strx)
                if end < 0 or end == stroff + strx:
                    continue
                nm = data[stroff + strx:end].decode("latin-1")
                symtab.setdefault(nm, []).append(n_value)
    if entry_vm and saw_seg and max(vm for vm, _f, _s in mod.segments) < entry_vm:
        mod.segments = [(vm + entry_vm, f, s) for vm, f, s in mod.segments]


def load_image(path, arch):
    """Return (data, modules, symtab, sorted_addresses) for a fat or thin file."""
    data = path.read_bytes()
    magic = struct.unpack_from(">I", data, 0)[0]
    if magic in FAT_MAGICS:
        nfat = struct.unpack_from(">I", data, 4)[0]
        rec_off, rec_size = 8, (20 if magic == FAT_MAGICS[0] else 32)
        base = None
        for i in range(nfat):
            rec = rec_off + i * rec_size
            cputype = struct.unpack_from(">I", data, rec)[0]
            if cputype == arch:
                if magic == FAT_MAGICS[0]:
                    base = struct.unpack_from(">I", data, rec + 8)[0]
                else:
                    base = struct.unpack_from(">Q", data, rec + 8)[0]
                break
        if base is None:
            fail("error: no %s slice in the fat binary" %
                 ("x86_64" if arch == CPU_TYPE_X86_64 else hex(arch)))
    else:
        base = 0

    modules, symtab = [], {}
    parse_module(data, base, "com.apple.kernel", modules, symtab)
    for cmd, p in load_commands(data, base):
        if cmd != LC_FILESET_ENTRY:
            continue
        cmdsize = struct.unpack_from("<I", data, p + 4)[0]
        vmaddr, fileoff, idoff = struct.unpack_from("<QQI", data, p + 8)
        end = data.find(b"\0", p + idoff, p + cmdsize)
        name = data[p + idoff:end].decode("latin-1") if end > p + idoff else "entry@%x" % vmaddr
        try:
            parse_module(data, fileoff, name, modules, symtab, entry_vm=vmaddr)
        except SystemExit:
            fail("error: unparsable fileset entry %s at 0x%x" % (name, fileoff))
    addrs = sorted({a for vas in symtab.values() for a in vas})
    return data, modules, symtab, addrs


def function_range(va, addrs, modules):
    """(start, end) of the code around va: symbol address to the next higher one."""
    lo = va
    hi = None
    for a in addrs:
        if a > va:
            hi = a
            break
    end = hi
    for mod in modules:
        for vmaddr, _fileoff, filesize in mod.segments:
            limit = vmaddr + filesize
            if lo < limit and (end is None or limit < end) and limit > lo:
                end = min(end, limit) if end is not None else limit
    if end is None or end <= lo:
        return None
    return lo, end


def parse_pattern(text):
    """'0F B7 ?? 3C' -> (template bytes with None wildcards, regex, wildcard)."""
    parts = text.split()
    template, rx = [], []
    for tok in parts:
        if tok == "??":
            template.append(None)
            rx.append(b".")
        else:
            template.append(int(tok, 16))
            rx.append(re.escape(bytes([template[-1]])))
    if not template or template[0] is None or len(template) < 2:
        fail("error: pattern too short or leading wildcard: %s" % text)
    return template, re.compile(b"".join(rx), re.DOTALL), parts


def disp_position(template, offset, bias, disp_len):
    """Index of the displacement bytes inside the pattern, or None."""
    if disp_len == 0:
        return None
    disp = offset - bias
    if disp < 0:
        fail("error: offset 0x%x < bias 0x%x" % (offset, bias))
    needle = struct.pack("<Q", disp)[:disp_len]
    for i in range(len(template) - disp_len + 1):
        if all(template[i + j] == needle[j] for j in range(disp_len)):
            return i
    return None


def read_tsv(path):
    rows = []
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        entry = line if not line.startswith("#") else ""
        if not entry.strip():
            continue
        fields = entry.split("\t")
        if len(fields) != 10:
            fail("error: %s:%d: expected 10 TAB-separated columns, got %d"
                 % (path, lineno, len(fields)))
        group, structure, field, offset, owner, pattern, disp_len, bias, risk, hint = fields
        try:
            off = int(offset, 16)
            dlen = int(disp_len)
            bias_v = int(bias, 16)
            hint_va = int(hint, 16) if hint.strip() not in ("", "-") else None
        except ValueError as exc:
            fail("error: %s:%d: bad numeric column: %s" % (path, lineno, exc))
        rows.append({"group": group, "structure": structure, "field": field,
                     "offset": off, "owner": owner.strip(), "pattern": pattern.strip(),
                     "disp_len": dlen, "bias": bias_v, "risk": risk.strip(),
                     "hint": hint_va, "line": lineno})
    if not rows:
        fail("error: %s: no rows" % path)
    return rows


def check_row(row, data, modules, symtab, addrs, va_hint):
    """-> (status, detail, site_va)"""
    owner = row["owner"]
    unnamed = owner.startswith("sub_")
    if not owner or owner == "-":
        return "SKIPPED", "sin dueño", None
    if unnamed and not va_hint:
        return "SKIPPED", "funcion sin simbolo (use --va-hint)", None

    template, rx, _parts = parse_pattern(row["pattern"])
    if unnamed:
        if not row["hint"]:
            return "SKIPPED", "sin VA de pista en el TSV", None
        seeds = [row["hint"]]
    else:
        seeds = sorted(symtab.get(owner, []))
        if not seeds:
            return "SYMBOL-MISSING", "dueño no definido en el fichero", None

    disp_idx = disp_position(template, row["offset"], row["bias"], row["disp_len"])

    def scan(pattern_regex, lo, hi):
        m = pattern_regex.search(data, lo, hi)
        return m

    for seed in seeds:
        rng = function_range(seed, addrs, modules)
        if not rng:
            continue
        lo, hi = rng
        f_lo = None
        for mod in modules:
            f_lo = mod.va_to_file(lo)
            if f_lo is not None:
                break
        if f_lo is None:
            continue
        m = scan(rx, f_lo, min(f_lo + (hi - lo), len(data)))
        if m:
            site = None
            for mod in modules:
                site = mod.file_to_va(m.start())
                if site is not None:
                    break
            return "OK", "desplazamiento intacto", site

    # Second pass: wildcard the displacement bytes and measure.
    if disp_idx is not None:
        rx2 = []
        for j, b in enumerate(template):
            rx2.append(b"." if (disp_idx <= j < disp_idx + row["disp_len"] or b is None)
                       else re.escape(bytes([b])))
        rx2 = re.compile(b"".join(rx2), re.DOTALL)
        for seed in seeds:
            rng = function_range(seed, addrs, modules)
            if not rng:
                continue
            lo, hi = rng
            f_lo = None
            for mod in modules:
                f_lo = mod.va_to_file(lo)
                if f_lo is not None:
                    break
            if f_lo is None:
                continue
            m = scan(rx2, f_lo, min(f_lo + (hi - lo), len(data)))
            if m:
                raw = m.group(0)[disp_idx:disp_idx + row["disp_len"]]
                measured = int.from_bytes(raw, "little") + row["bias"]
                site = None
                for mod in modules:
                    site = mod.file_to_va(m.start())
                    if site is not None:
                        break
                return "MOVED", "medido offset=0x%x (esperado 0x%x)" % (measured, row["offset"]), site
    return "NOT-FOUND", "patron ausente en el rango de la funcion", None


def main():
    ap = argparse.ArgumentParser(
        description="Re-check AWDL version-sensitive instruction patterns against "
                    "a kernelcache or kext Mach-O.")
    ap.add_argument("image", metavar="MACH-O", type=Path,
                    help="boot kernel collection or single kext Mach-O file")
    ap.add_argument("--tsv", type=Path, default=DEFAULT_TSV,
                    help="row file (default: %(default)s)")
    ap.add_argument("--arch", choices=["x86_64"], default="x86_64",
                    help="architecture slice of a fat binary (default: %(default)s)")
    ap.add_argument("--va-hint", action="store_true",
                    help="check unnamed-owner rows using their 25G241 site VA "
                         "(valid against that build only)")
    args = ap.parse_args()

    if not args.image.is_file():
        fail("error: missing input file: %s" % args.image)
    if not args.tsv.is_file():
        fail("error: missing TSV file: %s" % args.tsv)
    arch = CPU_TYPE_X86_64 if args.arch == "x86_64" else None
    if arch is None:
        fail("error: unsupported arch: %s" % args.arch)

    data, modules, symtab, addrs = load_image(args.image, arch)
    rows = read_tsv(args.tsv)

    counts = {"OK": 0, "MOVED": 0, "NOT-FOUND": 0, "SYMBOL-MISSING": 0, "SKIPPED": 0}
    print("Image: %s (%d modulos, %d simbolos definidos)"
          % (args.image, len(modules), sum(len(v) for v in symtab.values())))
    print("Rows: %s (%d filas)" % (args.tsv, len(rows)))
    print()
    for row in rows:
        status, detail, site = check_row(row, data, modules, symtab, addrs, args.va_hint)
        counts[status] += 1
        ident = "%s/%s.%s" % (row["group"], row["structure"], row["field"])
        site_txt = "@ 0x%x" % site if site else ""
        line = "%-14s %-46s offset=0x%-6x %s %s" % (
            status, ident[:46], row["offset"], row["owner"][:80], site_txt)
        if status == "MOVED":
            line += "  [%s]" % detail
        elif status == "SKIPPED":
            line += "  [%s]" % detail
        print(line)
    print()
    print("checked: %d filas, %d OK, %d MOVED, %d NOT-FOUND, %d SYMBOL-MISSING, %d SKIPPED"
          % (len(rows), counts["OK"], counts["MOVED"], counts["NOT-FOUND"],
             counts["SYMBOL-MISSING"], counts["SKIPPED"]))
    drifted = counts["MOVED"] + counts["NOT-FOUND"] + counts["SYMBOL-MISSING"]
    sys.exit(1 if drifted else 0)


if __name__ == "__main__":
    main()
