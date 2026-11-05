#!/usr/bin/env python3
"""Bind the tank liquid to a bottom-anchored native DMV clipping group.

The HTML converter supports property-bound widths. Vertical liquid uses
its standard BOX height/Y operands; geometry comes from the HTML layout.
"""
import re
import sys
from pathlib import Path


def bind(source):
    pattern = r"(BOX\s+@fuel_well, )(-?\d+), (-?\d+), (\d+), (\d+)(, OPAQUE)?"
    match = re.search(pattern, source)
    if not match or '"fuel.level"' not in source or '"fuel.stage"' not in source:
        raise ValueError("missing fuel geometry/properties; use dmvs_html/dmvs_js >= 0.4 and libtodmvs/dmvsi >= 0.8")
    x, y, width, height = map(int, match.group(2, 3, 4, 5))
    if min(width, height) <= 0:
        raise ValueError("fuel well must have a positive size")
    source = source[:match.start()] + (
        f"{match[1]}{x}, $fuel_liquid_y, {width}, $fuel_liquid_h"
    ) + source[match.end():]
    at = source.index(".var ")
    source = source[:at] + (
        f".var $fuel_liquid_y, int, {y}\n"
        f".var $fuel_liquid_h, int, {height}\n"
    ) + source[at:]
    at = source.index(".change ")
    source = source[:at] + ".change $fuel_level, fuel_update_liquid\n" + source[at:]
    return source + f"""
; Vertical fuel level: native clipping follows the converted HTML well.
fuel_update_liquid:
        SET $fuel_liquid_h, $fuel_level
        CLAMP $fuel_liquid_h, 0, 1000
        MUL $fuel_liquid_h, {height}
        DIV $fuel_liquid_h, 1000
        SET $fuel_liquid_y, {y + height}
        SUB $fuel_liquid_y, $fuel_liquid_h
        RET
"""


if __name__ == "__main__":
    path = Path(sys.argv[1])
    path.write_text(bind(path.read_text()))
