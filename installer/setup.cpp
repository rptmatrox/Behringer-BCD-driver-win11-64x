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
//   3. Windows MIDI Services, which the virtual MIDI port is created through and
//      which is part of Windows - so there is nothing to install, only a reading
//      to report, including whether this build carries microsoft/MIDI issue #1047
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
    // *** NOT A MEASUREMENT, AND IT IS THE ONLY ONE ON THIS STRUCTURE. *** Somebody
    // pressed the named door on the binding screen to say the binding is already
    // applied. Every other flag here is what this program read; this is what a person
    // said, and it is kept apart from the readings by this comment rather than by a
    // second structure, because it goes to exactly one place: the exit code.
    bool bindingClaimedByHand;

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

// ---------------------------------------------------------------------------
// *** 0 OR 3, AS A FUNCTION, SO THAT SOMETHING CAN ASK IT. ***
//
// This was seven clauses inside runSteps(), which is a function that installs a
// driver: the only way to find out what it answers for a given Pending was to run an
// installation. The round that added the named door on the binding screen needed
// exactly that question answered - does taking the door really make this program
// finish 3 rather than 0 - and an expression inside a function nothing can call is
// unreachable from any test, which is the shape this project keeps finding on the
// wrong side of a comment.
//
// It is a PURE FUNCTION OF THE Pending and reads nothing else, which is what lets
// installer/verify ask it about a machine where everything passed and the only thing
// outstanding is a claim somebody made by pressing a button.
//
// THE ORDER OF THE CLAUSES IS NOT A PRIORITY. Any one of them is enough for 3; they
// are written in the order the summary prints them so that the two lists can be read
// against each other.
// ---------------------------------------------------------------------------
static int exitCodeFor(const Pending* p)
{
    if (!p)
        return kExitOk;
    if (p->winUsbBindingMissing ||
        p->deviceNotConnected || p->startControlService ||
        p->controlServiceNotReplaced || needsDjRestart(p) ||
        p->wrongAccount || p->bindingClaimedByHand)
        return kExitPending;
    return kExitOk;
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

// ---------------------------------------------------------------------------
// THE ONE NOTICE THIS PROGRAM IS NOT ALLOWED TO OMIT.
//
// Resource 105 is Microsoft's Windows.Devices.Midi2.dll, and it is the only payload
// in this installer that is somebody else's binary. It is MIT licensed, and MIT's
// one condition is that "the above copyright notice and this permission notice
// shall be included in all copies or substantial portions of the Software".
// Embedding the DLL is making a copy of it; handing somebody BCD3000Setup.exe is
// distributing that copy. So the notice has to be inside the executable, not only
// in a LICENSE file that may not travel with it.
//
// *** IT IS PRINTED RATHER THAN MERELY STORED, AND THAT IS THE POINT. *** A string
// nothing emits is a string the linker may drop and a reader can never see. This
// goes through say(), which means the console, the log file, the window's log pane,
// a screen reader and the clipboard - the same path every other statement in this
// program takes. One function, ONE CALL SITE, at the foot of the payload report,
// because "what is inside this installer" is exactly the question the notice
// answers the legal half of.
//
// WHY THE FULL TEXT AND NOT A LINE POINTING AT LICENSE. Because the condition names
// the permission notice, not a reference to it, and because the failure mode of a
// pointer is silent: an installer distributed on its own would then carry a licence
// obligation and no way for the receiver to read what it is. The whole reason this
// dependency exists is that the library it replaced forbade redistribution; getting
// the replacement's terms right is not a formality here, it is the deliverable.
//
// The wording below is the MIT licence verbatim. It is duplicated in the
// repository's LICENSE file on purpose - that is what a licence notice IS, a copy -
// and installer/verify's "exe" mode reads it back out of the built binary against a
// literal typed in the harness, so the two cannot quietly drift apart.
// ---------------------------------------------------------------------------
static void printThirdPartyNotice()
{
    say(L"--- third party software redistributed by this installer ---");
    say(L"Windows.Devices.Midi2.dll - Windows MIDI Services");
    say(L"https://github.com/microsoft/MIDI");
    sayBlank();
    say(L"MIT License");
    sayBlank();
    say(L"Copyright (c) Microsoft Corporation.");
    sayBlank();
    say(L"Permission is hereby granted, free of charge, to any person obtaining a copy");
    say(L"of this software and associated documentation files (the \"Software\"), to deal");
    say(L"in the Software without restriction, including without limitation the rights");
    say(L"to use, copy, modify, merge, publish, distribute, sublicense, and/or sell");
    say(L"copies of the Software, and to permit persons to whom the Software is");
    say(L"furnished to do so, subject to the following conditions:");
    sayBlank();
    say(L"The above copyright notice and this permission notice shall be included in all");
    say(L"copies or substantial portions of the Software.");
    sayBlank();
    say(L"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR");
    say(L"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,");
    say(L"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE");
    say(L"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER");
    say(L"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,");
    say(L"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE");
    say(L"SOFTWARE.");
    sayBlank();
    say(L"The same notice is in this project's LICENSE file, under \"Third party software");
    say(L"redistributed by this project\". Everything else in this installer is this");
    say(L"project's own work and is MIT licensed too, Copyright (c) 2026 MatroX.");
    sayBlank();
}

// What this particular installer is carrying. Printed because "the installer ran"
// and "the installer contained the build you meant" are two different statements,
// and only the sizes can tell them apart after the fact.
//
// *** FIVE ROWS NOW, AND THE COUNT COMES FROM sizeof RATHER THAN FROM A 3 TYPED
//     TWICE. *** It was `static const Item items[3]` with `i < 3` beside it, so
// adding a payload and forgetting the loop bound would have printed four of five
// and reported nothing missing. The two DLLs added here are exactly the edit that
// would have hit it.
static void printPayloadReport()
{
    say(L"--- what is inside this installer ---");
    struct Item { int id; const wchar_t* name; };
    static const Item items[] = {
        { IDR_PAYLOAD_ASIO_DLL,    L"BcdAsio.dll (the ASIO driver)" },
        { IDR_PAYLOAD_BRIDGE_EXE,  L"BCD3000Bridge.exe (controls and LEDs)" },
        { IDR_PAYLOAD_UNINSTALLER, L"BCD3000Uninstall.exe" },
        { IDR_PAYLOAD_MIDI_DLL,    L"BcdMidi.dll (the virtual MIDI port)" },
        { IDR_PAYLOAD_WINMIDI_DLL, L"Windows.Devices.Midi2.dll (Microsoft's, MIT)" }
    };
    const int count = (int)(sizeof(items) / sizeof(items[0]));
    for (int i = 0; i < count; i++) {
        const void* data = 0;
        DWORD       size = 0;
        if (loadPayload(items[i].id, &data, &size))
            sayInfo(L"%s: %lu bytes", items[i].name, size);
        else
            sayFail(L"%s: MISSING from this build", items[i].name);
    }
    sayBlank();

    // The only call site. See the block over the function for why it is here and
    // not somewhere a reader has to go looking for.
    printThirdPartyNotice();
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
    say(L"  /replace-service  allow replacing the control service's files while it is");
    say(L"                    running: BCD3000Bridge.exe and the two DLLs beside it. Any");
    say(L"                    one of them being out of date is enough for this to have");
    say(L"                    something to do. It stops the service, which destroys the");
    say(L"                    virtual MIDI port, so any open DJ application has to be");
    say(L"                    restarted afterwards.");
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
// THE TWO NAMES THIS PROGRAM PUTS NEXT TO THE DRIVER, IN A FUNCTION SO THAT THE
// PROGRAM THAT DELETES THEM CAN BE COMPARED WITH THE PROGRAM THAT WRITES THEM.
//
// These four lines used to sit inline in installDriver(), as locals, which made them
// unreachable from anything but installDriver() itself. That mattered for one reason:
// uninstall.cpp derives the SAME two names a second time, in computeRemovalPaths(), and
// nothing anywhere compared the two derivations. The failure mode of a disagreement is
// silent and expensive - if this file wrote ".bak" and the uninstaller looked for
// ".bkp", the uninstaller's own checks would still agree with themselves and the
// install folder would survive every uninstall with a stray file in it, which is
// exactly the state that stops RemoveDirectoryW.
//
// So the derivation has a name and can be called. installer/verify calls this function
// and computeRemovalPaths() on the same input and compares the results. Neither side of
// that comparison is a literal somebody typed, which is the only way the check can fail
// for the reason it exists.
//
// THE LENGTH GUARD MOVED IN HERE WITH THEM, unchanged - same 8, same >=, same answer -
// because a caller that got the guard right and the format wrong, or the other way
// round, is the shape of thing this consolidation exists to remove. The message stays
// with the caller: this function reports "no" and the caller says what "no" means to
// the person reading, which is the split every other helper in this file uses.
//
// *** THE TWO OUT PARAMETERS ARE ARRAY REFERENCES AND NOT POINTERS, AND THAT IS THE
//     WHOLE OF THE ANSWER TO A HAZARD THE INLINE VERSION COULD NOT HAVE HAD. ***
// Every write below is bounded by kPathMax, including the unconditional
// staging[kPathMax - 1] = 0 - so with plain wchar_t* the size lived in this function
// while the buffer lived in the caller, and stagingAndBackupPaths(x, small, small2)
// would have written 1023 elements into whatever the caller happened to have. Nothing
// in the signature said otherwise. When these four lines were locals of installDriver()
// the question could not arise: the buffer and the writes were the same three lines of
// code. Extracting them is what opened it, so it is closed here rather than left to a
// convention.
//
// A size parameter was the other candidate and is worse for this function, because a
// size parameter is checked at RUN TIME and only on the path that runs. wchar_t
// (&)[kPathMax] is checked by the compiler, at every call site, on every build,
// including the ones nobody exercises - and it costs the callers nothing, because all
// three already pass a wchar_t[kPathMax] local. An assert was the third candidate and
// asserts nothing useful here: by the time one could fire the overflow has happened.
//
// *** WHAT THIS DOES NOT CLOSE, WRITTEN DOWN BECAUSE NO SIGNATURE CAN SAY IT. ***
// Aliasing. stagingAndBackupPaths(p, buf, buf), or passing the caller's own dllTarget
// buffer as one of the two outputs, is still accepted by the compiler and is still
// undefined behaviour in _snwprintf, which may not read a source it is writing over.
// THE CONTRACT IS: dllTarget, staging and backup must be three distinct buffers. All
// three call sites were read and all three satisfy it - installDriver() passes two
// fresh locals, and installer/verify passes two fresh locals in each of its two calls.
// ---------------------------------------------------------------------------
static bool stagingAndBackupPaths(const wchar_t* dllTarget,
                                  wchar_t (&staging)[kPathMax],
                                  wchar_t (&backup)[kPathMax])
{
    if (wcslen(dllTarget) + 8 >= kPathMax)
        return false;
    _snwprintf(staging, kPathMax - 1, L"%s.new", dllTarget);
    staging[kPathMax - 1] = 0;
    _snwprintf(backup, kPathMax - 1, L"%s.bak", dllTarget);
    backup[kPathMax - 1] = 0;
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
        wchar_t staging[kPathMax];
        wchar_t backup[kPathMax];
        if (!stagingAndBackupPaths(s->dllTarget, staging, backup)) {
            sayFail(L"the driver path is too long to stage a new file next to it");
            return false;
        }

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
// THE THREE FILES THIS HALF OF THE INSTALL OWNS, IN ONE TABLE, DECIDED TOGETHER.
//
// *** WHAT WENT WRONG, AND IT IS THE WHOLE REASON THIS IS A TABLE. *** The two DLLs
// were added in a loop that ran AFTER the control service's own three-way branch, and
// read a flag - serviceStillRunning - that the branch only cleared on the path where
// it stopped the service. So on the path where BCD3000Bridge.exe was already
// byte-identical, nothing stopped the service, the flag stayed true, and a DLL that
// differed printed "the control service is RUNNING ... run BCD3000Setup.exe
// /replace-service". Running that re-entered the SAME branch, because the bridge was
// still identical, and printed the same sentence. For ever. The only way out was to
// kill the process by hand or sign out; closing the DJ software does nothing, because
// the control service is started by the Startup shortcut and not by the DJ software.
//
// AND THE CASE IT BROKE IS THE ORDINARY ONE FROM NOW ON, not an edge case. BcdMidi.dll
// is a small C++ unit rebuilt far more often than the PyInstaller bundle, so "the DLL
// changed and the bridge did not" is the normal shape of an update. Design decision D4
// says the entire point of wrapping Microsoft's preview API behind a DLL of ours is
// that "a change costs one file" - and the defect made that one file the only one the
// installer could not deliver. The architecture's stated benefit was cancelled by a
// branch order.
//
// *** THE FIX IS THAT THE DECISION IS TAKEN ONCE, OVER ALL THREE FILES, BEFORE ANY OF
//     THEM IS TOUCHED. *** The disk is read for all three, that produces a bitmask of
// what differs, and planServiceInstall() answers what to do with the SET. There is no
// per-file "is it still running" question left to get wrong, because there is no
// per-file decision any more.
//
// The order is the order things are written in and the order the messages appear; the
// bridge is first because it is the thing the Startup shortcut points at.
// ---------------------------------------------------------------------------
struct ServiceFile {
    int            payloadId;
    const wchar_t* name;   // the leaf name it has on disk, inside the service folder
    const wchar_t* what;   // what it IS, for a message a user can act on
};

static const ServiceFile kServiceFiles[] = {
    { IDR_PAYLOAD_BRIDGE_EXE,  kBridgeExeFileName,
      L"the control and LED service" },
    { IDR_PAYLOAD_MIDI_DLL,    L"BcdMidi.dll",
      L"the virtual MIDI port" },
    { IDR_PAYLOAD_WINMIDI_DLL, L"Windows.Devices.Midi2.dll",
      L"Microsoft's Windows MIDI Services runtime" }
};
static const int kServiceFileCount = (int)(sizeof(kServiceFiles) /
                                           sizeof(kServiceFiles[0]));

enum ServicePlan {
    kServicePlanNothingToDo = 0,  // every file on disk is already the payload
    kServicePlanWrite       = 1,  // write the ones that differ, stopping first if running
    kServicePlanRefuse      = 2   // something differs, service running, no /replace-service
};

// ---------------------------------------------------------------------------
// 0, 1 OR 2, AS A PURE FUNCTION, SO THAT SOMETHING CAN ASK IT.
//
// The same argument exitCodeFor() carries: the only way to find out what a branch
// answers for a given machine is otherwise to run an installation, and this is a
// decision that has already been got wrong once in exactly the way a truth table would
// have caught. It reads NOTHING but its three arguments.
//
// differsMask has bit i set when kServiceFiles[i] on disk is not the payload this
// installer carries. Which bit is set is deliberately NOT part of the decision: any
// file being out of date needs the same treatment, because they all live in the folder
// the running control service holds open, and a plan that treated "only a DLL changed"
// as a lesser case is precisely the defect this replaced.
//
// *** THE INVARIANT THE HARNESS ASSERTS, WRITTEN HERE SO IT CAN BE READ AGAINST THE
//     CODE: there is NO non-empty differsMask for which /replace-service is refused.
//     *** That is the property whose absence was the defect. If a later edit adds a
// fourth answer or a special case, that assertion fails rather than a user re-reading
// the same advice for ever.
// ---------------------------------------------------------------------------
static ServicePlan planServiceInstall(unsigned differsMask, bool serviceRunning,
                                      bool replaceAllowed)
{
    if (differsMask == 0)
        return kServicePlanNothingToDo;
    if (serviceRunning && !replaceAllowed)
        return kServicePlanRefuse;
    return kServicePlanWrite;
}

// ---------------------------------------------------------------------------
// Step 2: the control service, the two DLLs beside it, and its Startup shortcut
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

    // -----------------------------------------------------------------------
    // READ ALL THREE FILES BEFORE DECIDING ANYTHING ABOUT ANY OF THEM.
    //
    // *** "BESIDE THE BRIDGE" IS THE WHOLE OF THE DEPLOYMENT FOR THE TWO DLLs, AND IT
    //     WAS MEASURED RATHER THAN CHOSEN. *** BcdMidi.dll reaches Windows MIDI
    // Services through C++/WinRT, whose activation falls back to
    // DllGetActivationFactory on a DLL sitting in the process directory. There is no
    // manifest to write, nothing to register, and no system directory to touch. The
    // packaged control service is a PyInstaller one-file build and its bootloader calls
    // SetDllDirectoryW(_MEIPASS), so the same rule holds after unpacking. Measured on
    // 2026-08-01.
    //
    // Ours without Microsoft's is a control service that starts, loads, and then fails
    // at the first MIDI call - the worst of the three possible outcomes, because it
    // looks like a working install. That is a second reason the three are decided
    // together rather than one at a time.
    //
    // *** ROW 0's PATH IS s->bridgeTarget AND IS NOT DERIVED AGAIN HERE. *** Every other
    // part of this program and the whole of the uninstaller compare against that value.
    // A second derivation of a path this project has already decided must have exactly
    // one is the failure stagingAndBackupPaths()'s block is about. The table's leaf name
    // is still kBridgeExeFileName, which is the name bridgeExePath() joins - and
    // installer/verify compares those two derivations rather than trusting this comment.
    // -----------------------------------------------------------------------
    const void* payload[kServiceFileCount];
    DWORD       payloadN[kServiceFileCount];
    wchar_t     targetPath[kServiceFileCount][kPathMax];
    unsigned    differs = 0;

    for (int i = 0; i < kServiceFileCount; i++) {
        payload[i]  = 0;
        payloadN[i] = 0;
        if (!loadPayload(kServiceFiles[i].payloadId, &payload[i], &payloadN[i])) {
            sayFail(L"this installer has no %s payload in it - the build is broken",
                    kServiceFiles[i].name);
            return false;
        }
        if (i == 0) {
            wcsncpy(targetPath[i], s->bridgeTarget, kPathMax - 1);
            targetPath[i][kPathMax - 1] = 0;
        } else if (!joinPath(targetPath[i], kPathMax, dir, kServiceFiles[i].name)) {
            sayFail(L"could not work out where to put %s", kServiceFiles[i].name);
            return false;
        }
        if (!fileHasContent(targetPath[i], payload[i], payloadN[i]))
            differs |= (1u << i);
    }

    const ServicePlan plan = planServiceInstall(differs, s->bridge.running,
                                                opt->replaceBridge);

    if (plan == kServicePlanRefuse) {
        // Refusing is the safe answer, not the lazy one. Replacing any of these means
        // stopping the process, and stopping the process destroys the virtual MIDI port
        // that an open DJ application is bound to.
        //
        // *** THE MESSAGE NAMES THE FILES, WHICH IS WHAT MAKES THE ADVICE ACTIONABLE.
        //     *** It used to say "the installed control service differs", which was a
        // sentence about BCD3000Bridge.exe printed on runs where the only thing out of
        // date was a DLL - so a user who compared the bridge's timestamps found nothing
        // wrong and had no way to tell what the installer meant.
        wchar_t list[512];
        list[0]  = 0;
        int named = 0;
        for (int i = 0; i < kServiceFileCount; i++) {
            if (!(differs & (1u << i)))
                continue;
            if (named && wcslen(list) + 2 < 500)
                wcscat(list, L", ");
            if (wcslen(list) + wcslen(kServiceFiles[i].name) < 500)
                wcscat(list, kServiceFiles[i].name);
            named++;
        }
        sayWarn(L"%d of the %d files this installer carries for the control service "
                L"differ from what is on disk, but the service is RUNNING and holds "
                L"them open, so NOTHING was replaced: %s",
                named, kServiceFileCount, list);
        sayInfo(L"stopping it destroys the virtual MIDI port, and any DJ application that "
                L"is open would stop seeing the controller until it is restarted");
        sayInfo(L"to replace them anyway, run the line below. It stops the control "
                L"service first, which is the only thing that lets these files be "
                L"written, and it does that however many of them are out of date:");
        sayInfo(L"    BCD3000Setup.exe /replace-service");
        // *** AND NOT "CLOSE YOUR DJ SOFTWARE", WHICH WAS ADVICE THAT DID NOTHING. ***
        // The control service is started by the Startup shortcut, not by the DJ
        // software, so closing the DJ software does not stop it and never did. What
        // closing the DJ software is genuinely for is losing less when the port goes.
        sayInfo(L"closing your DJ software first is worth doing - it is what loses the "
                L"port - but it is not what frees the files: only stopping the control "
                L"service does that, and only the switch above stops it.");
        pending->controlServiceNotReplaced = true;
    } else {
        if (plan == kServicePlanWrite && s->bridge.running) {
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
            //
            // *** THIS SENTENCE PROMISED THE PORT BACK, AND IT PROMISED IT ON THE
            //     ONE PATH WHERE THE PROMISE IS LEAST LIKELY TO HOLD. *** It read
            // "It exists again once the control service is running". Reaching this
            // line means a control service was RUNNING and has just been stopped -
            // which means a virtual port had been created this boot and has just
            // been closed, and that is precisely the precondition for
            // microsoft/MIDI issue #1047. So the old wording made its strongest
            // claim exactly where the defect bites hardest: a user updating the
            // product, which is one of the four cases design decision D5 names by
            // name. It has no reading behind it either - this program never
            // creates a port, so it cannot know. See the block over WinMidiInfo.
            sayInfo(L"the virtual MIDI port went with it. Whether starting the "
                    L"control service creates it again was NOT read by this "
                    L"installer - it never creates a port to find out - and a DJ "
                    L"application that was open has to be restarted either way.");
            // ...and on a machine whose build is on the known-bad list, the honest
            // answer is not silence: what clears it is a restart, and this is the
            // moment the user can act on that.
            if (classifyWindowsMidi(&s->winMidi) == kWinMidiKnownBad) {
                sayWarn(L"this Windows build (%lu.%lu) carries microsoft/MIDI issue "
                        L"#1047, where only the FIRST virtual port after a restart "
                        L"can be created. Restart the machine to get the port back.",
                        s->winMidi.serviceVersion[2], s->winMidi.serviceVersion[3]);
            }
            pending->midiPortDestroyed = true;
        }

        // ONE LOOP FOR ALL THREE, AND IT DOES NOT ASK ANYTHING ABOUT THE SERVICE.
        // By this point the plan has already settled whether anything may be written
        // and, if so, that the service has been stopped. A file that does not differ is
        // reported and skipped; a file that differs is written. There is no third case
        // left for a later edit to get wrong.
        //
        // The size in each message is the PAYLOAD's, which is what makes "the installer
        // contained the build you meant" answerable afterwards from the log alone.
        for (int i = 0; i < kServiceFileCount; i++) {
            if (!(differs & (1u << i))) {
                sayOk(L"%s is already identical (%lu bytes) - not rewritten",
                      kServiceFiles[i].name, payloadN[i]);
                continue;
            }
            if (!writeFileAtomic(targetPath[i], payload[i], payloadN[i], &err)) {
                sayFail(L"could not write %s (%s)", targetPath[i], winErrText(err));
                if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED)
                    sayInfo(L"the file is in use. Run with /replace-service, or sign out "
                            L"and in again and run the installer before starting "
                            L"anything else.");
                return false;
            }
            sayOk(L"%s written: %s (%lu bytes)", kServiceFiles[i].what, targetPath[i],
                  payloadN[i]);
        }

        // *** THE SERVICE HAS TO BE STARTED WHENEVER IT IS NOT RUNNING NOW. *** Two
        // ways to be in that state: this run wrote something, which means it stopped
        // the service if one was up; or nothing needed writing and none was running to
        // begin with. The one case that must NOT set it is "nothing to do and it is
        // already running", where telling the user to start what is already started is
        // a pending item that can never be cleared.
        if (plan == kServicePlanWrite || !s->bridge.running)
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

    // The two files that go BESIDE the control service. They are recorded for the same
    // reason every other path here is: this file's own header calls itself the record of
    // what the installer was about to use, and a record that lists two of the three
    // files the per user half installs is a record somebody will trust and be wrong.
    //
    // The leaf names come from kServiceFiles[] rather than being typed again here, so
    // renaming a file is still one edit. WHAT THIS DOES NOT DO, said rather than implied:
    // a fourth row added to that table would still need a line of its own below. There
    // is no loop that could avoid that, because the manifest is a named-key format and
    // the keys are part of its contract with whoever reads it.
    wchar_t midiDll[kPathMax];
    wchar_t winMidiDll[kPathMax];
    {
        wchar_t bdir[kPathMax];
        bool    haveDir = bridgeDirPath(bdir, kPathMax);
        if (!haveDir || !joinPath(midiDll, kPathMax, bdir, kServiceFiles[1].name))
            wcscpy(midiDll, L"(unknown)");
        if (!haveDir || !joinPath(winMidiDll, kPathMax, bdir, kServiceFiles[2].name))
            wcscpy(winMidiDll, L"(unknown)");
    }

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
        L"midi_port_dll=%s\r\n"
        L"windows_midi_runtime=%s\r\n"
        L"startup_shortcut=%s\r\n"
        L"previous_asio_registration=%s\r\n"
        L"\r\n"
        L"# windows_midi_runtime is Microsoft's Windows.Devices.Midi2.dll, redistributed\r\n"
        L"# under the MIT licence, Copyright (c) Microsoft Corporation. The notice is in\r\n"
        L"# this project's LICENSE and BCD3000Setup.exe prints it in full.\r\n"
        L"\r\n"
        L"# Not installed by this product and never removed by its uninstaller:\r\n"
        L"#   the WinUSB binding on the device (done once by hand with Zadig)\r\n",
        kProductName, BCD_VERSION_WSTR, BCD_VERSION_WSTR,
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        s->account.tokenAccount[0] ? s->account.tokenAccount : L"(unknown)",
        kAsioClsid, kAsioRegName,
        s->dllTarget, s->bridgeTarget, midiDll, winMidiDll, lnk,
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
// HOW THE TWO PREREQUISITES ARE DONE - the walkthrough page 2 exists to carry
//
// WHY THIS IS say() AND NOT A PAINTED BLOCK, which is the same argument 490904f
// settled for the warnings block and is repeated here because the temptation is
// bigger on page 2: the text is next to the rows it is about, so painting it looks
// natural. Four reasons it would be worse. (a) A painted block dies when the window
// closes, and this is the one text in the program a user needs while they are doing
// something ELSE - downloading a prerequisite, reading Zadig's list. (b) It is invisible
// to a screen reader; gui.cpp says so in its own header. (c) It does not exist in
// /console, which is how a run is verified and how a user sends what happened with
// a support request. (d) It would be the only text in this program that a small
// window can clip, because an EDIT with ES_MULTILINE and no ES_AUTOHSCROLL wraps
// and its WS_VSCROLL makes the overflow reachable, and painted text does neither.
//
// The window shows these same bytes in page 2's pane - not a second copy of them:
// setup.cpp captures the lines this function emits, through the sink every other
// line already goes through, and hands that buffer to the wizard. So the pane, the
// console, the log file and the clipboard cannot drift apart.
//
// THE ORDER IS THE CONTENT. Step 0 is the owner's correction and it earns its place
// twice over: Windows has to enumerate the mixer once before Zadig has anything to
// replace, AND enumKeyPresent means literally "this machine has seen this device at
// least once", so until the mixer has been plugged in once the WinUSB row cannot
// tell "never seen" from "seen, but not bound". Plugging in first separates the two
// facts the row today has to say in one breath.
//
// THE ONE ORDERING THAT IS A HABIT SAYS SO IN THE TEXT. Steps 1 and 2 are
// independent - a virtual MIDI port has nothing to do with the binding on the
// physical device - and the sentence in the step says "reasoned, not measured" in
// those words.
//
// *** THE REASON THAT USED TO BE GIVEN FOR PUTTING 1 FIRST IS NO LONGER TRUE, AND
//     THE ORDER IS KEPT ANYWAY. *** It was "1 installs a kernel component and may
// ask for a restart" - true of the third party driver this product no longer uses,
// and false of Windows MIDI Services, which installs nothing and asks for nothing.
// So step 1 is now first for the weaker reason that the flow, the screens and the
// captures are all numbered around it, and moving it would move four PNGs and a
// screen table to reorder two steps that are independent anyway. That is a habit
// with the cost of changing it written down, which is a different thing from a
// requirement - and the text says so where a reader can see it.
// ---------------------------------------------------------------------------
// STEP 1 OF THE WALKTHROUGH, ON ITS OWN, BECAUSE IT IS A SCREEN NOW.
//
// The paragraphs that used to stand here were the consent panel and the walkthrough
// step for a specific third party virtual MIDI driver - its provenance, its licence,
// its two-file detection and the winget offer built on top of it. They went with that
// dependency. What replaced them is not another install instruction, because there is
// nothing to install: Windows MIDI Services is in-box.
//
// *** THIS TEXT IS THE SAME ON EVERY MACHINE, AND THAT IS THE DIVISION OF LABOUR
//     BETWEEN THIS AND THE ROW ABOVE IT. *** The step body is one function with two
// consumers - the pane and /console - and it takes no MachineState, so it says what
// the STEP is and what the defect is; the ROW, describeMidiPort(), is what says which
// of the three states THIS machine is in. Every sentence here is therefore written so
// that it is true of every machine: the defect is described with the condition on it
// ("on the affected builds"), and the reader is sent to the row for the answer about
// their own. A pane that said "your build is affected" would be a machine-independent
// function claiming a machine-dependent fact.
//
// *** IT IS ONE FUNCTION WITH TWO CONSUMERS AND NOT TWO COPIES OF A PARAGRAPH. ***
// The MIDI port screen's pane is this text and the walkthrough's step 1 is this text,
// and the walkthrough calls it rather than repeating it - so /console prints exactly
// what it printed before, byte for byte, and the screen cannot say something the
// console does not.
//
// The blank line that separates step 1 from step 2 stays with the WALKTHROUGH and is
// not part of this, so the pane neither opens nor ends on an empty line.
// ---------------------------------------------------------------------------
static void printMidiPortStepBody()
{
    say(L"  1. The MIDI port.");
    say(L"     NOTHING TO INSTALL. The port is created through Windows MIDI Services,");
    say(L"     which is part of Windows itself. This step is a reading, not a job:");
    say(L"     this installer looks at whether the %s service is registered,",
        kMidiServiceName);
    say(L"     whether %s is there and what version it", kMidiTransportDllName);
    say(L"     carries, and whether this build is one with a known defect. The row on");
    say(L"     this screen says which of those three answers your machine gave.");
    say(L"     IT NEVER CREATES A PORT, so it never tells you one will work. Creating");
    say(L"     one is the only way to know, and see the next paragraph for why doing");
    say(L"     that here would cost you the port instead of testing it.");
    say(L"     THE KNOWN DEFECT: microsoft/MIDI issue #1047, open since 2026-07-16,");
    say(L"     acknowledged by Microsoft with no fix date. ON THE AFFECTED BUILDS only");
    say(L"     the FIRST virtual port after a restart can be created; once one has been");
    say(L"     created and closed, every later attempt fails until the machine is");
    say(L"     restarted. The defect is in a file that ships inside Windows, so the fix");
    say(L"     arrives through Windows Update and nothing here has to be reinstalled.");
    say(L"     Restarting the machine is what clears it in the meantime. Whether YOUR");
    say(L"     build is one of the affected ones is what the row above answers.");
    say(L"     WHY IT IS PUT BEFORE ZADIG: steps 1 and 2 are otherwise INDEPENDENT - the");
    say(L"     MIDI port has nothing to do with the binding on the physical device.");
    say(L"     This order is reasoned, not measured, and doing 2 first breaks nothing");
    say(L"     anybody here has measured.");
}

// Declared here and defined below, beside the buffer it fills: it is
// printMidiPortStepBody() with the MIDI screen's pane collecting it on the way past.
static void printMidiPortStep();

// ---------------------------------------------------------------------------
// STEP 2 OF THE WALKTHROUGH, AS A FUNCTION, FOR THE REASON STEP 1 IS ONE.
//
// *** ONE FUNCTION WITH TWO CONSUMERS, NOT TWO COPIES OF A PROCEDURE. *** The
// "Get Zadig" screen's pane is this text and the walkthrough's step 2 is this
// text, and the walkthrough calls it rather than repeating it - so /console
// prints exactly what it printed before, byte for byte, and the screen cannot say
// something the console does not.
//
// *** AND OF EVERY PARAGRAPH IN THIS PROGRAM THIS IS THE ONE THAT MUST NOT BE
//     DUPLICATED. *** It is the procedure for the operation this installer itself
//     calls the most dangerous in the whole installation, it names the exact line
// of a third party list to pick and the two look-alikes in that same list that do
// not work, and it carries the way back out for somebody who has already got it
// wrong. A second copy that drifted would be advice about a window nobody has.
//
// THE BLANK LINE that separates step 2 from step 3 stays with the WALKTHROUGH, so
// the pane neither opens nor ends on an empty line.
// ---------------------------------------------------------------------------
static void printZadigStepBody()
{
    say(L"  2. Run Zadig once and bind interface 0 to WinUSB.");
    say(L"     %s", kZadigDownloadPage);
    say(L"     a) Options > List All Devices FIRST. Without it the mixer's separate");
    say(L"        interfaces do not appear in the list at all, and this is the most");
    say(L"        common way this step goes wrong.");
    say(L"     b) Pick the line that reads BCD3000 (Interface 0). Confirm it by the");
    say(L"        USB ID 1397 00BF shown beside it and by MI_00 in the device path.");
    say(L"        Interface 0 is the one: not 1, not 2.");
    say(L"     c) Set the target driver to WinUSB. It is the box on the RIGHT of the");
    say(L"        green arrow, and the small up and down arrows beside that box are");
    say(L"        what change it. The \"More Information\" column further right is a");
    say(L"        list of links, not the selector - this is where people get stuck.");
    say(L"        libusb-win32 and libusbK sit in that same list and neither of them");
    say(L"        works here - the risk in this step is not failing to find WinUSB,");
    say(L"        it is picking something that looks like it.");
    say(L"     d) Two things in that window that look like problems and are not: the");
    say(L"        red cross beside WCID means nothing here, and the third small box");
    say(L"        of the USB ID row reads 00 because that is the interface number,");
    say(L"        which is the one you want.");
    say(L"     e) The way back out, so nobody is stranded: Device Manager, uninstall");
    say(L"        the device with \"delete the driver software\" ticked, unplug it,");
    say(L"        plug it in again, and start this step over.");
    // *** THE PICTURE'S CAVEAT IS SAID HERE AS WELL AS PAINTED UNDER THE PICTURE. ***
    // The caption under the screenshot is painted text, which is the one kind of text
    // in this program a screen reader cannot reach and /console never prints. So the
    // caveat lives here too, where every other word of the walkthrough lives, and the
    // caption is a pointer at the picture rather than the only place these sentences
    // exist. An image that promises one screen and delivers another is the same
    // defect two rounds of this project removed from its text.
    say(L"     WHAT THE PICTURE ON THE BINDING SCREEN SHOWS, AND WHAT IT DOES NOT.");
    say(L"     It was taken on a machine that is ALREADY bound, so two of its fields");
    say(L"     will not match yours and are not meant to: the Driver box on the left");
    say(L"     reads WinUSB there and will name whatever Windows put on the device");
    say(L"     for you (usbaudio or similar), and the button reads Reinstall Driver");
    say(L"     there while yours is more likely to read Replace Driver or Install");
    say(L"     Driver. Zadig picks that word from what is already bound and not");
    say(L"     every variant of it has been seen here, so: it is the large button,");
    say(L"     whatever it is called. The two things you have to MATCH are identical");
    say(L"     in both states, and they are the two the picture is there for - the");
    say(L"     line BCD3000 (Interface 0) in the list, and USB ID 1397 00BF.");
    say(L"     This is the most dangerous operation in the whole installation and it");
    say(L"     is done by hand on purpose: nothing here rebinds a USB device.");
}

// Declared here and defined below, beside the buffer it fills: it is
// printZadigStepBody() with the Zadig screen's pane collecting it on the way past.
static void printZadigStep();


static void printPrerequisiteWalkthrough()
{
    sayBlank();
    say(L"============= HOW THE TWO PREREQUISITES ARE DONE =============");
    say(L"Six steps, in the order they have to happen. Two of them are other");
    say(L"people's programs and neither one is run for you from here: what is");
    say(L"already in place on this machine is marked as such in the checks above.");
    sayBlank();

    say(L"  0. Plug the BCD3000 into a USB 2.0 port and switch it on.");
    say(L"     FIRST, and not out of politeness. Windows has to see the mixer once");
    say(L"     before there is anything for Zadig to replace. It also sharpens what");
    say(L"     the WinUSB check can tell you: Windows records a device the first");
    say(L"     time it is plugged in, so until that has happened once, \"never seen");
    say(L"     by this machine\" and \"seen, but not bound to WinUSB\" cannot be told");
    say(L"     apart - which is why that row has to say both in one sentence.");
    say(L"     USB 2.0 rather than 3.x: it is a USB 1.1 full speed device and its");
    say(L"     audio is isochronous, which some USB 3.x controllers do not carry");
    say(L"     reliably. That is the hardware, and there is no software fix for it.");
    sayBlank();

    printMidiPortStep();
    sayBlank();

    printZadigStep();
    sayBlank();

    say(L"  3. Run this installer and press Install.");
    say(L"     It writes the driver into Program Files, registers it as an ASIO");
    say(L"     driver, and puts the control service into your own profile. It asks");
    // *** IT SAID "steps 1 and 2" AND THE WINDOW REFUSES STEP 2, WHICH MADE THIS
    //     LINE INSTRUCT A SEQUENCE THE PROGRAM THEN BLOCKS. *** It is true of step 1
    // - buildScreens() leaves the MIDI port screen's blockNextWhenUnmet false on
    // purpose, because without the MIDI port the audio still works. It is FALSE of
    // step 2: the binding screen is the one entry in either flow that sets
    // blockNextWhenUnmet, nextAllowed() greys Next and the press is refused a second
    // time, and the only labelled way past is the named door - which writes into the
    // log, under the reader's name, a claim that this reader knows to be untrue.
    // Telling somebody they may skip a step and then making the only exit a false
    // statement is worse than not offering the shortcut at all.
    say(L"     for no restart, and it can be run before step 1 is done: the half it");
    say(L"     owns is real progress on its own. Step 2 is the exception and the");
    say(L"     window says so where it matters: the binding screen will not let you");
    say(L"     past until the binding reads as applied, because without it nothing");
    say(L"     works at all.");
    sayBlank();

    say(L"  4. Sign out and back in, or double click the Startup shortcut.");
    say(L"     The control service starts at every sign in, and it is what creates");
    say(L"     the MIDI port. Until it is running there are no knobs, no buttons and");
    say(L"     no LEDs. If you start it by hand, start it from Explorer and not from");
    say(L"     an administrator prompt: it has to run unelevated for the driver to");
    say(L"     reach it.");
    sayBlank();

    say(L"  5. Open your DJ software, choose the driver, and ignore the band that");
    say(L"     offers to download drivers.");
    say(L"     That band is a fixed property of the DJ software's own controller");
    say(L"     definition and it is not looking at your machine. Pressing it");
    say(L"     installs the manufacturer's 2010 package, which Windows then matches");
    say(L"     over the WinUSB binding from step 2, and both halves die at once.");
    say(L"     It is the one action that undoes everything above, and it is item 1");
    say(L"     of the warnings printed at the end of an install.");
}

// ---------------------------------------------------------------------------
// THE WORDS A PANE SHOWS, COLLECTED ON THEIR WAY TO THE CONSOLE.
//
// *** IT IS A TEE, NOT A SECOND PRINT. *** The lines go out through say() exactly
// once and reach everything they always reached - the console, the log file, and
// whatever sink the window installed - and this collects them ON THE WAY PAST. So
// a pane cannot say anything the log file does not, in different words or in a
// different order, which is the failure that two copies of a paragraph always
// eventually produce.
//
// *** THERE ARE TWO PANES NOW, AND ONE IS INSIDE THE OTHER. *** The MIDI port has a
// screen, its pane is step 1 of the walkthrough, and the walkthrough still prints
// step 1 in its place - so the same lines have to land in two buffers on one pass.
// That is why the collector is a STACK rather than a single buffer: the inner capture
// pushes, the outer stays pushed, and every active buffer gets the line. The obvious
// alternative - the inner capture chaining to the outer's sink - cannot work here at
// all, because both sinks would be this same function and it would call itself for
// ever. Measured by reading it, not by running it into a stack overflow.
//
// A buffer is a fixed block and it TRUNCATES rather than growing. Each is sized for
// several times what its emitter produces, and text that outgrew one would lose its
// tail in that pane while the console and the log kept all of it - a visible, bounded
// failure rather than an allocation on a path that has no way to report one.
// ---------------------------------------------------------------------------
struct PaneBuf {
    wchar_t* text;
    int      cap;    // the index the terminator is forced to; not the array size
    int      len;
    bool     full;
};

static wchar_t g_midiPaneText[8192];    // step 1 alone: the MIDI port screen's pane
static wchar_t g_zadigPaneText[8192];   // step 2 alone: the Get Zadig screen's pane

static PaneBuf g_midiPortStep = { g_midiPaneText, (int)(sizeof(g_midiPaneText) /
                                  sizeof(g_midiPaneText[0])) - 4, 0, false };
static PaneBuf g_zadigStep    = { g_zadigPaneText, (int)(sizeof(g_zadigPaneText) /
                                  sizeof(g_zadigPaneText[0])) - 4, 0, false };

static PaneBuf* g_paneCur  = 0;
static LineSink g_paneNext = 0;

static void paneTee(LineKind kind, const wchar_t* prefix, const wchar_t* body)
{
    PaneBuf* b = g_paneCur;
    if (b) {
        int n = (int)wcslen(prefix) + (int)wcslen(body) + 2;
        if (b->len + n < b->cap) {
            _snwprintf(b->text + b->len, (size_t)(b->cap - b->len), L"%s%s\r\n",
                       prefix, body);
            b->text[b->cap] = 0;
            b->len          = (int)wcslen(b->text);
        } else {
            b->full = true;
        }
    }
    if (g_paneNext)
        g_paneNext(kind, prefix, body);
}

// Run `emit`, and keep every line it says in `b` as well as sending it on.
//
// ONE CAPTURE AT A TIME, and that is a real bound rather than a simplification: the
// only nesting that could arise is a pane whose emitter is called from inside another
// pane's emitter, and the obvious way to write that - the inner capture chaining to
// the sink it found installed - cannot work, because that sink is this same function
// and it would call itself for ever. A second concurrent capture is refused here
// instead, so the failure is a pane with no text rather than a stack overflow. When a
// later task really needs two at once, this becomes a small array and the loop above
// walks it; it is not one today, and a stack nothing pushes twice would be machinery
// with no user.
static void captureInto(PaneBuf* b, void (*emit)(void))
{
    b->len     = 0;
    b->text[0] = 0;
    b->full    = false;
    if (g_paneCur) {
        emit();          // nothing is lost from the console or the log file
        return;
    }
    g_paneCur  = b;
    g_paneNext = lineSink();
    setLineSink(paneTee);
    emit();
    setLineSink(g_paneNext);
    g_paneNext = 0;
    g_paneCur  = 0;
    if (b->full)
        sayWarn(L"the text of one of the window's panes is longer than the pane can "
                L"hold, so it shows the start of it. All of it is above, and all of it "
                L"is in the log file.");
}

static void printMidiPortStep()
{
    captureInto(&g_midiPortStep, printMidiPortStepBody);
}

// *** THE SECOND PANE FED BY THE SAME TEE, AND THE COMMENT ABOVE captureInto() SAID
//     THIS DAY WAS COMING. *** It refuses a SECOND CONCURRENT capture - the inner
// emitter runs and its lines reach the console and the log file, but no buffer - and
// that bound still holds, because these two captures do not nest: the walkthrough
// calls printMidiPortStep() and printZadigStep() one after the other, each opening
// and closing its own capture. Two buffers is not two at once.
static void printZadigStep()
{
    captureInto(&g_zadigStep, printZadigStepBody);
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
// component and nothing that loads before sign in, so there is nothing a restart
// could change.
//
// IT SAYS "boot" NOWHERE ANY MORE, AND THAT IS THE POINT. It used to say "nothing
// that loads at boot time" while item 3, welcome bullet 2 and the shortcut this
// installer writes all say the control service starts at every sign in. Boot is not
// sign in, so the sentence was literally defensible - and this screen is read by
// somebody who is not technical, for whom the distinction does not exist. It also
// used to call closing and reopening the DJ software "the whole of what is needed
// here", a few lines under a numbered list of things that are still the reader's to
// do: two numbered lists in one screen disagreeing about what "the whole" is. The
// scope of item 8 is RESTARTING, and it now says only that.
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
    // *** THIS ITEM SAID "not checked by this installer yet" AND THAT STOPPED BEING
    //     TRUE IN THE ROUND THAT GAVE SCREEN 3 ITS NEW SUBJECT. *** It is checked
    // now - see describeMidiPort() - and a warnings block that disagreed with the
    // screen would be one of the eight items lying, which is what item 8's own
    // comment says costs the reader's belief in the other seven. Four lines, as
    // before, so the summary pane's line count and its clip do not move.
    say(L"  6. The MIDI port comes from Windows MIDI Services, part of Windows itself.");
    say(L"     There is nothing to install for it. This installer reads whether the");
    say(L"     service and its transport are there and whether this build is one with");
    say(L"     a known defect; it never creates a port, so it never promises you one.");
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
    say(L"     component and nothing that loads before you sign in. That is also why");
    say(L"     Secure Boot and memory integrity are still on - see the reason this");
    say(L"     project exists. The only thing that has to be started again is your");
    say(L"     DJ software: see 2. That is about RESTARTING and nothing else - if the");
    say(L"     list under \"Still on your side\" above has items in it, those are");
    say(L"     still yours to do.");
    say(L"     Measured, not assumed: on the machine this was built on, two complete");
    say(L"     runs of this installer, five loads of the driver by a host program, an");
    say(L"     83 minute audio run and a pulled cable test all happened inside ONE");
    say(L"     session of Windows, with no restart anywhere in it.");
    say(L"     ONE OTHER THING CAN ASK FOR A RESTART, and it is not this installer:");
    say(L"     Zadig (7), which does rebind a USB device. If it asks, it is asking");
    say(L"     for itself.");
}

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------

// *** WHICH MIXER THIS RUN WAS TOLD IT IS FOR, DECLARED HERE BECAUSE printSummary()
//     BELOW IS ITS CALLER AND THE DEFINITION IS 1400 LINES DOWN. ***
//
// Defined inside `namespace bcdsetup` beside chooseModel(), which is where the Run and
// the choice live. The declaration has to name that namespace or it declares a different
// function and the name does not resolve - the same rule the harness conventions record
// for bcdsetup::buildScreens.
//
// *** AND IT IS DELIBERATELY NOT IN A HEADER. *** The plan publishes it as
// "Produces: int bcdsetup::selectedModel()", and for one round it was published exactly
// that way and called by NOBODY - the twelfth "declared and unread" in this project, and
// the report of that round claimed a consumer it did not have. Its one caller is in this
// file, so a header entry would be a published interface with no external consumer, which
// is the same defect one level up. When something outside setup.cpp needs it, that is the
// round that moves this line into common.h.
namespace bcdsetup { int selectedModel(void); }

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

    // ---------------------------------------------------------------------
    // *** WHICH MIXER THIS RUN WAS SET UP FOR, AND THIS IS selectedModel()'s ONE
    //     CALLER. ***
    //
    // The device screen's two radio buttons change nothing about what gets installed -
    // the driver carries both profiles and matches on the usb ids at run time - so the
    // RECORD is the whole product of that screen. A record that reached only a log line
    // nobody reads back would be the "declared and unread" shape this project has now
    // counted twelve times, which is exactly what selectedModel() was for one round: a
    // published function with no caller anywhere.
    //
    // IT IS HERE AND NOT IN THE NUMBERED LIST BELOW, deliberately. "Still on your side"
    // is a list of ACTIONS, its count is asserted by installer\verify, and this is not an
    // action - it is a fact about what was assumed. Putting it in that list would also
    // renumber every item under it.
    //
    // *** AND THE UNPROVEN MODEL GETS A WARNING RATHER THAN A LINE. *** This is the last
    // place anybody reads before sending a support request, and "this was set up for a
    // mixer nobody has ever run this driver on" is the single most useful sentence such a
    // request can carry. Asked of modelProvenOnHardware() and not of the index, for the
    // reason chooseModel() asks it that way: the flag is the fact, and an index compared
    // against a literal is a second place to encode it.
    // ---------------------------------------------------------------------
    sayBlank();
    if (modelProvenOnHardware(selectedModel())) {
        say(L"This installation was set up for a %s, the model this driver was proven on.",
            modelName(selectedModel()));
    } else {
        sayWarn(L"This installation was set up for a %s. That path is EXPERIMENTAL and "
                L"has never been run on real hardware by anybody on this project: the "
                L"control surface is not supported yet, so the knobs, buttons and LEDs "
                L"will not work, and the audio is expected to work rather than known to. "
                L"Nothing installed above is different because of this - the driver "
                L"carries both profiles and matches on the USB ids when it runs.",
                modelName(selectedModel()));
    }

    sayBlank();
    say(L"Still on your side:");
    int items = 0;

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
        // *** IT SAID "The control service on disk is out of date", AND THAT IS ONE OF
        //     THREE FILES. *** The installer carries BCD3000Bridge.exe and the two DLLs
        // beside it, and any of them being out of date reaches this line. A reader who
        // was told the service was stale, checked it, and found it identical had been
        // sent to look at the wrong file. The console names which ones; this is the
        // summary, so it names the set.
        say(L"  %d. One or more of the control service's files on disk are out of date, "
            L"and", items);
        say(L"     the service was left RUNNING, which is what holds them open. The");
        say(L"     lines above say which files. Run:");
        say(L"       BCD3000Setup.exe /replace-service");
        say(L"     That stops the control service, which is the only thing that frees");
        say(L"     the files. Closing your DJ software does not stop it - the Startup");
        say(L"     shortcut does - but closing it first is still worth doing, because");
        say(L"     stopping the service destroys the virtual MIDI port.");
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
            // *** THE SENTENCE THAT USED TO STAND HERE PROMISED THE PORT BACK, AND
            //     THIS IS THE WORST PLACE IN THE PROGRAM TO PROMISE IT. *** It read
            // "The port exists again once the service is running; only then can a DJ
            // application find the controller." Two things are wrong with it and the
            // second is the one that decides:
            //
            //   1. This program has no reading of whether a port exists. It never
            //      creates one, so it cannot know - the same rule commit 13e374a
            //      established for every other sentence on every other screen.
            //   2. p->midiPortDestroyed is only ever set where this run STOPPED A
            //      RUNNING control service. A running service means a port had been
            //      created this boot; stopping it closed that port. That is exactly
            //      the precondition for microsoft/MIDI issue #1047, under which no
            //      later port can be created until the machine restarts. So the
            //      claim was made where it is LEAST likely to be true, on the update
            //      path design decision D5 names by name.
            //
            // What it says now: what happened, what was not looked at, and - only
            // when the build reading supports it - the one thing that clears it.
            say(L"     The control service was stopped so that its file could be replaced,");
            say(L"     and stopping it destroyed the virtual MIDI port. Whether starting the");
            say(L"     service creates the port again was NOT read by this installer: it");
            say(L"     never creates a port, so it cannot tell you.");
            if (classifyWindowsMidi(&s->winMidi) == kWinMidiKnownBad) {
                say(L"     AND THIS BUILD IS ONE WHERE IT PROBABLY WILL NOT. Windows %lu.%lu",
                    s->winMidi.serviceVersion[2], s->winMidi.serviceVersion[3]);
                say(L"     carries microsoft/MIDI issue #1047: only the FIRST virtual port");
                say(L"     after a restart can be created, and this run just closed one.");
                say(L"     Restart the machine - that is what brings the port back.");
            }
        // *** FIX ROUND 1: THE PREVIOUS VERSION OF THIS COMMENT WAS WRONG, AND A
        //     REVIEWER CAUGHT IT WITH A MEASUREMENT FROM THIS MACHINE. *** It argued
        // that restoring the bare `else` here was safe because Tasks 1-4 moved the
        // bridge onto Windows MIDI Services, "which needs no separate install this
        // program can fail to find" - so there was supposedly no longer a mechanism
        // under which the control service runs and cannot create a port. That is
        // false. microsoft/MIDI issue #1047 (open, acknowledged, no fix date) means
        // that after the first virtual port of a boot is closed, every later
        // creation attempt fails until reboot - measured twice on the owner's own
        // machine this session, after four more the session before. That is a live,
        // common, named mechanism under which the service is running and CANNOT
        // create a port, and it is the defining risk of this whole migration -
        // design's D5 is the owner's written acceptance of exactly it.
        //
        // *** AND THE ROUND THAT WROTE THAT LEFT THE FALSE SENTENCE ABOVE STANDING,
        //     WHICH IS THE PART WORTH REMEMBERING. *** It correctly refused to add a
        // claim in the `else` branch - the one about a port this run did NOT touch -
        // and it did not go back and read the branch three lines up that had been
        // claiming the port back, unconditionally, since long before any of this.
        // A rule applied to the code you are writing and not to the code beside it
        // leaves the defect where it was.
        //
        // *** WHAT IS SAID NOW, AND WHY IT IS ENTITLED TO SAY IT. *** This program
        // still has NO reading that can tell "the service owns a working port" from
        // "the service is running into issue #1047 and owns nothing" - creating a
        // port is the only thing that could tell them apart, and doing that would
        // spend this boot's one port. So the port itself is still not claimed, in
        // either direction. What IS new is a reading of the BUILD, which is a
        // different question with a cheap answer, and on a build that is on the
        // known-bad list the restart advice is backed by that reading rather than
        // by a guess. See classifyWindowsMidi() and the block over WinMidiInfo.
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

    // *** SOMEBODY PRESSED THE NAMED DOOR ON THE BINDING SCREEN. *** It lives here
    // and not in Pending because Pending is what this run MEASURED and this is what
    // somebody SAID: runSteps() copies it across on its way to the exit code, at the
    // one place the two kinds of fact are allowed to meet. See overrideBinding().
    bool         bindingDoorTaken;

    // *** WHICH MIXER THIS RUN WAS TOLD IT IS FOR. *** Here for the same reason
    // bindingDoorTaken is here and not in Pending: it is what somebody SAID, not what
    // this run measured. The measurement is state.usb, and nothing below ever writes
    // one from the other.
    //
    // AN INDEX INTO common.h's model table - kModelBcd3000 or kModelBcd2000 - and it
    // starts at kModelBcd3000 because a zeroed Run has to mean something honest and
    // the BCD3000 is the only model validated on hardware. See chooseModel().
    int          selectedModel;

    // *** FIX ROUND 1: `bool thirdPartyStarted` STOOD HERE AND IS DELETED. *** It
    // recorded that somebody else's installer had really been started from this
    // window, and it was read in two places: recheckMachine(), which softened its
    // "which is all that has happened so far" sentence, and reviewFooterFor(),
    // which swapped the install screen's footer for one admitting another program
    // had run.
    //
    // The round that removed the third party detection left the field, the footer
    // and both readers in place "for the offer a later task rebuilds". A reviewer
    // counted the writers and found the real state: NOTHING IN THIS PROGRAM WROTE
    // IT. The only writers were two lines in the harness setting it by hand, so a
    // struct member no production path could set was keeping two sentences alive
    // that no shipped run could print, and three suites were counting them - the
    // harness exercising its own fixture rather than the program.
    //
    // *** AND NO TASK LEFT IN THIS PLAN WILL WRITE IT EITHER, WHICH IS WHY IT GOES
    //     RATHER THAN WAITS. *** Task 6 gives this screen its new subject and
    // explicitly "stops telling the user to install anything" - it checks the
    // midisrv service and a file version and reports what it read. Task 7 ships its
    // payload embedded in this installer. Neither starts another program from this
    // window, so "kept for a later task" names no later task. If one is ever added,
    // it adds the flag, the sentence and the checks together, against a caller that
    // can actually set it - which is the only order in which any of the three can
    // be proved.
};

// Plain data with no destructor, like everything else at file scope in this
// project: nothing here may run code while the process is being unloaded.
static Run            g_run;
static bcdgui::Wizard g_wiz;

// Returns false when the run has to stop here. *stopCode is then the exit code,
// and *blockedNote is set when the reason is one a person should be shown rather
// than have a window disappear over.
// THE THREE SENTENCES THAT CAN LAND IN THE FOOT BAND ON PAGE 2, named rather than
// written at their use sites.
//
// They share that band with two buttons now, and the note gets whatever room the
// buttons leave. Only one of the three is ever rendered by installer/verify - the
// /preview one - so the other two would be exactly the kind of text that is cut
// without anything measuring it, which is what happened to this note once already.
// Named here, they can all be measured against the box they would land in.
//
// THEY ARE ALSO SHORTER THAN THEY WERE, and that was decided by measurement. In the
// room two buttons leave, the old 62 character version needed FOUR lines of the
// small font where the band gives three - it would have been cut, exactly the way
// this note was cut once before, and neither of these two is ever rendered by the
// harness. Both keep the half that matters: what cannot happen now.
static const wchar_t* const kNoteNoPaths =
    L"Windows folders unknown: nothing can be installed.";
static const wchar_t* const kNoteNoAdmin =
    L"Not an administrator: nothing can be installed.";
static const wchar_t* const kNotePreview =
    L"/preview: this run can look, and cannot install.";

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

    // The walkthrough, here rather than after the run, and unconditionally.
    //
    // HERE, because the MIDI port screen's pane is step 1 of it and the table is
    // built from this point on. The text has to exist before the window does,
    // exactly like the rows. (It was page 2's whole pane until this round; step 1
    // captures itself now, inside printMidiPortStep().)
    //
    // UNCONDITIONALLY, and that is a decision with a cost. On a machine where both
    // prerequisites are already in place these six steps are things the reader has
    // already done, which is noise. It is printed anyway because the alternative -
    // printing it only when something is missing - makes a pane appear and
    // disappear between runs, gives a screen two shapes to lay out and two shapes to
    // photograph, and takes the reference away from the one reader most likely to
    // want it: somebody whose mixer WAS working, is not working now, and is running
    // this again to find out why. The block's own opening line says what is already
    // in place is marked as such above.
    printPrerequisiteWalkthrough();

    if (run->opt.checkOnly) {
        say(L"/check was given: nothing was changed and nothing was written.");
        return false;
    }

    if (!run->state.pathsResolved) {
        sayFail(L"refusing to install without knowing where the files go");
        *stopCode    = kExitFailed;
        *blockedNote = kNoteNoPaths;
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
        *blockedNote = kNoteNoAdmin;
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
    // *** AND THE ONE THING IN HERE THAT WAS NOT MEASURED, CARRIED ACROSS AT THE ONE
    //     PLACE THE TWO KINDS OF FACT MEET. *** Every other flag on this structure is
    // this program's own reading of the machine. This one is a claim a person made by
    // pressing the named door on the binding screen, and it is here so that the exit
    // code below can be forced to 3 by it - the door's fourth property, and the only
    // one of the four that survives the window being closed.
    pending->bindingClaimedByHand = run->bindingDoorTaken;
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
    return exitCodeFor(pending);
}

// ---------------------------------------------------------------------------
// Page 2's "Check again", and why it is a function in THIS file.
//
// WHAT IT COSTS TO RUN IT ANYWHERE ELSE. gatherMachineState() reads a dozen registry
// keys, walks the whole process list and asks SetupAPI about the device interfaces.
// On the window's thread that is long enough for Windows to stop repainting, grey the
// window out and put "(not responding)" in its title - which is precisely the
// impression this window was built to remove. So it is a worker, and gui.cpp is
// handed it as a pointer: that file has no declaration of this function and cannot
// call it from a message handler even by mistake.
//
// IT REWRITES run->state, AND THAT IS THE POINT RATHER THAN A SIDE EFFECT. The page
// and the install have to be looking at the same snapshot. If the page said "found"
// while the install still acted on the reading from before the user fixed something,
// the summary would print a pending item about something that is no longer
// pending - the same class of defect as the false claim about the MIDI port that
// 490904f removed. Nothing else touches run->state while this runs: the window thread
// reads the WIZARD, not the state, and startWork() refuses while a re-check is in
// flight.
//
// It says what it did through say(), like everything else here, so the re-check is in
// the log pane, in the log file and in a redirected /console run - a measurement that
// only exists as pixels is a measurement nobody can send with a support request.
// ---------------------------------------------------------------------------
// PAGE 2's ONE CONTEXTUAL ACTION, AND THE LADDER THAT USED TO STAND HERE
//
// *** THIS TASK REMOVES AND DOES NOT ADD. *** The MIDI port screen's offer - the
// OfferKind/Offer pair, the winget/open-page ladder, and the worker that ran a
// third party installer and re-checked afterwards - is gone along with the
// detection it was a pure function of. MachineState no longer carries a MIDI
// backend reading, so there is nothing left for a ladder to decide between. A
// later task adds the replacement detection and, if it still wants an
// accelerated install, rebuilds the offer on top of it.
//
// *** FIX ROUND 1: THE TIMEOUT CONSTANT THAT USED TO STAND HERE WAS DELETED,
//     NOT KEPT "FOR REFERENCE". *** A reviewer found it: one hit in installer/ -
// its own definition - with its sole reader (the deleted worker) gone. That is
// dead code with a comment attached, which is the exact pattern this task was
// asked to hunt, not create. The 20 minute figure and the reasoning for it (a
// person may be reading a third party licence) are in this file's git history
// and in the ledger; a constant nothing reads does not belong in the binary to
// preserve them. If a later task rebuilds this worker, it re-derives or
// re-measures its own timeout.
static int recheckMachine(void* user);

// ---------------------------------------------------------------------------
// THE "Get Zadig" SCREEN'S BUTTON, AND IT OPENS A PAGE AND DOES NOTHING ELSE.
//
// *** WHY THERE IS A BUTTON HERE AT ALL: the owner asked for one. *** He decided, on
// mock-ups, that the address is visible as TEXT and that a button opens it - seeing it
// lets a person check it before clicking, copying it survives a broken default browser,
// and a button saves typing, because typing an address wrong is exactly how somebody
// downloads the wrong program. See Screen::addressLead in gui.h.
//
// *** IT NEVER STARTS ZADIG, AND THAT IS THE ONE LINE IN HERE THAT IS A POLICY. ***
// Rebinding a USB device is the single operation in this whole process that can leave
// the mixer unusable, and it stays in the user's hands on purpose - the same rule a
// winget offer would have to state for the other direction: an accelerated install
// for a downloadable dependency, never anything for Zadig. This function opens a web
// page. There is no ladder here and nothing to choose.
//
// *** IT REUSES openPageInBrowser() AND THEREFORE launchUnelevated(). *** That is the
// existing machinery and it is the reason a page opened from this elevated process does
// not inherit our token: a browser started with our rights would be a real downgrade
// for the user, not a cosmetic one. Nothing new was written for this button.
//
// AND IT ENDS BY MEASURING, like every other Screen::action - see the block on that
// field in gui.h. This screen has no row of its own to restate, because Zadig leaves
// nothing to read; what the re-check refreshes is the rest of the table and the review
// rows, and running it costs a registry read the user asked for by pressing a button.
// The alternative was an action that returns 0 without measuring, which is a second
// contract for one field.
//
// *** IT SAYS WHAT IT DID AND NOT WHAT IT ACHIEVED. *** openPageInBrowser() confirms
// that a launcher started, which is not the same statement as a page being open, and
// this says so in those words - the same distinction a winget offer's worker would
// draw about winget's exit code.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// *** THE PAINTED ADDRESS OPENED ITSELF. ONE FUNCTION FOR BOTH SCREENS, AND THE PAGE IS
//     AN ARGUMENT AND NEVER A LITERAL IN HERE. ***
//
// WHY IT EXISTS: the owner walked all nine screens on 2026-07-31 and asked for the one
// change - "o link azul no comeco da pagina deveria ter link tambem". Both screens that
// paint an address get it, not only the Zadig one he named.
//
// *** IT TAKES THE URL RATHER THAN CHOOSING ONE, AND THAT IS WHAT MAKES ONE FUNCTION
//     SAFE HERE. *** gui.cpp hands it Screen::addressUrl - the very pointer
// renderSubject() painted on the line that was clicked - so the address a reader saw and
// the page a browser is asked for are the same object, and there is no per-screen opener
// that could be aimed at the other screen's page. A second copy of this function, one
// per screen, is precisely the drift this project has closed for the re-check button's
// words and for the repository address.
//
// *** IT SAYS WHAT IT DID AND NOT WHAT IT ACHIEVED. *** openPageInBrowser() confirms
// that a launcher started, which is not the same statement as a page being open.
//
// *** AND IT DOES NOT END BY MEASURING. *** This block said the opposite until
// 2026-08-01 - "it ends by measuring, like every Screen::action, because
// Screen::addressOpen carries that contract unchanged" - and that stopped being true in
// the same round that wrote it, two screens further down this file. A comment claiming
// the behaviour the code had yesterday is exactly how the address defect of 2026-07-31
// was found, and this is the same shape in the same feature. It returns 0; see the block
// over openDownloadAddress() below for the measurement behind that and for what rides on
// it. It DOES run on the worker thread, for the reason the offer does: a process launch
// on the window thread is a window that stops repainting.
//
// A NULL OR EMPTY url IS NOT REACHABLE FROM THE PRESS - gui.cpp refuses the click when
// the screen has no address - and is handled anyway, because a function that dereferences
// its argument on the strength of a rule enforced in another file is a function that
// crashes the day the rule moves.
// ---------------------------------------------------------------------------
// THE LAUNCH ITSELF, AND WHAT IS SAID ABOUT IT. One piece of code, called by the painted
// address and by the Zadig button, so that two controls that reach the same page cannot
// come to describe the same act differently. It returns nothing: what the CALLER does
// afterwards is the caller's own contract, and the two callers differ there on purpose.
static void openPageAndSay(const wchar_t* url)
{
    if (!url || !url[0]) {
        sayWarn(L"there was no address to open, so nothing was opened");
        return;
    }
    DWORD err = 0;
    if (openPageInBrowser(url, &err))
        sayInfo(L"a browser was asked to open %s - which is not the same statement as "
                L"the page being open, and is all this program can see.", url);
    else
        sayWarn(L"a browser could not be started (%s). The address is %s",
                winErrText(err), url);
}

// ---------------------------------------------------------------------------
// *** AND IT RETURNS 0 RATHER THAN RE-READING THE MACHINE, WHICH IS THE ONE PLACE
//     Screen::addressOpen PARTS FROM Screen::action. ***
//
// Every Screen::action ends by measuring because an action CHANGES this machine - a
// winget offer runs somebody else's installer, so the page after it has to be read
// again. This asks a browser to open a public address. Nothing it does can change what
// was measured: the user has not downloaded anything yet, let alone installed it.
//
// *** THE COST IS MEASURED AND IS WHY THIS IS NOT MERELY USELESS. *** installer/verify
// times the re-check at 203 to 703 ms on the controller's machine, and startReviewWorker()
// greys four controls for the whole of it - "Check again", the contextual button, the
// door and the primary. That is a fifth to seven tenths of a second of dead window, plus
// a full machine report in the log file and in /console, bought each time somebody clicks
// a link, to repaint numbers that cannot have moved.
//
// *** WHAT RIDES ON THE 0, AND IT IS A PATH THAT ALREADY EXISTED. *** recheckProc() posts
// whatever comes back through postReviewDone(), and the WM_BCD_RDONE handler reads it as
// `if (n > 0 && n <= kMaxRows) reviewCount = n` - so 0 means "nothing was posted, leave
// the rows alone". recheckProc() already produces exactly that when an action pointer is
// null, so this is not a new value in the channel. Everything else on that path is
// unchanged: the flow still restates its table through Wizard::refreshScreens, the page
// is still relaid out, and the controls still come back.
//
// AND openZadigPage() BELOW STILL MEASURES. It is a Screen::action, its field's contract
// is untouched, and the objection recorded there - "an action that returns 0 without
// measuring is a second contract for one field" - is answered rather than overruled: this
// is a DIFFERENT field with ONE contract of its own. What the two share is the launch and
// the words about it, which is openPageAndSay() above.
// ---------------------------------------------------------------------------
static int openDownloadAddress(void* user, const wchar_t* url)
{
    (void)user;
    sayBlank();
    say(L"============= the address on the screen was clicked =============");
    sayInfo(L"it opens a web page and nothing else. Nothing is downloaded for you, "
            L"nothing is started for you and nothing is installed by this - so nothing "
            L"on this machine has changed and the checks are not being read again.");
    if (url && url[0])
        say(L"       page    : %s", url);
    openPageAndSay(url);
    return 0;
}

static int openZadigPage(void* user)
{
    sayBlank();
    say(L"============= Zadig: what this button is about to do =============");
    sayInfo(L"it opens a web page and nothing else. Zadig is NOT downloaded for you, "
            L"NOT started for you and NOT run for you: rebinding a USB device is the "
            L"one operation here that can leave the mixer unusable, and it stays in "
            L"your hands on purpose.");
    say(L"       page    : %s", kZadigDownloadPage);
    say(L"       author  : Pete Batard");

    // *** THE LAUNCH AND THE WORDS ABOUT IT ARE openPageAndSay()'S, SHARED WITH THE
    //     PAINTED ADDRESS ON THIS SAME SCREEN. *** Two controls that reach one page must
    // not come to describe the same act in two ways - in particular the distinction
    // between "a launcher started" and "the page is open", which is the honest half.
    //
    // *** WHAT IS NOT SHARED IS THE ENDING, AND THAT IS ON PURPOSE. *** This is a
    // Screen::action and ends by measuring, exactly as it always has and as that field's
    // contract requires; the address is a Screen::addressOpen and returns 0. See the
    // block over openDownloadAddress() for why the two differ and for the measurement
    // behind it. Nothing about THIS function's behaviour changed in that round.
    openPageAndSay(kZadigDownloadPage);
    return recheckMachine(user);
}

static int recheckMachine(void* user)
{
    Run* run = (Run*)user;
    sayBlank();
    // *** FIX ROUND 1: THE TWO-BRANCH VERSION OF THIS SENTENCE IS GONE, AND SO IS
    //     THE FIELD IT ASKED. *** It read Run::thirdPartyStarted and, when a winget
    // offer's worker had just run somebody else's installer before ending
    // `return recheckMachine(user)`, dropped the "which is all that has happened so
    // far" tail - which on that one path was false. The reasoning was right; what was
    // wrong was leaving it standing after the offer that could reach it had been
    // removed. Nothing in this program ever set the flag, so the softened sentence
    // was unreachable in every shipped run while the harness kept it counted by
    // setting the flag by hand. See the block where that field was declared in Run.
    //
    // The unconditional sentence is the true one for every path this program has:
    // a re-check reads the registry and looks for files, and on a run where nothing
    // else can have been started that really is all that has happened. A later task
    // that adds an offer restores the branch WITH the writer that makes it
    // reachable, and not before.
    say(L"Checking again. Nothing is being changed by this: it reads the registry and "
        L"looks for files, which is all that has happened so far.");
    gatherMachineState(&run->state);
    reportMachineState(&run->state);

    // A Wizard of its own, on this thread's stack. g_wiz belongs to the window
    // thread, and filling it from here would be two threads writing the rows the
    // painter is reading.
    bcdgui::Wizard fresh;
    ZeroMemory(&fresh, sizeof(fresh));
    bcdgui::fillPreflightRows(&fresh, &run->state);
    for (int i = 0; i < fresh.reviewCount; i++)
        bcdgui::postReviewRow(i, fresh.review[i].state, fresh.review[i].title,
                              fresh.review[i].detail);

    // The offer is a function of the state, and the state has just been re-read - so
    // it is re-decided the moment this returns, by buildScreens() running again over
    // the fresh state on the window thread. It used to be posted from HERE as a
    // string of its own, which was a second channel for a fact the table rebuild
    // already carries; a page that offered to install something it had just measured
    // as present is what both arrangements exist to prevent. See rebuildScreens().

    return fresh.reviewCount;
}

// ---------------------------------------------------------------------------
// THE WORDS THE FOUR SCREENS ARE NAMED BY, AND THE THREE LINES THE OPENING
// CARRIES. Named once here and used TWICE below - by the Wizard fields the
// renderer still reads, and by the screen table that will replace them - because
// two copies of a caption is exactly the drift this project keeps closing, and a
// refactor that created a second copy of every string it moved would be a strange
// way to begin removing duplication.
// ---------------------------------------------------------------------------
static const wchar_t* const kOpeningTitle =
    L"An ASIO driver for the BCD3000, without turning anything off";
static const wchar_t* const kOpeningBullet1 =
    L"Copies the driver into Program Files and registers it, so your DJ software "
    L"lists \"Behringer BCD3000\" as an ASIO device.";
static const wchar_t* const kOpeningBullet2 =
    L"Copies the control and LED service into your own profile and starts it at "
    L"every sign in, deliberately unelevated.";
static const wchar_t* const kOpeningBullet3 =
    L"Checks the WinUSB binding on the device, which it will not touch.";
// The mixer screen's own words. Its title is a CONSTANT for the same reason the
// four above are: installer/verify names the screen's capture file by comparing the
// title POINTER against these, so two screens of one kind cannot end up sharing a
// picture. A substring match on the words would have been the third defect of that
// class in this project in three rounds.
static const wchar_t* const kMixerTitle = L"Plug the mixer in and switch it on";
static const wchar_t* const kMixerBullet1 =
    L"USB 2.0 rather than 3.x: it is a USB 1.1 full speed device and its audio is "
    L"isochronous, which some USB 3.x controllers do not carry reliably. That is the "
    L"hardware, and there is no software fix for it.";
static const wchar_t* const kMixerBullet2 =
    L"This comes first, and not out of politeness. Windows records a device the first "
    L"time it is plugged in and that record survives; until it has happened once, "
    L"\"never seen by this machine\" and \"seen, but not bound to WinUSB\" cannot be "
    L"told apart, so every screen after this one can say less.";
static const wchar_t* const kMixerBullet3 =
    L"Nothing on this screen writes anything. It reads what Windows already recorded "
    L"and what is answering now, and Check again reads both a second time.";
// ---------------------------------------------------------------------------
// THE DEVICE SCREEN'S OWN WORDS, AND THE TWO OPTIONS IT OFFERS.
//
// Its title is a CONSTANT for the reason the mixer's and the MIDI port's are:
// installer/verify names the screen's capture file by comparing the title POINTER
// against these, so two screens of one kind cannot end up sharing a picture.
//
// *** THE SECOND LABEL SAYS WHAT DOES NOT WORK, AND THAT IS THE WHOLE POINT OF THIS
//     SCREEN. *** The BCD2000 path is in the driver, has never once been run, and
// nobody on this project owns one. native\bcdasio\usbdev.cpp records that as a fact -
// its profile carries provenOnHardware = false - and installer/verify asserts that the
// model this label hedges about IS the model that flag names, by reading that file as
// text. So the sentence and the fact cannot drift: the day somebody proves a BCD2000
// and flips the flag, this label fails a check instead of ageing quietly into a lie.
//
// THE MODEL NAMES ARE INTERPOLATED AND NOT SPELT, for the same reason any published
// path constant is interpolated rather than retyped: common.cpp already holds them, and
// a second copy of a name in a sentence is a second place it can drift from the table
// the harness checks.
// That is why these two are BUFFERS filled by buildModelLabels() rather than literals.
// *** THESE WORDS WERE CHOSEN AGAINST THE DESIGN'S ORIGINAL ONES, AND THE DESIGN HAS
//     SINCE BEEN RECONCILED TO THEM. THIS IS THE ONE PLACE THAT STORY IS RECORDED. ***
//
// The design's flow table row 2 originally read "Which device". This program calls the
// screen "Which mixer is this for", for two reasons: screen 1 is already "Plug the mixer in
// and switch it on", so "mixer" is the word this wizard has taught by the time anybody
// arrives here, and "device" is what Windows calls everything in Device Manager - the least
// specific noun available on a screen whose whole job is to be specific about one.
//
// A review flagged that the design and the program disagreed and that NOTHING asserted
// either. The disagreement was defensible; being unable to SEE it was not - that is how a
// spec and a program drift apart quietly. So installer\verify asserts what this title SAYS
// (not merely that this pointer is this constant, which is one value read twice and fails
// only if the assignment disappears), and the controller then reconciled the design's table
// to these words. As of that reconciliation the two agree.
//
// *** AND THE STORY IS TOLD HERE AND NOWHERE ELSE, WHICH IS THE SECOND CORRECTION. *** The
// check in installer\verify used to quote the design's old wording in its failure text, and
// went stale the moment the design changed - a second copy of another document's words with
// nothing keeping the two equal, which is the drift this project closes everywhere else.
// That message now quotes only the title itself. If the design and this program ever
// diverge again, this comment is the single place it has to be said.
//
// ONE DEFINITION, and it is this one: the screen's title, the capture key pageName() files
// its pictures under, and the check on its words all read this constant.
static const wchar_t* const kDeviceTitle = L"Which mixer is this for";
// *** THE FIRST BULLET IS THE ONE A REVIEWER SHOULD ARGUE WITH, SO IT SAYS THE
//     SURPRISING THING FIRST. *** Nothing this installer writes depends on the answer:
// the driver carries both profiles and matches on the vendor and product ids at run
// time, so the same files and the same registry keys are written either way. Saying so
// removes the one wrong idea this screen can plant - that picking the wrong line breaks
// the installation - and replaces it with the true one: it decides what to expect, and
// it goes in the log so a support request says which mixer somebody has.
static const wchar_t* const kDeviceBullet1 =
    L"This does not change what gets installed. The driver carries both profiles and "
    L"matches on the USB ids when it runs, so the same files and keys are written "
    L"either way. What it changes is what to expect - and it is recorded in the log, so "
    L"a support request says which mixer this was for.";
// *** THE DETECTION IS BCD3000 ONLY, AND THE SCREEN SAYS SO RATHER THAN LEAVING IT TO
//     BE INFERRED FROM AN EMPTY ROW. *** kUsbEnumKey names PID_00BF, so the one key
// this program reads is the BCD3000's: a BCD2000 owner is ALWAYS on the "nothing was
// detected" branch, whatever is plugged in. That is a limit and not a bug, and a screen
// that hid it would be presenting the absence of a detection as the absence of a
// device.
// *** ITS LAST CLAUSE WAS CUT BECAUSE THE ROW ALREADY SAID IT, AND THE 144 DPI STRIP IS
//     WHAT FOUND THE DUPLICATION. *** It ended "...so the choice below is yours to make
// and nothing above it disagrees with you", which is describeModel()'s own sentence for
// the machine that detected nothing - two statements of one fact, four lines apart, on a
// screen that was 2 logical pixels over its strip at 144 DPI. The cut is a duplication
// removed and not a fixture tuned: the fact is still on the screen, in the row, where it
// is a reading rather than a bullet.
static const wchar_t* const kDeviceBullet2 =
    L"Only the BCD3000 can be detected: the one registry key this installer reads is "
    L"that model's. On a BCD2000 nothing is found however it is plugged in.";
// The invitation to open an issue, and the repository address PAINTED on this screen
// rather than only said in the banner. Somebody who has just read that their mixer is
// unsupported needs the address in front of them, on that screen, instead of forty
// lines up a log pane. It is ALSO said through say() by the banner, so it reaches the
// console, the log file, a screen reader and the clipboard - which is what "copyable"
// means in this program, and painted text reaches none of those on its own.
//
// *** A BUFFER AND NOT A LITERAL, BECAUSE THE ADDRESS ALREADY EXISTS. *** It is
// bcdgui::kRepositoryUrl, one definition shared by the opening page, the console banner
// and installer\README.md. Spelling it a fourth time here is precisely the drift this
// project has closed for the re-check button's words and for any published path
// constant. Filled by buildDeviceWords().
static wchar_t g_deviceRepoBullet[512] = { 0 };

// The two option labels, in the model table's order. Buffers for the reason above: the
// names live in common.cpp, where installer\verify checks them against the driver's
// profile table, and a label that spelled "BCD2000" itself would be a copy of a name
// nothing forces to agree with the table the check reads.
//
// *** THE SECOND ONE IS THE LONGEST STRING EITHER CONTROL CARRIES, AND THAT IS WHY THE
//     RESERVED HEIGHT IS MEASURED. *** gui.cpp measures both labels WRAPPED, in the
// width the page really gives them, in the same font the control draws in - see
// kChoiceLeadH there. A fixed row height would be a promise about the length of this
// sentence.
static wchar_t g_modelLabel[bcdsetup::kModelCount][256];

// ---------------------------------------------------------------------------
// The device screen's interpolated words, built from the constants that already hold
// them. Pure assignment: it reads no machine and decides nothing, which is why
// buildScreens() can call it every time it runs without asking whether it has already.
//
// *** IT NAMES THE MODEL AND THEN SAYS WHAT IS TRUE OF IT, IN THAT ORDER, ON BOTH
//     LINES. *** The first option is the one validated on hardware and says so; the
// second says what does NOT work before it says what might, because "audio should work"
// read first and "the controls are not supported yet" read second is an encouragement
// with a caveat, and this project has removed that shape twice.
// ---------------------------------------------------------------------------
static void buildDeviceWords(void)
{
    _snwprintf(g_modelLabel[kModelBcd3000], 250,
               L"%s - the model this driver was built for and proven on. Everything "
               L"this installer claims was measured on one.",
               modelName(kModelBcd3000));
    g_modelLabel[kModelBcd3000][250] = 0;

    // *** EVERY CLAUSE IN HERE IS REQUIRED BY THE DESIGN OR BY A CHECK, WHICH IS WHY IT
    //     IS THIS LONG AND WHY IT IS NOT LONGER. *** "not supported yet" is the design's
    // own words and installer\verify ties it to the driver's provenOnHardware flag;
    // "Audio should work" is the design's other half and SHOULD is asserted rather than
    // WILL; "nobody on this project owns one" is the sentence the spec asks this screen
    // to carry. What was cut was a remark about the word "should" being the honest one -
    // meta-commentary on a word the reader can weigh unaided, and one wrapped line of a
    // screen that was 2 logical pixels over its strip at 144 DPI.
    _snwprintf(g_modelLabel[kModelBcd2000], 250,
               L"%s - EXPERIMENTAL. The control surface is not supported yet: the knobs, "
               L"buttons and LEDs will not work. Audio should work - nobody on this "
               L"project owns one, so this path has never been run.",
               modelName(kModelBcd2000));
    g_modelLabel[kModelBcd2000][250] = 0;

    _snwprintf(g_deviceRepoBullet, 500,
               L"If you have a %s, what happens is worth an issue either way - it works "
               L"or it does not, and both are news: %s",
               modelName(kModelBcd2000), bcdgui::kRepositoryUrl);
    g_deviceRepoBullet[500] = 0;
}
// ---------------------------------------------------------------------------
// THE MIDI PORT SCREEN'S OWN WORDS. Its title is a CONSTANT for the same reason the
// mixer's is: installer/verify names the screen's capture file by comparing the title
// POINTER against these.
//
// *** THERE IS STILL NO SECOND BULLET AND NO ADDRESS, AND THAT IS NOW A DECISION
//     RATHER THAN A GAP. *** They went with the third party detection they were built
// on - a vendor attribution, a download address and a gate over whether either was
// shown. The round that gave this screen its new subject did NOT bring them back:
// Windows MIDI Services is in-box, so there is no author to attribute and no page to
// send anybody to. Everything this screen now has to say is in its ROW, which is the
// one thing on it that a machine can change. See describeMidiPort().
// ---------------------------------------------------------------------------
static const wchar_t* const kMidiTitle = L"The MIDI port";
// *** SHORT BECAUSE IT WAS MEASURED, NOT BECAUSE IT WAS DRAFTED SHORT. *** The first
// version of this screen needed 273 logical pixels at 96 DPI against the 221 the pane
// leaves, and 441 against 330 at 144 - it overflowed a screen built in the round whose
// whole subject is content that does not fit.
// *** FIX ROUND 1: "it" HAD LOST ITS ANTECEDENT AND NOW NAMES ITS SUBJECT. *** This
// bullet used to open "Without it the audio works...", and it read that way because
// the bullet ABOVE it - the vendor attribution, removed with the third party
// detection - had introduced the port. With that gone, "it" followed a three word
// title and referred to nothing a reader had been given. "this port" costs seven
// characters against the budget in the note above, and the render was measured
// afterwards rather than assumed: the screen's reported height is unchanged at both
// DPIs, so the ratchet is not being asked to carry the wording.
static const wchar_t* const kMidiBullet1 =
    L"Without this port the audio works and every knob, button and LED is dead - "
    L"which is why this screen does not stop you.";
// The pane's first line. It says what the panel is, because the panel is the only
// part of this screen that scrolls and its first line is what a reader lands on.
static const wchar_t* const kMidiPaneCaption =
    L"Step 1 of the walkthrough, in full. This is the same text the console prints "
    L"and the log file keeps; you can select it here and copy it.";
// ---------------------------------------------------------------------------
// THE TWO SCREENS THE ZADIG STEP SPLITS INTO, AND WHY IT IS TWO AND NOT ONE.
//
// *** THIS IS THE SPLIT THE OWNER'S COMPLAINT WAS ACTUALLY ABOUT. *** Page 2 carried
// a 574 x 254 screenshot of Zadig and a three paragraph caption underneath its rows,
// and the at-rest capture of that page measured what it meant: at 96 DPI the last row
// was cut mid sentence at the bottom edge, and the picture of the operation this
// program itself calls the most dangerous in the whole installation was ENTIRELY
// below the fold on arrival, along with its caption, the account row and the footer.
// 63 per cent of that page needed scrolling at 96 DPI and 65 at 144.
//
// So: DOWNLOAD on one screen, APPLY on the other. The first exists so that the second
// has ONE subject - the picture and what to match in it - and it is deliberately
// short for that reason and for no other.
//
// *** AND THE BINDING SCREEN HAS NO PANE, WHICH IS A MEASURED CONSEQUENCE AND NOT A
//     PREFERENCE. *** A screen with a pane gets a 221 logical pixel strip at 96 DPI
// (398 less the pane and its gap). The picture alone is 254. Before a title, a rule,
// a row or one bullet, the picture is already 33 pixels taller than the whole strip,
// so a binding screen with a pane could not show its own subject at all. Its words
// are therefore a row and bullets, and the long form of the procedure is on the
// screen before it, in the pane that screen can afford.
// ---------------------------------------------------------------------------
static const wchar_t* const kZadigTitle = L"Get Zadig";
static const wchar_t* const kZadigBullet1 =
    L"It is one small program from Pete Batard, it installs nothing and it is run "
    L"once. Download it and come back: the next screen is where it is used.";
static const wchar_t* const kZadigBullet2 =
    L"It is not carried inside this installer and it is not started for you. "
    L"Rebinding a USB device is the one operation here that can leave the mixer "
    L"unusable, and it stays in your hands on purpose.";
static const wchar_t* const kZadigBullet3 =
    L"The whole procedure is in the panel below, and the picture of the window you "
    L"are about to see is on the next screen.";
// *** THE ADDRESS LEAD, AND THIS SCREEN IS WHERE THE ARRANGEMENT WAS BUILT FIRST. ***
// It HAD 53 logical pixels of slack at 96 DPI and 60 at 144 and nothing had to leave it,
// which is why the owner's two decisions were implemented here before the MIDI port
// screen, where a line costs a line.
//
// *** PAST TENSE, AND THE PRESENT TENSE IS THE OPPOSITE. *** The address block spent 31
// of that at 96 DPI and 46 at 144, so this screen now runs at 199 of 221 and 316 of 330 -
// 22 and 14 - which makes "Get Zadig" the TIGHTEST screen in the flow at 144 DPI, ahead
// of the MIDI port screen's 23. It is not the cheap place any more and the next round to
// go looking for room must not read this block and believe it is. Rule 1 holds both at
// deficit zero and would fail loudly, but a comment that sends somebody to the wrong
// screen wastes the round before the check ever runs.
//
// *** THERE IS NO "ALREADY PRESENT" STATE ON THIS SCREEN, AND THAT IS A PROPERTY OF
//     ZADIG RATHER THAN AN OMISSION. *** The state rule is that the address is
// prominent when the thing is MISSING and that a machine which already has it is told
// what was found and where. Zadig is a single portable executable that installs
// nothing, registers nothing and leaves no key behind: there is no reading that could
// tell this machine whether it has been downloaded, which is the same recorded reason
// this screen has no row at all. A screen cannot report on something it did not
// measure, so this one always says where to get it and never claims to know whether
// somebody already has.
// Short because it shares the address's line, and a lead that did not fit beside it
// would cost a line on both screens instead of on neither.
static const wchar_t* const kZadigAddressLead = L"Download Zadig here:";
// The words on this screen's button. It NAMES ITS SUBJECT and not its verb, so that
// it means the same thing from any scroll position. And it names what pressing it
// DOES: it opens a page. It does not download, it does not install, and it does not
// run Zadig, which is the one program in this process this installer deliberately
// never starts.
static const wchar_t* const kZadigOpenPageLabel = L"Open the Zadig page";
// The pane's first line, in the same shape the MIDI port screen's caption has: it
// says what the panel is, because the panel is the only part of this screen that
// scrolls and its first line is what a reader lands on.
static const wchar_t* const kZadigPaneCaption =
    L"Step 2 of the walkthrough, in full - what to pick in Zadig's window, the two "
    L"things in that list that look right and are not, and the way back out. This is "
    L"the same text the console prints and the log file keeps.";

static const wchar_t* const kBindingTitle = L"Apply the WinUSB binding";
// *** THESE ARE SHORT BECAUSE THE PICTURE IS 254 PIXELS TALL AND GOES ABOVE THEM. ***
// renderSubject() draws the screenshot between the row and the bullets so that it is
// on the screen when the screen opens, which is the whole of what this round is for.
// Everything these four point at is said at length in the panel on the screen before,
// in the console and in the log file.
static const wchar_t* const kBindingBullet1 =
    L"Options > List All Devices FIRST. Without it the mixer's separate interfaces "
    L"are not in the list at all, and this is the most common way this step goes "
    L"wrong.";
static const wchar_t* const kBindingBullet2 =
    L"Pick the line reading BCD3000 (Interface 0) - not 1, not 2 - and confirm it by "
    L"the USB ID 1397 00BF beside it.";
static const wchar_t* const kBindingBullet3 =
    L"Set the target driver to WinUSB in the box to the RIGHT of the green arrow. "
    L"libusb-win32 and libusbK sit in that same list and neither works here.";
static const wchar_t* const kBindingBullet4 =
    L"The way back out, so nobody is stranded: Device Manager, uninstall the device "
    L"with \"delete the driver software\" ticked, unplug it, plug it in and start "
    L"again.";
// *** THE NAMED DOOR'S WORDS, AND THEY ARE A SENTENCE RATHER THAN A VERB ON PURPOSE.
//     *** What can fail here is not the detection of the DEVICE but the READING of
// the binding - the interface guid under the device's Device Parameters - and a
// machine where that read fails for a reason other than "not bound" would strand
// somebody on a screen telling them to run Zadig when Zadig has already been run.
// That is exactly the defect round 2c998fa removed from page 2 and it must not come
// back through navigation.
//
// It says what taking it CLAIMS, so that pressing it is a statement and not a way
// round a check. A door with a label that behaves like a door without one is worse
// than no door: this one is offered only while the binding is missing, it is written
// into the log as having been taken, and it makes this program finish with exit code
// 3 - done, with something still pending - instead of 0.
// *** IT IS SHORTER THAN THE DESIGN'S SENTENCE, AND THE REASON IS A MEASUREMENT. ***
// Section 4.2 writes it as "The binding is already applied - continue anyway". That
// label needs 311 logical pixels at 96 DPI in this window's button font, and the foot
// band has 284 between "Check again" and Back - it was drawn OVER the Back button and
// the harness caught it by both halves at once. The window cannot be made wider: it
// has no WS_THICKFRAME, on purpose.
//
// What was cut is the subject and not the claim. This button stands on a screen
// titled "Apply the WinUSB binding", beside a row titled "The WinUSB binding", and
// "already applied" is the whole of what pressing it asserts. installer/verify
// asserts that the label still carries that phrase, that it differs from Next, and
// that the whole of it fits inside its box at both DPIs.
static const wchar_t* const kBindingDoorLabel =
    L"Already applied - continue anyway";

// ---------------------------------------------------------------------------
// THE SCREEN THAT INSTALLS. It was titled "What is on this machine now" and it was
// the page every subject was still piled on; the design has always called it
// "Install the driver" and given it one subject - see section 3, row 6, and 3.2:
// "what will be written and where, and what will not be touched".
//
// *** THE PLAN SAID THIS SCREEN WOULD BE DELETED AND NO TASK EVER DELETED IT. ***
// Two of its seven rows had had screens of their own since the MIDI port and the
// binding got theirs, and were being stated twice with nothing comparing the two
// statements. The other five are, one for one, what this program writes, whether it
// may write it, and who it writes it for - which is this screen's subject and no
// other screen's. So the duplication went and the screen stayed.
//
// *** THE WORD ON ITS BUTTON DID NOT MOVE AND IS NOT THIS SCREEN'S TO MOVE. *** The
// press that writes has been here since the table was built, and the design puts it
// here too. What changed is what the screen SAYS.
static const wchar_t* const kInstallTitle = L"Install the driver";
// *** THE TWO BULLETS, AND THE SECOND ONE IS THE HALF OF THE DESIGN NO SCREEN IN
//     THIS PROGRAM CARRIED. *** Every other screen states what it found. This one has
// to state what it is about to DO, and the sentence that makes pressing Install
// honest is the one about what it will leave alone - because the two things it will
// leave alone are the two whose rows just came off this page, and a reader who
// noticed them here for three screens has to be told they are still measured and
// still untouched rather than simply dropped.
//
// *** THEY ARE TWO AND SHORT BECAUSE THIS SCREEN IS 241 LOGICAL PIXELS PAST ITS
//     STRIP AT 96 DPI BEFORE THEY ARE ADDED. *** The long form of both is in the
// rows below them, in the console and in the log file. What painted text has to do
// here is frame the rows and say the thing the rows cannot.
static const wchar_t* const kInstallBullet1 =
    L"The rows below are what will be written, and where: the ASIO driver and its "
    L"registration machine wide, the control and LED service in your own profile.";
// *** "ours to INSTALL" AND NOT "ours to CHANGE" - THIS BULLET USED TO NAME TWO
//     THINGS LEFT ALONE AND NOW NAMES ONE. *** The MIDI port screen used to carry a
// button that ran a third party installer, which made "neither is ours to change" an
// absolute the program contradicted in front of the reader. That button and the
// detection under it are gone - this task removed them - so this bullet names only
// the thing this press still leaves alone.
//
// The WinUSB binding it will not apply at all: rebinding a USB device is what leaves
// hardware unusable, and that stays in the user's hands on purpose.
static const wchar_t* const kInstallBullet2 =
    L"What will not be touched: the WinUSB binding on the device. It has a screen of "
    L"its own before this one, it was read there, and it is not ours to install - "
    L"this press does not write to it.";
static const wchar_t* const kWorkTitle   = L"Installing";
static const wchar_t* const kDoneTitle   =
    L"The parts this installer owns are in place.";

namespace bcdsetup {

// ---------------------------------------------------------------------------
// ONE SUBJECT: IS THE MIXER HERE. FOUR ANSWERS.
//
// They are four because enumKeyPresent means literally "this machine has seen this
// device at least once" - common.cpp:1195-1202 sets it when the enumeration key
// opens, and that key survives forever - so it separates "never seen" from "seen and
// not bound", which is the distinction plugging in FIRST exists to create. A screen
// that said the same thing in two of the four would be a screen with a bug, so
// installer/verify asserts that the four texts are pairwise different as well as
// that each says its own sentence.
//
// *** WHEN IT IS MISSING, THE CABLE COMES BEFORE THE VIRTUAL MACHINE, AND THAT ORDER
//     IS MEASURED RATHER THAN TIDY. *** The loose cable was the real cause TWICE on
// this project, and both times the owner found it after the controller had blamed
// something else - once the VMware arbitrator, once the code. The detection reported
// the truth on both occasions; the ordering of the advice is what was wrong.
//
// IT DECIDES NOTHING AND READS NOTHING. It is presentation of a MachineState that
// gatherMachineState() has already filled, which is what lets the harness call it
// with four invented machines and read the sentence back.
// ---------------------------------------------------------------------------
static void describeCable(const MachineState* s, bcdgui::Row* out)
{
    if (s->usb.interfacePresentNow) {
        bcdgui::setRow(out, bcdgui::kRowOk, L"The mixer",
                       L"It is connected and this machine can reach it right now.");
        return;
    }
    if (!s->usb.enumKeyPresent) {
        bcdgui::setRow(out, bcdgui::kRowWarn, L"The mixer",
                       L"This machine has never seen a BCD3000. Plug it into a USB "
                       L"2.0 port, switch it on, and press Check again.");
        return;
    }
    if (!s->usb.guidPresent) {
        bcdgui::setRow(out, bcdgui::kRowWarn, L"The mixer",
                       L"This machine has seen this mixer before, so a cable that "
                       L"works has been in it. It did not answer when this window "
                       L"opened. In this order: reseat the USB cable, then look for a "
                       L"virtual machine or another program that has taken the "
                       L"device.");
        return;
    }
    bcdgui::setRow(out, bcdgui::kRowWarn, L"The mixer",
                   L"It is bound to WinUSB but is not answering right now. In this "
                   L"order: reseat the USB cable, then close any other program or "
                   L"virtual machine that may be holding it.");
}

// ---------------------------------------------------------------------------
// ONE SUBJECT: WHICH MIXER DID THIS MACHINE ACTUALLY REPORT. TWO ANSWERS, AND THE
// SECOND ONE IS THE REASON THIS SCREEN EXISTS IN THE DESIGN.
//
// *** IT IS TWO AND NOT FOUR, AND THE ARITHMETIC IS IN common.cpp. *** describeCable()
// has four branches because it has three independent bits to report. This has one
// question - was a device of a known model found - and the detection can only answer it
// for ONE model: kUsbEnumKey names PID_00BF, so the only key this program opens is the
// BCD3000's. There is no third state to invent and none is invented.
//
// *** THE SECOND BRANCH IS THE ONE THE DESIGN SPELLS OUT IN THOSE WORDS, AND IT IS A
//     TRAP THIS PROJECT HAS FALLEN INTO TWICE. *** With nothing detected there is
// nothing to preselect, and a preselection PRESENTED as a detection - "BCD3000" sitting
// selected on a machine where no BCD3000 was found - is the same class of statement as a
// summary claiming an install it only attempted. So the row says nothing was detected,
// in those words, and says that the choice below is therefore the person's. The default
// stays the BCD3000 because it is the only model validated on hardware, and the row is
// what stops that default reading as a finding.
//
// interfacePresentNow AND NOT enumKeyPresent, which is the one field choice here worth
// arguing about. enumKeyPresent means "this machine saw one once, and that record
// survives forever" - true of a machine whose mixer is in a drawer. What this screen
// reports is what is ANSWERING, because the question it introduces is which mixer is
// being installed for right now. The cable screen one back owns the other distinction
// and makes all four of them.
//
// IT DECIDES NOTHING AND READS NOTHING: presentation of a MachineState already
// gathered, which is what lets installer\verify call it with invented machines and read
// the sentence back.
//
// *** IT TAKES THE SELECTION AS WELL AS THE MACHINE NOW, AND THAT IS THE FIX FOR THE
//     WORST DEFECT THIS SCREEN HAS SHIPPED. ***
//
// It used to decide from ONE field, s->usb.interfacePresentNow, and on the satisfied
// branch it wrote: "A BCD3000 answered on this machine, SO THAT IS WHAT IS SELECTED
// BELOW. Nothing here has to be changed." - with a pass mark. What is selected below came
// from somewhere else entirely, Run::selectedModel, and nothing kept the two in step. So
// one click on the BCD2000 radio left a GREEN row saying a BCD3000 was selected, with the
// BCD2000 selected, and the log saying so at the same time. A support request carried both.
//
// *** THAT IS THE DESIGN'S OWN CENTRAL REQUIREMENT, INVERTED. *** The design says "a
// preselection presented as a detection, on a machine where nothing was detected, is the
// same class of statement this project has removed twice". This screen removed the forward
// case and shipped the mirror: a SELECTION presented as THE DETECTION, on a machine where
// the person had already overridden it - worse than the forward case, because the person
// took an action and the screen denied it.
//
// The not-detected branch never had the bug and the reason is worth keeping: it says the
// choice "STARTS on the BCD3000", and a tense that describes an initial state stays true
// after a change. One word was the whole difference.
//
// So the row is now a function of BOTH things the screen is about, and there are three
// answers rather than two. The third - the machine and the person disagreeing - is worth
// a row of its own rather than silence: on a machine where a BCD3000 answered and the
// BCD2000 is selected, somebody has either made a mistake or owns both, and either way
// the screen saying so is more use than a screen that mentions only what it measured.
// ---------------------------------------------------------------------------
static void describeModel(const MachineState* s, int selected, bcdgui::Row* out)
{
    const wchar_t* chosen = modelName(selected);
    if (!chosen)
        chosen = modelName(kModelBcd3000);

    // setRow() takes the format and the arguments, which is what every other row in this
    // file does: it truncates rather than overflowing, so there is no local buffer here
    // to get the terminator wrong on.
    if (s->usb.interfacePresentNow) {
        // *** THE MACHINE AND THE PERSON AGREE. *** Only this branch may wear a pass
        // mark, and it says what it MEASURED and what this run IS SET UP FOR as two
        // statements rather than deriving the second from the first.
        if (selected == kModelBcd3000) {
            bcdgui::setRow(out, bcdgui::kRowOk, L"Which mixer",
                           L"A %s answered on this machine, and that is what this "
                           L"installation is set up for.", modelName(kModelBcd3000));
            return;
        }
        // *** THEY DISAGREE, AND AMBER IS THE HONEST MARK. *** Not red: this is allowed,
        // and the design's whole reason for putting controls on this screen is that the
        // person may override the detection. Not green either, which is what the defect
        // did - a pass mark over a screen contradicting itself.
        bcdgui::setRow(out, bcdgui::kRowWarn, L"Which mixer",
                       L"A %s answered on this machine, but this installation is set up "
                       L"for a %s. That is allowed - change the choice below if it was "
                       L"not deliberate.", modelName(kModelBcd3000), chosen);
        return;
    }
    // NEUTRAL AND NOT AMBER, and that is not softening a finding. Amber here would be a
    // second complaint about the cable, which the screen one back has already made in
    // four different ways with the advice that goes with each. Nothing is wrong on THIS
    // screen: a fact is stated and a question is handed over.
    //
    // *** AND IT SAYS "STARTS", WHICH IS THE WORD THAT KEEPS THIS BRANCH TRUE. *** See
    // the block above: this is the sentence the satisfied branch should have been written
    // like in the first place.
    bcdgui::setRow(out, bcdgui::kRowNeutral, L"Which mixer",
                   L"Nothing was detected, so nothing below is a detection: the choice "
                   L"is yours. It starts on the %s because that is the only model this "
                   L"driver has been proven on, and not because one was found.",
                   modelName(kModelBcd3000));
}

// ---------------------------------------------------------------------------
// ONE SUBJECT: WHAT THIS INSTALLER LOOKED AT ABOUT WINDOWS MIDI SERVICES.
// THREE ANSWERS, AND NOT ONE OF THEM IS "THE PORT WILL WORK".
//
// *** THIS IS THE SCREEN'S NEW SUBJECT. *** It used to name a third party library
// and offer to install it. Windows MIDI Services is in-box, so there is nothing
// to offer; what is left is a reading, and the one thing on this whole screen a
// user could not work out for themselves - that their Windows build may carry a
// known defect in Microsoft's own virtual MIDI transport.
//
//   kWinMidiReady     nothing to do. It names what was LOOKED AT, with the
//                     numbers, and says outright that no port was created and
//                     so nothing here is a promise that one will work.
//   kWinMidiKnownBad  names microsoft/MIDI issue #1047, says the fix arrives
//                     through Windows Update rather than from anybody here, and
//                     says a restart clears it if it has already bitten.
//   kWinMidiUnread    prints the NUMERIC result of each read and stops. It does
//                     not invent a cause, because it did not measure one.
//
// *** WHY NOT A GREEN TICK ON THE FIRST ONE. *** kRowOk on this screen would be
// this program marking as satisfied a thing it has not tested. The reading is
// neutral in all three states and the row's WORDS carry the difference, which is
// the same discipline the Zadig screen records for having no row at all: a screen
// may not paint a mark for something it did not measure. The amber on the middle
// state is not a mark on the machine either - it is a warning about Windows, and
// it is the one state where there is something a person should act on.
//
// IT DECIDES NOTHING AND READS NOTHING ITSELF: pure presentation of a
// MachineState, so the harness can call it with three invented machines and read
// the three sentences back without owning three machines.
// ---------------------------------------------------------------------------
static void describeMidiPort(const MachineState* s, bcdgui::Row* out)
{
    const WinMidiInfo* w = &s->winMidi;
    switch (classifyWindowsMidi(w)) {
    case kWinMidiReady:
        bcdgui::setRow(out, bcdgui::kRowNeutral, L"The MIDI port",
                       L"Nothing to install - Windows brings this. Looked at: the "
                       L"%s service is registered, %s is present at version "
                       L"%lu.%lu.%lu.%lu, and the service binary is build %lu.%lu, "
                       L"which is not on the list of builds known to break virtual "
                       L"ports. No port was created to test it, so nothing here "
                       L"says one will work.",
                       kMidiServiceName, kMidiTransportDllName,
                       w->transportVersion[0], w->transportVersion[1],
                       w->transportVersion[2], w->transportVersion[3],
                       w->serviceVersion[2], w->serviceVersion[3]);
        break;
    case kWinMidiKnownBad:
        bcdgui::setRow(out, bcdgui::kRowWarn, L"The MIDI port",
                       L"This Windows build - %lu.%lu - is one where Microsoft's own "
                       L"virtual MIDI transport is known to fail: microsoft/MIDI "
                       L"issue #1047, open, no fix date. Only the first port after a "
                       L"restart can be created. Nothing here is yours to install "
                       L"and the fix arrives through Windows Update; if the controls "
                       L"have already gone quiet, restarting the machine clears it.",
                       w->serviceVersion[2], w->serviceVersion[3]);
        break;
    default:
        bcdgui::setRow(out, bcdgui::kRowNeutral, L"The MIDI port",
                       L"Not established, and no cause is guessed from it. Read: %s "
                       L"registered %d, %s present %d, its version read %d, the "
                       L"service binary's version read %d, last error %lu.",
                       kMidiServiceName, w->serviceRegistered ? 1 : 0,
                       kMidiTransportDllName, w->transportPresent ? 1 : 0,
                       w->transportVersionRead ? 1 : 0,
                       w->serviceVersionRead ? 1 : 0, w->lastError);
        break;
    }
}

// ---------------------------------------------------------------------------
// THE BINDING SCREEN'S ONE SUBJECT: IS INTERFACE 0 BOUND TO WinUSB.
//
// FIVE ANSWERS, and the first three are three for the same measured reason
// describeCable()'s are: enumKeyPresent means "this machine has seen this device at
// least once" and that record survives, so it separates "never seen" from "seen and
// not bound"; and guidOnOtherFunction separates "Zadig has not been run" from "Zadig
// was run on the wrong line of its list", which are the same red mark on two machines
// where the advice is opposite. Telling somebody who has just bound MI_01 to "run
// Zadig once" is telling them to redo what they have already done, on the one step of
// this installation that can leave the hardware unusable.
//
// *** AND THE OTHER TWO SEPARATE "APPLIED" FROM "APPLIED AND SEEN TO WORK", WHICH IS
//     THE ONLY AMBER THIS SCREEN HAS. *** interfacePresentNow is the device answering
// NOW; the binding is a property of the record Windows keeps and is applied whether or
// not the mixer is plugged in. Somebody who installs with the mixer unplugged - a
// common machine and not an exotic one - is bound, is allowed past, and is told that
// nothing about the hardware itself could be confirmed. This branch was the one
// installer/verify never drove, which said "FOUR ANSWERS" here for as long as there
// were five.
//
// *** > 0 AND NOT >= 0, WHICH IS THE SAME ARITHMETIC fillPreflightRows() USES. *** The
// field is -1 for "no sibling" and 0 is what a zeroed MachineState carries; read as
// an interface number, 0 would say "interface 0 is bound" and contradict guidPresent
// being false. So a state nobody filled in cannot invent a wrong binding.
//
// *** EVERY ONE OF THESE FIVE SENTENCES FITS ON ONE LINE, AND THAT IS THE WHOLE OF
//     THE BUDGET. *** This row is drawn ABOVE a 254 pixel picture on a 398 pixel
// strip, in a column 604 pixels wide at 96 DPI. A line of the row's small font is 13
// pixels there and 23 at 144 DPI, and the picture clears the fold by 7 and by 4. So a
// sentence that wraps to a SECOND line puts the bottom of the picture behind a scroll,
// on the screen whose only reason to exist is that it does not.
//
// TWO OF THE FIVE DID. "Interface %d ... the one already bound does no harm. Run Zadig
// again and pick the other line" was 139 characters and "Applied (%s), but the mixer is
// not answering, so nothing about the hardware itself could be confirmed" was 138; both
// wrapped, and on those two machines the picture ended 6 pixels past the fold at 96 DPI
// and 19 at 144. Nobody saw it because the harness rendered this screen on ONE invented
// machine, and that machine's row is one line. installer/verify now renders it once per
// reading, at both DPIs, and asks the layout where the picture ended.
//
// The one that hurt most was the wrong-interface machine: that person has already run
// Zadig once, is being told to run it again on the other line, and is exactly who most
// needs to compare the picture against what is on their screen.
//
// The long form of all of it - which box, which look-alikes, the way back out - is the
// pane on the screen before this one and the bullets under the picture.
//
// *** AND EVERY ANSWER THAT LEAVES Next GREY NOW NAMES THE CONTROL THAT UNDOES THE
//     REFUSAL. ***
//
// This is the only screen in either flow that REFUSES to be left, and until this
// round nothing on it said how to stop being refused. The greyed Next explained
// nothing; the foot note is not painted here at all (it is gated on the press that
// STARTS the work, which is a different screen, so /preview does not put one here
// either); and the only labelled alternative in the band was the door - "Already
// applied - continue anyway" - which is the CLAIM and not the honest path. The
// design's rule is a greyed button beside an EXPLANATION, and what stood here was a
// greyed button beside silence with an escape hatch next to it.
//
// *** IT IS IN THE ROW AND NOT IN A NOTE, AND THAT WAS MEASURED. *** The foot band on
// this screen is full: "Check again" ends at 128, the door runs 138..364 and Back
// begins at 422 at 96 DPI, which leaves footNote() a box 40 pixels wide after its own
// minimum kicks in. A sentence cannot go there. Nor can it go below the picture: the
// bullets and the caption are already past the fold, and this screen is 277 pixels
// over its strip at 96 DPI and 478 at 144, which installer/verify holds as a ceiling.
// The row is the only prose on this screen that a reader meets without scrolling.
//
// *** SO THE CLAUSE WAS PAID FOR OUT OF THE SENTENCES AND NOT OUT OF THE PICTURE. ***
// Measured lengths, all five: 103, 104, 80, 112, 99 characters, against a one-line
// budget of about 116 at 96 DPI. What was spent:
//   case 2  "this is how the driver reaches the hardware" - the mechanism, where
//           "nothing works before it" already gives the consequence. The mechanism is
//           in the walkthrough pane one screen back, in the console and in the log.
//   case 1  "it has to be interface 0" shortened to "not 0", which the sentence has
//           just named the alternative to. The reassurance was kept: it is the clause
//           that stops somebody trying to undo a binding that is doing no harm.
//   case 3  "nothing about the hardware itself could be confirmed" -> "nothing else
//           was confirmed". The binding IS confirmed - it was read - and "else" is
//           what says the rest was not.
// The interface guid stays in cases 3 and 4: it is 38 of those characters and it is
// the evidence that the binding is really there.
//
// The words come from bcdgui::kRecheckLabel rather than being spelled here, so the
// row and the button cannot come to name two different controls: rename the button
// and these sentences follow it. What installer/verify asserts is not the spelling -
// that cannot drift - but that each refusing reading still refers to the control at
// all, which is the thing that was missing.
//
// IT DECIDES NOTHING AND READS NOTHING: presentation of a MachineState that
// gatherMachineState() has already filled, which is what lets installer/verify call
// it with four invented machines and read the sentence back.
//
// *** AND THIS IS THE ONLY PLACE THE BINDING IS STATED NOW. *** The install screen
// carried a WinUSB row of its own for two rounds, on the argument that it was the only
// place the PROCEDURE was painted - true until the bullets under the picture above
// carried it. Once they did, the row was the same fact twice with nothing comparing
// the two, and the task that rebuilt that screen dropped it. The report did not lose a
// line: the screen's last bullet names this subject as one of the two the installer
// reads and will not change, and installer/verify asserts both halves - that the row
// is gone from there, and that this one still exists to have replaced it.
// ---------------------------------------------------------------------------
static void describeBinding(const MachineState* s, bcdgui::Row* out)
{
    if (!s->usb.enumKeyPresent) {
        bcdgui::setRow(out, bcdgui::kRowFail, L"The WinUSB binding",
                       L"Nothing for Zadig to rebind: this machine has never seen a "
                       L"BCD3000. Plug it in, then press %s.", bcdgui::kRecheckLabel);
        return;
    }
    if (!s->usb.guidPresent && s->usb.guidOnOtherFunction > 0) {
        bcdgui::setRow(out, bcdgui::kRowFail, L"The WinUSB binding",
                       L"Interface %d is bound, not 0, and that does no harm. Run "
                       L"Zadig on the other line, then press %s.",
                       s->usb.guidOnOtherFunction, bcdgui::kRecheckLabel);
        return;
    }
    if (!s->usb.guidPresent) {
        // *** AND THIS ROW IS WHERE THE PICTURE'S CAVEAT HAD TO GO, BECAUSE IT IS
        //     THE ONLY THING ON THIS SCREEN THAT IS ABOVE THE FOLD. *** Measured, at
        // 96 DPI: the strip is 398 logical pixels, the picture ends at 391, and the
        // page needs 655. So at rest a person sees the title, this row and the
        // photograph - and NOTHING else. The first line the fold hides was
        // "THESE TWO WILL LOOK DIFFERENT ON YOUR MACHINE", the caveat design section
        // 3.2 requires the caption to carry, on a screen that is only ever shown to
        // somebody whose mixer is NOT bound while the photograph is of one that is.
        // The picture ends 7 pixels above the fold, so the screen looks finished.
        //
        // *** THE BUDGET IS ONE WRAPPED LINE AND THAT IS WHY THE SENTENCE IS THIS
        //     SHORT. *** A second line of detail is 13 pixels at 96 DPI against 7 of
        // slack, so it would push the picture itself behind the scroll - the defect
        // this screen exists to remove. installer/verify holds the row to 116
        // characters for that reason and measures the picture's real clearance per
        // reading at both DPIs. 114 is what is left after the sentence that was
        // already here, and nothing is deleted to make room: the caption still
        // carries the caveat in full, and so does the walkthrough, the console and
        // the log. What this adds is the one clause that has to be READ AT REST.
        //
        // Only this reading gets it. Cases 0 and 1 are 103 and 104 characters and
        // there is no line left in them; case 2 is the ordinary "you have not run
        // Zadig yet" machine, which is the state this screen is for.
        bcdgui::setRow(out, bcdgui::kRowFail, L"The WinUSB binding",
                       L"Not applied yet, and nothing works before it. Run Zadig, "
                       L"then press %s. The picture will not match yours.",
                       bcdgui::kRecheckLabel);
        return;
    }
    if (!s->usb.interfacePresentNow) {
        bcdgui::setRow(out, bcdgui::kRowWarn, L"The WinUSB binding",
                       L"Applied (%s), but the mixer is not answering, so nothing "
                       L"else was confirmed.", s->usb.guid);
        return;
    }
    bcdgui::setRow(out, bcdgui::kRowOk, L"The WinUSB binding",
                   L"Applied and confirmed on the device that is connected now (%s).",
                   s->usb.guid);
}

// ---------------------------------------------------------------------------
// TAKING THE NAMED DOOR. See Screen::override in gui.h and section 4.2 of the design.
//
// *** IT CHANGES NOTHING ABOUT THE MACHINE AND EVERYTHING ABOUT THE RECORD. *** It
// does not set Screen::satisfied and it does not touch the MachineState: what this
// program measured stays what it measured, and every row, every summary line and
// every exit code goes on being computed from the reading rather than from a claim.
// What it does is put the claim where a claim belongs - in the log file, in the
// console and in the exit code - so that a support request three weeks later carries
// "this person said the binding was already there" instead of an unexplained pass.
//
// *** THE EXIT CODE IS FORCED AND NOT INFERRED, AND THAT DISTINCTION IS THE WHOLE
//     POINT. *** On the machine this door exists for - the binding IS applied and the
// registry read failed for some other reason - pending->winUsbBindingMissing is
// computed from that same failed read, so the exit code would come out 3 anyway and
// this flag would look redundant. It is not: it is the only path by which the 3 is
// caused by SOMEBODY HAVING PRESSED THIS, and installer/verify asserts exactly that
// by taking the door on a machine that is otherwise completely healthy - where every
// other pending flag is false and the run would finish 0.
//
// ON THE WINDOW THREAD, unlike the offer and the re-check: it reads nothing.
// ---------------------------------------------------------------------------
static int overrideBinding(void* user)
{
    Run* run = (Run*)user;
    if (!run)
        return 0;
    run->bindingDoorTaken = true;
    sayWarn(L"The \"%s\" door was taken on the binding screen: this run was told the "
            L"WinUSB binding is already applied, and what this machine reported is "
            L"unchanged above. This installation will finish with exit code 3 - done, "
            L"with something still pending.", kBindingDoorLabel);
    return 1;
}

// ---------------------------------------------------------------------------
// *** SOMEBODY PICKED A MIXER ON THE DEVICE SCREEN. ***
//
// ON THE WINDOW THREAD, like overrideBinding() and unlike a contextual action's worker:
// it records a selection and says it. It reads no registry and enumerates no
// processes, which is the whole of why a worker would need its own thread and this
// does not.
//
// *** IT WRITES ONE FIELD AND SAYS ONE LINE, AND THE SAYING IS NOT DECORATION. *** This
// choice changes nothing about what gets installed - the driver carries both profiles
// and matches on the USB ids at run time - so if it were not recorded it would be a
// control with no consequence anywhere, which is the "declared and unread" shape this
// project has now found eleven times. What it IS for is the log: say() puts this line in
// the console, in the log file, in the window's own pane and therefore in a support
// request, so three weeks later somebody can read which mixer this run was told it was
// for. That is exactly the job the named door's line does for the binding claim.
//
// *** THE EXPERIMENTAL PATH IS SAID AS A WARNING AND THE PROVEN ONE AS INFORMATION, AND
//     THE TEST IS THE FLAG RATHER THAN THE INDEX. *** modelProvenOnHardware() is the
// fact; comparing the index against kModelBcd2000 would be a second place that fact is
// encoded, and the day a third profile arrives the wrong one of the two would be updated.
//
// IT DOES NOT TOUCH THE EXIT CODE, and that is a decision rather than an omission.
// Exit code 3 means "done, with something still pending" and is computed from what this
// run MEASURED plus the one claim somebody made about a measurement. Saying which mixer
// you have is neither: nothing is pending because of it, and nothing was claimed about a
// reading. A choice that quietly changed the exit code would make an honest answer look
// like a failure.
//
// Returns non-zero when the choice was accepted, which is what gui.cpp does with the
// answer. A bad index is refused rather than stored: the window offers exactly the
// options the table gave it, so an index outside the model table means the two have
// disagreed and storing it would put a name nothing owns in the log.
// ---------------------------------------------------------------------------
static int chooseModel(void* user, int index)
{
    Run* run = (Run*)user;
    if (!run || index < 0 || index >= kModelCount)
        return 0;
    // Idempotent, and it is what keeps the log readable: syncChoiceButtons() copies the
    // table down into the controls after every re-check, and a repeated press of the
    // option that is already on would otherwise write the same line again and again.
    if (run->selectedModel == index)
        return 1;
    run->selectedModel = index;
    if (modelProvenOnHardware(index))
        sayInfo(L"This installation was told it is for a %s - the model this driver was "
                L"proven on.", modelName(index));
    else
        sayWarn(L"This installation was told it is for a %s. That path is EXPERIMENTAL "
                L"and has never been run: nobody on this project owns one. The control "
                L"surface is not supported yet - the knobs, buttons and LEDs will not "
                L"work - and the audio is expected to work rather than known to. What "
                L"gets installed is the same either way; this line is here so a support "
                L"request says which mixer it was for.",
                modelName(index));
    return 1;
}

// ---------------------------------------------------------------------------
// WHICH MIXER THIS RUN WAS TOLD IT IS FOR: 0 for the BCD3000, 1 for the BCD2000, and
// they are indices into common.h's model table rather than a private encoding.
//
// *** IT READS THE RUN AND NOT THE WINDOW, WHICH IS WHY IT CAN BE ASKED AFTER THE
//     WINDOW HAS CLOSED. *** The controls are gone by the time runSteps() finishes and
// the summary is printed; the answer is not, because the answer was never held by a
// control. See Run::selectedModel and chooseModel().
//
// A run nobody chose on answers kModelBcd3000, which is honest rather than convenient:
// it is the flow's own default, it is what the screen shows selected, and it is the
// only model this driver has been proven on.
// ---------------------------------------------------------------------------
int selectedModel(void)
{
    return g_run.selectedModel;
}

// ---------------------------------------------------------------------------
// The flow, as data.
//
// *** IT READS THE MachineState NOW, AND THAT IS THE CHANGE THIS ROUND MAKES. ***
// It used to take one and mark it unused. Entry 1 below is a screen with one
// subject and four things it might have to say about it, and which of the four is
// decided here, from the state gatherMachineState() filled before the window opened.
// The consequence for the harness is that buildScreens() is no longer a constant
// function of nothing: the same call on two different machines produces two
// different tables, so a suite that calls it with ONE invented machine tests one
// machine out of four. installer/verify calls it with all four.
//
// *** IT IS ALSO CALLED AGAIN AFTER EVERY RE-CHECK, ON THE WINDOW THREAD. *** See
// rebuildScreens() below. That is why it assigns every field it cares about rather
// than assuming a zeroed table: it has to be able to run over a table that is
// already filled and leave it describing the machine as it is NOW.
// ---------------------------------------------------------------------------
static void buildScreens(bcdgui::Wizard* w, const MachineState* s)
{
    bcdgui::Screen* sc = w->screens;

    // The device screen's interpolated words. Called here, every time, because it is
    // pure assignment from constants - the model names out of common.cpp and the
    // repository address out of gui.cpp - so there is nothing to be gained by asking
    // whether it has already run and one more piece of state to get wrong if we did.
    buildDeviceWords();

    // 0 - the opening. Measures nothing; it is who wrote this and what it will do.
    //
    // *** ITS BUTTON SAYS Start, AND THAT IS THE OWNER'S OWN CORRECTION. *** It said
    // Install and it installed nothing - it turned the page. This project spent two
    // rounds removing SENTENCES that claimed something that had not happened, and
    // went past this one six times because a button does not read like a sentence.
    // Start is what pressing it does: it starts the wizard, and nothing else.
    sc[0].kind             = bcdgui::kScreenInfo;
    sc[0].title            = kOpeningTitle;
    sc[0].primaryLabel     = L"Start";
    sc[0].bullets[0]       = kOpeningBullet1;
    sc[0].bullets[1]       = kOpeningBullet2;
    sc[0].bullets[2]       = kOpeningBullet3;
    sc[0].bullets[3]       = 0;
    sc[0].showDevicePhoto  = true;
    // *** AND THIS IS THE ENTRY THAT PAINTS THE WELCOME, SAID BY THE TABLE. *** The
    // renderer chose renderWelcome() by KIND until this round, which was true while
    // this was the only kScreenInfo in the flow. Entry 3 is the second, and without
    // this field it would have drawn this page's words instead of its own. See
    // Screen::paintsOpening.
    sc[0].paintsOpening    = true;

    // 1 - IS THE MIXER HERE. The first subject to get a screen of its own, and it
    // is first in the flow for a measured reason and not for a tidy one: see
    // describeCable() above, and section 3.1 of the design.
    //
    // *** IT DOES NOT SET startsTheWork, AND THAT FIELD IS WHY THIS SCREEN IS SAFE
    //     TO ADD. *** Before it existed, primaryActionFor() answered kPrimaryStart
    // for every kScreenCheck, so inserting this entry would have given the program a
    // SECOND button that writes to this machine - wearing the word Next. Measuring
    // something is not installing it. This screen advances, and installer/verify
    // asserts that exactly one screen in the flow starts the work.
    //
    // Its row is the answer for the machine this table was built from; there is no
    // measure() pointer on it, because a screen's own row has no channel from a
    // worker thread yet - the whole table is rebuilt on the window thread instead,
    // after the re-check the "Check again" button already runs. See rebuildScreens().
    sc[1].kind             = bcdgui::kScreenCheck;
    sc[1].title            = kMixerTitle;
    sc[1].primaryLabel     = L"Next";
    sc[1].bullets[0]       = kMixerBullet1;
    sc[1].bullets[1]       = kMixerBullet2;
    sc[1].bullets[2]       = kMixerBullet3;
    sc[1].bullets[3]       = 0;
    describeCable(s, &sc[1].row);
    sc[1].satisfied        = s->usb.interfacePresentNow;
    // blockNextWhenUnmet stays false: a person whose mixer is unplugged can still
    // install the machine wide half, which is real progress, and exit code 3 already
    // exists to say something is pending. Only the WinUSB binding screen blocks.

    // 2 - WHICH MIXER. The design's screen 2, and the last screen this redesign adds.
    //
    // WHERE IT SITS AND WHY. After the cable and before everything else, because a
    // detection needs the device present - which is the design's own reason, and it is
    // the same measured fact that put the cable first: Windows records a device the
    // first time it is plugged in.
    //
    // *** IT IS kScreenCheck, AND THAT IS THE ONE STRUCTURAL DECISION ON THIS SCREEN.
    //     *** It carries two radio buttons, which none of the four kinds describes, so
    // the choice was a new kind or this one. It is this one for three reasons, in
    // increasing order of how hard they are to argue with:
    //
    //   - IT REALLY MEASURES SOMETHING. describeModel() reads the MachineState and
    //     reports which mixer answered, in Screen::row, exactly like the three check
    //     screens before it. "Get Zadig" is kScreenInfo because there is NOTHING a row
    //     there could honestly say; here there is, and kScreenInfo would throw away an
    //     honest row to gain nothing.
    //   - THE HARNESS PREDICTED THIS. The count above countOfKind() in
    //     installer\verify says kScreenCheck is deliberately not counted "because Tasks
    //     3 to 6 make it five". Four screens carry that kind today. This is the fifth,
    //     and the prediction was written three tasks before this one.
    //   - A NEW KIND WOULD NOT COMPILE CHEAPLY. MSVC's C4062 makes an unhandled
    //     enumerator in a switch with no default an ERROR at /W4 /WX, and this program
    //     switches over ScreenKind in both the product and the harness. A fifth kind
    //     would break every one of those for a screen that renders like a check screen
    //     anyway.
    //
    // *** blockNextWhenUnmet STAYS FALSE, AND HERE IT COULD NOT BE ANYTHING ELSE. ***
    // The unmet state on this screen is "nothing was detected", and the whole design of
    // the screen is that the person then chooses by hand. A screen that refused to be
    // left until a detection succeeded would refuse a BCD2000 owner for ever: the one
    // registry key this program reads is the BCD3000's, so on that machine the detection
    // cannot succeed at all. See kDeviceBullet2.
    //
    // *** NO PANE, AND IT IS ARITHMETIC RATHER THAN taste. *** A screen with a pane gets
    // 221 logical pixels of strip at 96 DPI. This one paints its title, its rule, its
    // row, two measured option rows and three bullets, and that is well past 221 - it
    // fits the 398 a screen with no pane gets, with room to spare, and installer\verify
    // holds it at deficit ZERO because a screen it does not name is allowed no overflow.
    // That ratchet is also what makes the two controls safe on the page: see
    // kChoiceLeadH in gui.cpp.
    sc[2].kind             = bcdgui::kScreenCheck;
    sc[2].title            = kDeviceTitle;
    sc[2].primaryLabel     = L"Next";
    sc[2].bullets[0]       = kDeviceBullet1;
    sc[2].bullets[1]       = kDeviceBullet2;
    sc[2].bullets[2]       = g_deviceRepoBullet;
    sc[2].bullets[3]       = 0;
    // *** THE SELECTION IS DECIDED BEFORE THE ROW, AND THE ORDER IS THE FIX. *** The row
    // is a statement about what was found AND what this run is set up for, so it cannot
    // be written before the second of those is known. When the row came first it
    // described a selection it could not see, which is exactly how it came to claim the
    // detected model was selected on a machine where the person had picked the other one.
    // See describeModel() and Screen::choiceSelected.
    {
        const Run* run       = (const Run*)w->user;
        sc[2].choiceSelected = run ? run->selectedModel : kModelBcd3000;
    }
    describeModel(s, sc[2].choiceSelected, &sc[2].row);
    // *** satisfied IS THE DETECTION AND NOT THE CHOICE, WHICH IS THE SAME RULE THE
    //     BINDING SCREEN'S DOOR OBEYS. *** Picking a line does not make a mixer appear.
    // Nothing reads this field on this screen - blockNextWhenUnmet is false - and it is
    // written anyway because the field means "what did the machine say", and a screen
    // that answered it with "what did the person say" would be the one place in this
    // program where a claim was stored as a measurement.
    sc[2].satisfied        = s->usb.interfacePresentNow;
    sc[2].blockNextWhenUnmet = false;
    sc[2].choiceLabels[0]  = g_modelLabel[kModelBcd3000];
    sc[2].choiceLabels[1]  = g_modelLabel[kModelBcd2000];
    // *** THE SELECTION COMES FROM THE RUN AND NOT FROM THE WINDOW, WHICH IS WHY IT
    //     SURVIVES A RE-CHECK. *** rebuildScreens() calls this function again over the
    // machine the re-check has just read, so anything the window held for itself would
    // be overwritten silently. The run owns the choice; this line copies it into the
    // table; gui.cpp's syncChoiceButtons() copies the table into the controls. One
    // direction, three steps, no second author.
    //
    // *** AND IT IS READ OFF THE Run RATHER THAN LEFT ZERO, WHICH IS THE HALF A TEST
    //     WOULD MISS - AND DID MISS, FOR A WHOLE ROUND. *** A zeroed table already means
    // kModelBcd3000, so a line that simply did not assign this would look correct until
    // somebody picked the BCD2000 and pressed Check again, at which point the window
    // would silently go back to the BCD3000 and the log would disagree with the screen.
    // That is what this comment said, and every check in the suite still passed with the
    // line replaced by the constant, because no test ever built a run whose selection was
    // anything but zero. installer\verify now builds one: see the "a run that chose the
    // OTHER model" block in testDeviceScreen(). The assignment itself moved UP, above
    // describeModel(), because the row has to be able to see it.
    sc[2].choose           = chooseModel;

    // 3 - THE MIDI PORT. The second subject to get a screen, and the first screen in
    // this program to carry a pane.
    //
    // *** THE SCREEN HAS ITS NEW SUBJECT, AND IT STILL OFFERS NOTHING. *** The offer,
    // the address block and the three-way gate that used to stand here were built on
    // a third party detection that is gone; they are NOT rebuilt, because Windows
    // MIDI Services is in-box and there is nothing left for a button to install or
    // for an address to point at. What the screen does now is READ and REPORT - see
    // describeMidiPort() for the three states and their exact words, and the block
    // over WinMidiInfo in common.h for what is read and what is deliberately not.
    //
    // *** THE ROW IS THE ONLY THING THAT VARIES, AND THAT IS A LAYOUT DECISION AS
    //     MUCH AS AN EDITORIAL ONE. *** allowedDeficit() holds this screen at ZERO
    // overflow, so a second bullet appearing in one state and not another would make
    // the screen's height depend on the machine and take the measured worst case out
    // of anybody's hands. One row, three sentences, one of which is longer than the
    // others - and installer/verify photographs the LONGEST one for exactly that
    // reason. See shotState().
    //
    // blockNextWhenUnmet STAYS FALSE for the reason it always did: this screen's
    // subject is the knobs, the buttons and the LEDs, not the audio path, and the
    // binding screen is the one entry in either flow that blocks, because without the
    // binding nothing works at all.
    sc[3].kind             = bcdgui::kScreenCheck;
    sc[3].title            = kMidiTitle;
    sc[3].primaryLabel     = L"Next";
    sc[3].bullets[0]       = kMidiBullet1;
    sc[3].bullets[1]       = 0;
    sc[3].bullets[2]       = 0;
    sc[3].bullets[3]       = 0;
    sc[3].paneCaption      = kMidiPaneCaption;
    sc[3].paneText         = g_midiPaneText[0] ? g_midiPaneText : 0;
    describeMidiPort(s, &sc[3].row);
    sc[3].satisfied        = true;
    sc[3].blockNextWhenUnmet = false;
    sc[3].actionLabel      = 0;
    sc[3].action           = 0;
    sc[3].actionOnlyOpensAPage = false;
    sc[3].addressLead      = 0;
    sc[3].addressUrl       = 0;
    sc[3].addressOpen      = 0;

    // 4 - GET ZADIG. Download only, and deliberately short: it exists so that the
    // screen after it has ONE subject, which is the picture and what to match in it.
    //
    // *** IT IS kScreenInfo AND NOT kScreenCheck, AND THAT IS A STATEMENT ABOUT WHAT
    //     THIS PROGRAM CAN SEE. *** Zadig is a single portable executable that
    // installs nothing, registers nothing and leaves no key behind. There is no
    // reading that could tell this machine whether it has been downloaded, so a row
    // here would be a mark this program cannot honestly paint - and a screen that
    // reported on something it did not measure is the class this project has removed
    // twice. It says what to fetch and turns the page.
    //
    // ITS PANE IS STEP 2 OF THE WALKTHROUGH IN FULL, the same buffer the same say()
    // calls filled, so the panel, the console and the log file cannot drift apart.
    // The screen after this one cannot have a pane at all - see kBindingTitle - so
    // the long form of the procedure lives here, one screen before it is needed.
    sc[4].kind             = bcdgui::kScreenInfo;
    sc[4].title            = kZadigTitle;
    sc[4].primaryLabel     = L"Next";
    sc[4].bullets[0]       = kZadigBullet1;
    sc[4].bullets[1]       = kZadigBullet2;
    sc[4].bullets[2]       = kZadigBullet3;
    sc[4].bullets[3]       = 0;
    sc[4].paneCaption      = kZadigPaneCaption;
    sc[4].paneText         = g_zadigPaneText[0] ? g_zadigPaneText : 0;
    // *** THE ADDRESS AND THE BUTTON THE OWNER ASKED FOR, BUILT HERE FIRST. *** This
    // screen HAD 53 logical pixels of slack at 96 DPI and 60 at 144 and nothing had to
    // leave it, so the arrangement was got right here before being repeated on the MIDI
    // port screen, where a line costs a line. The block then spent 31 and 46 of it: this
    // screen is now at 199 of 221 and 316 of 330, which is 22 and 14 of slack and makes
    // it the tightest screen in the flow at 144 DPI. See kZadigAddressLead.
    //
    // UNCONDITIONAL, AND THAT IS NOT AN EXCEPTION TO THE STATE RULE - see
    // kZadigAddressLead. The rule is about a state this program can SEE; Zadig installs
    // nothing and leaves no key, which is the recorded reason this screen has no row at
    // all, so there is no "already present" here to be honest about.
    sc[4].addressLead      = kZadigAddressLead;
    // The PUBLISHED constant, for the reason the MIDI port screen's is: the pane, the
    // console, the log file and the pending summary all print this one. See common.cpp.
    sc[4].addressUrl       = kZadigDownloadPage;
    // *** AND IT OPENS ITSELF, WHICH IS THE SCREEN THE OWNER POINTED AT. *** "No passo do
    // zadig tem o botao la embaixo para ir para o site, mas o link azul no comeco da
    // pagina deveria ter link tambem." The button at the foot stays exactly as it was -
    // it is the keyboard's and the screen reader's way to the same page, and a painted
    // region is neither. The same opener the MIDI port screen uses, handed this screen's
    // own address by gui.cpp at the moment of the click.
    sc[4].addressOpen      = openDownloadAddress;
    // *** AND THE SECOND SCREEN IN THIS FLOW TO CARRY A CONTEXTUAL BUTTON, WHICH IS A
    //     COUNT installer/verify HELD AT ONE UNTIL THIS ROUND. *** The check there was
    // "the contextual button stands on exactly ONE screen": correct while one screen
    // carried an offer, and an obstacle to the second the moment there was one. It is a
    // per-screen agreement between the control and the table now, over the whole flow,
    // which is the property that was actually worth asserting - a table offering on two
    // screens was never the defect; a WINDOW showing the button on a screen the table is
    // silent about was.
    sc[4].actionLabel      = kZadigOpenPageLabel;
    sc[4].action           = openZadigPage;
    // *** AND /preview LEAVES IT ALIVE, WHICH IT DID NOT UNTIL 2026-08-01. *** This
    // button opens a web page and does nothing else - it is the one control in this
    // program whose comment already said so in capitals, "IT NEVER STARTS ZADIG". The
    // round that made the address clickable left it grey beside a live blue link that
    // opens the SAME page: one consequence, two controls, opposite answers. /preview
    // promises that nothing is written and nothing is registered, and this writes and
    // registers nothing. See Screen::actionOnlyOpensAPage.
    sc[4].actionOnlyOpensAPage = true;

    // 5 - APPLY THE WinUSB BINDING. The picture gets the screen.
    //
    // *** AND THIS IS THE ONE ENTRY IN EITHER FLOW THAT BLOCKS Next. *** The owner
    // chose it and the two dependencies are genuinely not equal: without the MIDI port
    // the audio works and only the knobs, the buttons and the LEDs die; without this
    // binding NOTHING works, because it is how the driver reaches the hardware.
    // Severity follows consequence, which is the same principle the row marks already
    // use. It is also the first consumer nextAllowed() has ever had.
    //
    // *** AND BLOCKING IS ONLY DEFENSIBLE BECAUSE OF THE DOOR BESIDE IT. *** What can
    // fail here is not the detection of the DEVICE - that has never once been wrong on
    // this project; twice the controller's DIAGNOSIS was wrong while the reading was
    // right - but the READING OF THE BINDING, which lives under the device's Device
    // Parameters. A machine where that read fails for some other reason would be
    // stranded on a screen telling somebody to run Zadig when Zadig has already been
    // run, and that is precisely the defect round 2c998fa removed from page 2.
    //
    // *** NO PANE, AND IT IS ARITHMETIC. *** A screen with a pane gets 221 logical
    // pixels of strip at 96 DPI. The picture alone is 254. See kBindingTitle.
    sc[5].kind             = bcdgui::kScreenCheck;
    sc[5].title            = kBindingTitle;
    sc[5].primaryLabel     = L"Next";
    sc[5].bullets[0]       = kBindingBullet1;
    sc[5].bullets[1]       = kBindingBullet2;
    sc[5].bullets[2]       = kBindingBullet3;
    sc[5].bullets[3]       = kBindingBullet4;
    sc[5].showZadigShot    = true;
    describeBinding(s, &sc[5].row);
    // *** satisfied IS THE READING AND NOTHING ELSE, INCLUDING AFTER THE DOOR HAS BEEN
    //     TAKEN. *** The door advances the flow; it does not make this true. If it
    // did, every row, every summary line and the exit code would be computed from a
    // claim instead of from a measurement, which is the one thing this program has
    // never done.
    //
    // guidPresent AND NOT interfacePresentNow: the binding is a property of the
    // RECORD Windows keeps for the device, and it is applied whether or not the mixer
    // is plugged in at this moment. Blocking on the cable here would be blocking on
    // the screen two screens back, which does not block on it and says why.
    sc[5].satisfied        = s->usb.guidPresent;
    sc[5].blockNextWhenUnmet = true;
    sc[5].overrideLabel    = kBindingDoorLabel;
    sc[5].override         = overrideBinding;

    // 6 - INSTALL THE DRIVER. The design's screen 6, and it is the screen this one
    // has always been on the way to being: "what will be written and where, and what
    // will not be touched".
    //
    // *** IT WAS NOT DELETED, IT WAS REBUILT, AND THE PLAN LOST THAT. *** Two tasks
    // timed themselves against "the review screen ceases to exist", and nothing
    // anywhere deleted it - because five of its seven rows had nowhere else to go.
    // They are, one for one, this screen's subject: what we write, whether we may
    // write it, who we write it for, and the one choice that survives the writing.
    // The two that DID have somewhere else to be are gone; see fillPreflightRows().
    //
    // *** AND ITS BUTTON IS THE ONE THAT SAYS Install, BECAUSE IT IS THE ONE THAT
    //     INSTALLS. *** Pressing it here calls startWork(): files and registry keys
    // are written from this press and from no other in the program. The design puts
    // that press on this screen, so nothing about it moved in this round - the word,
    // the field below it and the worker behind it are the ones that were already
    // right and already reviewed.
    sc[6].kind             = bcdgui::kScreenCheck;
    sc[6].title            = kInstallTitle;
    sc[6].primaryLabel     = L"Install";
    // *** THE HALF OF THE DESIGN THAT NO SCREEN STATED UNTIL NOW. *** The rows say
    // what is on this machine and therefore what will be written; nothing said what
    // will NOT be. See kInstallBullet2: the two subjects it names are exactly the two
    // whose rows left this page, and saying so here is what stops their removal
    // reading as their deletion.
    sc[6].bullets[0]       = kInstallBullet1;
    sc[6].bullets[1]       = kInstallBullet2;
    sc[6].bullets[2]       = 0;
    sc[6].bullets[3]       = 0;
    // *** AND THIS IS THE FIELD THAT MAKES THE PRESS AN INSTALL. *** It is written
    // here, beside the word that names it, and on no other entry in this flow: being
    // a check screen no longer implies starting the work, so the check screens Tasks
    // 3 to 6 split this one into advance unless one of them says this.
    sc[6].startsTheWork    = true;
    // *** THE FIELD STAYS, AND WHAT IT GRANTS IS ONE THING NOW RATHER THAN SIX. ***
    //
    // It was written to give this entry the whole of the old page 2 - the review rows,
    // the walkthrough pane, the Zadig picture, the "Check again" button, the
    // contextual action and the note beside them - and it was measured before this
    // round to find out how much of that it still does. The answer is one:
    //
    //   the rows          THIS FIELD, and only this. onMachineReview() has exactly one
    //                     reader left, renderCheckScreen(), which chooses
    //                     renderReview() over renderSubject() on it.
    //   the pane          gone from this screen in the round that gave the MIDI port
    //                     one, and read off Screen::paneText for every screen since.
    //   the picture       entry 5's, read off Screen::showZadigShot.
    //   "Check again"     on EVERY check screen - gated on the kind, because a
    //                     re-check really does change what any of them says.
    //   the action        read off Screen::actionLabel, so it stands beside its subject.
    //   the note          on the screen whose press STARTS the work, which is this one
    //                     for a different reason: primaryActionFor(), not this field.
    //
    // *** SO WHY IT IS NOT DELETED, WHICH IS THIS TASK'S JUDGEMENT AND NOT AN
    //     OVERSIGHT. *** This screen paints FIVE rows and renderSubject() paints ONE -
    // Screen::row, the entry's own answer about its own subject. The five here are not
    // one subject: they arrive by index through postReviewRow() while the re-check
    // worker is still running, which is what makes them fill in progressively, and a
    // Screen has no such channel and needs none for a single row. Deleting the field
    // would mean giving Screen an array and a channel to carry what Wizard::review and
    // postReviewRow() already carry correctly - new machinery to hold the same five
    // sentences. What the field says is true and is now the whole of what it says.
    sc[6].paintsMachineReview = true;

    // 7 - the work. The progress bar and the live log.
    //
    // Its primaryLabel is "Close" because that is the string this window really
    // puts in that control here: refreshButtons() writes the label and then HIDES
    // the button, so the table has to say what was written or it would be
    // describing a control the window does not make. It is NOT "Install": the
    // press that installs happens on the screen above, and the word has to be where
    // the press is - installer/verify checks that the flow's verb sits on exactly the
    // screen whose primaryActionFor() is kPrimaryStart.
    sc[7].kind             = bcdgui::kScreenWork;
    sc[7].title            = kWorkTitle;
    sc[7].primaryLabel     = L"Close";

    // 8 - the summary, and it is the last.
    sc[8].kind             = bcdgui::kScreenDone;
    sc[8].title            = kDoneTitle;
    sc[8].primaryLabel     = L"Close";
}

}

// ---------------------------------------------------------------------------
// The table, restated from a machine that has just been read again.
//
// *** IT RUNS ON THE WINDOW THREAD, AND THAT IS THE WHOLE OF ITS THREAD SAFETY. ***
// gui.cpp calls it from the handler for "the re-check has finished", which arrives
// as a posted message after recheckProc() has already returned from
// recheckMachine() - so the worker has finished writing run->state before this line
// reads it, and the Screen rows it writes are read by the painter on this same
// thread. There is no second copy and no lock, because there are no two threads.
//
// WHY IT REBUILDS THE WHOLE TABLE instead of restating one row: buildScreens() is
// pure assignment from constants and from the state, so running it twice is running
// it once with a fresher machine. A function that knew which entries carry a
// measured row would be a second list to keep equal to the first, which is what
// screenCount() exists to avoid.
//
// WHAT IT IS NOT: a measurement. Nothing here reads the registry or the device -
// recheckMachine() did that, on its own thread, before this was reached.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// THE SENTENCE UNDER THE INSTALL SCREEN'S ROWS, AND IT IS ONE SENTENCE AGAIN.
//
// *** IT WAS TWO FOR ONE ROUND, AND THE SECOND ONE COULD NOT BE REACHED. *** The
// history is worth keeping because the defect it fixed was real. This footer said
// "Nothing here has been touched ... all that has happened so far"
// unconditionally, and a person who had taken the accelerated install offer on
// screen 3 had had winget run somebody else's installer two screens earlier - so
// on that path the reassurance was false on the screen immediately before the
// press that installs. The fix was a second sentence, kReviewFooterAfterOffer,
// chosen by a selector reading Run::thirdPartyStarted.
//
// *** FIX ROUND 1: THE SECOND SENTENCE AND THE SELECTION ARE DELETED, BECAUSE THE
//     OFFER THAT MADE THE FIRST ONE FALSE IS GONE AND NOTHING CAN SET THE FLAG. ***
// Task 5 removed the third party detection and the offer built on it. That left a
// footer no shipped run could print, chosen by a field no shipped run could write,
// with three harness suites keeping both counted by setting the flag by hand. A
// sentence the program cannot reach is not a safeguard; it is the appearance of
// one, which is worse, because it reads to the next person as though this path
// were covered. See the block where Run::thirdPartyStarted was declared for why no
// task left in this plan brings it back.
//
// WHAT SURVIVES UNCHANGED, and it is the load-bearing half: ONE AUTHOR FOR THE
// VALUE. runWindowed() sets the initial footer from this function and
// rebuildScreens() sets it again from this function, so there is no second place
// deciding what the screen carries. The function stays, and it stays a function
// even though it now has one answer, for exactly that reason - inlining the
// constant at two call sites is how a screen ends up with two authors and, one
// round later, two different sentences. It is also the seam a future offer needs:
// restoring the branch is an edit HERE and nowhere else.
// ---------------------------------------------------------------------------
static const wchar_t* const kReviewFooterUntouched =
    L"Nothing here has been touched. Reading the registry and "
    L"looking for files is all that has happened so far, and "
    L"closing this window now changes nothing.";

// *** kReviewFooterAfterOffer STOOD HERE AND IS DELETED WITH THE FLAG THAT CHOSE
//     IT. *** It read: "This installer has still written nothing - reading the
// registry and looking for files is all it has done. But another program was
// started from this window earlier in this run, and closing the window does not
// undo it."
//
// One thing about it is worth carrying forward for whoever writes its replacement,
// because it was measured and the measurement cost a correction: LENGTH IS A
// LAYOUT DECISION ON THIS SCREEN. The first draft was 298 characters, and forced
// through the render pass it took the install screen from 543 to 556 logical
// pixels at 96 DPI and from 886 to 909 at 144 - one wrapped line each - turning
// the ratchet RED at both DPIs. The shipped version was 209 characters and fitted
// inside the same two wrapped lines the reassurance occupies, which is what let
// the harness assert the two rendered heights were EQUAL rather than raise a
// budget to admit a wording. A replacement sentence has the same 209 character
// room and the same obligation to prove it by rendering, not by argument.
//
// Also deleted with it: the render-pass check that asserted that equality, at both
// DPIs. It measured the cost of a state this program can no longer enter, and a
// check whose subject is unreachable cannot fail for the reason it was written.

// One answer, and still a function - see the block above for why the seam stays.
// The Run argument stays too: it is what a restored branch reads, and dropping it
// would make bringing one back a change at every call site instead of here.
static const wchar_t* reviewFooterFor(const Run* run)
{
    (void)run;
    return kReviewFooterUntouched;
}

static void rebuildScreens(bcdgui::Wizard* w, void* user)
{
    Run* run = (Run*)user;
    if (!w || !run)
        return;
    buildScreens(w, &run->state);
    // The one thing on this page that is a property of what HAPPENED in this run
    // rather than of what was measured, which is why buildScreens() - a pure
    // function of the MachineState - cannot be the place it is decided.
    w->reviewFooter = reviewFooterFor(run);
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

    g_wiz.welcomeLine1      = kOpeningTitle;
    g_wiz.welcomeLine2      = L"This installs an ASIO driver that reaches the mixer "
                              L"over WinUSB, plus a small service that carries the "
                              L"knobs, the buttons and the LEDs. It needs no signed "
                              L"kernel driver, so there is no Windows protection to "
                              L"switch off - which is the whole reason it exists.";
    g_wiz.welcomeBullets[0] = kOpeningBullet1;
    g_wiz.welcomeBullets[1] = kOpeningBullet2;
    g_wiz.welcomeBullets[2] = kOpeningBullet3;
    g_wiz.welcomeBullets[3] = 0;
    g_wiz.showDevicePhoto   = true;
    g_wiz.showNotAffiliated = true;

    // ONE string for the screen's title and for the caption renderReview() paints at
    // the top of it, because they are the same words on the same screen and two copies
    // of a caption is the drift Task 1 closed for kOpeningTitle and its siblings.
    g_wiz.reviewCaption = kInstallTitle;
    // From the same function rebuildScreens() uses, so the sentence has one author.
    // Nothing has been started yet at this point, so this is kReviewFooterUntouched -
    // but it is asked rather than assumed, because "the flag is false here" is the
    // kind of fact that stops being true the day somebody moves this call.
    g_wiz.reviewFooter  = reviewFooterFor(run);
    // The pane under the rows is described by the ENTRY that paints these rows now,
    // not by this structure - see buildScreens() below, and the block where
    // Wizard::reviewPaneText used to be. The two fields that were here held the same
    // caption and the same buffer the table already carried.

    // *** THE CAPTION UNDER THE ZADIG SCREENSHOT, AND WHY IT SPENDS MOST OF ITSELF ON
    //     WHAT WILL NOT MATCH. ***
    //
    // The picture was taken on a machine whose mixer is already bound. Two of its
    // fields therefore show a state a user about to run Zadig will not see - the
    // Driver box and the label on the big button - and a picture that promises one
    // screen and delivers another is exactly the defect this installer's text spent
    // two rounds having removed. It is still the right picture, because the two
    // things a user has to MATCH are identical in both states: the line in the list
    // and the USB ID. So the caption points at those two and declares the other two.
    //
    // *** THE BUTTON'S NAME IS DECLARED AS UNKNOWN RATHER THAN GUESSED. *** Zadig
    // chooses between Install, Replace, Reinstall and Upgrade according to what is
    // already bound, and not every variant has been seen on a machine here. "It is
    // the large button, whatever it is called" is true without inventing precision;
    // naming one of them would be this page stating something nobody measured.
    // installer/verify asserts that no single label is presented as the one to
    // expect.
    //
    // IT IS WRITTEN TO READ WITHOUT THE PICTURE, because the picture is dropped in
    // silence if the resource cannot be decoded. Every field is named by its own
    // label - the Driver box, the USB ID row, the More Information column - and not
    // by where it sits in the image.
    g_wiz.zadigCaption =
        L"Zadig, photographed on the machine this driver was built on. MATCH THESE "
        L"TWO: the line in the list reads BCD3000 (Interface 0), and the USB ID row "
        L"under it reads 1397 00BF - its third small box is the interface number and "
        L"has to be 00.\n"
        L"THESE TWO WILL LOOK DIFFERENT ON YOUR MACHINE, and that is expected, "
        L"because this one is already bound: here the Driver box on the left reads "
        L"WinUSB and the button reads Reinstall Driver. On a mixer that has not been "
        L"bound yet the Driver box names whatever Windows put there - usbaudio or "
        L"something like it - and the button is more likely to read Replace Driver "
        L"or Install Driver. Zadig picks that word from what is already bound, and "
        L"not every variant of it has been seen here: it is the large button, "
        L"whatever it is called.\n"
        L"What you change is the target box to the RIGHT of the green arrow, with "
        L"the small up and down arrows beside it. The More Information column on the "
        L"far right is links, not the selector. The red cross beside WCID means "
        L"nothing here.";

    bcdgui::fillPreflightRows(&g_wiz, &run->state);

    g_wiz.progressCaption = kWorkTitle;
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

    g_wiz.cannotCancelNote = L"This cannot be stopped once it has started.";
    g_wiz.doneCaptionOk      = kDoneTitle;
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
    g_wiz.recheck = recheckMachine;
    g_wiz.user    = run;
    // What the re-check means for the screens that carry a row of their own. The
    // rows on page 2 come back through postReviewRow() by index, from the worker;
    // this is the same news for the rest of the table, delivered on the window
    // thread once the worker has finished. See rebuildScreens().
    g_wiz.refreshScreens = rebuildScreens;

    // The contextual action used to be set here, as one label and one worker for the
    // whole flow. It belongs to the screen it is about, so buildScreens() writes it
    // onto the MIDI port's entry - from the same ladder, from the same state - and
    // this structure has nothing to say about it any more.

    if (blockedNote) {
        g_wiz.startBlockedNote = blockedNote;
        g_wiz.cancelExitCode   = blockedCode;
    } else if (run->opt.preview) {
        g_wiz.startBlockedNote = kNotePreview;
        g_wiz.cancelExitCode   = kExitAborted;
    } else {
        g_wiz.cancelExitCode = kExitAborted;
    }

    // The flow, as a table, built LAST so that it describes the Wizard that is about
    // to be shown rather than a half filled one. runWizard() opens on entry 0 and
    // navigates by this and nothing else, so a Wizard that reached it without this
    // call would open on a screen that is not there.
    buildScreens(&g_wiz, &run->state);
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
