# -*- mode: python ; coding: utf-8 -*-
#
# THE TWO DLLs BELOW ARE NOT OPTIONAL AND THEY ARE NOT ENOUGH ON THEIR OWN.
# Read this before changing either list.
#
# BcdMidi.dll is the product's virtual MIDI port. It does NOT import Microsoft's
# Windows.Devices.Midi2.dll: dumpbin /dependents shows no entry for it. Nothing
# is registered and there is no manifest either. The link is made at run time by
# C++/WinRT, which - see winrt/winrt/base.h,
# get_runtime_activation_factory_impl() - falls back to loading the bare name
# "Windows.Devices.Midi2.dll" with LOAD_LIBRARY_SEARCH_DEFAULT_DIRS, i.e.
# APPLICATION_DIR + SYSTEM32 + USER_DIRS.
#
# This is a ONE-FILE package with runtime_tmpdir=None, so the payload below is
# unpacked into %TEMP%\_MEIxxxxx while the .exe stays in
# %LOCALAPPDATA%\BCD3000Bridge\. Those are different directories, and
# _MEIxxxxx is in NONE of the three the loader searches. Measured on this
# machine on 2026-08-01, unfrozen, with the exact flag C++/WinRT passes: a
# bare-name load fails with error 126 unless the directory holding the DLL has
# been put into USER_DIRS.
#
# SO SHIPPING THE FILE IS HALF THE JOB. The other half belongs to whoever loads
# BcdMidi.dll: it must call os.add_dll_directory(sys._MEIPASS) BEFORE the first
# call into the DLL, and keep the returned object alive. See
# native/bcdmidi/pyloadtest.py, which measures all of this, and Task 2's report.
#
# The .pri is Microsoft's resource file for that DLL and travels beside it, the
# same way native/bcdmidi/build.bat copies it beside our binaries.

import os

_BCDMIDI = os.path.join(SPECPATH, '..', 'native', 'bcdmidi')

_BINARIES = [
    (os.path.join(_BCDMIDI, 'BcdMidi.dll'), '.'),
    (os.path.join(_BCDMIDI, 'Windows.Devices.Midi2.dll'), '.'),
]
_DATAS = [
    (os.path.join(_BCDMIDI, 'Windows.Devices.Midi2.pri'), '.'),
]

# A missing entry in binaries= is only a WARNING to PyInstaller: it builds a
# perfectly good .exe without the file and the failure surfaces later, on the
# owner's machine, as a MIDI port that never appears. The rule in this
# repository is that the file has to exist AND the step has to succeed - see the
# header of native/bcdmidi/build.bat - so a missing payload stops the build here.
for _src, _ in _BINARIES + _DATAS:
    if not os.path.exists(_src):
        raise SystemExit(
            'BCD3000Bridge.spec: missing payload %s\n'
            'Build it first:  cd ..\\native\\bcdmidi && build.bat dll\n'
            "That target builds BcdMidi.dll and copies Microsoft's runtime DLL "
            'and .pri beside it.' % os.path.abspath(_src))

# SPECPATH, NOT AN ABSOLUTE PATH, AND THAT IS THE WHOLE POINT OF THIS COMMENT.
# PyInstaller writes these two lines as absolute paths to whatever machine ran it
# first, and the file was published carrying one developer's home directory. That
# is not only a privacy leak: a clone on any other machine builds NOTHING, because
# Analysis() is handed a script path that does not exist there. SPECPATH is the
# directory holding this .spec - poc/ - which is exactly what those absolute paths
# used to name, so the build is unchanged and now works from any checkout. The two
# payload lists above already used it; these two did not.
a = Analysis(
    [os.path.join(SPECPATH, 'bridge_service.py')],
    pathex=[SPECPATH],
    binaries=_BINARIES,
    datas=_DATAS,
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
    name='BCD3000Bridge',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
