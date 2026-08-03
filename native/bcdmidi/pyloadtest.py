# BcdMidi load probe - the Task 2 gate.
#
# ONE QUESTION: when everything runs from inside a PyInstaller one-file
# executable, can BcdMidi.dll reach Microsoft's Windows.Devices.Midi2.dll?
#
# WHY THAT IS IN DOUBT, read off the generated projection and not guessed.
# winrt/winrt/base.h, get_runtime_activation_factory_impl(): when
# RoGetActivationFactory fails - which it does, because nothing here is
# registered - C++/WinRT walks the class name down to "Windows.Devices.Midi2.dll"
# and calls impl::load_library on that BARE NAME. impl::load_library, same file,
# is exactly:
#
#     LoadLibraryExW(library, nullptr, 0x00001000 /* LOAD_LIBRARY_SEARCH_DEFAULT_DIRS */)
#
# LOAD_LIBRARY_SEARCH_DEFAULT_DIRS is APPLICATION_DIR + SYSTEM32 + USER_DIRS.
# It is NOT the current directory, NOT %PATH%, and NOT the directory the calling
# DLL happens to live in. In a one-file package the application directory is
# where the .exe sits and the unpacked payload is in %TEMP%\_MEIxxxxx, so the two
# are different directories and the bare-name load has nothing to find.
#
# THE PROBE MIRRORS THAT CALL EXACTLY, and that is the point: stages 0 to 4
# answer the search-path half of the gate WITHOUT CREATING A MIDI PORT and
# without touching the MIDI service at all. Only --create goes further.
#
# WHY THAT MATTERS MORE THAN TIDINESS: microsoft/MIDI issue #1047, open as of
# 2026-08-01. Creating a virtual MIDI device can leave the machine's MIDI service
# unable to answer until a reboot. Every port this probe creates costs a reboot
# on the owner's working machine, so the default is no port, one create per run
# at most, and the mode is chosen on the command line rather than by a loop.
#
# USAGE
#     pyloadtest.exe [plain|adddir|preload] [--create]
#
#   plain    (default) load the Microsoft DLL the way C++/WinRT will, with no
#            help at all. This is the honest gate answer.
#   adddir   call os.add_dll_directory() on the payload directory first. That
#            adds it to USER_DIRS, which LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#            includes, so the same bare-name load then resolves.
#   preload  load the Microsoft DLL by absolute path first. A module already in
#            the process is matched by base name, so the later bare-name load
#            returns the existing handle without searching anything.
#   --create additionally calls BcdMidiCreatePort. THIS CREATES A REAL MIDI PORT.
#
# Exit codes: 0 all requested stages passed, 3 the Microsoft DLL was not
# reachable, 4 BcdMidi.dll would not load, 5 the port was not created.

import ctypes
import ctypes.wintypes as W
import os
import sys

MIDI2_DLL = "Windows.Devices.Midi2.dll"
BCDMIDI_DLL = "BcdMidi.dll"
MIDI2_PRI = "Windows.Devices.Midi2.pri"

# The single flag C++/WinRT passes. Duplicated here on purpose: if base.h ever
# changes, this probe must be changed with it and the comment above is the
# reason why.
LOAD_LIBRARY_SEARCH_DEFAULT_DIRS = 0x00001000

# The port name. 31 usable characters is the WinMM budget (see bcdmidi.h); this
# is 16. It is deliberately NOT "BCD3000": if a port ever survives a run of this
# probe, it must not be the one VirtualDJ's factory mapping matches on.
PROBE_PORT_NAME = "BCD3000 LOADTEST"

# Kept alive at module scope on purpose. os.add_dll_directory() returns an
# _AddedDllDirectory whose close() removes the directory again; the class has no
# __del__ in CPython 3.11, so dropping the reference would not undo it today,
# but relying on the absence of a finalizer is not a thing worth relying on.
_dll_dir_cookie = None
_preloaded = None


def payload_dir():
    """Where our two DLLs actually are. In a one-file package that is the
    unpacked payload directory, which is NOT where the .exe is."""
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        return meipass
    return os.path.dirname(os.path.abspath(__file__))


def app_dir():
    """The application directory, in the sense LOAD_LIBRARY_SEARCH_APPLICATION_DIR
    means it: the directory of the running image."""
    return os.path.dirname(os.path.abspath(sys.executable))


def get_dll_directory():
    """Whatever SetDllDirectoryW() was last called with in this process, or
    None if nobody called it.

    This is not curiosity. LOAD_LIBRARY_SEARCH_DEFAULT_DIRS is
    APPLICATION_DIR + SYSTEM32 + USER_DIRS, and USER_DIRS means "the
    directories added by AddDllDirectory OR SetDllDirectory". PyInstaller's
    bootloader imports SetDllDirectoryW - dumpbin -imports on the packaged .exe
    shows exactly one such import and no AddDllDirectory - so if it points at
    the payload directory, that one call is the entire reason a bare-name load
    works from a one-file package. Print it, do not assume it."""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.GetDllDirectoryW.restype = ctypes.c_uint32
    k32.GetDllDirectoryW.argtypes = [ctypes.c_uint32, ctypes.c_wchar_p]
    n = k32.GetDllDirectoryW(0, None)
    if not n:
        return None
    buf = ctypes.create_unicode_buffer(n + 1)
    if not k32.GetDllDirectoryW(n + 1, buf):
        return None
    return buf.value or None


def banner(text):
    print("")
    print("=" * 70)
    print(text)
    print("=" * 70)


def stage0_environment():
    banner("STAGE 0  environment")
    print("frozen            : %r" % getattr(sys, "frozen", False))
    print("sys._MEIPASS      : %s" % getattr(sys, "_MEIPASS", "(none)"))
    print("sys.executable    : %s" % sys.executable)
    print("application dir   : %s" % app_dir())
    print("payload dir       : %s" % payload_dir())
    print("cwd               : %s" % os.getcwd())
    print("python            : %s" % sys.version.split()[0])
    d = get_dll_directory()
    print("GetDllDirectoryW  : %s" % (d if d else "(not set)"))
    if d and os.path.normcase(d) == os.path.normcase(payload_dir()):
        print("                    ^ EQUALS the payload dir, so the payload dir")
        print("                      is in USER_DIRS and therefore inside")
        print("                      LOAD_LIBRARY_SEARCH_DEFAULT_DIRS")
    same = os.path.normcase(app_dir()) == os.path.normcase(payload_dir())
    print("app dir == payload: %s" % same)
    if not same:
        print("                    ^ this difference is the whole risk")


def stage1_inventory():
    banner("STAGE 1  where the files are")
    seen = {}
    for label, d in (("payload dir", payload_dir()),
                     ("application dir", app_dir())):
        for name in (BCDMIDI_DLL, MIDI2_DLL, MIDI2_PRI):
            p = os.path.join(d, name)
            ok = os.path.exists(p)
            size = os.path.getsize(p) if ok else 0
            print("%-16s %-28s %-5s %s"
                  % (label, name, "yes" if ok else "NO",
                     ("%d bytes" % size) if ok else ""))
            seen[(label, name)] = ok
    return seen


def stage2_apply_mode(mode):
    global _dll_dir_cookie, _preloaded
    banner("STAGE 2  mode: %s" % mode)
    if mode == "plain":
        print("nothing done - the loader gets no help. This is what the bridge")
        print("does today if nobody changes it.")
        return True
    if mode == "adddir":
        d = payload_dir()
        try:
            _dll_dir_cookie = os.add_dll_directory(d)
        except OSError as e:
            print("os.add_dll_directory(%s) FAILED: %s" % (d, e))
            return False
        print("os.add_dll_directory(%s) ok" % d)
        print("that directory is now in USER_DIRS, which")
        print("LOAD_LIBRARY_SEARCH_DEFAULT_DIRS includes")
        return True
    if mode == "preload":
        p = os.path.join(payload_dir(), MIDI2_DLL)
        try:
            _preloaded = ctypes.WinDLL(p, use_last_error=True)
        except OSError as e:
            print("preload of %s FAILED: %s" % (p, e))
            return False
        print("preloaded by absolute path: %s" % p)
        print("a module already in the process is matched by BASE NAME, so the")
        print("bare-name load in stage 3 should now return that same handle")
        return True
    print("unknown mode %r" % mode)
    return False


def stage3_reach_microsoft_dll():
    """The measurement. Byte for byte the call C++/WinRT makes."""
    banner("STAGE 3  the bare-name load C++/WinRT will do")
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.LoadLibraryExW.restype = ctypes.c_void_p
    k32.LoadLibraryExW.argtypes = [W.LPCWSTR, ctypes.c_void_p, ctypes.c_uint32]
    k32.GetProcAddress.restype = ctypes.c_void_p
    k32.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    k32.GetModuleFileNameW.restype = ctypes.c_uint32
    k32.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p,
                                       ctypes.c_uint32]
    k32.GetModuleHandleW.restype = ctypes.c_void_p
    k32.GetModuleHandleW.argtypes = [W.LPCWSTR]

    # THE CONTROL. A success below could mean two different things: the payload
    # directory really is being searched, or the module simply happened to be
    # loaded already - the loader matches an already-loaded module by BASE NAME
    # before it searches any directory, which is precisely what mode "preload"
    # exploits. These two lines tell those apart.
    already = k32.GetModuleHandleW(MIDI2_DLL)
    print("%s already in the process? %s"
          % (MIDI2_DLL, ("YES, handle 0x%X" % already) if already else "no"))
    # And the second control: a bare-name load of a DLL that exists ONLY in the
    # payload directory and is nowhere in system32, nowhere in the application
    # directory. If THIS resolves, the payload directory is genuinely on the
    # search path and the result above is not an accident of some other copy.
    ctypes.set_last_error(0)
    hb = k32.LoadLibraryExW(BCDMIDI_DLL, None, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
    if hb:
        buf = ctypes.create_unicode_buffer(1024)
        k32.GetModuleFileNameW(hb, buf, 1024)
        print("bare-name load of %s -> %s" % (BCDMIDI_DLL, buf.value))
        print("  the payload directory IS on the search path")
    else:
        e = ctypes.get_last_error()
        print("bare-name load of %s -> NULL, GetLastError %d (%s)"
              % (BCDMIDI_DLL, e, ctypes.FormatError(e).strip()))
        print("  the payload directory is NOT on the search path")
    print("")

    print('LoadLibraryExW("%s", NULL, 0x%08X)'
          % (MIDI2_DLL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
    ctypes.set_last_error(0)
    h = k32.LoadLibraryExW(MIDI2_DLL, None, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
    err = ctypes.get_last_error()
    if not h:
        print("  -> NULL, GetLastError %d (%s)"
              % (err, ctypes.FormatError(err).strip()))
        print("")
        print("VERDICT: Microsoft's DLL is NOT REACHABLE from this process.")
        print("Every WinRT activation in BcdMidi.dll will fail the same way.")
        return False
    print("  -> handle 0x%X" % h)
    buf = ctypes.create_unicode_buffer(1024)
    if k32.GetModuleFileNameW(h, buf, 1024):
        print("  -> resolved to %s" % buf.value)
    fac = k32.GetProcAddress(h, b"DllGetActivationFactory")
    print("  -> DllGetActivationFactory: %s"
          % (("0x%X" % fac) if fac else "MISSING - the fallback cannot work"))
    if not fac:
        return False
    print("")
    print("VERDICT: Microsoft's DLL is REACHABLE and exports the entry point")
    print("registration-free activation needs.")
    return True


def stage4_load_bcdmidi():
    banner("STAGE 4  load BcdMidi.dll  (no port is created here)")
    p = os.path.join(payload_dir(), BCDMIDI_DLL)
    print("loading %s" % p)
    try:
        lib = ctypes.WinDLL(p, use_last_error=True)
    except OSError as e:
        print("LOAD FAILED: %s" % e)
        return None
    print("loaded, handle 0x%X" % lib._handle)

    # THE SIGNATURE IS THE ONE IN bcdmidi.h AND IT HAS FIVE ARGUMENTS.
    # ctypes cannot check a declaration against a DLL. Declaring four against a
    # five-argument export makes the callee take whatever the fifth register
    # holds as a long* and write an HRESULT through it, corrupting memory that
    # belongs to something else, silently.
    #     void* BcdMidiCreatePort(const wchar_t* name, BcdMidiRecvCb cb,
    #                             void* user, unsigned int* errOut, long* hrOut)
    lib.BcdMidiCreatePort.restype = ctypes.c_void_p
    lib.BcdMidiCreatePort.argtypes = [W.LPCWSTR, ctypes.c_void_p,
                                      ctypes.c_void_p,
                                      ctypes.POINTER(ctypes.c_uint),
                                      ctypes.POINTER(ctypes.c_long)]
    lib.BcdMidiClosePort.restype = None
    lib.BcdMidiClosePort.argtypes = [ctypes.c_void_p]
    lib.BcdMidiErrorText.restype = ctypes.c_char_p
    lib.BcdMidiErrorText.argtypes = [ctypes.c_uint]
    print("resolved BcdMidiCreatePort (5 args), BcdMidiClosePort, "
          "BcdMidiErrorText")
    return lib


def stage5_create(lib):
    banner("STAGE 5  BcdMidiCreatePort  *** THIS CREATES A REAL MIDI PORT ***")
    err = ctypes.c_uint(0)
    hr = ctypes.c_long(0)
    print('calling BcdMidiCreatePort("%s", NULL, NULL, &err, &hr)'
          % PROBE_PORT_NAME)
    print("this can take up to 45 seconds - the DLL has a time limit on it")
    sys.stdout.flush()
    port = lib.BcdMidiCreatePort(PROBE_PORT_NAME, None, None,
                                 ctypes.byref(err), ctypes.byref(hr))
    text = lib.BcdMidiErrorText(err.value).decode("ascii", "replace")
    # The digits are never optional. See bcdmidi.h.
    print("returned  : %s" % (("0x%X" % port) if port else "NULL"))
    print("category  : %d  (%s)" % (err.value, text))
    print("HRESULT   : 0x%08X" % (hr.value & 0xFFFFFFFF))
    if not port:
        print("")
        print("VERDICT: the port was NOT created.")
        return False
    print("")
    print("VERDICT: PORT CREATED from inside the packaged process.")
    print("closing it now - this can take up to 30 seconds")
    sys.stdout.flush()
    lib.BcdMidiClosePort(port)
    print("closed.")
    return True


def main(argv):
    mode = "plain"
    create = False
    for a in argv[1:]:
        if a in ("plain", "adddir", "preload"):
            mode = a
        elif a == "--create":
            create = True
        else:
            print("usage: pyloadtest [plain|adddir|preload] [--create]")
            return 64

    stage0_environment()
    stage1_inventory()
    if not stage2_apply_mode(mode):
        return 3
    reachable = stage3_reach_microsoft_dll()
    lib = stage4_load_bcdmidi()
    if lib is None:
        banner("RESULT: BcdMidi.dll would not load  (exit 4)")
        return 4
    if not reachable:
        banner("RESULT: BcdMidi.dll loads, Microsoft's DLL is out of reach "
               "(exit 3)")
        print("Not calling BcdMidiCreatePort: the answer is already known and")
        print("a create attempt against a broken activation costs a reboot for")
        print("nothing. Rerun with a different mode.")
        return 3
    if not create:
        banner("RESULT: everything reachable, NO PORT CREATED  (exit 0)")
        print("Rerun with --create to prove the port itself. That is the step")
        print("that can cost a reboot.")
        return 0
    ok = stage5_create(lib)
    banner("RESULT: %s" % ("the gate is PASSED (exit 0)" if ok
                           else "creation failed (exit 5)"))
    return 0 if ok else 5


if __name__ == "__main__":
    sys.exit(main(sys.argv))
