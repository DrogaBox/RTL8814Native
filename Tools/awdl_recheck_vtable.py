#!/usr/bin/env python3
"""Check the vtable offsets assumed by Source/AirLayer/AWDLInterface.h against
a vtable dump of one macOS version.

Usage:
    python3 Tools/awdl_recheck_vtable.py <vtable-dump.txt>
    python3 Tools/awdl_recheck_vtable.py <vtable-dump.txt> --class IO80211AWDLProtocol
    python3 Tools/awdl_recheck_vtable.py <vtable-dump.txt> --header <header.h>

The dump must use the format of Docs/re/io80211_25.6.0/vt_*.txt: one line per
slot, "<index> <hex offset> <demangled method>", where a bare "0" marks an
empty cell and ___cxa_pure_virtual a pure slot. Every virtual declaration of
the header carrying a /* 0x... */ offset comment produces one row; the method
name, stripped of return type, parameters and class scope, is compared with
the demangled dump entry at the same offset, so inherited implementations
match under any defining class. OK marks equal names, MISMATCH a different
name in an occupied cell, MISSING an offset the dump does not reach or an
empty cell (a coverage limit, not a layout verdict). Each mismatch carries a
note suggesting a slot shift when the header name appears at another offset
of the dump, or a naming difference when it appears nowhere. The exit status
is 1 when at least one MISMATCH was printed, 0 otherwise, 2 on usage errors.
--class limits the check to the declarations of one class section of the
header (IO80211VirtualInterface, IO80211AWDLProtocol).
"""

import argparse
import re
import sys
from pathlib import Path

DEFAULT_HEADER = Path(__file__).resolve().parent.parent / "Source" / "AirLayer" / "AWDLInterface.h"

CLASS_LINE_RE = re.compile(r"^\s*(?:class|struct)\s+([A-Za-z_]\w*)")
VIRTUAL_LINE_RE = re.compile(r"^\s*virtual\b")
OFFSET_COMMENT_RE = re.compile(r"/\*\s*(0x[0-9A-Fa-f]+)\s*(pure)?\s*\*/")
DUMP_LINE_RE = re.compile(r"^\s*(\d+)\s+(0x[0-9A-Fa-f]+)\s+(\S.*?)\s*$")

EMPTY_CELL = "0"
PURE_VIRTUAL = "___cxa_pure_virtual"
STATUS_OK = "OK"
STATUS_MISMATCH = "MISMATCH"
STATUS_MISSING = "MISSING"


def method_name(declaration):
    """Method name of a virtual declaration, without return type, parameters
    or class scope; None when the shape is unexpected."""
    text = re.sub(r"^virtual\s+", "", declaration.strip())
    paren = text.find("(")
    if paren < 0:
        return None
    match = re.search(r"([~A-Za-z_]\w*)\s*$", text[:paren])
    return match.group(1) if match else None


def parse_header(path, only_class):
    """Collect (offset, offset text, method name, pure flag) from the offset
    comments of the virtual declarations, restricted to the only_class section
    when given. Class sections are tracked so --class can reject an unknown
    name."""
    entries = []
    classes_seen = []
    current_class = None
    pending = None
    pending_class = None
    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            class_match = CLASS_LINE_RE.match(raw)
            if class_match:
                current_class = class_match.group(1)
                if current_class not in classes_seen:
                    classes_seen.append(current_class)
                pending = None
                continue
            if VIRTUAL_LINE_RE.match(raw):
                pending = [raw.strip()]
                pending_class = current_class
            elif pending is not None:
                pending.append(raw.strip())
            if pending is None:
                continue
            if only_class is not None and pending_class != only_class:
                if ";" in " ".join(pending):
                    pending = None
                continue
            text = " ".join(pending)
            if ";" not in text:
                continue
            before, after = text.split(";", 1)
            pending = None
            comment = OFFSET_COMMENT_RE.search(after)
            if comment is None:
                continue
            name = method_name(before)
            if name is None:
                print("warning: unreadable declaration at offset %s"
                      % comment.group(1), file=sys.stderr)
                continue
            entries.append((int(comment.group(1), 16), comment.group(1), name,
                            comment.group(2) == "pure"))
    return entries, classes_seen


def parse_dump(path):
    """Read the dump into {offset: raw entry text}."""
    cells = {}
    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            match = DUMP_LINE_RE.match(raw)
            if match is None:
                continue
            offset = int(match.group(2), 16)
            entry = match.group(3).strip()
            if offset in cells and cells[offset] != entry:
                print("warning: dump declares offset %s twice" % match.group(2),
                      file=sys.stderr)
            cells[offset] = entry
    return cells


def base_name(entry):
    """Class-less method name of a demangled dump entry; empty cells and pure
    slots stay as they are."""
    if entry == EMPTY_CELL or entry == PURE_VIRTUAL:
        return entry
    paren = entry.find("(")
    head = entry if paren < 0 else entry[:paren]
    return head.split("::")[-1].strip()


def main():
    parser = argparse.ArgumentParser(
        description="Compare the vtable offsets of AWDLInterface.h with a vtable dump.")
    parser.add_argument("dump", help="vtable dump file (index, hex offset, demangled method)")
    parser.add_argument("--class", dest="klass", default=None, metavar="NAME",
                        help="check only the declarations of this header class section")
    parser.add_argument("--header", default=str(DEFAULT_HEADER), metavar="PATH",
                        help="header to check (default: Source/AirLayer/AWDLInterface.h)")
    args = parser.parse_args()

    header_path = Path(args.header)
    dump_path = Path(args.dump)
    for path in (header_path, dump_path):
        if not path.is_file():
            print("error: %s does not exist" % path, file=sys.stderr)
            return 2

    entries, classes_seen = parse_header(header_path, args.klass)
    if args.klass is not None and args.klass not in classes_seen:
        print("error: class %s not found in %s (found: %s)"
              % (args.klass, header_path, ", ".join(classes_seen)), file=sys.stderr)
        return 2
    if not entries:
        print("error: no offset comments found in %s" % header_path, file=sys.stderr)
        return 2

    declared = [entry[0] for entry in entries]
    if len(set(declared)) != len(declared):
        print("warning: the header declares the same offset more than once",
              file=sys.stderr)
    entries.sort(key=lambda entry: entry[0])
    cells = parse_dump(dump_path)
    if not cells:
        print("error: no dump entries parsed from %s" % dump_path, file=sys.stderr)
        return 2

    # First dump offset at which each class-less name appears, from the lowest
    # checked offset on. Used only to annotate mismatches, never to turn them
    # into OK: an occupied cell with a different name stays a mismatch.
    scope_min = min(declared)
    name_offsets = {}
    for offset, entry in cells.items():
        if offset >= scope_min and entry not in (EMPTY_CELL, PURE_VIRTUAL):
            name_offsets.setdefault(base_name(entry), offset)

    width_offset = max(len(entry[1]) for entry in entries)
    width_header = max(len(entry[2]) for entry in entries)
    print("%-*s  %-*s  %s" % (width_offset, "OFFSET",
                              width_header, "HEADER METHOD", "DUMP ENTRY / STATUS"))
    counts = {STATUS_OK: 0, STATUS_MISMATCH: 0, STATUS_MISSING: 0}
    for offset, offset_text, name, pure in entries:
        if offset not in cells:
            dump_text = ""
            status = STATUS_MISSING
            detail = "(beyond the dump)"
        else:
            raw = cells[offset]
            if raw == EMPTY_CELL:
                dump_text = raw
                status = STATUS_MISSING
                detail = "(cell is 0)"
            elif base_name(raw) == name:
                dump_text = raw
                status = STATUS_OK
                detail = ""
            elif raw == PURE_VIRTUAL:
                dump_text = raw
                if pure:
                    status = STATUS_OK
                    detail = "(pure on both sides)"
                else:
                    status = STATUS_MISMATCH
                    detail = "(pure slot in the dump class)"
            else:
                dump_text = raw
                status = STATUS_MISMATCH
                found = name_offsets.get(name)
                if found is not None and found != offset:
                    detail = "(name also at 0x%X: possible slot shift)" % found
                else:
                    detail = "(name absent from the dump: possible naming difference)"
        counts[status] += 1
        line = "%-*s  %-*s  %s  %s" % (width_offset, offset_text,
                                       width_header, name, dump_text, status)
        if detail:
            line += " " + detail
        print(line.rstrip())

    print("Checked %d header offsets against %s (dump covers 0x%X..0x%X): "
          "%d OK, %d MISMATCH, %d MISSING."
          % (len(entries), dump_path.name, min(cells), max(cells),
             counts[STATUS_OK], counts[STATUS_MISMATCH], counts[STATUS_MISSING]))
    return 1 if counts[STATUS_MISMATCH] else 0


if __name__ == "__main__":
    sys.exit(main())
