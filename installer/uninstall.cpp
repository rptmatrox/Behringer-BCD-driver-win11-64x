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
//   - the WinUSB binding on the device. We never applied it, and undoing a USB
//     binding is exactly the operation that can leave hardware unusable.
//   - the log files. They are the only record of what the driver did, and they
//     are what a bug report needs. The folder that holds them stays.
//
// STOPPING THE CONTROL SERVICE HAS A CONSEQUENCE, measured three times on the
// hardware: it destroys the virtual MIDI port, and a DJ application that is open
// at that moment does not go looking for the controller again until it is
// restarted. The uninstaller says so before it does it.
//
// AND THERE IS A /preview MODE, WHICH IS THE ONLY WAY TO READ ALL OF THE ABOVE
// AGAINST THIS PARTICULAR MACHINE WITHOUT RUNNING IT. This is the program in the
// project with the greatest power to destroy a working installation, and it had no
// dry run while BCD3000Setup.exe - the less destructive of the two - had one. That
// asymmetry was the wrong way round. /preview prints every path this program would
// touch and every action it would take, in the order the real run performs them,
// and writes nothing of its own: not a file, not a registry key, not a shortcut, not
// one of your processes stopped, and NOT THE LOG FILE. A mode that promises to write
// nothing and then writes something is worse than not having the mode.
//
// "OF ITS OWN" IS NOT A HEDGE, IT IS THE SEVENTH ENTRY ON THE LIST previewPlan() PRINTS.
// Every mode of this program calls gatherMachineState(), which calls detectWinget(),
// which starts winget.exe --version and terminates it if it misses its deadline. So the
// absolute "not a terminated process" was false on one branch, and the enumeration at
// the end of a preview names the child process rather than rounding the list down to
// six. The rest of that argument is beside the enumeration itself.

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
    bool preview;      // /preview: say what a real run would do, and write nothing
};

// *** THE WHOLE OF /preview's PROMISE, AS A PREDICATE WITH A NAME. ***
//
// There is exactly one call to logOpen() in this program and this is the first term
// of its condition, so "does /preview write" has ONE answer in ONE place instead of
// being a property of the order two statements happen to be in.
//
// IT IS A FUNCTION AND NOT AN INLINE !opt->preview, and the reason is a measurement
// problem rather than taste. installer/verify has to be able to assert both answers,
// and it cannot get them by running prepare(): prepare() on the machine the harness
// runs on would open the REAL log file, which is the exact write this predicate
// exists to prevent. So the decision is reachable on its own, and the harness checks
// the predicate for both options AND checks that a preview run of prepare() really
// left the log closed - the second one is the observation, the first one is what can
// be exercised in both directions without touching anything.
//
// THIS IS THE PRECEDENT BEING FOLLOWED, NOT AN INVENTION. When /preview was added to
// BCD3000Setup.exe the first implementation opened and wrote the log file, and it was
// caught before it shipped. setup.cpp's prepare() now gates the same call with
// "!checkOnly && !preview". The difference here is that the gate has a name.
static bool mayOpenLog(const Options* opt)
{
    return !opt->preview;
}

// *** THE OTHER GATE /preview DEPENDS ON, LIFTED FOR THE SAME REASON AND BY THE SAME
//     ARGUMENT. ***
//
// The last line of previewPlan()'s enumeration says "main() calls this function or it
// calls runRemoval(), never both" - and that sentence is what makes FIVE of the seven
// lines above it hold, because those five writes live inside runRemoval(). The other two
// are the log file, gated by mayOpenLog() above, and the child process, which has no
// gate; both are reached from prepare() and neither is covered by this predicate.
// Until this function existed, the decision behind that sentence was an inline boolean
// in main(): unreachable from installer/verify, so the harness could only assert that
// the SENTENCE WAS PRINTED. A check that a claim is printed is not a check that the
// claim is true, and this is the most load bearing claim the mode makes.
//
// So the decision has a name and lives here, exactly as mayOpenLog() does, and the
// harness asks it for both answers. It takes argsOk as a parameter rather than reading
// a global because it is the whole condition or it is not the gate: a bad command line
// is one of the FIVE ways this program ends up in the console, and leaving that term
// behind in main() would have left the check measuring four fifths of a decision.
//
// Reading it as English: a window is the default, and each of the FIVE terms takes it
// away for a reason of its own. Bad arguments and /help print text and exit. /console
// asks for the console by name and /yes answers the console's questions, so both mean
// one. /preview is text by definition - see the block over previewPlan().
static bool wantsWindow(const Options* opt, bool argsOk)
{
    return argsOk && !opt->help && !opt->console && !opt->assumeYes && !opt->preview;
}

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
    say(L"  /preview      say what a real run WOULD do, in the order it would do it,");
    say(L"                and write NOTHING OF ITS OWN - no file, no registry key, no");
    say(L"                shortcut, none of your processes stopped, and not even the");
    say(L"                log file. It ends by listing every way this program is able");
    say(L"                to write, including the one it did use: reading the machine");
    say(L"                state starts winget.exe --version, in every mode. Always in");
    say(L"                this console, never in a window: this mode exists to be read");
    say(L"                and pasted. Run it before you run the real thing.");
    say(L"  /yes          do not ask for confirmation. Ignored by /preview, which asks");
    say(L"                nothing because it does nothing.");
    say(L"  /help         this text.");
    sayBlank();
    say(L"Exit codes: 0 done, 1 a step failed, 2 bad arguments, 4 nothing was changed -");
    say(L"either stopped at your request or /preview.");
    sayBlank();
    say(L"Removes the ASIO driver, its registration, the control service and its");
    say(L"startup shortcut. Leaves the WinUSB binding on the device and the log");
    say(L"files alone - this program did not create any of them.");
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
        // *** /preview AND /console DO NOT CONTRADICT EACH OTHER HERE, AND IN
        //     BCD3000Setup.exe THEY DO. *** That is a real difference and not an
        // inconsistency: setup's /preview opens the WINDOW with its button disabled, so
        // asking for no window at the same time is a contradiction and setup refuses
        // it. This program's /preview is text by definition (see previewPlan), so
        // /console asks for what it is already doing. Accepting the pair costs nothing
        // and refusing it would be refusing a command line that says exactly what it
        // wants.
        else if (_wcsicmp(a, L"/preview") == 0)
            opt->preview = true;
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
// EVERY PATH THIS PROGRAM CAN REMOVE, WORKED OUT IN ONE PLACE.
//
// *** WHY THIS STRUCT EXISTS AT ALL, WHICH IS THE WHOLE ARGUMENT FOR IT. *** Adding
// /preview means a second reader of the same list of paths. A preview that builds its
// own list, beside a real run that builds another, is a pair of lists a human has to
// keep in sync - and this project has paid for that exact shape of defect more than
// once. So the derivation happens here, once.
//
// *** WHAT THAT BUYS IS NOT THE SAME FOR ALL EIGHT PATHS, AND SAYING OTHERWISE WOULD
//     BE CLAIMING A STRUCTURAL PROPERTY THAT ONLY FOUR OF THEM HAVE. *** This comment
// used to end "the preview cannot print a path the real run would not touch, and the
// real run cannot touch a path the preview did not print, because neither of them knows
// how to make one". That is an impossibility claim, and it is not true of the whole
// struct. Here is the split, which is what is actually true:
//
//   FOUR ARE SINGLE SOURCED BY CONSTRUCTION. dllBackup, dllStaging, manifest and
//   installedSelf are DERIVED HERE AND NOWHERE ELSE in this file, and both readers -
//   previewPlan() and step 4 of runRemoval() - read them from this struct. Those four
//   are the ones this file used to build inside runRemoval() with a _snwprintf and two
//   calls, and they are what this struct is really for. For them the sentence above
//   does hold: there is one derivation, so there is nothing to disagree with.
//
//   FOUR ARE COPIES, AND A COPY IS A SECOND VALUE. installDir, dllTarget, bridgeTarget
//   and shortcutFile were already single sourced before this struct existed - they come
//   from gatherMachineState() in common.cpp, the same function the setup uses - so they
//   are copied in here for the preview to read while runRemoval() goes on reading
//   state.* directly. Two names for one value. It is safe today because the copy is
//   verbatim, but nothing STRUCTURAL stops the two from drifting, so what backs it is a
//   measurement and not an argument: installer/verify runs computeRemovalPaths() on a
//   known MachineState and asserts wcscmp equality on all four. That check is the whole
//   of the guarantee for this half, and it is why it exists.
//
// AND THE .bak / .new PAIR HAS A SECOND DERIVATION THAT IS NOT IN THIS FILE AT ALL:
// setup.cpp's stagingAndBackupPaths() builds the same two names, because the setup is
// the program that WRITES those files. Single sourcing inside this file cannot see
// that one. If the setup wrote ".bak" and this file looked for ".bkp", every check
// below would still agree with itself and the install folder would survive every
// uninstall in silence. installer/verify calls BOTH functions and compares their
// answers - two real derivations, no literal in the middle - which is the only check
// here that can catch a wrong derivation rather than a missing one.
//
// *** IT CARRIES PATHS AND NOT EXISTENCE, AND THAT IS DELIBERATE. *** Nothing here
// records whether a file is on disk. The preview reads existence at the moment it
// prints, and says "right now"; the real run reads it again at the moment it deletes,
// which is after the control service has been stopped and can therefore be a
// different answer. A cached "exists" shared between the two would be a fact measured
// at one time and acted on at another - which is how a preview turns into a promise.
//
// WHAT THIS DOES *NOT* DO, said plainly rather than implied by silence. It does not
// table-drive the deletions. Step 4 of runRemoval() still has one hand written branch
// per path, because they are genuinely not the same operation: a file, a file that
// may be locked, a file this process is executing, and a directory get DeleteFileW,
// MoveFileExW with MOVEFILE_DELAY_UNTIL_REBOOT and RemoveDirectoryW respectively,
// with different messages and a LOAD BEARING ORDER between two of the pending
// operations. Collapsing four different operations into a loop over a table would be
// a rewrite of the one part of this program that has been validated on hardware, in a
// round whose job is to add a mode that changes nothing. THE COST OF NOT DOING IT:
// somebody adding a fifth path has to add it here and add its branch below, and
// nothing fails if they add only the branch. What is bought instead is that the
// PREVIEW and the RUN cannot disagree about the paths, which is the failure this
// round is actually about.
// ---------------------------------------------------------------------------
struct RemovalPaths {
    // Copies of the four gatherMachineState() already worked out.
    wchar_t installDir[kPathMax];
    wchar_t dllTarget[kPathMax];
    wchar_t bridgeTarget[kPathMax];
    wchar_t shortcutFile[kPathMax];

    // The two names the setup leaves next to the driver: the copy it kept of a driver
    // it replaced, and the staging file it writes a payload to before checking it.
    wchar_t dllBackup[kPathMax];      // dllTarget + ".bak"
    wchar_t dllStaging[kPathMax];     // dllTarget + ".new"
    bool    haveSideCars;             // false when dllTarget is too long to append to

    wchar_t manifest[kPathMax];
    bool    haveManifest;

    wchar_t installedSelf[kPathMax];  // the uninstaller's own copy in the install folder
    bool    haveInstalledSelf;
};

static void computeRemovalPaths(const MachineState* state, RemovalPaths* out)
{
    ZeroMemory(out, sizeof(*out));

    wcsncpy(out->installDir,   state->installDir,   kPathMax - 1);
    wcsncpy(out->dllTarget,    state->dllTarget,    kPathMax - 1);
    wcsncpy(out->bridgeTarget, state->bridgeTarget, kPathMax - 1);
    wcsncpy(out->shortcutFile, state->shortcutFile, kPathMax - 1);
    out->installDir[kPathMax - 1]   = 0;
    out->dllTarget[kPathMax - 1]    = 0;
    out->bridgeTarget[kPathMax - 1] = 0;
    out->shortcutFile[kPathMax - 1] = 0;

    // The same length guard runRemoval() used to make inline. Both suffixes are four
    // characters; the 8 is the guard that was already here and is kept rather than
    // tightened, because changing a bound is not this round's business.
    if (wcslen(out->dllTarget) + 8 < kPathMax) {
        _snwprintf(out->dllBackup,  kPathMax - 1, L"%s.bak", out->dllTarget);
        _snwprintf(out->dllStaging, kPathMax - 1, L"%s.new", out->dllTarget);
        out->dllBackup[kPathMax - 1]  = 0;
        out->dllStaging[kPathMax - 1] = 0;
        out->haveSideCars             = true;
    }

    out->haveManifest      = manifestPath(out->manifest, kPathMax);
    out->haveInstalledSelf = uninstallExePath(out->installedSelf, kPathMax);
}

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
    RemovalPaths paths;
    wchar_t      bridgeDir[kPathMax];
    // Where the log WOULD go. Named in the Run rather than being a local of
    // prepare(), because /preview has to be able to say which file it did not open -
    // and saying it from the same variable the open would have used is the difference
    // between a statement about this program and a sentence somebody typed.
    wchar_t      logFile[kPathMax];
    bool         logFileResolved;
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

    // THE PATHS ARE WORKED OUT UNCONDITIONALLY AND THE OPEN IS GATED SEPARATELY, and
    // the order of those two things is the whole of requirement A.
    //
    // It used to be one condition: bridgeDirPath() && logFilePath() && dirExists() ->
    // logOpen(). Putting the /preview test inside that chain would have meant either
    // hanging it on the end, where the two reads still happen but the reader has to
    // check four terms to see which one decides, or hanging it on the front, where it
    // short circuits bridgeDirPath() as well - and run->bridgeDir is what reportPlan()
    // and the preview both print as "the log files we are keeping". A gate that
    // silently empties a path used three lines later is how a correct decision
    // produces a wrong sentence.
    //
    // So: the reads first, then the decision, with mayOpenLog() as the FIRST term of
    // the only logOpen() call in this program. There is nothing before it that can
    // write.
    bool haveBridgeDir = bridgeDirPath(run->bridgeDir, kPathMax);
    run->logFileResolved = logFilePath(run->logFile, kPathMax);

    if (mayOpenLog(&run->opt) && haveBridgeDir && run->logFileResolved &&
        dirExists(run->bridgeDir))
        logOpen(run->logFile);

    gatherMachineState(&run->state);
    reportMachineState(&run->state);

    // Worked out here, from the state that was just read, so that BOTH readers - the
    // preview and step 4 of runRemoval() - get the same strings. See RemovalPaths.
    // Before the two refusals below, because /preview reaches this function and then
    // has to print the paths whatever those refusals decide.
    computeRemovalPaths(&run->state, &run->paths);

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

// ---------------------------------------------------------------------------
// /preview - the dry run, and the four decisions inside it.
//
// *** 1. WHY IT IS CONSOLE ONLY, WHERE BCD3000Setup.exe's /preview OPENS THE WINDOW
//        WITH ITS BUTTON DISABLED. *** The two modes share a name and not a purpose.
// Setup's exists to let somebody LOOK AT THE WINDOW on a machine that must not be
// touched - its own help says so in those words - so a window is the thing it is for.
// This one exists so that the list of paths and actions can be READ, line by line,
// beside the person who owns the machine, before the most destructive program in this
// project is allowed to run once. That is text: it is redirectable, it is pasteable
// into a message, a screen reader reaches it, and a shell waits for it. A window would
// put the one thing this mode produces into pixels, which is the one form of output
// that none of the four are true of.
//   THE SECOND REASON, which is smaller and pointed the same way: this program's
// window has no opening page - its FIRST page is already the review, already lists
// what would be removed, and already carries the row about the previous ASIO driver.
// A windowed /preview would therefore be the default run minus the log file, which is
// a thin mode with a whole branch behind it. The text mode is a different report.
//   WHAT IT COSTS, said rather than left to be discovered: somebody who wants to see
// the window before pressing Remove cannot get it without the log file being written.
// The default run gives them the window; only the log file separates the two.
//
// *** 2. WHY IT EXITS 4, ARGUED RATHER THAN COPIED FROM SETUP. *** Setup's /preview
// exits 4 and symmetry is worth something, but symmetry is not the argument. The
// argument is what the other codes would say. 0 in this program means "the driver and
// the control service have been removed" - a caller that checked for 0 after a preview
// would be told the machine had been uninstalled, which is the worst answer available.
// 1 means "a step failed", and no step ran. 2 means the command line was wrong, and it
// was not. 4 is the only code in this program's table whose meaning is "nothing was
// changed", which is the single most important fact about a preview run. So 4, and the
// help text and the README widen 4's wording from "stopped at your request" to
// "nothing was changed", which is what it always actually meant.
//   AND IT IS 4 EVEN WHEN A REAL RUN WOULD BE REFUSED. This mode does not return the
// code it thinks the real run would use. Its exit code is a statement about what THIS
// run changed, which is nothing, in every case. Returning a predicted code would be
// precisely the "this will work" claim the block below refuses to make.
//
// *** 3. WHERE THE "I CANNOT KNOW THAT" SENTENCES SIT: BOTH PLACES, ON PURPOSE. ***
// Each one is printed beside the step it is about, because that is where somebody
// reading the plan meets the thing being qualified, and a caveat two screens away from
// its subject is a caveat that qualifies nothing. They are then collected again in a
// block of their own near the end, because the one reader this mode is for is somebody
// deciding whether to run the real thing, and that decision is made from the list of
// unknowns as a whole. Repeating them costs eight lines. Printing them once, in either
// place alone, costs either the context or the summary.
//
// *** 4. WHAT IT MAY NOT SAY. *** Not one line here is allowed to read as "this would
// work". Everything is conditional, and the word "will" appears in no line this
// function PRINTS - installer/verify asserts that absence on the captured text, not on
// this comment, because "will" is how a description of an intention turns into a
// promise about a machine nobody has measured. reportPlan() a few hundred lines above
// is full of "will", correctly: it is what a real run says on its way to doing it.
// ---------------------------------------------------------------------------
static int previewPlan(Run* run, bool canRun, int wouldStopCode)
{
    MachineState* state = &run->state;
    RemovalPaths* paths = &run->paths;

    sayBlank();
    say(L"=========== PREVIEW: THIS RUN REMOVES NOTHING AND UNREGISTERS NOTHING "
        L"===========");
    sayInfo(L"/preview was given. Every line below says what a REAL run WOULD do, in "
            L"the order it would do it. None of it has happened.");
    sayInfo(L"nothing was written by this run except through the one channel it does "
            L"use - see \"what this run wrote\" at the end, which lists ALL SEVEN ways "
            L"this program is able to write, six with the reason they were not reached "
            L"and one that was: reading the machine state starts winget.exe --version, "
            L"in this mode as in every other.");
    // A flag that is silently ignored on the most destructive program in this project
    // is how somebody comes to believe /preview /yes removed something, or that it
    // did not. One line, and it says which it is.
    if (run->opt.assumeYes)
        sayInfo(L"/yes was given as well and has NO EFFECT here: this mode asks nothing "
                L"because it does nothing. The questions a real run would ask are named "
                L"below, each with the answer /yes would give it.");
    sayBlank();

    if (!canRun) {
        sayWarn(L"A REAL RUN WOULD REFUSE TO START AND EXIT %d, WITHOUT REMOVING "
                L"ANYTHING. The reason is the [FAIL] line above this block.",
                wouldStopCode);
        sayWarn(L"the steps below are therefore what it would do if that refusal were "
                L"dealt with. One of them - step 5 - cannot be described at all, "
                L"because a real run refuses before it reads the file that decides it.");
        sayBlank();
    }

    // -------------------------------------------------------------------
    say(L"--- step 1 of 5: stop the control and LED service ---");
    sayInfo(L"the process a real run would look for: %s", paths->bridgeTarget);
    if (!state->bridge.running) {
        sayOk(L"it is NOT running at this moment, so a real run starting now would skip "
              L"this step and remove nothing to get past it.");
        sayInfo(L"NOT KNOWABLE FROM A PREVIEW: whether it is running when you start the "
                L"real run. It starts at every sign in, and a real run reads that at the "
                L"moment it starts, not now. If it is running by then, everything in the "
                L"other branch of this step applies.");
    } else {
        sayWarn(L"it IS running at this moment - %d process%s, first process id %lu - so "
                L"a real run WOULD terminate it.", state->bridge.instanceCount,
                state->bridge.instanceCount == 1 ? L"" : L"es",
                (unsigned long)state->bridge.firstPid);
        sayWarn(L"terminating it destroys the virtual MIDI port. Any DJ application open "
                L"at that moment stops seeing the controller, and reopening the "
                L"application is the only way back. Measured three times on the "
                L"hardware.");
        sayWarn(L"NOT KNOWABLE FROM A PREVIEW: whether it can be stopped. Stopping it IS "
                L"the doing, and this mode does not do anything, so there is no honest "
                L"way to answer it from here. It can be refused - a process this "
                L"installer cannot open, or one belonging to a different administrator "
                L"account from the one signed in - and a preview cannot tell you which "
                L"of those you have.");
        sayInfo(L"if it cannot be stopped, a real run STOPS AT THIS STEP and exits 1 "
                L"WITHOUT REMOVING ANYTHING: nothing in steps 2 to 5 is attempted, the "
                L"driver stays registered, and every file below stays where it is.");
    }
    sayBlank();

    // -------------------------------------------------------------------
    say(L"--- step 2 of 5: remove the ASIO registration ---");
    sayInfo(L"the two registry locations a real run can write to, and the only two:");
    say(L"    HKEY_CLASSES_ROOT\\CLSID\\%s\\InprocServer32", kAsioClsid);
    say(L"    HKLM\\SOFTWARE\\ASIO\\%s", kAsioRegName);
    sayInfo(L"HKLM\\SOFTWARE\\ASIO itself is never removed: other ASIO drivers live "
            L"there.");
    if (!state->asio.clsidKeyPresent && !state->asio.asioNameKeyPresent) {
        sayOk(L"nothing is registered at this moment, so a real run would have nothing "
              L"to remove here.");
    } else if (state->registeredElsewhere) {
        sayWarn(L"the registration points at %s, which is not the copy this uninstaller "
                L"installed (%s).", state->asio.inprocPath, paths->dllTarget);
        sayInfo(L"a real run would LEAVE IT ALONE: another installation of this driver "
                L"owns it, and removing it is not this uninstaller's business.");
    } else if (fileExists(paths->dllTarget)) {
        sayInfo(L"the driver file is on disk at this moment, so a real run would load it "
                L"and call its DllUnregisterServer - the same thing regsvr32 /u does - "
                L"and then read the registry back to check that both entries above are "
                L"gone.");
        sayInfo(L"if either entry survived that call, or the call itself was refused, a "
                L"real run would delete the two entries directly, and only after "
                L"checking that each one belongs to this product.");
        sayWarn(L"NOT KNOWABLE FROM A PREVIEW: whether that driver accepts being "
                L"unregistered. Calling it is a write, so this mode does not call it. "
                L"The driver also shows a message box of its own when it fails, which a "
                L"real run can block on until somebody clicks OK.");
    } else {
        sayInfo(L"the driver file is NOT on disk at this moment, so there is nothing to "
                L"call: a real run would delete the two entries above directly, each "
                L"one only after checking that it belongs to this product.");
    }
    sayBlank();

    // -------------------------------------------------------------------
    say(L"--- step 3 of 5: remove the startup shortcut ---");
    sayInfo(L"the one file this step can delete: %s", paths->shortcutFile);
    if (!state->shortcutPresent) {
        sayOk(L"there is no such file at this moment, so a real run would delete nothing "
              L"here.");
    } else if (state->shortcutPointsAt[0] &&
               _wcsicmp(state->shortcutPointsAt, paths->bridgeTarget) != 0) {
        sayWarn(L"it exists and points at %s, which is not our control service.",
                state->shortcutPointsAt);
        sayInfo(L"a real run would LEAVE IT ALONE: it is somebody else's shortcut with "
                L"our name on it.");
    } else if (!state->shortcutPointsAt[0]) {
        sayWarn(L"it exists, and its target could NOT be read - so \"it is ours\" is a "
                L"guess and not a check.");
        sayInfo(L"a real run would ASK before deleting it: \"Delete it anyway? It "
                L"carries the name this product uses.\" With /yes that question is "
                L"answered yes without being asked.");
    } else {
        sayInfo(L"it exists and points at our control service, which is the proof a real "
                L"run requires, so a real run WOULD delete it.");
    }
    sayBlank();

    // -------------------------------------------------------------------
    // *** THE PATHS BELOW ARE THE ONES runRemoval() ACTS ON, not a second list. ***
    // Both read run->paths, which computeRemovalPaths() filled once. See RemovalPaths.
    say(L"--- step 4 of 5: remove the files and the install folder ---");
    sayInfo(L"every path a real run can delete, in the order it would try them, with "
            L"what is on disk AT THIS MOMENT. A real run reads each one again when it "
            L"gets there, and the answer can differ - step 1 stops a process and step 2 "
            L"can release a loaded file.");
    struct PreviewFile { const wchar_t* what; const wchar_t* path; bool listed; };
    const PreviewFile files[6] = {
        { L"the control service", paths->bridgeTarget,   true              },
        { L"the ASIO driver",     paths->dllTarget,      true              },
        { L"the driver it replaced (.bak)", paths->dllBackup,  paths->haveSideCars },
        { L"the staging file (.new)",       paths->dllStaging, paths->haveSideCars },
        { L"the install manifest",          paths->manifest,   paths->haveManifest },
        { L"this uninstaller's installed copy", paths->installedSelf,
                                                           paths->haveInstalledSelf }
    };
    for (int i = 0; i < 6; i++) {
        if (!files[i].listed) {
            sayWarn(L"%s: its path could not be worked out on this machine, so a real "
                    L"run would not touch it either", files[i].what);
            continue;
        }
        sayInfo(L"%s %s: %s", fileExists(files[i].path) ? L"[on disk now ]"
                                                       : L"[absent now  ]",
                files[i].what, files[i].path);
    }
    sayInfo(L"[folder      ] the install folder: %s", paths->installDir);
    sayBlank();
    sayInfo(L"a real run cannot delete its own running file, so when it is being run "
            L"FROM the install folder it hands that one file and then the folder to "
            L"Windows to remove at the next restart, in that order - the folder has to "
            L"be empty when its turn comes. Run from anywhere else, the installed copy "
            L"is just a file and goes at once.");
    sayInfo(L"the log files in %s are NOT in the list above, on purpose: they are the "
            L"only record of what the driver did.", run->bridgeDir);
    sayWarn(L"NOT KNOWABLE FROM A PREVIEW: whether any of these can be deleted. A DJ "
            L"application with the driver still loaded holds the driver file, and "
            L"nothing short of trying finds that out. A real run reports each refusal "
            L"and forces none of them.");
    sayBlank();

    // -------------------------------------------------------------------
    // *** THE STEP THIS WHOLE MODE IS MOST FOR. *** It is the difference between an
    // uninstaller that leaves the machine as it found it and one that leaves it with
    // no ASIO driver at all, so the preview states three things about it and not one:
    // whether the offer would be made, which file it is about, and whether that file
    // is on disk right now.
    say(L"--- step 5 of 5: put back the ASIO driver that was registered before ---");
    sayWarn(L"THIS IS THE STEP THAT DECIDES WHETHER THIS MACHINE STILL HAS AN ASIO "
            L"DRIVER AFTERWARDS. The registration is machine wide and holds one driver "
            L"at a time, so steps 2 to 4 leave nothing pointing anywhere.");
    if (!canRun) {
        sayWarn(L"NOT KNOWN, AND NOT BECAUSE OF THIS MODE: a real run refuses before it "
                L"reads the install manifest, so this preview has not read it either. "
                L"Deal with the refusal above and run this preview again - that is the "
                L"one question in this report you should not proceed without.");
    } else if (!run->havePrevious) {
        if (paths->haveManifest)
            sayInfo(L"the install manifest %s records no earlier registration.",
                    paths->manifest);
        else
            sayInfo(L"the install manifest could not be located on this machine.");
        sayInfo(L"so NO offer would be made by a real run: there is no recorded file to "
                L"put back, and this program never invents one.");
        sayWarn(L"AFTER A REAL RUN, NO ASIO DRIVER WOULD BE REGISTERED ON THIS MACHINE, "
                L"unless you have another one that this product knows nothing about. "
                L"Your DJ software would list no ASIO device for this mixer.");
    } else {
        sayInfo(L"the install manifest records this as the registration that was in "
                L"place before this product:");
        say(L"    %s", run->previous);
        if (fileExists(run->previous)) {
            sayOk(L"THAT FILE IS ON DISK AT THIS MOMENT - read from disk while this "
                  L"preview was printing.");
            sayInfo(L"so a real run WOULD MAKE THE OFFER, as the last thing it does: "
                    L"\"Register that driver again now?\". Answer yes unless you know "
                    L"you do not want it. With /yes it is answered yes without being "
                    L"asked, and in the window it is a question of its own.");
            sayInfo(L"a real run then proves it by reading the registry back, the same "
                    L"way the setup proves its own registration - and if the offer is "
                    L"declined, the regsvr32 line for that file is the FIRST thing in "
                    L"its summary.");
            sayWarn(L"NOT KNOWABLE FROM A PREVIEW: whether that driver accepts being "
                    L"registered again. Doing it means loading it and calling its "
                    L"DllRegisterServer, which is a write.");
        } else {
            sayWarn(L"THAT FILE IS NOT ON DISK AT THIS MOMENT, so it could not be put "
                    L"back and a real run would not offer to.");
            sayWarn(L"AFTER A REAL RUN, NO ASIO DRIVER WOULD BE REGISTERED ON THIS "
                    L"MACHINE. Find that file, or another ASIO driver, BEFORE you run "
                    L"the real thing - this is the reason this mode exists.");
        }
        sayInfo(L"the offer is only made when nothing is registered at that moment. If a "
                L"real run finds a registration still standing that was not ours to "
                L"remove, it leaves that alone and makes no offer.");
    }
    sayBlank();

    // -------------------------------------------------------------------
    say(L"--- what a real run would KEEP ---");
    sayInfo(L"the WinUSB binding on the device: we never applied it, and undoing a USB "
            L"binding is the operation that leaves hardware unusable.");
    sayInfo(L"the log files in %s: they are what a bug report needs.", run->bridgeDir);
    sayBlank();

    // -------------------------------------------------------------------
    // The unknowns, collected. See decision 3 in the block above this function for
    // why they are here AS WELL AS beside the steps they belong to.
    say(L"--- what this preview CANNOT know, collected in one place ---");
    sayInfo(L"1. whether the control service can be stopped (step 1). Stopping it is "
            L"the doing. If it cannot be stopped, a real run exits 1 having removed "
            L"NOTHING - which is a safe outcome, not a broken one.");
    sayInfo(L"2. whether it is even running when you start the real run (step 1). It "
            L"starts at every sign in.");
    sayInfo(L"3. whether the driver accepts being unregistered (step 2), or the "
            L"previous one accepts being registered again (step 5). Both mean loading a "
            L"DLL and calling into it, and both are writes.");
    sayInfo(L"4. whether each file can be deleted (step 4). A DJ application holding "
            L"the driver is invisible from here.");
    sayInfo(L"5. what a real run finds on disk when it gets there. Every \"at this "
            L"moment\" above was read now; a real run reads it again.");
    sayInfo(L"nothing above is a prediction that a real run would succeed. This mode "
            L"cannot make that statement and does not try to.");
    sayBlank();

    // -------------------------------------------------------------------
    // *** REQUIREMENT A, ANSWERED BY ENUMERATION AND NOT BY A CLAIM. *** Every way
    // this program is capable of writing, named, with the reason it was not reached.
    // The closing line is the one that makes FIVE of them hold: the registry, the
    // files, the shortcut, your processes and the restart queue all live in
    // runRemoval(), and main() calls either this function or that one and never both.
    //
    // *** IT IS FIVE AND NOT SIX, AND THE COUNT WAS WRONG HERE FOR THREE ROUNDS. ***
    // The log file is the seventh channel's quiet twin: logOpen() is called from
    // prepare(), NOT from runRemoval(), so "main() calls one or the other" says nothing
    // about it and never did. What holds it is mayOpenLog(), a gate of its own, which is
    // why that line names mayOpenLog() and not runRemoval(). Two entries on this list
    // are therefore outside the closing sentence, not one, and both are now said out
    // loud - because an enumeration whose closing line over-claims by one entry is the
    // same defect as an enumeration that omits one.
    //
    // *** THE SEVENTH CHANNEL IS HERE BECAUSE THE LIST WAS WRONG WITHOUT IT, and it is
    // the one channel the closing line does NOT cover. *** This function is reached
    // through prepare() -> gatherMachineState() -> detectWinget(), which runs
    // CreateProcessW on "winget.exe --version" and, if that child misses its deadline,
    // TerminateProcess on it. So "processes NOT TERMINATED" was false as worded on that
    // one branch, and an enumeration that promised "every way this program is able to
    // write" while omitting process creation was promising more than it listed - and
    // process creation is the only item on the list whose effects land outside this
    // program's own files, because winget's first run on an account can leave state of
    // its own behind. A mode whose whole value is that its enumeration is exhaustive
    // cannot round that down to six.
    //
    // FOR WHOEVER TAKES THE NEXT ROUND: this program has no use for winget at all - it
    // installs nothing and offers nothing, so detectWinget() could be skipped under
    // /preview outright. It is not skipped here because the skip belongs in
    // gatherMachineState(), which BCD3000Setup.exe shares, and changing a function two
    // programs read is not a change to make inside a round about a dry run.
    say(L"--- what this run wrote: nothing of its own. Here is every way this program "
        L"can ---");
    if (run->logFileResolved)
        sayInfo(L"the log file      %s", run->logFile);
    else
        sayInfo(L"the log file      (its path did not resolve on this machine)");
    sayInfo(L"                  NOT OPENED. mayOpenLog() is the first term of the "
            L"condition on the only logOpen() call in this program, and /preview makes "
            L"it false. The path above was worked out and not used.");
    sayInfo(L"the registry      NOT TOUCHED. Steps 2 and 5 are the only ones that write "
            L"to it - step 2 through DllUnregisterServer or deleteAsioRegistryKeys, "
            L"step 5 through the DllRegisterServer of the driver it puts back - and all "
            L"three live in runRemoval().");
    sayInfo(L"files             NOT TOUCHED. Only step 4 deletes the files listed above, "
            L"through DeleteFileW and RemoveDirectoryW, and both live in runRemoval().");
    sayInfo(L"the shortcut      NOT TOUCHED. It is a file too, and it is on its own line "
            L"because a different step deletes it: only step 3, through DeleteFileW, in "
            L"runRemoval().");
    sayInfo(L"your processes    NOT TERMINATED, and the control service in particular "
            L"was not stopped. stopBridge() is called from exactly one place in this "
            L"program, which is step 1 of runRemoval(). The one process this run did "
            L"create is the line below, and it is not yours.");
    sayInfo(L"a child process   ONE WAS STARTED: winget.exe --version, by detectWinget() "
            L"inside gatherMachineState(), which is the machine state read that EVERY "
            L"mode of this program performs - this one included, because a preview that "
            L"read a different machine from a real run would be a preview of something "
            L"else. It is a version probe: it is given no package and its one line of "
            L"output is only read. If it had missed its deadline this run would have "
            L"called TerminateProcess on it, so \"no process was terminated\" is not a "
            L"sentence this mode can honestly print; \"none of yours\" is. Running "
            L"winget for the first time on an account can leave state of its own under "
            L"AppData\\Local, and that is the only effect of this run that is outside "
            L"this program's own files. This uninstaller installs nothing and needs "
            L"nothing from winget - see the note in the source above this list.");
    sayInfo(L"restart queue     NOTHING QUEUED. MoveFileExW with "
            L"MOVEFILE_DELAY_UNTIL_REBOOT is called from step 4 only, in runRemoval().");
    sayInfo(L"AND WHAT MAKES FIVE OF THOSE SEVEN HOLD TOGETHER: main() calls this "
            L"function or it calls runRemoval(), never both - one decision, in "
            L"wantsWindow() and in the branch below it. The registry, the files, the "
            L"shortcut, your processes and the restart queue are all inside "
            L"runRemoval(), and this run did not call it.");
    sayInfo(L"AND THE OTHER TWO, WHICH THAT SENTENCE DOES NOT COVER: the log file is "
            L"reached from prepare() and not from runRemoval(), so it has a gate of its "
            L"own - mayOpenLog(), named on its line above. The child process is reached "
            L"from prepare() too and has no gate at all, which is why it is the one "
            L"entry on this list that happened.");
    sayBlank();
    say(L"=========== END OF PREVIEW. NOTHING WAS REMOVED OR UNREGISTERED. "
        L"===========");
    sayInfo(L"exit code 4: nothing was changed. The same code this program uses when "
            L"you decline at its prompt, and the same one BCD3000Setup.exe /preview "
            L"uses - the three runs have the same consequence for this machine, which "
            L"is none.");
    sayInfo(L"4 is NOT a prediction of what a real run would exit with. This mode does "
            L"not make predictions about a run it did not perform.");
    return kExitAborted;
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
    RemovalPaths& paths        = run->paths;
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
    //
    // THE TWO NAMES ARE NOT BUILT HERE ANY MORE. They come from RemovalPaths, which
    // is the one place that derives them, so that /preview names the same two files
    // this loop deletes instead of a second _snwprintf that happens to agree today.
    // Existence is still read HERE and not cached, because it is read after the
    // control service was stopped and the driver was deleted, and both of those can
    // change the answer.
    if (paths.haveSideCars) {
        const wchar_t* const sideCars[2] = { paths.dllBackup, paths.dllStaging };
        for (int i = 0; i < 2; i++) {
            if (!fileExists(sideCars[i]))
                continue;
            if (DeleteFileW(sideCars[i]))
                sayOk(L"removed %s", sideCars[i]);
            else
                sayWarn(L"could not remove %s (%s)", sideCars[i],
                        winErrText(GetLastError()));
        }
    }

    if (paths.haveManifest && fileExists(paths.manifest)) {
        if (DeleteFileW(paths.manifest))
            sayOk(L"removed %s", paths.manifest);
        else
            sayWarn(L"could not remove %s (%s)", paths.manifest,
                    winErrText(GetLastError()));
    }

    // 5. This program itself. A running executable cannot delete its own file, so
    //    when we are running from the install folder it is scheduled for the next
    //    restart. When we are running from somewhere else, the installed copy is
    //    just a file and goes now.
    wchar_t self[kPathMax];
    self[0] = 0;
    GetModuleFileNameW(0, self, kPathMax);

    if (paths.haveInstalledSelf && fileExists(paths.installedSelf)) {
        if (_wcsicmp(self, paths.installedSelf) != 0) {
            if (DeleteFileW(paths.installedSelf))
                sayOk(L"removed %s", paths.installedSelf);
            else
                sayWarn(L"could not remove %s (%s)", paths.installedSelf,
                        winErrText(GetLastError()));
        } else if (MoveFileExW(paths.installedSelf, 0, MOVEFILE_DELAY_UNTIL_REBOOT)) {
            sayInfo(L"%s is in use because you are running it - Windows will delete it at "
                    L"the next restart", paths.installedSelf);
        } else {
            sayWarn(L"could not schedule %s for deletion (%s) - delete it by hand",
                    paths.installedSelf, winErrText(GetLastError()));
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
// The three words this flow's screens are named by. Named once and used twice, for
// the same reason as their opposite numbers in setup.cpp.
static const wchar_t* const kConfirmTitle = L"What this will do";
static const wchar_t* const kWorkTitle    = L"Removing";
static const wchar_t* const kDoneTitle    =
    L"The driver and the control service have been removed.";

// ---------------------------------------------------------------------------
// THREE SCREENS, AND IT DOES NOT INHERIT THE SETUP'S OPENING.
//
// The two flows share gui.cpp, so this one gets the new chrome and the new
// navigation for free - and that is exactly the thing to be careful about. It has
// never had a welcome page - it used to say so with a flag, and now it says so by
// not having an entry of that kind, which is one fewer thing to keep true - and it
// must still have none: its first screen is the confirmation, entry 0, so the foot
// band's second
// button reads Cancel there rather than Back, which is what it read before.
//
// It does NOT become a six step wizard. The design says so and gives the reason:
// confirm, work, summary already match what this program does, and inventing steps
// for it would be adding screens to fit a shape rather than to carry a subject.
// installer/verify asserts both halves - that there are three, and that the first
// one is the confirmation rather than an opening - because the count alone would be
// happy with an opening plus two.
// ---------------------------------------------------------------------------
static void buildScreens(bcdgui::Wizard* w)
{
    bcdgui::Screen* sc = w->screens;

    // "Remove" is on entry 0 because entry 0 is where the press that removes
    // happens - primaryActionFor() calls this screen kPrimaryStart and IDC_PRIMARY
    // starts the worker from it. The word is here and in no second place: the
    // Wizard field that used to hold a copy of it is gone, for the same reason the
    // setup's copy of "Install" is.
    sc[0].kind         = bcdgui::kScreenCheck;
    sc[0].title        = kConfirmTitle;
    sc[0].primaryLabel = L"Remove";
    // The field that makes the press a removal, beside the word that names it. A
    // check screen no longer starts the work merely by being one.
    sc[0].startsTheWork = true;

    // *** THE LINE WHOSE ABSENCE SHIPPED A BLANK PAGE, AND IT WAS ABSENT FOR
    //     TWENTY-EIGHT COMMITS. ***
    //
    // The rows below - what will be removed, what will be kept and why, and the
    // warning that removing our registration can leave this machine with NO ASIO
    // driver at all - were never deleted. buildWizard() has filled Wizard::review
    // with them the whole time. What went missing is the sentence that says WHICH
    // ENTRY PAINTS THEM: renderCheckScreen() asks onMachineReview(), which reads
    // this field, and a check screen that does not set it is painted by
    // renderSubject() instead - which draws Screen::title, a rule, and then
    // Screen::row, which this entry has never had. Title, rule, nothing. The owner
    // ran the uninstaller at 02:00 and got exactly that: a heading reading "What
    // this will do" with nothing under it.
    //
    // WHAT IT COST, because it is not a cosmetic line. The third row is the offer
    // to put back the ASIO driver that was registered before this product, and it
    // says on the page - BEFORE the button - that removing ours leaves the machine
    // with no ASIO driver at all. He never saw it, answered "No" to the question at
    // the end, and his machine now has no ASIO driver registered.
    //
    // IT IS A FIELD AND NOT A KIND TEST for the reason gui.h gives above
    // Screen::paintsMachineReview, and this flow is the proof of the other half of
    // that reasoning: a flow whose ONLY check screen is the review still has to say
    // so, because "the only one of its kind" is not a thing the renderer can read.
    sc[0].paintsMachineReview = true;

    sc[1].kind         = bcdgui::kScreenWork;
    sc[1].title        = kWorkTitle;
    // "Close" for the same reason as the setup's work screen: it is the string this
    // window really puts in that control before hiding it.
    sc[1].primaryLabel = L"Close";

    sc[2].kind         = bcdgui::kScreenDone;
    sc[2].title        = kDoneTitle;
    sc[2].primaryLabel = L"Close";
}

// ---------------------------------------------------------------------------
// *** THE WORDS OF THE THREE SCREENS, SEPARATED FROM THE ACT OF SHOWING THEM. ***
//
// This was one function, and the last thing it did was enter the message loop. That
// made every word on these pages unreachable from installer/verify: the harness can
// call buildScreens() and read the TABLE, which is why the table's shape has been
// asserted since the 19th review, but the only way to reach Wizard::review - the
// rows the confirmation page is actually made of - was to open a window and start a
// removal. So the flow's structure was measured and its CONTENT was measured by
// nothing, and the page emptied itself for twenty-eight commits with every check
// green.
//
// The split is exactly the one that closes it: this function fills a Wizard the
// caller owns and shows nothing, and runWindowed() below is the two lines that show
// it. installer/verify calls this one with a Wizard of its own, on an invented
// machine, and reads the rows back.
//
// IT TAKES THE Wizard RATHER THAN WRITING g_wiz for the same reason buildScreens()
// does: a builder that can only ever write one global is a builder a second caller
// cannot use, and a second caller is the entire point.
// ---------------------------------------------------------------------------
static void buildWizard(Run* run, bcdgui::Wizard* w, const wchar_t* blockedNote,
                        int blockedCode)
{
    MachineState* s = &run->state;

    ZeroMemory(w, sizeof(*w));
    w->windowTitle = L"Behringer BCD3000 ASIO driver - Uninstall";
    w->headline    = L"Remove the Behringer BCD3000 ASIO driver";
    w->subhead     = L"Uninstaller " BCD_VERSION_WSTR
                        L"  -  it removes what the setup added, and nothing else";

    w->reviewCaption = kConfirmTitle;
    w->reviewFooter  = L"Nothing has been removed yet. Closing this window now leaves "
                          L"the machine exactly as it is.";

    int n = 0;
    bcdgui::setRow(&w->review[n++], bcdgui::kRowNeutral, L"Will be removed",
                   L"The ASIO registration for class id %s.\n%s\n%s\n%s",
                   kAsioClsid, s->installDir, s->bridgeTarget, s->shortcutFile);
    bcdgui::setRow(&w->review[n++], bcdgui::kRowNeutral, L"Will be kept",
                   L"The WinUSB binding on the device, because we never applied it "
                   L"and undoing a USB binding is what leaves hardware unusable. The log "
                   L"files in %s, because they are what a bug report needs.",
                   run->bridgeDir);

    // THE OFFER. On the page, before the button, in its own row.
    if (run->havePrevious && fileExists(run->previous))
        bcdgui::setRow(&w->review[n++], bcdgui::kRowWarn,
                       L"An ASIO driver was registered before this one",
                       L"%s\nThat file is still on disk. Removing our registration leaves "
                       L"this machine with NO ASIO driver at all, so this uninstaller will "
                       L"ask you at the end whether to register that one again. Say yes "
                       L"unless you know you do not want it.",
                       run->previous);
    else if (run->havePrevious)
        bcdgui::setRow(&w->review[n++], bcdgui::kRowFail,
                       L"An ASIO driver was registered before this one, and it is gone",
                       L"%s is recorded as the registration that was in place before this "
                       L"product, but that file is no longer on disk, so it cannot be put "
                       L"back. After this runs, no ASIO driver will be registered on this "
                       L"machine.",
                       run->previous);
    else
        bcdgui::setRow(&w->review[n++], bcdgui::kRowNeutral,
                       L"No earlier ASIO driver to put back",
                       L"The install manifest records no registration in place before this "
                       L"product, so there is nothing to restore. After this runs, no ASIO "
                       L"driver will be registered on this machine unless you have another "
                       L"one.");

    if (s->bridge.running)
        bcdgui::setRow(&w->review[n++], bcdgui::kRowWarn,
                       L"The control service is running and has to be stopped",
                       L"That destroys the virtual MIDI port. Any DJ application that is "
                       L"open right now will stop seeing the controller, and reopening the "
                       L"application is the only way back. Measured three times on the "
                       L"hardware.");
    w->reviewCount = n;

    w->progressCaption = kWorkTitle;
    w->stepCount       = kStepCount;
    bcdgui::setRow(&w->steps[kStepStopService], bcdgui::kRowWaiting,
                   L"Stop the control and LED service",
                   L"If this fails, nothing below is attempted at all.");
    bcdgui::setRow(&w->steps[kStepUnregister], bcdgui::kRowWaiting,
                   L"Remove the ASIO registration", 0);
    bcdgui::setRow(&w->steps[kStepShortcut], bcdgui::kRowWaiting,
                   L"Remove the startup shortcut",
                   L"Only when it can be proved to be ours.");
    bcdgui::setRow(&w->steps[kStepFiles], bcdgui::kRowWaiting,
                   L"Remove the driver, the service and the install folder", 0);
    bcdgui::setRow(&w->steps[kStepPutBack], bcdgui::kRowWaiting,
                   L"Offer to register the previous ASIO driver again",
                   L"You will be asked. This is the step that decides whether this "
                   L"machine still has an ASIO driver afterwards.");

    w->cannotCancelNote = L"This cannot be stopped once it has started.";
    w->doneCaptionOk      = kDoneTitle;
    w->doneCaptionStopped = L"Stopped. Nothing was removed.";
    w->doneCaptionFail    = L"Some steps failed. Nothing was forced.";

    w->work = runRemoval;
    w->user = run;

    if (blockedNote) {
        w->startBlockedNote = blockedNote;
        w->cancelExitCode   = blockedCode;
    } else {
        w->cancelExitCode = kExitAborted;
    }
    run->consentFromWindow = true;
    // Built last, for the same reason as the setup's: runWizard() opens on entry 0
    // and navigates by this table and nothing else.
    buildScreens(w);
}

// The two lines that were the tail of the function above. Kept as a function of its
// own so that main() reads the same as it always did and so that the one global this
// flow's window uses is written in one place.
static int runWindowed(Run* run, const wchar_t* blockedNote, int blockedCode)
{
    buildWizard(run, &g_wiz, blockedNote, blockedCode);
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

    // A window by default, the console whenever the answer is text. The four terms and
    // the reason for each are in wantsWindow(), which is where the decision now lives:
    // /preview is in that list rather than being handled further down, so that
    // bcdgui::init() and detachConsole() are never called at all in that mode. A window
    // that is created and then not shown is still a window, and this is the mode whose
    // whole claim is that it does the smallest possible amount.
    //
    // THE VARIABLE IS STILL HERE because it is not the same thing as the predicate: the
    // predicate answers "was a window asked for", and this answers "is there one", which
    // bcdgui::init() failing turns from yes into no three lines below.
    bool windowed = wantsWindow(&opt, argsOk);
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

    // reportPlan() is the REAL run's plan and it says "what will be removed", in the
    // present tense, because that is what it is about to do. /preview must not print
    // it: two plans in one report, one of them phrased as an intention, is exactly the
    // confusion this mode exists to remove. previewPlan() prints its own, in the order
    // the steps happen, which reportPlan() does not do.
    if (canRun && !opt.preview)
        reportPlan(&g_run);

    // *** /preview IS TESTED BEFORE EVERY OTHER BRANCH, INCLUDING THE BLOCKED ONE. ***
    // A run that cannot proceed still has a preview worth printing - it is the report
    // that says which refusal is in the way and what is still unknown behind it - and
    // putting this test after the !canRun branch would have turned the most useful
    // preview into a bare exit code.
    int code;
    if (opt.preview) {
        code = previewPlan(&g_run, canRun, stopCode);
    } else if (windowed) {
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
