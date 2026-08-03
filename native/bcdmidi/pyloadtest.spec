# -*- mode: python ; coding: utf-8 -*-
#
# Packages pyloadtest.py the SAME WAY the bridge is packaged, because the
# packaging is the thing under test. Everything that matters is copied from
# poc/BCD3000Bridge.spec and is marked below:
#
#   onefile            EXE() is given a.binaries and a.datas directly, with no
#                      COLLECT step. That is what makes the payload unpack into
#                      %TEMP%\_MEIxxxxx at run time instead of sitting beside
#                      the .exe.
#   runtime_tmpdir=None  same as the bridge - the payload goes to %TEMP%.
#   upx=True           same as the bridge. UPX is not on PATH on this machine,
#                      so it is a no-op here; it is written anyway so that this
#                      package cannot differ from the bridge's by accident.
#
# ONE DELIBERATE DIFFERENCE: console=True. The bridge is windowed and has
# nothing to say; this thing exists only to print.
#
# NOT COMMITTED ANYWHERE NEAR poc/. This is a probe, not a product.

import os

BCDMIDI = SPECPATH
BINARIES = [
    (os.path.join(BCDMIDI, 'BcdMidi.dll'), '.'),
    (os.path.join(BCDMIDI, 'Windows.Devices.Midi2.dll'), '.'),
]
DATAS = [
    (os.path.join(BCDMIDI, 'Windows.Devices.Midi2.pri'), '.'),
]

# PyInstaller only WARNS about a missing binary and then builds an .exe without
# it. A probe that silently tests a package missing the very file it is probing
# for would answer the wrong question. Same rule as build.bat: the file has to
# exist AND the step has to succeed.
for _src, _ in BINARIES + DATAS:
    if not os.path.exists(_src):
        raise SystemExit(
            'pyloadtest.spec: missing payload %s\n'
            'Run build.bat dll first - it builds BcdMidi.dll and copies '
            "Microsoft's runtime DLL and .pri beside it." % _src)

a = Analysis(
    [os.path.join(BCDMIDI, 'pyloadtest.py')],
    pathex=[BCDMIDI],
    binaries=BINARIES,
    datas=DATAS,
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='pyloadtest',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
