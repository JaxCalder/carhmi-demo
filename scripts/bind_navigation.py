#!/usr/bin/env python3
"""Bind dmview's native DRAG/PINCH to the HTML navigation's named groups.

The HTML converter handles the rest of the page. Fail on an incompatible
layout rather than silently shipping a view whose gestures cannot work.
"""
import re
import sys
from pathlib import Path


def bind(source, bindings):
    if "; Native touch bindings." in source:
        raise ValueError("gestures already bound: regenerate the HTML view first")
    handlers = {}
    for element, replacement in (("nav_recenter", "navg_recenter"),
                                 ("nav_pause", "navg_pause"),
                                 ("nav_zoom_in", "navg_zoom_in"),
                                 ("nav_zoom_out", "navg_zoom_out")):
        pattern = rf"(BOX\s+@{element},[^\n]*\n\s*ON\s+CLICK, )([A-Za-z0-9_]+)"
        match = re.search(pattern, source)
        if not match:
            raise ValueError(f"missing clickable navigation element: {element}")
        handlers[element] = match[2]
        source = source[:match.start(2)] + replacement + source[match.end(2):]
    for name in ("north", "east"):
        pattern = rf"(BOX\s+@nav_car_{name}, )176, 160,"
        source, count = re.subn(pattern, r"\g<1>$navg_car_x, $navg_car_y,", source)
        if count != 1:
            raise ValueError(f"missing vehicle projection: {name}")
    pattern = r"(BOX\s+@view_nav, 0, 0, 430, 248\n\s*OPACITY[^\n]*\n\s*FILL[^\n]*\n)"
    target = ("        BOX @nav_gestures, 0, 76, 380, 118, MULTI\n"
              "        ON PRESS, navg_press\n        ON DRAG, navg_drag\n"
              "        ON PINCH, navg_pinch\n        END\n")
    if not re.search(pattern, source):
        raise ValueError("missing navigation viewport")
    # Put the contact surface above the map/vehicle boxes, below the controls.
    # The runtime captures the deepest hit box; a background sibling cannot
    # receive touches through a later map image box.
    at = source.index("        BOX     @nav_zoom_in,")
    source = source[:at] + target + source[at:]
    bindings = bindings.replace("@RECENTER_HANDLER@", handlers["nav_recenter"])
    bindings = bindings.replace("@PAUSE_HANDLER@", handlers["nav_pause"])
    declarations = "\n".join(line for line in bindings.splitlines() if line.startswith(".var "))
    bindings = "\n".join(line for line in bindings.splitlines() if not line.startswith(".var "))
    at = source.index(".var ")
    source = source[:at] + declarations + "\n" + source[at:]
    return source + "\n" + bindings


if __name__ == "__main__":
    path = Path(sys.argv[1])
    path.write_text(bind(path.read_text(), Path(sys.argv[2]).read_text()))
