// BCD3000Setup.exe - installs the open source replacement driver for the
// Behringer BCD3000 on Windows 11.
//
// WHAT IT INSTALLS
//   1. BcdAsio.dll        the ASIO driver, copied to %ProgramFiles% and
//                         registered as a COM server (needs elevation)
//   2. BCD3000Bridge.exe  the control and LED service, copied into the user
//                         profile and started at sign in from a Startup shortcut
//
// WHAT IT ONLY CHECKS AND EXPLAINS, because it is not ours to install
//   3. teVirtualMIDI      third party, ships with loopMIDI
//   4. the WinUSB binding on the device, which is done once with Zadig
//
// WHY THE CONTROL SERVICE IS NOT ELEVATED, and why that is not an oversight:
// the service is the SERVER of the local channel that carries the controls and
// the LEDs, and the driver - which runs inside the DJ application, often
// elevated - is the CLIENT. An elevated process opens an object created by a
// normal one; the other direction is what usually gets refused. Starting the
// service from a Startup shortcut is what keeps it unelevated. Replacing that
// with a scheduled task that runs with highest privileges would silently break
// the controls on machines where the DJ application is elevated.
//
// WHAT IT DELIBERATELY DOES NOT DO
//   - it never rebinds the USB device. A wrong binding leaves the hardware
//     unusable, and this program has no way to verify a rebind before the user
//     finds out the hard way. It detects and instructs instead.
//   - it never turns off a Windows protection, and it never asks the user to.
//     The whole reason this project exists is that the 2010 driver needs one
//     turned off; suggesting the same thing would defeat the point.
//   - it does not stop a running control service unless explicitly told to,
//     because stopping it destroys the virtual MIDI port.

#include "common.h"
#include "gui.h"
#include "version.h"

#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

using namespace bcdsetup;

// Exit codes. Distinct on purpose: a caller has to be able to tell "finished,
// nothing left to do" from "finished, but the user still has work".
enum ExitCode {
    kExitOk           = 0,
    kExitFailed       = 1,
    kExitBadArguments = 2,
    kExitPending      = 3,
    kExitAborted      = 4     // stopped by the user; nothing was changed
};

struct Options {
    bool help;
    bool checkOnly;
    bool replaceBridge;
    bool assumeYes;
    bool console;      // /console: the old text output, and no window at all
    bool preview;      // /preview: the window, and no way to reach the steps
};

// The steps, as they appear on the progress page. The numbers are indices into
// Wizard::steps and nothing else depends on them.
enum StepIndex {
    kStepRecord  = 0,
    kStepDriver  = 1,
    kStepService = 2,
    kStepCount   = 3
};

// Things the user still has to do when we are done. Collected as we go and
// printed together at the end, because a message that scrolled past ten steps
// ago has not been read.
struct Pending {
    bool teVirtualMidiMissing;
    bool winUsbBindingMissing;
    bool deviceNotConnected;
    bool startControlService;
    bool controlServiceNotReplaced;
    bool wrongAccount;

    // *** WHY "RESTART YOUR DJ SOFTWARE" HAS THREE FLAGS AND NOT ONE ***
    //
    // It used to have one, and the sentence it printed said the virtual MIDI port
    // had been recreated during the install. That was measured to be FALSE on the
    // first real run of this installer: the control service kept the same process
    // ids across it, its own log got no new line, and its "port created" counter
    // stayed at one. The ADVICE was right; the REASON was invented.
    //
    // It matters twice, and the second time is the one that decides. A wrong reason
    // teaches a wrong model of the machine - and if it HAD been true it would have
    // meant the port was destroyed by an install, which is precisely the defect the
    // control service was redesigned to stop producing. A claim about the port is
    // therefore only allowed to appear when this run actually stopped the service.
    //
    // So each reason is now recorded at the one place where it is MEASURED, and the
    // summary prints only the reasons that happened.
    bool midiPortDestroyed;      // this run stopped the service, so the port went too
    bool driverFileReplaced;     // the DLL a host had loaded is a different file now
    bool registrationRepointed;  // the class id points at a different copy now
};

// True when something this run did changes what an already open DJ application
// would have to load again. One place, so the summary and the exit code cannot
// disagree about it.
static bool needsDjRestart(const Pending* p)
{
    return p->midiPortDestroyed || p->driverFileReplaced || p->registrationRepointed;
}

static void printBanner()
{
    say(L"%s - installer %s", kProductName, BCD_VERSION_WSTR);
    say(L"An open source ASIO driver over WinUSB, plus the control and LED service.");
    // The same credit and the same notice the window shows on its first page, from
    // the same strings, so that a console run and a window run make the same
    // statement and neither can be changed without the other.
    say(L"%s", bcdgui::kCreditsLine);
    say(L"%s", bcdgui::kRepositoryUrl);
    say(L"%s", bcdgui::kNotAffiliatedLine1);
    say(L"%s", bcdgui::kNotAffiliatedLine2);
    sayBlank();
}

// What this particular installer is carrying. Printed because "the installer ran"
// and "the installer contained the build you meant" are two different statements,
// and only the sizes can tell them apart after the fact.
static void printPayloadReport()
{
    say(L"--- what is inside this installer ---");
    struct Item { int id; const wchar_t* name; };
    static const Item items[3] = {
        { IDR_PAYLOAD_ASIO_DLL,    L"BcdAsio.dll (the ASIO driver)" },
        { IDR_PAYLOAD_BRIDGE_EXE,  L"BCD3000Bridge.exe (controls and LEDs)" },
        { IDR_PAYLOAD_UNINSTALLER, L"BCD3000Uninstall.exe" }
    };
    for (int i = 0; i < 3; i++) {
        const void* data = 0;
        DWORD       size = 0;
        if (loadPayload(items[i].id, &data, &size))
            sayInfo(L"%s: %lu bytes", items[i].name, size);
        else
            sayFail(L"%s: MISSING from this build", items[i].name);
    }
    sayBlank();
}

static void printHelp()
{
    say(L"Usage: BCD3000Setup.exe [options]");
    sayBlank();
    say(L"  (no options)      open the installer window.");
    say(L"  /console          do everything in this console instead of in a window.");
    say(L"                    Identical decisions, identical text; this is the mode an");
    say(L"                    automated run reads back to check what happened.");
    say(L"  /preview          open the window, report the machine, and DISABLE the");
    say(L"                    install button. Nothing is written - not even the log");
    say(L"                    file - and nothing is registered. For looking at the");
    say(L"                    window on a machine that must not be touched.");
    say(L"  /check            report the machine state and change nothing. Always in");
    say(L"                    the console, never in a window.");
    say(L"                    BCD3000Check.exe does the same without asking for");
    say(L"                    administrator rights, so prefer that one.");
    say(L"  /replace-service  allow replacing BCD3000Bridge.exe while it is running.");
    say(L"                    This stops it, which destroys the virtual MIDI port, so");
    say(L"                    any open DJ application has to be restarted afterwards.");
    say(L"  /yes              do not ask for confirmation. Every question still gets");
    say(L"                    logged with the answer that was assumed.");
    say(L"  /help             this text.");
    sayBlank();
    say(L"Exit codes: 0 done, 1 a step failed, 2 bad arguments, 3 done but something is");
    say(L"still pending on your side, 4 stopped at your request with nothing changed.");
    say(L"The same codes in both modes: closing the window before pressing the install");
    say(L"button is the same answer as declining at the console prompt.");
}

static bool parseArgs(int argc, wchar_t** argv, Options* opt)
{
    ZeroMemory(opt, sizeof(*opt));
    for (int i = 1; i < argc; i++) {
        const wchar_t* a = argv[i];
        if (_wcsicmp(a, L"/help") == 0 || _wcsicmp(a, L"-h") == 0 ||
            _wcsicmp(a, L"--help") == 0 || _wcsicmp(a, L"/?") == 0)
            opt->help = true;
        else if (_wcsicmp(a, L"/check") == 0)
            opt->checkOnly = true;
        else if (_wcsicmp(a, L"/console") == 0)
            opt->console = true;
        else if (_wcsicmp(a, L"/preview") == 0)
            opt->preview = true;
        else if (_wcsicmp(a, L"/replace-service") == 0)
            opt->replaceBridge = true;
        else if (_wcsicmp(a, L"/yes") == 0)
            opt->assumeYes = true;
        else {
            sayFail(L"unknown option: %s", a);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Step 1: the driver
// ---------------------------------------------------------------------------
static bool installDriver(const MachineState* s, bool* wroteFile)
{
    say(L"--- installing the ASIO driver ---");

    const void* data = 0;
    DWORD       size = 0;
    if (!loadPayload(IDR_PAYLOAD_ASIO_DLL, &data, &size)) {
        sayFail(L"this installer has no driver payload in it - the build is broken");
        return false;
    }

    DWORD err = 0;
    if (!ensureDir(s->installDir, &err)) {
        sayFail(L"could not create %s (%s)", s->installDir, winErrText(err));
        return false;
    }
    sayOk(L"install folder: %s", s->installDir);

    if (fileHasContent(s->dllTarget, data, size)) {
        sayOk(L"driver file is already identical (%lu bytes) - not rewritten", size);
    } else {
        // THE PAYLOAD IS INSPECTED BEFORE IT IS ALLOWED TO REPLACE ANYTHING.
        //
        // On a reinstall the file about to be overwritten is the one every host
        // application loads. Writing first and loading afterwards means a corrupt
        // or wrong architecture payload destroys a working driver and only then
        // reports a failure, which leaves the machine worse than it was found.
        //
        // So: write next to the target, check the file, keep a copy of what is
        // being replaced, and only then let it take the target's place.
        if (wcslen(s->dllTarget) + 8 >= kPathMax) {
            sayFail(L"the driver path is too long to stage a new file next to it");
            return false;
        }
        wchar_t staging[kPathMax];
        wchar_t backup[kPathMax];
        _snwprintf(staging, kPathMax - 1, L"%s.new", s->dllTarget);
        staging[kPathMax - 1] = 0;
        _snwprintf(backup, kPathMax - 1, L"%s.bak", s->dllTarget);
        backup[kPathMax - 1] = 0;

        if (!writeFileDirect(staging, data, size, &err)) {
            sayFail(L"could not write %s (%s)", staging, winErrText(err));
            sayInfo(L"nothing was replaced: %s is untouched", s->dllTarget);
            return false;
        }

        const wchar_t* whyNot  = 0;
        DWORD          loadErr = 0;
        if (!checkDriverFile(staging, &whyNot, &loadErr)) {
            DeleteFileW(staging);
            sayFail(L"the driver payload in this installer is not usable: %s", whyNot);
            if (loadErr)
                sayInfo(L"Windows said: %s", winErrText(loadErr));
            sayInfo(L"this is a broken build of the installer, not a broken machine. "
                    L"NOTHING was replaced: %s is untouched.", s->dllTarget);
            return false;
        }
        sayOk(L"payload checked before use: 64 bit DLL, loads, and exports "
              L"DllRegisterServer");

        // The copy is taken with CopyFile and not with a rename, so that the file
        // a running host application has loaded stays exactly where it is. If the
        // replacement below is refused because of that, the machine is unchanged.
        if (fileExists(s->dllTarget)) {
            if (CopyFileW(s->dllTarget, backup, FALSE))
                sayOk(L"the driver being replaced was kept as %s", backup);
            else
                sayWarn(L"could not keep a copy of the driver being replaced (%s) - "
                        L"continuing, the payload has already been checked",
                        winErrText(GetLastError()));
        }

        // MOVEFILE_REPLACE_EXISTING fails when the target is open by another
        // process, which is what we want: better to stop and say so than to half
        // replace a driver a host application is using.
        if (!MoveFileExW(staging, s->dllTarget, MOVEFILE_REPLACE_EXISTING)) {
            err = GetLastError();
            DeleteFileW(staging);
            sayFail(L"could not put the new driver in place at %s (%s)",
                    s->dllTarget, winErrText(err));
            if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED ||
                err == ERROR_USER_MAPPED_FILE) {
                sayInfo(L"a program still has the driver loaded. Close your DJ software "
                        L"and run this installer again.");
            }
            sayInfo(L"the driver that was already installed is untouched.");
            return false;
        }
        *wroteFile = true;
        sayOk(L"driver written: %s (%lu bytes)", s->dllTarget, size);
    }

    HRESULT hr    = 0;
    DWORD   winErr = 0;
    if (!callDllRegisterServer(s->dllTarget, &hr, &winErr)) {
        if (winErr)
            sayFail(L"could not load or call the driver for registration (%s)",
                    winErrText(winErr));
        else
            sayFail(L"the driver refused to register (DllRegisterServer returned 0x%08lX)",
                    (unsigned long)hr);
        sayInfo(L"the driver shows a message box of its own when this happens; the text "
                L"in it is the driver's own error code");
        return false;
    }
    sayOk(L"registration call succeeded");

    // Read the registry back instead of trusting the return value. This is the
    // one step whose effect is invisible in the filesystem, so it is the one
    // worth proving.
    AsioRegInfo after;
    detectAsioRegistration(&after);
    if (!after.inprocPresent || _wcsicmp(after.inprocPath, s->dllTarget) != 0) {
        sayFail(L"registration did not take: the class id now points at \"%s\" instead "
                L"of \"%s\"", after.inprocPath, s->dllTarget);
        return false;
    }
    if (!after.asioNameKeyPresent || !after.clsidMatches) {
        sayFail(L"HKLM\\SOFTWARE\\ASIO\\%s is missing or points elsewhere - host "
                L"applications would not list the driver", kAsioRegName);
        return false;
    }
    sayOk(L"verified in the registry: host applications will load %s", after.inprocPath);
    if (after.perUserShadowPresent)
        sayWarn(L"a per user registration still exists at HKCU\\Software\\Classes\\CLSID\\%s "
                L"and overrides the machine one for this user", kAsioClsid);
    return true;
}

// ---------------------------------------------------------------------------
// Step 2: the control service and its Startup shortcut
// ---------------------------------------------------------------------------
static bool installControlService(const MachineState* s, const Options* opt,
                                  Pending* pending)
{
    say(L"--- installing the control and LED service ---");

    // THIS HALF OF THE INSTALL IS REFUSED WHEN THE ACCOUNTS DIFFER.
    //
    // Everything below writes into a user profile and into a user's Startup
    // folder. When Windows asked for an administrator's password instead of a
    // plain "Yes", this process runs as that administrator while the desktop
    // belongs to somebody else, and there is no good answer:
    //   - writing into OUR profile installs the service for an account that
    //     never signs in, which is what used to happen;
    //   - writing into THEIR profile means an elevated process writing into a
    //     tree that account can modify, which is a way to aim an elevated write
    //     somewhere else entirely by replacing a folder with a junction.
    // The driver half is machine wide and is already done at this point, so the
    // install is not wasted: re-running from the right account finishes it.
    if (s->account.checked && !s->account.matched) {
        sayWarn(L"refusing to install the control service and its startup shortcut.");
        sayInfo(L"this installer is running as %s, but the desktop belongs to %s.",
                s->account.tokenAccount, s->account.shellAccount);
        sayInfo(L"the service and its shortcut live in a user profile, so they have to "
                L"be installed by the account that will actually use the BCD3000.");
        sayInfo(L"the ASIO driver is machine wide and IS installed. Sign in as %s and "
                L"run this installer again there - it will skip everything that is "
                L"already done.", s->account.shellAccount);
        pending->wrongAccount = true;
        return true;
    }
    if (!s->account.checked) {
        // No explorer.exe to ask, so the per user folders fell back to this
        // process's own profile. That is right for a normal elevation and wrong
        // for an administrator elevating into somebody else's session, and there
        // is no way to tell which one this is. Saying so beats being silent.
        sayWarn(L"could not confirm which account owns the desktop, so the folders "
                L"below are this process's own profile.");
        sayInfo(L"if the service does not start at the next sign in, check that it "
                L"landed in the profile of the account that signs in.");
    }

    const void* data = 0;
    DWORD       size = 0;
    if (!loadPayload(IDR_PAYLOAD_BRIDGE_EXE, &data, &size)) {
        sayFail(L"this installer has no control service payload in it - the build is broken");
        return false;
    }

    wchar_t dir[kPathMax];
    if (!bridgeDirPath(dir, kPathMax)) {
        sayFail(L"could not resolve the local application data folder");
        return false;
    }
    DWORD err = 0;
    if (!ensureDir(dir, &err)) {
        sayFail(L"could not create %s (%s)", dir, winErrText(err));
        return false;
    }
    sayOk(L"service folder: %s", dir);

    if (fileHasContent(s->bridgeTarget, data, size)) {
        sayOk(L"control service is already identical (%lu bytes) - not rewritten", size);
        if (!s->bridge.running)
            pending->startControlService = true;
    } else if (s->bridge.running && !opt->replaceBridge) {
        // Refusing is the safe answer, not the lazy one. Replacing the file means
        // stopping the process, and stopping the process destroys the virtual MIDI
        // port that an open DJ application is bound to.
        sayWarn(L"the installed control service differs from the one in this installer, "
                L"but it is RUNNING and was left untouched");
        sayInfo(L"stopping it destroys the virtual MIDI port, and any DJ application that "
                L"is open would stop seeing the controller until it is restarted");
        sayInfo(L"to replace it anyway, close your DJ software and run:");
        sayInfo(L"    BCD3000Setup.exe /replace-service");
        pending->controlServiceNotReplaced = true;
    } else {
        if (s->bridge.running) {
            sayInfo(L"stopping the running control service, as /replace-service asked");
            int   killed = 0;
            DWORD stopErr = 0;
            if (!stopBridge(&killed, &stopErr)) {
                sayFail(L"could not stop the running control service (%s)",
                        winErrText(stopErr));
                return false;
            }
            sayOk(L"stopped %d process%s", killed, killed == 1 ? L"" : L"es");
            // Recorded HERE, at the only place in this program that stops the
            // service, because this is the only place that knows the virtual MIDI
            // port went away. Nothing downstream is allowed to guess it.
            sayInfo(L"the virtual MIDI port went with it. It exists again once the "
                    L"control service is running, and a DJ application that was open "
                    L"has to be restarted to find it.");
            pending->midiPortDestroyed = true;
        }
        if (!writeFileAtomic(s->bridgeTarget, data, size, &err)) {
            sayFail(L"could not write %s (%s)", s->bridgeTarget, winErrText(err));
            if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED)
                sayInfo(L"the file is in use. Run with /replace-service, or sign out and "
                        L"in again and run the installer before starting anything else.");
            return false;
        }
        sayOk(L"control service written: %s (%lu bytes)", s->bridgeTarget, size);
        pending->startControlService = true;
    }

    // The Startup shortcut. This is what makes the service run at sign in AND
    // makes it run unelevated, which the local channel depends on.
    wchar_t lnk[kPathMax];
    if (!shortcutPath(lnk, kPathMax)) {
        sayFail(L"could not resolve the Startup folder");
        return false;
    }
    wchar_t existing[kPathMax];
    existing[0] = 0;
    bool haveExisting = fileExists(lnk) && readShortcutTarget(lnk, existing, kPathMax);

    if (haveExisting && _wcsicmp(existing, s->bridgeTarget) == 0) {
        sayOk(L"startup shortcut already correct: %s", lnk);
    } else {
        if (haveExisting)
            sayInfo(L"the existing startup shortcut points at %s - replacing it", existing);
        HRESULT hr = 0;
        if (!createShortcut(lnk, s->bridgeTarget, dir,
                            L"BCD3000 controls and LEDs (starts unelevated on purpose)",
                            &hr)) {
            sayFail(L"could not create the startup shortcut %s (0x%08lX)",
                    lnk, (unsigned long)hr);
            return false;
        }
        // Prove it, the same way the registration is proved.
        wchar_t check[kPathMax];
        check[0] = 0;
        if (!readShortcutTarget(lnk, check, kPathMax) ||
            _wcsicmp(check, s->bridgeTarget) != 0) {
            sayFail(L"the startup shortcut was written but does not point at %s",
                    s->bridgeTarget);
            return false;
        }
        sayOk(L"startup shortcut written: %s", lnk);
    }
    return true;
}

// ---------------------------------------------------------------------------
// The uninstaller and the manifest.
//
// These two go FIRST, before anything is registered or copied. If a later step
// fails, the machine is left half installed - and a half installed machine is
// exactly when you need a way to undo it and a record of what the registration
// used to point at.
// ---------------------------------------------------------------------------
static bool installUninstaller(const MachineState* s)
{
    say(L"--- installing the uninstaller first, so a failure is still undoable ---");

    const void* data = 0;
    DWORD       size = 0;
    if (!loadPayload(IDR_PAYLOAD_UNINSTALLER, &data, &size)) {
        sayFail(L"this installer has no uninstaller payload in it - the build is broken");
        return false;
    }
    DWORD err = 0;
    if (!ensureDir(s->installDir, &err)) {
        sayFail(L"could not create %s (%s)", s->installDir, winErrText(err));
        return false;
    }
    wchar_t path[kPathMax];
    if (!uninstallExePath(path, kPathMax)) {
        sayFail(L"could not resolve the install folder");
        return false;
    }
    if (fileHasContent(path, data, size)) {
        sayOk(L"uninstaller is already identical - not rewritten");
        return true;
    }
    if (!writeFileAtomic(path, data, size, &err)) {
        sayFail(L"could not write %s (%s)", path, winErrText(err));
        return false;
    }
    sayOk(L"uninstaller written: %s", path);
    return true;
}

// The ASIO registration that was in place before this product was installed.
//
// It is the only value in the manifest that cannot be worked out again later, and
// the only instruction that puts the machine back the way it was found.
//
// *** A SECOND RUN HAS NOTHING OF ITS OWN TO RECORD *** By then the registration
// already points at the install folder, so registeredElsewhere is false and this
// run's answer is empty. Writing that emptiness over what the first run recorded
// is how the machine ends up with no ASIO driver and no way back: the uninstaller
// reads "(none)", concludes there is nothing to restore, and says nothing. So a
// recorded value is read back and kept. A value that is present is NEVER replaced
// by an absent one.
static void resolvePreviousRegistration(const MachineState* s, wchar_t* out, DWORD count)
{
    out[0] = 0;
    if (s && s->registeredElsewhere && s->asio.inprocPath[0]) {
        wcsncpy(out, s->asio.inprocPath, count - 1);
        out[count - 1] = 0;
        return;
    }
    readRecordedPreviousRegistration(out, count);
}

static bool writeManifest(const MachineState* s, const wchar_t* previousRegistration)
{
    wchar_t path[kPathMax];
    if (!manifestPath(path, kPathMax))
        return false;

    // Belt and braces for the rule above, at the one place that does the writing:
    // whatever the caller worked out, an empty value never overwrites a recorded
    // one. The read is of a file a few hundred bytes long.
    wchar_t previous[kPathMax];
    previous[0] = 0;
    if (previousRegistration && previousRegistration[0]) {
        wcsncpy(previous, previousRegistration, kPathMax - 1);
        previous[kPathMax - 1] = 0;
    } else {
        readRecordedPreviousRegistration(previous, kPathMax);
    }

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t lnk[kPathMax];
    lnk[0] = 0;
    shortcutPath(lnk, kPathMax);

    wchar_t text[8192];
    _snwprintf(text, 8000,
        L"# %s - install manifest\r\n"
        L"#\r\n"
        L"# Written by BCD3000Setup.exe %s. Read by BCD3000Uninstall.exe, which uses it\r\n"
        L"# for information only: the uninstaller works out every path it deletes on its\r\n"
        L"# own, so editing this file cannot make it delete something else.\r\n"
        L"#\r\n"
        L"# It is written BEFORE the install steps run, on purpose. If a step fails, the\r\n"
        L"# paths below are what the installer was about to use, and\r\n"
        L"# previous_asio_registration is the value needed to put the machine back.\r\n"
        L"#\r\n"
        L"# previous_asio_registration is carried over from an earlier run of this\r\n"
        L"# installer when this run has nothing of its own to record. A second install\r\n"
        L"# sees the registration already pointing here, and overwriting the line with\r\n"
        L"# \"(none)\" would throw away the only instruction for undoing the first one.\r\n"
        L"#\r\n"
        L"installer_version=%s\r\n"
        L"installed_at=%04u-%02u-%02uT%02u:%02u:%02u\r\n"
        L"installed_by=%s\r\n"
        L"asio_clsid=%s\r\n"
        L"asio_registry_name=%s\r\n"
        L"asio_dll=%s\r\n"
        L"control_service=%s\r\n"
        L"startup_shortcut=%s\r\n"
        L"previous_asio_registration=%s\r\n"
        L"\r\n"
        L"# Not installed by this product and never removed by its uninstaller:\r\n"
        L"#   teVirtualMIDI (third party, ships with loopMIDI)\r\n"
        L"#   the WinUSB binding on the device (done once by hand with Zadig)\r\n",
        kProductName, BCD_VERSION_WSTR, BCD_VERSION_WSTR,
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        s->account.tokenAccount[0] ? s->account.tokenAccount : L"(unknown)",
        kAsioClsid, kAsioRegName,
        s->dllTarget, s->bridgeTarget, lnk,
        previous[0] ? previous : manifestNoPreviousText());
    text[8000] = 0;

    DWORD err = 0;
    if (!writeTextFileUtf8(path, text, &err)) {
        sayWarn(L"could not write the install manifest %s (%s) - the install itself is "
                L"unaffected", path, winErrText(err));
        return false;
    }
    sayOk(L"install manifest written: %s", path);
    return true;
}

// ---------------------------------------------------------------------------
// NEXT STEPS AND WARNINGS
//
// WHY THIS IS TEXT AND NOT A PAGE OF ITS OWN. It is said with say*(), like every
// other word this program produces, which puts it in four places at once: the
// window's summary pane, the console, the log file, and the clipboard. That is not
// a convenience - it is the only arrangement in which a warning cannot be lost.
// A painted block would exist for as long as the window is open and then be gone
// for ever; it would be invisible to a screen reader (see the header of gui.cpp);
// it would not be in /console mode, which is the mode an automated run reads back;
// and painted text is the only text in this program that can be clipped by a
// window that is too small. The pane, by contrast, is an EDIT control with
// ES_MULTILINE and no ES_AUTOHSCROLL, so it WRAPS, and with WS_VSCROLL, so the
// overflow is reachable. Nothing here can be silently cut at any DPI.
// What the window adds is one painted line pointing at this block, so that the
// first item does not depend on somebody scrolling - see Wizard::doneNotice.
//
// WHY EVERY ITEM IS HERE. Each one is something this project MEASURED, not general
// advice, and each one has a way of going wrong that costs the user their working
// installation or an evening of searching. Item 1 is first and is the loudest
// because it is the only one that can destroy an installation that already works,
// and because the user meets it BEFORE they would ever read a README: the DJ
// software puts it on the screen, phrased as an instruction, the first time the
// mixer is selected.
//
// ITEM 8 IS A CLAIM OF ABSENCE, which is the kind that needs its scope written into
// the sentence. It says nothing THIS INSTALLER does needs a restart - not that
// nothing in the whole process ever will - because items 6 and 7 point at two third
// party installers that legitimately may ask for one, and a warnings block that
// contradicts itself teaches the reader to stop believing all of it. It is also the
// one item whose reason is structural rather than anecdotal: there is no kernel
// component and nothing loads at boot, so there is nothing a restart could change.
//
// THE COUNT IN THE FIRST LINE IS PART OF THE TEXT, and it goes stale the moment an
// item is added. Adding item 8 made this line and the "1/7" labels in
// installer/verify obsolete in the same edit - and those labels would have gone on
// passing green while describing something false, which is worse than a failure. So
// the harness now checks the new count AND checks that the old one is GONE, in the
// text and inside the built binary.
//
// It is printed whether the install succeeded or failed. A failed install is if
// anything the moment somebody is most likely to start clicking things that offer
// to fix drivers.
// ---------------------------------------------------------------------------
static void printNextStepsAndWarnings()
{
    sayBlank();
    say(L"=================== NEXT STEPS AND WARNINGS ===================");
    say(L"Eight things worth knowing. The first one can undo everything above.");
    sayBlank();
    say(L"  ***********************************************************");
    say(L"  ***                                                     ***");
    say(L"  ***    1. DO NOT LET YOUR DJ SOFTWARE INSTALL A         ***");
    say(L"  ***       DRIVER FOR THIS MIXER.                        ***");
    say(L"  ***       IT UNDOES EVERYTHING ABOVE.                   ***");
    say(L"  ***                                                     ***");
    say(L"  ***********************************************************");
    say(L"     VirtualDJ recognises the BCD3000 - it shows the mixer, the mapping");
    say(L"     editor and every control - and STILL puts a band in its CONTROLLER");
    say(L"     tab saying that drivers have to be installed first, with a button");
    say(L"     beside it offering to download them. In Portuguese the band reads:");
    say(L"");
    // THE QUOTATION IS EXACT, AND THE ACCENTED LETTER IS AN ESCAPE ON PURPOSE.
    //
    // U+00EA is LATIN SMALL LETTER E WITH CIRCUMFLEX. Written as the raw two UTF-8
    // bytes it would be the only non ASCII character in this folder, and MSVC reads
    // a source file with no byte order mark as the machine's ANSI code page - so
    // those two bytes would compile, without a warning, into the two wrong
    // characters, and the quotation this item exists to make matchable would be
    // mojibake. The escape cannot be misread by any compiler on any machine.
    //
    // The destinations are all Unicode: WriteConsoleW to a console, UTF-8 to a
    // redirected stream and to the log file, DrawTextW and an EDIT control in the
    // window. The English gloss on the next line, and the sentence above, carry the
    // meaning on their own - so this item still works for somebody whose DJ
    // software is in another language, or whose terminal mangles the one character.
    say(L"         \"Voc\u00EA precisa instalar alguns drivers primeiro\"");
    say(L"         (\"you need to install some drivers first\")");
    say(L"");
    say(L"     The button beside it is labelled \"Download drivers\", or \"Baixar");
    say(L"     drivers\" in Portuguese. DO NOT PRESS IT.");
    say(L"");
    say(L"     THAT BAND IS WRONG HERE. It is a fixed property of VirtualDJ's own");
    say(L"     controller definition, written when the manufacturer's package was");
    say(L"     the only way to use this mixer. It is not looking at your machine,");
    say(L"     and nothing on your machine is missing.");
    say(L"     Pressing the button installs the manufacturer's 2010 INF files for");
    say(L"     USB\\VID_1397&PID_00BF. Windows matches those over the WinUSB binding");
    say(L"     this driver needs, and then two things die at the same moment: the");
    say(L"     old kernel driver refuses to load - code 39, its signature expired");
    say(L"     years ago - and the mixer is no longer bound to WinUSB, so there is");
    say(L"     no audio AND no controls. The way back out is to run Zadig again,");
    say(L"     which is the most dangerous step in this whole installation.");
    sayBlank();
    say(L"  2. Open your DJ software again after this installer has run.");
    say(L"     An application that was already running keeps using the driver file");
    say(L"     it loaded and the registration it read when it started. That is the");
    say(L"     whole reason, and it is the only one: installing does NOT recreate");
    say(L"     the virtual MIDI port. See 3.");
    sayBlank();
    say(L"  3. Do not stop, end or \"restart\" the control service as a routine step.");
    say(L"     BCD3000Bridge.exe owns the virtual MIDI port for as long as it runs.");
    say(L"     Ending it destroys the port, and every DJ application that had the");
    say(L"     controller open then has to be restarted before it sees a new one.");
    say(L"     That is a declared limit of how the port is created, not an");
    say(L"     oversight. Leave it running: it starts by itself at every sign in,");
    say(L"     and it releases the mixer to the driver and takes it back on its own.");
    sayBlank();
    say(L"  4. Plug the mixer into a USB 2.0 port.");
    say(L"     It is a USB 1.1 full speed device and its audio is isochronous, which");
    say(L"     some USB 3.x controllers do not carry reliably. That is the hardware;");
    say(L"     there is no software fix for it.");
    sayBlank();
    say(L"  5. If the mixer \"disappears\": CHECK THE CABLE FIRST.");
    say(L"     A plug that was not seated cost an hour of investigation on the");
    say(L"     machine this was built on, after two confident wrong theories. Push");
    say(L"     the cable home at both ends before suspecting anything else.");
    say(L"     Second suspect: a virtual machine. VMware's USB arbitrator took this");
    say(L"     mixer twice in one day, and from outside there is no way to tell");
    say(L"     \"another program has it\" from \"it is unplugged\".");
    sayBlank();
    say(L"  6. teVirtualMIDI is somebody else's software.");
    say(L"     The control service loads it while it runs, to create the MIDI port.");
    say(L"     It is NOT part of this project and is NOT redistributed here; it");
    say(L"     comes with loopMIDI. Audio works without it; the knobs, the buttons");
    say(L"     and the LEDs do not.");
    sayBlank();
    say(L"  7. The WinUSB binding is a requirement, and redoing it is the most");
    say(L"     dangerous operation in this process.");
    say(L"     It is how the driver reaches the hardware, it is done once by hand");
    say(L"     with Zadig, and a wrong binding leaves the mixer unusable. If");
    say(L"     something stops working, Zadig is the LAST thing to try - never the");
    say(L"     first guess. In order: the cable (5), then whether the control");
    say(L"     service is running (3), then the USB port (4). Zadig after all three.");
    sayBlank();
    say(L"  8. Nothing this installer does needs Windows to be restarted.");
    say(L"     The ASIO driver is a COM DLL: the DJ software loads it with");
    say(L"     LoadLibrary when it creates the driver's class, so there is no kernel");
    say(L"     component and nothing that loads at boot time. That is also why");
    say(L"     Secure Boot and memory integrity are still on - see the reason this");
    say(L"     project exists. Closing the DJ software and opening it again is the");
    say(L"     whole of what is needed here: see 2.");
    say(L"     Measured, not assumed: on the machine this was built on, two complete");
    say(L"     runs of this installer, five loads of the driver by a host program, an");
    say(L"     83 minute audio run and a pulled cable test all happened inside ONE");
    say(L"     session of Windows, with no restart anywhere in it.");
    say(L"     TWO OTHER THINGS CAN ASK FOR A RESTART, and neither of them is this");
    say(L"     installer: Zadig (7), and the teVirtualMIDI that comes with loopMIDI");
    say(L"     (6), which does install a kernel component. If either of those asks,");
    say(L"     it is asking for itself.");
}

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------
static void printSummary(const MachineState* s, const Pending* p, bool failed,
                         const wchar_t* previousRegistration)
{
    sayBlank();
    say(L"=========================== SUMMARY ===========================");
    if (failed) {
        sayFail(L"the installation did NOT complete. Nothing below is a workaround; fix "
                L"the [FAIL] lines above and run it again.");

        // *** THE MACHINE MAY HAVE NO ASIO DRIVER REGISTERED RIGHT NOW ***
        // The ASIO SDK's RegisterAsioDriver deletes HKCR\CLSID\{clsid} before it
        // writes the new one whenever the recorded path differs from the module
        // being registered - which is exactly this case, because the whole point
        // of the warning above is that the recorded path was somewhere else. A
        // failure between the delete and the write leaves nothing behind. The one
        // value that fixes that is in hand at this exact moment, so it is printed
        // instead of being left in a file the user has no reason to open.
        if (previousRegistration && previousRegistration[0]) {
            sayBlank();
            sayWarn(L"IF YOUR DJ SOFTWARE NOW LISTS NO ASIO DRIVER AT ALL: registering "
                    L"removes the previous entry before writing the new one, and this run "
                    L"stopped somewhere in between. To put back what was registered "
                    L"before, run this in an administrator prompt:");
            say(L"    regsvr32 \"%s\"", previousRegistration);
        }
    } else {
        sayOk(L"the parts this installer owns are in place.");
    }

    sayBlank();
    say(L"Still on your side:");
    int items = 0;

    if (p->teVirtualMidiMissing) {
        items++;
        say(L"  %d. Install loopMIDI, which brings teVirtualMIDI with it.", items);
        say(L"     %s", kTeVmDownloadPage);
        say(L"     Without it the control service cannot create the MIDI port, so the");
        say(L"     knobs, buttons and LEDs will not work. Audio works without it.");
    }
    if (p->winUsbBindingMissing) {
        items++;
        say(L"  %d. Bind the BCD3000 to WinUSB, once, with Zadig.", items);
        say(L"     %s", kZadigDownloadPage);
        say(L"     Pick the BCD3000 entry for interface 0 (MI_00) and replace its driver");
        say(L"     with WinUSB. Nothing works before this: it is how the driver reaches");
        say(L"     the hardware. This installer does not do it for you on purpose - a");
        say(L"     wrong binding leaves the device unusable.");
    }
    if (p->deviceNotConnected) {
        items++;
        say(L"  %d. Connect and power on the BCD3000. It was not enumerated during the", items);
        say(L"     install, so nothing about the device itself could be confirmed.");
    }
    if (p->startControlService) {
        items++;
        say(L"  %d. Start the control service. It starts by itself at your next sign in.", items);
        say(L"     To start it now without signing out, open this folder in Explorer and");
        say(L"     double click the shortcut:");
        say(L"       %s", s->shortcutFile);
        say(L"     Start it from Explorer, not from an administrator prompt: it has to");
        say(L"     run unelevated for the driver to reach it.");
    }
    if (p->controlServiceNotReplaced) {
        items++;
        say(L"  %d. The control service on disk is out of date and was left running.", items);
        say(L"     Close your DJ software, then run BCD3000Setup.exe /replace-service.");
    }
    // *** ONLY THE REASONS THAT WERE MEASURED. *** See Pending for what this used
    // to say and why saying it was worse than saying nothing.
    if (needsDjRestart(p)) {
        items++;
        say(L"  %d. Close your DJ software and open it again.", items);
        if (p->registrationRepointed) {
            say(L"     The registration now points at a different copy of the driver than");
            say(L"     the one your DJ software read when it started.");
        }
        if (p->driverFileReplaced) {
            say(L"     The driver file was replaced. An application that already had the");
            say(L"     old one loaded keeps using it until it is started again.");
        }
        if (p->midiPortDestroyed) {
            say(L"     The control service was stopped so that its file could be replaced,");
            say(L"     and stopping it DESTROYS the virtual MIDI port. The port exists again");
            say(L"     once the service is running; only then can a DJ application find the");
            say(L"     controller.");
        } else {
            say(L"     The virtual MIDI port was NOT touched by this install - the control");
            say(L"     service was not stopped, and it still owns the port it created.");
        }
    }
    if (p->wrongAccount) {
        items++;
        say(L"  %d. Install the control service from the right account. This installer ran", items);
        say(L"     as %s while the desktop belongs to %s, so the service and its startup",
            s->account.tokenAccount, s->account.shellAccount);
        say(L"     shortcut were NOT installed - they live in a user profile and would");
        say(L"     have gone into the wrong one. The ASIO driver is machine wide and is");
        say(L"     installed. Sign in as %s and run this installer there; it skips",
            s->account.shellAccount);
        say(L"     everything that is already done.");
    }
    if (items == 0)
        say(L"  nothing. Select \"Behringer BCD3000\" as the ASIO device in your DJ software.");

    printNextStepsAndWarnings();

    sayBlank();
    wchar_t log[kPathMax];
    if (logFilePath(log, kPathMax))
        say(L"A record of this run is in %s", log);
    wchar_t un[kPathMax];
    if (uninstallExePath(un, kPathMax))
        say(L"To undo everything above, run %s", un);
    say(L"===============================================================");
}

// ---------------------------------------------------------------------------
// The run, in two halves.
//
// THE FIRST HALF, prepare(), reads the machine and works out whether an install
// is possible at all. It only reads. It runs on the main thread in both modes and
// its output is the same output the console has always produced, in the same
// order.
//
// THE SECOND HALF, runSteps(), is everything that changes something. It runs on
// the main thread in /console mode and on a worker thread when there is a window,
// and it is the same function with the same decisions either way. Nothing in it
// knows which mode it is in: it says what it is doing with say*(), asks with
// askYesNo(), and marks a step with bcdgui::postStep(), which does nothing at all
// when there is no window to mark it on.
//
// That is the whole of the change to this file. Not one of the decisions below
// moved, and none of them can tell a window from a console.
// ---------------------------------------------------------------------------
struct Run {
    Options      opt;
    MachineState state;
    Pending      pending;
    wchar_t      previousRegistration[kPathMax];
};

// Plain data with no destructor, like everything else at file scope in this
// project: nothing here may run code while the process is being unloaded.
static Run            g_run;
static bcdgui::Wizard g_wiz;

// Returns false when the run has to stop here. *stopCode is then the exit code,
// and *blockedNote is set when the reason is one a person should be shown rather
// than have a window disappear over.
static bool prepare(Run* run, int* stopCode, const wchar_t** blockedNote)
{
    *stopCode    = kExitOk;
    *blockedNote = 0;

    // The log is opened before anything is changed, and never in /check mode:
    // /check has to be provably free of side effects so that it is safe to run on
    // a machine that is about to be used for something important.
    //
    // The log lives in the per user tree, so WHOSE tree that is has to be settled
    // before anything is written into it. This costs one extra read only pass over
    // the process list - gatherMachineState() does its own a few lines below - and
    // buys the guarantee that an elevated process running as one account never
    // creates a folder inside another account's profile.
    //
    // /preview is in the same sentence as /check for the same reason. It exists to
    // be run on a machine that must not be touched, and the help says nothing is
    // written - so it must not open a log file and must not create the folder the
    // log would go in. A mode that claims to write nothing and then writes
    // something is worse than not having the mode.
    if (!run->opt.checkOnly && !run->opt.preview) {
        AccountInfo who;
        detectInteractiveAccount(&who);
        if (who.checked && !who.matched) {
            sayWarn(L"no log file for this run: this installer is running as %s while the "
                    L"desktop belongs to %s, and the log lives in a user profile.",
                    who.tokenAccount, who.shellAccount);
            sayInfo(L"everything is still printed here. See the report below.");
        } else {
            wchar_t log[kPathMax];
            wchar_t dir[kPathMax];
            if (bridgeDirPath(dir, kPathMax) && logFilePath(log, kPathMax)) {
                DWORD err = 0;
                if (ensureDir(dir, &err))
                    logOpen(log);
                else
                    sayWarn(L"could not create %s (%s) - continuing without a log",
                            dir, winErrText(err));
            }
        }
    }

    printPayloadReport();

    gatherMachineState(&run->state);
    reportMachineState(&run->state);

    if (run->opt.checkOnly) {
        say(L"/check was given: nothing was changed and nothing was written.");
        return false;
    }

    if (!run->state.pathsResolved) {
        sayFail(L"refusing to install without knowing where the files go");
        *stopCode    = kExitFailed;
        *blockedNote = L"The Windows folders did not resolve. Nothing can be installed.";
        return false;
    }

    // Elevation. The embedded manifest asks Windows for it, so reaching here
    // without it means the prompt was refused or the manifest was stripped.
    if (!run->state.elevated) {
        sayFail(L"this installer needs administrator rights and does not have them.");
        sayInfo(L"registering an ASIO driver writes to HKEY_CLASSES_ROOT and to "
                L"HKLM\\SOFTWARE\\ASIO, and copying the driver writes into "
                L"%%ProgramFiles%%. Neither is possible without elevation.");
        sayInfo(L"right click BCD3000Setup.exe and choose \"Run as administrator\", or "
                L"start it from an elevated prompt.");
        sayInfo(L"nothing has been installed, registered or replaced. The only thing "
                L"written is this run's entry in the log file.");
        *stopCode    = kExitFailed;
        *blockedNote = L"Not running as administrator, so nothing can be installed.";
        return false;
    }
    return true;
}

// Marks every step from here on as not attempted, so that a progress page which
// stopped early does not leave three hollow circles looking like work in flight.
static void markRemainingSkipped(int from)
{
    for (int i = from; i < kStepCount; i++)
        bcdgui::postStep(i, bcdgui::kRowSkipped, L"not attempted");
}

static int runSteps(void* user)
{
    Run*          run     = (Run*)user;
    MachineState* state   = &run->state;
    Options*      opt     = &run->opt;
    Pending*      pending = &run->pending;

    ZeroMemory(pending, sizeof(*pending));
    pending->teVirtualMidiMissing = (state->tevm.state != kTeVmPresent);
    pending->winUsbBindingMissing = (!state->usb.enumKeyPresent || !state->usb.guidPresent);
    pending->deviceNotConnected   = (!pending->winUsbBindingMissing &&
                                     !state->usb.interfacePresentNow);
    pending->wrongAccount         = (state->account.checked && !state->account.matched);

    // The one consequence of this install that can quietly ruin a working setup:
    // the registration is machine wide and single valued, so installing repoints
    // every host application away from whatever copy is registered now.
    //
    // Resolved once, here, and used by three things that have to agree: the
    // manifest, the failure summary, and the uninstaller that reads the manifest
    // later. See resolvePreviousRegistration for why a second run must not
    // overwrite what the first one recorded.
    wchar_t* previousRegistration = run->previousRegistration;
    resolvePreviousRegistration(state, previousRegistration, kPathMax);
    if (!state->registeredElsewhere && previousRegistration[0])
        sayInfo(L"an earlier run of this installer recorded %s as the registration that "
                L"was in place before it - that record is being kept",
                previousRegistration);

    if (state->registeredElsewhere) {
        sayBlank();
        sayWarn(L"===================== READ THIS FIRST =====================");
        sayWarn(L"The driver is already registered, from a different location:");
        sayWarn(L"    registered now : %s", state->asio.inprocPath);
        sayWarn(L"    after installing: %s", state->dllTarget);
        sayWarn(L"There is only one registration per machine. Installing repoints every");
        sayWarn(L"host application to the copy in the install folder. If the copy that is");
        sayWarn(L"registered now is a build tree you keep rebuilding, your rebuilds will");
        sayWarn(L"stop having any effect until you register that copy again.");
        sayWarn(L"The old path is recorded in the install manifest either way.");
        sayWarn(L"===========================================================");
        if (!opt->assumeYes &&
            !askYesNo(L"Continue and repoint the registration to the install folder?")) {
            // Declined BEFORE the first write. This is the one point at which
            // there is genuinely nothing to undo, which is why the question is
            // here and not half way down the page.
            bcdgui::beginSummaryCapture();
            sayInfo(L"stopped at your request. Nothing has been installed, registered or "
                    L"replaced; the registration still points at %s.",
                    state->asio.inprocPath);
            sayInfo(L"the only thing written is this run's entry in the log file.");
            markRemainingSkipped(kStepRecord);
            bcdgui::postOutcome(bcdgui::kOutcomeStopped);
            return kExitAborted;
        }
    }

    sayBlank();
    bcdgui::postStep(kStepRecord, bcdgui::kRowBusy, 0);
    bool ok = installUninstaller(state);
    if (ok)
        writeManifest(state, previousRegistration);
    bcdgui::postStep(kStepRecord, ok ? bcdgui::kRowOk : bcdgui::kRowFail,
                     ok ? state->installDir : L"failed - see the log below");

    sayBlank();
    bool wroteDriver = false;
    // Kept separate from ok, which a LATER step can turn false. What the driver
    // step did to this machine does not become untrue because the control service
    // failed afterwards, and the two facts below are exactly the ones a user needs
    // in order to know whether to restart a DJ application.
    bool driverOk = false;
    if (ok) {
        bcdgui::postStep(kStepDriver, bcdgui::kRowBusy, 0);
        driverOk = installDriver(state, &wroteDriver);
        ok       = driverOk;
        bcdgui::postStep(kStepDriver, ok ? bcdgui::kRowOk : bcdgui::kRowFail,
                         ok ? state->dllTarget : L"failed - see the log below");
    } else {
        markRemainingSkipped(kStepDriver);
    }

    sayBlank();
    if (ok) {
        bcdgui::postStep(kStepService, bcdgui::kRowBusy, 0);
        ok = installControlService(state, opt, pending);
        bcdgui::RowState st = bcdgui::kRowFail;
        const wchar_t*   dt = L"failed - see the log below";
        if (ok && (pending->controlServiceNotReplaced || pending->wrongAccount)) {
            st = bcdgui::kRowWarn;
            dt = pending->wrongAccount
                 ? L"refused on purpose: this is the wrong account for it"
                 : L"left running and out of date on purpose - see the log below";
        } else if (ok) {
            st = bcdgui::kRowOk;
            dt = state->bridgeTarget;
        }
        bcdgui::postStep(kStepService, st, dt);
    } else {
        markRemainingSkipped(kStepService);
    }

    // The two things a driver install can change under an application that is
    // already running. Both are read from what actually happened, not assumed:
    //
    //   the FILE changed  - wroteDriver is set by installDriver only when it really
    //                       replaced the target, and clsidKeyPresent says there was
    //                       a registration for a host to have followed to it;
    //   the REGISTRATION moved - registeredElsewhere was measured before anything
    //                       was written, and installDriver reads the registry back
    //                       and fails if it did not take, so driverOk means it did.
    //
    // THE SECOND ONE USED TO BE MISSED ENTIRELY. A machine whose registration
    // pointed at another copy, with an already identical file in the install folder,
    // got no advice at all: the file was "already identical - not rewritten", so
    // wroteDriver stayed false, while the class id was repointed at a different DLL
    // underneath the running application. That is a normal second run of this
    // installer.
    if (driverOk && wroteDriver && state->asio.clsidKeyPresent)
        pending->driverFileReplaced = true;
    if (driverOk && state->registeredElsewhere)
        pending->registrationRepointed = true;

    // From here on every line also lands on the last page. It is the same text,
    // not a second version of it: the summary a user reads in the window IS the
    // summary the console prints.
    bcdgui::beginSummaryCapture();
    printSummary(state, pending, !ok, previousRegistration);

    if (!ok) {
        bcdgui::postOutcome(bcdgui::kOutcomeFailed);
        return kExitFailed;
    }
    bcdgui::postOutcome(bcdgui::kOutcomeOk);
    if (pending->teVirtualMidiMissing || pending->winUsbBindingMissing ||
        pending->deviceNotConnected || pending->startControlService ||
        pending->controlServiceNotReplaced || needsDjRestart(pending) ||
        pending->wrongAccount)
        return kExitPending;
    return kExitOk;
}

// ---------------------------------------------------------------------------
// The window. Words only: every judgement on the checks page comes from the
// MachineState that prepare() already gathered.
// ---------------------------------------------------------------------------
static int runWindowed(Run* run, const wchar_t* blockedNote, int blockedCode)
{
    ZeroMemory(&g_wiz, sizeof(g_wiz));
    g_wiz.windowTitle = L"Behringer BCD3000 ASIO driver - Setup";
    g_wiz.headline    = L"Behringer BCD3000 ASIO driver";
    g_wiz.subhead     = L"Open source installer " BCD_VERSION_WSTR
                        L"  -  independent project, not affiliated with the "
                        L"manufacturer";

    g_wiz.hasWelcome        = true;
    g_wiz.welcomeLine1      = L"An ASIO driver for the BCD3000, without turning "
                              L"anything off";
    g_wiz.welcomeLine2      = L"This installs an ASIO driver that reaches the mixer "
                              L"over WinUSB, plus a small service that carries the "
                              L"knobs, the buttons and the LEDs. It needs no signed "
                              L"kernel driver, so there is no Windows protection to "
                              L"switch off - which is the whole reason it exists.";
    g_wiz.welcomeBullets[0] = L"Copies the driver into Program Files and registers it, "
                              L"so your DJ software lists \"Behringer BCD3000\" as an "
                              L"ASIO device.";
    g_wiz.welcomeBullets[1] = L"Copies the control and LED service into your own "
                              L"profile and starts it at every sign in, deliberately "
                              L"unelevated.";
    g_wiz.welcomeBullets[2] = L"Checks two things it will not touch: teVirtualMIDI, and "
                              L"the WinUSB binding on the device.";
    g_wiz.welcomeBullets[3] = 0;
    g_wiz.showDevicePhoto   = true;
    g_wiz.showNotAffiliated = true;

    g_wiz.reviewCaption = L"What is on this machine now";
    g_wiz.reviewFooter  = L"Nothing here has been touched. Reading the registry and "
                          L"looking for files is all that has happened so far, and "
                          L"closing this window now changes nothing.";
    bcdgui::fillPreflightRows(&g_wiz, &run->state);

    g_wiz.progressCaption = L"Installing";
    g_wiz.stepCount       = kStepCount;
    bcdgui::setRow(&g_wiz.steps[kStepRecord], bcdgui::kRowWaiting,
                   L"Put the uninstaller in place and record what was found here",
                   L"First, so that a later failure is still undoable.");
    bcdgui::setRow(&g_wiz.steps[kStepDriver], bcdgui::kRowWaiting,
                   L"Install and register the ASIO driver",
                   L"Checked before it is allowed to replace anything, and verified "
                   L"afterwards by reading the registry back.");
    bcdgui::setRow(&g_wiz.steps[kStepService], bcdgui::kRowWaiting,
                   L"Install the control and LED service",
                   L"Left alone if it is already running the same build: stopping it "
                   L"destroys the virtual MIDI port.");

    g_wiz.startVerb        = L"Install";
    g_wiz.cannotCancelNote = L"This cannot be stopped once it has started.";
    g_wiz.doneCaptionOk      = L"The parts this installer owns are in place.";
    g_wiz.doneCaptionStopped = L"Stopped. Nothing on this machine was changed.";
    g_wiz.doneCaptionFail    = L"The installation did not complete.";

    // The one line on the last page that does not depend on scrolling. It points at
    // the block printNextStepsAndWarnings() writes into the pane below and gives
    // away its first item, because that item is the only one that can destroy an
    // installation that already works, and the user meets it - phrased as an
    // instruction, by their DJ software - before they would ever read a README.
    g_wiz.doneNotice = L"Before you open your DJ software, read \"NEXT STEPS AND "
                       L"WARNINGS\" below. Item 1: do not let your DJ software "
                       L"install a driver for this mixer, however firmly it offers "
                       L"to. That is the one action that undoes all of this.";

    g_wiz.work    = runSteps;
    g_wiz.user    = run;

    if (blockedNote) {
        g_wiz.startBlockedNote = blockedNote;
        g_wiz.cancelExitCode   = blockedCode;
    } else if (run->opt.preview) {
        g_wiz.startBlockedNote = L"/preview: this run can look, and cannot install.";
        g_wiz.cancelExitCode   = kExitAborted;
    } else {
        g_wiz.cancelExitCode = kExitAborted;
    }
    return bcdgui::runWizard(&g_wiz);
}

// ---------------------------------------------------------------------------
int main()
{
    int       argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    Options   opt;
    if (!argv) {
        sayFail(L"could not read the command line");
        return kExitFailed;
    }
    bool argsOk = parseArgs(argc, argv, &opt);
    LocalFree(argv);

    if (argsOk && opt.preview && opt.console) {
        sayFail(L"/preview and /console contradict each other: one asks for the window "
                L"and the other asks for no window");
        argsOk = false;
    }

    // A WINDOW BY DEFAULT, AND A CONSOLE WHENEVER THE ANSWER IS TEXT. /console,
    // /check, /help and a command line that could not be parsed are all text, and
    // all four keep the console exactly as it was.
    //
    // Note what this program is NOT: it is not a /SUBSYSTEM:WINDOWS binary. It
    // stays a console subsystem program and hides the console it was given,
    // because a shell WAITS for a console program and can redirect its output,
    // and that is how an automated run reads back what happened. A windows
    // subsystem binary returns to the prompt immediately and its redirected
    // output is whatever had been flushed by then - which would quietly break the
    // only way this installer can be checked without a person watching it. The
    // price is a console window that exists for the few milliseconds before the
    // line below hides it. That is the trade, stated rather than hidden.
    bool windowed = argsOk && !opt.help && !opt.checkOnly && !opt.console;
    if (windowed) {
        if (bcdgui::init()) {
            bcdgui::detachConsole();
            bcdgui::beginCapture();
        } else {
            // No window to be had. An installer that cannot draw must still be
            // able to install, so this falls back to the text it came from.
            windowed = false;
        }
    }

    printBanner();
    if (!argsOk) {
        sayBlank();
        printHelp();
        pauseIfWeOwnTheConsole();
        return kExitBadArguments;
    }
    if (opt.help) {
        printHelp();
        pauseIfWeOwnTheConsole();
        return kExitOk;
    }

    ZeroMemory(&g_run, sizeof(g_run));
    g_run.opt = opt;

    int            stopCode = kExitOk;
    const wchar_t* blocked  = 0;
    bool           canRun   = prepare(&g_run, &stopCode, &blocked);

    int code;
    if (windowed && (canRun || blocked)) {
        code = runWindowed(&g_run, canRun ? 0 : blocked, stopCode);
    } else if (!canRun) {
        code = stopCode;
    } else {
        code = runSteps(&g_run);
    }

    logClose();
    pauseIfWeOwnTheConsole();
    return code;
}
