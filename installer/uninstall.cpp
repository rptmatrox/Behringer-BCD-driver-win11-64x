// BCD3000Uninstall.exe - undoes exactly what BCD3000Setup.exe did.
//
// It removes: the ASIO registration, the driver file and its folder, the control
// service executable, and the Startup shortcut. It stops the control service,
// because the file cannot be deleted while it runs.
//
// IT PUTS BACK WHAT IT DISPLACED. The ASIO registration is machine wide and holds
// one driver at a time, so installing this product pointed every host application
// away from whatever was registered before. The setup recorded that path; this
// program checks that the file is still there and OFFERS TO REGISTER IT AGAIN.
// Without that, installing and then uninstalling turns a machine that had a
// working ASIO driver into a machine with none, which no uninstaller may do.
//
// It does NOT remove:
//   - teVirtualMIDI. It is a third party product that we neither ship nor
//     install, and other software on the machine may depend on it.
//   - the WinUSB binding on the device. We never applied it, and undoing a USB
//     binding is exactly the operation that can leave hardware unusable.
//   - the log files. They are the only record of what the driver did, and they
//     are what a bug report needs. The folder that holds them stays.
//
// STOPPING THE CONTROL SERVICE HAS A CONSEQUENCE, measured three times on the
// hardware: it destroys the virtual MIDI port, and a DJ application that is open
// at that moment does not go looking for the controller again until it is
// restarted. The uninstaller says so before it does it.

#include "common.h"
#include "gui.h"
#include "version.h"

#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

using namespace bcdsetup;

enum ExitCode {
    kExitOk           = 0,
    kExitFailed       = 1,
    kExitBadArguments = 2,
    kExitAborted      = 4     // stopped by the user; nothing was changed
};

struct Options {
    bool help;
    bool assumeYes;
    bool console;
};

// The steps as they appear on the progress page.
enum StepIndex {
    kStepStopService  = 0,
    kStepUnregister   = 1,
    kStepShortcut     = 2,
    kStepFiles        = 3,
    kStepPutBack      = 4,
    kStepCount        = 5
};

static void printHelp()
{
    say(L"Usage: BCD3000Uninstall.exe [options]");
    sayBlank();
    say(L"  (no options)  open the uninstaller window.");
    say(L"  /console      do everything in this console instead of in a window.");
    say(L"                Identical decisions, identical text.");
    say(L"  /yes          do not ask for confirmation.");
    say(L"  /help         this text.");
    sayBlank();
    say(L"Exit codes: 0 done, 1 a step failed, 2 bad arguments, 4 stopped at your");
    say(L"request with nothing changed.");
    sayBlank();
    say(L"Removes the ASIO driver, its registration, the control service and its");
    say(L"startup shortcut. Leaves teVirtualMIDI, the WinUSB binding on the device");
    say(L"and the log files alone - this program did not create any of them.");
}

static bool parseArgs(int argc, wchar_t** argv, Options* opt)
{
    ZeroMemory(opt, sizeof(*opt));
    for (int i = 1; i < argc; i++) {
        const wchar_t* a = argv[i];
        if (_wcsicmp(a, L"/help") == 0 || _wcsicmp(a, L"-h") == 0 ||
            _wcsicmp(a, L"--help") == 0 || _wcsicmp(a, L"/?") == 0)
            opt->help = true;
        else if (_wcsicmp(a, L"/console") == 0)
            opt->console = true;
        else if (_wcsicmp(a, L"/yes") == 0)
            opt->assumeYes = true;
        else {
            sayFail(L"unknown option: %s", a);
            return false;
        }
    }
    return true;
}

// Reading the install manifest lives in common.cpp, shared with the setup. It
// used to be a private copy here, and the setup had none: the setup could write
// the one value that matters without ever reading what was already in the file,
// which is how a second install run replaced a recorded path with "(none)" and
// left this program with nothing to tell the user.
//
// The manifest is used for INFORMATION ONLY. Every path this program deletes is
// worked out from the Windows folders, exactly the way the setup worked it out,
// so a tampered manifest cannot redirect a deletion.

// ---------------------------------------------------------------------------
// The run, in the same two halves as the setup: a read only first half that
// reports and works out what is possible, and a second half that removes things.
// The second half runs on the main thread in /console mode and on a worker thread
// when there is a window, and it is the same code with the same decisions either
// way.
//
// THE CONFIRMATION IS THE ONE THING THAT MOVED, and only its shape. In /console
// mode it is the same askYesNo() prompt it always was. In window mode it is the
// Remove button on the first page, which is the same consent obtained the way a
// window obtains consent - and it is written into the log in the same words, so
// the two are still comparable line for line.
// ---------------------------------------------------------------------------
struct Run {
    Options      opt;
    MachineState state;
    wchar_t      bridgeDir[kPathMax];
    wchar_t      previous[kPathMax];
    bool         havePrevious;
    bool         consentFromWindow;
};

static Run            g_run;
static bcdgui::Wizard g_wiz;

static bool prepare(Run* run, int* stopCode, const wchar_t** blockedNote)
{
    *stopCode    = kExitOk;
    *blockedNote = 0;

    wchar_t logPath[kPathMax];
    if (bridgeDirPath(run->bridgeDir, kPathMax) && logFilePath(logPath, kPathMax) &&
        dirExists(run->bridgeDir))
        logOpen(logPath);

    gatherMachineState(&run->state);
    reportMachineState(&run->state);

    if (!run->state.pathsResolved) {
        sayFail(L"could not resolve the Windows folders - refusing to delete anything");
        *stopCode    = kExitFailed;
        *blockedNote = L"The Windows folders did not resolve. Nothing will be removed.";
        return false;
    }
    if (!run->state.elevated) {
        sayFail(L"this uninstaller needs administrator rights and does not have them.");
        sayInfo(L"removing the ASIO registration writes to HKEY_CLASSES_ROOT and to "
                L"HKLM\\SOFTWARE\\ASIO. Nothing has been changed.");
        *stopCode    = kExitFailed;
        *blockedNote = L"Not running as administrator, so nothing can be removed.";
        return false;
    }

    // Read BEFORE anything is removed, because step 4 deletes the manifest.
    run->havePrevious = readRecordedPreviousRegistration(run->previous, kPathMax);
    if (run->havePrevious && _wcsicmp(run->previous, run->state.dllTarget) == 0) {
        // The recorded path is the copy we are about to delete, so it is not a way
        // back to anything. Cannot happen with a manifest this product wrote, and
        // is cheap to rule out.
        run->havePrevious = false;
    }
    return true;
}

// The plan, printed in both modes. The window shows the same facts as rows on its
// first page; this is what puts them in the log.
static void reportPlan(Run* run)
{
    MachineState* state = &run->state;

    say(L"--- what will be removed ---");
    sayInfo(L"the ASIO registration for class id %s", kAsioClsid);
    sayInfo(L"the driver file and its folder: %s", state->installDir);
    sayInfo(L"the control service: %s", state->bridgeTarget);
    sayInfo(L"the startup shortcut: %s", state->shortcutFile);
    sayBlank();
    say(L"--- what will be kept ---");
    sayInfo(L"teVirtualMIDI: not ours, other software may need it");
    sayInfo(L"the WinUSB binding on the device: we never applied it");
    sayInfo(L"the log files in %s", run->bridgeDir);
    sayBlank();
    if (run->havePrevious) {
        say(L"--- the driver that was registered before this one ---");
        sayInfo(L"%s", run->previous);
        if (fileExists(run->previous))
            sayInfo(L"that file is still on disk, and this uninstaller will offer to "
                    L"register it again at the end. Removing our registration leaves the "
                    L"machine with no ASIO driver at all until something is registered.");
        else
            sayWarn(L"that file is no longer on disk, so it cannot be put back. After this "
                    L"runs, no ASIO driver will be registered on this machine.");
        sayBlank();
    }
    if (state->bridge.running) {
        sayWarn(L"the control service is running and has to be stopped. That destroys the "
                L"virtual MIDI port.");
        sayWarn(L"any DJ application that is open right now will stop seeing the "
                L"controller, and reopening the application is the only way back.");
        sayBlank();
    }
}

static void markRemainingSkipped(int from)
{
    for (int i = from; i < kStepCount; i++)
        bcdgui::postStep(i, bcdgui::kRowSkipped, L"not attempted");
}

static int runRemoval(void* user)
{
    // References, so that every step below is the code it always was: this is a
    // presentation change and the steps are not to be edited for it.
    Run*          run          = (Run*)user;
    Options&      opt          = run->opt;
    MachineState& state        = run->state;
    wchar_t*      previous     = run->previous;
    bool          havePrevious = run->havePrevious;

    // The window's Remove button is the answer askYesNo() would have got at the
    // console. Recorded in the same words so that a log from either mode reads the
    // same.
    if (run->consentFromWindow)
        sayInfo(L"question: Remove the BCD3000 driver and control service? -> yes "
                L"(the Remove button in the window)");

    bool allGood = true;

    // 1. Stop the control service. Before deleting its file, and before touching
    //    the driver, so that nothing is holding the device when the driver goes.
    //
    //    *** FAILING HERE ABORTS THE WHOLE THING, BEFORE ANYTHING IS DESTROYED ***
    //    This step used to note the failure and carry on into unregistering and
    //    deleting. But the reason it can fail is that the service is alive and
    //    holding its own file - so carrying on unregisters the driver, fails to
    //    delete the file, and leaves a half removed installation that is worse
    //    than either finishing or not starting. Nothing below has happened yet, so
    //    stopping now costs the user nothing but a second run.
    say(L"--- stopping the control service ---");
    bcdgui::postStep(kStepStopService, bcdgui::kRowBusy, 0);
    if (!state.bridge.running) {
        sayOk(L"not running");
        bcdgui::postStep(kStepStopService, bcdgui::kRowOk, L"it was not running");
    } else {
        int   killed  = 0;
        DWORD stopErr = 0;
        if (stopBridge(&killed, &stopErr)) {
            sayOk(L"stopped %d process%s", killed, killed == 1 ? L"" : L"es");
            bcdgui::postStep(kStepStopService, bcdgui::kRowOk,
                             L"stopped - the virtual MIDI port is gone now");
        } else {
            sayFail(L"could not stop the control service (%s)", winErrText(stopErr));
            bcdgui::postStep(kStepStopService, bcdgui::kRowFail,
                             L"could not be stopped - nothing was removed");
            markRemainingSkipped(kStepUnregister);
            bcdgui::beginSummaryCapture();
            sayInfo(L"its file cannot be deleted while it runs, and it holds the device "
                    L"the driver needs, so going any further would leave this "
                    L"installation half removed.");
            sayInfo(L"NOTHING HAS BEEN REMOVED. The driver is still registered and every "
                    L"file is still in place.");
            sayInfo(L"end BCD3000Bridge.exe in Task Manager, or sign out and in again and "
                    L"run this uninstaller before starting anything else, then try again.");
            sayInfo(L"if this installer is running as a different administrator account "
                    L"from the one signed in, that alone is enough to be refused "
                    L"permission to stop it - run it from the signed in account.");
            bcdgui::postOutcome(bcdgui::kOutcomeFailed);
            return kExitFailed;
        }
    }
    sayBlank();

    // 2. Unregister the driver. This needs the file, so it happens before the
    //    deletion.
    say(L"--- removing the ASIO registration ---");
    bcdgui::postStep(kStepUnregister, bcdgui::kRowBusy, 0);
    bool regGoodBefore = allGood;
    bcdgui::RowState regRow = bcdgui::kRowOk;
    const wchar_t*   regNote = L"removed, and verified gone from the registry";
    if (!state.asio.clsidKeyPresent && !state.asio.asioNameKeyPresent) {
        sayOk(L"nothing registered");
        regNote = L"there was nothing registered";
    } else if (state.registeredElsewhere) {
        sayWarn(L"the registration points at %s, which is not the copy this uninstaller "
                L"installed (%s)", state.asio.inprocPath, state.dllTarget);
        sayWarn(L"leaving it alone. Another installation of this driver owns it.");
        regRow  = bcdgui::kRowWarn;
        regNote = L"left alone on purpose: it points at another copy of this driver";
    } else if (fileExists(state.dllTarget)) {
        HRESULT hr     = 0;
        DWORD   winErr = 0;
        if (callDllUnregisterServer(state.dllTarget, &hr, &winErr)) {
            AsioRegInfo after;
            detectAsioRegistration(&after);
            if (after.clsidKeyPresent || after.asioNameKeyPresent) {
                sayWarn(L"the driver reported success but registry entries remain - "
                        L"removing them directly");
                bool a = false, b = false;
                if (!deleteAsioRegistryKeys(state.dllTarget, &a, &b))
                    allGood = false;
            } else {
                sayOk(L"unregistered, and verified gone from the registry");
            }
        } else {
            if (winErr)
                sayWarn(L"could not call the driver to unregister (%s) - removing the "
                        L"registry entries directly", winErrText(winErr));
            else
                sayWarn(L"the driver refused to unregister (0x%08lX) - removing the "
                        L"registry entries directly", (unsigned long)hr);
            bool a = false, b = false;
            if (!deleteAsioRegistryKeys(state.dllTarget, &a, &b))
                allGood = false;
            else
                sayOk(L"registry entries removed (class id: %s, ASIO list: %s)",
                      a ? L"yes" : L"already gone", b ? L"yes" : L"already gone");
        }
    } else {
        sayInfo(L"the driver file is already gone - removing the registry entries directly");
        bool a = false, b = false;
        if (!deleteAsioRegistryKeys(state.dllTarget, &a, &b))
            allGood = false;
        else
            sayOk(L"registry entries removed (class id: %s, ASIO list: %s)",
                  a ? L"yes" : L"already gone", b ? L"yes" : L"already gone");
    }
    if (allGood != regGoodBefore) {
        regRow  = bcdgui::kRowFail;
        regNote = L"could not be removed - see the log below";
    }
    bcdgui::postStep(kStepUnregister, regRow, regNote);
    sayBlank();

    // 3. The Startup shortcut, and only if it is ours.
    say(L"--- removing the startup shortcut ---");
    bcdgui::postStep(kStepShortcut, bcdgui::kRowBusy, 0);
    bool             lnkGoodBefore = allGood;
    bcdgui::RowState lnkRow  = bcdgui::kRowOk;
    const wchar_t*   lnkNote = L"removed";
    if (!state.shortcutPresent) {
        sayOk(L"no shortcut to remove");
        lnkNote = L"there was none";
    } else if (state.shortcutPointsAt[0] &&
               _wcsicmp(state.shortcutPointsAt, state.bridgeTarget) != 0) {
        sayWarn(L"%s points at %s, not at our control service - leaving it alone",
                state.shortcutFile, state.shortcutPointsAt);
        lnkRow  = bcdgui::kRowWarn;
        lnkNote = L"left alone: it points somewhere else";
    } else if (!state.shortcutPointsAt[0]) {
        // The file has our name and sits in the Startup folder, but its target
        // could not be read, so "it is ours" is a guess rather than the check the
        // branch above performs. A guess is not enough to delete a file out of
        // somebody's Startup folder without saying so.
        sayWarn(L"%s could not be read, so there is no proof that it is ours",
                state.shortcutFile);
        if (opt.assumeYes ||
            askYesNo(L"Delete it anyway? It carries the name this product uses.")) {
            if (DeleteFileW(state.shortcutFile)) {
                sayOk(L"removed %s", state.shortcutFile);
            } else {
                sayFail(L"could not remove %s (%s)", state.shortcutFile,
                        winErrText(GetLastError()));
                allGood = false;
            }
        } else {
            sayInfo(L"left in place. Delete it by hand if it turns out to be ours.");
            lnkRow  = bcdgui::kRowWarn;
            lnkNote = L"left in place at your request";
        }
    } else if (DeleteFileW(state.shortcutFile)) {
        sayOk(L"removed %s", state.shortcutFile);
    } else {
        sayFail(L"could not remove %s (%s)", state.shortcutFile, winErrText(GetLastError()));
        allGood = false;
    }
    if (allGood != lnkGoodBefore) {
        lnkRow  = bcdgui::kRowFail;
        lnkNote = L"could not be removed - see the log below";
    }
    bcdgui::postStep(kStepShortcut, lnkRow, lnkNote);
    sayBlank();

    // 4. The files.
    say(L"--- removing files ---");
    bcdgui::postStep(kStepFiles, bcdgui::kRowBusy, 0);
    bool fileGoodBefore = allGood;
    if (!fileExists(state.bridgeTarget)) {
        sayOk(L"control service already absent");
    } else if (DeleteFileW(state.bridgeTarget)) {
        sayOk(L"removed %s", state.bridgeTarget);
    } else {
        sayFail(L"could not remove %s (%s)", state.bridgeTarget, winErrText(GetLastError()));
        allGood = false;
    }

    if (!fileExists(state.dllTarget)) {
        sayOk(L"driver already absent");
    } else if (DeleteFileW(state.dllTarget)) {
        sayOk(L"removed %s", state.dllTarget);
    } else {
        DWORD err = GetLastError();
        sayFail(L"could not remove %s (%s)", state.dllTarget, winErrText(err));
        if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION)
            sayInfo(L"a program still has the driver loaded. Close your DJ software and "
                    L"run the uninstaller again.");
        allGood = false;
    }

    // The two names the setup puts next to the driver: the copy it keeps of a
    // driver it replaced, and the staging file it writes a payload to before
    // checking it. Both live in the install folder and both have to go, or the
    // folder cannot be removed and the user is told to go and look inside it.
    wchar_t sideCar[kPathMax];
    static const wchar_t* const kSuffixes[2] = { L".bak", L".new" };
    for (int i = 0; i < 2; i++) {
        if (wcslen(state.dllTarget) + 8 >= kPathMax)
            break;
        _snwprintf(sideCar, kPathMax - 1, L"%s%s", state.dllTarget, kSuffixes[i]);
        sideCar[kPathMax - 1] = 0;
        if (!fileExists(sideCar))
            continue;
        if (DeleteFileW(sideCar))
            sayOk(L"removed %s", sideCar);
        else
            sayWarn(L"could not remove %s (%s)", sideCar, winErrText(GetLastError()));
    }

    wchar_t manifest[kPathMax];
    if (manifestPath(manifest, kPathMax) && fileExists(manifest)) {
        if (DeleteFileW(manifest))
            sayOk(L"removed %s", manifest);
        else
            sayWarn(L"could not remove %s (%s)", manifest, winErrText(GetLastError()));
    }

    // 5. This program itself. A running executable cannot delete its own file, so
    //    when we are running from the install folder it is scheduled for the next
    //    restart. When we are running from somewhere else, the installed copy is
    //    just a file and goes now.
    wchar_t self[kPathMax];
    self[0] = 0;
    GetModuleFileNameW(0, self, kPathMax);
    wchar_t installedSelf[kPathMax];
    bool haveInstalledSelf = uninstallExePath(installedSelf, kPathMax);

    if (haveInstalledSelf && fileExists(installedSelf)) {
        if (_wcsicmp(self, installedSelf) != 0) {
            if (DeleteFileW(installedSelf))
                sayOk(L"removed %s", installedSelf);
            else
                sayWarn(L"could not remove %s (%s)", installedSelf,
                        winErrText(GetLastError()));
        } else if (MoveFileExW(installedSelf, 0, MOVEFILE_DELAY_UNTIL_REBOOT)) {
            sayInfo(L"%s is in use because you are running it - Windows will delete it at "
                    L"the next restart", installedSelf);
        } else {
            sayWarn(L"could not schedule %s for deletion (%s) - delete it by hand",
                    installedSelf, winErrText(GetLastError()));
        }
    }

    if (dirExists(state.installDir)) {
        if (RemoveDirectoryW(state.installDir)) {
            sayOk(L"removed %s", state.installDir);
        } else if (MoveFileExW(state.installDir, 0, MOVEFILE_DELAY_UNTIL_REBOOT)) {
            // THE ORDER OF THESE TWO PENDING OPERATIONS IS LOAD BEARING. Windows
            // replays PendingFileRenameOperations in the order they were queued,
            // and a delayed RemoveDirectory only succeeds on an empty directory.
            // The uninstaller's own file was queued a few lines above, so the file
            // is deleted first and the directory is then empty when its turn
            // comes. Queue this one before that one and the directory removal
            // fails silently at the next restart, leaving the folder behind.
            sayInfo(L"%s will be removed at the next restart", state.installDir);
        } else {
            sayWarn(L"%s is not empty and could not be removed (%s) - look inside and "
                    L"delete it by hand", state.installDir, winErrText(GetLastError()));
        }
    }
    bcdgui::postStep(kStepFiles,
                     allGood == fileGoodBefore ? bcdgui::kRowOk : bcdgui::kRowFail,
                     allGood == fileGoodBefore ? L"removed"
                                               : L"something could not be removed - see "
                                                 L"the log below");
    sayBlank();

    // 6. Putting back the driver that was registered before this product.
    //
    // *** THIS IS THE STEP THAT DECIDES WHETHER THIS PROGRAM LEAVES THE MACHINE
    // WORSE THAN IT FOUND IT *** Before the install, this machine had a working,
    // verified ASIO driver. Everything above removed our registration and our
    // copy of the driver; the copy that was registered before is still on disk and
    // now has nothing pointing at it. A line of text at the end of a summary is
    // not a repair, so the repair is offered here and carried out.
    bool previousRestored   = false;
    bool previousStillThere = havePrevious && fileExists(previous);

    AsioRegInfo nowReg;
    detectAsioRegistration(&nowReg);
    bool nothingRegisteredNow = !nowReg.inprocPresent && !nowReg.asioNameKeyPresent;

    say(L"--- the ASIO driver that was registered before this one ---");
    bcdgui::postStep(kStepPutBack, bcdgui::kRowBusy, 0);
    if (!havePrevious) {
        sayInfo(L"the install manifest records no earlier registration, so there is "
                L"nothing to put back");
        bcdgui::postStep(kStepPutBack, bcdgui::kRowSkipped,
                         L"there was nothing registered before this driver");
    } else if (!nothingRegisteredNow) {
        sayInfo(L"an ASIO registration is still in place and was not ours to remove - "
                L"leaving it alone. It points at %s.", nowReg.inprocPath);
        bcdgui::postStep(kStepPutBack, bcdgui::kRowSkipped,
                         L"an ASIO registration is still in place and is not ours");
    } else if (!previousStillThere) {
        sayWarn(L"before this product was installed, host applications loaded %s, which "
                L"is no longer on disk - it cannot be put back", previous);
        bcdgui::postStep(kStepPutBack, bcdgui::kRowWarn,
                         L"that driver's file is gone, so it cannot be put back. NO ASIO "
                         L"DRIVER IS REGISTERED ON THIS MACHINE NOW.");
    } else {
        sayInfo(L"before this product was installed, host applications loaded:");
        sayInfo(L"    %s", previous);
        sayInfo(L"that file is still there, and nothing points at it now: no ASIO driver "
                L"is registered on this machine at this moment.");
        if (opt.assumeYes || askYesNo(L"Register that driver again now?")) {
            HRESULT hr     = 0;
            DWORD   winErr = 0;
            if (callDllRegisterServer(previous, &hr, &winErr)) {
                // Proved by reading the registry back, the same way the setup
                // proves its own registration.
                AsioRegInfo after;
                detectAsioRegistration(&after);
                if (after.inprocPresent && _wcsicmp(after.inprocPath, previous) == 0) {
                    sayOk(L"registered again, and verified: host applications will load %s",
                          previous);
                    previousRestored = true;
                    bcdgui::postStep(kStepPutBack, bcdgui::kRowOk, previous);
                } else {
                    sayWarn(L"the registration call succeeded but the class id now points "
                            L"at \"%s\" instead of \"%s\"", after.inprocPath, previous);
                    bcdgui::postStep(kStepPutBack, bcdgui::kRowWarn,
                                     L"the call succeeded but the registry does not agree "
                                     L"- see the log below");
                }
            } else if (winErr) {
                sayWarn(L"could not load or call that driver to register it (%s)",
                        winErrText(winErr));
                bcdgui::postStep(kStepPutBack, bcdgui::kRowWarn,
                                 L"that driver could not be loaded to register it");
            } else {
                sayWarn(L"that driver refused to register (DllRegisterServer returned "
                        L"0x%08lX)", (unsigned long)hr);
                bcdgui::postStep(kStepPutBack, bcdgui::kRowWarn,
                                 L"that driver refused to register itself again");
            }
        } else {
            sayInfo(L"left unregistered at your request.");
            bcdgui::postStep(kStepPutBack, bcdgui::kRowWarn,
                             L"left unregistered at your request. NO ASIO DRIVER IS "
                             L"REGISTERED ON THIS MACHINE NOW.");
        }
    }
    sayBlank();

    // From here on every line also lands on the last page. It is the same text
    // the console prints, not a second version of it, and it starts with the
    // regsvr32 line whenever this machine has been left with no ASIO driver.
    bcdgui::beginSummaryCapture();
    say(L"=========================== SUMMARY ===========================");

    // FIRST, before anything else, because it is the only line here that the user
    // has to act on and because it is the state of their audio right now. It used
    // to be the second to last paragraph of eight, which is the same as not
    // printing it. Printed only when all three are true: there was a driver, it is
    // still on disk, and nothing is registered now.
    if (previousStillThere && nothingRegisteredNow && !previousRestored) {
        sayWarn(L"THIS MACHINE HAS NO ASIO DRIVER REGISTERED. To put back the one that "
                L"was registered before this product, run this in an administrator "
                L"prompt:");
        say(L"    regsvr32 \"%s\"", previous);
        sayBlank();
    }

    if (allGood)
        sayOk(L"the driver and the control service have been removed.");
    else
        sayFail(L"some steps failed. See the [FAIL] lines above; nothing was forced.");
    sayBlank();
    say(L"Left in place on purpose:");
    say(L"  - teVirtualMIDI. Uninstall it from Windows \"Apps\" if you want it gone,");
    say(L"    but check first that nothing else on the machine uses it.");
    say(L"  - the WinUSB binding on the BCD3000. To put the device back on another");
    say(L"    driver, use Device Manager or Zadig yourself. We will not do it for you:");
    say(L"    a wrong binding leaves the hardware unusable.");
    say(L"  - the log files in %s", run->bridgeDir);
    if (previousRestored) {
        sayBlank();
        say(L"The driver that was registered before this product has been registered");
        say(L"again, so your DJ software has an ASIO device to select:");
        say(L"    %s", previous);
    } else if (havePrevious && !previousStillThere && nothingRegisteredNow) {
        sayBlank();
        say(L"Before this product was installed, the driver was registered from");
        say(L"    %s", previous);
        say(L"That file is not on disk any more, so nothing could be put back. No ASIO");
        say(L"driver is registered on this machine now.");
    }
    sayBlank();
    // *** THE REASON IS CONDITIONAL BECAUSE THE PORT IS. *** state.bridge.running
    // was read before step 1, and step 1 aborts the whole run if it cannot stop a
    // running service - so reaching here with it true means THIS run destroyed the
    // port, and reaching here with it false means there was no port to destroy.
    // Saying "the port is gone now" in the second case would be the same invented
    // reason the installer's summary used to print: right advice, wrong cause, and
    // a wrong cause teaches a wrong model of a machine somebody has to repair.
    if (state.bridge.running) {
        say(L"Restart your DJ software: the control service was stopped, so the virtual");
        say(L"MIDI port is gone, and a DJ application does not go looking for the");
        say(L"controller again on its own.");
    } else {
        say(L"Restart your DJ software: the registration it read when it started has");
        say(L"changed. The virtual MIDI port was already gone before this ran - the");
        say(L"control service was not running - so this uninstall destroyed nothing");
        say(L"a DJ application was holding.");
    }
    say(L"===============================================================");

    bcdgui::postOutcome(allGood ? bcdgui::kOutcomeOk : bcdgui::kOutcomeFailed);
    return allGood ? kExitOk : kExitFailed;
}

// ---------------------------------------------------------------------------
// The window. Minimal on purpose - confirm, work, summarise - with one thing
// deliberately not minimal: the offer to put back the ASIO driver that was
// registered before this product. That offer is the difference between an
// uninstaller that leaves a machine as it found it and one that leaves it with no
// ASIO driver at all, so it is stated on the first page BEFORE anything is
// removed, marked as a row of its own on the progress page, and asked as a
// question of its own. It is not allowed to be a line somebody scrolls past.
// ---------------------------------------------------------------------------
static int runWindowed(Run* run, const wchar_t* blockedNote, int blockedCode)
{
    MachineState* s = &run->state;

    ZeroMemory(&g_wiz, sizeof(g_wiz));
    g_wiz.windowTitle = L"Behringer BCD3000 ASIO driver - Uninstall";
    g_wiz.headline    = L"Remove the Behringer BCD3000 ASIO driver";
    g_wiz.subhead     = L"Uninstaller " BCD_VERSION_WSTR
                        L"  -  it removes what the setup added, and nothing else";
    g_wiz.hasWelcome  = false;

    g_wiz.reviewCaption = L"What this will do";
    g_wiz.reviewFooter  = L"Nothing has been removed yet. Closing this window now leaves "
                          L"the machine exactly as it is.";

    int n = 0;
    bcdgui::setRow(&g_wiz.review[n++], bcdgui::kRowNeutral, L"Will be removed",
                   L"The ASIO registration for class id %s.\n%s\n%s\n%s",
                   kAsioClsid, s->installDir, s->bridgeTarget, s->shortcutFile);
    bcdgui::setRow(&g_wiz.review[n++], bcdgui::kRowNeutral, L"Will be kept",
                   L"teVirtualMIDI, because it is not ours and other software may need "
                   L"it. The WinUSB binding on the device, because we never applied it "
                   L"and undoing a USB binding is what leaves hardware unusable. The log "
                   L"files in %s, because they are what a bug report needs.",
                   run->bridgeDir);

    // THE OFFER. On the page, before the button, in its own row.
    if (run->havePrevious && fileExists(run->previous))
        bcdgui::setRow(&g_wiz.review[n++], bcdgui::kRowWarn,
                       L"An ASIO driver was registered before this one",
                       L"%s\nThat file is still on disk. Removing our registration leaves "
                       L"this machine with NO ASIO driver at all, so this uninstaller will "
                       L"ask you at the end whether to register that one again. Say yes "
                       L"unless you know you do not want it.",
                       run->previous);
    else if (run->havePrevious)
        bcdgui::setRow(&g_wiz.review[n++], bcdgui::kRowFail,
                       L"An ASIO driver was registered before this one, and it is gone",
                       L"%s is recorded as the registration that was in place before this "
                       L"product, but that file is no longer on disk, so it cannot be put "
                       L"back. After this runs, no ASIO driver will be registered on this "
                       L"machine.",
                       run->previous);
    else
        bcdgui::setRow(&g_wiz.review[n++], bcdgui::kRowNeutral,
                       L"No earlier ASIO driver to put back",
                       L"The install manifest records no registration in place before this "
                       L"product, so there is nothing to restore. After this runs, no ASIO "
                       L"driver will be registered on this machine unless you have another "
                       L"one.");

    if (s->bridge.running)
        bcdgui::setRow(&g_wiz.review[n++], bcdgui::kRowWarn,
                       L"The control service is running and has to be stopped",
                       L"That destroys the virtual MIDI port. Any DJ application that is "
                       L"open right now will stop seeing the controller, and reopening the "
                       L"application is the only way back. Measured three times on the "
                       L"hardware.");
    g_wiz.reviewCount = n;

    g_wiz.progressCaption = L"Removing";
    g_wiz.stepCount       = kStepCount;
    bcdgui::setRow(&g_wiz.steps[kStepStopService], bcdgui::kRowWaiting,
                   L"Stop the control and LED service",
                   L"If this fails, nothing below is attempted at all.");
    bcdgui::setRow(&g_wiz.steps[kStepUnregister], bcdgui::kRowWaiting,
                   L"Remove the ASIO registration", 0);
    bcdgui::setRow(&g_wiz.steps[kStepShortcut], bcdgui::kRowWaiting,
                   L"Remove the startup shortcut",
                   L"Only when it can be proved to be ours.");
    bcdgui::setRow(&g_wiz.steps[kStepFiles], bcdgui::kRowWaiting,
                   L"Remove the driver, the service and the install folder", 0);
    bcdgui::setRow(&g_wiz.steps[kStepPutBack], bcdgui::kRowWaiting,
                   L"Offer to register the previous ASIO driver again",
                   L"You will be asked. This is the step that decides whether this "
                   L"machine still has an ASIO driver afterwards.");

    g_wiz.startVerb        = L"Remove";
    g_wiz.cannotCancelNote = L"This cannot be stopped once it has started.";
    g_wiz.doneCaptionOk      = L"The driver and the control service have been removed.";
    g_wiz.doneCaptionStopped = L"Stopped. Nothing was removed.";
    g_wiz.doneCaptionFail    = L"Some steps failed. Nothing was forced.";

    g_wiz.work = runRemoval;
    g_wiz.user = run;

    if (blockedNote) {
        g_wiz.startBlockedNote = blockedNote;
        g_wiz.cancelExitCode   = blockedCode;
    } else {
        g_wiz.cancelExitCode = kExitAborted;
    }
    run->consentFromWindow = true;
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

    // A window by default, the console whenever the answer is text. /yes is a
    // console idea - it exists so that an unattended run does not stop on a
    // question - so it keeps the console too: a window that answers its own
    // questions and then closes would be worse than no window.
    bool windowed = argsOk && !opt.help && !opt.console && !opt.assumeYes;
    if (windowed) {
        if (bcdgui::init()) {
            bcdgui::detachConsole();
            bcdgui::beginCapture();
        } else {
            windowed = false;
        }
    }

    say(L"%s - uninstaller %s", kProductName, BCD_VERSION_WSTR);
    sayBlank();
    if (!argsOk) {
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

    if (canRun)
        reportPlan(&g_run);

    int code;
    if (windowed) {
        code = runWindowed(&g_run, canRun ? 0 : blocked, stopCode);
    } else if (!canRun) {
        code = stopCode;
    } else if (!opt.assumeYes &&
               !askYesNo(L"Remove the BCD3000 driver and control service?")) {
        sayInfo(L"stopped at your request. Nothing has been removed or unregistered; the "
                L"only thing written is this run's entry in the log file.");
        code = kExitAborted;
    } else {
        sayBlank();
        code = runRemoval(&g_run);
    }

    logClose();
    pauseIfWeOwnTheConsole();
    return code;
}
