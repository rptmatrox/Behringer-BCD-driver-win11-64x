// BCD3000Check.exe - reports what is installed and what is missing, and changes
// nothing at all.
//
// Two reasons this exists as its own program:
//
//   1. It is the safe thing to run first. It never writes a file, never touches
//      the registry, never opens the USB device and never asks for administrator
//      rights, so it can be run on a machine that is about to be used for a gig
//      or for a measurement without disturbing anything.
//
//   2. It is how the detection code gets exercised. The installer itself cannot
//      be test run without changing the machine, so all of the detection lives in
//      common.cpp and this program is the harness for it.
//
// --self-test additionally checks the internal helpers that have an answer we can
// state up front, and reports pass or fail per case. It writes one uniquely named
// file inside the temporary folder and deletes it again; that is the only write
// this program is capable of, and only with that switch.

#include "common.h"
#include "version.h"

#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

using namespace bcdsetup;

static int g_checks = 0;
static int g_bad    = 0;

static void expect(bool condition, const wchar_t* what)
{
    g_checks++;
    if (condition) {
        sayOk(L"%s", what);
    } else {
        g_bad++;
        sayFail(L"%s", what);
    }
}

// Exercises the pure helpers. No registry, no install locations, nothing that
// another program could be using.
static void selfTest()
{
    say(L"--- self test of the helpers ---");

    // joinPath: joins, and refuses instead of truncating.
    wchar_t buf[kPathMax];
    expect(joinPath(buf, kPathMax, L"C:\\a", L"b.txt") && wcscmp(buf, L"C:\\a\\b.txt") == 0,
           L"joinPath builds C:\\a\\b.txt");
    wchar_t small[8];
    expect(!joinPath(small, 8, L"C:\\long", L"name.txt"),
           L"joinPath refuses a buffer that would truncate");
    expect(!joinPath(buf, kPathMax, L"C:\\a", 0),
           L"joinPath refuses a null component");

    // The paths the product uses have to resolve on any Windows.
    expect(installDirPath(buf, kPathMax) && buf[0] != 0, L"the install folder resolves");
    say(L"       %s", buf);
    expect(asioDllPath(buf, kPathMax) && buf[0] != 0, L"the driver path resolves");
    say(L"       %s", buf);
    expect(bridgeExePath(buf, kPathMax) && buf[0] != 0, L"the control service path resolves");
    say(L"       %s", buf);
    expect(shortcutPath(buf, kPathMax) && buf[0] != 0, L"the startup shortcut path resolves");
    say(L"       %s", buf);
    expect(logFilePath(buf, kPathMax) && buf[0] != 0, L"the log path resolves");
    say(L"       %s", buf);

    // The install folder must not be the driver file, and neither must be empty:
    // a silent empty string here would make the installer write to the root.
    wchar_t dir[kPathMax], dll[kPathMax];
    bool bothResolved = installDirPath(dir, kPathMax) && asioDllPath(dll, kPathMax);
    expect(bothResolved && _wcsicmp(dir, dll) != 0 && wcslen(dir) > 3 && wcslen(dll) > 3,
           L"the install folder and the driver file are different, non trivial paths");

    // fileExists / dirExists on things every Windows has, and on something that
    // cannot exist.
    wchar_t sysDir[MAX_PATH];
    sysDir[0] = 0;
    GetSystemDirectoryW(sysDir, MAX_PATH);
    expect(sysDir[0] != 0 && dirExists(sysDir), L"dirExists recognises the system folder");
    expect(!fileExists(sysDir), L"fileExists says no to a folder");
    wchar_t kernel[kPathMax];
    expect(joinPath(kernel, kPathMax, sysDir, L"kernel32.dll") && fileExists(kernel),
           L"fileExists recognises kernel32.dll");
    expect(!fileExists(L"C:\\this-path-does-not-exist-4f1c9a2e\\nope.bin"),
           L"fileExists says no to a path that is not there");
    expect(!dirExists(L"C:\\this-path-does-not-exist-4f1c9a2e"),
           L"dirExists says no to a folder that is not there");

    // fileHasContent is what makes the installer idempotent, so it is worth
    // proving on a real file rather than reasoning about. The file is created in
    // the temporary folder with the process id in its name and deleted again.
    wchar_t tmpDir[kPathMax];
    tmpDir[0] = 0;
    GetTempPathW(kPathMax, tmpDir);
    wchar_t tmpFile[kPathMax];
    _snwprintf(tmpFile, kPathMax - 1, L"%sbcd3000-selftest-%lu.tmp", tmpDir,
               GetCurrentProcessId());
    tmpFile[kPathMax - 1] = 0;

    const char  payload[]  = "BCD3000 self test payload";
    const DWORD payloadLen = (DWORD)(sizeof(payload) - 1);
    DWORD err = 0;
    if (!writeFileAtomic(tmpFile, payload, payloadLen, &err)) {
        g_checks++;
        g_bad++;
        sayFail(L"writeFileAtomic could not write %s (%s)", tmpFile, winErrText(err));
    } else {
        expect(fileExists(tmpFile), L"writeFileAtomic produced the file");
        expect(fileHasContent(tmpFile, payload, payloadLen),
               L"fileHasContent says yes to identical bytes");
        expect(!fileHasContent(tmpFile, payload, payloadLen - 1),
               L"fileHasContent says no when the length differs");
        const char other[] = "BCD3000 self test payloaD";   // last byte differs
        expect(!fileHasContent(tmpFile, other, payloadLen),
               L"fileHasContent says no when one byte differs");
        // And it overwrites in place, which is what a second install run does.
        const char again[] = "second";
        expect(writeFileAtomic(tmpFile, again, (DWORD)(sizeof(again) - 1), &err) &&
               fileHasContent(tmpFile, again, (DWORD)(sizeof(again) - 1)),
               L"writeFileAtomic replaces an existing file");
        expect(DeleteFileW(tmpFile) && !fileExists(tmpFile),
               L"the temporary file was removed again");
    }

    expect(!fileHasContent(L"C:\\this-path-does-not-exist-4f1c9a2e\\nope.bin", payload,
                           payloadLen),
           L"fileHasContent says no for a file that is not there");

    // ensureDir on a folder that is already there must succeed without creating
    // anything, which is the case every second install run hits.
    err = 0;
    expect(sysDir[0] != 0 && ensureDir(sysDir, &err) && err == 0,
           L"ensureDir succeeds on a folder that already exists");

    // The constants that are contracts with code outside this folder. They are
    // compared as text, because a mismatch here does not break any build - it
    // makes the installer report on the wrong thing.
    expect(_wcsicmp(kAsioClsid, L"{B0D3000A-51E7-4C2B-9F3A-1234ABCD5678}") == 0,
           L"the class id still matches IID_ASIO_DRIVER in native/bcdasio/bcdasio.cpp");
    expect(wcscmp(kAsioRegName, L"Behringer BCD3000") == 0,
           L"the ASIO registry name still matches the one the driver registers");
    expect(wcscmp(kUsbEnumKey,
                  L"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_1397&PID_00BF&MI_00") == 0,
           L"the device enumeration key still matches kEnumKey in usbdev.cpp");
    expect(wcscmp(kBridgeExeFileName, L"BCD3000Bridge.exe") == 0,
           L"the control service file name is unchanged");
    expect(wcscmp(kShortcutFileName, L"BCD3000 Bridge.lnk") == 0,
           L"the startup shortcut name is unchanged");

    // winErrText has to produce something for a code with no message as well as
    // for a common one.
    expect(wcsstr(winErrText(ERROR_FILE_NOT_FOUND), L"error 2") != 0,
           L"winErrText includes the numeric code");
    expect(winErrText(0xDEADBEEF)[0] != 0,
           L"winErrText produces text for a code with no system message");

    sayBlank();
    say(L"self test: %d checks, %d failed", g_checks, g_bad);
    sayBlank();
}

int main()
{
    int       argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool wantSelfTest = false;
    bool wantHelp     = false;
    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (_wcsicmp(argv[i], L"--self-test") == 0)
                wantSelfTest = true;
            else
                wantHelp = true;
        }
        LocalFree(argv);
    }

    say(L"%s - machine check %s", kProductName, BCD_VERSION_WSTR);
    say(L"This program only reads. It changes nothing.");
    sayBlank();

    if (wantHelp) {
        say(L"Usage: BCD3000Check.exe [--self-test]");
        say(L"  no arguments  report the machine state");
        say(L"  --self-test   additionally check the installer's own helpers");
        pauseIfWeOwnTheConsole();
        return 0;
    }

    if (wantSelfTest)
        selfTest();

    MachineState state;
    gatherMachineState(&state);
    reportMachineState(&state);

    say(L"=========================== VERDICT ===========================");
    bool ready = true;
    if (state.tevm.state != kTeVmPresent) {
        say(L"  teVirtualMIDI is missing      -> controls and LEDs will not work");
        ready = false;
    }
    if (!state.usb.enumKeyPresent || !state.usb.guidPresent) {
        say(L"  the WinUSB binding is missing -> nothing will work until Zadig is run");
        ready = false;
    }
    if (!state.asio.clsidKeyPresent || !state.asio.asioNameKeyPresent) {
        say(L"  the driver is not registered  -> run BCD3000Setup.exe");
        ready = false;
    }
    if (!state.bridge.running) {
        say(L"  the control service is not running -> controls and LEDs are dead");
        ready = false;
    }
    if (state.registeredElsewhere) {
        // Not a fault by itself - it is the normal state of a development machine -
        // but it decides which binary a host application actually loads, so it
        // does not belong only in the detail above.
        say(L"  the registered driver is NOT the copy in the install folder:");
        say(L"      %s", state.asio.inprocPath);
        ready = false;
    }
    if (state.usb.guidPresent && !state.usb.interfacePresentNow)
        say(L"  the BCD3000 is not connected right now, so nothing about the device "
            L"itself could be confirmed.");
    if (ready)
        say(L"  everything this program can see is in place.");
    say(L"===============================================================");

    pauseIfWeOwnTheConsole();
    return (g_bad > 0) ? 1 : 0;
}
