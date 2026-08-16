#!/usr/bin/env python3
"""Print the useful numbers from a z88dk chess.map."""

import sys

need = [
    "__CODE_head",
    "__CODE_END_head",
    "__RODATA_head",
    "__RODATA_END_head",
    "__DATA_head",
    "__DATA_END_head",
    "__BSS_head",
    "__BSS_END_head",
    "CRT_ORG_CODE",
    "CRT_MAX_HEAP_ADDRESS",
]
found = {}
with open(sys.argv[1]) as f:
    for line in f:
        name = line.split("=")[0].strip()
        if name in need:
            found[name] = int(
                line.split("=")[1].split(";")[0].strip().replace("$", "0x"), 16
            )


def sz(a, b):
    return found.get(b, 0) - found.get(a, 0)


org = found.get("CRT_ORG_CODE", 0x8000)
bss_end = found.get("__BSS_END_head", 0)
heap = found.get("CRT_MAX_HEAP_ADDRESS", 0xFF56)
print()
print("map:")
print("  org        $%04X" % org)
print(
    "  code       %6d  ($%04X-$%04X)"
    % (
        sz("__CODE_head", "__CODE_END_head"),
        found.get("__CODE_head", 0),
        found.get("__CODE_END_head", 0),
    )
)
print(
    "  rodata     %6d  ($%04X-$%04X)"
    % (
        sz("__RODATA_head", "__RODATA_END_head"),
        found.get("__RODATA_head", 0),
        found.get("__RODATA_END_head", 0),
    )
)
print(
    "  data       %6d  ($%04X-$%04X)"
    % (
        sz("__DATA_head", "__DATA_END_head"),
        found.get("__DATA_head", 0),
        found.get("__DATA_END_head", 0),
    )
)
print(
    "  bss        %6d  ($%04X-$%04X)"
    % (
        sz("__BSS_head", "__BSS_END_head"),
        found.get("__BSS_head", 0),
        found.get("__BSS_END_head", 0),
    )
)
print("  total RAM  %6d  ($%04X-$%04X)" % (bss_end - org, org, bss_end))
print("  stack room %6d  (to $%04X)" % (heap - bss_end, heap))
