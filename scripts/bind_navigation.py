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

    def box(name):
        match = re.search(rf"BOX\s+@{name}, ([^\n]+)", source)
        if not match:
            raise ValueError(f"missing navigation element: {name}")
        return [part.strip() for part in match[1].split(",")]

    viewport = box("view_nav")
    width, height = map(int, viewport[2:4])
    car_x, car_y, car_w, car_h = map(int, box("nav_car_north")[:4])
    near_w, near_h = map(int, box("nav_map_near")[2:4])
    wide_w, wide_h = map(int, box("nav_map_wide")[2:4])
    touch_top = 76                     # Below the turn instruction card.
    touch_width = int(box("nav_zoom_in")[0]) - 8
    touch_height = height - touch_top - 54  # Above the route summary.
    if min(touch_width, touch_height) <= 0 or near_w < width or wide_w < width or min(near_h, wide_h) < height:
        raise ValueError("navigation map does not cover the viewport")
    geometry = {
        "CAR_X": car_x, "CAR_Y": car_y,
        "CAR_CX": car_w // 2, "CAR_CY": car_h * 3 // 5,
        "CENTER_X": car_x + car_w // 2, "CENTER_Y": car_y + car_h * 3 // 5,
        "NEAR_MIN_X": width - near_w, "NEAR_MIN_Y": height - near_h,
        "WIDE_MIN_X": width - wide_w, "WIDE_MIN_Y": height - wide_h,
        "TOUCH_TOP": touch_top,
    }
    for name in ("north", "east"):
        pattern = rf"(BOX\s+@nav_car_{name}, ){car_x}, {car_y},"
        source, count = re.subn(pattern, r"\g<1>$navg_car_x, $navg_car_y,", source)
        if count != 1:
            raise ValueError(f"missing vehicle projection: {name}")
    target = (f"        BOX @nav_gestures, 0, {touch_top}, {touch_width}, {touch_height}, MULTI\n"
              "        ON PRESS, navg_press\n        ON DRAG, navg_drag\n"
              "        ON PINCH, navg_pinch\n        END\n")
    for key, value in geometry.items():
        bindings = bindings.replace(f"@{key}@", str(value))
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
