#!/usr/bin/env python3
"""Cross-check the documented AWDL pure slots against the isolated C++ header.

Usage: check_awdl_header.py [pure_slots.md [awdl_header.h]]
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
if len(sys.argv) > 3:
    print(f"Usage: {pathlib.Path(sys.argv[0]).name} [pure_slots.md [awdl_header.h]]", file=sys.stderr)
    sys.exit(2)
TABLE_PATH = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "Docs/AWDL_PURE_SLOTS.md"
HEADER_PATH = pathlib.Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "Source/IO80211/IO80211AWDL_25_6_0.h"
EXPECTED_SLOTS = [490, *range(507, 570)]
KNOWN_NON_PURE_TRAILER = {570, 571, 572}

TABLE_ROW = re.compile(
    r"^\|\s*(\d+)\s*\|\s*`([^`]+)`\s*\|\s*`([^`]+)`\s*\|\s*$"
)
HEADER_METHOD = re.compile(
    r"^\s*virtual\s+\w+\s+(\w+)\(([^()]*)\)\s*=\s*0;\s*//\s*slot\s+(\d+)\s*$"
)


def fail(message: str) -> None:
    print(f"FAIL {message}")


def normalize_type(text: str) -> str:
    """Ignore formatting whitespace while retaining qualifiers and punctuation."""
    return re.sub(r"\s+", "", text)


def parse_table(path: pathlib.Path) -> dict[int, tuple[str, tuple[str, ...], str]]:
    rows: dict[int, tuple[str, tuple[str, ...], str]] = {}
    for line_no, line in enumerate(path.read_text().splitlines(), 1):
        match = TABLE_ROW.match(line)
        if not match:
            continue
        slot, address, signature = match.groups()
        slot_no = int(slot)
        if slot_no in KNOWN_NON_PURE_TRAILER:
            continue  # AWDL_PURE_SLOTS.md also documents three concrete Broadcom-only slots.
        if slot_no not in EXPECTED_SLOTS:
            raise ValueError(f"{path}:{line_no}: unexpected slot row {slot_no}")
        name, open_paren, args_and_close = signature.partition("(")
        if not open_paren or not args_and_close.endswith(")"):
            raise ValueError(f"{path}:{line_no}: cannot parse signature {signature!r}")
        arg_text = args_and_close[:-1].strip()
        args = tuple(arg.strip() for arg in arg_text.split(",")) if arg_text else ()
        if slot_no in rows:
            raise ValueError(f"{path}:{line_no}: duplicate slot {slot_no}")
        rows[slot_no] = (name.strip(), args, address)
    return rows


def parse_header(path: pathlib.Path) -> dict[int, tuple[str, tuple[str, ...]]]:
    rows: dict[int, tuple[str, tuple[str, ...]]] = {}
    for line_no, line in enumerate(path.read_text().splitlines(), 1):
        match = HEADER_METHOD.match(line)
        if not match:
            continue
        name, arg_text, slot = match.groups()
        slot_no = int(slot)
        args = tuple(arg.strip() for arg in arg_text.split(",")) if arg_text.strip() else ()
        if slot_no in rows:
            raise ValueError(f"{path}:{line_no}: duplicate pure slot comment {slot_no}")
        rows[slot_no] = (name, args)
    return rows


def main() -> int:
    for path in (TABLE_PATH, HEADER_PATH):
        if not path.is_file():
            print(f"ERROR missing input: {path.relative_to(ROOT)}", file=sys.stderr)
            return 2

    try:
        table = parse_table(TABLE_PATH)
        header = parse_header(HEADER_PATH)
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"ERROR {exc}", file=sys.stderr)
        return 2

    errors = 0
    expected = set(EXPECTED_SLOTS)
    for label, rows in (("table", table), ("header", header)):
        actual = set(rows)
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        if missing:
            fail(f"{label}: missing slots {missing}")
            errors += len(missing)
        if extra:
            fail(f"{label}: unexpected pure-slot rows {extra}")
            errors += len(extra)
        if len(rows) != 64:
            fail(f"{label}: expected 64 unique pure slots, found {len(rows)}")
            errors += 1

    table_order = list(table)
    header_order = list(header)
    if table_order != EXPECTED_SLOTS:
        fail("table rows are not in the required slot order 490, 507..569")
        errors += 1
    if header_order != EXPECTED_SLOTS:
        fail("header declarations/comments are not in the required slot order 490, 507..569")
        errors += 1

    print("AWDL pure-slot header cross-check:")
    for slot in EXPECTED_SLOTS:
        if slot not in table or slot not in header:
            continue
        table_name, table_args, address = table[slot]
        header_name, header_args = header[slot]
        issues = []
        if table_name != header_name:
            issues.append(f"method table={table_name} header={header_name}")
        normalized_table_args = tuple(normalize_type(arg) for arg in table_args)
        normalized_header_args = tuple(normalize_type(arg) for arg in header_args)
        if normalized_table_args != normalized_header_args:
            issues.append(f"args table={table_args!r} header={header_args!r}")
        if issues:
            print(f"FAIL slot {slot} {address}: " + "; ".join(issues))
            errors += 1
        else:
            print(f"PASS slot {slot}: {table_name}({', '.join(table_args)})")

    if errors:
        print(f"Summary: FAILED ({errors} discrepancy/discrepancies)")
        return 1
    print("Summary: PASS (64/64 slots, order, method names, argument types and slot comments match)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
