// The window. Presentation only: nothing in gui.cpp decides anything about the
// machine.
//
// WHY THERE IS A WINDOW AT ALL. The installer used to run in a console. A black
// console window that appears by itself and starts writing to the registry is
// indistinguishable, to somebody who did not write it, from something malicious.
// The console mode is still there and still the authority - see /console in
// setup.cpp - but it is no longer what a person sees.
//
// WHY NO PACKAGING TOOL. Same reason there is no packaging tool for the install
// logic: this project is not trading one third party dependency for another. The
// whole window is USER32, GDI, COMCTL32 and WIC - Windows components, present on
// every machine that can run the driver.
//
// THE ONE RULE OF THIS FILE. The steps run on a worker thread and the window
// lives on the main thread. The worker NEVER touches a window, a control or a
// device context; it calls say*() and the three post*() functions below, and each
// of those either posts a message or sends one. Everything that reads or writes a
// control runs inside wizardProc() on the main thread. The reason is not
// tidiness: an install writes files and registry keys, and if that ran on the
// window's thread Windows would stop repainting, grey the window out and add
// "(not responding)" to the title - which looks exactly like the thing we are
// trying to stop looking like.

#pragma once

#include "common.h"

namespace bcdgui {

// The non affiliation notice. ONE definition, used by the window's first page,
// by the console banner and quoted in installer\README.md, so that the three can
// not drift apart.
extern const wchar_t* const kNotAffiliatedLine1;
extern const wchar_t* const kNotAffiliatedLine2;

// Who wrote this and where it lives. One definition each, for the same reason as
// the two lines above: the window's first page and the console banner both print
// them, and two copies of a sentence drift.
//
// The dash is a plain ASCII hyphen, like every other dash in this folder. An em
// dash would be correct typography and would be the only non ASCII character in
// the installer's own words, which is a trade with nothing on its side.
extern const wchar_t* const kCreditsLine;
extern const wchar_t* const kRepositoryUrl;

// ---------------------------------------------------------------------------
// Rows. The pre-flight checks and the installation steps are the same shape: a
// mark, a title, and a line of detail.
// ---------------------------------------------------------------------------
enum RowState {
    kRowNeutral = 0,   // stated, not judged (a plain fact about the machine)
    kRowWaiting = 1,   // a step that has not started
    kRowBusy    = 2,   // a step that is running now
    kRowOk      = 3,
    kRowWarn    = 4,
    kRowFail    = 5,
    kRowSkipped = 6
};

const int kMaxRows  = 10;
const int kRowTitle = 160;
const int kRowText  = 512;

struct Row {
    RowState state;
    wchar_t  title[kRowTitle];
    wchar_t  detail[kRowText];
};

// Fills one row. Truncates instead of overflowing, like every other string in
// this project.
void setRow(Row* row, RowState state, const wchar_t* title, const wchar_t* detailFmt, ...);

// ---------------------------------------------------------------------------
// A flow. Plain data, filled in by setup.cpp or uninstall.cpp before the window
// opens. No constructors and no destructors: this project has none of those at
// file scope, and a wizard that ran code at unload would be the first.
// ---------------------------------------------------------------------------
struct Wizard {
    // --- words. All owned by the caller and all in English. ---
    const wchar_t* windowTitle;
    const wchar_t* headline;        // header band, every page
    const wchar_t* subhead;         // header band, second line

    // --- page 1, the opening. Skipped when hasWelcome is false, which is what
    //     the uninstaller does: it opens straight on its confirmation. ---
    bool           hasWelcome;
    const wchar_t* welcomeLine1;
    const wchar_t* welcomeLine2;
    const wchar_t* welcomeBullets[4];   // short lines; a null ends the list
    bool           showDevicePhoto;
    bool           showNotAffiliated;

    // --- page 2, the review. The pre-flight checks for the setup, the removal
    //     plan for the uninstaller. Scrolls when it does not fit. ---
    const wchar_t* reviewCaption;
    const wchar_t* reviewFooter;    // e.g. "Nothing has been changed yet."
    int            reviewCount;
    Row            review[kMaxRows];

    // --- page 3, the work ---
    const wchar_t* progressCaption;
    int            stepCount;
    Row            steps[kMaxRows];

    // --- buttons ---
    const wchar_t* startVerb;       // "Install" / "Remove"
    // Shown next to the buttons while the work runs. This is where the promise
    // about cancelling is kept or not made: see setup.cpp.
    const wchar_t* cannotCancelNote;

    // --- page 4. Which one is used is stated by work() through postOutcome(),
    //     not guessed from an exit code: "stopped before anything happened" and
    //     "a step failed" are different things to be told. ---
    const wchar_t* doneCaptionOk;
    const wchar_t* doneCaptionStopped;
    const wchar_t* doneCaptionFail;

    // A short block painted between that caption and the summary pane, in the
    // attention colour. It exists for the one thing on the last page that has to
    // be read without scrolling, and it is a POINTER at the summary rather than a
    // second copy of it: the sentences themselves are said with say*(), so they
    // reach the console, the log file, a screen reader and the clipboard, and a
    // painted block cannot become the only place a warning exists.
    //
    // Null means nothing is painted and the pane simply starts higher. Wrapped and
    // measured by the same renderer that draws it, so a longer sentence at a higher
    // DPI moves the pane down instead of being clipped.
    const wchar_t* doneNotice;

    // --- what to run ---
    // Called ONCE, on the worker thread, after the user has pressed startVerb.
    // Returns the process exit code. It may call say*(), askYesNo() and the
    // post*() functions below, and nothing else that concerns the window.
    int   (*work)(void* user);
    void*   user;

    // Returned by runWizard() when the user closes the window before the work
    // has started, which is the only point at which there is anything to cancel.
    int     cancelExitCode;

    // When set, the start button is disabled and this sentence is printed beside
    // it. Two things use it, and both are cases where showing the window and
    // refusing is better than exiting silently:
    //   /preview, which exists so that the window can be looked at on a machine
    //   that must not be touched;
    //   a machine the installer has already decided it cannot install on - no
    //   elevation, or the Windows folders did not resolve. The user gets to read
    //   the reason on the checks page instead of watching the window vanish.
    const wchar_t* startBlockedNote;
};

// ---------------------------------------------------------------------------
// Opening the window
// ---------------------------------------------------------------------------

// Registers the window classes, loads Common Controls v6 and builds the fonts.
// False when the window cannot be had at all, and then the caller falls back to
// the console rather than failing: an installer that cannot draw must still be
// able to install.
bool init();

// Hides and releases the console this process was given, so that window mode
// shows a window and nothing else, and silences the console side of say*().
// Called only after init() succeeded. The log file keeps every line.
void detachConsole();

// Starts collecting say*() lines and answering askYesNo() through the window.
//
// Called BEFORE the window exists, because the machine has to be read and
// reported before the checks page can show anything, and those lines belong in
// the log pane too. Until there is a window they are kept in memory and they are
// poured into the pane the moment it is created. The alternative was reading the
// machine twice - once for the page and once for the run - and then the page
// would be showing a different snapshot from the one that was acted on.
void beginCapture();

// Runs the window and its message loop until it closes. Returns the exit code:
// work()'s return value when the work ran, wiz->cancelExitCode when it did not.
int runWizard(Wizard* wiz);

// ---------------------------------------------------------------------------
// Worker thread -> window. The only three things the worker may say about the
// window, and none of them touches one.
// ---------------------------------------------------------------------------

// The state of step i, and optionally a new detail line for it.
void postStep(int index, RowState state, const wchar_t* detail);

// What the last page should say. Called by work() before it returns.
enum Outcome { kOutcomeOk = 0, kOutcomeStopped = 1, kOutcomeFailed = 2 };
void postOutcome(Outcome outcome);

// From here on, every line also goes to the last page's summary pane. The point
// is that the summary a user reads in the window is the same text the console
// prints, character for character, because it IS that text - not a second copy
// of it that can drift.
void beginSummaryCapture();

// ---------------------------------------------------------------------------
// Turning what was measured into rows. Presentation of a MachineState that has
// already been gathered; it reads nothing and decides nothing.
// ---------------------------------------------------------------------------
void fillPreflightRows(Wizard* wiz, const bcdsetup::MachineState* s);

}
