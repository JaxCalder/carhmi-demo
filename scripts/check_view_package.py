#!/usr/bin/env python3
"""Verify that the release package contains both current compiled view bundles."""
import struct
import sys
import zipfile
from pathlib import Path

base = Path(sys.argv[1])
with zipfile.ZipFile(base / 'packages/carhmi_view.zip') as package:
    modules = [name for name in package.namelist() if name.endswith('.dmf')]
    if modules != ['carhmi_view.dmf']:
        raise SystemExit(f'Expected one shared view module, got {modules}')
    for screen, size in [('480x272', (480, 272)), ('320x240', (320, 240))]:
        name = f'views/{screen}/carhmi.dmv'
        view = package.read(name)
        if struct.unpack_from('<HH', view, 12) != size:
            raise SystemExit(f'Wrong view size: {name}')
        for resource in (base / 'views' / screen).iterdir():
            if resource.is_file() and package.read(f'views/{screen}/{resource.name}') != resource.read_bytes():
                raise SystemExit(f'Stale packaged resource: {resource}')
print('carhmi_view: both screen bundles verified')
