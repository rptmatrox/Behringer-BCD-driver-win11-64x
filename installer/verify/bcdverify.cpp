// Verification harness for rounds 2 and 3 of the BCD3000 installer. NOT part of
// the product and never shipped: it lives outside the repository on purpose.
//
// WHY IT INCLUDES THE SOURCES INSTEAD OF LINKING THEM. Everything worth proving
// here lives in a `static` function of setup.cpp - printSummary() and the wizard
// that runWindowed() fills in - or in a `static` variable of gui.cpp, which is
// where the DPI and the page index live. A harness that linked the two objects
// could reach none of it and would have to test a COPY of the text, which is the
// one thing a text harness must not do. So both files are pulled into this
// translation unit and the real functions are called.
//
//   setup.cpp's main()      is renamed away, so this file keeps its own.
//   gui.cpp's runWizard()   is renamed away, and the name it left behind is
//                           defined here to render the four pages instead of
//                           entering a message loop.
//
// WHY NOTHING IT DOES CAN TOUCH THE MACHINE. Two independent reasons.
//   1. It never enters a step. Nothing calls runSteps(); installDriver(),
//      installControlService(), stopBridge() and callDllRegisterServer() are
//      never reached. The only product code with a side effect that runs at all
//      is prepare(), which reads.
//   2. It runs prepare() with opt.preview set, which is the mode whose whole
//      contract is that it writes nothing - not even the log file, not even the
//      folder the log would live in.
// It is also asInvoker, so it could not write to HKLM or Program Files if it
// tried.
//
// AND WHY NO PIXEL REACHES THE OWNER'S SCREEN: every window is created after the
// thread has been moved to a PRIVATE DESKTOP, which is not the input desktop.
// Nothing is shown, nothing takes focus.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <wchar.h>
#include <limits.h>

// gui.cpp's message loop is renamed away; the name is redefined below.
#define runWizard runWizard_realMessageLoop
#include "gui.cpp"
#undef runWizard

namespace bcdgui { int runWizard(Wizard* wiz); }

#define main setupRealMain
#include "setup.cpp"
#undef main

// uninstall.cpp, IN A NAMESPACE OF ITS OWN, and that is the whole trick.
//
// WHY IT WAS NOT HERE BEFORE. The 19th review declared this gap: the uninstaller was
// the one product source this harness never compiled, so nothing in it was measured.
// The obstacle looked large, because uninstall.cpp and setup.cpp are two files with no
// namespace between them that both define main(), Options, Run, ExitCode, StepIndex,
// parseArgs(), printHelp(), prepare(), markRemainingSkipped(), runWindowed(), g_run
// and g_wiz. Pulling both into one translation unit collides on twelve names, not one,
// so the two #defines that already tame setup.cpp were never going to be enough.
//
// WHAT IT ACTUALLY COST: these few lines. A namespace renames all twelve at once.
//
// *** AN #include INSIDE A namespace IS THE REAL HAZARD, AND THE SIX LINES BELOW ARE
//     WHAT REMOVES IT - BY CONSTRUCTION, NOT BY COINCIDENCE. ***
//
// This comment used to name <windows.h> as the thing that would have sunk the approach.
// That was the wrong hazard, and wrong in the safest possible direction: <windows.h> is
// precisely the header that CANNOT get inside this namespace, because common.h pulls it
// in at the top of this file and its include guard makes every later mention a no-op.
//
// The hazard that is real is a header uninstall.cpp ADDS LATER which setup.cpp and
// gui.cpp do not already include - <shlobj.h>, <psapi.h>, <winsvc.h>, any of them. That
// one would be seen for the first time from inside namespace bcduninstall, and it would
// either fail loudly with several hundred errors or, far worse, succeed and quietly put
// Win32 declarations in a namespace where a later ::-qualified use would not find them.
// Nothing mechanical stood between this file and that, and it depended on a property of
// a DIFFERENT file (what setup.cpp happens to include) that nobody editing uninstall.cpp
// has any reason to check.
//
// So uninstall.cpp's six includes are repeated HERE, above the namespace, in the order
// it lists them. They are the whole of its include block. Every one of them is now
// guaranteed already-included by the time the namespace opens, so the nested lines are
// no-ops BY CONSTRUCTION. When uninstall.cpp grows a seventh include, this list is the
// place that has to grow with it - and that is a much smaller thing to get wrong than
// what it replaces, because the failure is at compile time and in this file.
#include "common.h"
#include "gui.h"
#include "version.h"
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

// It compiles at /W4 with no warning; /wd4505, which this harness already needed for
// setup.cpp's unused statics, covers the uninstaller's too. Measured before the checks
// were written, precisely because the whole round depended on the answer.
//
// main() is still renamed away as well as being inside the namespace. It is legal in a
// namespace and MSVC accepts it, but a function called main that is not THE main is the
// kind of thing that is true today and surprising later.
namespace bcduninstall {
#define main uninstallRealMain
#include "uninstall.cpp"
#undef main
}

// ---------------------------------------------------------------------------
// Counting. Every number this harness prints comes with its denominator.
// ---------------------------------------------------------------------------
static int g_checks = 0;
static int g_fails  = 0;

// A NOTE ON THE MESSAGES HANDED TO THIS FUNCTION. Most are built with _snwprintf into
// a stack buffer, and MSVC's _snwprintf writes the terminator only when the output is
// SHORTER than the count it was given. Every site whose format string interpolates a
// string of unbounded length therefore writes the terminator itself; the sites that
// interpolate only integers and literals cannot reach the count and are left alone,
// which is stated here rather than left to be worked out one call at a time.
static void check(bool cond, const wchar_t* what)
{
    g_checks++;
    if (!cond) {
        g_fails++;
        wprintf(L"  [FAIL] %s\n", what);
    } else {
        wprintf(L"  [ ok ] %s\n", what);
    }
}

// ---------------------------------------------------------------------------
// A CHECK THAT COULD NOT BE MADE HERE, NAMED SO THAT IT IS NOT MISTAKEN FOR ONE
// THAT WAS.
//
// There is exactly one of these and it is the end to end launch: this harness runs
// unelevated, and CreateProcessWithTokenW needs SE_IMPERSONATE_NAME, which a
// filtered token does not carry. A guard that simply returned early would leave the
// suite reporting success for work it never did, which is the failure mode the
// whole project is built against - so a skip is COUNTED SEPARATELY, printed with
// its own mark, and listed again at the end where a reader cannot miss it.
//
// It is deliberately NOT counted as a check. The denominator has to keep meaning
// "things this run measured".
static const int kSkipMax = 8;
static const wchar_t* g_skipWhat[kSkipMax];
static int            g_skips = 0;

static void skipped(const wchar_t* what)
{
    if (g_skips < kSkipMax)
        g_skipWhat[g_skips] = what;
    g_skips++;
    wprintf(L"  [skip] %s\n", what);
}

// ---------------------------------------------------------------------------
// Capturing what the product says
// ---------------------------------------------------------------------------
static const int kCapMax  = 600;
static const int kCapWide = 1024;
static wchar_t   g_cap[kCapMax][kCapWide];
static int       g_capN = 0;

static void capSink(bcdsetup::LineKind, const wchar_t* prefix, const wchar_t* body)
{
    if (g_capN >= kCapMax)
        return;
    _snwprintf(g_cap[g_capN], kCapWide - 1, L"%s%s", prefix, body);
    g_cap[g_capN][kCapWide - 1] = 0;
    g_capN++;
}

static void capReset()
{
    g_capN = 0;
}

static bool capHas(const wchar_t* needle)
{
    for (int i = 0; i < g_capN; i++)
        if (wcsstr(g_cap[i], needle))
            return true;
    return false;
}

// The line a phrase first appears on, so that "above" and "below" can be measured
// instead of assumed.
static int capIndexOf(const wchar_t* needle)
{
    for (int i = 0; i < g_capN; i++)
        if (wcsstr(g_cap[i], needle))
            return i;
    return -1;
}

static int capCount(const wchar_t* needle)
{
    int n = 0;
    for (int i = 0; i < g_capN; i++)
        if (wcsstr(g_cap[i], needle))
            n++;
    return n;
}

// The numbers of the numbered items a block really has, in the order they are
// printed, read out of the text instead of taken on trust from the first line.
//
// IT TAKES A FIRST LINE, AND THAT IS NOT DECORATION. printSummary() numbers its own
// list under "Still on your side" in exactly the same shape - two spaces, a digit, a
// dot, a space - so a capture that holds both blocks would have this function
// counting nine or ten items and blaming the warnings block for them. It is safe in
// the one call there is today only because that capture holds nothing else. Given a
// starting line it is safe in the next call too, which is the one that would
// otherwise be the trap.
//
// MULTI DIGIT, because it used to read the number one character at a time: "10. " has
// a '0' where the dot was looked for, so a tenth item was skipped and four checks
// stayed green over nine items. The numbers go into an int array rather than a string
// of digits for the same reason.
//
// Item 1 lives inside the box of stars, so a leading run of stars and of spaces is
// stepped over before the digits are looked for.
//
// WHAT A CONTINUATION LINE DOES, corrected: this comment used to say one "cannot
// match", and that is false. A wrapped line that happens to begin "  9. " WOULD be
// counted - the test is on the shape of the line's start, not on where the line sits
// in an item. What saves it is the direction of the error: a spurious match makes the
// count too HIGH and the sequence wrong, so it fails loudly instead of passing
// quietly. The one line in the block that starts with a digit today, "     83 minute
// audio run", does not match because there is no dot after the digits.
static int capItemNumbers(int from, int* out, int cap)
{
    int n = 0;
    for (int i = from < 0 ? 0 : from; i < g_capN; i++) {
        const wchar_t* p = g_cap[i];
        while (*p == L' ')
            p++;
        if (*p == L'*') {
            while (*p == L'*')
                p++;
            while (*p == L' ')
                p++;
        }
        if (*p < L'1' || *p > L'9')
            continue;
        int value = 0;
        while (*p >= L'0' && *p <= L'9') {
            value = value * 10 + (int)(*p - L'0');
            p++;
        }
        if (p[0] != L'.' || p[1] != L' ')
            continue;
        if (n < cap)
            out[n] = value;
        n++;
    }
    return n;
}

static int capWidest()
{
    int w = 0;
    for (int i = 0; i < g_capN; i++) {
        int len = (int)wcslen(g_cap[i]);
        if (len > w)
            w = len;
    }
    return w;
}

static void capDump()
{
    for (int i = 0; i < g_capN; i++)
        wprintf(L"%s\n", g_cap[i]);
}

// ---------------------------------------------------------------------------
// A machine state that is invented rather than read, so that both branches of
// the conditional can be exercised on a machine where only one of them is true.
// ---------------------------------------------------------------------------
static void fakeState(bcdsetup::MachineState* s)
{
    ZeroMemory(s, sizeof(*s));
    s->pathsResolved = true;
    s->elevated      = true;
    wcscpy(s->installDir,   L"C:\\Program Files\\BCD3000 ASIO Driver");
    wcscpy(s->dllTarget,    L"C:\\Program Files\\BCD3000 ASIO Driver\\BcdAsio.dll");
    wcscpy(s->bridgeTarget, L"C:\\Users\\Example\\AppData\\Local\\BCD3000Bridge\\BCD3000Bridge.exe");
    wcscpy(s->shortcutFile, L"C:\\Users\\Example\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\BCD3000 controls.lnk");
    wcscpy(s->account.tokenAccount, L"EXAMPLE\\owner");
    wcscpy(s->account.shellAccount, L"EXAMPLE\\owner");
    s->account.checked = true;
    s->account.matched = true;
    s->usb.enumKeyPresent     = true;
    s->usb.guidPresent        = true;
    s->usb.interfacePresentNow = true;
    wcscpy(s->usb.guid, L"{a5dcbf10-6530-11d2-901f-00c04fb951ed}");
}

// ---------------------------------------------------------------------------
// THE THREE MACHINES SCREEN 3'S THREE STATES ARE SEEDED FROM, AND THE ONLY PLACE
// IN THIS FILE THAT INVENTS A Windows MIDI Services READING.
//
// INVENTED, not read: this harness runs on ONE machine, on ONE build, and that
// build can only ever produce one of the three answers. A suite that read this
// machine would test whichever state it happened to be in and leave the other two
// completely unexercised - which is how a state nobody has ever seen ships.
//
// *** ONE AUTHOR FOR THE NUMBERS, INCLUDING THE PICTURE'S. *** shotState() calls
// this rather than assigning the same eight fields again, so the machine the
// captures are rendered from and the machine testMidiPortScreen() asserts on
// cannot drift apart. They drifting apart is how a suite ends up proving
// something about a state no picture shows.
//
// The numbers are true-shaped, in the discipline shotState() records: 1.0.15.0 is
// the Windows MIDI Services component version measured on the owner's machine on
// 2026-08-02, 26100.8875 is KB5101650 - the build microsoft/MIDI issue #1047
// names - and 26100.8000 is a June build before it, which the issue's reporter
// confirmed does not have the defect.
// ---------------------------------------------------------------------------
static void midiMachine(bcdsetup::MachineState* s, bcdsetup::WinMidiState want)
{
    fakeState(s);
    bcdsetup::WinMidiInfo* w = &s->winMidi;
    ZeroMemory(w, sizeof(*w));
    if (want == bcdsetup::kWinMidiUnread) {
        // A Windows with no MIDI Services at all: no service, no transport, and a
        // numeric reason. Everything else stays zero, which is the point - the row
        // has to print those zeros rather than invent a story about them.
        w->lastError = ERROR_FILE_NOT_FOUND;
        return;
    }
    w->serviceRegistered    = true;
    w->transportPresent     = true;
    w->transportVersionRead = true;
    w->transportVersion[0]  = 1;
    w->transportVersion[1]  = 0;
    w->transportVersion[2]  = 15;
    w->transportVersion[3]  = 0;
    w->serviceVersionRead   = true;
    w->serviceVersion[0]    = 10;
    w->serviceVersion[1]    = 0;
    w->serviceVersion[2]    = 26100;
    w->serviceVersion[3]    = (want == bcdsetup::kWinMidiKnownBad) ? 8875 : 8000;
}

// ===========================================================================
// *** WHAT A CAPTURE IS A PICTURE OF: AN INVENTED MACHINE, NOT THIS ONE. ***
//
// WHAT WENT WRONG. Everything under shots\ was a photograph of whichever machine
// the harness happened to run on, and those twelve files are TRACKED in a public
// repository. fillPreflightRows() prints the account that owns the desktop, so
// page-2-checks-96dpi.png and its 144 DPI twin carried the owner's real name; the
// log pane on page 3 carried the profile directory three times over.
//
// AND WHY THE PUBLICATION GATE DID NOT CATCH IT, WHICH IS THE PART THAT DECIDES
// THE FIX. That gate is a text search over the tree. The name was in PIXELS. No
// combination of flags on any search tool could have found it. The gate did not
// fail - it was incapable by construction, and a second gate of the same kind
// would be incapable in the same way.
//
// WHY A SYNTHETIC STATE RATHER THAN REDACTING THE TWO LINES THAT LEAK TODAY.
// Redaction closes two lines and leaves the CLASS open: the WinUSB row prints an
// interface guid, the ASIO row prints an install path, reportMachineState() prints
// three profile paths, and the next line somebody adds prints whatever it prints.
// Rendering from a state this file invents closes all of them at once - and closes
// two other things with them:
//
//   THE COUPLING TO THE BUILD. printPayloadReport() reads the resources
//   bcdverify.rc embeds, one of which is BCD3000Uninstall.exe, and it prints their
//   sizes into the log pane. So page-3-*.png changed every time the uninstaller
//   was recompiled, for a reason that has nothing to do with the page.
//
//   THE DRIFT. Page 2 went from amber to green between two runs with nobody
//   touching the code, because the machine under it had changed. A picture that
//   moves on its own is a picture a reviewer learns to ignore.
//
// WHAT IS LOST, AND IT IS A REAL LOSS, SAID HERE RATHER THAN LEFT TO BE NOTICED.
// A capture is no longer evidence of "how this page looks ON THIS MACHINE". Nobody
// can point at shots\ and say the owner's own run looked like that. That was never
// the question this harness answers - it measures layout, clipping and reach, and
// none of the three needs a real machine - but a reader who wanted the first thing
// will not find it here any more.
//
// THE VALUES ARE CHOSEN TO EXERCISE THE PAGE, NOT TO FLATTER IT. fillPreflightRows()
// has four kinds of mark and this state produces all four. That is measured by
// checkMarkKinds() rather than trusted to this comment, because a later edit to
// either file could quietly reduce the page to six green ticks.
//
// IT IS ALSO A MACHINE THAT COULD EXIST. reportMachineState() prints the same
// state into the log pane on page 3, so a state assembled purely to collect marks
// produces a page that contradicts itself - "not installed" over "running (2
// processes)" - and a reviewer who reads one page as nonsense stops reading the
// others. What is described below is a plausible machine on the eve of a first
// install: nothing of ours in place, an older copy registered from somewhere else,
// the mixer plugged in, and the installer started from the wrong account.
//
// ONE ROW IS DECIDED BY THE FILESYSTEM AND IT CANNOT BE HELPED: fillPreflightRows()
// asks fileExists(bridgeTarget) before it calls the service "not installed". The
// answer is no on any machine without a user called Example - and if it ever were
// yes, that row would turn amber, the grey mark would disappear from the page and
// checkMarkKinds() below would FAIL. Loud, not silent, which is the only property
// worth insisting on here.
// ===========================================================================
static void shotState(bcdsetup::MachineState* s)
{
    // ONE invented machine. The names and paths are fakeState()'s, so there is a
    // single place to read what the fiction is - and midiMachine() supplies screen
    // 3's readings for the same reason, in the state explained at the foot of this
    // function.
    midiMachine(s, bcdsetup::kWinMidiKnownBad);

    // Row 1, ASIO registration -> AMBER, and it prints an install path, which is
    // the second of the two kinds of thing this page used to leak.
    s->asio.clsidKeyPresent    = true;
    s->asio.inprocPresent      = true;
    wcscpy(s->asio.inprocPath, L"C:\\Program Files\\Another Vendor\\BcdAsio.dll");
    s->asio.asioNameKeyPresent = true;
    s->asio.clsidMatches       = true;
    s->registeredElsewhere     = true;

    // Row 2, control and LED service -> GREY. fakeState() zeroes bridge, so it is
    // not running; see the note above about the one fileExists() behind this.
    s->shortcutPresent = false;

    // The WinUSB binding not applied. NOT A ROW ON THE INSTALL SCREEN ANY MORE - it
    // is stated on a screen of its own, by describeBinding(), and that screen is
    // photographed too. It is still set here because it is what that screen
    // renders, and because this is still a plausible machine on the eve of a first
    // install: the mixer has been plugged in once, and Zadig has not been run.
    //
    // *** AND THE INSTALL SCREEN LOST ITS RED WITH THEM, WHICH IS A PROPERTY OF THE
    //     PAGE AND NOT OF THIS STATE. *** The red used to come from the WinUSB row.
    // Of the rows that remain, exactly one has a red branch at all - "Administrator
    // rights", when the process is not elevated - so no elevated machine can produce
    // a red on that page, whatever else is invented here. checkMarkKinds() says so in
    // those terms rather than demanding a mark this page can only paint by making the
    // pictured machine one that cannot install.
    //
    // THE COST, said rather than left to be noticed: the binding row's GREEN branch
    // was the only thing on this page that printed the interface guid, so no capture
    // of it shows a guid. Nothing about the GATE changes - checkNoLiveIdentity() reads
    // whatever strings the rows really carry - but the pictures of THIS page no longer
    // demonstrate that value being an invented one. The binding screen's own row still
    // interpolates it in two of its five branches.
    s->usb.enumKeyPresent      = true;
    s->usb.guidPresent         = false;
    s->usb.interfacePresentNow = false;
    wcscpy(s->usb.guid, L"{eeeeeeee-1111-2222-3333-444444444444}");

    // Row 7, Windows 10 -> GREY, and it exists at all only on Windows 10. The
    // machine this harness runs on is not one, so this row is invented in the
    // strongest sense available: the capture shows a branch the renderer would never
    // take here. 19045 is Windows 10 22H2, which is what makes the row's sentence
    // true rather than a plausible looking number.
    s->os.read        = true;
    s->os.major       = 10;
    s->os.minor       = 0;
    s->os.build       = 19045;
    s->os.isWindows10 = true;

    // Row 5, administrator rights -> GREEN. This is also what makes prepare()'s
    // answer for this state "nothing blocks the install except /preview", which is
    // the note runWindowed() then puts beside the buttons.
    s->elevated = true;

    // The two facts a winget-based offer would be made of, invented like everything
    // else here even though no current screen reads them - see the block in setup.cpp
    // where Run::thirdPartyStarted WAS declared for why the offer that used to
    // consume these is gone, and why the flag went too rather than waiting for it.
    //
    // THE VERSION STRING IS DELIBERATELY NOT A PLAUSIBLE ONE. Every other invented
    // value in this function is chosen to be true-shaped - 19045 really is Windows
    // 10 22H2 - because the sentence around it has to read correctly. A winget
    // version has no sentence around it: it is printed as a bare fact in the log
    // pane, and a bare fact that looks real is a bare fact somebody will quote. So
    // it says what it is.
    //
    // winget is USABLE and the token CAN launch. The desktop belongs to a different
    // account, which is the very next thing this function sets and the thing row 6
    // is about. One invented machine, one story.
    s->winget.state = bcdsetup::kWingetUsable;
    wcscpy(s->winget.version, L"v0.0.0-invented-for-this-picture");
    s->tokenCanLaunch = true;

    // Row 6, which account this is for -> AMBER, naming BOTH accounts. This is on
    // purpose the branch that prints the most identity, because it is the branch
    // that has to be proved harmless. The branch that leaked was the quiet green
    // one beside it.
    s->account.checked = true;
    s->account.matched = false;
    wcscpy(s->account.tokenAccount, L"EXAMPLE\\owner");
    wcscpy(s->account.shellAccount, L"EXAMPLE\\somebodyelse");

    // ---------------------------------------------------------------------
    // SCREEN 3'S SUBJECT IS SET AT THE TOP OF THIS FUNCTION, BY midiMachine(),
    // AND THE STATE IT IS SET TO IS THE ONE WITH THE LONGEST SENTENCE ON PURPOSE.
    //
    // *** THE PICTURE HAS TO BE OF THE WORST CASE, BECAUSE THE RENDER PASS ONLY
    //     MEASURES WHAT IT PHOTOGRAPHS. *** describeMidiPort() has three states
    // and this function picks ONE of them for both DPIs. allowedDeficit() holds
    // 2b-midi at ZERO overflow, so if the photographed state were the shortest
    // one, the ratchet would be measuring a page that is not the page a user on
    // the affected build sees, and the longest sentence could go under the fold
    // with the suite green. testMidiPortScreen() ASSERTS that the known-bad row is
    // the longest of the three, and asserts that this function really produces
    // that state, rather than either being trusted to this comment.
    //
    // *** AND IT IS A MACHINE THAT REALLY EXISTED. *** A machine on KB5101650: the
    // service registered, the transport present at the Windows MIDI Services
    // component version, and midisrv.exe reporting the build issue #1047's own
    // title names. Nothing is invented in shape - only in which machine it is.
    // ---------------------------------------------------------------------
}

// ---------------------------------------------------------------------------
// THE GATE, WHICH HAS TO LIVE WHERE THE WORDS ARE STILL WORDS.
//
// A picture cannot be searched, so this reads every string a renderer turns into
// pixels and looks in all of them for the four things that identify the machine
// and the person running the harness. It runs BEFORE the first capture is taken
// and it fails the run, so nothing about it depends on somebody remembering to
// open a PNG and look.
//
// WHAT IT STILL CANNOT DO, said plainly. It does not read pixels; it reads the
// list of sources below. A page that started painting from a source not in that
// list would be outside it. The list is short on purpose so that extending it is
// obvious, and shootAtDpi() paints nothing that is not in it today.
// ---------------------------------------------------------------------------
struct Needle {
    wchar_t        text[kPathMax];
    const wchar_t* what;
};
// ONE definition of the table's size. It was written twice - the array bound here and
// a literal 6 in addNeedle()'s guard - which is the same shape as every other pair of
// numbers this project has had to keep equal by hand.
static const int kMaxNeedles = 6;
static Needle g_needle[kMaxNeedles];
static int    g_needleN = 0;
// How many DISTINCT strings the gate was offered, and how many of them it could not
// hold. See addNeedle(): the second is asserted to be zero, because a privacy gate
// that quietly searches for less than it was given reports safety it does not have.
static int    g_needleOffered = 0;
static int    g_needleDropped = 0;

static bool containsNoCase(const wchar_t* hay, const wchar_t* needle)
{
    if (!hay || !needle || !needle[0])
        return false;
    size_t n = wcslen(needle);
    for (const wchar_t* p = hay; *p; p++)
        if (_wcsnicmp(p, needle, n) == 0)
            return true;
    return false;
}

// A needle shorter than four characters is refused rather than searched for: a
// machine called "PC" would match half the English on these pages and the gate
// would be a permanent false alarm, which is how a gate stops being read.
//
// *** AND A NEEDLE THIS TABLE COULD NOT HOLD USED TO BE DROPPED WITHOUT A WORD. ***
// The guard was `g_needleN >= 6` and a plain return. collectLiveIdentity() adds five
// needles and is called from BOTH gates - the preview's text scan and the picture
// scan - so the table filled with five distinct strings plus one repeat of the first,
// and the sixth DISTINCT string anybody added later would have been discarded in
// silence by both. A privacy gate that searches for less than it was given reports a
// safety it has not measured, and "no silent caps" is a rule of this project.
//
// Both halves of that are closed here, and they are two different faults:
//   A REPEAT IS NOT A NEW NEEDLE. The dedup below is what makes a second call to
//   collectLiveIdentity() cost nothing, which is what both call sites already assume.
//   Without it the table was full of duplicates before it was full of needles.
//   A DROP IS LOUD. Deduplicating alone would only postpone the silence to the
//   seventh string, so the cap counts what it refused and checkNoLiveIdentity()
//   asserts that count is zero, with the two sides coming from different places: the
//   offered counter is incremented before the table is consulted, the held count is
//   the table's own length.
static void addNeedle(const wchar_t* text, const wchar_t* what)
{
    if (!text || wcslen(text) < 4)
        return;
    for (int i = 0; i < g_needleN; i++)
        if (_wcsicmp(g_needle[i].text, text) == 0)
            return;
    g_needleOffered++;
    if (g_needleN >= kMaxNeedles) {
        g_needleDropped++;
        wprintf(L"  NEEDLE DROPPED: %s does not fit a table of %d - the gate is now "
                L"searching for less than it was given\n",
                what ? what : L"(unnamed)", kMaxNeedles);
        return;
    }
    wcsncpy(g_needle[g_needleN].text, text, kPathMax - 1);
    g_needle[g_needleN].text[kPathMax - 1] = 0;
    g_needle[g_needleN].what               = what;
    g_needleN++;
}

static void collectLiveIdentity()
{
    wchar_t buf[kPathMax];
    DWORD   n = kPathMax;
    if (GetUserNameW(buf, &n))
        addNeedle(buf, L"this account's name");
    n = kPathMax;
    if (GetComputerNameW(buf, &n))
        addNeedle(buf, L"this computer's name");
    if (GetEnvironmentVariableW(L"USERPROFILE", buf, kPathMax))
        addNeedle(buf, L"this profile's directory");
    if (bcdsetup::getLocalAppDataDir(buf, kPathMax))
        addNeedle(buf, L"this profile's local application data directory");
    if (bcdsetup::getStartupDir(buf, kPathMax))
        addNeedle(buf, L"this profile's startup folder");
}

static const wchar_t* g_hitWhat  = 0;
static const wchar_t* g_hitWhere = 0;

static void scanForIdentity(const wchar_t* text, const wchar_t* where)
{
    if (!text || g_hitWhat)
        return;
    for (int i = 0; i < g_needleN; i++)
        if (containsNoCase(text, g_needle[i].text)) {
            g_hitWhat  = g_needle[i].what;
            g_hitWhere = where;
            return;
        }
}

// ---------------------------------------------------------------------------
// *** A SENTENCE WITH EVERY BRACED SPAN EMPTIED, FOR MESSAGES THAT QUOTE A ROW. ***
//
// This harness prints diagnostics to stdout and stdout is what gets pasted into
// reports - the rule is stated at the head of the shot suite: what it must not do is
// put this machine's values there. The rows describeBinding() writes interpolate the
// WinUSB interface guid, which shotState() names as one of the two per machine values
// the leak of 2026-07-29 was about, and a run whose state has gone live prints the
// real one. That happened: a reviewer had to redact this file's own output by hand.
//
// So a message that quotes a row quotes it through here. Everything between { and }
// becomes "...", which keeps the whole of the diagnostic - which sentence it is, how
// long it is, which branch produced it - and loses only the value. The scan that
// decides whether a row NAMES this machine is unaffected: it reads the row itself,
// never this.
//
// It is not a privacy control; nothing is protected by masking a string on its way to
// a console. It is the last line of a defence whose real work is done by shotState().
// ---------------------------------------------------------------------------
static const wchar_t* maskBracedSpans(const wchar_t* text, wchar_t* out, int cap)
{
    int o = 0;
    if (cap < 1)
        return L"";
    if (!text) {
        out[0] = 0;
        return out;
    }
    for (const wchar_t* p = text; *p && o < cap - 1; p++) {
        out[o++] = *p;
        if (*p != L'{')
            continue;
        // Swallow to the closing brace, or to the end when there is none, and put
        // three dots where the value was.
        while (*(p + 1) && *(p + 1) != L'}')
            p++;
        for (int d = 0; d < 3 && o < cap - 1; d++)
            out[o++] = L'.';
    }
    out[o] = 0;
    return out;
}

// ===========================================================================
// PART 1 - the summary's text, on the real printSummary()
// ===========================================================================
static void testSummaryText()
{
    wprintf(L"\n=== PART 1: printSummary(), the real one, per branch ===\n");

    bcdsetup::MachineState s;
    fakeState(&s);
    bcdsetup::setLineSink(capSink);
    bcdsetup::setConsoleEcho(false);

    // -------------------------------------------------------------------
    // Branch A: the control service WAS stopped by this run.
    // -------------------------------------------------------------------
    wprintf(L"\n-- branch A: service stopped (midiPortDestroyed) --\n");
    Pending a;
    ZeroMemory(&a, sizeof(a));
    a.midiPortDestroyed   = true;
    a.startControlService = true;
    capReset();
    printSummary(&s, &a, false, L"");
    check(capHas(L"Close your DJ software and open it again"),
          L"A: the restart item is printed");
    check(capHas(L"stopping it destroyed the virtual MIDI port"),
          L"A: the port sentence appears, and says destroyed - which is the half this "
          L"run really measured");
    // *** THE CARRIED FALSE PROMISE, ASSERTED GONE. *** This branch used to say "The
    // port exists again once the service is running; only then can a DJ application
    // find the controller." It is reachable ONLY where this run stopped a running
    // control service - which means a port had been created this boot and has just
    // been closed, and that is the precondition for microsoft/MIDI issue #1047. So
    // the sentence made its strongest claim on the one path where the defect is most
    // likely to bite. Both halves are searched for, because a rewrite that kept
    // either would have kept the promise.
    check(!capHas(L"The port exists again") && !capHas(L"exists again once the"),
          L"A: and it does NOT promise the port back - the sentence that did was "
          L"unbacked by any reading AND stood on the exact path issue #1047 breaks");
    check(capHas(L"was NOT read by this installer") &&
          capHas(L"it never creates a port"),
          L"A: what stands there instead says what was NOT looked at, and why this "
          L"program cannot answer it");
    check(!capHas(L"was NOT touched by this install"),
          L"A: the \"not touched\" sentence is absent");
    check(!capHas(L"recreated"),
          L"A: the word \"recreated\" appears nowhere");
    // ...and on a machine that is not on the known-bad list, the #1047 paragraph is
    // NOT printed. fakeState() leaves winMidi zeroed, which classifies as unread.
    check(!capHas(L"#1047"),
          L"A: and no word about issue #1047 on a machine whose build reading did not "
          L"put it on the list - the warning is a consequence of a reading");

    // -------------------------------------------------------------------
    // Branch A2: the same run, on a machine whose build IS on the known-bad list.
    //
    // *** THIS IS THE PAIR THAT MAKES THE CONDITIONAL A CONDITIONAL. *** A2 differs
    // from A in exactly one thing - the MachineState's winMidi readings - so a
    // paragraph that printed unconditionally passes A2 and fails A, and one that
    // never printed fails A2. Neither alone proves the gate.
    // -------------------------------------------------------------------
    wprintf(L"\n-- branch A2: same run, but the build is on the known-bad list --\n");
    bcdsetup::MachineState bad;
    midiMachine(&bad, bcdsetup::kWinMidiKnownBad);
    capReset();
    printSummary(&bad, &a, false, L"");
    check(capHas(L"AND THIS BUILD IS ONE WHERE IT PROBABLY WILL NOT"),
          L"A2: on a known-bad build the summary says the port probably will NOT come "
          L"back, which is the honest half the old promise had backwards");
    check(capHas(L"#1047") && capHas(L"26100.8875"),
          L"A2: ...and names the defect and the build it read, so the claim is "
          L"traceable to a reading and to an issue a user can look up");
    check(capHas(L"Restart the machine"),
          L"A2: ...and names the one thing that clears it, which is what design "
          L"decision D5 accepted having to do");
    check(!capHas(L"The port exists again"),
          L"A2: and it still does not promise the port back anywhere");

    // -------------------------------------------------------------------
    // Branch B: the driver file was replaced and the service was NOT touched.
    //
    // *** FIX ROUND 1: THIS IS THE B2 FIXTURE, RESTORED, BECAUSE THE CLAIM IT
    //     GUARDS AGAINST IS STILL REACHABLE. *** A reviewer measured microsoft/MIDI
    // issue #1047 on the owner's own machine: after the first virtual port of a
    // boot closes, every later creation attempt fails until reboot. So "the
    // service was not stopped, therefore it still owns the port it created" is
    // false exactly when the service is running into that defect - a live,
    // common, named mechanism this program has no reading for. See the block over
    // needsDjRestart's `else` in setup.cpp for the full account, including my
    // first, wrong attempt at this fix (kept rather than deleted, because a
    // corrected comment that erases its own mistake teaches the next reader
    // nothing).
    //
    // The fix is silence: printSummary() no longer claims the port's state either
    // way when only the driver file moved. This branch is what makes that claim
    // able to fail again - it is exactly the machine the original bug reached
    // (file replaced, service left running), and now asserts the ABSENCE of both
    // halves of the old sentence rather than the presence of one of them.
    // -------------------------------------------------------------------
    wprintf(L"\n-- branch B: driver file replaced, service intact --\n");
    Pending b;
    ZeroMemory(&b, sizeof(b));
    b.driverFileReplaced = true;
    capReset();
    printSummary(&s, &b, false, L"");
    check(capHas(L"Close your DJ software and open it again"),
          L"B: the restart item is printed");
    check(capHas(L"The driver file was replaced"),
          L"B: the reason given is the driver file");
    check(!capHas(L"was NOT touched by this install") &&
          !capHas(L"still owns the port it created"),
          L"B: NEITHER half of the old port claim prints - this program has no "
          L"reading of whether a port exists, so it says nothing rather than "
          L"guess (this is the fixed defect's own fixture)");
    check(!capHas(L"destroyed the virtual MIDI port"),
          L"B: no claim that the port was destroyed");
    check(!capHas(L"recreated"),
          L"B: the word \"recreated\" appears nowhere");

    // -------------------------------------------------------------------
    // Branch C: only the registration moved. Nothing here claims the port's state
    // either, for the same reason B does not.
    // -------------------------------------------------------------------
    wprintf(L"\n-- branch C: registration repointed only --\n");
    Pending c;
    ZeroMemory(&c, sizeof(c));
    c.registrationRepointed = true;
    capReset();
    printSummary(&s, &c, false, L"");
    check(capHas(L"Close your DJ software and open it again"),
          L"C: the restart item is printed at all");
    check(capHas(L"registration now points at a different copy"),
          L"C: the reason given is the registration");
    check(!capHas(L"The driver file was replaced"),
          L"C: no claim that the file changed");
    check(!capHas(L"was NOT touched by this install") &&
          !capHas(L"still owns the port it created"),
          L"C: and neither half of the port claim prints here either");

    // -------------------------------------------------------------------
    // Branch D: nothing changed under a running application.
    // -------------------------------------------------------------------
    wprintf(L"\n-- branch D: nothing that needs a restart --\n");
    Pending d;
    ZeroMemory(&d, sizeof(d));
    d.winUsbBindingMissing = true;
    capReset();
    printSummary(&s, &d, false, L"");
    check(!capHas(L"Close your DJ software and open it again"),
          L"D: no restart item");
    // NOT "the port is never mentioned": the standing warnings block DOES talk
    // about the port, and has to - item 3 is the rule about not ending the control
    // service. What must not appear is a CLAIM ABOUT THIS RUN. That distinction is
    // the whole of C1, so the check is written on the two per run sentences and not
    // on the words "virtual MIDI port".
    check(!capHas(L"stopping it destroyed the virtual MIDI port"),
          L"D: no per run claim that the port was destroyed");
    check(!capHas(L"was NOT touched by this install"),
          L"D: and no per run claim about the port either way");
    check(capHas(L"owns the virtual MIDI port for as long as it runs"),
          L"D: the standing rule about the port is still there");
    check(capHas(L"Bind the BCD3000 to WinUSB"),
          L"D: the item that IS pending is still printed");

    // -------------------------------------------------------------------
    // Branch E: all three reasons at once, and a failed install.
    // -------------------------------------------------------------------
    wprintf(L"\n-- branch E: all three reasons, failed install --\n");
    Pending e;
    ZeroMemory(&e, sizeof(e));
    e.midiPortDestroyed     = true;
    e.driverFileReplaced    = true;
    e.registrationRepointed = true;
    capReset();
    printSummary(&s, &e, true, L"C:\\Other\\Old.dll");
    check(capCount(L"Close your DJ software and open it again") == 1,
          L"E: exactly one restart item, not three");
    check(capHas(L"registration now points at a different copy") &&
          capHas(L"The driver file was replaced") &&
          capHas(L"stopping it destroyed the virtual MIDI port"),
          L"E: all three reasons are given");
    check(capHas(L"regsvr32 \"C:\\Other\\Old.dll\""),
          L"E: the way back is still printed on failure");
    check(capHas(L"NEXT STEPS AND WARNINGS"),
          L"E: the warnings block is printed on a FAILED install too");

    // *** ITEM 8 QUOTES A HEADING THAT ANOTHER FUNCTION PRINTS, AND NOTHING TIED THE
    //     TWO TOGETHER. ***
    //
    // printNextStepsAndWarnings() ends item 8 by pointing at the list "Still on your
    // side" ABOVE it; printSummary() is what prints that heading, forty lines earlier
    // in setup.cpp. Rename the heading and item 8 goes on naming something that is
    // not on the screen - a label that nothing confirms, which is the defect class
    // this folder exists to catch, and it walked in with the sentence that was added
    // to fix a different one.
    //
    // Both halves are in THIS capture because printSummary() calls the warnings
    // block, so the tie is one comparison and needs no second run: the heading is
    // printed, the quotation is printed, and the heading really is above it. The two
    // needles are distinct - one ends in a colon, the other is inside quotation marks
    // - so neither can be satisfied by the other's line.
    int     headingAt = capIndexOf(L"Still on your side:");
    int     quotedAt  = capIndexOf(L"\"Still on your side\"");
    wchar_t tie[300];
    _snwprintf(tie, 290,
               L"E: item 8's quotation names a heading printSummary really prints, and "
               L"prints it ABOVE the quotation (heading on line %d, quotation on line "
               L"%d)", headingAt, quotedAt);
    tie[290] = 0;
    check(headingAt >= 0 && quotedAt > headingAt, tie);

    // ===================================================================
    // *** BRANCH F: WHICH MIXER THIS RUN WAS SET UP FOR, WHICH IS bcdsetup::
    //     selectedModel()'s ONE CALLER AND THE ONLY PLACE THE DEVICE SCREEN'S CHOICE
    //     IS EVER READ BACK. ***
    //
    // The device screen's two radio buttons change nothing that is installed - the driver
    // carries both profiles and matches on the usb ids at run time - so the RECORD is the
    // whole product of that screen. For one round that record reached a say() line
    // asserted only inside the device suite, against a Run that suite then threw away, and
    // selectedModel() had no caller at all. A review named it the twelfth "declared and
    // unread" in this project.
    //
    // *** IT IS HERE AND NOT IN testDeviceScreen(), AND THE REASON IS A DEFECT THAT ROUND
    //     INTRODUCED. *** The first fix put these checks in the device suite, which drove
    // printSummary() there - about 100 lines of product output landing in the middle of
    // PART 2e3b. That is the documented interleave artefact, and it SPLIT A CHECK NAME
    // ACROSS 200 LINES: "...the run chose 1 and the entry s" ended one line and "ays 1,
    // which is the first check..." appeared 208 lines later. This part is where
    // printSummary()'s branches are covered, and it is where the console echo is already
    // off - see setConsoleEcho(false) at the head of this function, which is the mechanism
    // that first fix had not found. A new branch of printSummary()'s text belongs in the
    // part whose subject is printSummary()'s text.
    //
    // *** IT DRIVES THE GLOBAL g_run AND PUTS IT BACK, AND THE RESTORE IS LOAD BEARING.
    //     *** selectedModel() reads g_run so the answer survives the window closing.
    // buildShotSummary() later fills the capture pane from this same printSummary(), ONCE,
    // before either DPI pass - so a suite that left g_run holding the BCD2000 would put the
    // EXPERIMENTAL warning into page-4-finished at both DPIs and change two committed
    // captures from a text suite. Saved, restored, and asserted back.
    //
    // WHERE EACH SIDE COMES FROM: the left is the real printSummary()'s output through the
    // product's own line sink; the right is phrases written in this file plus the model
    // name out of common.cpp. printSummary() cannot see these literals.
    // ===================================================================
    {
        wprintf(L"\n-- branch F: which mixer the run was set up for --\n");
        int      saved = ::g_run.selectedModel;
        Pending  f;
        ZeroMemory(&f, sizeof(f));
        wchar_t  fw[400];

        // *** THE MODEL NAME IS ASKED OF THE MODEL LINE ITSELF AND NOT OF THE WHOLE
        //     OUTPUT, AND THAT IS A CORRECTION. ***
        //
        // capHas(modelName(kModelBcd3000)) reads the WHOLE capture, and printSummary()
        // already contains "Select \"Behringer BCD3000\" as the ASIO device in your DJ
        // software" on the items-zero path - so that needle was satisfied whether or not the
        // model line existed at all. A re-review measured it: deleting the proven model's
        // line left the third check below GREEN, and only the two pane checks in the pixel
        // proof caught the loss. A conjunct that cannot fail is the family of defect this
        // whole task's fix rounds have been about.
        //
        // So both halves now FIND THE LINE first, by the one phrase only it carries, and
        // then ask what is on THAT line. The haystack is one sentence instead of a hundred,
        // which is the same narrowing testCableScreen() relies on for its four needles.
        ::g_run.selectedModel = bcdsetup::kModelBcd2000;
        capReset();
        printSummary(&s, &f, false, L"");
        int  atLine    = capIndexOf(L"was set up for");
        // wcsstr() and not posOf(): that helper is defined further down this file, beside
        // the suite that needs a POSITION. Here only presence on one known line matters.
        bool namedOnIt = atLine >= 0 &&
                         wcsstr(g_cap[atLine],
                                bcdsetup::modelName(bcdsetup::kModelBcd2000)) != 0;
        _snwprintf(fw, 390,
                   L"F: the summary says which mixer this run was set up for - line %d of "
                   L"%d, and THAT LINE names the %s, which is the one place a support "
                   L"request reads and the first caller selectedModel() has ever had",
                   atLine, g_capN, bcdsetup::modelName(bcdsetup::kModelBcd2000));
        fw[390] = 0;
        check(namedOnIt, fw);
        check(capHas(L"EXPERIMENTAL"),
              L"F: ...and for the model nobody has ever run, the summary says EXPERIMENTAL "
              L"there too - the loudest place in the output carries the thing a support "
              L"request most needs to know");

        // ...and the proven model gets the plain line and NOT the warning, which is what
        // says the summary reads the choice rather than printing one sentence for both.
        ::g_run.selectedModel = bcdsetup::kModelBcd3000;
        capReset();
        printSummary(&s, &f, false, L"");
        int  atProven    = capIndexOf(L"was set up for");
        bool provenNamed = atProven >= 0 &&
                           wcsstr(g_cap[atProven],
                                  bcdsetup::modelName(bcdsetup::kModelBcd3000)) != 0;
        bool stillLoud   = capHas(L"EXPERIMENTAL");
        _snwprintf(fw, 390,
                   L"F: ...and a run set up for the %s says so on line %d WITHOUT the "
                   L"experimental warning (named %d, warned %d), so the summary follows the "
                   L"CHOICE and does not print one sentence for both",
                   bcdsetup::modelName(bcdsetup::kModelBcd3000), atProven,
                   provenNamed ? 1 : 0, stillLoud ? 1 : 0);
        fw[390] = 0;
        check(provenNamed && !stillLoud, fw);

        capReset();
        ::g_run.selectedModel = saved;
        _snwprintf(fw, 390,
                   L"F: ...and this suite put g_run back as it found it (%d), so the "
                   L"captures are of the default run and not of this check's fixture",
                   ::g_run.selectedModel);
        check(::g_run.selectedModel == saved, fw);
    }

    bcdsetup::setLineSink(0);
    bcdsetup::setConsoleEcho(true);
}

// ===========================================================================
// PART 2 - the eight warnings, on the real printNextStepsAndWarnings()
//
// THE LABELS BELOW SAY "1/8", AND THAT NUMBER IS PART OF WHAT IS UNDER TEST. They
// said "1/7" until item 8 was added, and a label is not checked by anything: it
// would have gone on printing green while describing a block that had eight items.
// That is the defect class this folder exists to catch, so the count itself is
// checked FOUR ways: the items are counted, their numbers are read back in order, the
// new wording has to be present, and the old wording has to be GONE - the last of
// those twice over, here and inside the built binary (mode "exe"). Two of the four are
// counting and two are wording; the comment here used to say "four ways" in one
// sentence and "the three wording checks" in the next, which cannot both be right.
// The counting pair is the half that was missing until round 4, and the wording pair
// is satisfied by a block that says eight and has nine - measured, in the comment
// beside them.
// ===========================================================================
static void testWarningsBlock()
{
    wprintf(L"\n=== PART 2: printNextStepsAndWarnings(), the real one ===\n");

    bcdsetup::setLineSink(capSink);
    bcdsetup::setConsoleEcho(false);
    capReset();
    printNextStepsAndWarnings();
    bcdsetup::setLineSink(0);
    bcdsetup::setConsoleEcho(true);

    check(capHas(L"NEXT STEPS AND WARNINGS"), L"the block has its banner");

    // The eight topics, one check each, each looking for the thing that only
    // that topic can say.
    check(capHas(L"DO NOT LET YOUR DJ SOFTWARE INSTALL A"),
          L"1/8 do not let the DJ software install a driver");
    check(capHas(L"USB\\VID_1397&PID_00BF") && capHas(L"code 39"),
          L"1/8 carries the measured reason (the INF match and code 39)");
    check(capHas(L"\"Download drivers\"") && capHas(L"Baixar") &&
          capHas(L"DO NOT PRESS IT"),
          L"1/8 names the button, in both languages, with the verb");
    // Written with a universal character name here for the same reason it is
    // written that way in setup.cpp: a raw accented byte in either file would make
    // this comparison depend on which code page the compiler thought the source
    // was in, and a test that can pass for the wrong reason is worse than none.
    check(capHas(L"Voc\u00EA precisa instalar alguns drivers primeiro"),
          L"1/8 quotes the band literally, with the accented letter intact");
    // And the code point itself, so "intact" is measured and not asserted.
    {
        const wchar_t* found = 0;
        for (int i = 0; i < g_capN && !found; i++)
            found = wcsstr(g_cap[i], L"precisa instalar");
        unsigned code = 0;
        if (found && found >= g_cap[0]) {
            // walk back to the accented letter: "Voc<e-circumflex> precisa"
            const wchar_t* e = found - 2;
            code = (unsigned)*e;
        }
        wchar_t what[128];
        _snwprintf(what, 120, L"1/8 the accented letter is U+%04X (want U+00EA)", code);
        check(code == 0x00EA, what);
    }
    check(capHas(L"Open your DJ software again after this installer has run"),
          L"2/8 reopen the DJ software");
    check(capHas(L"installing does NOT recreate"),
          L"2/8 and it kills the false reason explicitly");
    check(capHas(L"Do not stop, end or \"restart\" the control service"),
          L"3/8 do not stop the control service");
    check(capHas(L"USB 2.0 port"), L"4/8 use a USB 2.0 port");
    check(capHas(L"CHECK THE CABLE FIRST"), L"5/8 check the cable first");
    check(capHas(L"VMware's USB arbitrator"), L"5/8 and the VM comes second");
    // *** ITEM 6 SAID "not checked by this installer yet" AND THIS ROUND MADE THAT
    //     FALSE. *** The screen checks it now, so a warnings block still saying it
    // does not would be the block contradicting the screen - and item 8's own comment
    // records what that costs: a reader who catches one item lying stops believing
    // the other seven. Both halves are asserted: the new subject present, and the old
    // claim gone, because a rewrite that kept the old sentence anywhere would keep
    // the contradiction.
    check(capHas(L"The MIDI port comes from Windows MIDI Services"),
          L"6/8 names what the port really comes from, and says it is part of Windows");
    check(capHas(L"There is nothing to install for it") &&
          capHas(L"it never creates a port"),
          L"6/8 ...and says there is nothing to install and that this program never "
          L"creates a port, which is why it never promises one");
    check(!capHas(L"is not checked by this installer yet"),
          L"6/8 ...and the claim that it is NOT checked is gone, because it is - a "
          L"warnings block that disagreed with screen 3 would make both unreadable");
    check(capHas(L"WinUSB binding is a requirement"),
          L"7/8 the WinUSB binding, and Zadig last");
    check(capHas(L"never the"), L"7/8 says Zadig is the last thing to try");
    check(capHas(L"Nothing this installer does needs Windows to be restarted"),
          L"8/8 nothing here needs Windows restarted");
    // "before you sign in", NOT "at boot time". The control service starts at every
    // sign in - item 3 says so, welcome bullet 2 says so, and the installer writes the
    // Startup shortcut that does it - so a sentence about boot was true only to a
    // reader who knows that boot and sign in are different moments, which is not the
    // reader this screen is written for.
    check(capHas(L"LoadLibrary") && capHas(L"no kernel") &&
          capHas(L"nothing that loads before you sign in"),
          L"8/8 gives the structural reason, not a reassurance");
    // The label is about item 8's REASON, not about the block: the block still says
    // "Secure Boot" two lines further down, and it has to. What is asserted here is
    // narrower than "does not say boot", which is what the label used to claim.
    check(!capHas(L"at boot time") && !capHas(L"loads at boot"),
          L"8/8 does not date the load to boot while everything else says \"sign in\"");
    // And it no longer calls reopening the DJ software "the whole of what is needed",
    // a few lines under printSummary()'s own numbered list of things that are still
    // the reader's to do. Item 8's scope is RESTARTING.
    check(!capHas(L"whole of what is needed"),
          L"8/8 does not claim to be the whole of a first install");
    // *** THE SCOPE OF ITEM 8 IS THE POINT OF ITEM 8. *** It is a claim of absence,
    // and item 7 points at a third party installer that may legitimately ask for a
    // restart. Item 6 used to point at a second one - this task removed the third
    // party detection it was about, so item 6 no longer claims anything about
    // restarts and item 8 now names only Zadig. A block that said "nothing needs a
    // restart" while also telling the reader to run Zadig would contradict itself,
    // and a reader who catches one warnings block lying stops believing the other
    // seven items.
    check(capHas(L"ONE OTHER THING CAN ASK FOR A RESTART") &&
          capHas(L"it is not this") &&
          capHas(L"Zadig (7)"),
          L"8/8 does NOT contradict 7: it names it as something that CAN ask");
    // *** THE COUNT, WITH SOMEBODY ACTUALLY COUNTING. ***
    //
    // The two wording checks below were the whole of the count proof, and the comment
    // that used to sit here claimed the second one was "what an added item nine would
    // trip over". IT IS NOT, and the reviewer of round 3 measured the path: append
    // "  9. ..." to printNextStepsAndWarnings() and change nothing else.
    // "Eight things worth knowing" is still present; "Seven things" is still absent
    // here and still absent from the binary; all four checks go on printing green
    // while the block says eight and has nine. What the absent half catches is
    // RENUMBERING the first line, not ADDING an item - and nothing in this file
    // counted the items, although capCount() was three functions away.
    //
    // So the items are counted, and their numbers are read back in order. That fails
    // on a ninth item, on a deleted one, on two items numbered 5 - and, since this
    // round, on a tenth: the reader used to look at ONE character, so "10. " had a '0'
    // where it wanted the dot and was skipped entirely.
    //
    // The count starts at the banner rather than at line 0. Nothing else is in this
    // capture today, but printSummary()'s own list numbers itself in the same shape,
    // and the next caller that captures both blocks would otherwise be handed nine or
    // ten items with the warnings block blamed for them.
    int     nums[32];
    int     items = capItemNumbers(capIndexOf(L"NEXT STEPS AND WARNINGS"), nums, 32);
    wchar_t seq[256];
    seq[0] = 0;
    for (int i = 0; i < items && i < 32; i++) {
        wchar_t one[16];
        _snwprintf(one, 15, i ? L",%d" : L"%d", nums[i]);
        one[15] = 0;
        if (wcslen(seq) + wcslen(one) < 250)
            wcscat(seq, one);
    }
    wprintf(L"  numbered items in the block: %d (%s)\n", items, seq);
    bool inOrder = (items > 0);
    for (int i = 0; i < items && i < 32; i++)
        if (nums[i] != i + 1)
            inOrder = false;
    wchar_t countWhat[400];
    _snwprintf(countWhat, 390,
               L"the block really HAS eight numbered items: %d counted in the text", items);
    countWhat[390] = 0;
    check(items == 8, countWhat);
    _snwprintf(countWhat, 390,
               L"and they run 1..%d with none missing and none repeated: %s", items, seq);
    countWhat[390] = 0;
    check(inOrder, countWhat);
    check(capHas(L"Eight things worth knowing"),
          L"the block counts itself, and the count is eight");
    check(!capHas(L"Seven things"),
          L"the old count is GONE from the text, not just outnumbered");

    // Item 1 has to be the FIRST thing in the block and the loudest thing in it.
    int bannerAt = -1, item1At = -1, item2At = -1, starBox = 0;
    for (int i = 0; i < g_capN; i++) {
        if (bannerAt < 0 && wcsstr(g_cap[i], L"NEXT STEPS AND WARNINGS"))
            bannerAt = i;
        if (item1At < 0 && wcsstr(g_cap[i], L"DO NOT LET YOUR DJ SOFTWARE"))
            item1At = i;
        if (item2At < 0 && wcsstr(g_cap[i], L"Open your DJ software again"))
            item2At = i;
        if (wcsstr(g_cap[i], L"******"))
            starBox++;
    }
    check(bannerAt >= 0 && item1At > bannerAt && item2At > item1At,
          L"order: banner, then item 1, then item 2");
    check(starBox >= 2, L"item 1 is inside a box the eye cannot miss");

    // A console is 80 columns by default and the log is read in a text editor.
    // Nothing here may need horizontal scrolling in either.
    int widest = capWidest();
    wprintf(L"  widest line in the block: %d characters (of 80 allowed)\n", widest);
    check(widest <= 80, L"every line fits 80 columns");

    wprintf(L"  lines in the block: %d\n", g_capN);
}

// ===========================================================================
// PART 2b - the review rows, and whether a RE-CHECK can address them by index
//
// WHY THE ROW COUNT IS A CORRECTNESS PROPERTY AND NOT A COSMETIC ONE.
// postReviewRow() takes an INDEX. If the number or the order of the rows depended on
// what the machine happened to look like when the page was rebuilt, a re-check would
// post row 4 of its own snapshot into row 4 of the page and the page would tell the
// user something false ABOUT A DIFFERENT CHECK. So the index has to mean the same
// question every time fillPreflightRows() runs, whatever it is handed.
// ===========================================================================
static int reviewIndexOf(const bcdgui::Wizard* w, const wchar_t* titlePart)
{
    for (int i = 0; i < w->reviewCount; i++)
        if (wcsstr(w->review[i].title, titlePart))
            return i;
    return -1;
}

static void testReviewRows()
{
    wprintf(L"\n=== PART 2b: fillPreflightRows(), the real one, twice over ===\n");

    // Task 1: a review row can be replaced after the page was first filled, which is
    // what a re-check does. The FIRST fill is the one the install acts on; the
    // SECOND has to be able to say something different, or the button is a lie.
    //
    // *** THE FIELD THIS BLOCK FLIPS HAD TO CHANGE WITH THE PAGE, AND THAT IS NOT
    //     BOOKKEEPING. *** It used to install teVirtualMIDI between the two fills.
    // That row is not on this page any more - it is stated on the MIDI port's own
    // screen - so the flip changed nothing, both fills came back identical, and all
    // three checks below would have passed while measuring that a re-check can say
    // NOTHING different. A check whose two sides became one is worse than no check.
    // The ASIO registration is flipped instead: it is the first row of this page, it
    // is the one the install is FOR, and it moves from grey to green on exactly the
    // event this block is about.
    {
        bcdsetup::MachineState s;
        ZeroMemory(&s, sizeof(s));
        s.pathsResolved = true;
        s.elevated      = true;
        bcdgui::Wizard w;
        ZeroMemory(&w, sizeof(w));
        bcdgui::fillPreflightRows(&w, &s);
        int before = w.reviewCount;
        for (int i = 0; i < before; i++)
            wprintf(L"  row %d: [%d] %s\n", i, (int)w.review[i].state, w.review[i].title);
        check(before >= 4, L"the review starts with at least four rows");
        int asio = reviewIndexOf(&w, L"ASIO driver registration");
        check(asio >= 0 && w.review[asio].state == bcdgui::kRowNeutral,
              L"...and on a machine with nothing registered the ASIO row is grey, "
              L"which is the state the re-check below has to be able to move");

        // now pretend the driver was registered from the install folder and the user
        // pressed Check again
        s.asio.clsidKeyPresent    = true;
        s.asio.asioNameKeyPresent = true;
        s.asio.clsidMatches       = true;
        s.asio.inprocPresent      = true;
        wcscpy(s.asio.inprocPath, L"C:\\Program Files\\BCD3000 ASIO\\BcdAsio.dll");
        bcdgui::Wizard w2;
        ZeroMemory(&w2, sizeof(w2));
        bcdgui::fillPreflightRows(&w2, &s);
        check(w2.reviewCount == before,
              L"a re-check produces the same number of rows, so indices stay stable");
        int asio2 = reviewIndexOf(&w2, L"ASIO driver registration");
        // *** >= 0 AS WELL AS EQUAL, AND THE FIRST DRAFT OF THIS LINE HAD ONLY THE
        //     EQUALITY. *** reviewIndexOf() answers -1 for "not found", so an edit
        //     that RENAMED this row made both fills answer -1 and the check passed on
        //     -1 == -1 - measured, by an injection that renamed it. A comparison
        //     between two absences is not a statement that the row did not move.
        check(asio2 == asio && asio2 >= 0,
              L"the ASIO row is findable by title and has not moved");
        check(asio2 >= 0 && w2.review[asio2].state == bcdgui::kRowOk,
              L"after the driver is registered the row turns ok on a re-check");
    }

    // *** THE COUNT ASSERTION ABOVE PASSED ON ITS VERY FIRST RUN, AND NOT FOR THE
    //     REASON IT WAS WRITTEN FOR. ***
    //
    // Measured, printed above: both fills produce FIVE rows and the fifth is "Which
    // account this is for". Both blocks ZeroMemory the MachineState and neither one
    // touches `account`, so account.checked is false in both and the SAME optional row
    // is emitted twice. Two equal counts because the same branch was taken twice is
    // not the statement "the count does not depend on the state", and the second is
    // what an index posted from a re-check is measured against.
    //
    // So the variable is flipped here, which is what makes this the failing test. There
    // was ONE slot for TWO questions and it could be left out altogether:
    //
    //   elevated, desktop owner read and it is us -> no row at all       (4 rows)
    //   elevated, desktop owner not readable      -> "Which account..."  (5 rows)
    //   elevated, desktop owner is somebody else  -> "Which account..."  (5 rows)
    //   not elevated                              -> "Administrator..."  (5 rows)
    //
    // Four states, two different counts, and a row 4 that asks one of two different
    // questions. The fix is two permanent rows, one question each, never omitted - and
    // NOT a shorter page: the elevation row now says so when elevation is fine, which
    // is information the page did not carry before.
    {
        const int kStates = 4;
        static const wchar_t* label[4] = {
            L"elevated, desktop owner is this account",
            L"elevated, desktop owner unreadable",
            L"elevated, desktop owner is somebody else",
            L"not elevated"
        };
        bcdgui::Wizard w[kStates];
        for (int k = 0; k < kStates; k++) {
            bcdsetup::MachineState s;
            ZeroMemory(&s, sizeof(s));
            s.pathsResolved = true;
            s.elevated      = (k != 3);
            s.account.checked = (k != 1);
            s.account.matched = (k != 1 && k != 2);
            wcscpy(s.account.tokenAccount, L"EXAMPLE\\owner");
            wcscpy(s.account.shellAccount,
                   k == 2 ? L"EXAMPLE\\somebodyelse" : L"EXAMPLE\\owner");
            ZeroMemory(&w[k], sizeof(w[k]));
            bcdgui::fillPreflightRows(&w[k], &s);
            wprintf(L"  %-42s %d rows\n", label[k], w[k].reviewCount);
        }

        // The subject comparison walks the UNION of the two row lists, not the shorter
        // of them: a row that is simply absent from one snapshot is a difference in
        // subject too, and comparing up to the shorter list would have printed [ok]
        // here while the counts were 4 and 5 - measured, it did.
        bool sameCount = true, sameSubjects = true;
        for (int k = 1; k < kStates; k++) {
            if (w[k].reviewCount != w[0].reviewCount)
                sameCount = false;
            for (int i = 0; i < bcdgui::kMaxRows; i++) {
                bool in0 = i < w[0].reviewCount;
                bool ink = i < w[k].reviewCount;
                if (in0 != ink)
                    sameSubjects = false;
                else if (in0 && wcscmp(w[k].review[i].title, w[0].review[i].title) != 0)
                    sameSubjects = false;
            }
        }
        wchar_t what[400];
        _snwprintf(what, 390,
                   L"the row COUNT does not depend on the machine: %d/%d/%d/%d rows over "
                   L"the four account and elevation states",
                   w[0].reviewCount, w[1].reviewCount, w[2].reviewCount,
                   w[3].reviewCount);
        check(sameCount, what);
        check(sameSubjects,
              L"and every index asks the same QUESTION in all four, so a row posted "
              L"by index cannot land on a different check");

        // AND THE COUNT WAS NOT MADE STABLE BY DELETING INFORMATION. Each of the four
        // states still gets its own answer; only the slots stopped moving.
        int el0 = reviewIndexOf(&w[0], L"Administrator rights");
        int el3 = reviewIndexOf(&w[3], L"Administrator rights");
        check(el0 >= 0 && el3 >= 0 && el0 == el3 &&
              w[0].review[el0].state == bcdgui::kRowOk &&
              w[3].review[el3].state == bcdgui::kRowFail,
              L"elevation is still judged, not flattened: ok when elevated, red when "
              L"not, in the same slot");
        int ac0 = reviewIndexOf(&w[0], L"Which account this is for");
        int ac1 = reviewIndexOf(&w[1], L"Which account this is for");
        int ac2 = reviewIndexOf(&w[2], L"Which account this is for");
        check(ac0 >= 0 && ac0 == ac1 && ac0 == ac2 &&
              w[0].review[ac0].state == bcdgui::kRowOk &&
              w[1].review[ac1].state == bcdgui::kRowWarn &&
              w[2].review[ac2].state == bcdgui::kRowWarn &&
              wcsstr(w[2].review[ac2].detail, L"EXAMPLE\\somebodyelse") != 0,
              L"the account question is still answered three different ways, and the "
              L"wrong account case still names both accounts");
    }

    // ===================================================================
    // *** TWO BLOCKS STOOD HERE AND WENT WITH THE TWO ROWS THEY MEASURED. ***
    //
    // They asserted the severity of the teVirtualMIDI and WinUSB rows on this page -
    // amber for the one where the audio still works, red for the one where nothing
    // does - and, for the binding, that "Zadig was run on the wrong line" and "Zadig
    // has not been run" are different sentences. Both rows are stated on screens of
    // their own now and this page does not repeat them, so both blocks were asserting
    // about rows that do not exist. They were deleted rather than retargeted here,
    // because their subject moved and the suites that own it were already driving it:
    //
    //   the marks and the three teVirtualMIDI readings   testMidiPortScreen(), which
    //       asserts NO case is red - a stronger and DIFFERENT statement than the row
    //       made. The row called "found, but not where the service opens it" red;
    //       the screen calls it amber, deliberately, because the audio still works.
    //       The row was the one place in the program that disagreed with the rest.
    //   the five binding readings, their marks, and the wrong-line sentence
    //       testBindingScreen(), which drives five readings where this drove three.
    //   the zeroed state that must not invent a wrong binding
    //       testBindingScreen(), against describeBinding() itself, which is where the
    //       arithmetic now lives. The copy here was a duplicate of it.
    //
    // What was genuinely only here is repaired where the fact now lives, not
    // reinstated here: see the "not 0" needle in testBindingScreen().
    // ===================================================================

    // *** THE TWO REGISTRY CONSTANTS THE SIBLING SEARCH IS BUILT FROM STILL DESCRIBE
    //     THE SAME DEVICE AS THE BINDING CHECK. ***
    //
    // kUsbEnumKey is the function key detectWinUsbBinding() reads; kUsbEnumParentKey
    // and kUsbFunctionPrefix are what the sibling search walks. If those drifted, the
    // page would answer "you bound interface 1" about a different device from the one
    // whose absence it just reported - and nothing else in the program would notice,
    // because both halves would still be internally consistent.
    {
        wchar_t rebuilt[512];
        _snwprintf(rebuilt, 500, L"%s\\%s00", bcdsetup::kUsbEnumParentKey,
                   bcdsetup::kUsbFunctionPrefix);
        rebuilt[500] = 0;
        wprintf(L"  rebuilt: %s\n  actual : %s\n", rebuilt, bcdsetup::kUsbEnumKey);
        check(wcscmp(rebuilt, bcdsetup::kUsbEnumKey) == 0,
              L"the parent key and the function prefix rebuild kUsbEnumKey exactly, "
              L"so the sibling search looks at the device the binding check reads");
    }

    // *** AND THE MODEL TABLE DESCRIBES THE SAME DEVICE AS THAT KEY, WHICH IS A THIRD
    //     COPY OF THE SAME TWO IDS AND THEREFORE A THIRD PLACE THEY CAN DRIFT. ***
    //
    // The device screen offers models out of common.cpp's table, and the DETECTION that
    // fills that screen's row reads kUsbFunctionPrefix - which spells the same vendor and
    // product id in a different form. If those drifted, the screen would offer a mixer
    // this program cannot detect while reporting on one it does not name, and both halves
    // would still be internally consistent, which is the exact shape the check above
    // exists for one level down.
    //
    // WHERE EACH SIDE COMES FROM: the left is built from modelVid(0) and modelPid(0),
    // which are numbers; the right is a registry path written as text. Two
    // representations of one fact, in two definitions, neither derived from the other.
    //
    // MODEL 0 AND NOT A LOOP: there is one detection key and it is the proven model's.
    // The BCD2000 has no key of its own here on purpose - see kDeviceBullet2, which is
    // the screen saying so - and inventing one would be this file asserting a detection
    // the program does not perform.
    {
        wchar_t fromModel[128];
        _snwprintf(fromModel, 120, L"VID_%04X&PID_%04X&MI_",
                   (unsigned)bcdsetup::modelVid(bcdsetup::kModelBcd3000),
                   (unsigned)bcdsetup::modelPid(bcdsetup::kModelBcd3000));
        fromModel[120] = 0;
        wchar_t what2[400];
        _snwprintf(what2, 390,
                   L"the model table and the detection key name the same device - the "
                   L"table rebuilds \"%s\" and the key prefix is \"%s\"",
                   fromModel, bcdsetup::kUsbFunctionPrefix);
        what2[390] = 0;
        check(wcscmp(fromModel, bcdsetup::kUsbFunctionPrefix) == 0, what2);
    }

    // *** THE WINDOWS 10 ROW, AND THE ONE THING IT IS NOT ALLOWED TO BREAK. ***
    //
    // It is the first row in this page's history that is CONDITIONAL on something
    // other than the four states above, and Task 1 spent a whole round making the
    // row count independent of the machine so that postReviewRow()'s index means the
    // same question every time. Two facts keep those from contradicting each other,
    // and both are asserted rather than argued:
    //
    //   THE VERSION CANNOT CHANGE WHILE THE PROGRAM IS OPEN. Windows does not
    //   change its build number under a running process; doing so takes a restart,
    //   which ends this one. So within the lifetime of a window - which is the only
    //   lifetime an index has to survive - the row is present or absent, never both.
    //
    //   IT IS THE LAST ROW. Even if the first fact were wrong, appending means no
    //   OTHER row's index can move when it appears or disappears. That is what is
    //   checked below, over the union of the two lists, exactly as the four state
    //   check above does it.
    //
    // AND IT IS NEUTRAL, NOT AMBER. Nothing about Windows 10 is a defect on this
    // machine or a thing the user has to fix; it is a fact that opens an option this
    // program cannot recommend and will not hide. An amber mark would be this page
    // telling somebody their working operating system is a problem.
    {
        bcdsetup::MachineState ten, eleven;
        ZeroMemory(&ten, sizeof(ten));
        ten.pathsResolved = true; ten.elevated = true;
        ten.account.checked = true; ten.account.matched = true;
        eleven = ten;

        ten.os.read = true; ten.os.major = 10; ten.os.minor = 0;
        ten.os.build = 19045; ten.os.isWindows10 = true;
        eleven.os.read = true; eleven.os.major = 10; eleven.os.minor = 0;
        eleven.os.build = 26200; eleven.os.isWindows10 = false;

        bcdgui::Wizard w10, w11;
        ZeroMemory(&w10, sizeof(w10));
        ZeroMemory(&w11, sizeof(w11));
        bcdgui::fillPreflightRows(&w10, &ten);
        bcdgui::fillPreflightRows(&w11, &eleven);
        wprintf(L"  windows 10 build %lu: %d rows; windows 11 build %lu: %d rows\n",
                (unsigned long)ten.os.build, w10.reviewCount,
                (unsigned long)eleven.os.build, w11.reviewCount);

        int at10 = reviewIndexOf(&w10, L"Windows 10");
        check(at10 >= 0, L"on Windows 10 the page says so");
        check(at10 >= 0 && w10.review[at10].state == bcdgui::kRowNeutral,
              L"and it is NEUTRAL, not a warning: a working operating system is not "
              L"a defect on this page");
        check(at10 == w10.reviewCount - 1,
              L"it is the LAST row, so its presence cannot move the index of any "
              L"check postReviewRow() addresses");
        check(reviewIndexOf(&w11, L"Windows 10") < 0 &&
              w11.reviewCount == w10.reviewCount - 1,
              L"on Windows 11 the row is absent altogether - the sentence would be "
              L"false there");

        bool sameSubjects = true;
        for (int i = 0; i < w11.reviewCount; i++)
            if (wcscmp(w10.review[i].title, w11.review[i].title) != 0)
                sameSubjects = false;
        check(sameSubjects,
              L"every row the two versions share is at the same index and asks the "
              L"same question");

        // WHAT THE ROW HAS TO SAY, because a bare "you are on Windows 10" would be
        // trivia. It is here to name an option the user has and this program does
        // not: the manufacturer's own package, which may still work there - and then
        // to close the loop, because taking that option is precisely warning 1.
        const wchar_t* d = at10 >= 0 ? w10.review[at10].detail : L"";
        check(wcsstr(d, L"24H2") != 0,
              L"...and names the Windows version whose change this project exists "
              L"for");
        check(wcsstr(d, L"was not announced for Windows 10") != 0,
              L"...says the removal was not announced for Windows 10, which is the "
              L"whole reason the option is open");
        check(wcsstr(d, L"mutually exclusive") != 0,
              L"...and that the two choices are mutually exclusive");
        check(wcsstr(d, L"19045") != 0,
              L"...over the build it actually measured, rather than a claim about "
              L"Windows 10 in general");

        // A version that could not be read is not Windows 10. Saying nothing is the
        // right answer to a question nobody could answer, and the alternative - a row
        // that guesses - is how a page starts stating things it did not measure.
        bcdsetup::MachineState unknown;
        ZeroMemory(&unknown, sizeof(unknown));
        unknown.pathsResolved = true; unknown.elevated = true;
        bcdgui::Wizard wu;
        ZeroMemory(&wu, sizeof(wu));
        bcdgui::fillPreflightRows(&wu, &unknown);
        check(reviewIndexOf(&wu, L"Windows 10") < 0,
              L"a version that could not be read produces no row at all, rather "
              L"than a guess");
    }
}

// ===========================================================================
// PART 2c - the walkthrough, and the ORDER being the thing under test
//
// The six steps are not a script somebody liked the sound of. Step 0 is first
// because enumKeyPresent means literally "this machine has seen this device at
// least once", so until the mixer has been plugged in once the WinUSB row cannot
// tell "never seen" from "seen but not bound" - which is why the row's own text
// says both facts in one sentence. Plugging in first separates them, so the order
// improves what the page can DIAGNOSE and not only what it advises.
//
// EVERY ORDER CHECK BELOW USES capIndexOf, WHICH IS THE FIRST LINE A PHRASE
// APPEARS ON. That makes the assertions strict in a way worth stating: a mention
// of loopMIDI anywhere above step 0 - in a heading, in a summary of what is
// coming - fails this, correctly, because a reader meets the words in the order
// they are printed and not in the order they are numbered.
// ===========================================================================
static void testWalkthrough()
{
    wprintf(L"\n=== PART 2c: printPrerequisiteWalkthrough(), the real one ===\n");

    capReset();
    bcdsetup::setLineSink(capSink);
    bcdsetup::setConsoleEcho(false);
    // Unqualified, like printNextStepsAndWarnings() above it. setup.cpp says
    // "using namespace bcdsetup" and then defines its own functions as file statics
    // in the GLOBAL namespace, so bcdsetup::printPrerequisiteWalkthrough does not
    // exist and never did - the plan's snippet wrote it that way and it does not
    // compile.
    printPrerequisiteWalkthrough();
    bcdsetup::setConsoleEcho(true);
    bcdsetup::setLineSink(0);

    int plug  = capIndexOf(L"USB 2.0 port and switch it on");
    // Step 1's own subject. This needle used to name a third party ("loopMIDI"),
    // then named the GAP left when that went ("does not check anything yet"), and
    // now names what the step is really about - because the step has a subject
    // again. See printMidiPortStepBody() in setup.cpp.
    int loop  = capIndexOf(L"created through Windows MIDI Services");
    int zadig = capIndexOf(L"List All Devices");
    // *** THIS NEEDLE WAS "this installer" AND IT WAS NOT DISCRIMINATING. *** The
    // walkthrough's own steps talk ABOUT this installer, so the first line matching
    // it was whichever step mentioned the program first - not the step that tells
    // somebody to RUN it. The round that gave step 1 its new subject made that
    // visible: step 1 now says "this installer looks at whether the midisrv service
    // is registered", which matched at line 20 and put "ours" BEFORE Zadig at 42.
    // The check went red for a wording change, which is the right outcome from the
    // wrong cause - the ordering never moved. Narrowed to the imperative that only
    // the step in question can carry, which is the same string the console-subject
    // table already identifies that step by.
    int ours  = capIndexOf(L"Run this installer and press Install");
    wprintf(L"  %d lines; plug=%d loop=%d zadig=%d ours=%d\n",
            g_capN, plug, loop, zadig, ours);

    check(plug >= 0, L"the walkthrough starts by having the mixer plugged in");
    check(plug >= 0 && loop  > plug,  L"plugging in comes before the MIDI port step");
    check(loop >= 0 && zadig > loop,
          L"the MIDI port step comes before Zadig - reasoned, not measured");
    check(zadig >= 0 && ours > zadig, L"Zadig comes before our installer");
    check(capHas(L"1397"),  L"the walkthrough names the USB ID");
    check(capHas(L"00BF"),  L"...both halves of it");
    check(capHas(L"MI_00"), L"...and the function");
    check(capHas(L"libusb"),
          L"the WRONG targets are named too - the risk is not failing to find "
          L"WinUSB, it is finding something that looks like it");

    // *** THE HONEST LIMIT IS PART OF THE TEXT, NOT PART OF A REVIEW THREAD. ***
    // Steps 1 and 2 are independent: a virtual MIDI port has nothing to do with the
    // binding on the physical device. The order between them is kept for the restart
    // reason alone, and that reason is REASONED AND NOT MEASURED. A walkthrough that
    // presents a habit as a requirement is the same defect class as a summary that
    // claims an install did something it only attempted.
    check(capHas(L"reasoned, not measured"),
          L"the one ordering that is a habit rather than a requirement says so");
    check(capHas(L"INDEPENDENT"),
          L"...and names which two steps it is talking about");

    // -------------------------------------------------------------------
    // *** STEP 1 IS THE SAME TEXT ON EVERY MACHINE, SO EVERY SENTENCE IN IT HAS TO
    //     BE TRUE OF EVERY MACHINE. *** printMidiPortStepBody() takes no
    // MachineState - it is one function feeding the pane, /console and the log file -
    // so it may describe the defect but may not tell anybody they have it. The row
    // above it, describeMidiPort(), is what says which of the three states this
    // machine is in, and testMidiPortScreen() is where that is asserted.
    //
    // WHERE EACH SIDE COMES FROM: the haystack is what say() really emitted through
    // the capture sink; the needles are phrases written in this file.
    // -------------------------------------------------------------------
    check(capHas(L"NOTHING TO INSTALL"),
          L"step 1 opens by saying there is nothing to install for the MIDI port, "
          L"because Windows MIDI Services is in-box");
    check(capHas(L"IT NEVER CREATES A PORT"),
          L"...and says outright that this program never creates a port, which is why "
          L"nothing anywhere in it promises one will work");
    check(capHas(L"#1047") && capHas(L"no fix date"),
          L"...and names the Windows defect by its issue number and says it is "
          L"unfixed, so the one thing a user could not guess is written down");
    check(capHas(L"ON THE AFFECTED BUILDS") && capHas(L"the row above answers"),
          L"...and states the defect CONDITIONALLY and points at the row for this "
          L"machine's own answer - a machine-independent text may not claim a "
          L"machine-dependent fact");
    check(capHas(L"arrives through Windows Update"),
          L"...and says where the fix comes from, which is why this product does not "
          L"have to be republished for it");
    check(!capHas(L"does not check anything yet"),
          L"...and the claim that this step checks nothing is GONE, because it checks "
          L"three things now and the pane may not disagree with the row above it");

    // The two steps after ours, which are the ones a user meets when they think the
    // work is over: the port does not exist until the service runs, and the DJ
    // software's offer to download drivers undoes everything.
    check(capIndexOf(L"Startup shortcut") > ours,
          L"starting the control service comes after our installer");
    check(capIndexOf(L"download drivers") > ours,
          L"and the last step is the one that can undo all of it");

    // A walkthrough that promises to do any of it for the user would be a promise
    // this program has decided not to keep - it never rebinds a USB device and it
    // never redistributes somebody else's installer.
    check(!capHas(L"we will install") && !capHas(L"will be installed for you"),
          L"nothing here promises to do somebody else's install for the user");

    // *** THE TWO THINGS READ OFF THE SCREENSHOT, WHICH ARE HERE AND NOT ONLY IN THE
    //     PAINTED CAPTION. *** Painted text is the one kind of text in this program
    // that /console never prints and a screen reader cannot reach, so a fact that
    // lived only under the picture would be a fact half the audience never gets.
    check(capHas(L"WCID"),
          L"the walkthrough says the red cross beside WCID means nothing here - "
          L"without it somebody reads it as a failure");
    check(capHas(L"More Information"),
          L"...and that the More Information column is links and not the selector, "
          L"which is where people get stuck");
    check(capHas(L"up and down arrows"),
          L"...and which control actually changes the target");

    // *** AND THE PICTURE'S CAVEAT, WHICH IS THE HALF THAT KEEPS IT HONEST. *** The
    // screenshot is of a machine that is already bound, so the Driver box and the
    // button label differ from what a new user sees. A picture that promises one
    // screen and delivers another is the defect two rounds of this project removed
    // from this installer's text.
    check(capHas(L"ALREADY bound"),
          L"the walkthrough declares that the picture is of an already bound "
          L"machine");
    check(capHas(L"usbaudio"),
          L"...and says what the Driver box will read instead on a clean one");
    check(capHas(L"whatever it is called"),
          L"...and refuses to name one button label as the one to expect, because "
          L"Zadig picks that word from what is already bound and not every variant "
          L"has been seen here");
}

// ===========================================================================
// PART 2d - PUTTING AN ADDRESS ON A COMMAND LINE, AND LAUNCHING WITHOUT ELEVATION
//
// *** THIS PART USED TO BE "THE WINGET CONTRACT, WHICH IS THE WHOLE OF THE
//     ACCELERATION", AND HALF ITS SUBJECT NO LONGER EXISTS. *** The contract had two
// properties. The FIRST - never suppress the author's own interface - was asserted
// on the string buildWingetInstallCommand() returned; that function had no
// production caller left after Task 5 and is deleted, and its fourteen checks went
// with it (see the block where they stood, below). The SECOND - unelevated, in the
// desktop owner's session - is still live and still asserted here as far as an
// unelevated harness can reach, because openPageInBrowser() ends in
// launchUnelevated(); the part it cannot reach is SKIPPED BY NAME rather than
// passed over, and that named skip is one of the two this suite reports.
//
// So what this part now measures is the two boundaries this program still crosses:
// a url going onto a command line, and a process being started in somebody else's
// session. Both have real callers.
//
// The binary scan that used to be kept as a BACKSTOP in PART 4 - the winget id and
// the SHA-256 in the shipped file - went in Task 5 with the constants it read,
// because a scan for a literal the program no longer contains cannot fail.
// ===========================================================================
static void testWingetContract()
{
    wprintf(L"\n=== PART 2d: the winget command line, and the provenance ===\n");

    // -------------------------------------------------------------------
    // *** FOURTEEN CHECKS ON THE COMMAND LINE STOOD HERE AND WENT WITH THE BUILDER
    //     THEY ASSERTED ON. ***
    //
    // They proved that the string buildWingetInstallCommand() returned carried none
    // of the seven flags that suppress an author's own installer interface, that it
    // named the package, the source and --exact, and that it refused a truncating
    // buffer and an id that was really a flag. Every one of them drove the product's
    // function with a package id THIS FILE owned, `kTestPackageId = "Example.Package"`,
    // because after Task 5 the program had no id of its own left to pass.
    //
    // *** AND THAT IS WHY THEY GO. *** Both sides of those comparisons had become
    // harness-owned: a function with no production caller, driven by a literal
    // invented here, asserting a contract about a command line this program never
    // builds. That is the harness testing its own fixture, which is the finding this
    // round was asked to act on rather than count. The rule the fourteen encoded is
    // not lost - it is the standing instruction over the winget block in common.h,
    // which is what somebody rebuilding the offer reads first, and rebuilding it
    // means rebuilding these checks against a live caller.
    //
    // What is left in this part is the two things that ARE still live: the url
    // boundary, because this program really does put kZadigDownloadPage on a command
    // line, and the launch mechanism, because openPageInBrowser() really does end in
    // launchUnelevated().
    // -------------------------------------------------------------------

    // -------------------------------------------------------------------
    // The url boundary check. kZadigDownloadPage is a literal this program opens,
    // so it must pass; the shapes that could carry a command must not. The
    // equivalent check against a third party download page went with the
    // detection and the offer it fed - see the block in setup.cpp where
    // Run::thirdPartyStarted was declared.
    // -------------------------------------------------------------------
    {
        check(bcdsetup::isSafeUrlForCommandLine(bcdsetup::kZadigDownloadPage),
              L"the Zadig page is safe to put on a command line");
        check(!bcdsetup::isSafeUrlForCommandLine(L"https://x.example/\" & calc.exe"),
              L"a url carrying a quote and a second command is refused");
        check(!bcdsetup::isSafeUrlForCommandLine(L"file:///C:/Windows/System32"),
              L"a url that is not http or https is refused");
        check(!bcdsetup::isSafeUrlForCommandLine(L"https://x.example/a b"),
              L"...and so is one with a space in it");
    }

    // -------------------------------------------------------------------
    // THE LAUNCH MECHANISM, PROVED WITH A TARGET THAT CHANGES NOTHING.
    //
    // cmd /c exit 7 is the target because the exit code is unambiguous and cmd.exe
    // is on every Windows. It installs nothing, writes nothing and reads nothing.
    //
    // *** WHAT THIS RUN CANNOT COVER, AND IT IS NOT WORKED AROUND. ***
    // CreateProcessWithTokenW needs SE_IMPERSONATE_NAME. This harness is asInvoker,
    // so its token is filtered and does not carry it, and the end to end path is
    // therefore not reachable from here at all. The false branch below is the
    // expected one, and what it asserts is the only thing that IS provable here:
    // that a refusal comes back with a real error code, so the fallback ladder has
    // something to say instead of a silent nothing. The rest is skipped BY NAME.
    // -------------------------------------------------------------------
    // What detectWinget() says about THIS machine. PRINTED AND NOT CHECKED, on
    // purpose: the answer is a property of whoever runs the harness, and a check on
    // it would fail on a clone for a reason that has nothing to do with the
    // installer. It is here because a detection nobody ever runs is a detection
    // nobody knows is broken - and every assertion above uses invented states.
    {
        bcdsetup::WingetInfo wg;
        bcdsetup::detectWinget(&wg);
        wprintf(L"  detectWinget() on this machine: state %d, version \"%s\", last %lu\n",
                (int)wg.state, wg.version, (unsigned long)wg.lastError);
    }

    {
        DWORD  err  = 0;
        HANDLE proc = 0;
        wchar_t cmd[64];
        wcscpy(cmd, L"cmd.exe /c exit 7");
        bool ok = bcdsetup::launchUnelevated(cmd, &err, &proc);
        wprintf(L"  launchUnelevated(cmd /c exit 7) -> %s, err %lu (%s), elevated %s\n",
                ok ? L"started" : L"refused", (unsigned long)err,
                bcdsetup::winErrText(err), bcdsetup::isElevated() ? L"yes" : L"no");
        if (!ok) {
            check(err != 0,
                  L"a refused launch reports a real error code, so the fallback has "
                  L"something to say");
            skipped(L"launchUnelevated end to end: needs an elevated caller, and this "
                    L"harness is asInvoker. The controller runs it with the owner.");
        } else {
            WaitForSingleObject(proc, 10000);
            DWORD code = 1;
            GetExitCodeProcess(proc, &code);
            wchar_t what[200];
            _snwprintf(what, 190,
                       L"the child ran and its exit code came back (%lu, wanted 7)",
                       (unsigned long)code);
            check(code == 7, what);

            // And the point of the whole exercise: the child is NOT elevated, so a
            // child that needs administrator rights raises its own UAC prompt
            // naming its own publisher.
            HANDLE tok = 0;
            bool   readIt = OpenProcessToken(proc, TOKEN_QUERY, &tok) != 0;
            TOKEN_ELEVATION el;
            ZeroMemory(&el, sizeof(el));
            DWORD got = 0;
            if (readIt)
                GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &got);
            if (tok)
                CloseHandle(tok);
            _snwprintf(what, 190,
                       L"the child is NOT elevated (token read %d, elevated %lu), so a "
                       L"child that needs administrator rights raises its OWN prompt "
                       L"naming its OWN publisher",
                       readIt ? 1 : 0, (unsigned long)el.TokenIsElevated);
            check(readIt && el.TokenIsElevated == 0, what);
        }
        if (proc)
            CloseHandle(proc);
    }
}

// ===========================================================================
// PART 2e USED TO STAND HERE: THE FALLBACK LADDER, EVERY RUNG OF IT.
//
// *** THIS TASK REMOVES AND DOES NOT ADD. *** offerState() and testOfferLadder()
// exercised chooseLoopMidiOffer() through every rung of its winget/open-page
// ladder, and that function is gone along with the MachineState field it read.
// There is no offer to test until a later task rebuilds one on top of the
// replacement detection.
// ===========================================================================

// ===========================================================================
// PART 2e2 - THE SCREEN TABLE
//
// The four hard coded pages became a table of Screen descriptors. This suite is
// what stops that table quietly losing an entry, and it is deliberately the only
// thing in this file that reads it without a window: the whole reason the flow is
// DATA now is that data can be asserted from here, and a page index living inside
// a message handler could not be.
//
// *** WHERE EACH SIDE OF EVERY COMPARISON COMES FROM. *** The left side is always
// the table that buildScreens() filled - the product's own function, called here on
// a Wizard this suite zeroed itself. The right side is always a literal written on
// this line. Neither is derived from the other: the counts are not read back out of
// anything the product computed, and buildScreens() has no way to see them. If
// buildScreens() stops filling an entry, or fills it with the wrong kind, the
// literal does not move with it.
// ===========================================================================

// *** A POSITION CANNOT ASSERT A COUNT, AND THE CODE RESTS ON THE COUNT. ***
// screenOfKind() returns the FIRST entry of a kind and goToKind() sends the window
// to it, and both of them are only correct if there is exactly one. Every check in
// this suite compares a POSITION, and a table built as [Work, Check, Work, Done]
// satisfies all of them while giving screenOfKind() two answers to choose between.
// This is what closes that gap. It counts over the filled part of the table only,
// for the same reason screenCount() exists: a zeroed tail is not part of the flow.
static int countOfKind(const bcdgui::Wizard* w, bcdgui::ScreenKind kind)
{
    int n = 0;
    int end = bcdgui::screenCount(w);
    for (int i = 0; i < end; i++)
        if (w->screens[i].kind == kind)
            n++;
    return n;
}

static void testScreenTable()
{
    wprintf(L"\n--- the screen table ---\n");

    wchar_t what[256];

    bcdsetup::MachineState s;
    fakeState(&s);
    bcdgui::Wizard w;
    ZeroMemory(&w, sizeof(w));
    bcdsetup::buildScreens(&w, &s);

    _snwprintf(what, 250,
               L"the setup describes its flow as a table of screens (%d of an expected "
               L"9: the mixer, WHICH mixer, the MIDI port, getting Zadig and applying "
               L"the binding each have a screen of their own)",
               bcdgui::screenCount(&w));
    check(bcdgui::screenCount(&w) == 9, what);
    check(w.screens[0].kind == bcdgui::kScreenInfo,
          L"screen 0 is the opening, which measures nothing");

    // *** ENTRY 1 WAS ASSERTED BY NOTHING, THE SAME HOLE THE UNINSTALLER HAD. ***
    // Entries 0, 2 and 3 each had a line; entry 1 had none, so a table built as
    // [Info, Info, Work, Done] passed every check in this suite. What that costs on
    // the setup side is the Install button: IDC_PRIMARY calls startWork() on a
    // kScreenCheck that says startsTheWork and only setScreen(g_screen + 1) on a
    // kScreenInfo, so pressing Install would walk onto the work screen with no
    // worker behind it. The pictures would also move - page 2 draws its rows from
    // onKind(kScreenCheck) - but a picture proves what it renders, not what the
    // entry IS, and this suite is where the kind is meant to be stated.
    //
    // ENTRY 1 IS NOW THE MIXER, and the two lines it needs are about the two things
    // that make it safe: it measures, and its press does NOT install. A table where
    // it started the work would be a program with two buttons that write to this
    // machine, one of them wearing the word Next.
    check(w.screens[1].kind == bcdgui::kScreenCheck,
          L"entry 1 is a screen that measures one subject of its own");
    check(!w.screens[1].startsTheWork && !w.screens[1].paintsMachineReview,
          L"...and it neither starts the work nor paints the machine review, so a "
          L"check screen is no longer automatically either of those things");

    // ===================================================================
    // *** ENTRY 2 IS "WHICH MIXER", THE LAST SCREEN THIS REDESIGN ADDS, AND THE FIVE
    //     LINES IT NEEDS ARE THE FIVE THAT MAKE IT A SCREEN AND NOT A NEW KIND. ***
    //
    // It is kScreenCheck because it really measures something - describeModel() reads
    // the MachineState and reports which mixer answered - and the count above
    // countOfKind() predicted exactly this three tasks ago: "kScreenCheck is
    // deliberately NOT counted, because Tasks 3 to 6 make it five." This is the fifth.
    //
    // *** AND IT IS THE FIRST ENTRY IN EITHER FLOW TO CARRY A CHOICE, SO THE FIELDS ARE
    //     ASSERTED THE WAY THE PANE AND THE ACTION WERE WHEN THEY WERE NEW. *** Both of
    // those were declared and unread for rounds, and an entry that set them and got
    // nothing looked exactly like an entry that worked. Three fields make a choice - two
    // labels and a function - so all three are looked at, and the ORDER of the labels is
    // looked at too: a table that offered the experimental model first would preselect
    // nothing wrong and still put the unproven path at the top of the screen.
    // ===================================================================
    check(w.screens[2].kind == bcdgui::kScreenCheck &&
          w.screens[2].title == ::kDeviceTitle,
          L"entry 2 is WHICH mixer, and it measures one subject of its own: this screen "
          L"is a check screen and not a fifth kind, because it has a reading to report");
    check(!w.screens[2].startsTheWork && !w.screens[2].paintsMachineReview,
          L"...and it neither starts the work nor paints the machine review, so the "
          L"screen that carries this program's only controls-on-the-page inherits none "
          L"of page 2's furniture");
    check(w.screens[2].choose != 0 &&
          w.screens[2].choiceLabels[0] != 0 && w.screens[2].choiceLabels[0][0] != 0 &&
          w.screens[2].choiceLabels[1] != 0 && w.screens[2].choiceLabels[1][0] != 0,
          L"...and it carries a CHOICE of its own - two labels and a function to record "
          L"the press - which no entry in either flow could have before this round");
    check(!w.screens[2].paneText && !w.screens[2].showZadigShot &&
          !w.screens[2].showDevicePhoto,
          L"...and no pane and no picture: its two controls need the whole 398 pixel "
          L"strip, and a pane would cut that to 221 and put a scroll under them");
    // *** THE BLOCK IT DELIBERATELY DOES NOT SET, AND HERE IT COULD NOT BE ANYTHING
    //     ELSE. *** The unmet state on this screen is "nothing was detected", and the
    // whole design of the screen is that the person then chooses by hand. Blocking would
    // refuse a BCD2000 owner for ever, because the one key this program reads is the
    // BCD3000's and on that machine the detection cannot succeed at all.
    check(!w.screens[2].blockNextWhenUnmet,
          L"...and it does NOT block Next: nothing detected is the state this screen "
          L"exists to hand over to a person, not a wall to stop them at");

    // *** ENTRY 3 IS THE MIDI PORT, AND WHAT IT NEEDS SAID CHANGED WITH THIS TASK. ***
    // It measures, it does not start the work, and it does not paint the machine
    // review - the same three the mixer needs, for the same reason. It still carries
    // a pane of its own, declared and unread until the round that added it. It no
    // longer carries an action: the winget offer this screen used to have is gone
    // along with the third party detection it was built on - see the block over
    // kMidiTitle in setup.cpp.
    check(w.screens[3].kind == bcdgui::kScreenCheck,
          L"entry 3 is a screen that measures one subject of its own");
    check(!w.screens[3].startsTheWork && !w.screens[3].paintsMachineReview,
          L"...and it neither starts the work nor paints the machine review, so the "
          L"second added screen inherits none of page 2's furniture either");
    check(w.screens[3].paneText != 0 && w.screens[3].paneText[0] != 0 &&
          w.screens[3].paneCaption != 0,
          L"...and it carries a pane of its OWN, with a caption of its own, which no "
          L"screen but the machine review could have before this round");
    check(w.screens[3].action == 0 && w.screens[3].actionLabel == 0,
          L"...and no action any more - the button this screen used to offer went with "
          L"the detection it was about");
    // *** AND THE BLOCK IT DELIBERATELY DOES NOT SET. *** Without the MIDI port the
    // audio works and the controls do not, so this screen must let somebody past.
    //
    // ASKED OF THE FIELD AND NOT OF nextAllowed(), on purpose. That predicate is
    // declared, defined and called by NOTHING in gui.cpp today: the screen that
    // blocks is the WinUSB binding and it does not exist yet, so a check on the
    // predicate here would be a test of an interface with no caller - which is
    // exactly the shape this project keeps finding on the wrong side of a comment.
    // The field is real data that buildScreens() really writes, and it is what the
    // predicate will read when there is something to read it for.
    check(!w.screens[3].blockNextWhenUnmet,
          L"...and it does NOT block Next: without it the audio works and only the "
          L"knobs, buttons and LEDs are dead, which is not a reason to stop somebody "
          L"installing the half this project owns");

    // NAME REWRITTEN: this line compares ONE position and cannot see a second work
    // screen. "Exactly one" is now asserted below, by count, and this name says only
    // what it looks at.
    // *** THE TWO SCREENS THIS ROUND ADDS, AND THE ORDER IS THE ASSERTION. ***
    // Get Zadig exists so that applying the binding has ONE subject, so a table that
    // put them the other way round - or that put the download after the picture -
    // would have built the two screens and lost the reason for having two.
    check(w.screens[4].kind == bcdgui::kScreenInfo &&
          w.screens[4].title == ::kZadigTitle,
          L"entry 4 is Get Zadig, and it measures nothing: Zadig installs nothing and "
          L"leaves no key, so there is no reading a row here could honestly paint");
    check(w.screens[5].kind == bcdgui::kScreenCheck &&
          w.screens[5].title == ::kBindingTitle,
          L"...and entry 5 is applying the binding, which comes AFTER getting it");
    check(!w.screens[5].paneText,
          L"...and it has NO pane: a screen with one gets a 221 logical pixel strip at "
          L"96 DPI and the picture alone is 254, so a pane here would cost the screen "
          L"its own subject");
    check(w.screens[5].showZadigShot && !w.screens[4].showZadigShot,
          L"...and the picture is on the screen that APPLIES the binding, not on the "
          L"one that downloads the program");

    check(w.screens[6].kind == bcdgui::kScreenCheck &&
          w.screens[6].paintsMachineReview,
          L"entry 6 is the machine review, which is still the page every remaining "
          L"subject sits on");
    check(!w.screens[6].showZadigShot,
          L"...and it does NOT ask for the Zadig picture any more - the 254 pixels and "
          L"the three paragraph caption that arrived below its fold are entry 5's");
    check(w.screens[7].kind == bcdgui::kScreenWork,
          L"entry 7 is the work screen, so the work is neither the first thing nor "
          L"the last");
    check(w.screens[8].kind == bcdgui::kScreenDone,
          L"the last screen is the summary");
    // *** EXACTLY ONE ENTRY OFFERS A CHOICE, WHICH IS WHAT ITS TWO CONTROLS REST ON. ***
    // flowHasChoice() creates the controls when ANY entry asks, and layout() shows them
    // on whichever screen the window is on that asks - so a second entry carrying a
    // choice would put the same pair of radio buttons on two screens, with the second
    // screen's labels and the first screen's meaning. Counted here rather than assumed,
    // for the reason the machine review's count is counted: it is the property the
    // controls' correctness actually depends on.
    {
        int choices = 0;
        for (int i = 0; i < bcdgui::screenCount(&w); i++)
            if (w.screens[i].choose && w.screens[i].choiceLabels[0])
                choices++;
        _snwprintf(what, 250,
                   L"exactly one entry offers a choice (%d), so this program's only two "
                   L"controls on a page have one screen to be on", choices);
        check(choices == 1, what);
    }

    // *** EXACTLY ONE ENTRY PAINTS THE MACHINE REVIEW, WHICH IS WHAT ITS FURNITURE
    //     RESTS ON. *** The pane, the Zadig picture, the "Check again" and action
    // buttons and the note beside them are all gated on this one field now. Two
    // entries carrying it would put the same pane on two screens and give
    // measureRecheckAndNudge() two answers to choose between; zero would take page 2
    // away entirely and leave a window with no rows in it.
    int reviews = 0;
    for (int i = 0; i < bcdgui::screenCount(&w); i++)
        if (w.screens[i].paintsMachineReview)
            reviews++;
    _snwprintf(what, 250,
               L"exactly one entry paints the machine review (%d), so page 2's pane, "
               L"picture and buttons have one screen to be on", reviews);
    check(reviews == 1, what);

    // *** THE COUNT THAT goToKind() ACTUALLY RESTS ON. ***
    // gui.cpp says, above screenOfKind() and again above goToKind(), that any flow
    // this program builds has exactly one work screen and exactly one done screen.
    // Until this line that sentence named a verification that did not exist. The
    // two kinds counted here are the two screenOfKind() is called with from gui.cpp;
    // kScreenCheck is deliberately NOT counted, because Tasks 3 to 6 make it five.
    int setupWork = countOfKind(&w, bcdgui::kScreenWork);
    int setupDone = countOfKind(&w, bcdgui::kScreenDone);
    _snwprintf(what, 250,
               L"the setup's table holds exactly one work screen (%d) and exactly one "
               L"done screen (%d), which is the uniqueness screenOfKind() rests on",
               setupWork, setupDone);
    check(setupWork == 1 && setupDone == 1, what);

    bcdgui::Wizard u;
    ZeroMemory(&u, sizeof(u));
    bcduninstall::buildScreens(&u);
    _snwprintf(what, 250,
               L"the uninstaller describes THREE screens and does not inherit the "
               L"setup's opening (%d)", bcdgui::screenCount(&u));
    check(bcdgui::screenCount(&u) == 3, what);

    // *** THE COUNT ALONE CANNOT SAY THIS, WHICH IS WHY IT IS A SECOND CHECK. ***
    // A uninstaller that built [opening, confirm, work] would also count three and
    // would also pass the line above, while having grown the welcome page it has
    // never had. The kind of its FIRST entry is the thing that says it did not.
    check(u.screens[0].kind == bcdgui::kScreenCheck,
          L"...and it opens on its confirmation rather than on an opening, which the "
          L"count alone cannot tell");

    // *** THE ENTRY NO CHECK HAD EVER LOOKED AT, IN THE FLOW WITH NO PICTURES. ***
    // Entry 1 was the one gap in this table: [Check, Info, Done] counted three,
    // opened on the confirmation and ended on the summary, and passed everything.
    // What it costs is not cosmetic. startWork() sets g_working and then calls
    // goToKind(kScreenWork), which finds nothing and returns without doing anything
    // - so the window stays on the confirmation, with its primary button still
    // reading "Remove" and still enabled, while the worker deletes files and
    // unregisters the driver behind it. The uninstaller renders into no tracked
    // capture, so no picture in this repository would ever have shown that.
    check(u.screens[1].kind == bcdgui::kScreenWork,
          L"...and its middle screen is the work, which is where startWork() sends a "
          L"removal it has already begun");

    // *** THIS ONE EXISTS BECAUSE OF A GUARD IN gui.cpp, AND IT IS FAIR TO SAY SO. ***
    // goToKind() is where the finished run is sent, and it does NOTHING when the flow
    // has no screen of that kind - which leaves the window on the work page instead
    // of blanking it. That is the right behaviour to have chosen and it is also a
    // SILENT one, where the setPage(kPgDone) it replaces could not miss. The count
    // above would be perfectly happy with three screens ending in the wrong kind, so
    // the thing the guard made quiet is asserted here instead.
    check(u.screens[2].kind == bcdgui::kScreenDone,
          L"...and its last screen is the summary, which is where a finished removal "
          L"is sent");

    // *** THE FIELD WHOSE ABSENCE SHIPPED A BLANK PAGE, AND EVERY CHECK ABOVE STAYED
    //     GREEN THROUGH IT. ***
    //
    // Read the four above again with this one missing: three screens, opening on a
    // kScreenCheck, work in the middle, done at the end - all true, all still true,
    // of a first page that painted its heading, a rule and nothing else for
    // twenty-eight commits. The kinds are the flow's SHAPE and this is the only line
    // in the table that says the page has anything on it: renderCheckScreen() sends
    // an entry without it to renderSubject(), which draws Screen::title, a rule and
    // Screen::row - and this entry has no row, because its content is the flow's
    // review.
    //
    // *** IT IS ASSERTED OF THE UNINSTALLER SEPARATELY FROM THE SETUP AND THAT IS
    //     THE LESSON, NOT AN OVERSIGHT BEING CORRECTED. *** The identical check
    // exists for the setup a few dozen lines above and has held the whole time. The
    // flow that broke is the flow nobody wrote it for.
    check(u.screens[0].paintsMachineReview,
          L"...and the confirmation screen SAYS it paints the flow's review rows, "
          L"which is the one line between 'What this will do' and an empty page");

    // The same uniqueness the setup's table is held to. This flow has one check
    // screen today, so the count can only be 0 or 1 - which is exactly the pair the
    // line above cannot distinguish from a second entry stealing the rows if one is
    // ever added.
    {
        int uReviews = 0;
        for (int i = 0; i < bcdgui::screenCount(&u); i++)
            if (u.screens[i].paintsMachineReview)
                uReviews++;
        _snwprintf(what, 250,
                   L"...and exactly one entry in the uninstaller's table paints them "
                   L"(%d), so the rows have one screen to be on in both flows",
                   uReviews);
        check(uReviews == 1, what);
    }

    // The same count, for the other flow. Three screens ending in the right kinds
    // still does not say there is only one of each: a two entry table cannot hold
    // two work screens, but a table that grows can, and this flow is the one whose
    // goToKind() misses are invisible.
    int unWork = countOfKind(&u, bcdgui::kScreenWork);
    int unDone = countOfKind(&u, bcdgui::kScreenDone);
    _snwprintf(what, 250,
               L"the uninstaller's table holds exactly one work screen (%d) and exactly "
               L"one done screen (%d), so goToKind() has one answer in both flows",
               unWork, unDone);
    check(unWork == 1 && unDone == 1, what);

    // ===================================================================
    // *** buildScreens() IS A FUNCTION OF THE MACHINE NOW, SO ONE INVENTED MACHINE
    //     TESTS ONE MACHINE OUT OF FOUR. ***
    //
    // Every check above this line calls it with fakeState() - a mixer that is plugged
    // in, bound and answering. That was the whole truth while the function ignored
    // the state it was handed and said `(void)s;`. It reads it now, and the entry it
    // reads it for has four answers, so the questions worth asking of the OTHER three
    // machines are the two below: does the flow keep its shape, and does the screen
    // really follow the machine.
    //
    // WHERE EACH SIDE COMES FROM: the tables are the product's, built here from four
    // states this suite invents; the expected shape is the literal 6 and the kinds
    // written on this line. The second check compares the four tables with EACH
    // OTHER, which is a comparison no literal could make and the only one that can
    // catch the state being ignored again.
    // ===================================================================
    {
        static const bool usb[4][3] = {
            { false, false, false },   // never seen
            { true,  false, false },   // seen, never bound, not here
            { true,  true,  false },   // bound, not answering
            { true,  true,  true  }    // connected
        };
        bcdgui::Wizard four[4];
        bool sameShape = true;
        for (int i = 0; i < 4; i++) {
            bcdsetup::MachineState m;
            fakeState(&m);
            m.usb.enumKeyPresent       = usb[i][0];
            m.usb.guidPresent          = usb[i][1];
            m.usb.interfacePresentNow  = usb[i][2];
            ZeroMemory(&four[i], sizeof(four[i]));
            bcdsetup::buildScreens(&four[i], &m);
            if (bcdgui::screenCount(&four[i]) != 9)
                sameShape = false;
            for (int k = 0; k < 9; k++)
                if (four[i].screens[k].kind != w.screens[k].kind ||
                    four[i].screens[k].startsTheWork != w.screens[k].startsTheWork)
                    sameShape = false;
        }
        check(sameShape,
              L"the flow has the same nine screens on all four machines - the design "
              L"says always all of them, and a satisfied step is not an empty screen "
              L"because it reports what it found");

        // ...and the screen really follows the machine. Four different machines, four
        // different sentences: if buildScreens() went back to ignoring the state, or
        // if two of describeCable()'s branches were made to say the same thing, this
        // is the line that goes red rather than a suite that quietly tests one case
        // four times.
        int pairsAlike = 0;
        for (int i = 0; i < 4; i++)
            for (int j = i + 1; j < 4; j++)
                if (wcscmp(four[i].screens[1].row.detail,
                           four[j].screens[1].row.detail) == 0)
                    pairsAlike++;
        _snwprintf(what, 250,
                   L"...and the mixer screen's row follows the machine it was built "
                   L"from: 4 machines, %d pairs of them saying the same thing",
                   pairsAlike);
        check(pairsAlike == 0, what);
    }

    // ===================================================================
    // *** A SCREEN'S PICTURE FLAG IS SUFFICIENT ON ITS OWN, AND THAT IS ABOUT THE
    //     GATE THAT DECODES THE BITMAP AND NOT THE ONE THAT DRAWS IT. ***
    //
    // renderSubject() reads Screen::showZadigShot and Screen::showDevicePhoto. It can
    // only draw a bitmap something else DECODED, and every decode site used to ask
    // Wizard::zadigCaption and Wizard::showDevicePhoto instead - so a screen that set
    // its own flag on a flow whose Wizard field was null got no picture and no
    // complaint. It worked in the shipped program only because setup.cpp sets both,
    // which is the definition of a defect waiting for the next author.
    //
    // THE FLOWS BELOW ARE INVENTED, AND THAT IS THE ONLY WAY TO ASK THIS. The real
    // setup flow satisfies both halves of the union, so asking it would pass against
    // the broken code as easily as against the fixed one. Each of these is exactly the
    // shape the defect took: one screen, its own flag set, and a Wizard that offers
    // nothing.
    //
    // WHERE EACH SIDE COMES FROM: the left is the product's own gate, the expression
    // runWizard(), onDpiChanged() and this harness's window builder all use; the right
    // is a table this suite built field by field. The predicate never reads the field
    // the check varies except through the union it is being asked about.
    {
        bcdgui::Wizard pic;
        ZeroMemory(&pic, sizeof(pic));
        pic.screens[0].kind  = bcdgui::kScreenCheck;
        pic.screens[0].title = L"a screen that asks for a picture";

        // A flow offering neither. FIRST, because a predicate that simply answered
        // true would satisfy both checks below and this is what refuses it.
        check(!bcdgui::flowNeedsPhoto(&pic) && !bcdgui::flowNeedsZadigShot(&pic),
              L"a flow whose table asks for no picture and whose Wizard offers none "
              L"decodes neither, so the gate is a question and not a yes");

        pic.screens[0].showZadigShot = true;
        check(bcdgui::flowNeedsZadigShot(&pic),
              L"...a screen that asks for the Zadig picture gets it DECODED even "
              L"though the flow's zadigCaption is null - the screen's flag is enough");

        pic.screens[0].showZadigShot   = false;
        pic.screens[0].showDevicePhoto = true;
        check(bcdgui::flowNeedsPhoto(&pic),
              L"...and the same for the photograph, whose flow flag is false: a "
              L"picture asked for by an entry cannot vanish because the flow is silent");
    }

    // *** A ZEROED ENTRY ENDS THE TABLE, ASSERTED RATHER THAN ASSUMED. ***
    // screenCount() walks until it meets an entry with no title instead of reading a
    // stored number, and that is the whole reason there is no stored number to keep
    // equal to the table. This is done LAST because it destroys the table it is done
    // to; the checks above have already had their answers.
    ZeroMemory(&w.screens[2], sizeof(w.screens[2]));
    _snwprintf(what, 250,
               L"a zeroed entry ends the table - the flow is 2 screens long once entry "
               L"2 is cleared (%d), so no stored count can drift from it",
               bcdgui::screenCount(&w));
    check(bcdgui::screenCount(&w) == 2, what);
}

// ===========================================================================
// PART 2e2b - THE SCREEN THAT INSTALLS: WHAT WILL BE WRITTEN, AND WHAT WILL NOT
//             BE TOUCHED
//
// *** THE FIRST TWO CHECKS BELOW ARE THE PAIR WHOSE ABSENCE MADE A DUPLICATION
//     INVISIBLE FOR THREE TASKS. *** The MIDI port got a screen of its own in Task 4
// and the WinUSB binding in Task 5, and this page went on stating both a second time.
// Nothing compared the two statements, because nothing could: one lives in
// describeMidiPort()/describeBinding() and the other in fillPreflightRows(), and every
// check in this file looked at one of the two. Both rounds shipped green.
//
// *** AND THE FIVE THAT STAY ARE ASSERTED BY NAME AND NOT BY COUNT. *** A count would
// pass on any five rows. This project has measured that trap twice, and here it would
// be worse than usual: the row list is MACHINE DEPENDENT - the Windows 10 row is
// emitted only on Windows 10 and deliberately last - so a count is not even a fixed
// number to compare against. Both machines are driven below and the four unconditional
// subjects are looked for by title in each.
// ===========================================================================
static void testInstallScreen(void)
{
    wprintf(L"\n--- the screen that installs ---\n");

    wchar_t what[400];

    // Two machines, one Windows 10 and one Windows 11, because the row list is not
    // the same length on both and a check written against one of them would be a
    // check written against a fixture.
    bcdsetup::MachineState ten, eleven;
    fakeState(&ten);
    ten.os.read = true; ten.os.major = 10; ten.os.minor = 0;
    ten.os.build = 19045; ten.os.isWindows10 = true;
    eleven = ten;
    eleven.os.build = 26200; eleven.os.isWindows10 = false;

    bcdgui::Wizard w10, w11;
    ZeroMemory(&w10, sizeof(w10));
    ZeroMemory(&w11, sizeof(w11));
    bcdgui::fillPreflightRows(&w10, &ten);
    bcdgui::fillPreflightRows(&w11, &eleven);
    wprintf(L"  rows: %d on Windows 10, %d on Windows 11\n",
            w10.reviewCount, w11.reviewCount);

    // ---- the two that are stated on screens of their own ----
    //
    // WHERE EACH SIDE COMES FROM: the left is the real fillPreflightRows() run over a
    // machine this suite invented; the right is the literal title the deleted rows
    // carried. Putting either row back turns these red and names it.
    // *** THE NEEDLE IS THE ROW'S TITLE NOW, NOT THE OLD SUBJECT'S NAME. *** It used
    // to search for "teVirtualMIDI", which no describeMidiPort() row has said since
    // this task removed the detection - a needle that can never be found again is a
    // check that can never fail, so it is the row's own title, "The MIDI port",
    // which fillPreflightRows() genuinely could still duplicate if a future round
    // reunited the two screens by accident.
    _snwprintf(what, 390,
               L"the MIDI port is stated on its own screen and not a second time here "
               L"(row index %d of %d, -1 is right)",
               reviewIndexOf(&w11, L"The MIDI port"), w11.reviewCount);
    what[390] = 0;
    check(reviewIndexOf(&w11, L"The MIDI port") < 0 &&
          reviewIndexOf(&w10, L"The MIDI port") < 0, what);

    _snwprintf(what, 390,
               L"...and so is the binding, whose screen is the one that refuses to be "
               L"left (row index %d of %d, -1 is right)",
               reviewIndexOf(&w11, L"WinUSB"), w11.reviewCount);
    what[390] = 0;
    check(reviewIndexOf(&w11, L"WinUSB") < 0 &&
          reviewIndexOf(&w10, L"WinUSB") < 0, what);

    // ---- the four that have nowhere else to be, on BOTH machines ----
    {
        static const wchar_t* const kOurs[4] = {
            L"ASIO driver registration",
            L"Control and LED service",
            L"Administrator rights",
            L"Which account this is for"
        };
        for (int i = 0; i < 4; i++) {
            int at10 = reviewIndexOf(&w10, kOurs[i]);
            int at11 = reviewIndexOf(&w11, kOurs[i]);
            _snwprintf(what, 390,
                       L"\"%s\" is still on this screen, on both Windows versions "
                       L"(index %d and %d)", kOurs[i], at11, at10);
            what[390] = 0;
            check(at11 >= 0 && at10 >= 0 && at10 == at11, what);
        }
    }

    // ...and the fifth, which exists only where its sentence is true, and last.
    // testReviewRows() also drives this row, and that suite's subject is
    // postReviewRow()'s index staying meaningful; this one's is that the row is one of
    // the five subjects this screen still OWNS after two were taken off it. The
    // overlap is one line and the two would fail for different reasons.
    {
        int at10 = reviewIndexOf(&w10, L"Windows 10");
        _snwprintf(what, 390,
                   L"...and the Windows 10 row is last where it applies and absent "
                   L"where its sentence would be false (index %d of %d on 10, %d on 11)",
                   at10, w10.reviewCount, reviewIndexOf(&w11, L"Windows 10"));
        what[390] = 0;
        check(at10 == w10.reviewCount - 1 && reviewIndexOf(&w11, L"Windows 10") < 0,
              what);
    }

    // ---- the screen's own words ----
    bcdsetup::MachineState s;
    fakeState(&s);
    bcdgui::Wizard w;
    ZeroMemory(&w, sizeof(w));
    bcdsetup::buildScreens(&w, &s);

    // The design's row 6 is "Install the driver", and the title is the one thing on a
    // screen a reader cannot scroll past. Asked of the WORDS rather than of the
    // pointer: comparing titleAt(5) with the constant buildScreens() assigned would be
    // one value read twice, and the thing that has to be true is what it SAYS.
    // The message names the NEEDLE as well as the haystack: a failure reading only
    // `titled for what it does: "Foo"` tells a reader what was found and not what was
    // wanted, which is half a verdict.
    _snwprintf(what, 390,
               L"the screen that installs is titled for what it does - its title must "
               L"contain \"Install\" and it says \"%s\"",
               w.screens[6].title ? w.screens[6].title : L"(none)");
    what[390] = 0;
    check(w.screens[6].title != 0 && wcsstr(w.screens[6].title, L"Install") != 0,
          what);

    // *** AND THE HALF OF THE DESIGN NO SCREEN STATED UNTIL THIS TASK. *** Section 3.2
    // asks this screen for "what will be written and where, AND what will not be
    // touched". The rows are the first half. The second is the sentence that makes an
    // Install press honest, and it is what the two dropped rows leave behind: the
    // subjects are still measured, on their own screens, and this program still will
    // not change either of them.
    // *** AND THE TWO SUBJECTS LEFT THIS PAGE BY MOVING, NOT BY BEING DELETED. ***
    // The two checks at the head of this suite say the rows are GONE from here, and on
    // their own they would pass just as happily if the facts had been deleted from the
    // program - which is the trade renderReview() refused for three tasks in those
    // words: "a report with a hole in it where its most important line used to be is a
    // worse page than a report that repeats a screen". So the destination is asserted
    // beside the absence. The rows below are written by describeMidiPort() and
    // describeBinding() onto entries 3 and 5, from the same MachineState. (Those two
    // indices were 2 and 4 until the device screen was inserted at position 2; the
    // subjects did not move, the table grew in front of them.)
    _snwprintf(what, 390,
               L"...and neither fact was deleted with its row: entry 3 must still measure "
               L"a row naming \"MIDI port\" and says \"%s\", entry 5 one naming "
               L"\"WinUSB\" and says \"%s\"",
               w.screens[3].row.title[0] ? w.screens[3].row.title : L"(nothing)",
               w.screens[5].row.title[0] ? w.screens[5].row.title : L"(nothing)");
    what[390] = 0;
    check(wcsstr(w.screens[3].row.title, L"MIDI port") != 0 &&
          wcsstr(w.screens[5].row.title, L"WinUSB") != 0, what);
    {
        bool saysNot = false, saysWinUsb = false;
        for (int i = 0; i < 4 && w.screens[6].bullets[i]; i++) {
            if (wcsstr(w.screens[6].bullets[i], L"will not"))
                saysNot = true;
            // *** ONE SUBJECT NOW, NOT TWO. *** This bullet used to name teVirtualMIDI
            // alongside WinUSB, because the MIDI port screen used to carry a button
            // that ran a third party installer - see the block over kInstallBullet2
            // in setup.cpp. That button and the detection under it are gone, so this
            // bullet names only the WinUSB binding, which this press still leaves
            // alone.
            if (wcsstr(w.screens[6].bullets[i], L"WinUSB"))
                saysWinUsb = true;
        }
        check(saysNot,
              L"...and it says what will NOT be touched, not only what will - which is "
              L"the half of the design no screen carried before this task");
        check(saysWinUsb,
              L"...naming the one subject it measures and leaves alone, which is what "
              L"the WinUSB row dropped from this page leaves behind");
    }
}

// ===========================================================================
// PART 2e3 - THE MIXER SCREEN'S ONE SUBJECT, AND ITS FOUR ANSWERS
//
// describeCable() is the first thing in this program that turns one field of the
// MachineState into one screen's sentence, and the reason it has four branches
// rather than two is a measured fact: enumKeyPresent means "this machine has seen
// this device at least once" and that key survives forever, so it separates "never
// seen" from "seen and not bound". A screen that said the same thing in two of the
// four would lose exactly the distinction that plugging in first exists to create.
//
// *** THE SUBSTRING HAZARD, ASKED AND ANSWERED FOR EVERY NEEDLE. *** This file has
// found a substring matching the wrong thing in three consecutive rounds - a path
// that matched inside five other printed lines, a folder name that matched as the
// prefix of an unrelated path. Both were needles searched in a haystack of MANY
// sentences. These are searched in ONE: the haystack is a single Row::detail that
// describeCable() has just written, so the only strings a needle can be found in are
// the four this function can produce. The four are compared against each other
// below, which is what makes that argument checkable instead of asserted: if any
// case's text contained another case's needle, the pairwise check would still pass
// but the case checks would stop distinguishing, so both are here.
//
// WHERE EACH SIDE COMES FROM: the left is the sentence the product wrote for a
// machine this suite invented; the right is a phrase written in this file. Neither
// reads the other, and setup.cpp cannot see these literals.
// ===========================================================================

// Where a needle starts, or -1. There is no such helper in this file - the plan's
// sample called capHasIn() and indexOfIn(), neither of which exists - and the two
// ordering checks below need the POSITION and not merely the presence.
static int posOf(const wchar_t* hay, const wchar_t* needle)
{
    const wchar_t* at = (hay && needle) ? wcsstr(hay, needle) : 0;
    return at ? (int)(at - hay) : -1;
}

// ---------------------------------------------------------------------------
// *** THE SAME SEARCH, IGNORING CASE, AND IT EXISTS BECAUSE A CLASS GUARD BUILT IN
//     THIS FILE FELL TO ONE CAPITAL LETTER. ***
//
// posOf() is wcsstr(), which is case SENSITIVE, and that is correct for almost
// everything this file asks: an exact label, a published address, a constant. It is
// wrong for exactly one question - "does this prose point at a control?" - because
// three of that check's needles are sentence-openers and every line of a pane begins
// with a capital. Measured, on the same say() line of the same pane, one character
// apart:
//
//     "press the button"  ->  RED, 4 offences, screen 3 named
//     "Press the button"  ->  906 checks, 0 failures, VERIFY_OK
//
// So the guard caught the phrasing that already existed and missed the one somebody
// would most naturally write next - in a pane, past a green harness, visible only to
// a person looking at a running window, which is precisely how instance 14 arrived.
//
// It is a separate function and posOf() is left alone ON PURPOSE. Making the whole
// file case-blind would loosen dozens of exact-spelling comparisons that are exact for
// a reason; what needed loosening is the one search whose subject is English prose.
//
// _wcsnicmp() and not a hand-rolled fold: the needles are ASCII by standing
// constraint, the haystacks are this program's own prose, and a fold written here
// would be a third thing to get wrong.
// ---------------------------------------------------------------------------
static int posOfNoCase(const wchar_t* hay, const wchar_t* needle)
{
    if (!hay || !needle || !needle[0])
        return -1;
    size_t n = wcslen(needle);
    for (const wchar_t* at = hay; *at; at++)
        if (_wcsnicmp(at, needle, n) == 0)
            return (int)(at - hay);
    return -1;
}

static void testCableScreen()
{
    wprintf(L"\n--- the mixer screen: one subject, four answers ---\n");

    wchar_t what[400];

    struct CableCase {
        bool                 enumKey;
        bool                 guid;
        bool                 present;
        const wchar_t*       must;
        bcdgui::RowState     mark;
    };
    // The four machines, in the order the branches are written, with the phrase each
    // one has to say and the mark it has to wear.
    static const CableCase cases[4] = {
        { false, false, false, L"has never seen",             bcdgui::kRowWarn },
        { true,  false, false, L"has seen this mixer before", bcdgui::kRowWarn },
        { true,  true,  false, L"is not answering right now", bcdgui::kRowWarn },
        { true,  true,  true,  L"is connected",               bcdgui::kRowOk   }
    };

    bcdgui::Row got[4];
    for (int i = 0; i < 4; i++) {
        bcdsetup::MachineState s;
        fakeState(&s);
        s.usb.enumKeyPresent      = cases[i].enumKey;
        s.usb.guidPresent         = cases[i].guid;
        s.usb.interfacePresentNow = cases[i].present;
        ZeroMemory(&got[i], sizeof(got[i]));
        bcdsetup::describeCable(&s, &got[i]);
        wprintf(L"  usb(%d,%d,%d) -> [%d] %s\n", cases[i].enumKey ? 1 : 0,
                cases[i].guid ? 1 : 0, cases[i].present ? 1 : 0, (int)got[i].state,
                got[i].detail);
        _snwprintf(what, 390,
                   L"the mixer screen tells case %d apart by name - it has to say "
                   L"\"%s\" and it says \"%s\"", i, cases[i].must, got[i].detail);
        what[390] = 0;
        check(posOf(got[i].detail, cases[i].must) >= 0, what);
    }

    // *** FOUR ANSWERS AND NOT THREE. *** Each case says its own phrase, which is
    // not the same as the four being different: two branches could each carry the
    // other's phrase and pass every line above. This is the line that says the screen
    // really distinguishes four machines, and it is why the plan asked for it.
    int alike = 0;
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++)
            if (wcscmp(got[i].detail, got[j].detail) == 0)
                alike++;
    _snwprintf(what, 390,
               L"...and the four are four: %d of the 6 pairs say the same thing", alike);
    check(alike == 0, what);

    // ===================================================================
    // *** AND EACH NEEDLE IS IN ITS OWN TEXT AND IN NONE OF THE OTHER THREE, WHICH
    //     IS THE ASSERTION THIS SUITE WAS SHORT OF. ***
    //
    // The two checks above are each half of a discrimination and together they are
    // still not the whole of it. "case i contains needle i" is satisfied by four
    // texts that all contain all four needles; "the four texts differ" is satisfied
    // by two texts that differ in a comma and both carry needle 0. Neither implies
    // that a needle NAMES a case, and naming is the only thing these needles are for.
    //
    // The reviewer of the round that added this screen checked all twelve cross pairs
    // by hand and they hold today. That is his reading, not the suite's: it does not
    // survive one rewording, and a rewording is exactly what the later tasks do to
    // these sentences. It is the fourth consecutive round in which this hazard - a
    // substring found where nobody looked - produced a finding in this file.
    //
    // WHERE EACH SIDE COMES FROM: the haystacks are four sentences the product wrote
    // for four machines this suite invented; the needle is a phrase written in this
    // file. setup.cpp cannot see these literals and this function does not read its.
    // ===================================================================
    for (int i = 0; i < 4; i++) {
        int strays     = 0;
        int firstStray = -1;
        for (int j = 0; j < 4; j++) {
            if (j == i)
                continue;
            if (posOf(got[j].detail, cases[i].must) >= 0) {
                strays++;
                if (firstStray < 0)
                    firstStray = j;
            }
        }
        _snwprintf(what, 390,
                   L"...and \"%s\" names case %d and nobody else - %d of the other 3 "
                   L"texts carry it too (first such case: %d)",
                   cases[i].must, i, strays, firstStray);
        what[390] = 0;
        check(strays == 0, what);
    }

    // The mark is the severity, and the severity is the consequence: only the machine
    // whose mixer is answering gets a pass mark, and none of the other three gets a
    // red - the install goes ahead without the mixer, which is why Next is not
    // blocked here and exit code 3 exists instead.
    _snwprintf(what, 390,
               L"only the connected machine wears a pass mark, and no case is red "
               L"(marks %d %d %d %d)", (int)got[0].state, (int)got[1].state,
               (int)got[2].state, (int)got[3].state);
    check(got[0].state == bcdgui::kRowWarn && got[1].state == bcdgui::kRowWarn &&
          got[2].state == bcdgui::kRowWarn && got[3].state == bcdgui::kRowOk, what);

    // ===================================================================
    // *** AND WHEN IT IS MISSING, THE CABLE COMES BEFORE THE VIRTUAL MACHINE. ***
    //
    // Not a matter of taste. The loose cable was the real cause TWICE on this project
    // and both times the owner found it after the controller had blamed something
    // else - once the VMware arbitrator, once the code. Advice in the wrong order
    // costs somebody the same hour twice.
    //
    // THE NEEDLES: "reseat the USB cable" is the advice clause itself and appears
    // once in each of the two texts - it is not the word "cable", which also appears
    // in case 1's "a cable that works has been in it" and would have made this check
    // pass for a reason that is not the one it is about. "virtual machine" appears
    // once in each. BOTH indices are required to be found: an ordering test where a
    // missing needle scores -1 passes for the wrong reason, which is the arithmetic
    // this project's checks have got wrong before.
    // ===================================================================
    for (int i = 1; i <= 2; i++) {
        int cable = posOf(got[i].detail, L"reseat the USB cable");
        int vm    = posOf(got[i].detail, L"virtual machine");
        _snwprintf(what, 390,
                   L"case %d puts the cable before the virtual machine, because the "
                   L"cable was the real cause both times (cable at %d, virtual "
                   L"machine at %d)", i, cable, vm);
        what[390] = 0;
        check(cable >= 0 && vm >= 0 && cable < vm, what);
    }
}

// ===========================================================================
// PART 2e3b - THE DEVICE SCREEN, AND THE ONE CROSS-CHECK IN THIS FILE WHOSE TWO SIDES
//             ARE A COMPILED CONSTANT AND A SOURCE FILE THE PRODUCT NEVER LINKS
//
// *** WHY THIS SUITE READS native\bcdasio\usbdev.cpp AS TEXT. *** The installer names
// two mixers, with a vendor id, a product id and a "has this ever run on real hardware"
// flag each. The driver names the same two, in its own profile table. Those are two
// copies of one set of facts, and the installer cannot link the driver's copy: build.bat
// compiles five product units and not one of them includes anything from native\, so
// reaching DeviceProfile would mean adding units or include paths and moving the six
// units build.bat strict is pinned to.
//
// So the copies are checked against each other. The installer's side is a COMPILED
// CONSTANT - modelName(), modelVid(), modelPid(), modelProvenOnHardware(), which this
// translation unit really compiled from common.cpp. The driver's side is the BYTES OF
// ITS SOURCE, parsed here. Neither reads the other and the product links neither
// direction, which is what makes this a check rather than one value read twice. Reading
// source text in this harness is established: PART 4's freshness check reads every file
// the binary's words come from.
//
// *** AND THE FLAG IS TIED TO THE SCREEN'S WORDS, WHICH IS THE HALF THAT AGES. *** The
// screen hedges about one of the two models - "the control surface is not supported yet"
// - and the reason it may is that nobody has ever run that path. That reason lives in
// usbdev.cpp as provenOnHardware, and a sentence and a flag that nothing compares drift:
// the day somebody proves a BCD2000 on hardware and flips it, a screen still calling it
// unsupported would be a lie that no check in this repository could see. The check below
// is a SET EQUALITY over both models, so it fails in both directions - a hedge with no
// flag behind it, and a flag with no hedge in front of it.
//
// WHY THE PARSE IS POSITIONAL AND NOT BY NAME. Searching usbdev.cpp for "BCD2000" would
// find the driver's entry whatever position it sits in, and the installer's model
// INDICES are what selectedModel() returns and what the screen's two labels are ordered
// by - so agreement of order is part of what has to be true. The i-th profile in the
// driver's table is compared with the i-th model in the installer's, and that is why the
// count of profiles is checked first: index agreement is meaningless if the tables are
// different lengths.
// ===========================================================================

// Defined at global scope in PART 4, and forward declared rather than copied. That
// function handles a size limit and a partial read, and a second reader in this file
// would be a second place to get both right. It is the only thing shared across the two
// parts, which is why this is the only forward declaration here.
static BYTE* readWholeFile(const wchar_t* path, SIZE_T* sizeOut);

// One profile as the driver's SOURCE spells it.
struct DriverProfileText {
    char name[64];
    long vid;
    long pid;
    bool proven;
};

// *** THE PATH IS BUILT FROM THIS BINARY'S OWN LOCATION AND NOT FROM THE WORKING
//     DIRECTORY. *** PART 4's freshness check resolves "..\setup.cpp" from the cwd, and
// that is a documented footgun of this harness - it only works when it is run from
// installer\verify. A check that silently could not find its file would report safety,
// which is worse than no check, so this one cannot be moved by a cd: bcdverify.exe lives
// in installer\verify, so three directories up from it is the repository root whatever
// anybody's shell is pointing at.
static bool driverSourcePath(wchar_t* out, int cap)
{
    wchar_t exe[MAX_PATH];
    DWORD   n = GetModuleFileNameW(0, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return false;
    // Off the file name, then off "verify", then off "installer".
    for (int up = 0; up < 3; up++) {
        wchar_t* slash = wcsrchr(exe, L'\\');
        if (!slash)
            return false;
        *slash = 0;
    }
    _snwprintf(out, (size_t)cap - 1, L"%s\\native\\bcdasio\\usbdev.cpp", exe);
    out[cap - 1] = 0;
    return true;
}

static int countOfText(const char* hay, const char* needle)
{
    int n = 0;
    for (const char* at = strstr(hay, needle); at; at = strstr(at + 1, needle))
        n++;
    return n;
}

static const char* nthOfText(const char* hay, const char* needle, int index)
{
    const char* at = hay;
    for (int i = 0; ; i++) {
        at = strstr(at, needle);
        if (!at)
            return 0;
        if (i == index)
            return at;
        at++;
    }
}

// The next 0x.... at or after p, as a number, and where its digits stopped. -1 when
// there is none, which the caller treats as a parse failure rather than as a zero: a
// vendor id of 0 would compare unequal and look like a drift instead of like a harness
// that did not understand the file.
static long nextHexText(const char* p, const char** after)
{
    const char* at = strstr(p, "0x");
    if (!at)
        return -1;
    long        v      = 0;
    int         digits = 0;
    const char* d      = at + 2;
    for (; *d; d++) {
        int x;
        if (*d >= '0' && *d <= '9')
            x = *d - '0';
        else if (*d >= 'a' && *d <= 'f')
            x = *d - 'a' + 10;
        else if (*d >= 'A' && *d <= 'F')
            x = *d - 'A' + 10;
        else
            break;
        v = v * 16 + x;
        digits++;
    }
    if (after)
        *after = d;
    return digits > 0 ? v : -1;
}

// The index-th profile of the driver's table, out of its source text.
//
// EVERY ANCHOR HERE IS ONE THIS SUITE ALSO COUNTS. "// model" ends the line that opens a
// profile and appears exactly once per profile; the two ids are the first two 0x values
// after it; the flag is the first "provenOnHardware" after those. That last one is the
// only ordering that matters: the word also appears in prose ABOVE the second profile,
// and searching from the profile's own name rather than from the top of the file is what
// steps over it. A parse that read the comment would have got the right answer for the
// wrong reason, which is the shape this file has been bitten by three times.
static bool parseDriverProfile(const char* text, int index, DriverProfileText* out)
{
    ZeroMemory(out, sizeof(*out));
    const char* marker = nthOfText(text, "// model", index);
    if (!marker)
        return false;

    const char* lineStart = marker;
    while (lineStart > text && lineStart[-1] != '\n')
        lineStart--;
    const char* q1 = 0;
    for (const char* p = lineStart; p < marker; p++) {
        if (*p == '"') {
            q1 = p + 1;
            break;
        }
    }
    if (!q1)
        return false;
    const char* q2 = 0;
    for (const char* p = q1; p < marker; p++) {
        if (*p == '"') {
            q2 = p;
            break;
        }
    }
    if (!q2 || (SIZE_T)(q2 - q1) >= sizeof(out->name))
        return false;
    memcpy(out->name, q1, (SIZE_T)(q2 - q1));
    out->name[q2 - q1] = 0;

    const char* after = 0;
    out->vid = nextHexText(marker, &after);
    if (out->vid < 0)
        return false;
    out->pid = nextHexText(after, &after);
    if (out->pid < 0)
        return false;

    const char* flag = strstr(after, "provenOnHardware");
    if (!flag)
        return false;
    const char* fl = flag;
    while (fl > text && fl[-1] != '\n')
        fl--;
    bool sawTrue = false, sawFalse = false;
    for (const char* p = fl; p < flag; p++) {
        if (strncmp(p, "true", 4) == 0)
            sawTrue = true;
        if (strncmp(p, "false", 5) == 0)
            sawFalse = true;
    }
    // NEITHER OR BOTH IS A PARSE FAILURE AND NOT A false. A line this function has not
    // understood must not be reported as a model nobody has proven, because "not proven"
    // is exactly the answer the screen hedges on - the harness would then agree with a
    // screen for a reason that has nothing to do with the driver.
    if (sawTrue == sawFalse)
        return false;
    out->proven = sawTrue;
    return true;
}

// ===========================================================================
// *** THE DEVICE ROW, COMPOSED HERE FROM THE MACHINE STATE AND THE SELECTION - AND WHY
//     THIS SHAPE REPLACES FOUR NARROWER CHECKS INSTEAD OF JOINING THEM. ***
//
// THREE ROUNDS WENT INTO ONE GUARD, ONE INJECTION AT A TIME, AND EACH ROUND FOUND A NEW
// HOLE IN THE PATCH THE ROUND BEFORE HAD ADDED:
//
//   1. the original Critical - the satisfied branch claimed the detected model was the
//      selected one, and went on claiming it after the person clicked the other option.
//      Fixed by giving describeModel() the selection and a third branch.
//   2. the two nouns SWAPPED - "A BCD2000 answered ... set up for a BCD3000" on a machine
//      where the BCD3000 answered. False in both halves. 777 checks, 0 failures.
//      Fixed by asserting the ORDER of the two names.
//   3. the verb NEGATED with the order left correct - "A BCD3000 did NOT answer ... set up
//      for a BCD2000 regardless". False. 381 checks, 0 failures. Order is necessary and
//      not sufficient.
//   4. and one more, found while writing this and never tried by anybody: a FALSE CLAUSE
//      APPENDED TO THE AGREEING ROW - "...and that is what this installation is set up
//      for. The control surface is fully supported and the audio has been proven on this
//      unit." That is a flat lie about the path nobody has ever run, and it measured
//      402 checks, 0 failures. Nothing in this repository read the agreeing row's TEXT at
//      all: only its mark, and that it differed from the disagreeing row's.
//
// *** SO THE DEFECT IS THE SHAPE OF THE GUARD AND NOT THE FOURTH HOLE. *** This ledger
// already recorded this exact pattern under another name three tasks earlier - "the same
// prefix danger found ONE CHECK AT A TIME, in three consecutive rounds". A guard made of
// substring and position tests can only ever refuse the mutations somebody thought to
// injure; the sentence has an unbounded number of ways to become false, and each round
// bought exactly one of them.
//
// *** AND ROUND 3, WHICH REPLACED THE SUBSTRINGS WITH THIS EXACT COMPARISON, WAS BEATEN
//     TOO - BY FOUR MORE INJECTIONS, ONE OF THEM A MEASURED REGRESSION. *** The re-review
// named the root and it is not any one of the four holes. Three structural properties were
// exploitable, and the comment that used to stand here asserted the opposite of two of
// them:
//
//   1. THE EXPECTATION COVERED A STRICT SUBSET OF THE OBJECT. `bcdgui::Row` has three
//      fields - state, title, detail - and ExpectedRow composed two. `Row::title` is
//      painted in bold above the sentence (gui.cpp:1248) and, at gui.cpp:1509, it is the
//      field that decides whether the row is DRAWN AT ALL. A false heading measured
//      406/0/1 and 841/0/1; an EMPTIED heading removed the design's required sentence from
//      the shipped screen entirely and also measured 841/0/1. The old comment here said
//      "the whole row is compared, character for character" and "there is no mutation of
//      the text this does not see". Both were false, and the second was false in the
//      worst direction: the mutation it did not see is the one that deletes the row.
//   2. THE MATRIX BYPASSED THE PRODUCT'S OWN WIRING. It called describeModel() directly on
//      stack-size grounds, and the one check that did go through buildScreens() used the
//      DISAGREEING selection only. So "detected machine + agreeing selection + through the
//      table" had no coverage at all, and `describeModel(s, kModelBcd2000, &sc[2].row)` -
//      one token in buildScreens() - measured 406 checks 0 failures here while measuring
//      402/2 at the commit BEFORE round 3. Round 3 deleted two assertions that caught it
//      and justified the deletion, in a comment, with "every one of them is strictly
//      subsumed". *** THAT CLAIM WAS FALSE AND IT IS DELETED WITH THIS ROUND. *** Three of
//      the five were genuinely subsumed; two were not, and the comment would have told the
//      next maintainer not to restore them.
//   3. EVERY ASSERTION READ THE STRUCT AND NEVER THE PAGE. Nothing connected
//      describeModel()'s output to what drawRow() actually paints, so any defect living in
//      the gap between the two was invisible by construction.
//
// WHAT THIS IS INSTEAD, and it answers the three in order:
//
//   1. ExpectedRow carries ALL THREE of Row's fields. The heading is composed here like
//      the sentence and compared like the sentence, so a false heading and an emptied one
//      both fail.
//   2. EVERY case goes through the product's own buildScreens(), on a Run this suite
//      filled - all four of them, both selections on both machines. Nothing here calls
//      describeModel() directly any more. The stack objection that justified the direct
//      call is answered by allocating ONE Wizard and reusing it, rather than by declaring
//      four.
//   3. The same comparison is made AGAIN ON THE PAGE, in pixels - see
//      measureModelRowOnThePage() in the bcdgui block below. It renders the screen twice,
//      once from the row buildScreens() produced and once from the row this file composes,
//      and requires the two surfaces to be identical byte for byte; and it proves
//      separately that the renderer really reads each of the three fields, by perturbing
//      each one and requiring the page to change.
//
// *** IT IS BRITTLE AGAINST REWORDING, AND THAT IS THE POINT RATHER THAN THE COST. ***
// Changing the screen's words now means changing this function too, and a reader who has
// to edit the expected sentence is a reader who has been made to look at what it says. The
// alternative - a guard that survives rewording - is precisely what let three false
// sentences through.
//
// *** WHERE EACH SIDE COMES FROM, and this is the question that matters most for a golden
//     text check. *** The left is setup.cpp's rendered Row. The right is these format
// strings, written in installer\verify, in a different file the product does not compile
// against. Neither is derived from the other, and the BRANCH is chosen here from the
// inputs rather than copied from the product's answer - so a describeModel() that picked
// the wrong branch fails too, not only one that worded a branch wrongly.
//
// *** THE RESIDUAL, STATED AS IT IS AND NOT AS THE PREVIOUS ROUND STATED IT. *** The known
// weakness of any golden text is a CO-MUTATION: somebody changes the product and pastes
// the new string in here without reading it. Round 3 answered that with "the semantic
// needles on the neutral branch stay where they are... exact text and independent meaning,
// guarding each other". That accounting was wrong twice and the correction is the point of
// writing it down:
//
//   - THE NEEDLES EXISTED ON ONE BRANCH OF THREE. Round 3 removed every
//     independent-meaning assertion from BOTH detected branches, and then cited the ones
//     that survive on the neutral branch as the mutual guard. They are restored below, on
//     all three branches, and what they are for is said plainly: they are the CO-MUTATION
//     BACKSTOP and nothing else. Against a single-sided edit the exact comparison subsumes
//     every one of them.
//   - AND A NEEDLE CANNOT SEE AN APPEND. Keeping both neutral phrases and APPENDING
//     "Either model is fully supported by this driver, including its control surface" -
//     co-mutated into this file at the same time - measured 406 checks, 0 failures. A
//     substring test constrains the phrase it names and says nothing about what else the
//     sentence contains. So the backstop against an appended lie is not a needle at all:
//     it is forbiddenClaimIn() below, which scans every branch's row for the claims this
//     screen exists to prevent, on every machine, whatever else the sentence says.
//
// What honestly remains after all of that: a co-mutation that rewrites a sentence into a
// DIFFERENT false statement, in both files at once, in words forbiddenClaimIn() does not
// name, passes. That is the cost of a golden text and it is paid deliberately - the edit
// is required, in a second file, under this comment.
//
// *** AND THERE IS NO COMPILER FLOOR UNDER ANY OF THIS. *** Round 3's report claimed the
// original Critical was "uncompilable in every shape" because dropping the `selected`
// parameter raises C4100 under /W4 /WX. It is dodged by one line - `(void)selected;` -
// which compiles with zero warnings and is gui.cpp's own established idiom, used twice
// there for exactly this purpose. The checks still catch the dodged form; what does not
// exist is the floor beneath them. It is said here so that nobody reads a build flag as a
// second line of defence.
//
// The model names are interpolated rather than spelled, so this composition does not
// hard-code a name the driver's table might disagree with - and those names are themselves
// cross-checked against native\bcdasio\usbdev.cpp as text earlier in this same suite.
// ===========================================================================
struct ExpectedRow {
    bcdgui::RowState mark;
    wchar_t          title[bcdgui::kRowTitle];
    wchar_t          detail[bcdgui::kRowText];
};

static void expectedModelRow(const bcdsetup::MachineState* s, int selected,
                             ExpectedRow* out)
{
    ZeroMemory(out, sizeof(*out));
    const wchar_t* proven = bcdsetup::modelName(bcdsetup::kModelBcd3000);
    const wchar_t* chosen = bcdsetup::modelName(selected);
    if (!chosen)
        chosen = proven;

    // *** THE HEADING IS COMPOSED HERE TOO, AND IT IS THE FIELD THE PREVIOUS ROUNDS LEFT
    //     OUT. *** describeModel() writes the same heading on all three branches, so this
    // is one literal rather than three - but it is a literal in THIS file, which setup.cpp
    // cannot see, exactly like the sentences. An emptied heading is not a shorter string
    // here: it is a row that gui.cpp:1509 does not draw at all, which is why the page-side
    // guard exists as well as this one.
    wcsncpy(out->title, L"Which mixer", bcdgui::kRowTitle - 1);

    if (s->usb.interfacePresentNow) {
        if (selected == bcdsetup::kModelBcd3000) {
            out->mark = bcdgui::kRowOk;
            _snwprintf(out->detail, bcdgui::kRowText - 1,
                       L"A %s answered on this machine, and that is what this "
                       L"installation is set up for.", proven);
        } else {
            out->mark = bcdgui::kRowWarn;
            _snwprintf(out->detail, bcdgui::kRowText - 1,
                       L"A %s answered on this machine, but this installation is set up "
                       L"for a %s. That is allowed - change the choice below if it was "
                       L"not deliberate.", proven, chosen);
        }
    } else {
        // The neutral branch deliberately ignores the selection - it says the choice
        // STARTS on the proven model, which stays true whatever is chosen. Composing it
        // without `selected` is what pins that, and it is why two of the four cases below
        // expect the same sentence.
        out->mark = bcdgui::kRowNeutral;
        _snwprintf(out->detail, bcdgui::kRowText - 1,
                   L"Nothing was detected, so nothing below is a detection: the choice "
                   L"is yours. It starts on the %s because that is the only model this "
                   L"driver has been proven on, and not because one was found.", proven);
    }
    out->title[bcdgui::kRowTitle - 1] = 0;
    out->detail[bcdgui::kRowText - 1] = 0;
}

// ---------------------------------------------------------------------------
// *** THE CLAIMS THIS ROW MAY NEVER MAKE, ON ANY BRANCH, ABOUT ANY MACHINE - AND THIS IS
//     THE ONE GUARD IN THIS BLOCK THAT A CO-MUTATION DOES NOT CARRY WITH IT. ***
//
// Injection 4 and INJ7 are the same shape: a false clause APPENDED to a sentence that is
// otherwise correct. "...and that is what this installation is set up for. The control
// surface is fully supported and the audio has been proven on this unit." Both measured
// green against every substring needle this suite has ever carried, because a needle
// constrains the phrase it names and ignores everything else.
//
// What those clauses have in common is not their wording, it is their SUBJECT: they claim
// the thing the design says this screen exists to keep unsaid. Nobody on this project owns
// a BCD2000, usbdev.cpp:59 records that in words, and the driver's own profile table
// carries provenOnHardware = false for it. The screen's ROW is about which mixer answered
// and which one this run is set up for; it is never the place a claim about the control
// surface or about what has been proven belongs. The one label that IS allowed to hedge
// about the BCD2000 is Screen::choiceLabels, which is asserted separately and by name.
//
// So this scans the row - whatever else it says - for that subject. It is not a golden
// text, it survives every rewording, and it fires on an append that a needle cannot see.
//
// WHERE EACH SIDE COMES FROM: the haystack is a sentence describeModel() wrote for a
// machine this suite invented; the needles are phrases written in this file, which
// setup.cpp cannot see and does not contain.
// ---------------------------------------------------------------------------
static const wchar_t* forbiddenClaimIn(const wchar_t* detail)
{
    static const wchar_t* const kNever[] = {
        L"control surface is fully supported",
        L"control surface is supported",
        L"proven on this unit",
        L"fully supported"
    };
    if (!detail)
        return 0;
    for (int i = 0; i < (int)(sizeof(kNever) / sizeof(kNever[0])); i++)
        if (posOf(detail, kNever[i]) >= 0)
            return kNever[i];
    return 0;
}

// Where two strings first differ, or -1 when they are identical. The check messages carry
// the offset rather than the two sentences: both are about 200 characters and neither
// would survive a 460 count, so the strings are printed by wprintf and the verdict names
// the position.
static int firstDiffAt(const wchar_t* a, const wchar_t* b)
{
    if (!a || !b)
        return 0;
    int i = 0;
    while (a[i] && a[i] == b[i])
        i++;
    return (a[i] == b[i]) ? -1 : i;
}

static void testDeviceScreen(void)
{
    wprintf(L"\n--- the device screen, against the driver's own profile table ---\n");

    wchar_t what[500];

    // ---- the two sides of the cross-check ----
    wchar_t path[MAX_PATH];
    bool    haveP = driverSourcePath(path, MAX_PATH);
    SIZE_T  size  = 0;
    BYTE*   raw   = haveP ? readWholeFile(path, &size) : 0;
    char*   text  = 0;
    if (raw) {
        text = (char*)HeapAlloc(GetProcessHeap(), 0, size + 1);
        if (text) {
            memcpy(text, raw, size);
            text[size] = 0;
        }
        HeapFree(GetProcessHeap(), 0, raw);
    }
    _snwprintf(what, 460,
               L"the driver's profile table is READABLE as text, so the two model "
               L"tables can be compared at all: %s",
               haveP ? path : L"(the path could not be built)");
    what[460] = 0;
    // A FAILURE AND NOT A SKIP. A skip here would take every check below it with it and
    // leave the suite reading green, which is the exact shape an interrupted Task 6c
    // shipped: nine checks, zero call sites, a green suite.
    check(text != 0, what);

    if (text) {
        // *** THE COUNT FIRST, BECAUSE INDEX AGREEMENT IS MEANINGLESS WITHOUT IT. ***
        // The installer's kModelCount is what selectedModel() indexes and what orders the
        // screen's two labels. If the driver grew a third profile, comparing entry by
        // entry would still pass on the first two and the installer would be silently
        // one model short of the hardware the driver accepts.
        int profiles = countOfText(text, "// model");
        _snwprintf(what, 460,
                   L"the driver's table and the installer's name the same NUMBER of "
                   L"mixers - %d profiles in usbdev.cpp, %d models in common.cpp",
                   profiles, bcdsetup::kModelCount);
        check(profiles == bcdsetup::kModelCount, what);

        for (int i = 0; i < bcdsetup::kModelCount; i++) {
            DriverProfileText d;
            bool ok = parseDriverProfile(text, i, &d);
            _snwprintf(what, 460,
                       L"profile %d of the driver's table parses out of its source - "
                       L"name, vid, pid and provenOnHardware", i);
            check(ok, what);
            if (!ok)
                continue;

            // The name, and it is the check that makes the INDEX mean one thing in both
            // tables. Narrowed for the comparison because usbdev.cpp is a C++ file of
            // narrow literals and common.cpp holds wide ones; the widening is done here
            // so that both sides stay exactly what their own file says.
            wchar_t wide[64];
            for (int k = 0; k < 64; k++) {
                wide[k] = (wchar_t)(unsigned char)d.name[k];
                if (!d.name[k])
                    break;
            }
            wide[63] = 0;
            const wchar_t* mine = bcdsetup::modelName(i);
            _snwprintf(what, 460,
                       L"...model %d is the same mixer in both tables and in the same "
                       L"POSITION: the driver says \"%s\", the installer says \"%s\"",
                       i, wide, mine ? mine : L"(none)");
            what[460] = 0;
            check(mine != 0 && wcscmp(mine, wide) == 0, what);

            _snwprintf(what, 460,
                       L"...and its usb ids agree: the driver says %04lX:%04lX, the "
                       L"installer says %04X:%04X",
                       d.vid, d.pid, (unsigned)bcdsetup::modelVid(i),
                       (unsigned)bcdsetup::modelPid(i));
            check(d.vid == (long)bcdsetup::modelVid(i) &&
                  d.pid == (long)bcdsetup::modelPid(i), what);

            _snwprintf(what, 460,
                       L"...and whether anybody has ever RUN it agrees: the driver says "
                       L"provenOnHardware = %s, the installer says %s",
                       d.proven ? L"true" : L"false",
                       bcdsetup::modelProvenOnHardware(i) ? L"true" : L"false");
            check(d.proven == bcdsetup::modelProvenOnHardware(i), what);
        }
    }

    // ---- the screen itself, on a machine where a mixer answered ----
    bcdsetup::MachineState found;
    fakeState(&found);
    found.usb.interfacePresentNow = true;
    bcdgui::Wizard wf;
    ZeroMemory(&wf, sizeof(wf));
    bcdsetup::buildScreens(&wf, &found);

    // *** WHERE THE SCREEN IS FOUND: BY ITS TITLE AND NOT BY THE NUMBER 2. *** Every
    // index in this file moved by one when this screen was inserted, and an index written
    // here would move again for the next screen anybody adds. The table suite above is
    // where positions are asserted; this suite is about what the screen SAYS.
    int at = -1;
    for (int i = 0; i < bcdgui::screenCount(&wf); i++)
        if (wf.screens[i].title == ::kDeviceTitle)
            at = i;
    _snwprintf(what, 460, L"the flow has a screen about WHICH mixer (index %d)", at);
    check(at >= 0, what);
    if (at < 0) {
        if (text)
            HeapFree(GetProcessHeap(), 0, text);
        return;
    }

    // ===================================================================
    // *** WHAT THE TITLE SAYS, AND NOT MERELY THAT ITS POINTER IS ITS OWN CONSTANT. ***
    //
    // The table suite asserts `title == ::kDeviceTitle`, which is one value read twice: it
    // fails only if the assignment disappears. testInstallScreen() explicitly refuses that
    // shape for its own screen and asserts wcsstr(title, L"Install") instead, with the
    // reason written down - and this screen, whose title is also its capture key, had no
    // such check.
    //
    // *** IT USED TO QUOTE THE DESIGN'S WORDS AND IT DOES NOT ANY MORE, WHICH IS ITSELF
    //     ONE OF THIS PROJECT'S RECORDED DEFECTS. *** The message read "the design's table
    // says \"Which device\"". That was true when it was written; the controller has since
    // reconciled the design's flow table to this program's words, so the check's own failure
    // text asserted a fact the design no longer contained - a second copy of another
    // document's wording, living where nothing could keep it equal to the original. That is
    // exactly the drift this folder closes for the re-check button's label and the
    // teVirtualMIDI path, and it lasted one round.
    //
    // So this message no longer describes any other document. It quotes the one thing it can
    // keep true: the title constant this program really assigned. Where the design stands is
    // recorded once, at kDeviceTitle in setup.cpp, next to the words themselves - and if the
    // two diverge again, THAT comment is the single place it has to be said.
    //
    // The assertion is on the SUBJECT the title names, in the word this wizard has already
    // taught by the time anybody reaches this screen. A retitle that drops it fails here and
    // lands in a review, which is the point.
    //
    // WHERE EACH SIDE COMES FROM: the left is the string setup.cpp really assigned; the
    // right is two literals written in this file. setup.cpp cannot see them.
    // ===================================================================
    {
        const wchar_t* t = wf.screens[at].title;
        _snwprintf(what, 460,
                   L"the screen is titled for the subject it asks about, in the word this "
                   L"wizard has already taught: \"%s\" - asked of what the title SAYS, not "
                   L"of which constant it is",
                   t ? t : L"(none)");
        what[460] = 0;
        check(t != 0 && posOf(t, L"mixer") >= 0 && posOf(t, L"Which") >= 0, what);
    }

    // ===================================================================
    // *** THE HEDGE AND THE FLAG, AS A SET EQUALITY, WHICH IS WHAT MAKES IT FAIL IN
    //     BOTH DIRECTIONS. ***
    //
    // WHERE EACH SIDE COMES FROM: the left is provenOnHardware parsed out of
    // native\bcdasio\usbdev.cpp - a file this product does not compile and this suite
    // read off the disk - and the right is the words setup.cpp really put in the option
    // label the window really draws. The installer's own copy of the flag is
    // deliberately NOT the left hand side: modelProvenOnHardware() and the label are
    // both written in installer\, and a check between them would be this file agreeing
    // with itself.
    //
    // "not supported yet" is the design's own clause and it is searched in ONE haystack
    // - a single option label - so the substring hazard that has bitten this file three
    // times has one place to hide and the loop below closes it: every label is asked, so
    // the clause appearing on the WRONG one fails just as loudly as it missing from the
    // right one.
    // ===================================================================
    if (text) {
        for (int i = 0; i < bcdsetup::kModelCount && i < 2; i++) {
            DriverProfileText d;
            if (!parseDriverProfile(text, i, &d))
                continue;
            const wchar_t* label = wf.screens[at].choiceLabels[i];
            bool           hedged =
                label != 0 && posOf(label, L"not supported yet") >= 0;
            _snwprintf(what, 460,
                       L"the screen hedges about exactly the model the DRIVER'S SOURCE "
                       L"calls unproven - model %d (%hs): usbdev.cpp says proven=%s, the "
                       L"label says \"not supported yet\"=%s",
                       i, d.name, d.proven ? L"true" : L"false",
                       hedged ? L"true" : L"false");
            what[460] = 0;
            check(hedged == !d.proven, what);
        }

        // ===============================================================
        // ...and the same for the audio half of the design's sentence, on the same label,
        // because "the controls do not work" and "the audio should work" are two promises
        // and the design requires both. SHOULD and not WILL: nobody has run it.
        //
        // *** COUNTED RATHER THAN `continue`d, AND THAT IS A FIX. *** This loop used to
        // read `if (!parse(...) || d.proven) continue;` and run its check once - which
        // means that on a driver table where BOTH models were proven it ran ZERO times and
        // the suite's total silently dropped from 369 to 368. A review saw exactly that
        // while injecting the flag flip. A check that VANISHES under a defect reports
        // nothing while looking like it passed, and the Global Constraints warn that a
        // moved total hides a swap.
        //
        // So the shape is now a fixed number of checks whatever the driver says: count how
        // many models are unproven, assert that it is exactly one, and assert that the one
        // carries both required clauses. Two checks, always two, and the first of them is
        // the one that goes red if the driver's table ever stops having an unproven model
        // to hedge about.
        // ===============================================================
        {
            int unproven = 0, softest = 0, hedged = 0;
            for (int i = 0; i < bcdsetup::kModelCount && i < 2; i++) {
                DriverProfileText d;
                if (!parseDriverProfile(text, i, &d) || d.proven)
                    continue;
                unproven++;
                const wchar_t* label = wf.screens[at].choiceLabels[i];
                if (label && posOf(label, L"Audio should work") >= 0)
                    softest++;
                if (label && posOf(label, L"not supported yet") >= 0)
                    hedged++;
            }
            _snwprintf(what, 460,
                       L"exactly ONE of the driver's models is unproven (%d of %d), which "
                       L"is what the sentence below is about - and counting it means this "
                       L"check cannot stop existing instead of going red",
                       unproven, bcdsetup::kModelCount);
            check(unproven == 1, what);

            _snwprintf(what, 460,
                       L"...and that one model's label carries BOTH clauses the design "
                       L"requires - the audio SHOULD work rather than WILL (%d of %d) and "
                       L"the control surface is not supported yet (%d of %d)",
                       softest, unproven, hedged, unproven);
            check(unproven == 1 && softest == unproven && hedged == unproven, what);
        }
    }

    // ---- the option order, and it is not cosmetic ----
    _snwprintf(what, 460,
               L"the PROVEN model is offered first - option 0 names \"%s\" and option 1 "
               L"names \"%s\"",
               bcdsetup::modelName(0) ? bcdsetup::modelName(0) : L"(none)",
               bcdsetup::modelName(1) ? bcdsetup::modelName(1) : L"(none)");
    what[460] = 0;
    check(wf.screens[at].choiceLabels[0] != 0 && wf.screens[at].choiceLabels[1] != 0 &&
          posOf(wf.screens[at].choiceLabels[0], bcdsetup::modelName(0)) >= 0 &&
          posOf(wf.screens[at].choiceLabels[1], bcdsetup::modelName(1)) >= 0 &&
          bcdsetup::modelProvenOnHardware(0), what);

    // ---- the repository address, ON THIS SCREEN and not only in the banner ----
    //
    // *** WHY THIS IS ASKED OF THE SCREEN'S OWN BULLETS AND NOT OF capHas(). *** The
    // console banner already says kRepositoryUrl, so a capHas(L"github.com") would pass
    // on a screen that never mentioned it - a check satisfied by a line forty rows up a
    // log pane, which is precisely the "visible where it is needed" property this is
    // about. The haystack is this entry's own painted bullets and nothing else.
    //
    // WHERE EACH SIDE COMES FROM: the left is the buffer buildDeviceWords() filled; the
    // right is the constant gui.cpp holds, which is the ONE definition the opening page,
    // the console banner and installer\README.md also use. A bullet that stopped
    // interpolating it - or that spelled a fourth copy of the address - fails here.
    {
        bool sawUrl = false, sawHost = false;
        for (int i = 0; i < 4 && wf.screens[at].bullets[i]; i++) {
            if (posOf(wf.screens[at].bullets[i], bcdgui::kRepositoryUrl) >= 0)
                sawUrl = true;
            if (posOf(wf.screens[at].bullets[i], L"github.com/") >= 0)
                sawHost = true;
        }
        check(sawUrl,
              L"the repository address is PAINTED on this screen, interpolated from the "
              L"one definition the banner and the README also use - somebody told their "
              L"mixer is unsupported should not have to scroll a log pane for it");
        check(sawHost,
              L"...and it really is a repository address and not an empty buffer that "
              L"happened to compare equal to one");
    }

    // ===================================================================
    // *** A PRESELECTION PRESENTED AS A DETECTION, ON A MACHINE WHERE NOTHING WAS
    //     DETECTED, IS THE CLASS THIS PROJECT HAS REMOVED TWICE. ***
    //
    // The design requires this screen to SAY SO in those words when screen 1 found
    // nothing, and to leave the choice to the person with the BCD3000 as the default
    // because it is the only model validated on hardware. Both halves are checked, and
    // so is the thing that makes them a pair rather than two sentences: the row on the
    // machine that found something and the row on the machine that did not must DIFFER,
    // or the screen is not following the machine at all.
    //
    // WHERE EACH SIDE COMES FROM: the left is describeModel()'s sentence for a machine
    // this suite invented; the right is a phrase written in this file. setup.cpp cannot
    // see these literals.
    // ===================================================================
    bcdsetup::MachineState none;
    fakeState(&none);
    none.usb.enumKeyPresent      = false;
    none.usb.guidPresent         = false;
    none.usb.interfacePresentNow = false;
    bcdgui::Wizard wn;
    ZeroMemory(&wn, sizeof(wn));
    bcdsetup::buildScreens(&wn, &none);

    _snwprintf(what, 460,
               L"with nothing plugged in the screen SAYS nothing was detected rather "
               L"than implying a detection it did not make: \"%s\"",
               wn.screens[at].row.detail);
    what[460] = 0;
    check(posOf(wn.screens[at].row.detail, L"Nothing was detected") >= 0, what);

    check(posOf(wn.screens[at].row.detail, L"the choice is yours") >= 0,
          L"...and it hands the choice over in those words, which is the half that "
          L"stops the default reading as a finding");

    _snwprintf(what, 460,
               L"...and the default is still the only model proven on hardware - "
               L"option %d of %d, \"%s\"",
               wn.screens[at].choiceSelected, bcdsetup::kModelCount,
               bcdsetup::modelName(wn.screens[at].choiceSelected)
                   ? bcdsetup::modelName(wn.screens[at].choiceSelected)
                   : L"(none)");
    what[460] = 0;
    check(bcdsetup::modelProvenOnHardware(wn.screens[at].choiceSelected), what);

    // The two machines say different things, which is what says the row follows the
    // machine at all. Two texts that were identical would pass every needle above if the
    // needle happened to be in both.
    _snwprintf(what, 460,
               L"...and the two machines get two different rows, so this screen reads "
               L"the machine rather than printing a constant (%s)",
               wcscmp(wn.screens[at].row.detail, wf.screens[at].row.detail) == 0
                   ? L"they are identical" : L"they differ");
    what[460] = 0;
    check(wcscmp(wn.screens[at].row.detail, wf.screens[at].row.detail) != 0, what);

    // The mark, and it is NEUTRAL rather than amber on purpose: the screen one back has
    // already made the cable complaint in four different ways with the advice for each.
    // Nothing is wrong HERE, and an amber mark would be a second complaint about a
    // subject this screen does not own.
    _snwprintf(what, 460,
               L"...and it wears a NEUTRAL mark and not a warning (%d): the cable is the "
               L"screen before's subject and it makes that complaint four ways",
               (int)wn.screens[at].row.state);
    check(wn.screens[at].row.state == bcdgui::kRowNeutral, what);

    // ===================================================================
    // *** AND THE PRESS RECORDS SOMETHING, WHICH IS THE ONLY THING A CONTROL WITH NO
    //     EFFECT ON THE INSTALL CAN HONESTLY DO. ***
    //
    // Nothing this installer writes depends on the answer - the driver carries both
    // profiles and matches on the usb ids at run time - so if chooseModel() recorded
    // nothing, the two controls would be a question with no consequence anywhere, which
    // is the "declared and unread" shape this project has found eleven times. What it is
    // for is the log, and the log is what a support request carries.
    //
    // DRIVEN THROUGH THE PRODUCT'S OWN FUNCTION, on a Run this suite owns, with the line
    // sink pointed at this file's buffer - the same idiom every suite here uses to read
    // what the product said. capReset() afterwards because g_cap is shared and a later
    // suite would otherwise find these lines.
    // ===================================================================
    {
        ::Run r;
        ZeroMemory(&r, sizeof(r));
        capReset();
        bcdsetup::setLineSink(capSink);
        // *** AND THE CONSOLE ECHO OFF, WHICH THE FIRST DRAFT OF THIS BLOCK DID NOT DO.
        //     *** chooseModel() ends with a sayWarn(), and say*() writes to the console as
        // well as to the sink. Redirected, that output interleaves mid-line with this
        // file's own wprintf - the documented artefact - and the normalised NAME diff for
        // this round caught it doing exactly that: a check name in this suite came back
        // ending "...rather than print[warn] This i". PART 1 has silenced the echo around
        // its printSummary() calls since long before this round; this block needed the
        // same and had not found the mechanism.
        bcdsetup::setConsoleEcho(false);

        int okBad   = bcdsetup::chooseModel(&r, bcdsetup::kModelCount);
        int okFirst = bcdsetup::chooseModel(&r, bcdsetup::kModelBcd2000);
        int stored  = r.selectedModel;
        int lines   = g_capN;
        int okAgain = bcdsetup::chooseModel(&r, bcdsetup::kModelBcd2000);
        int linesAgain = g_capN;
        bool saidIt = capHas(L"EXPERIMENTAL");
        bool saidWhich = capHas(bcdsetup::modelName(bcdsetup::kModelBcd2000));

        bcdsetup::setLineSink(0);
        bcdsetup::setConsoleEcho(true);

        _snwprintf(what, 460,
                   L"an index the model table does not have is REFUSED rather than "
                   L"stored (returned %d, wanted 0)", okBad);
        check(okBad == 0, what);

        _snwprintf(what, 460,
                   L"...a real choice is accepted and RECORDED in the run (returned %d, "
                   L"stored %d, wanted %d)", okFirst, stored,
                   bcdsetup::kModelBcd2000);
        check(okFirst != 0 && stored == bcdsetup::kModelBcd2000, what);

        check(saidIt && saidWhich,
              L"...and it is SAID, naming the mixer and the word EXPERIMENTAL, so the "
              L"console, the log file and a support request carry which mixer this run "
              L"was told it was for");

        // *** IDEMPOTENT, AND THAT IS ABOUT THE LOG AND NOT ABOUT TIDINESS. ***
        // syncChoiceButtons() copies the table down into the controls after every
        // re-check, and gui.cpp calls chooseModel() on every BN_CLICKED. Without this,
        // pressing the option that is already on would write the whole EXPERIMENTAL
        // paragraph again, and a log that repeats a warning is a log somebody stops
        // reading.
        _snwprintf(what, 460,
                   L"...and choosing the SAME model again says nothing a second time "
                   L"(%d lines after the first press, %d after the second)",
                   lines, linesAgain);
        check(okAgain != 0 && linesAgain == lines && lines > 0, what);

        capReset();
    }

    // ===================================================================
    // *** FOUR RUNS THAT REALLY CHOSE SOMETHING, AND THIS IS WHY THE WORST DEFECT OF AN
    //     EARLIER ROUND WAS INVISIBLE. ***
    //
    // Every Wizard above this line, and every Wizard in shootAtDpi(), has either a null
    // `user` or a zeroed Run - so `sc[2].choiceSelected = run ? run->selectedModel : ...`
    // ALWAYS produced 0, and every check about the selection compared 0 with 0. Three
    // separate things hid behind that:
    //
    //   - the round trip was untested. Replacing the whole ternary with the constant
    //     kModelBcd3000 - deleting the exact line whose own comment says "THE HALF A TEST
    //     WOULD MISS" - passed all 760 checks. So did replacing syncChoiceButtons()'s
    //     `s->choiceSelected == i` with `i == 0`.
    //   - describeModel()'s satisfied branch said "A BCD3000 answered on this machine, SO
    //     THAT IS WHAT IS SELECTED BELOW. Nothing here has to be changed", with a PASS
    //     mark, and went on saying it after the person clicked the BCD2000. That is the
    //     design's central requirement inverted - a selection presented as the detection -
    //     on the screen written to prevent it, reachable in one click, and green.
    //   - bcdsetup::selectedModel() had no caller and no check at all.
    //
    // One arrangement closes all three: a real Run, in `user`, with a real selection, on a
    // machine that really answered. Round 2 built exactly ONE of those - the disagreeing
    // one - and the mirror defect walked through the hole that left. It is four now, which
    // is every combination of the two machines and the two selections.
    //
    // WHERE EACH SIDE COMES FROM: the left is the row and the field the product's own
    // buildScreens() produced from a Run this suite filled in; the right is the selection
    // this suite chose and phrases written in this file. buildScreens() cannot see these
    // literals and this block does not read its.
    // ===================================================================
    // ===================================================================
    // *** EVERY BRANCH, THROUGH THE PRODUCT'S OWN TABLE, AND THAT IS THE CHANGE. ***
    //
    // Round 3 had two blocks here: one wiring case through buildScreens() using the
    // DISAGREEING selection, and a four case matrix that called describeModel() DIRECTLY.
    // The re-review's second structural finding is exactly the gap between them - "detected
    // machine + agreeing selection + through the table" was covered by nothing, and
    // `describeModel(s, kModelBcd2000, &sc[2].row)` inside buildScreens() measured 406
    // checks and 0 failures while measuring 402 and 2 at the commit before. On the default
    // machine that defect paints an amber row saying the run is set up for a BCD2000 with
    // the BCD3000 radio visibly selected: the original Critical, in the mirror.
    //
    // So there is one block and every case goes through buildScreens(). The chain asserted
    // is RUN -> TABLE -> describeModel() -> ROW, four times, on both selections and both
    // machines.
    //
    // *** THE STACK OBJECTION IS ANSWERED RATHER THAN OBEYED. *** The old comment gave "four
    // Wizards would be about 180 KB of stack" as the reason for bypassing the wiring. One
    // Wizard, on the heap, reused across the four cases, costs one allocation and removes
    // the reason.
    //
    // *** THE FOURTH CASE IS NOT REDUNDANT. *** Cases 2 and 3 expect the SAME sentence,
    // because the neutral branch ignores the selection - it says the choice STARTS on the
    // proven model, which stays true whatever was chosen. Running both is what pins that
    // property: a neutral branch that began interpolating the selection would fail case 3
    // while case 2 stayed green.
    //
    // WHERE EACH SIDE COMES FROM: the left is the Row the product's own buildScreens() put
    // in the table, from a Run this file filled in; the right is the whole row - mark,
    // heading and sentence - composed by expectedModelRow() in installer\verify from the
    // same two inputs, in a file the product does not compile against. The BRANCH is chosen
    // on this side from the inputs and never copied from the product's answer.
    // ===================================================================
    bcdgui::Row branchRow[4];
    ZeroMemory(branchRow, sizeof(branchRow));
    {
        struct RowCase {
            bool           present;
            int            selected;
            const wchar_t* label;
        };
        static const RowCase rows[4] = {
            { true,  bcdsetup::kModelBcd3000, L"detected, and that model chosen" },
            { true,  bcdsetup::kModelBcd2000, L"detected, and the OTHER model chosen" },
            { false, bcdsetup::kModelBcd3000, L"nothing detected, default kept" },
            { false, bcdsetup::kModelBcd2000, L"nothing detected, other model chosen" }
        };
        bcdgui::Wizard* w =
            (bcdgui::Wizard*)HeapAlloc(GetProcessHeap(), 0, sizeof(bcdgui::Wizard));
        check(w != 0, L"the four branch machines have a wizard to be built into - one "
                      L"allocation, reused, which is what lets every case go through "
                      L"buildScreens() instead of around it");
        for (int i = 0; w && i < 4; i++) {
            ::Run r;
            ZeroMemory(&r, sizeof(r));
            fakeState(&r.state);
            r.state.usb.interfacePresentNow = rows[i].present;
            r.selectedModel                 = rows[i].selected;

            ZeroMemory(w, sizeof(*w));
            w->user = &r;
            bcdsetup::buildScreens(w, &r.state);

            int k = -1;
            for (int j = 0; j < bcdgui::screenCount(w); j++)
                if (w->screens[j].title == ::kDeviceTitle)
                    k = j;
            _snwprintf(what, 460,
                       L"[%s]: the flow this machine builds still has the mixer screen "
                       L"(index %d)", rows[i].label, k);
            what[460] = 0;
            check(k >= 0, what);
            if (k < 0)
                continue;

            branchRow[i] = w->screens[k].row;

            // --- the round trip: the value this file put in the RUN is the value the
            //     TABLE ended up with ---
            //
            // Asked of all four and not only of the BCD2000 runs. On its own, a case whose
            // selection is kModelBcd3000 compares 0 with 0 and cannot tell the field from
            // the fallback - which is why round 2 introduced this check at kModelBcd2000
            // and said so. As a SET of four the discrimination is complete: a
            // choiceSelected hard-coded to 0 fails cases 1 and 3, and one hard-coded to 1
            // fails cases 0 and 2.
            _snwprintf(what, 460,
                       L"[%s]: the table carries the RUN's selection and not the fallback "
                       L"- the run chose %d and the entry says %d",
                       rows[i].label, rows[i].selected, w->screens[k].choiceSelected);
            what[460] = 0;
            check(w->screens[k].choiceSelected == rows[i].selected, what);

            // --- the whole row, character for character, and now it really is the whole
            //     row: mark, heading and sentence ---
            ExpectedRow want;
            expectedModelRow(&r.state, r.selectedModel, &want);
            int dt = firstDiffAt(w->screens[k].row.title,  want.title);
            int dd = firstDiffAt(w->screens[k].row.detail, want.detail);
            wprintf(L"  [%s]\n    head : \"%s\"\n    row  : %s\n    want : %s\n",
                    rows[i].label, w->screens[k].row.title, w->screens[k].row.detail,
                    want.detail);
            _snwprintf(what, 460,
                       L"[%s]: the row the TABLE ends up with is EXACTLY the row this file "
                       L"composes from the machine and the selection - heading differs at "
                       L"%d, sentence at %d (-1 is identical), mark %d against %d. All "
                       L"THREE fields, because the heading is what gui.cpp decides to draw "
                       L"a row on at all",
                       rows[i].label, dt, dd, (int)w->screens[k].row.state,
                       (int)want.mark);
            what[460] = 0;
            check(dt < 0 && dd < 0 && w->screens[k].row.state == want.mark, what);
        }
        if (w)
            HeapFree(GetProcessHeap(), 0, w);
    }

    // ===================================================================
    // *** AND THE INDEPENDENT MEANING, RESTORED ON THE TWO DETECTED BRANCHES AND SAID TO BE
    //     WHAT IT IS. ***
    //
    // Round 3 deleted every meaning assertion from both detected branches on the ground
    // that each was "strictly subsumed" by the exact comparison, and then cited the
    // meaning assertions that survive on the NEUTRAL branch as the co-mutation defence.
    // Three of the five deletions were sound. Two were not, and are re-closed by the loop
    // above going through buildScreens(). These three are the remaining ones, and they are
    // back with their real justification written down instead of a false one:
    //
    // AGAINST A SINGLE SIDED EDIT THEY ARE SUBSUMED, AND THAT IS NOT A REASON TO DROP THEM.
    // Their whole value is the case the exact comparison cannot see - somebody editing
    // setup.cpp and pasting the new sentence into expectedModelRow() without reading it.
    // A co-mutation carries the golden text with it and does not carry these.
    //
    // WHERE EACH SIDE COMES FROM: the haystacks are the rows the product's own
    // buildScreens() produced, captured by the loop above; the needles are the model names
    // out of the installer's own table, which are themselves pinned against
    // native\bcdasio\usbdev.cpp as text earlier in this suite. Nothing here is read back
    // from describeModel().
    // ===================================================================
    {
        const wchar_t* proven = bcdsetup::modelName(bcdsetup::kModelBcd3000);
        const wchar_t* other  = bcdsetup::modelName(bcdsetup::kModelBcd2000);
        int pAgree  = (proven && other) ? posOf(branchRow[0].detail, proven) : -1;
        int oAgree  = (proven && other) ? posOf(branchRow[0].detail, other)  : 0;
        int pWarn   = (proven && other) ? posOf(branchRow[1].detail, proven) : -1;
        int oWarn   = (proven && other) ? posOf(branchRow[1].detail, other)  : -1;

        _snwprintf(what, 460,
                   L"the AGREEING row names the model that answered and does not name the "
                   L"other one - \"%s\" at %d, \"%s\" at %d - so a green row cannot be "
                   L"about a mixer nobody found",
                   proven ? proven : L"(none)", pAgree, other ? other : L"(none)", oAgree);
        what[460] = 0;
        check(proven != 0 && other != 0 && pAgree >= 0 && oAgree < 0, what);

        _snwprintf(what, 460,
                   L"...and the DISAGREEING row names both, in found-then-chosen ORDER - "
                   L"\"%s\" at %d before \"%s\" at %d - which is the half the two nouns "
                   L"swapped got past",
                   proven ? proven : L"(none)", pWarn, other ? other : L"(none)", oWarn);
        what[460] = 0;
        check(pWarn >= 0 && oWarn >= 0 && pWarn < oWarn, what);

        // Only agreement may wear a pass mark, counted over all four branches rather than
        // asserted one at a time: a mark table that turned everything green passes four
        // separate "is this one right" checks only by failing all four, and this says the
        // shape of the whole thing in one number.
        int passes = 0;
        for (int i = 0; i < 4; i++)
            if (branchRow[i].state == bcdgui::kRowOk)
                passes++;
        _snwprintf(what, 460,
                   L"...and exactly ONE of the four machines wears a pass mark (%d of 4): "
                   L"the one where the detection and the selection agree, which is the "
                   L"whole subject of this screen",
                   passes);
        what[460] = 0;
        check(passes == 1 && branchRow[0].state == bcdgui::kRowOk, what);

        // *** AND THE CLAIM NO BRANCH MAY MAKE, WHICH IS THE ONE GUARD AN APPEND CANNOT
        //     WALK PAST. *** See forbiddenClaimIn(). Injection 4 and INJ7 both appended a
        // false clause to a sentence that was otherwise correct and both measured green
        // against every needle this suite has carried; a needle constrains the phrase it
        // names and ignores the rest of the sentence. This does not name a phrase the row
        // must contain - it names the SUBJECT the row may never raise, on any branch.
        const wchar_t* bad = 0;
        for (int i = 0; i < 4; i++)
            if (!bad)
                bad = forbiddenClaimIn(branchRow[i].detail);
        _snwprintf(what, 460,
                   L"none of the 4 rows claims the control surface is supported or that "
                   L"anything has been proven on this unit - 4 phrases looked for in each"
                   L"%s%s%s. Nobody on this project owns a BCD2000 and this row is not "
                   L"where that would be said",
                   bad ? L", found \"" : L"", bad ? bad : L"", bad ? L"\"" : L"");
        what[460] = 0;
        check(bad == 0, what);
    }

    // *** WHERE THE OTHER HALF OF THIS SCREEN'S RECORD IS ASSERTED, AND WHY IT IS NOT
    //     HERE. *** chooseModel() writing the choice into the Run is asserted above. The
    // choice being READ BACK - by bcdsetup::selectedModel(), inside printSummary(), which
    // is the one place a support request reads it - is PART 1 BRANCH F. It lives there
    // because it is a branch of printSummary()'s text, and because driving printSummary()
    // from this suite dumped about 100 lines of product output into the middle of PART
    // 2e3b and split a check name across 208 lines of it. See the block over branch F.

    if (text)
        HeapFree(GetProcessHeap(), 0, text);
}

// ===========================================================================
// PART 2e4 USED TO BE THE MIDI PORT SCREEN'S SUBJECT, ITS PANE, AND THE ONE BUTTON
//          THIS INSTALLER OFFERED TO PRESS.
//
// *** THIS TASK REMOVES AND DOES NOT ADD. *** The fallback ladder this section used
// to test against the screen's wiring (chooseLoopMidiOffer(), the address block, the
// three-state gate) is gone along with the third party detection it was built on.
// testTeVmReadings() below is repurposed to testServiceIsRegistered(), which keeps
// the two checks on the one function this task's judgement call left in place - see
// the block over serviceIsRegistered() in common.h. testMidiPortScreen() is cut down
// to what the minimal screen actually has: a title, one bullet, a neutral row, no
// button and no address.
// ===========================================================================

// ===========================================================================
// PART 2e5 - THE BINDING SCREEN: ITS FOUR ANSWERS, AND THE NAMED DOOR
//
// *** THIS IS THE SCREEN THE OWNER'S COMPLAINT WAS ACTUALLY ABOUT, AND IT IS ALSO THE
//     ONLY SCREEN IN EITHER FLOW THAT REFUSES TO BE LEFT. *** Both halves are here:
// the sentence the screen says about the machine, and the three properties of the
// door that make blocking defensible at all.
//
// WHERE EACH SIDE COMES FROM: the haystacks are sentences the product wrote for
// machines this suite invented; the needles are phrases written in this file, which
// setup.cpp cannot see. The flows are built by the product's own buildScreens() and
// the verdicts are read out of the product's own nextAllowed() - never out of the
// field those verdicts are computed from.
// ===========================================================================
static void testBindingScreen()
{
    wprintf(L"\n--- the binding screen, and the named door ---\n");

    wchar_t what[400];

    // -------------------------------------------------------------------
    // FIVE ANSWERS, and the suite used to drive four.
    //
    // The first three are three for the reason describeCable()'s are: "never seen" and
    // "seen and not bound" are different machines, and "Zadig was run on the wrong line
    // of its list" is a different machine again from "Zadig has not been run". Telling
    // the third person to run Zadig once is telling them to redo the thing they have
    // just done, on the one step that can leave the hardware unusable.
    //
    // *** CASE 3 IS THE ONE THIS SUITE NEVER REACHED, AND IT IS THE ONLY AMBER. ***
    // describeBinding() has always had five branches; this table had four rows, and the
    // missing one was "applied, but the mixer does not respond" - which is the machine
    // of anybody who installs with the mixer unplugged, not an exotic branch. Nothing
    // above could have caught a defect in it: the pairwise and stray-needle checks
    // compare the texts this table PRODUCES, and no comparison between four texts can
    // fail on a fifth that was never asked for.
    //
    // It is placed before "applied and confirmed" so that the two `guid == true` rows
    // sit together and differ only in `present`, which is the single field that tells
    // them apart.
    // -------------------------------------------------------------------
    struct BindCase {
        bool             enumKey;
        bool             guid;
        int              other;
        bool             present;
        const wchar_t*   must;
        bcdgui::RowState mark;
        // Whether this reading leaves Screen::satisfied false, and with it Next grey.
        // It is the same expression buildScreens() assigns from - s->usb.guidPresent -
        // and it is written per row rather than derived so that a row can be added
        // without the answer being inferred from a field somebody else set.
        bool             refusesNext;
    };
    static const BindCase cases[5] = {
        { false, false, -1, false, L"has never seen",   bcdgui::kRowFail, true  },
        { true,  false,  1, false, L"Interface 1",      bcdgui::kRowFail, true  },
        { true,  false, -1, false, L"Not applied yet",  bcdgui::kRowFail, true  },
        { true,  true,  -1, false, L"is not answering", bcdgui::kRowWarn, false },
        { true,  true,  -1, true,  L"confirmed on the device", bcdgui::kRowOk, false }
    };
    const int kBindCases = 5;

    bcdgui::Row got[5];
    for (int i = 0; i < kBindCases; i++) {
        bcdsetup::MachineState st;
        fakeState(&st);
        st.usb.enumKeyPresent      = cases[i].enumKey;
        st.usb.guidPresent         = cases[i].guid;
        st.usb.guidOnOtherFunction = cases[i].other;
        st.usb.interfacePresentNow = cases[i].present;
        ZeroMemory(&got[i], sizeof(got[i]));
        bcdsetup::describeBinding(&st, &got[i]);
        wprintf(L"  usb(%d,%d,other=%d,%d) -> [%d] %s\n", cases[i].enumKey ? 1 : 0,
                cases[i].guid ? 1 : 0, cases[i].other, cases[i].present ? 1 : 0,
                (int)got[i].state, got[i].detail);
        _snwprintf(what, 390,
                   L"the binding screen tells case %d apart by name - it has to say "
                   L"\"%s\" and it says \"%s\"", i, cases[i].must, got[i].detail);
        what[390] = 0;
        check(posOf(got[i].detail, cases[i].must) >= 0, what);
    }

    // ===================================================================
    // *** THE ONE SCREEN THAT REFUSES TO BE LEFT NAMES THE CONTROL THAT UNDOES THE
    //     REFUSAL. ***
    //
    // On a machine with no WinUSB binding this screen greys Next, paints no foot note
    // at all - footNote() is gated on the press that STARTS the work, which is another
    // screen, so not even /preview puts one here - and offers exactly one labelled
    // alternative: the door, which is the CLAIM. The honest path is the button in the
    // same band, and nothing on the screen said so. A greyed button beside silence,
    // with an escape hatch next to it, is the shape of the defect this whole redesign
    // was started against.
    //
    // WHERE EACH SIDE COMES FROM, AND WHAT THIS CAN AND CANNOT FAIL ON - said exactly,
    // because the first draft of this block claimed something that is not true.
    //
    // The haystack is a sentence describeBinding() wrote in setup.cpp for a machine
    // this suite invented. The needle is bcdgui::kRecheckLabel, the string gui.cpp
    // hands to CreateWindowExW when it makes the control - and the sentence does not
    // spell those words either: it INTERPOLATES the same constant.
    //
    // So this does NOT catch a rename, and it must not: renaming the button carries the
    // row with it, which is the whole reason the constant exists and is the outcome
    // wanted. A needle spelled out here instead would go red on a rename that was
    // entirely correct, which is a check that cries wolf.
    //
    // What it catches is the defect that was actually there: prose on the screen that
    // refuses to be left which does not refer to the control that undoes the refusal.
    // Rewrite the sentence without the interpolation - which is exactly how it stood
    // before this round - and this goes red naming the case and quoting the row.
    //
    // It is asked ONLY of the readings that refuse, and asked of every one of them: a
    // machine that is bound is not being refused anything and has no refusal to undo.
    // ===================================================================
    for (int i = 0; i < kBindCases; i++) {
        if (!cases[i].refusesNext)
            continue;
        _snwprintf(what, 390,
                   L"...and case %d refuses Next, so it names the control that undoes "
                   L"the refusal (\"%s\"): \"%s\"", i, bcdgui::kRecheckLabel,
                   got[i].detail);
        what[390] = 0;
        check(posOf(got[i].detail, bcdgui::kRecheckLabel) >= 0, what);
    }

    int alike = 0;
    for (int i = 0; i < kBindCases; i++)
        for (int j = i + 1; j < kBindCases; j++)
            if (wcscmp(got[i].detail, got[j].detail) == 0)
                alike++;
    _snwprintf(what, 390,
               L"...and the five are five: %d of the 10 pairs say the same thing",
               alike);
    check(alike == 0, what);

    // *** AND EACH NEEDLE IS IN ITS OWN TEXT AND ABSENT FROM THE OTHER THREE. ***
    // Needle-in-own-text plus pairwise-difference does not imply discrimination, and
    // this is the fifth consecutive round in which that gap has produced a finding in
    // this file - most recently against a plausible rewording that stole another
    // case's needle while both older checks stayed green.
    for (int i = 0; i < kBindCases; i++) {
        int strays     = 0;
        int firstStray = -1;
        for (int j = 0; j < kBindCases; j++) {
            if (j == i)
                continue;
            if (posOf(got[j].detail, cases[i].must) >= 0) {
                strays++;
                if (firstStray < 0)
                    firstStray = j;
            }
        }
        _snwprintf(what, 390,
                   L"...and \"%s\" names case %d and nobody else - %d of the other 4 "
                   L"texts carry it too (first such case: %d)",
                   cases[i].must, i, strays, firstStray);
        what[390] = 0;
        check(strays == 0, what);
    }

    // *** THREE OF THE FIVE ARE RED AND EXACTLY ONE IS AMBER, AND THAT IS THE POINT OF
    //     THIS SCREEN. *** The MIDI port screen has no red case at all, because without
    // it the audio works. Here nothing works, and the mark says so on every machine
    // that is not bound. The amber is the machine that IS bound and did not answer:
    // real progress, nothing to redo, and a reading that could not be confirmed.
    {
        int wrong = 0;
        for (int i = 0; i < kBindCases; i++)
            if (got[i].state != cases[i].mark)
                wrong++;
        _snwprintf(what, 390,
                   L"every unbound machine wears a RED mark and the one that is bound "
                   L"but silent wears an AMBER - severity follows consequence (marks "
                   L"%d %d %d %d %d, %d of 5 wrong)",
                   (int)got[0].state, (int)got[1].state, (int)got[2].state,
                   (int)got[3].state, (int)got[4].state, wrong);
        check(wrong == 0, what);
    }

    // *** THE WRONG-LINE SENTENCE NAMES THE INTERFACE THAT HAS TO BE BOUND AS WELL AS
    //     THE ONE THAT IS, AND THAT HALF WAS ASSERTED ON A ROW THAT NO LONGER EXISTS.
    //     ***
    // The install screen carried a WinUSB row saying "interface %d ... and it has to
    // be interface 0", and two checks over there held both halves of it. The row went
    // when that screen stopped repeating this one, and only the "Interface 1" half was
    // held here. The other half is the one that matters most: this is the person who
    // has already run Zadig once, and a sentence that names what they picked without
    // naming what to pick instead tells them to do again what they have just done.
    //
    // WHERE EACH SIDE COMES FROM: the haystack is case 1's sentence, written by
    // describeBinding() for a machine this suite invented; the needle is a literal on
    // this line. Note it is "not 0" and not "0" - the guid, the USB ID and the
    // interface number would all match a bare digit.
    check(posOf(got[1].detail, L"not 0") >= 0,
          L"...and the wrong-line sentence names the interface that HAS to be bound, "
          L"not only the one that is - the reader has already run Zadig once");

    // *** A ZEROED STATE MUST NOT INVENT A WRONG BINDING. *** guidOnOtherFunction is
    // 0 in every ZeroMemory'd MachineState in this tree, and read as an interface
    // number 0 says "interface 0 is bound" - which guidPresent being false has
    // already denied. Same arithmetic fillPreflightRows() uses, asserted separately
    // because this is a separate function that could get it wrong on its own.
    {
        bcdsetup::MachineState zeroed;
        ZeroMemory(&zeroed, sizeof(zeroed));
        zeroed.usb.enumKeyPresent = true;
        bcdgui::Row r;
        ZeroMemory(&r, sizeof(r));
        bcdsetup::describeBinding(&zeroed, &r);
        check(posOf(r.detail, L"Interface") < 0 && posOf(r.detail, L"Not applied") >= 0,
              L"a ZEROED state - which is what every ZeroMemory in this tree produces "
              L"- claims no wrong binding at all");
    }

    // -------------------------------------------------------------------
    // *** THE ROW IS SHORT, AND THAT IS ARITHMETIC RATHER THAN STYLE. ***
    //
    // It is drawn ABOVE a 254 logical pixel picture on a 398 pixel strip, in a column
    // of 604 pixels at 96 DPI (page 683, minus 2 x kMargin 26, minus the mark and its
    // gap, 27). A line of g_fSmall in that column is 13 pixels at 96 DPI and 23 at 144.
    // The picture clears the fold by 7 and by 4. So ONE wrapped line is the whole
    // budget: a row whose detail takes two puts the last rows of the picture behind a
    // scroll, which is the defect this screen exists to remove.
    //
    // (Those two numbers used to read "about 17" and "a 633 pixel column". 17 is
    // g_fBodyBold, the row TITLE, and 633 is not a width anything on this page has.
    // Neither was load bearing, and both were wrong.)
    //
    // *** AND THIS CHECK NO LONGER CLAIMS THE PIXEL PROPERTY, BECAUSE IT NEVER BOUNDED
    //     IT. *** It was named "stays short enough to keep the picture above the fold"
    // with a bound of 200 characters, and its own comment conceded 200 is about TWO
    // wrapped lines - so it permitted, by design, the exact state its name said it
    // prevented. Two of the five sentences sat at 139 and 138 and passed it while the
    // picture really ended 6 pixels past the fold at 96 DPI and 19 at 144.
    //
    // A count of characters cannot bound a count of pixels in either direction: the
    // same 116 characters fit in one line of narrow glyphs and not in one line of wide
    // ones. So the PROPERTY is measured where it lives - measureShotOverReadings()
    // renders this screen once per reading, at both DPIs, and asks the layout where the
    // picture ended. What survives here is what a character count really is: a cheap
    // budget an author editing setup.cpp meets in the text suite, set at the ONE LINE
    // limit measured at 96 DPI rather than at two.
    //
    // WHERE EACH SIDE COMES FROM: the left is the length of a sentence the product
    // wrote; the right is a bound written in this file. 116 is measured - case 0 at
    // 103 characters occupies 533 of the 604 pixel column, so the line holds about 13
    // more.
    // -------------------------------------------------------------------
    const int kOneLineChars = 116;
    for (int i = 0; i < kBindCases; i++) {
        _snwprintf(what, 390,
                   L"...and case %d's sentence stays inside the ONE line this row gets "
                   L"- %d characters, at most %d measured at 96 DPI. Whether the "
                   L"picture really clears the fold is asked in pixels, per reading, by "
                   L"measureShotOverReadings()",
                   i, (int)wcslen(got[i].detail), kOneLineChars);
        what[390] = 0;
        check((int)wcslen(got[i].detail) <= kOneLineChars, what);
    }

    // ===================================================================
    // *** THE NAMED DOOR, AND SECTION 4.2 ASKS FOR FOUR PROPERTIES. ***
    //
    // Three of them are answerable here, from two invented flows and the product's
    // own pure predicate. The fourth - that it is a real control with a whole
    // sentence on it - needs a window and is measureOverrideButton()'s.
    // ===================================================================
    bcdgui::Wizard unbound;
    bcdgui::Wizard bound;
    int            b = -1;
    {
        bcdsetup::MachineState st;
        fakeState(&st);
        st.usb.enumKeyPresent = true;
        st.usb.guidPresent    = false;
        ZeroMemory(&unbound, sizeof(unbound));
        bcdsetup::buildScreens(&unbound, &st);

        st.usb.guidPresent = true;
        ZeroMemory(&bound, sizeof(bound));
        bcdsetup::buildScreens(&bound, &st);

        for (int i = 0; i < bcdgui::screenCount(&unbound); i++)
            if (unbound.screens[i].title == ::kBindingTitle)
                b = i;
        check(b >= 0, L"the binding screen is in the flow at all");
    }
    if (b < 0)
        return;

    // PROPERTY 1 - Next is refused while the binding is missing, and allowed the
    // moment it is there. Asked of nextAllowed(), the product's own predicate, on two
    // tables the product built from two machines this suite invented.
    check(!bcdgui::nextAllowed(&unbound, b),
          L"Next is refused while the binding is missing: without it NOTHING works, "
          L"which is not true of any other screen in this flow");
    check(bcdgui::nextAllowed(&bound, b),
          L"...and it is allowed the moment the binding is there, so the block is a "
          L"property of the machine and not of the screen");

    // ...and of no OTHER screen, at either state. A flow that blocked everywhere
    // would pass the two lines above and be unusable.
    {
        int blocks = 0;
        for (int i = 0; i < bcdgui::screenCount(&unbound); i++)
            if (!bcdgui::nextAllowed(&unbound, i))
                blocks++;
        _snwprintf(what, 390,
                   L"...and it is the ONLY screen that refuses - %d of %d do, on a "
                   L"machine with no mixer bound, no loopMIDI and nothing installed",
                   blocks, bcdgui::screenCount(&unbound));
        check(blocks == 1, what);
    }

    // PROPERTY 2 - the door is REACHABLE ONLY FROM THE UNMET STATE. The table carries
    // the label on both machines, because a control whose existence depended on a
    // registry read would be a window built from a measurement; what changes is
    // whether the screen OFFERS it, and that is what gui.cpp's overrideLabelOf() -
    // exercised here through the same fields it reads - answers.
    check(unbound.screens[b].overrideLabel != 0 && unbound.screens[b].override != 0,
          L"the door has BOTH a label and a function behind it - a door with no "
          L"function is a control that swallows a press");
    check(!unbound.screens[b].satisfied && bound.screens[b].satisfied,
          L"...and satisfied follows the READING, which is what decides whether the "
          L"door is offered: unmet on the unbound machine, met on the bound one");

    // PROPERTY 3 - it is labelled DIFFERENTLY from Next. Both strings are read out of
    // the same table, which is the only comparison that can catch a door renamed to
    // agree with the button beside it.
    {
        const wchar_t* door = unbound.screens[b].overrideLabel;
        const wchar_t* next = bcdgui::primaryLabelFor(&unbound, b, false);
        _snwprintf(what, 390,
                   L"...and the way past is a DIFFERENT label, not a second Next - the "
                   L"door says \"%s\" and the button says \"%s\"", door, next);
        what[390] = 0;
        check(door != 0 && next != 0 && wcscmp(door, next) != 0, what);
        // ...and it says what taking it CLAIMS, rather than naming a direction. A
        // door labelled "Continue" is a Next with a longer word.
        check(posOf(door, L"Already applied") >= 0,
              L"...and it says what pressing it CLAIMS - a door labelled with a "
              L"direction is a second Next in more letters");
    }

    // PROPERTY 4 - taking it is RECORDED, and it makes this program finish with exit
    // code 3 rather than 0.
    //
    // *** THE MACHINE BELOW IS COMPLETELY HEALTHY, AND THAT IS THE WHOLE DESIGN OF
    //     THIS CHECK. *** On the machine the door exists for, winUsbBindingMissing is
    // already true and the exit code would be 3 without the door having been touched
    // - so a check run there would pass against a door that did nothing at all. Here
    // every pending flag is false and the run finishes 0; the ONLY thing that can
    // make it 3 is the press.
    //
    // WHERE EACH SIDE COMES FROM: the left is the product's own exitCodeFor() reading
    // a Pending this suite filled by calling the product's own overrideBinding(); the
    // right is the two literals 0 and 3, which are the values the README's exit code
    // table gives. Neither side reads the other.
    {
        Run r;
        ZeroMemory(&r, sizeof(r));
        Pending healthy;
        ZeroMemory(&healthy, sizeof(healthy));

        check(::exitCodeFor(&healthy) == 0,
              L"a run with nothing outstanding finishes 0, which is what makes the "
              L"next line about the door and not about the machine");

        capReset();
        bcdsetup::setLineSink(capSink);
        bcdsetup::setConsoleEcho(false);
        int advanced = bcdsetup::overrideBinding(&r);
        bcdsetup::setLineSink(0);
        bcdsetup::setConsoleEcho(true);

        check(advanced != 0,
              L"taking the door advances the flow - a door that records a claim and "
              L"leaves the user where they were is a control that swallows a press");
        check(r.bindingDoorTaken,
              L"...and the run records that it was taken");

        // IT IS WRITTEN TO THE LOG, and that is asserted on the LINES the product
        // said rather than on a flag: the flag reaches nothing but the exit code, and
        // a support request three weeks later carries the log.
        int said = 0;
        for (int i = 0; i < g_capN; i++)
            if (posOf(g_cap[i], L"already applied") >= 0)
                said++;
        _snwprintf(what, 390,
                   L"...and it SAYS so, through say(), so it is in the log file, the "
                   L"console and a support request - %d of %d captured lines name it",
                   said, g_capN);
        what[390] = 0;
        check(said >= 1, what);

        healthy.bindingClaimedByHand = r.bindingDoorTaken;
        _snwprintf(what, 390,
                   L"...and this run now finishes with exit code %d - done, with "
                   L"something still pending - on a machine where every other check "
                   L"passed and it would otherwise have been 0",
                   ::exitCodeFor(&healthy));
        what[390] = 0;
        check(::exitCodeFor(&healthy) == 3, what);
    }
}

// ===========================================================================
// PART 2e3c - THE TWO CHEAP READS SCREEN 3'S NEW SUBJECT IS BUILT ON.
//
// serviceIsRegistered() was KEPT by the round that removed the third party
// detection, on the written reasoning that the round after it would ask the SCM
// about midisrv. It now has that production caller - detectWindowsMidi() - so
// that decision is spent rather than pending. fileVersionNumbers() is published
// for the identical reason and gets the identical two-directional test: a file
// that must have a version resource, and a path that cannot exist.
// ===========================================================================
static void testServiceIsRegistered()
{
    wprintf(L"\n--- the two cheap reads under Windows MIDI Services ---\n");

    // -------------------------------------------------------------------
    // THE SCM READ, IN BOTH DIRECTIONS. A helper that always says yes passes the
    // first line and fails the second; one that always says no does the opposite.
    // PlugPlay is the Plug and Play service: it is on every Windows this program can
    // run on, it cannot be removed, and it is named here rather than being some
    // service this project installs - a check whose "must exist" side is our own
    // service would go red on any machine we have not installed on yet.
    // -------------------------------------------------------------------
    bool always = bcdsetup::serviceIsRegistered(L"PlugPlay");
    bool never  = bcdsetup::serviceIsRegistered(L"bcd3000-no-such-service-zz");
    wprintf(L"  serviceIsRegistered(PlugPlay)=%d  (bogus name)=%d\n",
            always ? 1 : 0, never ? 1 : 0);
    check(always,
          L"the SCM read finds a service Windows always has - PlugPlay - so it can "
          L"answer yes");
    check(!never,
          L"...and it does NOT find a name that cannot exist, so it can answer no: a "
          L"reading that only ever says yes would have made the new state unreachable");

    // -------------------------------------------------------------------
    // THE VERSION RESOURCE READ, IN BOTH DIRECTIONS.
    //
    // WHERE EACH SIDE COMES FROM: the left is what GetFileVersionInfoW pulled out
    // of a file on this disk; the right is a property no version resource can
    // legitimately lack - kernel32.dll's major number is 10 on every Windows this
    // program is allowed to run on, and the four numbers cannot all be zero on a
    // file that HAS the resource. Neither is computed from the other, and neither
    // is a number this project chose.
    //
    // kernel32.dll and not the MIDI transport, deliberately: the transport is
    // absent on a Windows without MIDI Services, and a check that goes red on a
    // machine the product has to survive is a check about the machine and not
    // about the reader.
    // -------------------------------------------------------------------
    wchar_t sysDir[MAX_PATH];
    sysDir[0] = 0;
    GetSystemDirectoryW(sysDir, MAX_PATH);
    wchar_t kernel[bcdsetup::kPathMax];
    bcdsetup::joinPath(kernel, bcdsetup::kPathMax, sysDir, L"kernel32.dll");

    DWORD v[4] = { 9, 9, 9, 9 };
    DWORD err  = 0xDEAD;
    bool  gotIt = bcdsetup::fileVersionNumbers(kernel, v, &err);
    wprintf(L"  fileVersionNumbers(kernel32.dll)=%d  %lu.%lu.%lu.%lu  err=%lu\n",
            gotIt ? 1 : 0, v[0], v[1], v[2], v[3], err);
    {
        wchar_t what[400];
        _snwprintf(what, 390,
                   L"the version resource read answers on a file that has one - "
                   L"kernel32.dll reads %lu.%lu.%lu.%lu, error %lu",
                   v[0], v[1], v[2], v[3], err);
        what[390] = 0;
        check(gotIt && err == 0 && v[0] == 10 && (v[2] != 0 || v[3] != 0), what);
    }

    DWORD z[4] = { 9, 9, 9, 9 };
    DWORD zerr = 0;
    bool  gotNothing = bcdsetup::fileVersionNumbers(
        L"C:\\this-path-does-not-exist-4f1c9a2e\\nope.dll", z, &zerr);
    wprintf(L"  fileVersionNumbers(missing)=%d  %lu.%lu.%lu.%lu  err=%lu\n",
            gotNothing ? 1 : 0, z[0], z[1], z[2], z[3], zerr);
    {
        wchar_t what[400];
        _snwprintf(what, 390,
                   L"...and it says NO for a path that cannot exist, with a numeric "
                   L"reason and the four numbers zeroed rather than left as the "
                   L"caller's - answered %d, error %lu, read %lu.%lu.%lu.%lu",
                   gotNothing ? 1 : 0, zerr, z[0], z[1], z[2], z[3]);
        what[390] = 0;
        check(!gotNothing && zerr != 0 &&
              z[0] == 0 && z[1] == 0 && z[2] == 0 && z[3] == 0, what);
    }
}

// ===========================================================================
// PART 2e3d - THE KNOWN-BAD BUILD LIST FOR microsoft/MIDI ISSUE #1047.
//
// *** THIS IS THE ONE PLACE IN THE PRODUCT THAT CHANGES WHEN MICROSOFT SHIPS THE
//     FIX, SO IT IS THE ONE PLACE THAT GETS A CHECK WHICH GOES RED WHEN IT IS
//     EDITED. *** That is intentional and it is not brittleness: whoever edits
// the table is making a claim about Windows, and having to update a line here
// with the same commit is how that claim gets written down instead of slipped in.
//
// TWO HALVES, AND THEY ARE DIFFERENT CLAIMS:
//
//   1. THE DECISION, over a table this file owns. All four outcomes - older than
//      the bad revision, at it, past it with no fix, and past a shipped fix - are
//      driven here. The fourth has NO input in the shipped table today, so
//      without a table of our own it would be a branch nothing could reach.
//
//   2. THE SHIPPED TABLE ITSELF, against numbers written down in this file from
//      the issue. Half 1 alone would be the harness testing its own fixture,
//      which is the exact defect this project has found nine times.
// ===========================================================================
static void testKnownBadMidiBuilds()
{
    wprintf(L"\n--- the known-bad build list (microsoft/MIDI #1047) ---\n");

    wchar_t what[400];

    // -------------------------------------------------------------------
    // HALF 1: the decision, over a table nothing else in this program can see.
    // The numbers are invented on purpose - 90000 is not a Windows build - so that
    // no accidental agreement with the shipped table can make one of these pass.
    // -------------------------------------------------------------------
    static const bcdsetup::KnownBadMidiBuild kOurs[] = {
        {  90000,  100,   0 },    // reported, no fix has shipped
        {  90001,  100, 200 }     // reported, and fixed at 200
    };
    const int n = (int)(sizeof(kOurs) / sizeof(kOurs[0]));

    check(!bcdsetup::midiBuildIsKnownBadIn(kOurs, n, 90000, 99),
          L"a revision BELOW the one the defect arrived in is not known bad - the "
          L"build before KB5101650 is the shape of this case");
    check(bcdsetup::midiBuildIsKnownBadIn(kOurs, n, 90000, 100),
          L"...the revision it arrived in IS known bad, so the boundary is inclusive "
          L"at the bottom");
    check(bcdsetup::midiBuildIsKnownBadIn(kOurs, n, 90000, 100000),
          L"...and so is every revision above it while no fix has shipped, which is "
          L"the case the OWNER'S OWN MACHINE is in: .8972 is past .8875 and the "
          L"defect still reproduces on it");
    check(!bcdsetup::midiBuildIsKnownBadIn(kOurs, n, 90001, 200),
          L"...and the branch that does not exist in the shipped table yet: once a "
          L"first fixed revision is written down, that revision is NOT known bad");
    check(bcdsetup::midiBuildIsKnownBadIn(kOurs, n, 90001, 199),
          L"...while the revision just under the fix still is, so the top boundary "
          L"is exclusive and the range really is a range");
    check(!bcdsetup::midiBuildIsKnownBadIn(kOurs, n, 90002, 100),
          L"...and a build nobody has reported is not known bad at any revision - "
          L"this program does not condemn a machine it has no report about");
    check(!bcdsetup::midiBuildIsKnownBadIn(0, 0, 90000, 100),
          L"...and an empty list condemns nothing, which is what the day after the "
          L"fix should look like");

    // -------------------------------------------------------------------
    // HALF 2: the SHIPPED table, against the issue as this file records it.
    //
    // WHERE EACH SIDE COMES FROM: the left is the table common.cpp compiles into
    // the product; the right is 26100/26200 and 8875 written here from
    // microsoft/MIDI issue #1047's own title and from KB5101650. Neither is
    // derived from the other, and the left cannot move without this going red.
    // -------------------------------------------------------------------
    wprintf(L"  shipped list has %d row(s):\n", bcdsetup::kKnownBadMidiBuildCount);
    for (int i = 0; i < bcdsetup::kKnownBadMidiBuildCount; i++)
        wprintf(L"    build %lu, bad from revision %lu, fixed at %lu\n",
                bcdsetup::kKnownBadMidiBuilds[i].build,
                bcdsetup::kKnownBadMidiBuilds[i].firstBadRevision,
                bcdsetup::kKnownBadMidiBuilds[i].firstFixedRevision);

    _snwprintf(what, 390,
               L"the shipped known-bad list is the two servicing branches issue "
               L"#1047 names and nothing else (%d rows)",
               bcdsetup::kKnownBadMidiBuildCount);
    what[390] = 0;
    check(bcdsetup::kKnownBadMidiBuildCount == 2, what);

    bool has26100 = false, has26200 = false, anyFixed = false;
    for (int i = 0; i < bcdsetup::kKnownBadMidiBuildCount; i++) {
        const bcdsetup::KnownBadMidiBuild* b = &bcdsetup::kKnownBadMidiBuilds[i];
        if (b->build == 26100 && b->firstBadRevision == 8875)
            has26100 = true;
        if (b->build == 26200 && b->firstBadRevision == 8875)
            has26200 = true;
        if (b->firstFixedRevision != 0)
            anyFixed = true;
    }
    check(has26100 && has26200,
          L"...and both of them start at revision 8875, which is KB5101650 - the "
          L"update the issue's title names");
    check(!anyFixed,
          L"...and no row claims a fix has shipped, because none has: issue #1047 is "
          L"open with no timeline. THIS CHECK IS SUPPOSED TO GO RED THE DAY IT DOES, "
          L"and whoever makes it red publishes which update fixed it");

    // And the product's own one-line wrapper answers off that table, so the
    // function every caller uses is the function these rows describe.
    check(bcdsetup::midiBuildIsKnownBad(26100, 8875) &&
          bcdsetup::midiBuildIsKnownBad(26200, 8875) &&
          bcdsetup::midiBuildIsKnownBad(26100, 8972),
          L"midiBuildIsKnownBad() answers YES on both builds the issue names and on "
          L"the .8972 the owner's machine is really at");
    check(!bcdsetup::midiBuildIsKnownBad(26100, 8000) &&
          !bcdsetup::midiBuildIsKnownBad(19045, 8875),
          L"...and NO on the June build and on a Windows 10 that was never reported, "
          L"so the answer is about the range and not about the function");
}

// The MIDI port screen out of a real buildScreens() run over one of those
// machines, so what is read back is the screen the PRODUCT built and not a row
// this file asked describeMidiPort() for directly.
static const bcdgui::Screen* midiScreenFor(bcdgui::Wizard* w,
                                           bcdsetup::MachineState* s,
                                           bcdsetup::WinMidiState want,
                                           int* indexOut)
{
    midiMachine(s, want);
    ZeroMemory(w, sizeof(*w));
    bcdsetup::buildScreens(w, s);
    if (indexOut)
        *indexOut = -1;
    for (int i = 0; i < bcdgui::screenCount(w); i++)
        if (w->screens[i].title == ::kMidiTitle) {
            if (indexOut)
                *indexOut = i;
            return &w->screens[i];
        }
    return 0;
}

static void testMidiPortScreen()
{
    wprintf(L"\n--- the MIDI port screen: three states, three invented machines ---\n");

    wchar_t what[400];

    // ===================================================================
    // *** THE SCREEN'S NEW SUBJECT, ASSERTED ON THE TEXT THAT LANDS ON THE PAGE
    //     AND NOT ON THE STRUCT THAT PRODUCED IT. ***
    //
    // WHERE EACH SIDE COMES FROM, and this is the whole reason the suite is
    // written this way: the HAYSTACK is Screen::row.detail as buildScreens()
    // filled it, from a MachineState this file invented - so it has been through
    // classifyWindowsMidi(), through describeMidiPort()'s switch and through
    // setRow()'s formatting. The NEEDLES are phrases and numbers written down
    // HERE, in this file, which setup.cpp cannot see. Asserting
    // `state.winMidi.serviceVersion[3] == 8875` instead would be reading back the
    // number this file just wrote - the trap that made eighteen checks on the
    // Setup screens incapable of failing, where ink was compared against a width
    // the renderer sets to the same value.
    //
    // AND THE THREE ARE ASSERTED AGAINST EACH OTHER, not only each against its
    // own phrase. A describeMidiPort() that ignored the machine and printed one
    // sentence would pass "mine is present" three times over; it fails the
    // moment each state is also required to be free of the other two's words.
    // ===================================================================
    bcdsetup::MachineState sReady, sBad, sUnread;
    bcdgui::Wizard         wReady, wBad, wUnread;
    int                    iReady = -1, iBad = -1, iUnread = -1;
    const bcdgui::Screen*  scReady =
        midiScreenFor(&wReady, &sReady, bcdsetup::kWinMidiReady, &iReady);
    const bcdgui::Screen*  scBad =
        midiScreenFor(&wBad, &sBad, bcdsetup::kWinMidiKnownBad, &iBad);
    const bcdgui::Screen*  scUnread =
        midiScreenFor(&wUnread, &sUnread, bcdsetup::kWinMidiUnread, &iUnread);

    check(scReady != 0 && scBad != 0 && scUnread != 0,
          L"the MIDI port screen is in the table for all three machines, by its title");

    if (scReady && scBad && scUnread) {
        const wchar_t* tReady  = scReady->row.detail;
        const wchar_t* tBad    = scBad->row.detail;
        const wchar_t* tUnread = scUnread->row.detail;
        wprintf(L"  READY   : %s\n", tReady);
        wprintf(L"  KNOWNBAD: %s\n", tBad);
        wprintf(L"  UNREAD  : %s\n", tUnread);

        // ---- STATE 1: nothing to do, and it says what was LOOKED AT ----
        _snwprintf(what, 390,
                   L"state 1 (service there, build not known bad): the page says "
                   L"there is nothing to install, and names the SERVICE it looked at "
                   L"- \"%.180s\"", tReady);
        what[390] = 0;
        check(posOf(tReady, L"Nothing to install") >= 0 &&
              posOf(tReady, L"midisrv") >= 0, what);
        check(posOf(tReady, L"Midi2.VirtualMidiTransport.dll") >= 0 &&
              posOf(tReady, L"1.0.15.0") >= 0,
              L"...and it names the TRANSPORT FILE it looked at, at the version that "
              L"was read - not a version the renderer could have supplied");
        check(posOf(tReady, L"26100.8000") >= 0,
              L"...and the BUILD it read, which is this machine's invented one and "
              L"not the one the harness runs on");
        check(posOf(tReady, L"not on the list") >= 0,
              L"...and it says what that build was compared against");
        // *** THE SENTENCE THIS WHOLE SCREEN EXISTS TO GET RIGHT. ***
        check(posOf(tReady, L"No port was created") >= 0 &&
              posOf(tReady, L"says one will work") >= 0,
              L"...and it refuses to promise a port: it says outright that none was "
              L"created and that nothing here says one will work");

        // ---- STATE 2: name the defect, the delivery route, and the cure ----
        check(posOf(tBad, L"#1047") >= 0,
              L"state 2 (build on the known-bad list): the page names the defect by "
              L"its issue number, so a user can look it up");
        check(posOf(tBad, L"26100.8875") >= 0,
              L"...and names the build it read, which is what put the screen in this "
              L"state");
        check(posOf(tBad, L"Windows Update") >= 0 &&
              posOf(tBad, L"yours to install") >= 0,
              L"...and says the fix arrives through Windows Update and that nothing "
              L"here is the user's to install");
        check(posOf(tBad, L"restarting the machine clears it") >= 0,
              L"...and says a restart clears it, which is the one thing a user can do "
              L"about it today");
        check(scBad->row.state == bcdgui::kRowWarn,
              L"...and it is the only one of the three that is amber, because it is "
              L"the only one with something to act on");

        // ---- STATE 3: the numbers, and no invented cause ----
        check(posOf(tUnread, L"Not established") >= 0 &&
              posOf(tUnread, L"no cause is guessed") >= 0,
              L"state 3 (anything else): the page says the reading did not settle and "
              L"that it is not guessing why");
        check(posOf(tUnread, L"registered 0") >= 0 &&
              posOf(tUnread, L"present 0") >= 0 &&
              posOf(tUnread, L"last error 2") >= 0,
              L"...and it prints the NUMERIC result of each read, including the error "
              L"code - a support log can act on 2, it cannot act on \"failed\"");

        // ---- the three really are three ----
        check(wcscmp(tReady, tBad) != 0 && wcscmp(tBad, tUnread) != 0 &&
              wcscmp(tReady, tUnread) != 0,
              L"the three states produce three DIFFERENT sentences - a row that "
              L"ignored the machine would produce one and pass every check above");
        check(posOf(tReady, L"#1047") < 0 && posOf(tUnread, L"#1047") < 0,
              L"...and only the known-bad state names the defect: the other two do "
              L"not warn about a build they did not find on the list");
        check(posOf(tBad, L"Nothing to install") < 0 &&
              posOf(tReady, L"Not established") < 0,
              L"...and neither of the other two carries state 1's or state 3's "
              L"opening, so each needle is discriminating rather than merely present");

        // *** NONE OF THE THREE PROMISES A PORT, WHICH IS THE RULE RATHER THAN A
        //     PROPERTY OF ONE STATE. *** The phrase searched for is the one the
        // installer really used to print, in printSummary() and in the console,
        // before this round made both honest.
        check(posOf(tReady, L"exists again") < 0 && posOf(tBad, L"exists again") < 0 &&
              posOf(tUnread, L"exists again") < 0,
              L"and NOT ONE of the three says the port \"exists again\" or anything "
              L"else this program has no reading for - it never creates one");

        // *** THE PHOTOGRAPHED STATE IS THE LONGEST ONE, ASSERTED RATHER THAN
        //     ASSUMED. *** shotState() picks the known-bad machine so that the render
        // pass measures the worst case against a screen allowedDeficit() holds at
        // zero. If a later edit made another state longer, the picture would stop
        // being of the worst case and nothing else in this file would notice.
        {
            int lReady = (int)wcslen(tReady), lBad = (int)wcslen(tBad),
                lUnread = (int)wcslen(tUnread);
            _snwprintf(what, 390,
                       L"the state installer/verify PHOTOGRAPHS is the longest of the "
                       L"three, so the zero-overflow ratchet measures the worst case "
                       L"(ready %d, known bad %d, unread %d characters)",
                       lReady, lBad, lUnread);
            what[390] = 0;
            check(lBad >= lReady && lBad >= lUnread, what);
        }

        // And the screen shotState() really builds is that state, which is what ties
        // the paragraph above to the PNG rather than to an intention.
        {
            bcdsetup::MachineState shot;
            shotState(&shot);
            check(bcdsetup::classifyWindowsMidi(&shot.winMidi) ==
                      bcdsetup::kWinMidiKnownBad,
                  L"...and the machine every capture is rendered from really is in "
                  L"that state, so the picture and the ratchet agree with this suite");
        }
    }

    // The screen's furniture, on one of the three: one bullet, no button, no
    // address. It reports; it does not offer, because there is nothing to offer.
    bcdsetup::MachineState s;
    bcdgui::Wizard w;
    int midiIndex = -1;
    const bcdgui::Screen* midi =
        midiScreenFor(&w, &s, bcdsetup::kWinMidiReady, &midiIndex);
    if (midi) {
        check(midi->bullets[0] != 0 && midi->bullets[1] == 0 &&
              midi->bullets[2] == 0 && midi->bullets[3] == 0,
              L"exactly one bullet - the state is in the ROW, so the screen's HEIGHT "
              L"does not depend on which machine is looking at it");
        check(midi->actionLabel == 0 && midi->action == 0,
              L"no button - Windows MIDI Services is in-box, so there is nothing for "
              L"a button to install");
        check(midi->addressLead == 0 && midi->addressUrl == 0 &&
              midi->addressOpen == 0,
              L"no address either - there is no page to send anybody to for something "
              L"that is already part of Windows");
        // *** FIX ROUND 1: THIS USED TO BE `check(midi->satisfied, ...)`, AND THAT
        //     FIELD HAS NO CONSUMER ON THIS SCREEN. *** gui.cpp's nextAllowed()
        // returns true at `!blockNextWhenUnmet` before it ever reads satisfied, and
        // this screen sets blockNextWhenUnmet false on purpose - its subject is the
        // knobs and the LEDs, not the audio path, so it does not hold anybody up.
        // Flipping satisfied to false therefore changes no behaviour anywhere, which
        // makes an assertion on it a check that cannot go red for the thing it
        // claims to be about. Asked of the product's own gate instead, over the real
        // screen index, so what is asserted is the CONSEQUENCE that a user could
        // notice rather than a field that happens to be set.
        check(midiIndex >= 0 && bcdgui::nextAllowed(&w, midiIndex) &&
              !midi->blockNextWhenUnmet,
              L"Next is allowed off the MIDI port screen - without the port the audio "
              L"still works, so it holds nobody up (asked of nextAllowed(), because "
              L"satisfied has no reader where blockNextWhenUnmet is false)");

        // *** AND IT IS ALLOWED OFF THE KNOWN-BAD STATE TOO, WHICH IS THE ONE THAT
        //     COULD PLAUSIBLY HAVE BEEN MADE TO BLOCK. *** The temptation on an amber
        // row is to stop the user. Design decision D5 is the owner's written
        // acceptance of this defect - the machine gets restarted sooner or later
        // anyway - so a screen that refused to continue would be this program
        // overruling a recorded decision on the strength of a Windows bug it cannot
        // fix. Asserted on the state, not assumed from the shared table entry.
        check(iBad >= 0 && bcdgui::nextAllowed(&wBad, iBad) &&
              scBad != 0 && !scBad->blockNextWhenUnmet,
              L"...and off the KNOWN-BAD state as well: it warns, it does not block - "
              L"see design decision D5, which accepted exactly this risk");
    }
}

// ===========================================================================
// PART 2e3 - THE "Get Zadig" SCREEN'S ADDRESS AND ITS BUTTON
//
// This screen had no suite of its own: its pane and its captures were measured inside
// the render pass and nothing looked at its words. The round that gave it the address
// and the second contextual button in this flow is the round that gives it one, because
// both of those are decisions of the TABLE and a render pass can only see the result.
// ===========================================================================
static void testZadigScreen()
{
    wprintf(L"\n--- the Get Zadig screen ---\n");

    wchar_t what[400];

    bcdsetup::MachineState s;
    fakeState(&s);
    bcdgui::Wizard w;
    ZeroMemory(&w, sizeof(w));
    bcdsetup::buildScreens(&w, &s);
    int zadig = -1;
    for (int k = 0; k < bcdgui::screenCount(&w); k++)
        if (w.screens[k].title == ::kZadigTitle)
            zadig = k;

    // *** THE ADDRESS IS ZADIG'S AND NOT loopMIDI'S. *** Pointer identity, which is a real
    // discrimination between the two published constants because they are different
    // strings - and is NOT the single definition check, which lives in
    // testAddressIsDefinedOnce() because a byte-identical copy is folded by the linker
    // and no pointer comparison in this process can see it. That was measured, by
    // injection, in this round.
    const wchar_t* addr = (zadig >= 0) ? w.screens[zadig].addressUrl : 0;
    _snwprintf(what, 390,
               L"the Get Zadig screen carries ZADIG's published address and not the "
               L"loopMIDI page - \"%s\"", addr ? addr : L"(none)");
    what[390] = 0;
    check(zadig >= 0 && addr == bcdsetup::kZadigDownloadPage, what);

    // ...and it is unconditional here, which is NOT an exception to the state rule. The
    // rule is about a state this program can SEE: Zadig installs nothing, registers
    // nothing and leaves no key, which is the recorded reason this screen has no row at
    // all. A machine that already has Zadig is indistinguishable from one that does not,
    // so there is no "already present" here for the screen to be honest about - and a
    // screen that guessed would be reporting on something it did not measure.
    _snwprintf(what, 390,
               L"...and it has no row, which is why the address is unconditional here: "
               L"there is no reading that could say whether Zadig has been downloaded "
               L"(row title \"%s\")",
               (zadig >= 0 && w.screens[zadig].row.title[0]) ? w.screens[zadig].row.title
                                                             : L"(none)");
    what[390] = 0;
    check(zadig >= 0 && w.screens[zadig].row.title[0] == 0, what);

    // *** AND THE LEAD, BECAUSE THE OWNER ASKED FOR THE TOP OF THE SCREEN TO BE DIRECT
    //     ABOUT WHAT TO DO. *** A bare address under a rule says where but not what. The
    // needle is the verb, written in this file; the haystack is the string setup.cpp
    // really assigned.
    const wchar_t* lead = (zadig >= 0) ? w.screens[zadig].addressLead : 0;
    _snwprintf(what, 390,
               L"...above a line that says what to DO with it, which is the owner's "
               L"\"direct about what to do\" - \"%s\"", lead ? lead : L"(none)");
    what[390] = 0;
    check(lead != 0 && posOf(lead, L"Download") >= 0, what);

    // *** THE BUTTON, AND ITS LABEL NAMES WHAT PRESSING IT DOES AND NOTHING MORE. ***
    // This is Rule 2 on the one control in this program that is easiest to overstate:
    // pressing it opens a web page. It does not download Zadig, it does not install it,
    // and it must never claim to run it - rebinding a USB device is the one operation in
    // this whole process that can leave the mixer unusable, and it stays in the user's
    // hands on purpose, which is the same policy chooseLoopMidiOffer() states for the
    // other direction.
    //
    // WHERE EACH SIDE COMES FROM: the left is the label setup.cpp really assigned; the
    // right is three literals written in this file - a verb that must be there and two
    // that must not. setup.cpp cannot see them.
    const wchar_t* blabel = (zadig >= 0) ? w.screens[zadig].actionLabel : 0;
    _snwprintf(what, 390,
               L"the button on this screen says what pressing it DOES - it opens a page, "
               L"it does not download and it does not run Zadig (\"%s\")",
               blabel ? blabel : L"(none)");
    what[390] = 0;
    check(blabel != 0 && posOf(blabel, L"Open") >= 0 &&
          posOf(blabel, L"Download") < 0 && posOf(blabel, L"Run") < 0, what);

    _snwprintf(what, 390,
               L"...and there is a function behind it, so the label is not a control "
               L"that swallows the press (action %s)",
               (zadig >= 0 && w.screens[zadig].action) ? L"set" : L"MISSING");
    what[390] = 0;
    check(zadig >= 0 && w.screens[zadig].action != 0, what);

    // *** AND THE ADDRESS THE PANE PRINTS IS THE SAME ONE. *** The haystack is the buffer
    // the product's own say() calls filled on their way to the console and the log file;
    // the needle is the published constant. A screen and a console naming two different
    // pages is the failure, and neither side here can produce the other.
    {
        const wchar_t* pane = (zadig >= 0 && w.screens[zadig].paneText)
                              ? w.screens[zadig].paneText : L"";
        check(posOf(pane, bcdsetup::kZadigDownloadPage) >= 0,
              L"...and the pane, which is what the console printed and the log file "
              L"keeps, names the SAME page as the address painted above it");
    }
}

// ===========================================================================
// PART 2e3b - "ONE DEFINITION, PUBLISHED - NOT A COPY", ASSERTED ON THE SOURCE TEXT
//             BECAUSE NO POINTER COMPARISON IN THIS PROCESS CAN SAY IT
//
// *** THIS SUITE EXISTS BECAUSE THE OBVIOUS CHECK WAS INJECTED AND PASSED. *** The first
// form of the single definition check was pointer identity: `screen.addressUrl ==
// bcdsetup::kZadigDownloadPage`, on the argument that a byte-identical second literal
// spelled out in setup.cpp would be a different object and would fail it. The injection
// Task 6d's step 7 prescribes - replace the published constant with a copy in one of the
// two places - was run and ALL 829 CHECKS STAYED GREEN, at both DPIs, in the two places
// that comparison is made.
//
// WHY, AND THE FIRST ANSWER WRITTEN HERE NAMED THE WRONG SWITCH - CORRECTED BY
// MEASUREMENT, NOT BY ARGUMENT. This block used to say that /O2 implies /GF and that "the
// linker's default /OPT:ICF then folds identical COMDATs across units". The reviewer of
// Task 6d built it both ways and measured: with /OPT:NOICF the literals STILL fold and the
// pointer checks still cannot fail; with /GF- they stop folding and three pointer checks go
// red. So the mechanism is /GF alone - content-derived COMDAT names plus ordinary duplicate
// symbol elimination - and common.cpp's definition and a copy in setup.cpp end up as ONE
// address in the image either way. The two sides of that comparison were the same object,
// which is this project's own definition of a check that cannot fail.
//
// *** THE PRACTICAL CONSEQUENCE, WHICH IS WHY THE CORRECTION IS WORTH THE PARAGRAPH: ***
// somebody "repairing" a pointer check by adding /OPT:NOICF would restore a check that
// STILL cannot fail, and would believe they had fixed it. Do not assert "defined once" with
// a pointer anywhere. Read the source text, the way this suite does.
//
// *** SO THE TWO SIDES ARE THE COMPILED CONSTANT AND THE BYTES OF THE SOURCE. *** The
// installer's own .cpp and .h files are read off the disk and the address literal is
// counted in each. It must appear exactly ONCE across all of them, and that once must be
// in common.cpp - which is what "published" means: every other file names the symbol.
// Nothing the compiler or the linker does can hide a second spelling from this.
//
// THE IDIOM IS ESTABLISHED HERE: testDeviceScreen() reads native\bcdasio\usbdev.cpp as
// text for the same class of reason, and PART 4's freshness check reads every file the
// binary's words come from. The path is built from this binary's own location and not from
// the working directory, exactly like driverSourcePath(), because a check that silently
// could not find its file would report safety - which is worse than no check.
//
// WHAT IT CANNOT CATCH, said rather than left to be discovered: a copy written with a
// different spelling of the same address - a trailing slash dropped, http for https. That
// is a different defect and it is what the pane checks are for: the pane is the bytes
// say() really printed, and it is searched for the published constant.
// ===========================================================================
static bool installerSourcePath(const wchar_t* leaf, wchar_t* out, int cap)
{
    wchar_t exe[MAX_PATH];
    DWORD   n = GetModuleFileNameW(0, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return false;
    // Off the file name, then off "verify". What is left is installer\.
    for (int up = 0; up < 2; up++) {
        wchar_t* slash = wcsrchr(exe, L'\\');
        if (!slash)
            return false;
        *slash = 0;
    }
    _snwprintf(out, (size_t)cap - 1, L"%s\\%s", exe, leaf);
    out[cap - 1] = 0;
    return true;
}

// One address, counted across the installer's own sources. `inCommon` is how many times
// it is spelled in common.cpp - the definition - and `elsewhere` is everywhere else.
static void countAddressSpellings(const char* needle, int* inCommon, int* elsewhere,
                                  int* filesRead, wchar_t* firstCopy, int copyCap)
{
    static const wchar_t* const sources[7] = {
        L"common.cpp", L"common.h", L"setup.cpp", L"gui.cpp", L"gui.h",
        L"uninstall.cpp", L"check.cpp"
    };
    *inCommon  = 0;
    *elsewhere = 0;
    *filesRead = 0;
    if (firstCopy && copyCap > 0)
        firstCopy[0] = 0;
    for (int i = 0; i < 7; i++) {
        wchar_t path[MAX_PATH];
        if (!installerSourcePath(sources[i], path, MAX_PATH))
            continue;
        SIZE_T size = 0;
        BYTE*  raw  = readWholeFile(path, &size);
        if (!raw)
            continue;
        char* text = (char*)HeapAlloc(GetProcessHeap(), 0, size + 1);
        if (text) {
            memcpy(text, raw, size);
            text[size] = 0;
            int here = countOfText(text, needle);
            (*filesRead)++;
            if (i == 0)
                *inCommon += here;
            else if (here > 0) {
                *elsewhere += here;
                if (firstCopy && copyCap > 0 && firstCopy[0] == 0) {
                    _snwprintf(firstCopy, (size_t)copyCap - 1, L"%s (%d)", sources[i],
                               here);
                    firstCopy[copyCap - 1] = 0;
                }
            }
            HeapFree(GetProcessHeap(), 0, text);
        }
        HeapFree(GetProcessHeap(), 0, raw);
    }
}

// ===========================================================================
// PART 2e3c - A REBUILD IS A FRESH BUILD, AND THIS IS THE CLASS BEHIND A DEFECT THAT
//             WAS FOUND BY A PERSON EDITING THE AREA RATHER THAN BY ANY CHECK
//
// *** THE INSTANCE. *** buildScreens() set the MIDI port screen's addressLead and
// addressUrl only inside `if (o.kind != kActionNone)`, with no else, while actionLabel
// was assigned unconditionally. It runs AGAIN after every re-check, over a table that is
// already filled - its own header says so and says that is why it must assign every field
// it cares about - so: machine without teVirtualMIDI, address shown, press `Install
// loopMIDI...`, winget installs it, re-check, offer becomes kActionNone, THE BUTTON GOES
// AND THE ADDRESS STAYS. "Download loopMIDI here:" under a green row reading "There is
// nothing to do on this screen", on the exact path the owner walks.
//
// *** WHY NOTHING SAW IT. *** Every check on that rule builds a Wizard, zeroes it and
// calls buildScreens() once. All of them therefore ask whether a field was ever SET.
// None could ask whether it was CLEARED, because none of them ever built twice.
//
// *** SO THIS ASKS THE GENERAL QUESTION, AND IT IS THE CHEAP ONE. *** For every ordered
// pair of invented machines, build A into a zeroed table, build B over the top of it, and
// require the result to equal a fresh build of B. That is the property buildScreens()
// claims in its own header, stated once, over the WHOLE Screen and not over the three
// fields that happened to bite. Any field assigned inside a conditional anywhere in that
// function fails this.
//
// *** ONE CHECK NAME AND N-SQUARED COMPARISONS UNDERNEATH, *** in the shape this harness
// already uses for the per-screen sweeps: the count of disagreements, and the first one
// named. Thirty of them at six states, which would be thirty lines of denominator saying
// the same thing.
//
// WHERE EACH SIDE COMES FROM: both are the product's own buildScreens() over the SAME
// MachineState. The only difference between them is what was in the table beforehand, so
// a disagreement is by construction a field the function failed to write.
// ===========================================================================

// *** EVERY BYTE, AND NOT A LIST OF FIELDS. THE FIRST VERSION OF THIS WAS A LIST AND THE
//     REVIEW FOUND THE HOLE IN ITS TRIPWIRE. ***
//
// The comparison used to be field by field, guarded by sizeof(Screen) so that growing the
// struct without extending the list went red. The review measured that guard: adding
// `bool bcdPaddingProbe` to Screen left sizeof at exactly 1536, because the bool fell into
// existing padding - tripwire green, suite green, field uncompared. A pointer trips it
// (1544). So the guard caught the fields nobody adds and missed the small ones everybody
// does.
//
// *** THE HOLE IS CLOSED BY DELETING THE THING THAT HAD IT. *** memcmp over the whole
// struct compares every byte including padding, so there is no list to go stale and no
// sizeof to ratchet - a field added anywhere is compared from the moment it exists,
// whatever its size. That is why the tripwire is gone rather than tightened: a guard on a
// list is only ever as good as the list.
//
// *** WHAT memcmp COULD NOT DO NAIVELY, AND HOW IT IS HANDLED. *** setRow() writes strings
// into Row's fixed arrays and leaves whatever was past the terminator alone, so a rebuilt
// table can carry the tail of a LONGER previous detail behind its own NUL. memcmp would
// call that a disagreement when the two screens say exactly the same thing. So both sides
// are copied and their two string members re-written into freshly zeroed buffers first.
// If somebody adds a THIRD array member with the same habit, this goes red - loudly and
// wrongly, which is the safe direction for a mistake of that kind to fail in.
//
// The padding itself compares equal because both Wizards are ZeroMemory'd before any
// build and buildScreens() only ever assigns members.
//
// THE FIELD LIST BELOW SURVIVES FOR ONE PURPOSE ONLY: NAMING. It is what turns a red line
// into "field addressLead" instead of "some byte". It is best-effort by construction and
// says so when it cannot name the difference - the COMPARISON is bytes and does not
// consult it.
static void normaliseScreen(bcdgui::Screen* dst, const bcdgui::Screen* src)
{
    memcpy(dst, src, sizeof(*dst));
    wchar_t t[bcdgui::kRowTitle];
    wchar_t d[bcdgui::kRowText];
    ZeroMemory(t, sizeof(t));
    ZeroMemory(d, sizeof(d));
    wcsncpy(t, src->row.title,  bcdgui::kRowTitle - 1);
    wcsncpy(d, src->row.detail, bcdgui::kRowText  - 1);
    memcpy(dst->row.title,  t, sizeof(t));
    memcpy(dst->row.detail, d, sizeof(d));
}

static const wchar_t* nameTheDifference(const bcdgui::Screen* a, const bcdgui::Screen* b)
{
    const wchar_t* dummy = 0;
    const wchar_t** field = &dummy;
#define BCD_SAME(expr, name) do { if ((a->expr) != (b->expr)) { *field = name; \
                                   return *field; } } while (0)
    BCD_SAME(kind,                L"kind");
    BCD_SAME(title,               L"title");
    BCD_SAME(primaryLabel,        L"primaryLabel");
    BCD_SAME(startsTheWork,       L"startsTheWork");
    BCD_SAME(paintsMachineReview, L"paintsMachineReview");
    BCD_SAME(paintsOpening,       L"paintsOpening");
    BCD_SAME(addressLead,         L"addressLead");
    BCD_SAME(addressUrl,          L"addressUrl");
    BCD_SAME(addressOpen,         L"addressOpen");
    BCD_SAME(bullets[0],          L"bullets[0]");
    BCD_SAME(bullets[1],          L"bullets[1]");
    BCD_SAME(bullets[2],          L"bullets[2]");
    BCD_SAME(bullets[3],          L"bullets[3]");
    BCD_SAME(showDevicePhoto,     L"showDevicePhoto");
    BCD_SAME(showZadigShot,       L"showZadigShot");
    BCD_SAME(paneCaption,         L"paneCaption");
    BCD_SAME(paneText,            L"paneText");
    BCD_SAME(actionLabel,         L"actionLabel");
    BCD_SAME(action,              L"action");
    BCD_SAME(blockNextWhenUnmet,  L"blockNextWhenUnmet");
    BCD_SAME(overrideLabel,       L"overrideLabel");
    BCD_SAME(override,            L"override");
    BCD_SAME(choiceLabels[0],     L"choiceLabels[0]");
    BCD_SAME(choiceLabels[1],     L"choiceLabels[1]");
    BCD_SAME(choiceSelected,      L"choiceSelected");
    BCD_SAME(choose,              L"choose");
    BCD_SAME(row.state,           L"row.state");
    BCD_SAME(satisfied,           L"satisfied");
    BCD_SAME(actionOnlyOpensAPage, L"actionOnlyOpensAPage");
#undef BCD_SAME
    if (wcscmp(a->row.title, b->row.title) != 0)
        return L"row.title";
    if (wcscmp(a->row.detail, b->row.detail) != 0)
        return L"row.detail";
    return L"a member this message cannot name - the comparison is bytes and the naming "
           L"is a list, so add it to nameTheDifference()";
}

static bool screensAgree(const bcdgui::Screen* a, const bcdgui::Screen* b,
                         const wchar_t** field)
{
    bcdgui::Screen na;
    bcdgui::Screen nb;
    normaliseScreen(&na, a);
    normaliseScreen(&nb, b);
    if (memcmp(&na, &nb, sizeof(na)) == 0)
        return true;
    if (field)
        *field = nameTheDifference(&na, &nb);
    return false;
}

static void testRebuildIsAFreshBuild()
{
    wprintf(L"\n=== PART 2e3c: a rebuild is a fresh build ===\n");

    wchar_t what[400];

    // *** THERE IS NO sizeof TRIPWIRE HERE ANY MORE, AND ITS ABSENCE IS THE FIX. *** The
    // first version of this suite compared a LIST of fields and guarded the list with
    // sizeof(Screen) == 1536. The review measured that guard and found the hole: a `bool`
    // added to Screen fell into existing padding, sizeof stayed at exactly 1536, and the
    // field went uncompared with everything green. A pointer trips it (1544), so the
    // guard caught the fields nobody adds and missed the small ones everybody does.
    // screensAgree() compares BYTES now, so there is no list to go stale and nothing to
    // ratchet - see the block over it.

    struct Machine {
        const wchar_t* name;
        int            which;
    };
    // Six machines that make buildScreens() take genuinely different branches over
    // the mixer and binding screens, which are the two screens left whose rows still
    // depend on the MachineState. (The MIDI port screen no longer branches on
    // anything - see the block over describeMidiPort() - so it is no longer a
    // source of the difference this suite needs, and is not used to manufacture
    // one.)
    //
    // *** FIX ROUND 1: MACHINE 4 USED TO SET registeredElsewhere AND THE SENTENCE
    //     ABOVE WAS FALSE OF IT. *** That field is read by
    // resolvePreviousRegistration(), which writes the install manifest - NOT by
    // buildScreens(), which is the only function this suite drives. So machine 4's
    // screen table came out byte-identical to machine 0's, and 2 of the 30 ordered
    // pairs were comparing a machine against itself: the two that can never
    // disagree, in a suite whose entire subject is disagreement. It passed, and it
    // proved nothing, which is the more expensive of the two ways to be wrong.
    //
    // It is now the state describeBinding() really branches on: guidPresent false
    // AND guidOnOtherFunction above zero, which is setup.cpp's "Zadig was run, but
    // on the wrong function" arm. That is genuinely distinct from machine 2, which
    // is guidPresent false with guidOnOtherFunction at zero and takes the other arm.
    static const Machine kMachines[6] = {
        { L"a whole machine already installed",      0 },
        { L"the mixer has never been seen",           1 },
        { L"the mixer is seen but not bound",         2 },
        { L"the mixer is bound but not connected now", 3 },
        { L"Zadig was run, but on the wrong function", 4 },
        { L"nothing installed at all",                5 }
    };

    int            pairs      = 0;
    int            disagreed  = 0;
    const wchar_t* firstWhere = L"(none)";
    const wchar_t* firstField = L"(none)";

    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            if (i == j)
                continue;
            bcdsetup::MachineState a;
            bcdsetup::MachineState b;
            for (int pass = 0; pass < 2; pass++) {
                bcdsetup::MachineState* s = pass ? &b : &a;
                int which = pass ? kMachines[j].which : kMachines[i].which;
                if (which == 5) {
                    ZeroMemory(s, sizeof(*s));
                } else {
                    fakeState(s);
                    if (which == 1) s->usb.enumKeyPresent = false;
                    if (which == 2) s->usb.guidPresent = false;
                    if (which == 3) s->usb.interfacePresentNow = false;
                    if (which == 4) {
                        s->usb.guidPresent        = false;
                        s->usb.guidOnOtherFunction = 1;
                    }
                }
            }

            // The rebuilt table: A first, then B over the top of it, which is what
            // rebuildScreens() does to a live flow after every re-check.
            bcdgui::Wizard rebuilt;
            ZeroMemory(&rebuilt, sizeof(rebuilt));
            bcdsetup::buildScreens(&rebuilt, &a);
            bcdsetup::buildScreens(&rebuilt, &b);

            // ...and the same B into a table that has never held anything else.
            bcdgui::Wizard fresh;
            ZeroMemory(&fresh, sizeof(fresh));
            bcdsetup::buildScreens(&fresh, &b);

            pairs++;
            int n = bcdgui::screenCount(&fresh);
            if (bcdgui::screenCount(&rebuilt) != n) {
                disagreed++;
                if (wcscmp(firstWhere, L"(none)") == 0) {
                    firstWhere = kMachines[j].name;
                    firstField = L"screenCount";
                }
                continue;
            }
            for (int k = 0; k < n; k++) {
                const wchar_t* fieldName = 0;
                if (!screensAgree(&rebuilt.screens[k], &fresh.screens[k], &fieldName)) {
                    disagreed++;
                    if (wcscmp(firstWhere, L"(none)") == 0) {
                        firstWhere = kMachines[j].name;
                        firstField = fieldName;
                    }
                    break;
                }
            }
        }
    }

    wprintf(L"  %d ordered pairs of invented machines, %d disagreement(s)\n", pairs,
            disagreed);
    _snwprintf(what, 390,
               L"buildScreens() over a table that is already filled produces exactly what "
               L"it produces over a zeroed one - %d ordered pairs, %d disagreement(s) "
               L"(first: \"%s\", field %s). This is the class the address defect of "
               L"2026-07-31 belonged to: a field written only in one branch keeps the "
               L"PREVIOUS machine's answer across a re-check",
               pairs, disagreed, firstWhere, firstField);
    what[390] = 0;
    check(pairs == 30 && disagreed == 0, what);
}

static void testAddressIsDefinedOnce()
{
    wprintf(L"\n--- the download address is defined ONCE, in common.cpp ---\n");

    wchar_t what[400];

    struct AddressCase {
        const char*    needle;   // narrow, because the source is read as bytes
        const wchar_t* who;
    };
    // The needle stops before the last path element on purpose: what must not exist twice
    // is the ADDRESS, and a second spelling that shares the host is already a second copy
    // of the thing that matters. It is also what keeps the count independent of the
    // trailing slash the Zadig page carries.
    //
    // *** ONE ENTRY NOW, NOT TWO. *** The third party download page this table used to
    // check alongside Zadig's is gone from the program along with the detection and the
    // offer that named it - it is not a literal here to count because it is not a
    // literal anywhere in the program any more, which the Step 5 audit in the task
    // report confirms across the whole tree rather than only these seven sources.
    static const AddressCase addresses[1] = {
        { "https://zadig.akeo.ie",                            L"the Zadig page"    }
    };

    // *** FIX ROUND 1: THE BOUND IS DERIVED FROM THE TABLE AND IS NOT THE LITERAL 1.
    //     *** When the second row went, three loops in this file were left reading
    // `i < 1` beside a `[1]` table - which is correct today and silently wrong the
    // day somebody adds a row back, because the table would grow and the loop would
    // not. It would not fail: it would quietly stop checking the new address, which
    // is the failure mode this whole suite exists to catch one level up. Taken from
    // sizeof so the two cannot drift apart again.
    const int kCases = (int)(sizeof(addresses) / sizeof(addresses[0]));
    for (int i = 0; i < kCases; i++) {
        int inCommon = 0, elsewhere = 0, filesRead = 0;
        wchar_t firstCopy[120];
        countAddressSpellings(addresses[i].needle, &inCommon, &elsewhere, &filesRead,
                              firstCopy, 120);
        wprintf(L"  %-20s %d in common.cpp, %d elsewhere, %d source files read\n",
                addresses[i].who, inCommon, elsewhere, filesRead);

        // The definition is where it is published, which is also what makes the count
        // below mean "no copies" rather than "the address is nowhere".
        _snwprintf(what, 390,
                   L"%s is DEFINED in common.cpp, once - found %d spelling(s) there "
                   L"across %d of 7 sources read", addresses[i].who, inCommon,
                   filesRead);
        what[390] = 0;
        check(filesRead == 7 && inCommon == 1, what);

        // ...and nowhere else. THIS is the check the pointer comparison could not be:
        // the injection that folded into one address in the image is two spellings on
        // the disk, and this counts them.
        //
        // *** THE MESSAGE USED TO NAME A FILE THIS SCAN NEVER OPENS. *** It read "the
        // screen, the console, the log file and the README cannot come to name different
        // pages". The seven files above are .cpp and .h; no README is among them, and the
        // review of Task 6d measured live hand written copies of the loopMIDI address in
        // installer\README.md and two in the repository root's README.md. A message that
        // claims coverage it does not have is the defect class this project has counted
        // more than a dozen times, so the sentence now names only what it reads - and the
        // installer's own README is asserted by testReadmeAddressesMatchTheProgram()
        // below, which really does open it.
        _snwprintf(what, 390,
                   L"...and NOWHERE else in the 7 installer SOURCES this scan reads, so "
                   L"the screen, the console and the log file cannot come to name "
                   L"different pages - %d copy(ies)%s%s", elsewhere,
                   firstCopy[0] ? L", first in " : L"",
                   firstCopy[0] ? firstCopy : L"");
        what[390] = 0;
        check(elsewhere == 0, what);
    }
}

// ===========================================================================
// PART 2e3d - /console CARRIES THE SAME SUBJECTS THE WINDOW STEPS THROUGH
//
// *** "SIX SUBJECTS" AND "NINE SCREENS" ARE BOTH TRUE, OF DIFFERENT THINGS, AND A
//     CHECK THAT CONFLATED THEM WOULD BE WRONG IN A WAY THAT LOOKED RIGHT. ***
//
// The design's section 3 settles which set this is about, in its own words: "Six steps,
// one subject per screen, always all six, plus a final screen", and its table then says
// "screen 0 is not a step and is not counted as one". The flow setup.cpp builds has NINE
// entries: those six steps, the opening, the progress screen and the summary.
//
// THIS SUITE ASSERTS THE SIX STEPS, and it DERIVES the six from the table rather than
// writing it down - see stepScreenCount(). The three it leaves out are left out for a
// reason each, and none of the reasons is "the check was easier that way":
//
//   the opening    Its subject is what this is, who wrote it and the non affiliation
//                  notice. Its console counterpart is the BANNER, and the last check
//                  below asserts it - so the exclusion is proved here rather than
//                  claimed. It is not a step in any flow.
//   the progress   Its subject IS the console. Every line the work produces goes out
//                  through say(); in /console that output is the whole of the mode.
//                  Asking the console to also print the word "Installing" would be
//                  asserting a caption on a progress bar, which is not a subject.
//   the summary    printSummary() is the console's version of it, and PART 1 above
//                  tests that text branch by branch on the real function.
//
// *** AND THE PLAN'S STEP 3 FOR THIS TASK IS REFUSED, WITH THE MEASUREMENT WRITTEN DOWN
//     RATHER THAN THE REFUSAL LEFT IN A REPORT. *** It reads "Make /console walk the
// table rather than printing the old single walkthrough". Four things stand against it:
//
//   (a) THE DESIGN FORBIDS IT. Section 5.2 lists /console under "What stays out", and
//       section 7 names "the six step walkthrough and its order" among the content
//       decisions this redesign leaves alone.
//   (b) TWO OF THE WALKTHROUGH'S SIX STEPS CORRESPOND TO NO SCREEN AT ALL - signing back
//       in so the control service starts, and the DJ software's offer to download
//       drivers, which is the one action that undoes a working install and is item 1 of
//       the warnings block. A console that walked the table would print neither.
//   (c) THE TWO PANES ARE THIS WALKTHROUGH'S OWN BYTES. printLoopMidiStepBody() and
//       printZadigStepBody() are called BY the walkthrough and collected on the way past
//       into the MIDI port screen's pane and the Zadig screen's pane. Replacing the
//       walkthrough would either empty those two panes or make a second copy of the two
//       paragraphs setup.cpp names as the ones that must never be duplicated.
//   (d) SCREEN TEXT NAMES CONTROLS A CONSOLE RUN DOES NOT HAVE - two radio buttons, a
//       contextual action button, a named door. Printing it into a mode with no window
//       is this project's signature defect, counted thirteen times.
//
// So the parity is asserted and /console is not rewritten. The day the console stops
// carrying one of the six, THIS is what goes red, and the fix is then decided against a
// measurement instead of ahead of one.
//
// *** WHERE EACH SIDE COMES FROM, AND THE CLAIM THIS BLOCK USED TO MAKE WAS TOO WIDE BY
//     ONE SUBJECT. *** The left side of every check below is text the PRODUCT emitted
// through its own line sink: printPrerequisiteWalkthrough(), printSummary() and
// printBanner(), the real ones. The right side is a phrase written in this file, which
// none of those functions can see - and that was true of five of the six. Subject 4's
// needle WAS bcdsetup::kZadigDownloadPage, the same compiled constant the walkthrough
// prints, so both sides moved together: rewriting that address in common.cpp to a
// different host left this check green while the console sent people somewhere else. It
// is spelled out in this file now, and the note beside it records the measurement.
//
// *** AND WHAT THE NEEDLES ARE, SAID PLAINLY: THEY ARE HAND-PICKED PROXIES FOR EACH
//     SUBJECT AND NOT THE SCREENS' TITLES. *** /console does not print screen titles and
// is not asked to - the design puts the mode out of scope and section 7 keeps the
// walkthrough's own wording. So each needle is a phrase this file judges to be the
// console's statement OF that subject, and what the check proves is "the console still
// says this", not "the console describes this screen". Subject 2 is the clearest case and
// its needle is matched in printSummary() - a record of what the run was set up for - in
// a mode that has no radio buttons to offer; the block above the table argues why that is
// the honest place for it. A reader tightening this should change the needles, not the
// message.
//
// The ORDER the left side is measured against is a third source again - it is read off
// the table buildScreens() filled, so a round that reorders the screens without
// reordering the console fails here instead of shipping two different procedures to two
// audiences.
// ===========================================================================

// The screens that are STEPS, counted off the table instead of written down.
//
// This is the design's own rule in code: the opening (Screen::paintsOpening), the
// progress screen (kScreenWork) and the summary (kScreenDone) are the three its table
// treats separately, and what is left is the six it calls steps. COUNTED and not stored,
// for the reason screenCount() is not stored: a literal six is a second number somebody
// has to keep equal to the table.
static int stepScreenCount(const bcdgui::Wizard* w)
{
    int n   = 0;
    int end = bcdgui::screenCount(w);
    for (int i = 0; i < end; i++) {
        if (w->screens[i].paintsOpening)
            continue;
        if (w->screens[i].kind == bcdgui::kScreenWork ||
            w->screens[i].kind == bcdgui::kScreenDone)
            continue;
        n++;
    }
    return n;
}

// Where a screen sits in the flow, found by the identity of its title POINTER - which is
// pageName()'s rule, for pageName()'s reason: this file has had a substring needle match
// the wrong screen in three consecutive rounds.
//
// *** THIS IS NOT THE POINTER COMPARISON PART 2e3b REFUSES, AND THE DIFFERENCE IS WHAT IS
//     BEING ASKED. *** That one asked "is this string defined once", which a pointer
// cannot answer, because /GF gives identical literals one address whatever the linker is
// told about ICF. This one LOCATES an entry in a table the same translation unit filled
// from the same constant. There is nothing here for a second spelling to hide from.
static int screenIndexOfTitle(const bcdgui::Wizard* w, const wchar_t* title)
{
    int end = bcdgui::screenCount(w);
    for (int i = 0; i < end; i++)
        if (w->screens[i].title == title)
            return i;
    return -1;
}

// One step screen, the phrase a console run has to say about it, and where in the console
// that phrase lives.
struct ConsoleSubject {
    const wchar_t* title;        // the screen, by its own title constant
    const wchar_t* needle;       // what /console has to SAY about that subject
    bool           inProcedure;  // the walkthrough carries it, so its ORDER is asserted
    const wchar_t* carriedBy;
};

static void testConsoleCarriesTheSubjects()
{
    wprintf(L"\n=== PART 2e3d: /console carries the six subjects the window steps "
            L"through ===\n");

    wchar_t what[400];

    bcdsetup::MachineState s;
    fakeState(&s);
    bcdgui::Wizard w;
    ZeroMemory(&w, sizeof(w));
    bcdsetup::buildScreens(&w, &s);

    // *** THE FIVE THAT ARE PROCEDURE AND THE ONE THAT IS NOT. *** Five of the six steps
    // are things somebody DOES to the machine, in an order the design argues for in
    // section 3.1, and the walkthrough is the console's statement of exactly that
    // procedure - so their order is asserted. "Which mixer is this for" is a CHOICE and
    // not an act: there is nothing to do about it on the machine, /console has no radio
    // buttons to offer, and a walkthrough step telling somebody to pick a line in a
    // window they are not looking at would name a control that is not there. The console
    // carries it where the console already carries it - printSummary(), as the record of
    // what this run was set up for - and this suite says so rather than either pretending
    // it is in the procedure or dropping it from the six.
    const ConsoleSubject kSubjects[6] = {
        { ::kMixerTitle,   L"USB 2.0 port and switch it on",             true,
          L"the walkthrough" },
        { ::kDeviceTitle,  L"was set up for",                            false,
          L"the summary"     },
        { ::kMidiTitle,    L"created through Windows MIDI Services",     true,
          L"the walkthrough" },
        // *** SPELLED HERE AND NOT bcdsetup::kZadigDownloadPage, WHICH IS WHAT IT USED
        //     TO BE AND WHICH MADE THIS THE ONE CHECK IN THE BLOCK THAT COULD NOT FAIL.
        //     *** Both sides were then the same compiled constant: the walkthrough prints
        // kZadigDownloadPage and the needle WAS kZadigDownloadPage, so changing that
        // constant in common.cpp moved both sides together. It was injected -
        // common.cpp's address rewritten to a different host entirely - and this check
        // stayed GREEN while the console told people to fetch Zadig from somewhere else.
        // The host is written out here, in a file the product does not compile against,
        // so the two sides are finally two things.
        { ::kZadigTitle,   L"zadig.akeo.ie",                             true,
          L"the walkthrough" },
        { ::kBindingTitle, L"Options > List All Devices FIRST",          true,
          L"the walkthrough" },
        { ::kInstallTitle, L"Run this installer and press Install",      true,
          L"the walkthrough" }
    };
    const int kSubjectCount = 6;

    // -------------------------------------------------------------------
    // The denominator, and it is the whole of the "six or nine" question.
    // WHERE EACH SIDE COMES FROM: the left is counted over the table setup.cpp built;
    // the right is the length of the list above, written in this file. Add a seventh
    // step screen and the left becomes 7 while the right stays 6, so the new screen
    // cannot ship without somebody saying what the console prints for it.
    // -------------------------------------------------------------------
    int screens = bcdgui::screenCount(&w);
    int steps   = stepScreenCount(&w);
    _snwprintf(what, 390,
               L"the flow is %d screens and %d of them are the design's STEPS - the other "
               L"%d are the opening, the progress screen and the summary, which section 3 "
               L"counts separately. This suite names %d subjects and asserts THE STEPS",
               screens, steps, screens - steps, kSubjectCount);
    check(steps == kSubjectCount && screens - steps == 3, what);

    // ...and every subject named above is a screen the table really holds, listed in the
    // table's own order. A list that named a screen that had gone, or that drifted out of
    // flow order, would make the order check below measure the wrong thing quietly.
    int  at[6];
    bool allFound  = true;
    bool ascending = true;
    {
        wchar_t list[160];
        list[0] = 0;
        for (int i = 0; i < kSubjectCount; i++) {
            at[i] = screenIndexOfTitle(&w, kSubjects[i].title);
            if (at[i] < 0)
                allFound = false;
            else if (i > 0 && at[i - 1] >= 0 && at[i] <= at[i - 1])
                ascending = false;
            wchar_t one[16];
            _snwprintf(one, 15, i ? L",%d" : L"%d", at[i]);
            one[15] = 0;
            if (wcslen(list) + wcslen(one) < 150)
                wcscat(list, one);
        }
        _snwprintf(what, 390,
                   L"...and each of those %d subjects names a screen the table really "
                   L"holds, in the table's own order - found at %s of %d entries",
                   kSubjectCount, list, screens);
        what[390] = 0;
        check(allFound && ascending, what);
    }

    // -------------------------------------------------------------------
    // What the product really says, captured through the product's own sink.
    //
    // THE CONSOLE ECHO IS OFF for the reason PART 1's branch F records: printSummary()
    // is about a hundred lines of product output, and letting it reach the same stream
    // this harness wprintf()s to is what once split a check NAME across 200 lines.
    // -------------------------------------------------------------------
    int said[6];
    for (int i = 0; i < kSubjectCount; i++)
        said[i] = -1;

    capReset();
    bcdsetup::setLineSink(capSink);
    bcdsetup::setConsoleEcho(false);
    // Unqualified, like testWalkthrough(): setup.cpp says "using namespace bcdsetup" and
    // then defines its own functions as file statics in the GLOBAL namespace.
    printPrerequisiteWalkthrough();
    bcdsetup::setConsoleEcho(true);
    bcdsetup::setLineSink(0);
    int walkLines = g_capN;
    for (int i = 0; i < kSubjectCount; i++)
        if (kSubjects[i].inProcedure)
            said[i] = capIndexOf(kSubjects[i].needle);

    Pending nothingPending;
    ZeroMemory(&nothingPending, sizeof(nothingPending));
    capReset();
    bcdsetup::setLineSink(capSink);
    bcdsetup::setConsoleEcho(false);
    printSummary(&s, &nothingPending, false, L"");
    bcdsetup::setConsoleEcho(true);
    bcdsetup::setLineSink(0);
    int summaryLines = g_capN;
    for (int i = 0; i < kSubjectCount; i++)
        if (!kSubjects[i].inProcedure)
            said[i] = capIndexOf(kSubjects[i].needle);

    capReset();
    bcdsetup::setLineSink(capSink);
    bcdsetup::setConsoleEcho(false);
    printBanner();
    bcdsetup::setConsoleEcho(true);
    bcdsetup::setLineSink(0);
    int  bannerLines = g_capN;
    bool saidCredit  = capHas(bcdgui::kCreditsLine);
    bool saidRepo    = capHas(bcdgui::kRepositoryUrl);
    bool saidNotice1 = capHas(bcdgui::kNotAffiliatedLine1);
    bool saidNotice2 = capHas(bcdgui::kNotAffiliatedLine2);

    // -------------------------------------------------------------------
    // Six checks, one per subject. The screen is named by the title the PRODUCT put in
    // the table, so a renamed screen renames its own failure message.
    // -------------------------------------------------------------------
    wprintf(L"  %d walkthrough lines, %d summary lines, %d banner lines\n",
            walkLines, summaryLines, bannerLines);
    for (int i = 0; i < kSubjectCount; i++) {
        _snwprintf(what, 380,
                   L"/console says step screen %d's subject - '%s' - and %s carries it on "
                   L"line %d", at[i], kSubjects[i].title, kSubjects[i].carriedBy, said[i]);
        what[380] = 0;
        check(said[i] >= 0, what);
    }

    // -------------------------------------------------------------------
    // The order, walked in the TABLE's order and not in this list's.
    //
    // The two are the same today and check 2 above says so; walking the table anyway is
    // what makes this fail when somebody reorders buildScreens() and leaves the console
    // alone. Section 3.1 argues that order once - plug in first, loopMIDI before Zadig
    // because it can ask for a restart, get Zadig before applying it, ours last - and
    // both the screens and the walkthrough are built from that one argument. A reader
    // who used the window and a reader who read the console must not be given two
    // different procedures.
    // -------------------------------------------------------------------
    {
        int order[6];
        for (int k = 0; k < kSubjectCount; k++) {
            int best = -1;
            for (int i = 0; i < kSubjectCount; i++) {
                bool taken = false;
                for (int j = 0; j < k; j++)
                    if (order[j] == i)
                        taken = true;
                if (taken)
                    continue;
                if (best < 0 || at[i] < at[best])
                    best = i;
            }
            order[k] = best;
        }
        int            walked     = 0;
        int            prev       = -1;
        const wchar_t* firstBad   = 0;
        for (int k = 0; k < kSubjectCount; k++) {
            int i = order[k];
            if (i < 0 || !kSubjects[i].inProcedure)
                continue;
            walked++;
            if (said[i] < 0 || said[i] <= prev) {
                if (!firstBad)
                    firstBad = kSubjects[i].title;
            } else {
                prev = said[i];
            }
        }
        _snwprintf(what, 380,
                   L"...and the %d of %d subjects the walkthrough carries come in the "
                   L"TABLE's order, so the window and the console describe one procedure "
                   L"and not two - first out of order: %s",
                   walked, kSubjectCount, firstBad ? firstBad : L"(none)");
        what[380] = 0;
        check(walked == kSubjectCount - 1 && !firstBad, what);
    }

    // -------------------------------------------------------------------
    // The opening, which is not a step, and the banner that is its console counterpart.
    //
    // *** WHAT THIS CAN AND CANNOT CATCH, SAID RATHER THAN IMPLIED. *** Both sides read
    // ONE constant each: printBanner() says kCreditsLine and this asks for kCreditsLine.
    // It therefore catches a line being DROPPED from the banner, which is a real and
    // otherwise unmeasured failure - nothing in this harness looked at printBanner()
    // before this line - and it cannot catch the window and the console disagreeing,
    // because there is nothing for them to disagree with: one pair of strings in gui.cpp
    // is painted by the opening screen and printed by the banner, which is the design's
    // own mechanism for that notice and not something a test has to re-establish.
    // -------------------------------------------------------------------
    _snwprintf(what, 380,
               L"the console BANNER carries the opening screen's own three things - the "
               L"credit (%d), the repository address (%d) and both lines of the non "
               L"affiliation notice (%d,%d) - in %d lines, which is why the opening is "
               L"not one of the six",
               saidCredit ? 1 : 0, saidRepo ? 1 : 0, saidNotice1 ? 1 : 0,
               saidNotice2 ? 1 : 0, bannerLines);
    check(saidCredit && saidRepo && saidNotice1 && saidNotice2, what);
}

// ===========================================================================
// PART 2e3e - THE README DESCRIBES THE PROGRAM THAT EXISTS
//
// *** THIS SUITE IS THE PRICE OF THE README SECTION IT GUARDS. *** "The window" names
// the nine screens one by one, which is the only honest way to describe a wizard - and
// a document that spells nine titles by hand is nine strings nothing keeps equal to the
// program. That is the drift this project has closed a dozen times in code and had left
// wide open in prose. So the section is written in a shape a check can read: every
// screen title appears inside backticks, in flow order, between the "## The window"
// heading and the "### Modes" one.
//
// *** WHY THE HAYSTACK IS BOUNDED TO THAT SECTION, AND IT IS NOT TIDINESS. *** The word
// "Installing" appears in this README in a sentence about ASIO registration, hundreds of
// lines below, and an unbounded search would find that one and read the nine titles out
// of order. The bound is also what makes the order assertion mean "in the section that
// describes the flow" rather than "somewhere in a 700 line file".
//
// WHERE EACH SIDE COMES FROM: the left is the bytes of README.md read off the disk; the
// right is the table buildScreens() filled, walked in its own order. Neither can see the
// other, and the file is not compiled into anything.
// ===========================================================================

// The installer's README, widened byte by byte. Every file in this folder is ASCII with
// LF endings by standing constraint, so a byte IS a character here; a stray non-ASCII
// byte would widen to a character no needle contains, which fails a check rather than
// crashing a search.
//
// *** AND THE CARRIAGE RETURNS ARE DROPPED, BECAUSE WITHOUT THAT THIS SUITE FAILS ON A
//     FRESH CLONE OF THIS REPOSITORY AND WAS MEASURED DOING IT. *** Two needles below are
// LF-anchored - "\n## The window\n" and "\n### Modes\n". Git's `core.autocrlf=true` is the
// Windows default and rewrites text files to CRLF ON CHECKOUT, so on a fresh worktree the
// bytes are "\r\n## The window\r\n" and both needles miss. Measured: 792 CR bytes in the
// file and 856 checks, 3 failures, VERIFY_FAIL, against 856 and 0 from the LF blob. There
// was no .gitattributes in this repository at all; one has been added at the root, which
// is the durable fix for everyone who clones it.
//
// This is the OTHER half, and it is the half that does not depend on a configuration file
// being read: a check whose ability to pass depends on how the person who cloned the
// repository has git set up is a check that reports the state of a machine rather than the
// state of the program. Stripping CR here makes the needles true of the CONTENT, whatever
// wrote it to disk.
static wchar_t* readInstallerReadme(SIZE_T* charsOut)
{
    *charsOut = 0;
    wchar_t path[MAX_PATH];
    if (!installerSourcePath(L"README.md", path, MAX_PATH))
        return 0;
    SIZE_T size = 0;
    BYTE*  raw  = readWholeFile(path, &size);
    if (!raw)
        return 0;
    wchar_t* text = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (size + 1) * sizeof(wchar_t));
    if (text) {
        SIZE_T n = 0;
        for (SIZE_T i = 0; i < size; i++) {
            if (raw[i] == 0x0D)
                continue;
            text[n++] = (wchar_t)(unsigned char)raw[i];
        }
        text[n]   = 0;
        *charsOut = n;
    }
    HeapFree(GetProcessHeap(), 0, raw);
    return text;
}

static void testReadmeDescribesTheScreens()
{
    wprintf(L"\n--- the README describes the screens this program really builds ---\n");

    wchar_t what[400];

    bcdsetup::MachineState s;
    fakeState(&s);
    bcdgui::Wizard w;
    ZeroMemory(&w, sizeof(w));
    bcdsetup::buildScreens(&w, &s);
    int screens = bcdgui::screenCount(&w);

    SIZE_T   chars = 0;
    wchar_t* text  = readInstallerReadme(&chars);

    // The section, bounded by its two headings. A missing file or a missing heading FAILS
    // here: a suite that could not find its haystack and reported nothing would be the
    // "vanishes instead of failing" shape, which reads exactly like a pass.
    wchar_t* from = text ? wcsstr(text, L"\n## The window\n") : 0;
    wchar_t* to   = from ? wcsstr(from,  L"\n### Modes\n")    : 0;
    int sectionLen = (from && to) ? (int)(to - from) : 0;
    _snwprintf(what, 390,
               L"the README has a '## The window' section ending at '### Modes' - %d "
               L"characters read, section %d of them", (int)chars, sectionLen);
    check(text != 0 && from != 0 && to != 0 && sectionLen > 0, what);

    int            found        = 0;
    int            lastPos      = -1;
    bool           ordered      = true;
    const wchar_t* firstMissing = 0;
    const wchar_t* firstOutOfOrder = 0;
    if (from && to) {
        // The section is cut out of the buffer this function owns, so no search can walk
        // past "### Modes" into the rest of the file.
        wchar_t saved = *to;
        *to = 0;
        for (int i = 0; i < screens; i++) {
            // In backticks, which is what makes the needle unambiguous: "Installing" is a
            // word, `Installing` is a name this README uses for a screen. Without the
            // quoting, "Install the driver" and prose about installing would both be
            // candidates for the same needle.
            wchar_t needle[200];
            _snwprintf(needle, 190, L"`%s`", w.screens[i].title);
            needle[190] = 0;
            int p = posOf(from, needle);
            if (p < 0) {
                if (!firstMissing)
                    firstMissing = w.screens[i].title;
                continue;
            }
            found++;
            if (p <= lastPos) {
                ordered = false;
                if (!firstOutOfOrder)
                    firstOutOfOrder = w.screens[i].title;
            } else {
                lastPos = p;
            }
        }
        *to = saved;
    }

    _snwprintf(what, 380,
               L"...and it names every screen the table holds, by the title the program "
               L"paints - %d of %d found; first missing: %s",
               found, screens, firstMissing ? firstMissing : L"(none)");
    what[380] = 0;
    check(found == screens && screens > 0, what);

    _snwprintf(what, 380,
               L"...and it names them in the FLOW's order, so a screen inserted in the "
               L"middle cannot leave the README describing the old sequence - first out "
               L"of order: %s", firstOutOfOrder ? firstOutOfOrder : L"(none)");
    what[380] = 0;
    check(ordered && found == screens, what);

    // *** AND IT DOES NOT REPEAT THE ONE CLAIM ABOUT THIS WINDOW IT HAD WRONG. ***
    // The paragraph under the table said the secondary reads "`Back` on every screen
    // after it". It does not: it reads Cancel on the work screen, always has, and
    // Rule 2 requires it to - the press there stops rather than goes back. Nine
    // per-screen checks were green throughout, because none of them was asked about
    // the SHAPE of the answer; the shape is now asserted off the real controls in the
    // render pass ("the secondary reads Cancel on exactly the screens where..."), and
    // this is the half that stops the sentence coming back.
    //
    // *** IT IS A BLACKLIST OF ONE PHRASE, AND IT HAS THE SAME HOLE THE PANE SEARCH
    //     HAS - THE HOLE IS WORSE HERE BECAUSE THE MESSAGE ASSERTS A NEGATIVE. ***
    // Measured by the closing review: the identical false sentence, re-wrapped across
    // a markdown line break so that "`Back` on every screen" ends one line and
    // "after it" begins the next, gives 920 checks, 0 failures - while this check
    // prints that the README "no longer claims" it. A check that prints a conclusion
    // its search cannot reach is the defect this whole branch exists to remove, so the
    // message now says what it really did: it looked for one spelling and did not find
    // it.
    //
    // It cannot catch a NEW wrong sentence and it cannot catch this one re-wrapped. It
    // catches this one returning verbatim, which is the failure that actually happened,
    // and the product-side half - "the secondary reads Cancel on exactly the screens
    // where its press does not go back", asserted off the real controls in the render
    // pass - is what pins the FACT. This pins the paragraph.
    //
    // WHERE EACH SIDE COMES FROM: the haystack is the README off the disk; the needle
    // is a phrase written here, and it is a phrase the corrected paragraph does not
    // contain.
    check(text != 0 && posOf(text, L"`Back` on every screen after it") < 0,
          L"...and that one false spelling about the secondary button - \"`Back` on "
          L"every screen after it\", on one line - is not in the README. Re-wrapped or "
          L"reworded it would pass; what pins the fact is the render pass's check on "
          L"the real controls");

    if (text)
        HeapFree(GetProcessHeap(), 0, text);
}

// ===========================================================================
// The two download addresses, as the installer's README spells them.
//
// *** THIS EXISTS BECAUSE PART 2e3b's MESSAGE CLAIMED IT AND PART 2e3b NEVER OPENED THE
//     FILE. *** That scan reads seven .cpp and .h files and says "defined once"; the
// review of Task 6d measured a live hand written copy of the loopMIDI address at
// installer\README.md and two more in the repository root's README.md, none of which any
// check had ever read.
//
// *** THE DUPLICATE IS KEPT, AND THE REASON IS WHAT THAT BLOCK OF THE README IS FOR. ***
// It sits under "The three values, and what to do if the download disappears", whose
// whole argument is that a hash and an address published only on somebody's web page are
// a hash and an address nobody has at the moment they matter. The README is read by
// people who never build this code. Deleting the address to satisfy a single definition
// rule would remove the durability that block exists to provide, to protect a property
// nobody was checking.
//
// *** SO IT REPEATS, AND THIS IS WHAT MAKES THE TWO AGE TOGETHER. *** The needle is the
// COMPILED constant out of common.cpp, not a literal typed here: change the address in
// common.cpp and the exact-spelling count in the README drops to zero on the next run.
// And the host is counted separately from the whole address, so a README that keeps the
// site but drifts on the scheme or the path - http for https, an old page name - shows up
// as more hosts than exact spellings instead of passing on a substring.
//
// WHAT THIS DELIBERATELY DOES NOT DEMAND: that the README mention both. It carries the
// loopMIDI page because the three values block promises it; it has never carried the
// Zadig page, and inventing a requirement for one here would be this suite writing
// product decisions instead of measuring them. The rule asserted is the honest one - the
// README must not spell an address the program does not.
// ===========================================================================
// *** THE WHOLE ADDRESS AND NOT A PREFIX OF ONE, WHICH THE FIRST DRAFT OF THIS SUITE GOT
//     WRONG AND AN INJECTION FOUND. *** kZadigDownloadPage is "https://zadig.akeo.ie/",
// which is a PREFIX of every longer address on that site. The injection this check exists
// for - the README growing a spelling the program does not have - was run as
// "https://zadig.akeo.ie/downloads" and the check stayed GREEN, because a plain wcsstr()
// found the constant inside it and counted one exact spelling against one mention of the
// site. That is a check that cannot fail against its own defect, which is the family this
// project has now found ten of.
//
// So an occurrence counts only when what FOLLOWS it cannot be part of the same address.
// The leading side is not guarded and does not need to be: the constant carries its own
// scheme, so there is no character that could precede "https://" and still leave a URL.
//
// *** AND A FULL STOP IS NOT A TAIL CHARACTER ON ITS OWN, WHICH THE FIRST VERSION OF THIS
//     GOT BACKWARDS AND WHICH WOULD HAVE COST A RED THAT MEANT NOTHING. *** '.' was in the
// set unconditionally, so the README writing the address at the end of a sentence -
// "...loopMIDI.html." - stopped counting as a mention of it and the exact-spelling count
// dropped to zero on a README that was perfectly correct. Ending a sentence with a URL is
// the ordinary way to write one, and a check that fires on it teaches people to distrust
// the check. So '.' counts as part of the address only when what comes after it could
// continue an address, which is what tells "loopMIDI.html" from "loopMIDI.html.".
static bool isUrlTailChar(wchar_t c)
{
    if (c >= L'a' && c <= L'z')
        return true;
    if (c >= L'A' && c <= L'Z')
        return true;
    if (c >= L'0' && c <= L'9')
        return true;
    return c == L'/' || c == L'-' || c == L'_' || c == L'~' ||
           c == L'?' || c == L'#' || c == L'=' || c == L'&' || c == L'+' ||
           c == L'%' || c == L':' || c == L'@';
}

// Does the address carry on at this character? A run of dots followed by nothing that can
// be in a URL is punctuation, however many of them there are.
static bool urlContinuesAt(const wchar_t* at)
{
    while (*at == L'.')
        at++;
    return isUrlTailChar(*at);
}

static int countWholeUrl(const wchar_t* text, const wchar_t* url)
{
    int    n   = 0;
    size_t len = wcslen(url);
    for (const wchar_t* at = wcsstr(text, url); at; at = wcsstr(at + 1, url))
        if (!urlContinuesAt(at + len))
            n++;
    return n;
}

static void testReadmeAddressesMatchTheProgram()
{
    wprintf(L"\n--- the README's download addresses are the program's, character for "
            L"character ---\n");

    wchar_t what[400];

    SIZE_T   chars = 0;
    wchar_t* text  = readInstallerReadme(&chars);

    struct ReadmeAddress {
        const wchar_t* full;      // the compiled constant, which is the whole address
        const wchar_t* host;      // the part a drifted spelling would still share
        const wchar_t* who;
        bool           mustBeThere;
        const wchar_t* because;
    };
    // *** ONE ROW NOW, NOT TWO. *** The third party download page this table used to
    // check is gone from the program and from installer/README.md together - see
    // the block over kMidiTitle in setup.cpp.
    const ReadmeAddress kAddresses[1] = {
        { bcdsetup::kZadigDownloadPage, L"zadig.akeo.ie",         L"the Zadig page",
          false, L"this README has never carried it" }
    };

    // From sizeof, not the literal 1 - see the note over the same loop in
    // testAddressIsDefinedOnce().
    const int kCases = (int)(sizeof(kAddresses) / sizeof(kAddresses[0]));
    for (int i = 0; i < kCases; i++) {
        int exact = 0;
        int hosts = 0;
        if (text) {
            exact = countWholeUrl(text, kAddresses[i].full);
            // The HOST count stays a loose substring on purpose, and it is the half that
            // makes the comparison mean anything: a drifted spelling still mentions the
            // site, so it raises this count without raising the one above.
            for (const wchar_t* at = wcsstr(text, kAddresses[i].host); at;
                 at = wcsstr(at + 1, kAddresses[i].host))
                hosts++;
        }
        _snwprintf(what, 380,
                   L"the README spells %s exactly as common.cpp does, or not at all - %d "
                   L"mention(s) of the site, %d of them character for character%s",
                   kAddresses[i].who, hosts, exact,
                   kAddresses[i].mustBeThere ? L", and it has to be there because "
                                               L"the three values block promises it"
                                             : L"");
        what[380] = 0;
        check(text != 0 && hosts == exact &&
              (!kAddresses[i].mustBeThere || exact >= 1), what);
    }

    if (text)
        HeapFree(GetProcessHeap(), 0, text);
}

// ===========================================================================
// PART 2e3f - THE SAME QUESTION OF THE README AT THE ROOT OF THE REPOSITORY
//
// *** THIS FILE IS THE ONE THE WORLD READS, AND IT WAS THE ONE NOTHING READ. *** The
// suite above guards installer\README.md's copy of the loopMIDI address. The final
// review measured TWO MORE hand-typed third party addresses in the repository's root
// README.md - the loopMIDI page and the Zadig page - and no check in this harness had
// ever opened that file. The plan's own words (plans/...:1149) are that a third party
// download address is the one string in this project that must not be typed twice
// unguarded, because the failure mode is not a broken build: it is a person following a
// stale or mistyped address to somebody else's download.
//
// The root README is also the document a stranger meets first. installer\README.md is
// read by people who are already building this; README.md is read by people deciding
// whether to trust it at all, and it is the copy most likely to be pasted into a forum
// post. It is the highest-consequence untyped-once string in this repository and it was
// the only one with no guard on it.
//
// THE RULE IS THE SAME ONE AND IT IS DELIBERATELY THE HONEST ONE: the README must not
// spell an address the program does not. Every mention of the SITE has to be the whole
// address, character for character, as common.cpp defines it. It does not demand that
// the file mention either page - that would be this suite writing an editorial decision
// - but it does demand that the file be readable and carry at least one of the two, so
// that a root README nobody can open cannot pass by having nothing in it.
//
// WHERE EACH SIDE COMES FROM: the haystack is the bytes of the repository's root
// README.md read off the disk, which is compiled into nothing; the needles are the
// COMPILED constants out of common.cpp. Change the address in common.cpp and the exact
// spelling count in this file drops to zero on the next run, while the host count stays
// where it was - which is what makes the two age together instead of drifting.
// ===========================================================================
static bool repoRootPath(const wchar_t* leaf, wchar_t* out, int cap)
{
    wchar_t exe[MAX_PATH];
    DWORD   n = GetModuleFileNameW(0, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return false;
    // Off the file name, then off "verify", then off "installer". What is left is the
    // repository root. One level more than installerSourcePath(), spelled out here
    // rather than hidden in an argument, because a path built by counting separators is
    // a path somebody will get wrong.
    for (int up = 0; up < 3; up++) {
        wchar_t* slash = wcsrchr(exe, L'\\');
        if (!slash)
            return false;
        *slash = 0;
    }
    _snwprintf(out, (size_t)cap - 1, L"%s\\%s", exe, leaf);
    out[cap - 1] = 0;
    return true;
}

// Widened byte by byte with the carriage returns dropped, for the reasons written over
// readInstallerReadme(): the standing constraint is ASCII with LF, and a check whose
// verdict depends on how the reader's git is configured reports the state of a machine
// rather than the state of the repository.
static wchar_t* readRepoRootTextFile(const wchar_t* leaf, SIZE_T* charsOut)
{
    *charsOut = 0;
    wchar_t path[MAX_PATH];
    if (!repoRootPath(leaf, path, MAX_PATH))
        return 0;
    SIZE_T size = 0;
    BYTE*  raw  = readWholeFile(path, &size);
    if (!raw)
        return 0;
    wchar_t* text = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (size + 1) * sizeof(wchar_t));
    if (text) {
        SIZE_T n = 0;
        for (SIZE_T i = 0; i < size; i++) {
            if (raw[i] == 0x0D)
                continue;
            text[n++] = (wchar_t)(unsigned char)raw[i];
        }
        text[n]   = 0;
        *charsOut = n;
    }
    HeapFree(GetProcessHeap(), 0, raw);
    return text;
}

// The repository's root README.md, which is what this file's two callers below and the
// suite after them read. One line, so that "which file" stays a parameter of the reader
// above rather than a second copy of the same twenty lines.
static wchar_t* readRepoRootReadme(SIZE_T* charsOut)
{
    return readRepoRootTextFile(L"README.md", charsOut);
}

static void testRootReadmeAddressesMatchTheProgram()
{
    wprintf(L"\n--- the repository's own README spells the two download addresses the "
            L"way the program does ---\n");

    wchar_t what[400];

    SIZE_T   chars = 0;
    wchar_t* text  = readRepoRootReadme(&chars);

    // *** THE FILE WAS REALLY OPENED, ASKED FIRST AND ON ITS OWN. *** Without this the
    // two counts below are 0 and 0 on a run that could not find the file at all, which
    // is "hosts == exact" and would pass. That is the shape this project has found ten
    // of, and it is one line to close.
    _snwprintf(what, 390,
               L"the repository's root README.md was found and read - %d characters",
               (int)chars);
    what[390] = 0;
    check(text != 0 && chars > 0, what);

    struct RootAddress {
        const wchar_t* full;
        const wchar_t* host;
        const wchar_t* who;
    };
    // *** ONE ROW NOW, NOT TWO. *** The third party download page this table used to
    // check against the root README is gone from the program - it is not a literal
    // this harness can compare a document against any more. Whatever the root
    // README says about that history is its own editorial call and outside this
    // task's file list.
    const RootAddress kAddresses[1] = {
        { bcdsetup::kZadigDownloadPage, L"zadig.akeo.ie",         L"the Zadig page"    }
    };

    // From sizeof, not the literal 1 - see the note over the same loop in
    // testAddressIsDefinedOnce().
    const int kCases = (int)(sizeof(kAddresses) / sizeof(kAddresses[0]));
    int total = 0;
    for (int i = 0; i < kCases; i++) {
        int exact = 0;
        int hosts = 0;
        if (text) {
            // countWholeUrl() and not wcsstr(): kZadigDownloadPage is a PREFIX of every
            // longer address on that site, and the injection that proved it matters is
            // written out over that function.
            exact = countWholeUrl(text, kAddresses[i].full);
            for (const wchar_t* at = wcsstr(text, kAddresses[i].host); at;
                 at = wcsstr(at + 1, kAddresses[i].host))
                hosts++;
        }
        total += hosts;
        _snwprintf(what, 390,
                   L"the root README spells %s exactly as common.cpp does, or not at "
                   L"all - %d mention(s) of the site, %d of them character for character",
                   kAddresses[i].who, hosts, exact);
        what[390] = 0;
        check(text != 0 && hosts == exact, what);
    }

    // *** AND IT REALLY CARRIES THE ONE, WHICH IS WHAT STOPS THE CHECK ABOVE FROM
    //     BEING TRUE OF AN EMPTY FILE. *** hosts == exact holds trivially at 0 and 0.
    // This says the comparison had something to compare.
    _snwprintf(what, 390,
               L"...and it mentions the Zadig site, so the comparison above had "
               L"something to compare - %d mention(s) in total", total);
    what[390] = 0;
    check(total >= 1, what);

    if (text)
        HeapFree(GetProcessHeap(), 0, text);
}

// ===========================================================================
// PART 2e3h - THE LICENCE NOTICE THAT HAS TO TRAVEL WITH SOMEBODY ELSE'S BINARY
//
// *** THIS IS THE ONE SUITE IN THIS FILE WHOSE SUBJECT IS A LEGAL OBLIGATION AND NOT
//     A DEFECT. *** BCD3000Setup.exe embeds Microsoft's Windows.Devices.Midi2.dll as
// resource 105. It is MIT licensed, and MIT's single condition is that "the above
// copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software". Embedding is copying; handing somebody the
// installer is distributing that copy. Omit the notice and the product is being
// distributed without permission - which is EXACTLY the state this whole migration
// existed to leave: the library this one replaced came with an SDK header saying
// unauthorized distribution is prohibited, and that is what stopped publication.
//
// So there is no "it still works" consolation available here. Everything else this
// harness measures is about a program being wrong; this is about a product not being
// publishable. It is worth its own suite for that reason alone.
//
// WHERE EACH SIDE COMES FROM, and there are three pairs:
//
//   1. The repository's LICENSE, read off the disk, against needles TYPED IN THIS
//      FILE. This file is not compiled into LICENSE and LICENSE is compiled into
//      nothing, so the two cannot move together. Change a character of the notice in
//      LICENSE and the needle stops matching.
//
//   2. The notice the PROGRAM prints, captured through the product's own line sink,
//      against the bytes of LICENSE. This is the pair that matters most and the one
//      neither document could have on its own: a LICENSE file is no use to somebody
//      handed only BCD3000Setup.exe, and a notice inside the binary that says
//      something different from the repository's is two contradictory statements of
//      the same permission. Neither side is a literal here - one is compiled
//      setup.cpp, the other is a file on disk - so this fails if EITHER drifts.
//
//   3. The two READMEs, against the file name and the word LICENSE. A reader has to
//      be able to find the notice from the document they happen to open.
//
// The installer's OWN copy inside the built executable is checked separately, in the
// "exe" mode, because that needs a built binary. Both halves exist on purpose: this
// one fails on a fresh clone with no payloads, that one fails on a binary that was
// never rebuilt.
// ===========================================================================
static void testMitNoticeTravelsWithTheBinary()
{
    wprintf(L"\n--- the MIT notice for the one redistributed binary ---\n");

    wchar_t what[500];

    SIZE_T   chars = 0;
    wchar_t* text  = readRepoRootTextFile(L"LICENSE", &chars);

    // Asked first and on its own, for the reason the root README suite gives: without
    // it every posOf() below is -1 on a run that could not open the file at all, and a
    // suite that reports "the notice is missing" when the file simply was not found
    // sends the reader to fix the wrong thing.
    _snwprintf(what, 490, L"the repository's LICENSE was found and read - %d characters",
               (int)chars);
    what[490] = 0;
    check(text != 0 && chars > 0, what);

    // The MIT licence, in pieces, so that a failure names WHICH sentence went. A single
    // needle spanning the whole text would fail identically for a missing copyright line
    // and for a reflowed paragraph, and those need different fixes.
    struct Needle {
        const wchar_t* text;
        const wchar_t* why;
    };
    static const Needle kMustBeThere[] = {
        { L"Copyright (c) Microsoft Corporation.",
          L"the copyright notice MIT requires to be included" },
        { L"Windows.Devices.Midi2.dll",
          L"the name of the file the obligation is attached to" },
        { L"github.com/microsoft/MIDI",
          L"where it came from, so the reader can check the terms at the source" },
        { L"Permission is hereby granted, free of charge, to any person obtaining a copy",
          L"the permission notice itself, in full rather than by reference" },
        { L"The above copyright notice and this permission notice shall be included in "
          L"all",
          L"the condition that makes this whole section compulsory" },
        { L"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND",
          L"the warranty disclaimer that completes the MIT text" }
    };
    const int kNeedleCount = (int)(sizeof(kMustBeThere) / sizeof(kMustBeThere[0]));
    for (int i = 0; i < kNeedleCount; i++) {
        int at = text ? posOf(text, kMustBeThere[i].text) : -1;
        _snwprintf(what, 490, L"LICENSE carries %s - found at %d",
                   kMustBeThere[i].why, at);
        what[490] = 0;
        check(text != 0 && at >= 0, what);
    }

    // *** AND THE ENTRY FOR THE DEPENDENCY THIS PRODUCT NO LONGER HAS IS GONE. *** It
    // described a run-time dependency loaded from the system directory, which stopped
    // being true when the port moved to Windows MIDI Services. A licence file that
    // describes a dependency the product does not have is not a small inaccuracy: it is
    // the file a lawyer, a distributor or a packager reads to decide what this product
    // contains.
    {
        int at = text ? posOf(text, L"teVirtualMIDI") : -1;
        _snwprintf(what, 490,
                   L"...and LICENSE no longer describes the third party MIDI driver this "
                   L"product stopped using - \"teVirtualMIDI\" is at %d, where -1 is the "
                   L"only passing answer", at);
        what[490] = 0;
        check(text != 0 && at < 0, what);
    }

    // -------------------------------------------------------------------
    // PAIR 2: what the PROGRAM says, against what the repository says.
    //
    // printThirdPartyNotice() is captured through the product's own sink, exactly as
    // testConsoleCarriesTheSubjects() captures the walkthrough, and every non-blank line
    // it produced has to occur in LICENSE. Neither side is a literal in this file.
    //
    // The console echo is off for the reason PART 1's branch F records: product output
    // on the harness's own stream splits check names across the screen.
    // -------------------------------------------------------------------
    capReset();
    bcdsetup::setLineSink(capSink);
    bcdsetup::setConsoleEcho(false);
    printThirdPartyNotice();
    bcdsetup::setConsoleEcho(true);
    bcdsetup::setLineSink(0);
    const int noticeLines = g_capN;

    int            said     = 0;   // lines that are STATEMENTS OF the notice
    int            framing  = 0;   // lines that are ABOUT it - this program's own words
    int            missing  = 0;
    const wchar_t* firstBad = 0;
    for (int i = 0; i < noticeLines; i++) {
        const wchar_t* line = g_cap[i];
        while (*line == L' ')
            line++;
        if (!*line)
            continue;
        // Four lines are the program's own FRAMING rather than part of the notice: the
        // console section rule it prints above every block, and the three that point the
        // reader at LICENSE and state this project's own copyright. They are ABOUT the
        // notice, so they are not required to be in LICENSE - and they are named here by
        // their words rather than skipped by index, because an index would silently start
        // excusing a different line the moment somebody edits the paragraph.
        if (wcsstr(line, L"--- third party software redistributed by this installer") ||
            wcsstr(line, L"The same notice is in this project's LICENSE file") ||
            wcsstr(line, L"redistributed by this project\". Everything else") ||
            wcsstr(line, L"project's own work and is MIT licensed too")) {
            framing++;
            continue;
        }
        said++;
        if (!text || posOf(text, line) < 0) {
            missing++;
            if (!firstBad)
                firstBad = g_cap[i];
        }
    }
    _snwprintf(what, 490,
               L"every word of the notice the INSTALLER prints is also in LICENSE - %d "
               L"lines printed, %d of them statements of the notice, %d not found in "
               L"LICENSE%s%s",
               noticeLines, said, missing, missing ? L", first: " : L"",
               missing && firstBad ? firstBad : L"");
    what[490] = 0;
    check(text != 0 && missing == 0, what);

    // ...and it really printed something. missing == 0 is true of a function that emits
    // nothing at all, which is the shape this project has found ten of. This is a
    // different question from the one above: "did it print real content" does not care
    // whether a given line is a statement of the notice or this program's own framing
    // around it, so it counts BOTH (said + framing), not said alone. said alone is 19 on
    // this exact text - correct for the "statements of the notice" wording above, but it
    // would make this floor check fail on a notice that is not a stub, for a reason that
    // has nothing to do with being a stub.
    const int contentLines = said + framing;
    _snwprintf(what, 490,
               L"...and the installer's notice is a whole licence and not a line - %d "
               L"lines, of which %d carry text, floor 20", noticeLines, contentLines);
    what[490] = 0;
    check(noticeLines >= 20 && contentLines >= 20, what);

    // -------------------------------------------------------------------
    // PAIR 3: a reader can find the notice from either README.
    // -------------------------------------------------------------------
    // *** "pointed" LOOKS FOR A LINK TO THE FILE, NOT JUST THE WORD. *** A bare
    // substring L"LICENSE" also matches the unrelated
    // native/bcdasio/LICENSE-asiosample.txt that the root README names two lines below
    // its own MIT bullet - so a needle that stopped at the word would have passed a
    // README whose only "LICENSE" was that OTHER file's name, having proven nothing
    // about this one. Both READMEs point at the real file with a markdown link, and
    // each uses a different relative path to it - the needle below is that link,
    // reproduced exactly, not the bare word.
    struct Doc {
        const wchar_t* leaf;
        const wchar_t* who;
        const wchar_t* licenseLink;  // the markdown link this doc actually uses
    };
    static const Doc kDocs[2] = {
        { L"README.md",            L"the repository's root README", L"](LICENSE)" },
        { L"installer\\README.md", L"the installer's README",       L"](../LICENSE)" }
    };
    for (int i = 0; i < 2; i++) {
        SIZE_T   docChars = 0;
        wchar_t* doc      = readRepoRootTextFile(kDocs[i].leaf, &docChars);
        int      named    = doc ? posOf(doc, L"Windows.Devices.Midi2.dll") : -1;
        int      pointed  = doc ? posOf(doc, kDocs[i].licenseLink) : -1;
        int      whose    = doc ? posOf(doc, L"Microsoft Corporation") : -1;
        _snwprintf(what, 490,
                   L"%s names the redistributed file (%d), says whose it is (%d) and "
                   L"points at LICENSE (%d) - %d characters read",
                   kDocs[i].who, named, whose, pointed, (int)docChars);
        what[490] = 0;
        check(doc != 0 && docChars > 0 && named >= 0 && whose >= 0 && pointed >= 0, what);
        if (doc)
            HeapFree(GetProcessHeap(), 0, doc);
    }

    if (text)
        HeapFree(GetProcessHeap(), 0, text);
}

// ===========================================================================
// PART 2e3f2 - /replace-service HAS TO DO SOMETHING WHEN THE BRIDGE ITSELF
// IS UNTOUCHED AND ONLY A SIDE DLL DIFFERS. THIS IS THE ORDERING BUG.
//
// *** THE DEFECT, IN ONE SENTENCE. *** The two side DLLs used to be decided in
// a loop that ran AFTER the control service's own three-way branch, reading a
// serviceStillRunning flag that branch only cleared on the path where it
// itself stopped the service - which only happened when the BRIDGE differed.
// So a machine whose BCD3000Bridge.exe was byte-identical but whose
// BcdMidi.dll or Windows.Devices.Midi2.dll differed printed the refuse
// message and named /replace-service - and running /replace-service re-entered
// the SAME "bridge unchanged, nothing to stop" branch, so nothing changed.
// The switch was recommended on exactly the machine where it was a no-op.
//
// THE FIX moved the decision to planServiceInstall(differsMask, running,
// replaceAllowed): it looks at whether the SET of differing files is empty,
// never at which bit is set. That is precisely the property this suite
// checks FOR - a differsMask with the bridge bit CLEAR and only a side bit
// set has to behave identically to one with the bridge bit set, once
// replaceAllowed is true.
//
// WHERE EACH SIDE OF EVERY COMPARISON BELOW COMES FROM. The left side is
// ::planServiceInstall() - the product's own function - called with the
// exact three arguments installControlService() passes it (differsMask,
// s->bridge.running, opt->replaceBridge), never re-derived here. The right
// side is a ServicePlan literal (kServicePlanRefuse / kServicePlanWrite)
// typed into this file. Neither side is built from the other: a table of
// expected answers assembled from the same bitmask arithmetic as the
// function under test would agree with itself on every input, including the
// one this suite exists to catch. This project has been bitten by exactly
// that shape of check nine times before.
//
// installControlService() ITSELF IS NEVER CALLED. It writes files and can
// stop a running service, which is exactly what this harness must never do
// (see the file header). planServiceInstall() is the pure function the brief
// asked to be driven directly for that reason: it is reachable, it is the
// whole of the decision, and it is where the ordering bug actually lived
// once the restructure moved the decision out of the per-file loop.
// ===========================================================================
static void testReplaceServiceActuallyReplacesASideFile()
{
    wprintf(L"\n--- /replace-service on the path the ordering bug broke ---\n");

    wchar_t what[400];

    // kServiceFiles[0] is the bridge; [1] is BcdMidi.dll; [2] is
    // Windows.Devices.Midi2.dll (see the table above installControlService()).
    // Bit 0 is deliberately left CLEAR in both masks below - the bridge is
    // byte-identical - because that is the exact machine the defect broke.
    const unsigned kMidiDllOnly    = (1u << 1);
    const unsigned kWinMidiDllOnly = (1u << 2);

    ServicePlan p1 = ::planServiceInstall(kMidiDllOnly, true, false);
    _snwprintf(what, 390,
               L"a side DLL alone differs, service running, WITHOUT /replace-service "
               L"still refuses (the safe default) - plan %d, wanted %d (Refuse)",
               (int)p1, (int)kServicePlanRefuse);
    what[390] = 0;
    check(p1 == kServicePlanRefuse, what);

    // *** THIS IS THE CHECK THAT WOULD HAVE CAUGHT THE DEFECT. *** Same
    // differsMask as above - the bridge is still untouched - only
    // replaceAllowed flips from false to true. Under the old code this stayed
    // Refuse forever, because nothing about /replace-service reached a
    // decision gated on the bridge bit alone. Under the fix, the answer
    // depends only on whether the SET is empty, so /replace-service now does
    // something on exactly the machine that recommended it.
    ServicePlan p2 = ::planServiceInstall(kMidiDllOnly, true, true);
    _snwprintf(what, 390,
               L"...and WITH /replace-service the SAME differsMask now writes - plan "
               L"%d, wanted %d (Write), on the exact machine that used to print the "
               L"same refusal for ever",
               (int)p2, (int)kServicePlanWrite);
    what[390] = 0;
    check(p2 == kServicePlanWrite, what);

    // The OTHER side file alone, so the assertion is not an artefact of which
    // index happens to be BcdMidi.dll - the report is explicit that BOTH DLLs
    // shared the broken loop.
    check(::planServiceInstall(kWinMidiDllOnly, true, false) == kServicePlanRefuse,
          L"...and the SAME is true with Windows.Devices.Midi2.dll alone out of "
          L"date instead - refused without the switch");
    check(::planServiceInstall(kWinMidiDllOnly, true, true) == kServicePlanWrite,
          L"...and written WITH the switch - both side files, not just one, escape "
          L"the old dead end");

    // *** THE INVARIANT THE CODE COMMENT OVER planServiceInstall() NAMES BY
    //     NAME, MADE EXECUTABLE. *** "There is NO non-empty differsMask for
    // which /replace-service is refused." Exhaustive over every non-empty
    // subset of the three files - not just the two single-bit cases above -
    // because the defect was precisely a subset (a side DLL alone) the old
    // code never enumerated as its own case.
    int      violations      = 0;
    unsigned firstViolation  = 0;
    const unsigned kAllMasks = (1u << kServiceFileCount) - 1;
    for (unsigned mask = 1; mask <= kAllMasks; mask++) {
        if (::planServiceInstall(mask, true, true) == kServicePlanRefuse) {
            if (violations == 0)
                firstViolation = mask;
            violations++;
        }
    }
    _snwprintf(what, 390,
               L"...and NO non-empty set of differing files stays refused once "
               L"/replace-service is given - %d of %d non-empty subsets still "
               L"refused (first, if any: 0x%X)",
               violations, (int)kAllMasks, firstViolation);
    what[390] = 0;
    check(violations == 0, what);

    // And the untouched case, so this suite also names what must NOT change:
    // nothing differing must never be turned into a write by the switch, on a
    // running OR a stopped service - the switch replaces files that differ,
    // it does not force a rewrite of files that are already correct.
    check(::planServiceInstall(0, true, true) == kServicePlanNothingToDo,
          L"...and an empty set stays NothingToDo even WITH the switch and the "
          L"service running - /replace-service does not force needless writes");
    check(::planServiceInstall(0, false, false) == kServicePlanNothingToDo,
          L"...and the same is true with the service already stopped");
}

// ===========================================================================
// PART 2e3g - THE THREE STRINGS A USER MATCHES AGAINST THEIR OWN HARDWARE
//
// *** THEY ARE HAND-TYPED PROSE AND UNTIL NOW THEY WERE TIED TO NOTHING. *** The
// program tells the reader to confirm the device by the USB ID "1397 00BF", to pick the
// list line "BCD3000 (Interface 0)", and to select "Behringer BCD3000" in their DJ
// software's ASIO list. Those are the three moments where the user compares this
// program's words against their own machine, and two of them are on the step this
// installer itself calls the most dangerous operation in the whole installation.
//
// Every one of them is a literal typed into a say() or into a bullet. The SOURCES OF
// TRUTH are elsewhere and are properly tied to each other already: kModelVids and
// kModelPids are checked against kUsbFunctionPrefix and against the driver's own
// profile table, and kAsioRegName is checked against what the installer registers. What
// nothing checked was the PROSE. It was existence-checked - "this sentence contains an
// ID" - and an existence check cannot tell 00BF from 00BD.
//
// So: change kModelPids[0], or rename the ASIO device, and every check in this harness
// stays green while four screens tell somebody to confirm a USB ID that is not theirs,
// or to look for an ASIO device that is not in their list. On the binding step being
// wrong costs them the use of the hardware.
//
// *** THE PROSE IS NOT INTERPOLATED, AND THAT IS DELIBERATE. *** Rewriting six say()
// calls to build their strings at run time would put a format specifier in the middle
// of the one paragraph this program says must never be duplicated or gone wrong, and
// would make the walkthrough's text depend on a table lookup. Asserting equality costs
// nothing at run time and fails in the same place either way - here, loudly, naming the
// count.
//
// WHERE EACH SIDE COMES FROM: the needle is COMPOSED AT RUN TIME from modelVid(),
// modelPid(), modelName() and kAsioRegName - numbers and a name out of common.cpp's
// table; the haystack is the bytes of the installer's own sources read off the disk, as
// text, which is where the hand-typed copies live. Neither is derived from the other,
// and the source files are compiled into nothing this test can see.
// ===========================================================================

// The installer's sources are ASCII by standing constraint. Narrow a wide constant so
// it can be counted in them, and REFUSE rather than mangle if it is not ASCII: a needle
// silently truncated at a non-ASCII byte is a needle that matches nothing and a check
// that passes for the wrong reason.
static bool narrowAscii(const wchar_t* w, char* out, int cap)
{
    int n = 0;
    for (; w[n]; n++) {
        if (n >= cap - 1 || w[n] > 127 || w[n] < 32)
            return false;
        out[n] = (char)w[n];
    }
    out[n] = 0;
    return n > 0;
}

static void testHardwareStringsMatchTheModelTable()
{
    wprintf(L"\n--- the strings the user matches against their own hardware ---\n");

    wchar_t what[400];

    // ---------------------------------------------------------------
    // 1 - THE USB ID, AS THE SCREENS AND THE WALKTHROUGH SPELL IT.
    //
    // "%04X %04X" is Zadig's own layout of that row - two four digit fields with a
    // space between them - and it is what the prose was written to match. The numbers
    // come from the model table, so the needle changes the moment the table does.
    // ---------------------------------------------------------------
    {
        char needle[64];
        _snprintf(needle, 60, "%04X %04X",
                  (unsigned)bcdsetup::modelVid(bcdsetup::kModelBcd3000),
                  (unsigned)bcdsetup::modelPid(bcdsetup::kModelBcd3000));
        needle[60] = 0;
        int inCommon = 0, elsewhere = 0, filesRead = 0;
        wchar_t firstCopy[120];
        countAddressSpellings(needle, &inCommon, &elsewhere, &filesRead, firstCopy, 120);
        _snwprintf(what, 390,
                   L"the USB ID the reader is told to confirm is the one the model table "
                   L"names - \"%hs\" rebuilt from modelVid/modelPid, found %d time(s) in "
                   L"the prose of %d sources (first: %s)",
                   needle, elsewhere, filesRead,
                   firstCopy[0] ? firstCopy : L"(none)");
        what[390] = 0;
        // FOUR is not a style budget, it is the anti-vacuity floor: the Zadig step, the
        // picture's caveat, the binding bullet and the binding screen's caption each
        // carry it, and a run that finds fewer has lost one of those four to a rewording
        // - which is the drift this exists to catch, in the other direction.
        check(filesRead == 7 && elsewhere >= 4, what);
    }

    // ---------------------------------------------------------------
    // 2 - THE LINE IN ZADIG'S LIST, WHICH IS THE MODEL NAME PLUS THE INTERFACE.
    //
    // Interface 0 is written here and not derived: kUsbFunctionPrefix ends at "MI_" and
    // the "00" that follows it is spelled in kUsbEnumKey, which is asserted against the
    // prefix elsewhere. What is derived is the part that identifies the MIXER, which is
    // the part the model table owns and the part that would change if it did.
    // ---------------------------------------------------------------
    {
        char model[64];
        char needle[96];
        if (!narrowAscii(bcdsetup::modelName(bcdsetup::kModelBcd3000), model, 64)) {
            check(false, L"the proven model's name is ASCII, so it can be counted in "
                         L"sources that are ASCII by standing constraint");
        } else {
            _snprintf(needle, 90, "%s (Interface 0)", model);
            needle[90] = 0;
            int inCommon = 0, elsewhere = 0, filesRead = 0;
            wchar_t firstCopy[120];
            countAddressSpellings(needle, &inCommon, &elsewhere, &filesRead,
                                  firstCopy, 120);
            _snwprintf(what, 390,
                       L"the list line the reader is told to pick names the model the "
                       L"table names - \"%hs\" rebuilt from modelName(), found %d time(s) "
                       L"in the prose of %d sources (first: %s)",
                       needle, elsewhere, filesRead,
                       firstCopy[0] ? firstCopy : L"(none)");
            what[390] = 0;
            check(filesRead == 7 && elsewhere >= 3, what);
        }
    }

    // ---------------------------------------------------------------
    // 3 - THE ASIO DEVICE NAME, WHICH IS THE ONE THE USER LOOKS FOR IN SOMEBODY ELSE'S
    //     DROP-DOWN.
    //
    // Here the needle is the published constant itself and the haystack is deliberately
    // `elsewhere` - the copies OUTSIDE common.cpp. Counting common.cpp would be counting
    // the definition against itself, which is the family of comparison this file has
    // found nine of. What is asserted is that the PROSE spells what the installer
    // registers: rename the device and the prose stops matching, in this direction and
    // in the other one.
    // ---------------------------------------------------------------
    {
        char needle[96];
        if (!narrowAscii(bcdsetup::kAsioRegName, needle, 96)) {
            check(false, L"the ASIO device name is ASCII");
        } else {
            int inCommon = 0, elsewhere = 0, filesRead = 0;
            wchar_t firstCopy[120];
            countAddressSpellings(needle, &inCommon, &elsewhere, &filesRead,
                                  firstCopy, 120);
            _snwprintf(what, 390,
                       L"the ASIO device name the reader is told to select is the one "
                       L"this installer registers - \"%hs\", defined %d time(s) in "
                       L"common.cpp and spelled %d time(s) in the prose (first: %s)",
                       needle, inCommon, elsewhere,
                       firstCopy[0] ? firstCopy : L"(none)");
            what[390] = 0;
            check(filesRead == 7 && inCommon >= 1 && elsewhere >= 1, what);
        }
    }
}

// ===========================================================================
// PART 2e3h1 - THE INSTALL SCREEN'S FOOTER, ON THE PATH THAT MAKES IT FALSE
//
// *** THE SENTENCE IS "Nothing here has been touched ... all that has happened so far",
//     AND IT IS PAINTED ON THE SCREEN IMMEDIATELY BEFORE THE PRESS THAT INSTALLS. ***
// On a machine where the reader took the loopMIDI offer two screens earlier, winget has
// run Tobias Erichsen's installer and that installer carries a kernel mode driver -
// which this program itself says, in those words, on the screen the button stands on.
// Both halves of the footer were false there.
//
// *** IT IS CHECKED HERE AND NOT IN A CAPTURE, AND THE REASON IS THE ONE THE REVIEW
//     NAMED. *** The render pass photographs ONE invented machine and on that machine
// the offer has not been taken, so the second wording would appear in no image however
// many captures were added - the same structural blindness that let the absent-control
// sentence live in a pane for fourteen instances. What CAN be measured is the function
// that chooses, over both states, which is what this does.
//
// WHERE EACH SIDE COMES FROM: the left is what the product's own reviewFooterFor()
// returns for a Run this suite built and zeroed itself - setup.cpp's Run and its
// selector both live at the translation unit's global scope, beside runWindowed(),
// which is why neither carries the bcdsetup:: prefix the rest of this file uses; the right is phrases written in
// this file. setup.cpp cannot see them, and this file does not spell either footer.
// ===========================================================================
static void testInstallFooterIsTheReassurance()
{
    wprintf(L"\n--- the install screen's footer, and that the screen really asks for "
            L"it ---\n");

    wchar_t what[400];

    // *** THIS SUITE USED TO BE testInstallFooterAfterTheOffer() AND HALF OF IT WAS
    //     TESTING ITS OWN FIXTURE. *** Four of its six checks drove
    //     reviewFooterFor() with `afterOffer.thirdPartyStarted = true` - a field
    // nothing in the product wrote - and asserted the second footer that answered.
    // Two of the three writers of that field in the whole tree were in this
    // function. The field, the second footer and those four checks are gone
    // together; see the block where the field was declared in setup.cpp.
    //
    // What is left is the two facts that are still about the program: what the
    // selector answers, and that the screen really asks it. The anti-vacuity check
    // that used to guard "the two really are two" is gone WITH the second sentence -
    // there is one answer now, so there is no pair for it to be about, and keeping a
    // check whose subject is a second constant that does not exist would be the same
    // mistake one level down.
    static ::Run quiet;
    ZeroMemory(&quiet, sizeof(quiet));
    const wchar_t* before = ::reviewFooterFor(&quiet);

    _snwprintf(what, 390,
               L"the install screen gives the reassurance - it says nothing has been "
               L"touched and that closing the window changes nothing");
    what[390] = 0;
    check(before != 0 &&
          posOf(before, L"Nothing here has been touched") >= 0 &&
          posOf(before, L"all that has happened so far") >= 0 &&
          posOf(before, L"changes nothing") >= 0, what);

    // ===================================================================
    // *** AND THE SCREEN REALLY ASKS THE SELECTOR, WHICH NOTHING ABOVE SAYS. ***
    //
    // Everything so far is about reviewFooterFor(). A correct selector that nothing
    // calls is a fix that is not shipped, and the re-review proved the cost by
    // measurement: with `w->reviewFooter = reviewFooterFor(run);` deleted from
    // rebuildScreens(), the whole suite was 906 checks, 0 failures, VERIFY_OK, while
    // the product went on painting the false sentence. Four passing checks about a
    // function nobody calls.
    //
    // THE SENTINEL IS THE POINT. A zeroed Wizard's footer is null, and "does not
    // contain the false claim" is TRUE of a null - so a check written against a
    // zeroed field would pass on the deleted line. The field is seeded with a string
    // this file owns instead, so the deletion leaves something that is neither
    // footer and every clause below fails.
    //
    // WHERE EACH SIDE COMES FROM: the left is what the product's own rebuildScreens()
    // wrote into a Wizard this suite allocated and seeded; the right is phrases
    // written here plus a sentinel setup.cpp cannot produce.
    // ===================================================================
    static const wchar_t* const kSentinel = L"(nothing wrote this footer)";

    {
        static ::Run untouched;
        ZeroMemory(&untouched, sizeof(untouched));
        fakeState(&untouched.state);

        bcdgui::Wizard w;
        ZeroMemory(&w, sizeof(w));
        w.reviewFooter = kSentinel;
        ::rebuildScreens(&w, &untouched);

        _snwprintf(what, 390,
                   L"...and rebuildScreens() really WRITES it - the sentinel this file "
                   L"seeded the field with is gone and the reassurance is in its place, "
                   L"so deleting the assignment is red rather than silent");
        what[390] = 0;
        check(w.reviewFooter != 0 && w.reviewFooter != kSentinel &&
              posOf(w.reviewFooter, L"Nothing here has been touched") >= 0, what);
    }
}

// ===========================================================================
// PART 2e3h - THE VERSION, WHICH IS FOUR INTEGERS AND TWO STRINGS AND NOTHING TIED
//             ANY OF THEM TOGETHER
//
// version.h publishes BCD_VERSION_MAJOR/MINOR/PATCH/BUILD as integers and
// BCD_VERSION_STR / BCD_VERSION_WSTR as hand-typed strings, side by side, with nothing
// deriving one from the other. The integers feed FILEVERSION in all three .rc files -
// which is what Windows shows in a file's properties. The strings feed the console
// banner, THE HEADER BAND ON EVERY ONE OF THE NINE SCREENS, the install manifest and
// the uninstaller's banner - which is what a person sees.
//
// Bump MAJOR alone and the same binary reports 2.0.0.0 in its properties and 1.0.0 in
// its window, and BCD_VERSION appeared NOWHERE in this harness, so 887 checks had
// nothing to say about it. A version that disagrees with itself is what a support
// request cannot recover from: the number in the screenshot and the number in the file
// are different numbers.
//
// WHERE EACH SIDE COMES FROM: the left is composed here from the three INTEGERS; the
// right is the STRING somebody typed beside them. They are two macros in one header and
// they are genuinely two values - which is the whole finding.
// ===========================================================================
static void testVersionAgreesWithItself()
{
    wprintf(L"\n--- the version string and the version integers ---\n");

    wchar_t what[400];

    wchar_t builtW[64];
    _snwprintf(builtW, 60, L"%d.%d.%d",
               BCD_VERSION_MAJOR, BCD_VERSION_MINOR, BCD_VERSION_PATCH);
    builtW[60] = 0;
    _snwprintf(what, 390,
               L"the wide version string is the three version integers - %d.%d.%d "
               L"rebuilds as \"%s\" and version.h publishes \"%s\"",
               BCD_VERSION_MAJOR, BCD_VERSION_MINOR, BCD_VERSION_PATCH,
               builtW, BCD_VERSION_WSTR);
    what[390] = 0;
    check(wcscmp(builtW, BCD_VERSION_WSTR) == 0, what);

    // The narrow spelling is a THIRD independent literal, and it is the one the install
    // manifest writes, so a machine's record of what is installed hangs on it.
    char builtA[64];
    _snprintf(builtA, 60, "%d.%d.%d",
              BCD_VERSION_MAJOR, BCD_VERSION_MINOR, BCD_VERSION_PATCH);
    builtA[60] = 0;
    _snwprintf(what, 390,
               L"...and so is the narrow one, which is a separate literal beside it - "
               L"rebuilt \"%hs\" against published \"%hs\"", builtA, BCD_VERSION_STR);
    what[390] = 0;
    check(strcmp(builtA, BCD_VERSION_STR) == 0, what);

    // *** AND THE BUILD FIELD IS NOT IN EITHER STRING, WHICH IS WHY THE TWO ABOVE ARE
    //     THREE FIELDS AND NOT FOUR. *** FILEVERSION carries all four; the published
    // strings carry three by design. Asserted rather than assumed, so that a round which
    // starts using BUILD has to decide what the window says about it instead of the
    // window quietly going on saying three quarters of the version.
    _snwprintf(what, 390,
               L"...and the fourth field is still the one nothing publishes - "
               L"BCD_VERSION_BUILD is %d, and neither string carries a fourth part",
               BCD_VERSION_BUILD);
    what[390] = 0;
    check(BCD_VERSION_BUILD == 0, what);
}

// ===========================================================================
// PART 2e4 - NO SENTENCE ON A SCREEN NAMES A CONTROL THAT SCREEN DOES NOT SHOW
//
// *** THIS IS THIS PROJECT'S SIGNATURE DEFECT AND THE THIRTEENTH INSTANCE OF IT WAS
//     FOUND IN THE OWNER'S SCREENSHOT, IN THE SAME BLOCK OF PROSE TASK 6d REWRITES. ***
// The MIDI port screen's second bullet read "It comes inside loopMIDI, Tobias Erichsen's
// program. THE BUTTON BELOW starts HIS installer, unelevated, so Windows names him in its
// own prompt." On his machine there is no such button - only "Check again" - because
// teVirtualMIDI is present and chooseLoopMidiOffer() returns kActionNone. The text
// pointed at a control the screen does not show.
//
// It was invisible to this harness for a reason worth writing down: the render pass
// photographs ONE invented machine, and on that machine the button IS there. The defect
// lives in the states that are not photographed, which is why this is asked of the TABLE
// over several machines rather than of a picture.
//
// *** IT IS A NEEDLE LIST AND THEREFORE A BLACKLIST, WHICH THIS FILE NORMALLY REFUSES,
//     AND THE REASON IT IS ACCEPTED HERE IS THAT THE ALTERNATIVE DOES NOT EXIST. ***
// Rule 2's own check became a whitelist because a button's label is drawn from a small
// closed set of words. Prose has no such set. What CAN be listed is the small number of
// ways English points at a control on the same screen - "the button below", "the button
// on this screen", "press the button" - and every one of them is a phrase somebody writes
// deliberately. A phrase nobody thought of is not caught, and saying so is better than
// implying otherwise.
//
// *** AND INSTANCE FOURTEEN WAS FOUND BY THE FINAL REVIEW, ON THE SAME SCREEN, WHILE
//     THIS CHECK WAS GREEN. *** printLoopMidiStepBody() said "- the button in the bottom
// left corner", and buildScreens() assigns that text to the MIDI port screen's
// Screen::paneText: it is PAINTED IN THE WINDOW, four lines under the bullet that had
// just been fixed. Two holes let it through and both are closed here.
//
//   1. THE HAYSTACK WAS bullets AND addressLead AND NOT paneText. A pane is prose on a
//      screen exactly as a bullet is, it is the LONGEST prose on any screen in this
//      program, and it was the one field this walk did not read. Instance thirteen was a
//      bullet and the guard was fitted to it; the twin sat 1,400 lines away in the pane
//      of the very same screen and was invisible by construction.
//   2. THE NEEDLE LIST DID NOT CONTAIN A CORNER. Five phrases, all of them "button" plus
//      a direction. "the button in the bottom left corner" names a POSITION, which is the
//      other way English points at a control, and it is the worse of the two here because
//      it can be wrong in a second way: that corner really does hold a button on that
//      screen - "Check again" - so the sentence is false even when a button IS present.
//
// ===========================================================================
// *** WHAT THIS CHECK IS, IN ONE SENTENCE, AND IT IS NOT WHAT ITS NAME SUGGESTS. ***
//
// It searches ten fixed spellings, case-insensitively, in four prose fields, on the
// screen-states that carry no contextual button. That is the whole of it.
//
// *** IT IS NOT A CLASS GUARD, AND THE CODE MUST NOT SAY IT IS. *** The block above
// calls widening it "the class the fourteenth instance belonged to". A reviewer
// attacked it six times with ordinary edits and won six times. Two of the six are
// worth naming, because they are the useful part of this record:
//
//   1. `Press the loopMIDI button to start it for you.` PASSES. That sentence obeys
//      THE RULE THIS SAME COMMIT WROTE, four lines above the text it replaced: name
//      the control by its SUBJECT, not by where it sits. So the guard is blind to the
//      house style the fix itself adopted, and the next author following the new rule
//      is the author most likely to walk past it.
//
//   2. THE CAUGHT SENTENCE, RE-WRAPPED, PASSES. Not reworded - re-wrapped. say()
//      emits one line each and these needles are matched inside a single line, so
//      moving a line break splits the phrase and the offence disappears. setup.cpp's
//      block at the loopMIDI pane already warns about exactly this failure mode, for
//      two OTHER needles, in those words - and nothing defends these ten from it.
//
// *** AND ITS SCOPE IS NARROWER THAN THE DEFECT IT IS NAMED AFTER. *** The walk SKIPS
// any screen-state that has a contextual button, on the argument that a screen which
// HAS the button may name it. But half of what instance 14 actually was is a sentence
// naming a control that IS on the screen and putting it in the WRONG PLACE - "the
// button in the bottom left corner", where that corner holds "Check again". On a
// machine that offers the loopMIDI button, screen 3 has a button and this walk does
// not look at it at all.
//
// So: it catches the phrasings that exist, and that has value - it is what makes
// instances 13 and 14 unable to come back by copy-paste. It does not close the class,
// and this project's own standard is that a guard states what it cannot do rather
// than letting its name state something else. A seventh needle would buy an eighth
// defeat; the honest move is the sentence you are reading.
//
// WHERE EACH SIDE COMES FROM: the haystack is Screen::bullets, Screen::addressLead,
// Screen::paneCaption and Screen::paneText as setup.cpp really filled them, over
// machines this suite invented; the needles are literals in this file, which
// setup.cpp cannot see.
// ===========================================================================
static void testNoAbsentControlNamed()
{
    wprintf(L"\n--- no sentence names a control its screen does not show ---\n");

    wchar_t what[400];

    // The ways this program's prose has pointed, or could plausibly point, at a control
    // sitting on the same screen. "below" and "beside" are the two directions a painted
    // line can point at the foot band from; the corners are the four places a sentence
    // can put a control it cannot see.
    //
    // *** THE COUNT IS DERIVED FROM THE TABLE AND NOT WRITTEN TWICE. *** The loops below
    // used a literal 5 in three places against an array of 5. Adding a needle without
    // finding all three is a needle that is never compared against anything, and the
    // message would go on printing "5 phrases" while searching for the first five. This
    // file has already shipped checks that did not run; a hand-copied loop bound is one
    // of the ways that happens.
    static const wchar_t* const naming[] = {
        L"button below", L"button on this screen", L"press the button",
        L"button beside", L"the button above",
        L"bottom left corner", L"bottom right corner",
        L"top left corner", L"top right corner",
        L"the button in the"
    };
    const int kNaming = (int)(sizeof(naming) / sizeof(naming[0]));

    // Two machines - a fully invented working one and a zeroed one - so that the walk
    // is over more than a single accident of state. The MIDI port screen no longer
    // has a button in ANY state - see the block over describeMidiPort() - so the
    // per-state variation this loop used to need is gone with it; what is left worth
    // walking twice is everything else in the table.
    int examined  = 0;   // screens with NO contextual button, over all the machines
    int offenders = 0;
    // *** HOW MANY OF THOSE REALLY HAD A PANE IN THEM, BECAUSE THE PANE IS THE FIELD
    //     THIS ROUND ADDED AND AN EMPTY HAYSTACK IS THE WAY A WIDENED CHECK LIES. ***
    // The whole point of reading Screen::paneText is that the MIDI port screen
    // carries a pane and no button. If buildScreens() ever stopped filling paneText -
    // `g_midiPaneText[0] ? ... : 0` makes an empty buffer produce a null - this walk
    // would still examine the same screens, still find no offence, and still read
    // green while searching nothing. Counted here and asserted below.
    int panesRead = 0;
    wchar_t firstOffence[240];
    firstOffence[0] = 0;

    for (int m = 0; m < 2; m++) {
        {
            bcdsetup::MachineState s;
            if (m == 0)
                fakeState(&s);
            else
                ZeroMemory(&s, sizeof(s));
            bcdgui::Wizard w;
            ZeroMemory(&w, sizeof(w));
            bcdsetup::buildScreens(&w, &s);

            for (int i = 0; i < bcdgui::screenCount(&w); i++) {
                const bcdgui::Screen* sc = &w.screens[i];
                bool hasButton = sc->action != 0 && sc->actionLabel != 0 &&
                                 sc->actionLabel[0] != 0;
                if (hasButton)
                    continue;   // a screen that HAS the button may name it
                examined++;
                // Every field on a Screen that carries PROSE a reader sees. paneText is
                // the one that was missing and it is the one the fourteenth instance
                // lived in; paneCaption is here because it is painted at the top of the
                // pane and is prose by the same argument.
                const wchar_t* texts[] = {
                    sc->bullets[0], sc->bullets[1], sc->bullets[2], sc->bullets[3],
                    sc->addressLead, sc->paneCaption, sc->paneText
                };
                const int kTexts = (int)(sizeof(texts) / sizeof(texts[0]));
                if (sc->paneText && sc->paneText[0])
                    panesRead++;
                for (int b = 0; b < kNaming; b++) {
                    for (int t = 0; t < kTexts; t++) {
                        // *** CASE-BLIND, AND THAT IS THE WHOLE OF WHY THIS CHECK IS
                        //     WORTH ANYTHING. *** See posOfNoCase(). Three of the ten
                        // needles are sentence-openers and every line of a pane starts
                        // with a capital, so with wcsstr() this walk caught the wording
                        // that already existed and missed the next one somebody writes.
                        if (!texts[t] || posOfNoCase(texts[t], naming[b]) < 0)
                            continue;
                        offenders++;
                        if (!firstOffence[0]) {
                            _snwprintf(firstOffence, 230,
                                       L"screen %d (%s) says \"%s\" and shows no such "
                                       L"control", i, sc->title ? sc->title : L"?",
                                       naming[b]);
                            firstOffence[230] = 0;
                        }
                    }
                }
            }
        }
    }

    wprintf(L"  %d screen-states with no contextual button, %d phrases each, %d of them "
            L"carrying a pane\n", examined, kNaming, panesRead);
    // The message says what was SEARCHED and not what was proved. It used to open
    // "no screen without a contextual button names one in its own words", which is a
    // claim about the class and is one the search cannot make: it is ten spellings,
    // and a reviewer walked past it six times out of six. See the block above.
    _snwprintf(what, 390,
               L"none of the screen-states without a contextual button carries any of "
               L"the %d spellings this search knows - %d states examined over their "
               L"bullets, address lead, pane caption AND PANE, %d offence(s)%s%s",
               kNaming, examined, offenders, firstOffence[0] ? L": " : L"",
               firstOffence[0] ? firstOffence : L"");
    what[390] = 0;
    // The second clause is the anti-vacuity one and it is not decoration: if
    // buildScreens() stopped filling actionLabel at all, every screen would look like a
    // screen with no button, the walk would be over the whole flow and the check would
    // still be about something. If it started filling it EVERYWHERE, `examined` would
    // fall to zero and this would pass having looked at nothing.
    check(offenders == 0 && examined >= 6, what);

    // ...and the field this round added was really read. See panesRead.
    _snwprintf(what, 390,
               L"...and the PANE was really in the haystack rather than a null pointer "
               L"skipped in silence - %d of the %d screen-states examined carried one",
               panesRead, examined);
    what[390] = 0;
    check(panesRead >= 1, what);
}

// ===========================================================================
// PART 2e2 - RULE 2: THE BUTTON NAMES THE CONSEQUENCE OF PRESSING IT
//
// The design's second rule, and the owner's own words behind it: "os botoes tem que
// fazer sentido na tela do instalador. Nao e INSTALL logo de cara. E NEXT. o botao
// precisa refletir exatamente o que vai acontecer."
//
// *** WHERE EACH SIDE OF EVERY COMPARISON COMES FROM, BECAUSE THAT IS THE THING
//     THIS PROJECT HAS GOT WRONG FOUR TIMES - AND THE HONEST ANSWER IS SMALLER
//     THAN THE ONE THAT USED TO BE WRITTEN HERE. ***
//
// This block used to say the label and the consequence come from "two files, two
// authors, no shared value". They do not. The LABEL is `primaryLabel` and the
// consequence is decided by `kind` and `startsTheWork`, and all three are ADJACENT
// FIELDS OF ONE STRUCT INITIALIZER, in setup.cpp or uninstall.cpp, written by one
// author in one edit. Only the MAPPING from those fields to a PrimaryAction lives
// elsewhere, in gui.cpp's primaryActionFor(). One file, one author, adjacent lines.
//
// WHAT THE CHECKS BELOW THEREFORE DO AND DO NOT CATCH, stated so that a later round
// does not read more into a pass than is in it:
//   - they DO catch either half moved alone, and that is not a small thing.
//     primaryActionFor() never reads primaryLabel and primaryLabelFor() never reads
//     kind or startsTheWork, so the two are independent VALUES even though they are
//     neighbouring lines: renaming the button without moving the press fails here,
//     and moving the press without renaming the button fails here.
//   - they do NOT catch both halves edited together to agree on something wrong.
//     Nothing about "these two fields are consistent" can. What catches that is the
//     harness's own literals - Start, Next, Install, Remove, Close, asserted below
//     against every screen - and THOSE are a genuine second source, because they are
//     in this file and a table author editing setup.cpp does not touch them.
//
// Before primaryActionFor() existed the consequence lived inside frameProc()'s
// IDC_PRIMARY arm and nothing outside gui.cpp could evaluate it at all, which is
// precisely why "the opening's button says Install and installs nothing" survived six
// rounds of review. That is the improvement these checks really represent.
//
// *** THE SUBSTRING HAZARD, ASKED AND ANSWERED. *** Every comparison below is
// wcscmp() against a whole label and never wcsstr(), so "Install" cannot match inside
// "Install loopMIDI..." - which is a real string in this program, on the CONTEXTUAL
// action button, and is exactly the needle a substring test would have found in the
// wrong place. "Installing" is another live one: it is the work screen's title. The
// contextual button is a different control with a different rule (it names its own
// subject and may say Install about somebody else's installer); counting it as a
// primary label would make the "exactly one" check below unable to mean anything.
// ===========================================================================

// ---------------------------------------------------------------------------
// *** RULE 2 AS A WHITELIST, WHICH IS WHAT IT HAD TO BECOME. ***
//
// The check this replaces was a BLACKLIST OF TWO EXACT STRINGS: it walked the
// advancing screens and counted the ones whose label was exactly L"Install" or
// exactly L"Remove". It caught the defect the owner found, and it would have gone
// straight past L"Install loopMIDI...", L"Install the driver" or L"Set up WinUSB" as
// the PRIMARY label of a screen that only turns the page - every one of which is the
// same lie, and Tasks 3 to 6 add exactly the screens on which an author would write
// one. A list of forbidden words can only forbid the words somebody already thought
// of.
//
// So the question is turned round. Every screen's label must be one of the words
// that BELONGS to what its press does. There is no "other" arm: a label nobody
// listed fails, which is the opposite of a blacklist's default and is the whole
// point.
//
// The sets are small on purpose, and each of these is a real string in the program
// today: Start opens the setup, Install and Remove are the two flows' verbs, Close
// is the last screen's and also what the work screen really carries in the control
// before the button is hidden. Next is listed although nothing says it yet - it is
// the word the design gives the five check screens Tasks 3 to 6 add, and a task that
// has to widen this set to ship is a task that should have to say so here.
//
// wcscmp() on whole strings, for the reason in the block above.
// ---------------------------------------------------------------------------
static bool labelFitsAction(bcdgui::PrimaryAction act, const wchar_t* l)
{
    if (!l)
        return false;
    switch (act) {
    case bcdgui::kPrimaryAdvance:
        return wcscmp(l, L"Start") == 0 || wcscmp(l, L"Next") == 0;
    case bcdgui::kPrimaryStart:
        return wcscmp(l, L"Install") == 0 || wcscmp(l, L"Remove") == 0;
    default:
        // kPrimaryClose, and kPrimaryNone - the work screen, where the label is
        // written into the control and the control is then hidden. The table has to
        // say what was really written, and what is really written is Close.
        return wcscmp(l, L"Close") == 0;
    }
}

static const wchar_t* wordsWantedFor(bcdgui::PrimaryAction act)
{
    switch (act) {
    case bcdgui::kPrimaryAdvance: return L"Start or Next";
    case bcdgui::kPrimaryStart:   return L"Install or Remove";
    default:                      return L"Close";
    }
}

static const wchar_t* deedOf(bcdgui::PrimaryAction act)
{
    switch (act) {
    case bcdgui::kPrimaryAdvance: return L"only turns the page";
    case bcdgui::kPrimaryStart:   return L"writes to this machine";
    case bcdgui::kPrimaryClose:   return L"closes the window";
    default:                      return L"is not offered there at all";
    }
}

// One check PER SCREEN, and not one count over the flow. A count says how many
// screens lie; it does not say which, and the round that wrote the blacklist also
// wrote an overflow check that counted screens without naming them and could not
// tell a fixed screen from a newly broken one. The failure has to carry the screen,
// the word it wears and the deed it wears it over.
static void checkRule2(const bcdgui::Wizard* w, const wchar_t* flow)
{
    wchar_t what[400];
    int end = bcdgui::screenCount(w);
    for (int i = 0; i < end; i++) {
        bcdgui::PrimaryAction act = bcdgui::primaryActionFor(w, i, false);
        const wchar_t* l = bcdgui::primaryLabelFor(w, i, false);
        _snwprintf(what, 380,
                   L"%s screen %d wears a word that belongs to its press: it says "
                   L"'%s' over a press that %s, so it has to say %s",
                   flow, i, l ? l : L"(null)", deedOf(act), wordsWantedFor(act));
        what[380] = 0;
        check(labelFitsAction(act, l), what);
    }
}

// ---------------------------------------------------------------------------
// *** RULE 2 OVER THE OTHER BUTTON, WHICH IS WHERE IT WAS STILL A LIST OF THE
//     MISTAKES SOMEBODY HAD ALREADY MADE. ***
//
// The PRIMARY button's guard became a whitelist a round ago - labelFitsAction() above,
// and the block over it records the two forbidden words it replaced. The CONTEXTUAL
// button never got one. Screen::actionLabel, the button that stands in the foot band
// beside "Check again", had exactly two kinds of assertion on it: that each rung of
// chooseLoopMidiOffer() produces the exact string this file expects, and that the
// button appears on the screens whose entries carry it. Both are about the LADDER and
// about the WIRING. Neither is about the set of words this program is allowed to put on
// a button, so a screen added tomorrow with actionLabel = L"Fix this" passes every
// check in this file - and "Fix this" is precisely the label Rule 2 exists to forbid,
// because it does not name what pressing it does.
//
// *** AND THE OWNER RULED ON THE LABEL THAT MADE THIS URGENT, ON 2026-07-31. *** Rule 2
// used to end "Install appears exactly once in the whole program", while the same
// section requires that a button launching somebody else's installer says so and names
// them. The program obeys the second and therefore breaks the first: kOfferWingetLabel
// is L"Install loopMIDI..." on screen 3, and the install screen's primary is
// L"Install". The design document carried BOTH SIDES of that contradiction, which is
// why no implementer had to resolve it and none did. The ruling: BOTH STAY. The rule
// that matters is the heading - the button names the consequence of pressing it - and
// both labels satisfy it exactly. Install loopMIDI... runs Tobias Erichsen's installer;
// Install writes the driver. Neither promises anything it does not do.
//
// So what changes is the guard and not the program. Every contextual label the flow can
// produce must be one of the words listed here, and a label nobody listed FAILS until
// somebody decides it is honest. That is the opposite of a blacklist's default and it
// is the whole point: a list of forbidden words can only forbid the words somebody
// already thought of.
//
// WHERE EACH SIDE COMES FROM: the left is Screen::actionLabel and Screen::overrideLabel
// out of tables the product's own buildScreens() filled from machines this suite
// invented; the right is four literals written in this file. setup.cpp cannot see them,
// and buildScreens() has no way to read them.
// ---------------------------------------------------------------------------
static bool contextualLabelAllowed(const wchar_t* l)
{
    // *** ONE ENTRY NOW, NOT THREE. *** The winget offer's two labels are gone along
    // with the MIDI port screen's offer - see the block over kMidiTitle in
    // setup.cpp. What is left names its own SUBJECT rather than a verb, which is
    // what lets it live in a band away from the row it acts on.
    return wcscmp(l, L"Open the Zadig page") == 0;
}

// The named door of section 4.2 is a third kind of button and gets its own set of one.
// It is not a Next and it is not an offer: pressing it is a STATEMENT, so its words
// have to be the statement. A verb here would be a way round a check.
static bool overrideLabelAllowed(const wchar_t* l)
{
    return wcscmp(l, L"Already applied - continue anyway") == 0;
}

static void checkContextualLabels()
{
    wchar_t what[400];

    // *** THREE MACHINES NOW, NOT SIX. *** The winget/page ladder that used to need
    // three teVirtualMIDI readings crossed with two winget readings is gone along
    // with the offer it drove - see the block over kMidiTitle in setup.cpp. What is
    // left to walk is a fully invented working machine, a zeroed one, and one with
    // the account mismatched, which is enough variety to keep this walk about more
    // than one accident of state.
    int labelled  = 0;      // screen-states carrying a contextual label at all
    int strangers = 0;
    int doors     = 0;
    int strangeDoors = 0;
    int sawZadig = 0;
    wchar_t firstStranger[200];
    firstStranger[0] = 0;

    for (int m = 0; m < 3; m++) {
        {
            bcdsetup::MachineState s;
            if (m == 0)
                fakeState(&s);
            else if (m == 1)
                ZeroMemory(&s, sizeof(s));
            else {
                fakeState(&s);
                s.account.matched = false;
            }
            bcdgui::Wizard w;
            ZeroMemory(&w, sizeof(w));
            bcdsetup::buildScreens(&w, &s);

            for (int i = 0; i < bcdgui::screenCount(&w); i++) {
                const wchar_t* l = w.screens[i].actionLabel;
                if (l && l[0]) {
                    labelled++;
                    if (wcscmp(l, L"Open the Zadig page") == 0)
                        sawZadig++;
                    if (!contextualLabelAllowed(l)) {
                        strangers++;
                        if (!firstStranger[0]) {
                            _snwprintf(firstStranger, 190,
                                       L"screen %d wears '%s'", i, l);
                            firstStranger[190] = 0;
                        }
                    }
                }
                const wchar_t* d = w.screens[i].overrideLabel;
                if (d && d[0]) {
                    doors++;
                    if (!overrideLabelAllowed(d)) {
                        strangeDoors++;
                        if (!firstStranger[0]) {
                            _snwprintf(firstStranger, 190,
                                       L"screen %d's door wears '%s'", i, d);
                            firstStranger[190] = 0;
                        }
                    }
                }
            }
        }
    }

    _snwprintf(what, 390,
               L"every contextual button in the setup wears a word from the permitted "
               L"set - %d label(s) over 3 machines, %d that nobody listed%s%s",
               labelled, strangers, firstStranger[0] ? L": " : L"",
               firstStranger[0] ? firstStranger : L"");
    what[390] = 0;
    // `labelled` is the anti-vacuity half. If buildScreens() stopped filling
    // actionLabel, every label would be null, the walk would find no stranger and this
    // would pass having compared nothing - which is exactly the shape of the nine
    // comparisons the final review found that cannot fail.
    check(strangers == 0 && labelled >= 3, what);

    // *** AND THE ONE PERMITTED WORD REALLY OCCURS. *** A whitelist that is never
    // matched against anything is a whitelist of nothing. This says the three
    // machines between them produced the one label the set permits: Zadig's. If
    // that stopped being reachable, this goes red instead of the set quietly
    // becoming larger than the program.
    _snwprintf(what, 390,
               L"...and the one word in that set is one the program really produces - "
               L"'Open the Zadig page' %d", sawZadig);
    what[390] = 0;
    check(sawZadig >= 1, what);

    _snwprintf(what, 390,
               L"...and the named door wears the sentence that says what taking it "
               L"CLAIMS - %d door(s) over 3 machines, %d that nobody listed",
               doors, strangeDoors);
    what[390] = 0;
    check(strangeDoors == 0 && doors >= 1, what);

    // The other flow, so that "every contextual button in the program" is really every
    // one. The uninstaller has no offer and no door today; if it grows one, this is what
    // sends whoever wrote it to the set above rather than letting a fourth word in.
    bcdgui::Wizard u;
    ZeroMemory(&u, sizeof(u));
    bcduninstall::buildScreens(&u);
    int uContextual = 0;
    for (int i = 0; i < bcdgui::screenCount(&u); i++)
        if ((u.screens[i].actionLabel && u.screens[i].actionLabel[0]) ||
            (u.screens[i].overrideLabel && u.screens[i].overrideLabel[0]))
            uContextual++;
    _snwprintf(what, 390,
               L"the uninstaller offers no contextual button and no door at all (%d of "
               L"%d screens), so the set above is the whole program's",
               uContextual, bcdgui::screenCount(&u));
    what[390] = 0;
    check(uContextual == 0, what);
}

static int countLabel(const bcdgui::Wizard* w, const wchar_t* label)
{
    int n = 0;
    int end = bcdgui::screenCount(w);
    for (int i = 0; i < end; i++)
        if (w->screens[i].primaryLabel && wcscmp(w->screens[i].primaryLabel, label) == 0)
            n++;
    return n;
}

static int countAction(const bcdgui::Wizard* w, bcdgui::PrimaryAction act)
{
    int n = 0;
    int end = bcdgui::screenCount(w);
    for (int i = 0; i < end; i++)
        if (bcdgui::primaryActionFor(w, i, false) == act)
            n++;
    return n;
}

// The one screen whose button starts the work, or -1. Named by what it DOES.
static int screenThatStarts(const bcdgui::Wizard* w)
{
    int end = bcdgui::screenCount(w);
    for (int i = 0; i < end; i++)
        if (bcdgui::primaryActionFor(w, i, false) == bcdgui::kPrimaryStart)
            return i;
    return -1;
}

static void testPrimaryLabels()
{
    wprintf(L"\n--- the words on the primary button ---\n");

    wchar_t what[400];

    bcdsetup::MachineState s;
    fakeState(&s);
    bcdgui::Wizard w;
    ZeroMemory(&w, sizeof(w));
    bcdsetup::buildScreens(&w, &s);

    for (int i = 0; i < bcdgui::screenCount(&w); i++)
        wprintf(L"  setup screen %d: label '%s', action %d\n", i,
                bcdgui::primaryLabelFor(&w, i, false),
                (int)bcdgui::primaryActionFor(&w, i, false));

    // *** THE BUTTON THE OWNER OBJECTED TO, BY NAME. ***
    const wchar_t* opening = bcdgui::primaryLabelFor(&w, 0, false);
    _snwprintf(what, 380,
               L"the opening's button says Start, because pressing it starts nothing "
               L"but the wizard - it got '%s'", opening ? opening : L"(null)");
    what[380] = 0;
    check(opening != 0 && wcscmp(opening, L"Start") == 0, what);

    // ...and it says it about a press that really does only turn the page. A label
    // check on its own would pass on a table that said Start over a press that
    // installs, which is the same defect with the words swapped.
    _snwprintf(what, 380,
               L"...over a press that only turns the page (action %d, wanted %d)",
               (int)bcdgui::primaryActionFor(&w, 0, false),
               (int)bcdgui::kPrimaryAdvance);
    check(bcdgui::primaryActionFor(&w, 0, false) == bcdgui::kPrimaryAdvance, what);

    // *** Install SITS ON THE SCREEN THAT INSTALLS - ASKED OF THE ACTION, NOT OF A
    //     POSITION. *** The design moves that press to a screen of its own once the
    // five check screens exist; this asks where the press IS, so it keeps meaning
    // the same thing when it moves.
    int starts = screenThatStarts(&w);
    const wchar_t* startLabel = starts >= 0 ? bcdgui::primaryLabelFor(&w, starts, false)
                                            : L"(no screen starts the work)";
    _snwprintf(what, 380,
               L"the word Install is on the screen whose press installs - screen %d "
               L"says '%s'", starts, startLabel);
    what[380] = 0;
    check(starts >= 0 && wcscmp(startLabel, L"Install") == 0, what);

    // *** THIS COUNTS PRIMARY LABELS AND IT ALWAYS DID, AND SAYING SO IS THE OWNER'S
    //     RULING WRITTEN INTO THE MESSAGE. *** countLabel() walks
    // Screen::primaryLabel. It never saw kOfferWingetLabel, which is a CONTEXTUAL
    // label and lives in a different field on a different control - so the old
    // wording, "the word appears exactly once in the whole program", was a claim
    // this check could not make and did not make. On 2026-07-31 the owner ruled that
    // both Install loopMIDI... and Install stay, because each names its own
    // consequence, and the design was corrected to match. What is asserted here is
    // the half that is true and that matters: ONE press in this program writes to
    // this machine, and only that press wears the bare word. checkContextualLabels()
    // is what holds the other button to its own set.
    int installs = countLabel(&w, L"Install");
    _snwprintf(what, 380,
               L"...and no other screen's PRIMARY button says it: %d of %d screens. The "
               L"contextual 'Install loopMIDI...' is a different control with its own "
               L"whitelist, and the owner ruled both labels honest",
               installs, bcdgui::screenCount(&w));
    check(installs == 1, what);

    // ONE press writes to this machine. The count above says one screen carries the
    // word; this says one screen carries the deed. Both are needed: a table with two
    // starting screens and one Install label would pass the first and be a program
    // with a second, unlabelled way to install.
    int starters = countAction(&w, bcdgui::kPrimaryStart);
    _snwprintf(what, 380,
               L"exactly one screen in the setup starts the work at all (%d), so there "
               L"is no second press that writes to this machine", starters);
    check(starters == 1, what);

    _snwprintf(what, 380,
               L"a finished flow closes, whatever screen it is on - screen 0 says '%s'",
               bcdgui::primaryLabelFor(&w, 0, true));
    what[380] = 0;
    check(wcscmp(bcdgui::primaryLabelFor(&w, 0, true), L"Close") == 0, what);

    // ...and the deed matches the word in that state too, which is the one case
    // where both functions carry the same override and could have drifted apart.
    _snwprintf(what, 380,
               L"...and it really closes rather than merely saying so (action %d)",
               (int)bcdgui::primaryActionFor(&w, 0, true));
    check(bcdgui::primaryActionFor(&w, 0, true) == bcdgui::kPrimaryClose, what);

    // *** EVERY SCREEN'S WORD MUST BELONG TO ITS PRESS. *** The general form of the
    // owner's complaint rather than a second test of screen 0: it holds for every
    // screen the later tasks add, and it is what those tasks trip if they label a
    // Next screen with something that sounds like an action. See checkRule2() for
    // why this is a whitelist and not the list of two forbidden words it replaced.
    checkRule2(&w, L"setup");

    // ---------------------------------------------------------------
    // The other flow. It shares gui.cpp, so it inherits the rule, and its verb is
    // the one that can destroy a working install.
    // ---------------------------------------------------------------
    bcdgui::Wizard u;
    ZeroMemory(&u, sizeof(u));
    bcduninstall::buildScreens(&u);

    for (int i = 0; i < bcdgui::screenCount(&u); i++)
        wprintf(L"  uninstall screen %d: label '%s', action %d\n", i,
                bcdgui::primaryLabelFor(&u, i, false),
                (int)bcdgui::primaryActionFor(&u, i, false));

    int uStarts = screenThatStarts(&u);
    const wchar_t* uLabel = uStarts >= 0 ? bcdgui::primaryLabelFor(&u, uStarts, false)
                                         : L"(no screen starts the removal)";
    _snwprintf(what, 380,
               L"the uninstaller's Remove is on the screen whose press removes - "
               L"screen %d says '%s'", uStarts, uLabel);
    what[380] = 0;
    check(uStarts >= 0 && wcscmp(uLabel, L"Remove") == 0, what);

    int removes = countLabel(&u, L"Remove");
    _snwprintf(what, 380,
               L"...and on NO other screen: %d of %d screens say Remove",
               removes, bcdgui::screenCount(&u));
    check(removes == 1, what);

    // *** AND THE WORD Install DOES NOT APPEAR ON A PRIMARY BUTTON IN THE UNINSTALLER
    //     AT ALL. *** With the setup's count of one above, this is the other half of
    // "exactly one press in this program writes to a machine, and it is not this
    // flow's". The old message said "exactly once in the whole program", which was
    // the sentence the owner corrected on 2026-07-31: the contextual button on screen
    // 3 says Install loopMIDI... and stays, because it names whose installer it runs.
    int uInstalls = countLabel(&u, L"Install");
    _snwprintf(what, 380,
               L"the uninstaller's primary says Install on no screen at all (%d), so "
               L"the one press that writes a driver is in the other flow", uInstalls);
    check(uInstalls == 0, what);

    // The same whitelist over the other flow. It was never asked of the uninstaller
    // at all: the blacklist this replaces ran on the setup's table only, so the
    // program with the greatest power here to destroy a working installation was the
    // one flow whose labels nothing checked screen by screen.
    checkRule2(&u, L"uninstall");

    // *** AND THE SAME QUESTION OF THE OTHER BUTTON, WHICH IS THE HALF OF RULE 2 THAT
    //     WAS STILL UNGUARDED. *** See the block over contextualLabelAllowed().
    checkContextualLabels();
}

// ===========================================================================
// PART 2f - BCD3000Uninstall.exe /preview, ON THE REAL previewPlan()
//
// THE PROGRAM THIS SUITE IS ABOUT HAS THE GREATEST POWER IN THIS PROJECT TO DESTROY A
// WORKING INSTALLATION, and until this round it had no dry run at all while
// BCD3000Setup.exe - the less destructive of the two - had one. This suite is the
// measurement of the mode that closes that gap, and every check below reads the text
// the product's own previewPlan() emitted through say(), not a copy of it.
//
// *** THE FOUR CASES ARE FOUR INVENTED MACHINES, and the reason is R4 rather than
// convenience. *** previewPlan() prints paths, and on this machine some of those paths
// are inside the owner's profile. A suite that ran it against the real machine would
// put the owner's account name into harness output that gets pasted into reports -
// which is the class of leak the synthetic shotState() exists to close for the
// pictures. So the state is fakeState()'s, the per user paths are invented, and
// checkPreviewHasNoLiveIdentity() reads every captured line back through the same
// needles the picture gate uses.
//
// *** WHAT IS NOT INVENTED, named so it is not mistaken for invention. *** Three
// things come from the real machine even here: computeRemovalPaths() asks
// manifestPath() and uninstallExePath(), which resolve %ProgramFiles% - machine wide,
// so no identity in it - and previewPlan() calls fileExists() on paths as it prints,
// so whether the driver is "on disk now" is a fact about this machine. Neither is
// identity and neither decides a check below.
//
// *** AND ONE CHECK RUNS AGAINST THE REAL MACHINE ON PURPOSE: *** the one that calls
// the real prepare() in /preview and reads logIsOpen() back. That is the only way to
// measure "it did not open the log" rather than assert it, and it is safe precisely
// because the mode under test is the one that writes nothing. Its Run is thrown away
// immediately afterwards, exactly as wmain() throws away the one prepare() fills.
// ===========================================================================

// The invented machine every preview case starts from. fakeState() supplies the
// state, so there is one place to read what the fiction is; the per user paths a
// Run carries beside the state are invented here.
static void previewRun(bcduninstall::Run* run)
{
    ZeroMemory(run, sizeof(*run));
    run->opt.preview = true;
    fakeState(&run->state);
    bcduninstall::computeRemovalPaths(&run->state, &run->paths);
    wcscpy(run->bridgeDir,
           L"C:\\Users\\Example\\AppData\\Local\\BCD3000Bridge");
    wcscpy(run->logFile,
           L"C:\\Users\\Example\\AppData\\Local\\BCD3000Bridge\\install.log");
    run->logFileResolved = true;
}

// *** WHAT "WELL FORMED" MEANS FOR THE TWO PATHS previewRun() INVENTS, AND WHY THE
//     QUESTION IS ASKED AT ALL. ***
//
// Round 20 shipped an invented bridgeDir whose backslashes had been eaten by the C
// escaping - it read C:UsersExampleAppDataLocalBCD3000Bridge - and the preview printed
// that four times. Two gates were open at once. The compiler DID say so (C4429 once and
// C4129 four times) and nobody read past the last two lines of its output, which is
// item 8's problem and is fixed there. And checkPreviewHasNoLiveIdentity() could not
// have caught it either way: its job is to find a REAL profile that leaked in, and a
// mangled fiction has no identity in it at all. So a malformed fixture passed every
// check in this suite while making four lines of the product's output nonsense.
//
// The test is therefore about SHAPE and nothing else, because shape is the whole of
// what went wrong: an absolute local path is a letter, a colon, a separator, and then
// at least one more separator, because both of these are meant to name something INSIDE
// a folder rather than a drive root. The mangled value fails at the third character.
//
// It is deliberately not a test that the path is real. Both values are fiction on
// purpose - see the header of this part - so "does it exist" is the wrong question and
// asking it would drag the owner's machine back into a suite built to keep it out.
static bool looksLikeAbsolutePath(const wchar_t* p)
{
    if (!p || !p[0])
        return false;
    if (!iswalpha(p[0]) || p[1] != L':' || p[2] != L'\\')
        return false;
    return wcschr(p + 3, L'\\') != 0;
}

// A path that certainly exists and carries no identity, for the one branch of step 5
// that needs the recorded driver to be ON DISK. It is a fiction like every other value
// here - nobody's previous ASIO driver is kernel32.dll - and it is the only way to
// exercise that branch without the harness creating a file.
static void systemFileThatExists(wchar_t* out, DWORD count)
{
    out[0] = 0;
    wchar_t dir[kPathMax];
    if (GetSystemDirectoryW(dir, kPathMax))
        _snwprintf(out, count - 1, L"%s\\kernel32.dll", dir);
    out[count - 1] = 0;
}

// Runs previewPlan() with the console silenced and every line captured.
static int runPreview(bcduninstall::Run* run, bool canRun, int wouldStopCode)
{
    capReset();
    bcdsetup::setConsoleEcho(false);
    bcdsetup::setLineSink(capSink);
    int code = bcduninstall::previewPlan(run, canRun, wouldStopCode);
    bcdsetup::setLineSink(0);
    bcdsetup::setConsoleEcho(true);
    return code;
}

// R4, on text instead of on pixels. Same needles as the picture gate.
static void checkPreviewHasNoLiveIdentity(const wchar_t* caseName)
{
    collectLiveIdentity();
    g_hitWhat  = 0;
    g_hitWhere = 0;
    for (int i = 0; i < g_capN; i++)
        scanForIdentity(g_cap[i], L"a line of the preview");
    wchar_t what[600];
    _snwprintf(what, 590,
               L"%s: no live machine identity anywhere in the preview's output%s%s",
               caseName, g_hitWhat ? L" - found " : L"", g_hitWhat ? g_hitWhat : L"");
    what[590] = 0;
    check(g_hitWhat == 0, what);
}

// The five things that have to be true of EVERY preview, whichever branch it took:
// the exit code, the absence of "will", the six channels it did not use, the one it
// did, and no live identity anywhere in the text.
static void checkPreviewInvariants(const wchar_t* caseName, int code)
{
    wchar_t what[600];

    // *** 4, AND WRITTEN AS 4 RATHER THAN AS kExitAborted. *** Comparing the returned
    // value against the enumerator it was returned from would compare a constant with
    // itself and could not fail. The literal is the specification: the README and the
    // help text both say a preview exits 4, and this is that sentence as a number.
    _snwprintf(what, 590, L"%s: exits 4, the only code in this program that means "
                          L"nothing was changed (got %d)", caseName, code);
    what[590] = 0;
    check(code == 4, what);

    // Requirement C, made mechanical. Everything a preview says is conditional, so
    // the word that turns a description into a promise may not appear at all.
    bool promised = false;
    for (int i = 0; i < g_capN; i++)
        if (wcsstr(g_cap[i], L"will") || wcsstr(g_cap[i], L"Will") ||
            wcsstr(g_cap[i], L"WILL"))
            promised = true;
    _snwprintf(what, 590,
               L"%s: says WOULD and never WILL - not one line reads as a promise about "
               L"a machine this mode did not touch", caseName);
    what[590] = 0;
    check(!promised, what);

    // Requirement A's output half: the enumeration is printed, and it names the six
    // channels that were NOT reached rather than claiming the total. The seventh - the
    // one that WAS reached - is the check below this one, and the split is deliberate:
    // see the comment there for what was measured about keeping them together.
    _snwprintf(what, 590, L"%s: prints the list of every way this program can write, "
                          L"and says nothing was written", caseName);
    what[590] = 0;
    check(capHas(L"what this run wrote: nothing") &&
          capHas(L"NOT OPENED") && capHas(L"NOT TOUCHED") &&
          capHas(L"NOT TERMINATED") && capHas(L"NOTHING QUEUED") &&
          capHas(L"never both"), what);

    // *** THE SEVENTH CHANNEL, WHICH THE SIX NEEDLES ABOVE CANNOT REACH - MEASURED, NOT
    //     SUSPECTED. *** The whole 1053 byte "a child process ONE WAS STARTED" block was
    // deleted from previewPlan() and this suite still printed 209 checks, 0 failures,
    // VERIFY_OK. The round's headline product change - the admission that this mode does
    // start one process - could be removed again without a single check going red, and
    // the "ALL SEVEN" sentence at the head of the preview was asserted nowhere either.
    // That is the same shape of hole as a claim that is printed but not true: here it was
    // a claim that could stop being printed and nothing would notice.
    //
    // *** WHY THE NEEDLE IS THE ROW LABEL AND THE FACT TOGETHER. *** "a child process" on
    // its own is not enough, and that is not caution: the closing lines of the same
    // enumeration now say "The child process is reached from prepare() too", so the bare
    // phrase survives the deletion of the row it is supposed to measure. The label plus
    // "ONE WAS STARTED: winget.exe --version" exists on exactly one line and dies with it.
    // Same lesson as the bridgeDir check further down, found in a different corner.
    //
    // THREE TERMS, BECAUSE THE ROW MAKES THREE SEPARATE ADMISSIONS: that a process was
    // started and which one, that TerminateProcess was on the table for it, and that the
    // head of the enumeration promises SEVEN and not six. Drop any one of them and the
    // enumeration is over-claiming again.
    _snwprintf(what, 590,
               L"%s: names the ONE channel this mode did use - winget.exe --version, "
               L"started by detectWinget() - and the head of the list promises SEVEN "
               L"ways and not six", caseName);
    what[590] = 0;
    check(capHas(L"lists ALL SEVEN ways") &&
          capHas(L"a child process   ONE WAS STARTED: winget.exe --version") &&
          capHas(L"called TerminateProcess on it"), what);

    checkPreviewHasNoLiveIdentity(caseName);
}

static void testUninstallPreview()
{
    wprintf(L"\n--- BCD3000Uninstall.exe /preview ---\n");

    // -------------------------------------------------------------------
    // The arguments, and the help text that documents them.
    // -------------------------------------------------------------------
    {
        wchar_t* argv[2] = { (wchar_t*)L"BCD3000Uninstall.exe", (wchar_t*)L"/preview" };
        bcduninstall::Options o;
        bool ok = bcduninstall::parseArgs(2, argv, &o);
        check(ok && o.preview, L"/preview is recognised on the command line");
        check(ok && !o.console && !o.assumeYes && !o.help,
              L"...and sets nothing else: it is one flag with one meaning");

        wchar_t* bad[2] = { (wchar_t*)L"BCD3000Uninstall.exe",
                            (wchar_t*)L"/pre-view" };
        bcduninstall::Options o2;
        capReset();
        bcdsetup::setConsoleEcho(false);
        bcdsetup::setLineSink(capSink);
        bool ok2 = bcduninstall::parseArgs(2, bad, &o2);
        bcdsetup::setLineSink(0);
        bcdsetup::setConsoleEcho(true);
        check(!ok2, L"...and a near miss is still refused rather than ignored");
    }
    {
        capReset();
        bcdsetup::setConsoleEcho(false);
        bcdsetup::setLineSink(capSink);
        bcduninstall::printHelp();
        bcdsetup::setLineSink(0);
        bcdsetup::setConsoleEcho(true);
        check(capHas(L"/preview"), L"the help text documents /preview");
        check(capHas(L"4 nothing was changed"),
              L"...and exit code 4 is described as \"nothing was changed\" rather than "
              L"only as \"stopped at your request\", which is what it now covers");
    }

    // -------------------------------------------------------------------
    // Requirement A, the decision itself. mayOpenLog() is the first term of the
    // condition on the only logOpen() call in the program, so it is asked directly
    // for both answers - which is a thing prepare() cannot be asked for, because
    // prepare() without /preview would open the real log file on this machine.
    // -------------------------------------------------------------------
    {
        bcduninstall::Options o;
        ZeroMemory(&o, sizeof(o));
        o.preview = false;
        check(bcduninstall::mayOpenLog(&o),
              L"a run without /preview is allowed to open the log file, which is what "
              L"stops this gate from being a gate that closes everything");
        o.preview = true;
        check(!bcduninstall::mayOpenLog(&o),
              L"a /preview run is refused the log file");
    }

    // -------------------------------------------------------------------
    // THE OTHER GATE THE ENUMERATION LEANS ON, ASKED THE SAME WAY.
    //
    // previewPlan()'s last line says "main() calls this function or it calls
    // runRemoval(), never both", and that sentence is what makes FIVE of the seven write
    // channels above it hold - not six and not all seven. The log file is held by
    // mayOpenLog() instead, because logOpen() is called from prepare(); the child process
    // is held by nothing, because it happened. Until wantsWindow() existed the only
    // thing measured about the sentence was
    // capHas(L"never both") - a check that the SENTENCE IS PRINTED, which is not a check
    // that it is true, and is the same shape of hole mayOpenLog() was invented to close
    // one gate over. The decision was an inline boolean in main() and unreachable from
    // here; now it has a name, so it gets both answers like the other one.
    //
    // FIVE TERMS, FIVE SEPARATE ANSWERS, and each false case is asked on its own rather
    // than all at once: a single "everything off" case would pass even if four of the
    // five terms had been dropped from the predicate. The predicate is
    // argsOk && !help && !console && !assumeYes && !preview - five, and the six checks
    // below are the true case plus one false case per term. The prose here said FOUR for
    // three rounds while the checks underneath it covered all five; it was the sentence
    // that was short, never the coverage.
    // -------------------------------------------------------------------
    {
        bcduninstall::Options o;
        ZeroMemory(&o, sizeof(o));
        check(bcduninstall::wantsWindow(&o, true),
              L"with a good command line and no flags the uninstaller asks for a window, "
              L"which is what stops this gate from being a gate that closes everything");

        ZeroMemory(&o, sizeof(o));
        o.preview = true;
        check(!bcduninstall::wantsWindow(&o, true),
              L"...and /preview does not: the mode whose whole claim is that it writes "
              L"nothing never reaches bcdgui::init(), so \"previewPlan() or runRemoval(), "
              L"never both\" is measured here and not merely printed by the preview");

        ZeroMemory(&o, sizeof(o));
        o.console = true;
        check(!bcduninstall::wantsWindow(&o, true), L"...nor does /console");

        ZeroMemory(&o, sizeof(o));
        o.assumeYes = true;
        check(!bcduninstall::wantsWindow(&o, true),
              L"...nor does /yes, which keeps the console rather than opening a window "
              L"that answers its own questions");

        ZeroMemory(&o, sizeof(o));
        o.help = true;
        check(!bcduninstall::wantsWindow(&o, true), L"...nor does /help");

        ZeroMemory(&o, sizeof(o));
        check(!bcduninstall::wantsWindow(&o, false),
              L"...and neither does a command line that did not parse, which is the term "
              L"that would have been left behind in main() if the predicate had been "
              L"lifted without it");
    }

    // -------------------------------------------------------------------
    // ...and the observation, on the real prepare(), on this machine. This is the
    // only check in the suite that is not against an invented machine, and it is the
    // only one that can measure the log file rather than reason about it.
    // -------------------------------------------------------------------
    {
        static bcduninstall::Run probe;
        ZeroMemory(&probe, sizeof(probe));
        probe.opt.preview = true;
        int            stop    = 0;
        const wchar_t* blocked = 0;
        capReset();
        bcdsetup::setConsoleEcho(false);
        bcdsetup::setLineSink(capSink);
        bcduninstall::prepare(&probe, &stop, &blocked);
        bcdsetup::setLineSink(0);
        bcdsetup::setConsoleEcho(true);
        capReset();          // it read the real machine; none of it is kept
        check(!bcdsetup::logIsOpen(),
              L"the real prepare(), run with /preview, left no log file open - read back "
              L"from common.cpp's own handle, not concluded from the source");

        // *** AND THE POSITIVE CONTROL, WITHOUT WHICH THE CHECK ABOVE PASSES VACUOUSLY.
        //
        // The gate on the only logOpen() call in the program has four terms:
        //
        //     mayOpenLog(&opt) && haveBridgeDir && logFileResolved && dirExists(bridgeDir)
        //
        // and ANY of the four being false leaves the log closed. On a machine where
        // %LOCALAPPDATA%\BCD3000Bridge does not exist - a machine that never ran this
        // product, which is most machines and every clean CI box - the fourth term is
        // false, the log stays closed for a reason that has nothing to do with /preview,
        // and the check above prints [ok] while mayOpenLog() did no work at all. That is
        // a green light for a gate that was never asked.
        //
        // So the other three terms are measured here and asserted TRUE. With all three
        // true, mayOpenLog() is provably the only term that could have been false, and
        // the observation above becomes evidence rather than an accident of this
        // machine's disk layout.
        //
        // THE TWO PATH CALLS ARE REPEATED RATHER THAN READ OUT OF probe, and that is on
        // purpose: bridgeDirPath() can fail and still have written into its buffer, so
        // probe.bridgeDir[0] cannot tell true from false. These are the same two
        // functions prepare() called, they only read, and they are asked again for their
        // real answer. probe.logFileResolved IS read from the Run, because prepare()
        // stores that one exactly as it received it.
        //
        // IF THIS EVER FAILS, IT IS NOT A DEFECT IN THE PRODUCT. It says this machine
        // has no bridge folder, which makes the check above unmeasurable here rather
        // than false - the honest reading is "come back on a machine that has the
        // product installed", and that is why the failure text says which term went.
        wchar_t realBridgeDir[kPathMax];
        bool    haveBridgeDir = bcdsetup::bridgeDirPath(realBridgeDir, kPathMax);
        bool    dirIsThere    = haveBridgeDir && bcdsetup::dirExists(realBridgeDir);
        check(haveBridgeDir && probe.logFileResolved && dirIsThere,
              haveBridgeDir
                ? (probe.logFileResolved
                     ? (dirIsThere
                          ? L"...and the other three terms of that gate were all TRUE, "
                            L"so mayOpenLog() is provably the only one that closed it - "
                            L"without this the check above passes on any machine that "
                            L"simply has no bridge folder"
                          : L"...positive control UNMET: the bridge folder is not on "
                            L"this machine, so the log would have stayed closed with or "
                            L"without /preview and the check above measured nothing")
                     : L"...positive control UNMET: logFilePath() did not resolve")
                : L"...positive control UNMET: bridgeDirPath() did not resolve");
    }

    // -------------------------------------------------------------------
    // Requirement D. computeRemovalPaths() is the one place that derives the list,
    // and the four it copies have to arrive intact.
    // -------------------------------------------------------------------
    {
        bcdsetup::MachineState s;
        fakeState(&s);
        bcduninstall::RemovalPaths p;
        bcduninstall::computeRemovalPaths(&s, &p);
        check(wcscmp(p.installDir,   s.installDir)   == 0 &&
              wcscmp(p.dllTarget,    s.dllTarget)    == 0 &&
              wcscmp(p.bridgeTarget, s.bridgeTarget) == 0 &&
              wcscmp(p.shortcutFile, s.shortcutFile) == 0,
              L"computeRemovalPaths() carries the four paths gatherMachineState() "
              L"already worked out through unchanged");
    }

    // -------------------------------------------------------------------
    // *** THE ONE CHECK IN THIS SUITE THAT CAN CATCH A WRONG DERIVATION RATHER THAN A
    //     MISSING ONE, AND WHY EVERY OTHER PATH CHECK CANNOT. ***
    //
    // The eight path checks in CASE A compare run.paths.X against text printed FROM
    // run.paths.X. Same origin on both sides. They catch a path the preview dropped or
    // duplicated, which is worth having, but they cannot catch a path that is derived
    // wrongly: if computeRemovalPaths() built dllTarget + ".bkp", both sides of every
    // one of those eight checks would move together and all eight would stay green.
    // Asserting dllBackup == dllTarget + L".bak" here would be no better - a literal
    // typed in this file is a third opinion, not a second measurement, and it turns the
    // check into an assertion that the harness agrees with itself.
    //
    // THE NON TAUTOLOGICAL CROSS CHECK IS THAT THESE TWO NAMES ARE DERIVED TWICE, IN TWO
    // PROGRAMS, AND THE OTHER PROGRAM IS THE ONE THAT WRITES THE FILES. setup.cpp's
    // stagingAndBackupPaths() builds them so it can stage and back up; uninstall.cpp's
    // computeRemovalPaths() builds them so it can delete them. Both are real product
    // code, both are in this translation unit, and NEITHER SIDE OF THE COMPARISON BELOW
    // IS A STRING TYPED HERE - the only literal is the shared input, which is
    // fakeState()'s dllTarget and is fed identically to both.
    //
    // THE DEFECT IT EXISTS TO CATCH IS SILENT AND PERMANENT. If the setup wrote ".bak"
    // and the uninstaller looked for ".bkp", nothing would fail, nothing would be
    // reported, and every uninstall would leave a copy of a driver in the install folder
    // - which is also what stops RemoveDirectoryW, so the folder survives too. The user
    // would be told to go and look inside a folder that should not exist.
    // -------------------------------------------------------------------
    {
        bcdsetup::MachineState s;
        fakeState(&s);

        bcduninstall::RemovalPaths p;
        bcduninstall::computeRemovalPaths(&s, &p);

        wchar_t setupStaging[kPathMax];
        wchar_t setupBackup[kPathMax];
        bool    setupOk = stagingAndBackupPaths(s.dllTarget, setupStaging, setupBackup);

        check(setupOk && p.haveSideCars &&
              wcscmp(p.dllBackup, setupBackup) == 0,
              L"the .bak name the uninstaller would DELETE is the same string setup.cpp "
              L"would WRITE - two derivations in two programs, compared against each "
              L"other and not against a literal in this file");
        check(setupOk && p.haveSideCars &&
              wcscmp(p.dllStaging, setupStaging) == 0,
              L"...and the same for the .new staging name");

        // The length guard is derived twice as well - once in each function - so it is
        // compared the same way, and on an input where the answer is NO. Without this
        // case the boolean comparison above only ever sees true == true, and a guard
        // that had been dropped from one side would never show.
        bcdsetup::MachineState longS;
        fakeState(&longS);
        for (DWORD i = 0; i < kPathMax - 1; i++)
            longS.dllTarget[i] = L'x';
        longS.dllTarget[kPathMax - 1] = 0;

        bcduninstall::RemovalPaths longP;
        bcduninstall::computeRemovalPaths(&longS, &longP);
        wchar_t ls[kPathMax];
        wchar_t lb[kPathMax];
        bool    longSetupOk = stagingAndBackupPaths(longS.dllTarget, ls, lb);
        check(!longSetupOk && !longP.haveSideCars,
              L"...and on a driver path too long to append four characters to, BOTH "
              L"programs refuse to build the two names - the guard is derived twice too, "
              L"so it is compared twice too");
    }

    // -------------------------------------------------------------------
    // CASE A: nothing running, no earlier registration recorded. The quiet machine,
    // and the one whose preview has to say the loudest thing in the mode.
    // -------------------------------------------------------------------
    {
        static bcduninstall::Run run;
        previewRun(&run);
        run.havePrevious = false;
        int code = runPreview(&run, true, 0);
        checkPreviewInvariants(L"no earlier driver", code);

        check(capHas(L"records no earlier registration") ||
              capHas(L"could not be located"),
              L"no earlier driver: says the manifest records none, rather than saying "
              L"nothing about it");
        check(capHas(L"NO offer would be made"),
              L"...and says outright that no offer would be made");
        check(capHas(L"NO ASIO DRIVER WOULD BE REGISTERED ON THIS MACHINE"),
              L"...and states the consequence for the machine in those words");

        // *** EVERY PATH IN RemovalPaths IS IN THE PREVIEW. *** The two sides come
        // from different places: the struct computeRemovalPaths() filled, and the
        // text previewPlan() printed. A preview that grew a list of its own would
        // fail here the moment the two stopped agreeing.
        //
        // EACH ONE ASKS FOR A NON EMPTY PATH FIRST, and that is not decoration: capHas()
        // is wcsstr(), and wcsstr() finds an empty needle in any haystack. Without the
        // guard, a computeRemovalPaths() that stopped filling a field would turn these
        // checks GREEN rather than red - a check that passes harder when the thing it
        // measures disappears. Found by asking what defect would be injected to make
        // each one fail.
        //
        // *** AND TWO OF THE EIGHT NEEDED MORE THAN THAT GUARD - MEASURED, THIRD ROUND
        //     RUNNING THAT A PREFIX HAZARD TURNED UP ONE CHECK AT A TIME. ***
        // The guard stops an EMPTY needle. It does nothing about a needle that is a
        // PREFIX of some other line the preview prints for an unrelated reason, which is
        // the defect that already cost this file the bridgeDir check (see the block a
        // little further down). fakeState() gives installDir =
        // "C:\Program Files\BCD3000 ASIO Driver" and derives dllTarget, dllBackup,
        // dllStaging, the manifest and the installed self from it, so:
        //
        //   installDir matched inside FIVE other printed lines, and dllTarget inside the
        //   .bak and the .new lines. Both were proved vacuous: the dllTarget row was
        //   deleted from step 4's loop and the install folder line's %s was replaced with
        //   a literal, and BOTH CHECKS STILL PRINTED [ok] WITH THE THING THEY MEASURE
        //   GONE.
        //
        // The fix is the one that already worked once here: the needle is the SENTENCE
        // AND THE PATH, so it can only be found on the line that names the path for the
        // reason the check is about.
        //
        // THE OTHER SIX WERE RE-DERIVED RATHER THAN ASSUMED SOUND. dllBackup and
        // dllStaging end in ".bak" and ".new" and nothing printed extends them;
        // the manifest and the installed self are leaves under installDir with nothing
        // printed beneath them; shortcutFile is in the Startup folder, which nothing else
        // printed touches; and bridgeTarget is not under installDir at all in fakeState()
        // - it is under the bridge folder - and both of its two prints are genuine
        // namings. Those six ask for the bare path deliberately, so that a preview which
        // moved a path to a different sentence still counts as having named it.
        check(run.paths.bridgeTarget[0] && capHas(run.paths.bridgeTarget),
              L"the preview names the control service, from RemovalPaths");

        // dllTarget is a strict prefix of dllBackup and dllStaging, so this one asks for
        // step 4's own row: "the ASIO driver: <path>".
        wchar_t driverRow[kPathMax + 64];
        _snwprintf(driverRow, kPathMax + 63, L"the ASIO driver: %s", run.paths.dllTarget);
        driverRow[kPathMax + 63] = 0;
        check(run.paths.dllTarget[0] && capHas(driverRow),
              L"...the ASIO driver, in the step 4 row that names it - not merely as the "
              L"prefix of the .bak and .new lines, which is what this check used to "
              L"settle for");
        check(run.paths.haveSideCars && run.paths.dllBackup[0] &&
              capHas(run.paths.dllBackup),
              L"...the .bak copy of the driver it replaced");
        check(run.paths.haveSideCars && run.paths.dllStaging[0] &&
              capHas(run.paths.dllStaging),
              L"...the .new staging file");
        check(run.paths.haveManifest && run.paths.manifest[0] &&
              capHas(run.paths.manifest),
              L"...the install manifest");
        check(run.paths.haveInstalledSelf && run.paths.installedSelf[0] &&
              capHas(run.paths.installedSelf),
              L"...this uninstaller's own installed copy");
        // installDir is a strict prefix of five other printed paths, so this one asks for
        // the line that names it as the FOLDER rather than for the folder's characters.
        wchar_t folderRow[kPathMax + 64];
        _snwprintf(folderRow, kPathMax + 63, L"the install folder: %s",
                   run.paths.installDir);
        folderRow[kPathMax + 63] = 0;
        check(run.paths.installDir[0] && capHas(folderRow),
              L"...the install folder, in the line that names it as a folder - the five "
              L"paths derived from it are no longer accepted as evidence that it was "
              L"named");
        check(run.paths.shortcutFile[0] && capHas(run.paths.shortcutFile),
              L"...and the startup shortcut");

        // *** THE TWO PATHS THAT ARE NOT IN RemovalPaths, AND WERE READ BY NO CHECK AT
        //     ALL UNTIL NOW. *** The preview prints bridgeDir TWICE - once in step 4, as
        //     the folder whose logs are not on the deletion list, and once under "what a
        //     real run would KEEP", which is the one place in the output that names
        //     something the user gets to keep - and logFile once, as the file it did not
        //     open. (reportPlan() prints bridgeDir a third time and that is where "three"
        //     came from, but reportPlan() is what a REAL run prints on its way to doing
        //     it; /preview never calls it, so its line is not in this capture.) Both
        //     came out of the Run rather than out of RemovalPaths, so the eight checks
        //     above walked straight past them, and that gap is exactly where round 20's
        //     mangled fixture lived. Present, and shaped like a path: see
        //     looksLikeAbsolutePath() for what shape means here and why it is the right
        //     question.
        // *** THE NEEDLE IS THE SENTENCE AND THE PATH TOGETHER, AND THAT IS NOT
        //     DECORATION - IT IS THE SECOND DEFECT THIS ROUND FOUND. *** The first
        //     version of this check asked capHas(run.bridgeDir) on its own. It passed
        //     with every bridgeDir print DELETED from previewPlan, which was measured by
        //     injecting exactly that: fakeState()'s bridgeTarget is
        //     <bridgeDir>\BCD3000Bridge.exe, so the folder is a PREFIX of a path the
        //     preview prints for a completely different reason, and wcsstr found it
        //     there. A check that a folder is NAMED has to ask for the line that names
        //     it, or it is a check that some longer path happens to start with it.
        wchar_t keptLine[kPathMax + 64];
        _snwprintf(keptLine, kPathMax + 63, L"the log files in %s", run.bridgeDir);
        keptLine[kPathMax + 63] = 0;
        check(run.bridgeDir[0] && capHas(keptLine),
              L"the preview names the log folder it is KEEPING, in the sentence that says "
              L"it is kept - from the Run rather than from RemovalPaths, which is why no "
              L"check read it before");
        check(looksLikeAbsolutePath(run.bridgeDir),
              L"...and that folder is shaped like an absolute path with its separators "
              L"intact, which the invented one silently was not for a whole round");
        check(run.logFileResolved && run.logFile[0] && capHas(run.logFile),
              L"the preview names the log file it did NOT open, from the same variable "
              L"the open would have used");
        check(looksLikeAbsolutePath(run.logFile),
              L"...and it too is shaped like an absolute path");
        check(run.bridgeDir[0] &&
              wcsncmp(run.logFile, run.bridgeDir, wcslen(run.bridgeDir)) == 0 &&
              run.logFile[wcslen(run.bridgeDir)] == L'\\',
              L"...and the log file is INSIDE that folder, which is the one relation "
              L"between the two that the product's own logFilePath() guarantees and a "
              L"fixture can get wrong without any check noticing");
    }

    // -------------------------------------------------------------------
    // CASE B: the control service running, and an earlier driver recorded that is
    // still on disk. This is the case the whole mode exists for, and the one where
    // requirement C has the most to be honest about.
    // -------------------------------------------------------------------
    {
        static bcduninstall::Run run;
        previewRun(&run);
        run.state.bridge.running       = true;
        run.state.bridge.instanceCount = 2;
        run.state.bridge.firstPid      = 4242;
        run.havePrevious               = true;
        systemFileThatExists(run.previous, kPathMax);
        int code = runPreview(&run, true, 0);
        checkPreviewInvariants(L"service running, earlier driver on disk", code);

        check(capHas(L"it IS running at this moment"),
              L"running service: the preview says it is running now, with the count and "
              L"the process id it read");
        check(capHas(L"NOT KNOWABLE FROM A PREVIEW: whether it can be stopped"),
              L"...and says in those terms that whether it can be STOPPED is not "
              L"knowable without doing it");
        check(capHas(L"exits 1 WITHOUT REMOVING ANYTHING"),
              L"...and that a real run exits 1 having removed nothing when it cannot be "
              L"stopped, which is the safe outcome and has to be readable as one");
        check(capHas(L"destroys the virtual MIDI port"),
              L"...and that stopping it destroys the virtual MIDI port");

        check(run.previous[0] && capHas(run.previous),
              L"earlier driver: the preview names the exact file the manifest recorded");
        check(capHas(L"THAT FILE IS ON DISK AT THIS MOMENT"),
              L"...and says whether that file is on disk RIGHT NOW, which is the fact "
              L"the offer depends on");
        check(capHas(L"WOULD MAKE THE OFFER"),
              L"...and says whether the offer would be made at all");

        // The order is the content: a preview whose steps do not run in the order the
        // real run performs them is a different document from the one asked for.
        int s1 = capIndexOf(L"step 1 of 5");
        int s2 = capIndexOf(L"step 2 of 5");
        int s3 = capIndexOf(L"step 3 of 5");
        int s4 = capIndexOf(L"step 4 of 5");
        int s5 = capIndexOf(L"step 5 of 5");
        check(s1 >= 0 && s1 < s2 && s2 < s3 && s3 < s4 && s4 < s5,
              L"...and all five steps are printed in the order the real run performs "
              L"them");
        check(capHas(L"what this preview CANNOT know, collected in one place"),
              L"the unknowns are collected in a block of their own as well as sitting "
              L"beside the steps they qualify");
        check(capHas(L"nothing above is a prediction that a real run would succeed"),
              L"...closed by the sentence that refuses to make the claim this mode "
              L"cannot make");
    }

    // -------------------------------------------------------------------
    // CASE C: an earlier driver recorded whose file is gone. The case where a real
    // run leaves the machine with no ASIO driver and cannot help it.
    // -------------------------------------------------------------------
    {
        static bcduninstall::Run run;
        previewRun(&run);
        run.havePrevious = true;
        wcscpy(run.previous,
               L"C:\\Program Files\\Another Vendor\\NoLongerThere\\Asio.dll");
        int code = runPreview(&run, true, 0);
        checkPreviewInvariants(L"earlier driver gone", code);

        check(run.previous[0] && capHas(run.previous),
              L"earlier driver gone: the preview still names the recorded file");
        check(capHas(L"NOT ON DISK AT THIS MOMENT"),
              L"...and says it is not on disk");
        check(capHas(L"NO ASIO DRIVER WOULD BE REGISTERED ON THIS MACHINE"),
              L"...and states the consequence before anybody runs the real thing");
    }

    // -------------------------------------------------------------------
    // CASE D: a machine where a real run would refuse to start. The preview that is
    // most likely to be read, and the one that has the least to say - so what it
    // does say has to include the fact that it could not read the file step 5
    // depends on.
    // -------------------------------------------------------------------
    {
        static bcduninstall::Run run;
        previewRun(&run);
        run.havePrevious = false;
        int code = runPreview(&run, false, 1);
        checkPreviewInvariants(L"a real run would refuse", code);

        check(capHas(L"A REAL RUN WOULD REFUSE TO START AND EXIT 1"),
              L"blocked: the preview says a real run would refuse, and with which code");
        check(capHas(L"WITHOUT REMOVING ANYTHING"),
              L"...and that the refusal removes nothing");
        check(capHas(L"NOT KNOWN, AND NOT BECAUSE OF THIS MODE"),
              L"...and that step 5 is unanswerable because the manifest was never read, "
              L"which is a different kind of ignorance from the rest and is labelled as "
              L"one");
    }
}

// ===========================================================================
// PART 2g - THE UNINSTALLER'S FIRST SCREEN, WHICH SHIPPED BLANK FOR TWENTY-EIGHT
//           COMMITS
//
// *** WHAT HAPPENED, BECAUSE THE SHAPE OF THE HOLE MATTERS MORE THAN THE DEFECT. ***
// The owner ran BCD3000Uninstall.exe at 02:00 and the first page showed the heading
// "What this will do", a rule, and nothing else. The four rows that page is made of
// had not been deleted - buildWizard() had been filling Wizard::review with them the
// whole time - but the entry that opens the flow had stopped saying it was the entry
// that PAINTS them, so renderCheckScreen() sent it to renderSubject(), which draws a
// title, a rule and a row this screen has never had.
//
// *** AND WHY NOTHING WENT RED. *** This suite is what was missing. testScreenTable()
// asserts the uninstaller's table STRUCTURE - three screens, the first is the
// confirmation, exactly one work screen and one done screen - and every one of those
// stayed true of a page with nothing on it. The flow's CONTENT was reachable from
// nowhere: buildScreens() is a function the harness could always call, and the rows
// were built inside runWindowed(), whose last act was to enter the message loop. So
// the only way to reach the words was to open a window and start a removal.
//
// uninstall.cpp splits that function in two for this suite: buildWizard() fills a
// Wizard the caller owns, runWindowed() shows it. Everything below reads rows the
// PRODUCT built, on machines this file invents.
//
// *** THE THIRD ROW IS WHY THIS IS NOT A COSMETIC SUITE. *** It is the one that says,
// on the page and BEFORE the button, that removing our registration can leave the
// machine with NO ASIO driver at all and that the uninstaller will offer to put the
// previous one back. He never saw it, answered "No" at the end, and HKLM\SOFTWARE\ASIO
// now holds one entry that is not this product's. A row that only exists in a struct
// nobody paints is a row that does not exist.
// ===========================================================================

// Which of the three answers the manifest can give about the driver that was
// registered before this product. All three change the third row, and the round that
// wrote this found that the third row is the one with a machine behind it.
enum PrevDriver { kPrevOnDisk, kPrevRecordedButGone, kPrevNone };

// The invented machine each case below is built on. previewRun() supplies the state
// and the two per user paths - so the fiction is written in one place - and this adds
// the two facts the confirmation screen branches on.
static void uninstallScreenRun(bcduninstall::Run* run, PrevDriver prev, bool serviceUp)
{
    previewRun(run);
    run->state.bridge.running = serviceUp;
    if (serviceUp) {
        run->state.bridge.instanceCount = 2;
        run->state.bridge.firstPid      = 4242;
    }
    run->havePrevious = (prev != kPrevNone);
    if (prev == kPrevOnDisk)
        systemFileThatExists(run->previous, kPathMax);
    else if (prev == kPrevRecordedButGone)
        wcscpy(run->previous,
               L"C:\\Windows\\System32\\NoSuchAsioDriver-bcd3000-verify.dll");
    else
        run->previous[0] = 0;
}

// Every needle below is searched in the row the product filled, so a row that lost a
// clause fails on the clause rather than on a length.
static bool rowSays(const bcdgui::Row* r, const wchar_t* needle)
{
    return wcsstr(r->detail, needle) != 0;
}

// R4 over the words this flow would paint. It is the uninstaller's half of
// checkNoLiveIdentity(), which walks the setup's Wizard and ends on a count that
// belongs to the setup's summary pane. Same needles, same scanner, this flow's fields.
static void checkUninstallHasNoLiveIdentity(bcdgui::Wizard* w, const wchar_t* caseName)
{
    collectLiveIdentity();
    g_hitWhat  = 0;
    g_hitWhere = 0;

    scanForIdentity(w->windowTitle,     L"the uninstaller's window title");
    scanForIdentity(w->headline,        L"the uninstaller's headline");
    scanForIdentity(w->subhead,         L"the uninstaller's subhead");
    scanForIdentity(w->reviewCaption,   L"the confirmation screen's caption");
    scanForIdentity(w->reviewFooter,    L"the confirmation screen's footer");
    int rowsScanned = 0;
    for (int i = 0; i < w->reviewCount; i++) {
        rowsScanned++;
        scanForIdentity(w->review[i].title,  L"a row title on the confirmation screen");
        scanForIdentity(w->review[i].detail, L"a row's text on the confirmation screen");
    }
    scanForIdentity(w->progressCaption, L"the work screen's caption");
    for (int i = 0; i < w->stepCount; i++) {
        scanForIdentity(w->steps[i].title,  L"a step title");
        scanForIdentity(w->steps[i].detail, L"a step's text");
    }
    for (int i = 0; i < bcdgui::screenCount(w); i++) {
        scanForIdentity(w->screens[i].title,        L"a screen's title");
        scanForIdentity(w->screens[i].primaryLabel, L"a screen's primary button");
    }
    scanForIdentity(w->cannotCancelNote,   L"the note beside the buttons");
    scanForIdentity(w->doneCaptionOk,      L"the summary screen's caption");
    scanForIdentity(w->doneCaptionStopped, L"the summary screen's caption");
    scanForIdentity(w->doneCaptionFail,    L"the summary screen's caption");

    wchar_t what[600];
    // The denominator is printed for the reason checkNoLiveIdentity() prints its own:
    // a loop over zero rows scans nothing and reports the same clean pass as a loop
    // over four, so the number of rows really walked is part of the verdict.
    _snwprintf(what, 590,
               L"%s: nothing the uninstaller would paint names this machine - %d "
               L"strings looked for, %d rows walked%s%s", caseName, g_needleN,
               rowsScanned, g_hitWhat ? L" - found " : L"",
               g_hitWhat ? g_hitWhat : L"");
    what[590] = 0;
    check(g_hitWhat == 0 && rowsScanned == w->reviewCount && rowsScanned > 0, what);
}

static void testUninstallConfirmationScreen()
{
    wprintf(L"\n--- the uninstaller's confirmation screen, which shipped BLANK ---\n");

    wchar_t what[600];

    // -------------------------------------------------------------------
    // THE MACHINE THE OWNER HAD: a driver registered before this one, still on
    // disk, and the control service running. Four rows, and the two that matter
    // most to him are the last two.
    // -------------------------------------------------------------------
    static bcduninstall::Run  run;
    static bcdgui::Wizard     w;
    uninstallScreenRun(&run, kPrevOnDisk, true);
    bcduninstall::buildWizard(&run, &w, 0, bcduninstall::kExitOk);

    // *** THE HEADING AND THE ROWS ARE THE SAME STRING, WHICH IS THE WHOLE POINT OF
    //     THE DEFECT. *** renderReview() paints Wizard::reviewCaption; the entry
    // carries Screen::title. A page promising "What this will do" with nothing under
    // it is those two having parted company, so the check is pointer identity against
    // the flow's own constant rather than a comparison of two spellings.
    check(w.reviewCaption == bcduninstall::kConfirmTitle &&
          w.screens[0].title == bcduninstall::kConfirmTitle,
          L"the caption over the rows and the title of the screen they are on are the "
          L"SAME constant, so a page cannot promise one thing and be named another");

    // *** THE COUNT, FIRST, BECAUSE ZERO IS WHAT SHIPPED. ***
    _snwprintf(what, 590,
               L"the confirmation screen carries FOUR rows on a machine with a previous "
               L"driver and a running service (%d) - a heading with nothing under it is "
               L"what the owner was shown", w.reviewCount);
    check(w.reviewCount == 4, what);

    // ...and not one of them is an empty row. setRow() is the only thing that writes a
    // title, so a row with none was never filled; a row with a title and no text is a
    // coloured mark beside a blank line, which is the same defect one row deep.
    {
        int filled = 0;
        for (int i = 0; i < w.reviewCount && i < bcdgui::kMaxRows; i++)
            if (w.review[i].title[0] && w.review[i].detail[0])
                filled++;
        _snwprintf(what, 590,
                   L"...and every one of them carries BOTH a heading and its text (%d of "
                   L"%d), so no row is a coloured mark beside a blank line",
                   filled, w.reviewCount);
        check(filled == w.reviewCount && w.reviewCount > 0, what);
    }

    // *** THE SENTENCE THAT MAKES CLOSING THE WINDOW SAFE. *** It is the footer, it is
    // under the rows, and it is the only line on the page that tells somebody who has
    // read this far that they can still walk away.
    check(w.reviewFooter != 0 &&
          wcsstr(w.reviewFooter, L"Nothing has been removed yet") != 0 &&
          wcsstr(w.reviewFooter, L"leaves the machine exactly as it is") != 0,
          L"the footer says nothing has been removed yet and that closing the window "
          L"leaves the machine exactly as it is");

    // ---- row 0: what goes ---------------------------------------------
    // The four paths are read out of the Run this file invented, not spelled again
    // here: a literal on both sides would be this file agreeing with itself.
    check(wcscmp(w.review[0].title, L"Will be removed") == 0,
          L"the first row is 'Will be removed'");
    check(rowSays(&w.review[0], bcduninstall::kAsioClsid) &&
          rowSays(&w.review[0], run.state.installDir) &&
          rowSays(&w.review[0], run.state.bridgeTarget) &&
          rowSays(&w.review[0], run.state.shortcutFile),
          L"...and it names all four things that go: the ASIO class id, the install "
          L"folder, the control service and the startup shortcut");

    // ---- row 1: what stays, AND WHY, which is the load bearing half ----
    check(wcscmp(w.review[1].title, L"Will be kept") == 0,
          L"the second row is 'Will be kept'");
    // *** TWO THINGS STAY NOW, NOT THREE. *** teVirtualMIDI is gone from this row
    // along with the detection and the offer it was named beside - this uninstaller
    // never touched it in the first place, and now this program does not even look
    // for it. See the block over kInstallBullet2 in setup.cpp for the installer's
    // side of the same change.
    check(rowSays(&w.review[1], L"WinUSB binding") &&
          rowSays(&w.review[1], run.bridgeDir),
          L"...and it names the two things that stay: the WinUSB binding on the "
          L"device, and the log files");
    // *** THE REASONS ARE NOT DECORATION AND THEY ARE ASSERTED ONE AT A TIME. ***
    // "we never applied it and undoing a USB binding is what leaves hardware unusable"
    // is the sentence that stops the next person deciding this uninstaller ought to
    // tidy up after Zadig. A row that kept its nouns and lost its reasons would pass
    // the check above and would have lost the thing worth reading.
    check(rowSays(&w.review[1],
                  L"we never applied it and undoing a USB binding is what leaves "
                  L"hardware unusable"),
          L"...and WHY the WinUSB binding is not touched, which is the reason this "
          L"program does not undo what Zadig did");
    check(rowSays(&w.review[1], L"they are what a bug report needs"),
          L"...and WHY the log files stay");

    // ---- row 2: THE ROW THE OWNER NEEDED AND DID NOT GET ---------------
    check(w.review[2].state == bcdgui::kRowWarn,
          L"the row about the driver registered before this one is a WARNING row and "
          L"not a neutral one");
    check(wcscmp(w.review[2].title,
                 L"An ASIO driver was registered before this one") == 0,
          L"...titled so that it reads as a fact about this machine");
    check(rowSays(&w.review[2], run.previous),
          L"...and it prints WHICH driver, by path");
    check(rowSays(&w.review[2],
                  L"Removing our registration leaves this machine with NO ASIO driver "
                  L"at all"),
          L"...and states the consequence in the words that matter: NO ASIO driver at "
          L"all. This is the sentence that was on the page in e47858a, was on no page "
          L"at 41e1498, and would have changed the answer the owner gave");
    check(rowSays(&w.review[2], L"ask you at the end whether to register that one "
                                L"again") &&
          rowSays(&w.review[2], L"Say yes unless you know you do not want it"),
          L"...and says the question is coming and what to answer, BEFORE the button "
          L"rather than after the removal");

    // ---- row 3: the service --------------------------------------------
    check(w.review[3].state == bcdgui::kRowWarn &&
          wcscmp(w.review[3].title,
                 L"The control service is running and has to be stopped") == 0 &&
          rowSays(&w.review[3], L"destroys the virtual MIDI port"),
          L"the fourth row warns that the running control service has to be stopped "
          L"and that this destroys the virtual MIDI port");

    checkUninstallHasNoLiveIdentity(&w, L"previous driver on disk, service running");

    // -------------------------------------------------------------------
    // *** THE OTHER THREE MACHINES, BECAUSE THE THIRD ROW IS A FUNCTION OF THE
    //     MACHINE AND ONE STATE TESTS ONE ANSWER OUT OF THREE. ***
    //
    // WHERE EACH SIDE COMES FROM: the left is the row the product built from a Run
    // this file invented; the right is what the design says that machine has to be
    // told. The three answers differ in KIND as well as in words - warn, fail,
    // neutral - which is the difference between "it can be put back", "it cannot"
    // and "there was never one".
    // -------------------------------------------------------------------
    {
        static bcduninstall::Run  gone;
        static bcdgui::Wizard     wg;
        uninstallScreenRun(&gone, kPrevRecordedButGone, false);
        bcduninstall::buildWizard(&gone, &wg, 0, bcduninstall::kExitOk);
        _snwprintf(what, 590,
                   L"with the previous driver recorded but no longer on disk the screen "
                   L"still carries three rows (%d) and the third is a FAILURE row",
                   wg.reviewCount);
        check(wg.reviewCount == 3 && wg.review[2].state == bcdgui::kRowFail, what);
        check(wcsstr(wg.review[2].title, L"and it is gone") != 0 &&
              rowSays(&wg.review[2], L"it cannot be put back") &&
              rowSays(&wg.review[2],
                      L"no ASIO driver will be registered on this machine"),
              L"...and it says the file is gone, that it cannot be put back, and that "
              L"after this runs no ASIO driver will be registered on this machine");

        static bcduninstall::Run  none;
        static bcdgui::Wizard     wn;
        uninstallScreenRun(&none, kPrevNone, false);
        bcduninstall::buildWizard(&none, &wn, 0, bcduninstall::kExitOk);
        _snwprintf(what, 590,
                   L"with no previous driver recorded at all the screen still carries "
                   L"three rows (%d) and the third is neutral", wn.reviewCount);
        check(wn.reviewCount == 3 && wn.review[2].state == bcdgui::kRowNeutral, what);
        check(wcscmp(wn.review[2].title, L"No earlier ASIO driver to put back") == 0 &&
              rowSays(&wn.review[2],
                      L"no ASIO driver will be registered on this machine unless you "
                      L"have another one"),
              L"...and it still states the consequence, so the ASIO answer is on the "
              L"page on every machine and not only on the lucky ones");

        // *** AND THE PROPERTY BEHIND ALL THREE, AS ONE SENTENCE. *** The three checks
        // above are about three machines. This one is about the SCREEN: whatever the
        // manifest says, the third row exists, it is filled, and it uses the words "ASIO
        // driver". A refactor that dropped the branch on one machine would leave two of
        // the three above green.
        int said = 0;
        bcdgui::Wizard* all[3] = { &w, &wg, &wn };
        for (int i = 0; i < 3; i++)
            if (all[i]->reviewCount >= 3 && all[i]->review[2].title[0] &&
                all[i]->review[2].detail[0] &&
                (wcsstr(all[i]->review[2].title, L"ASIO driver") != 0 ||
                 wcsstr(all[i]->review[2].detail, L"ASIO driver") != 0))
                said++;
        _snwprintf(what, 590,
                   L"on all THREE machines the confirmation screen carries a filled row "
                   L"about the ASIO driver this removal decides the fate of (%d of 3)",
                   said);
        check(said == 3, what);

        checkUninstallHasNoLiveIdentity(&wg, L"previous driver recorded but gone");
        checkUninstallHasNoLiveIdentity(&wn, L"no previous driver recorded");
    }

    // -------------------------------------------------------------------
    // *** THE FIVE STEPS OF THE WORK SCREEN, WHICH NOTHING HAD EVER READ EITHER. ***
    // The brief for this round said to assume nothing about the other two screens, so
    // they are read here rather than taken on trust. The work screen is the one whose
    // rows arrive already written - all five, in the waiting state - so a screen that
    // lost them would be a progress page with a caption and no list.
    // -------------------------------------------------------------------
    {
        int filled = 0, waiting = 0;
        for (int i = 0; i < w.stepCount && i < bcdgui::kMaxRows; i++) {
            if (w.steps[i].title[0])
                filled++;
            if (w.steps[i].state == bcdgui::kRowWaiting)
                waiting++;
        }
        _snwprintf(what, 590,
                   L"the work screen arrives with all FIVE steps already named and all "
                   L"five waiting (%d named, %d waiting, of %d)",
                   filled, waiting, w.stepCount);
        check(w.stepCount == 5 && filled == 5 && waiting == 5, what);
        check(w.progressCaption == bcduninstall::kWorkTitle &&
              w.screens[1].title == bcduninstall::kWorkTitle,
              L"...and the caption painted over them is the same constant as the "
              L"screen's title, like the confirmation screen's");
        // The step that decides whether this machine still has an ASIO driver
        // afterwards says so, in the list, before it runs.
        check(wcsstr(w.steps[bcduninstall::kStepPutBack].title,
                     L"register the previous ASIO driver again") != 0 &&
              wcsstr(w.steps[bcduninstall::kStepPutBack].detail,
                     L"decides whether this machine still has an ASIO driver "
                     L"afterwards") != 0,
              L"...and the last step names the offer and says what it decides");
    }

    // ...and the summary screen's three captions, which are the only words it paints.
    check(w.doneCaptionOk == bcduninstall::kDoneTitle &&
          w.screens[2].title == bcduninstall::kDoneTitle &&
          w.doneCaptionStopped != 0 && w.doneCaptionStopped[0] &&
          w.doneCaptionFail    != 0 && w.doneCaptionFail[0],
          L"the summary screen has a caption for all three outcomes and the successful "
          L"one is the same constant as the screen's title");
}

// ===========================================================================
// PART 3 - the pages, rendered, at two DPIs, on a private desktop
// ===========================================================================
namespace bcdgui {

static const wchar_t* g_shotDir = 0;

// gui.cpp's buffer of lines that arrived before there was a window is what fills
// the log pane on page 3. wmain() empties it after prepare() has run, because
// prepare() reads THIS machine and the pane is a picture of an invented one.
static void discardEarlyLog()
{
    releaseEarly();
}

static bool savePng(HBITMAP bmp, int w, int h, const wchar_t* path)
{
    IWICImagingFactory* fac = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, 0, CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, (void**)&fac)) || !fac)
        return false;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    BYTE* pixels = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * h * 4);
    if (!pixels) { fac->Release(); return false; }
    HDC dc = GetDC(0);
    GetDIBits(dc, bmp, 0, (UINT)h, pixels, &bi, DIB_RGB_COLORS);
    ReleaseDC(0, dc);

    // GDI DOES NOT WRITE THE ALPHA BYTE. Every pixel TextOut and FillRect
    // produced comes back with alpha 0, so a 32bppBGRA PNG made straight from
    // this buffer is fully transparent and looks blank in any viewer - which is
    // exactly what the first run of this harness produced, while the ink
    // measurement below (which reads only R, G and B) was correct all along.
    // Forced opaque, except where AlphaBlend deliberately wrote alpha for the
    // device photograph; there is nothing behind these pages, so opaque is right.
    for (SIZE_T i = 0; i < (SIZE_T)w * h; i++)
        pixels[i * 4 + 3] = 0xFF;

    IWICStream*        st  = 0;
    IWICBitmapEncoder* en  = 0;
    IWICBitmapFrameEncode* fr = 0;
    bool ok = false;
    if (SUCCEEDED(fac->CreateStream(&st)) &&
        SUCCEEDED(st->InitializeFromFilename(path, GENERIC_WRITE)) &&
        SUCCEEDED(fac->CreateEncoder(GUID_ContainerFormatPng, 0, &en)) &&
        SUCCEEDED(en->Initialize(st, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(en->CreateNewFrame(&fr, 0)) &&
        SUCCEEDED(fr->Initialize(0))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(fr->SetSize((UINT)w, (UINT)h)) &&
            SUCCEEDED(fr->SetPixelFormat(&fmt)) &&
            SUCCEEDED(fr->WritePixels((UINT)h, (UINT)w * 4, (UINT)w * h * 4, pixels)) &&
            SUCCEEDED(fr->Commit()) && SUCCEEDED(en->Commit()))
            ok = true;
    }
    if (fr) fr->Release();
    if (en) en->Release();
    if (st) st->Release();
    fac->Release();
    HeapFree(GetProcessHeap(), 0, pixels);
    return ok;
}

// ---------------------------------------------------------------------------
// *** THE ANSWER savePng() GIVES, READ. ***
//
// It returned a bool that was read at NEITHER of its two call sites. The hole was
// closed by proxy - by asking whether the output directory exists before rendering -
// and a directory that exists is not a picture that was written. Everything below
// still leaves the run green with no new picture: a full disk, a read only file, a
// PNG a viewer has open, a WIC failure, and the directory being removed between the
// check and the write. And the tracked file then keeps its OLD bytes, so `git status`
// is silent and the md5 of a stale picture gets read as proof of a fresh one.
//
// So every write goes through here, and three things are asked of each: savePng()
// said yes, the file is on disk with something in it, and it was written DURING THIS
// RUN. The last is the one that catches the stale picture, which is the failure that
// looks most like a success.
static FILETIME g_runStart;
static int      g_pngWanted = 0;
static int      g_pngOk     = 0;
static wchar_t  g_pngFirstBad[400];

// ---------------------------------------------------------------------------
// *** AND WHICH PATHS WERE WRITTEN, BECAUSE THE TWO COUNTS ABOVE CANNOT SEE A
//     CAPTURE OVERWRITTEN BY ANOTHER CAPTURE. ***
//
// g_pngWanted and g_pngOk both count PER CALL. Two calls that name the same file
// increment both, so a run in which one page's picture is silently replaced by
// another's reports "42 of 42" and is green. That is not hypothetical: it is exactly
// what happened when a screen was added and its capture reused a name, and it was
// caught by a human counting 42 promises against 40 files on disk - a comparison this
// harness never made.
//
// So every call records its path, and the check at the end asks whether the number of
// DISTINCT paths equals the number of writes.
//
// WHERE EACH SIDE COMES FROM, since a check whose two sides share a source cannot
// fail. The left is g_pngPathN, the cardinality of a set: it grows only when a path
// compares unequal to every path already in the table. The right is g_pngWanted, a
// counter incremented unconditionally at the top of every call. Nothing writes both:
// a second call naming one path moves the counter and leaves the set alone, which is
// the only state this check exists to see.
//
// The comparison is case insensitive because these are Windows paths and two spellings
// of one file are one file.
// ---------------------------------------------------------------------------
static const int kPngPathMax = 128;
static wchar_t   g_pngPaths[kPngPathMax][512];
static int       g_pngPathN    = 0;
static int       g_pngDupes    = 0;
static bool      g_pngPathsFull = false;
static wchar_t   g_pngFirstDup[512];

static void recordPngPath(const wchar_t* path)
{
    for (int i = 0; i < g_pngPathN; i++) {
        if (_wcsicmp(g_pngPaths[i], path) != 0)
            continue;
        g_pngDupes++;
        if (!g_pngFirstDup[0]) {
            _snwprintf(g_pngFirstDup, 500, L"%s", path);
            g_pngFirstDup[500] = 0;
        }
        return;
    }
    // A table that filled up would make the two sides disagree for a reason that is
    // not a duplicate, so it is remembered and named rather than left to look like
    // one.
    if (g_pngPathN >= kPngPathMax) {
        g_pngPathsFull = true;
        return;
    }
    _snwprintf(g_pngPaths[g_pngPathN], 500, L"%s", path);
    g_pngPaths[g_pngPathN][500] = 0;
    g_pngPathN++;
}

static void savePngChecked(HBITMAP bmp, int w, int h, const wchar_t* path)
{
    g_pngWanted++;
    // BEFORE the write and not after it: two calls naming one path is a promise this
    // run cannot keep however well either write went, and a run where the second write
    // failed would otherwise hide the collision behind the failure.
    recordPngPath(path);
    const wchar_t* why = 0;
    if (!savePng(bmp, w, h, path)) {
        why = L"savePng() said no";
    } else {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa))
            why = L"nothing is on disk at that path";
        else if (fa.nFileSizeHigh == 0 && fa.nFileSizeLow == 0)
            why = L"the file is empty";
        else if (CompareFileTime(&fa.ftLastWriteTime, &g_runStart) < 0)
            why = L"the file on disk is OLDER than this run - the old picture is "
                  L"still there";
    }
    if (!why) {
        g_pngOk++;
        return;
    }
    if (!g_pngFirstBad[0]) {
        _snwprintf(g_pngFirstBad, 390, L"%s: %s", path, why);
        g_pngFirstBad[390] = 0;
    }
}

// The bounding box of everything that is not the background colour.
// This is what turns "nothing was clipped" from an opinion into a measurement:
// the renderers report a content height, and this says where the ink actually
// stopped. Ink below the reported height is ink the scroll range cannot reach.
struct InkBox { int left, top, right, bottom; bool any; };

// THE BACKGROUND IS A PARAMETER, AND THE REGION IS TOO, because the header band is
// NOT white: it has its own colour (CLR_HEAD_BG), and a measurement hard wired to
// white would report the whole band as ink and prove nothing about the text in it.
// The page measurement below passes 0 for the region and white for the colour, which
// is what it always did, so nothing it proved has changed.
static InkBox inkOfRegion(HBITMAP bmp, int w, int h, const RECT* region,
                          COLORREF bg)
{
    InkBox b = { w, h, -1, -1, false };
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    BYTE* px = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * h * 4);
    if (!px)
        return b;
    HDC dc = GetDC(0);
    GetDIBits(dc, bmp, 0, (UINT)h, px, &bi, DIB_RGB_COLORS);
    ReleaseDC(0, dc);

    int x0 = 0, y0 = 0, x1 = w, y1 = h;
    if (region) {
        x0 = region->left   < 0 ? 0 : region->left;
        y0 = region->top    < 0 ? 0 : region->top;
        x1 = region->right  > w ? w : region->right;
        y1 = region->bottom > h ? h : region->bottom;
    }
    // A DIB section is BGRA in memory; a COLORREF is 0x00BBGGRR.
    BYTE wantB = GetBValue(bg), wantG = GetGValue(bg), wantR = GetRValue(bg);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            BYTE* p = px + ((SIZE_T)y * w + x) * 4;
            if (p[0] != wantB || p[1] != wantG || p[2] != wantR) {
                b.any = true;
                if (x < b.left)   b.left = x;
                if (y < b.top)    b.top = y;
                if (x > b.right)  b.right = x;
                if (y > b.bottom) b.bottom = y;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, px);
    return b;
}

static InkBox inkOf(HBITMAP bmp, int w, int h)
{
    return inkOfRegion(bmp, w, h, 0, RGB(0xFF, 0xFF, 0xFF));
}

// One child control, drawn into the page surface where it sits.
//
// A PRIVATE DESKTOP IS NOT PAINTED BY WINDOWS. That is measured, not assumed: the
// first version of this harness BitBlt'ed from the private desktop's screen DC and
// every capture came back a uniform blank of exactly the same file size. So the
// controls are asked to draw themselves. WM_PRINTCLIENT is the message a standard
// control answers by rendering its client area into the DC it is given - which is
// how the two EDIT panes, the ones carrying the installer's own words, get into
// the picture at all.
static void printChildInto(HDC mem, HWND ctl)
{
    if (!ctl || !IsWindowVisible(ctl))
        return;
    RECT r;
    GetWindowRect(ctl, &r);
    MapWindowPoints(HWND_DESKTOP, g_page, (POINT*)&r, 2);
    int save = SaveDC(mem);
    // The client edge is not part of the client area, so it is drawn here: without
    // it the pane's boundary would be invisible in the picture, and the boundary is
    // the thing a reader needs in order to judge whether text is being cut by it.
    HPEN    pn = CreatePen(PS_SOLID, 1, CLR_LINE);
    HGDIOBJ op = SelectObject(mem, pn);
    HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
    Rectangle(mem, r.left - 1, r.top - 1, r.right + 1, r.bottom + 1);
    SelectObject(mem, op);
    SelectObject(mem, ob);
    DeleteObject(pn);
    SetViewportOrgEx(mem, r.left, r.top, 0);
    IntersectClipRect(mem, 0, 0, r.right - r.left, r.bottom - r.top);
    SendMessageW(ctl, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    RestoreDC(mem, save);
}

// Which KIND of screen entry i is, on the wizard being rendered.
//
// This harness walks the flow by INDEX, because the captures are named by position
// and their names are what a reviewer compares between rounds. What it DISPATCHES
// on is the kind, exactly like the product's own painter - so a flow that puts its
// work screen sixth is rendered correctly here without this file being told.
static ScreenKind kindAt(int i)
{
    return (g_wiz && i >= 0 && i < screenCount(g_wiz)) ? g_wiz->screens[i].kind
                                                       : kScreenInfo;
}

// The title POINTER entry i carries. Used to tell two screens of one kind apart -
// see pageName() - and it is the pointer and not the words on purpose.
static const wchar_t* titleAt(int i)
{
    return (g_wiz && i >= 0 && i < screenCount(g_wiz)) ? g_wiz->screens[i].title : 0;
}

// "This entry is the old page 2." The product's own field, read rather than
// guessed: three of the measurements below are about the pane, the picture and the
// two foot band buttons, and all three belong to that one screen and not to check
// screens in general. See Screen::paintsMachineReview.
static bool reviewAt(int i)
{
    return g_wiz && i >= 0 && i < screenCount(g_wiz) &&
           g_wiz->screens[i].kind == kScreenCheck &&
           g_wiz->screens[i].paintsMachineReview;
}

// "This entry asks for a pane." The product's own field again, and it is a DIFFERENT
// question from reviewAt() as of this round - the machine review has a pane and so
// does the MIDI port screen. Everything about the strip the painted content gets
// asks this, because layout() takes the pane's height out of the page for any screen
// that has one; asking reviewAt() instead would say "the whole page scrolls" about a
// screen where it does not.
static bool paneAt(int i)
{
    return g_wiz && i >= 0 && i < screenCount(g_wiz) &&
           g_wiz->screens[i].paneText != 0 && g_wiz->screens[i].paneText[0] != 0;
}

// ...and "this entry offers an action", for the same reason: the contextual button
// belongs to a screen now, and how many screens show one is a property of the flow
// that only a walk over the table can answer.
static bool actionAt(int i)
{
    return g_wiz && i >= 0 && i < screenCount(g_wiz) &&
           g_wiz->screens[i].action != 0 &&
           g_wiz->screens[i].actionLabel != 0 &&
           g_wiz->screens[i].actionLabel[0] != 0;
}

// Renders one page into a surface TALLER than the window, exactly the way
// pageProc's WM_PAINT does, and answers where the ink stopped.
static InkBox renderTall(int page, int pw, int ph, int contentH, const wchar_t* png,
                         bool withChildren)
{
    int surfaceH = contentH + 200;   // deliberate slack: ink that runs past the
                                     // reported content height has to be visible
    if (withChildren && surfaceH < ph + 200)
        surfaceH = ph + 200;
    HDC     screen = GetDC(0);
    HDC     mem    = CreateCompatibleDC(screen);
    HBITMAP bmp    = CreateCompatibleBitmap(screen, pw, surfaceH);
    HGDIOBJ ob     = SelectObject(mem, bmp);
    fillRect(mem, 0, 0, pw, surfaceH, CLR_PAGE_BG);
    SetBkMode(mem, TRANSPARENT);
    switch (kindAt(page)) {
    case kScreenInfo:  renderInfoScreen(mem, pw, ph, false);  break;
    // The product's own dispatch, and not renderReview() directly. A check screen
    // that carries its own subject is painted by renderSubject(), and a camera that
    // called renderReview() for every kScreenCheck would photograph a page the
    // window does not draw - the same picture twice, under two different names.
    case kScreenCheck: renderCheckScreen(mem, pw, ph, false); break;
    case kScreenWork:  renderWorkChrome(mem, pw, false);      break;
    case kScreenDone:  renderDoneChrome(mem, pw, false);      break;
    }
    // The ink box is taken BEFORE the children go on, because the question it
    // answers is about the PAINTED text - the only text in this program that a
    // window can clip. The panes are windows and cannot be clipped by the page:
    // they wrap and they scroll.
    InkBox ink = inkOf(bmp, pw, surfaceH);
    if (withChildren) {
        if (kindAt(page) == kScreenWork) {
            printChildInto(mem, g_bar);
            printChildInto(mem, g_log);
        } else if (kindAt(page) == kScreenDone) {
            printChildInto(mem, g_summary);
        }
    }
    if (png)
        savePngChecked(bmp, pw, surfaceH, png);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(0, screen);
    return ink;
}

// ===========================================================================
// THE HEADER BAND - the half of the pixel proof that was missing.
//
// WHAT THE HOLE WAS, because the shape of it matters more than the defect. The
// measurement above proves "no ink below the reported content height" for the PAGE,
// four pages at two DPIs, 8 of 8. The header band is painted by the FRAME, outside
// every pixel that measurement looks at. So the proof was not wrong and it was not
// weak - it was SCOPED, and the number 8 of 8 looked like it covered everything.
// That is the most dangerous kind of hole there is, and the thing that found the
// clipped g was the owner's eye on the second real run.
//
// TWO REASONS inkOf() COULD NOT SIMPLY BE POINTED AT THE BAND. The band has its own
// background colour, so "not white" is the whole band rather than the text in it -
// hence inkOfRegion(). And a box that clips its text produces ink that stops FLUSH
// against the box, which is not by itself wrong: the subhead's own descenders land
// on the very last row of its cell at 96 DPI, legitimately. A constant to compare
// against would therefore be either a false alarm or a rubber stamp.
//
// SO THE YARDSTICK IS THE FONT ITSELF. The same string, the same font, the same
// box, the same flags, the same colours - with the bottom of the box taken away.
// Whatever ink that produces is what the band is supposed to show. The band is
// right when the painted ink reaches exactly as far down as that, and wrong the
// moment it stops short.
//
// WHAT THAT ORACLE DOES AND DOES NOT GUARANTEE. This comment used to end "at any DPI
// and in any face, with no number to keep in step", which is true of VERTICAL
// CLIPPING and was being read as a claim about the band. It is one question - is the
// box at least as tall as the font's cell - and it says nothing about three others,
// each of which the reviewer of round 3 walked as a live path to a green failure:
//
//   WHERE the box is. The oracle's box IS hb.title, so moving the box moves the ruler
//   with it. Set kHeadPadTop to 0 and the headline starts on row 7 instead of 19,
//   visibly against the top of the window, and all twelve band checks print [ok]. The
//   same for kHeadPadBottom 8 -> 0 and kHeadGap 2 -> 0: the kHeadH floor swallows all
//   three, because a band that needs less than the floor is given the floor and the
//   space the constant used to hold becomes dead space nothing measured. Closed by
//   requiring the band's height to BE the sum of its parts - which is also what makes
//   the 96 DPI tie (derived 70, floor 70) visible instead of silent. That check is
//   what catches all three; the top padding check beside it answers a different
//   question and is honest about which, in the comment on it.
//     WHICH DPI ACTUALLY DOES THE CATCHING, MEASURED, because the sentence that used
//     to sit here got it backwards. 96 DPI: derived 70 against a floor of 70, slack
//     ZERO. 144 DPI: derived 108 against a floor of 105, slack 3. The floor is what
//     hides a reduction, so the DPI with no slack is the strict one: ANY reduction of
//     any of the three constants drops derived below 70 and is caught at 96 DPI ALONE.
//     144 is the lax half - kHeadGap 2 -> 0 lands its arithmetic on exactly 105, which
//     is exactly the floor, and passes there. The old comment called this "a check the
//     SUITE makes over two DPIs and not one either DPI makes alone", which is a
//     symmetric conclusion drawn from an asymmetric measurement: 96 makes it alone and
//     144 does not make it at all. Keeping 144 is still worth it, for the other eleven
//     checks and because the slack is what would change first if the fonts changed.
//
//     AND IT IS LOCKED ONLY FROM BELOW, which nothing here catches. Every INCREASE
//     passes: kHeadPadTop = 40 gives derived 98 and a height of 98, so the equality
//     holds, and the check beside this one that looks at the space above the headline
//     passes too because BOTH ITS SIDES READ S(kHeadPadTop). A headline pushed 28
//     pixels down the band is twelve green results. Left as a known limit rather than
//     closed with a literal: a number kept in step by hand is the failure mode this
//     whole file is arranged to avoid, and an oversized band is a defect the eye
//     catches on the first run while a clipped descender is not.
//
//   HORIZONTAL clipping. paintFrameInto() passes DT_END_ELLIPSIS, and the oracle
//   reproduces the flag and the box width exactly, so a headline shortened to "..."
//   by a longer string or a narrower window would produce IDENTICAL ink on both sides
//   and pass. Not live today - the ink's right edge is 482 of 700 at 96 DPI and 722 of
//   1050 at 144, and the window cannot be resized - so it is closed by measuring what
//   each string NEEDS (DT_CALCRECT, no ellipsis) against the room its box has.
//
//   WHETHER THERE IS ANY INK AT ALL in each line. If CLR_HEAD_TXT ever equalled
//   CLR_HEAD_BG, painted ink and unclipped ink would both be -1, the equality would
//   pass, and bandInk.any would survive on the subhead's ink alone. Closed by asking
//   each of the two boxes separately whether anything in it differs from the band.
// ===========================================================================
static bool hasDescender(const wchar_t* s)
{
    for (; s && *s; s++)
        if (*s == L'g' || *s == L'j' || *s == L'p' || *s == L'q' || *s == L'y')
            return true;
    return false;
}

// What each string needs horizontally, with nothing allowed to shorten it. This is
// the same measurement DrawTextW makes before it decides to elide: DT_CALCRECT and
// NO DT_END_ELLIPSIS, so the answer is the full string's width whether it fits or
// not. Compared against the box's width it answers the one question the ink oracle
// cannot: is anything being replaced by "..."?
//
// IT FAILS CLOSED, which it did not. On any GDI failure - no memory DC, a font that
// will not select, DrawTextW refusing - r was left at all zeroes and this returned 0,
// and the caller's "needs 0, has 648" printed [ok]. A measurement whose failure mode
// is a pass is worse than no measurement, and unclippedInk() beside it already fails
// closed through its -1 sentinel. So a failure returns a width nothing can fit in:
// the comparison goes red and the message says 2147483647, which reads as a broken
// measurement rather than as a narrow string.
static int neededWidth(HFONT font, const wchar_t* text)
{
    HDC screen = GetDC(0);
    HDC mem    = CreateCompatibleDC(screen);
    if (!mem) {
        if (screen)
            ReleaseDC(0, screen);
        return INT_MAX;
    }
    HGDIOBJ of = SelectObject(mem, font);
    RECT    r  = { 0, 0, 0, 0 };
    int     h  = DrawTextW(mem, text, -1, &r, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(mem, of);
    DeleteDC(mem);
    ReleaseDC(0, screen);
    if (h <= 0)
        return INT_MAX;
    return r.right - r.left;
}

// Where the ink of one line really is when nothing can clip it. The whole box is
// returned, not just its bottom: the top is what says how much of the padding above
// the headline is the font's own leading and how much is kHeadPadTop.
static InkBox unclippedInk(HFONT font, const wchar_t* text, RECT box,
                           COLORREF fg, COLORREF bg)
{
    int cell = box.bottom - box.top;
    if (cell < 1)
        cell = 32;
    int w = box.right + 2;
    int h = box.bottom + 4 * cell + 64;
    HDC     screen = GetDC(0);
    HDC     mem    = CreateCompatibleDC(screen);
    HBITMAP bmp    = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ ob     = SelectObject(mem, bmp);
    fillRect(mem, 0, 0, w, h, bg);
    SetBkMode(mem, TRANSPARENT);
    SelectObject(mem, font);
    SetTextColor(mem, fg);
    RECT r = box;
    r.bottom = h;                 // the ONLY difference from what the product does
    DrawTextW(mem, text, -1, &r, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    InkBox ink = inkOfRegion(bmp, w, h, 0, bg);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(0, screen);
    return ink;
}

static void measureHeadBand(Wizard* wiz, int dpi)
{
    RECT fc;
    GetClientRect(g_frame, &fc);
    int cw = fc.right, ch = fc.bottom;

    HeadBand hb;
    {
        HDC probe = GetDC(g_frame);
        hb = headBand(probe, cw);
        ReleaseDC(g_frame, probe);
    }

    // Deliberately SHORTER than the window: the band, plus a strip of what is under
    // it. The foot band paintFrameInto() draws at ch - kFootH falls outside this
    // bitmap and is clipped away, which is what leaves that strip as untouched paint
    // and therefore makes "the band did not leak into the page" measurable.
    int     surfaceH = hb.height + S(24);
    HDC     screen   = GetDC(0);
    HDC     mem      = CreateCompatibleDC(screen);
    HBITMAP bmp      = CreateCompatibleBitmap(screen, cw, surfaceH);
    HGDIOBJ ob       = SelectObject(mem, bmp);
    fillRect(mem, 0, 0, cw, surfaceH, CLR_PAGE_BG);   // what is really under it
    paintFrameInto(mem, cw, ch);

    RECT   band     = { 0, 0, cw, hb.height - hb.accentH };
    RECT   below    = { 0, hb.height, cw, surfaceH };
    InkBox bandInk  = inkOfRegion(bmp, cw, surfaceH, &band,     CLR_HEAD_BG);
    InkBox titleInk = inkOfRegion(bmp, cw, surfaceH, &hb.title, CLR_HEAD_BG);
    InkBox subInk   = inkOfRegion(bmp, cw, surfaceH, &hb.sub,   CLR_HEAD_BG);
    InkBox belowInk = inkOfRegion(bmp, cw, surfaceH, &below,    CLR_PAGE_BG);

    int titleWant = unclippedInk(g_fHead, wiz->headline, hb.title,
                                 CLR_HEAD_TXT, CLR_HEAD_BG).bottom;
    int subWant   = unclippedInk(g_fHeadSmall, wiz->subhead, hb.sub,
                                 CLR_HEAD_DIM, CLR_HEAD_BG).bottom;

    // The same headline in the same box moved to the top of the bitmap, so that its
    // ink's distance from the top of its CELL is measured on its own. That number is
    // the font's internal leading and it belongs to the font, not to the layout: it is
    // what lets the padding above the headline be checked against kHeadPadTop instead
    // of against itself.
    RECT atTop     = hb.title;
    atTop.bottom  -= atTop.top;
    atTop.top      = 0;
    int  leadTop   = unclippedInk(g_fHead, wiz->headline, atTop,
                                  CLR_HEAD_TXT, CLR_HEAD_BG).top;

    // The band's height as the fonts and the three paddings ask for it, beside the
    // floor that is allowed to raise it. They are EQUAL at 96 DPI, which is why the
    // band the owner has already seen is still 70 tall - a coincidence worth printing,
    // because it is exactly what hid the three constants from every check here.
    int derived = hb.sub.bottom + S(kHeadPadBottom) + hb.accentH;
    int floorH  = S(kHeadH);

    int titleNeed = neededWidth(g_fHead, wiz->headline);
    int subNeed   = neededWidth(g_fHeadSmall, wiz->subhead);
    int room      = hb.title.right - hb.title.left;

    wchar_t png[512];
    _snwprintf(png, 500, L"%s\\header-band-%ddpi.png", g_shotDir, dpi);
    savePngChecked(bmp, cw, surfaceH, png);

    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(0, screen);

    int rule = hb.height - hb.accentH;      // first row of the rule that closes it
    wprintf(L"  header band cw=%4d height=%3d rule=%3d title=%d..%d sub=%d..%d "
            L"ink=(%d,%d)-(%d,%d)\n",
            cw, hb.height, rule, (int)hb.title.top, (int)hb.title.bottom,
            (int)hb.sub.top, (int)hb.sub.bottom, bandInk.left, bandInk.top,
            bandInk.right, bandInk.bottom);
    wprintf(L"    from the fonts and the paddings: %d; kHeadH floor: %d; %s\n",
            derived, floorH,
            derived == floorH ? L"EQUAL - the floor changes nothing here" :
            derived >  floorH ? L"the paddings win, the floor is not reached" :
                                L"THE FLOOR IS PADDING THE BAND");
    wprintf(L"    padding above the headline: %d = kHeadPadTop %d + the font's own %d\n",
            bandInk.top, S(kHeadPadTop), leadTop);
    wprintf(L"    widths needed/available: headline %d/%d, subhead %d/%d\n",
            titleNeed, room, subNeed, room);

    wchar_t what[256];
    _snwprintf(what, 250, L"%ddpi header band: something is painted in it", dpi);
    check(bandInk.any, what);

    // The measurement can only see a cut tail if there is a tail to cut. Asserted,
    // not assumed: a headline without a descender would make every check below
    // pass while proving nothing at all about descenders.
    _snwprintf(what, 250,
               L"%ddpi header band: both lines carry a descender (g j p q y), so a cut "
               L"tail is visible to this measurement", dpi);
    check(hasDescender(wiz->headline) && hasDescender(wiz->subhead), what);

    // *** THE CLIPPED g, AS A NUMBER. ***
    _snwprintf(what, 250,
               L"%ddpi header band: the headline is NOT cut - painted ink ends on row %d, "
               L"the font's own ink on row %d", dpi, titleInk.bottom, titleWant);
    check(titleInk.bottom == titleWant, what);

    _snwprintf(what, 250,
               L"%ddpi header band: the subhead is NOT cut - painted ink ends on row %d, "
               L"the font's own ink on row %d", dpi, subInk.bottom, subWant);
    check(subInk.bottom == subWant, what);

    // The owner's own words for the defect: "it needs more space with the line
    // below". So the lowest ink in the band and the rule that closes it are not
    // allowed to touch.
    _snwprintf(what, 250,
               L"%ddpi header band: %d clear rows between the lowest ink (%d) and the rule "
               L"that closes the band (%d)", dpi, rule - bandInk.bottom - 1,
               bandInk.bottom, rule);
    check(bandInk.bottom >= 0 && bandInk.bottom < rule - 1, what);

    // And the same question the page checks ask, asked of the band: nothing painted
    // outside what it says it occupies. The page begins on row hb.height.
    _snwprintf(what, 250,
               L"%ddpi header band: nothing leaks below the band, where the page begins "
               L"(row %d)", dpi, hb.height);
    check(!belowInk.any, what);

    // *** THE THREE CONSTANTS, WHICH NOTHING ABOVE THIS LINE LOOKS AT. ***
    //
    // Every check so far uses hb.title and hb.sub as both the subject and the ruler, so
    // all three of kHeadPadTop, kHeadGap and kHeadPadBottom can be set to 0 with twelve
    // green results and a title jammed against the top of the window. The reason they
    // hide is the kHeadH floor: a band that asks for less than the floor is given the
    // floor, so the constants come out of the height and go into dead space that no
    // check measured. THIS is the check that ties them to something, and it is the same
    // check that makes the 96 DPI tie visible: derived 70 against a floor of 70 today,
    // and anything less than 70 the moment one of the three is reduced.
    //
    // *** WHY IT IS NOT "height == max(sum of parts, floor)", WHICH IS THE OBVIOUS
    //     WAY TO STOP IT FROM FORBIDDING THE FLOOR TO ACT. ***
    //
    // Because that is headBand()'s own last two lines, copied. b.height is the sum of
    // the parts, and then "if (b.height < S(kHeadH)) b.height = S(kHeadH)" - which IS
    // max(sum, floor). An assertion in that shape cannot fail for any value of any of
    // the three constants, at any DPI, in any font: it is an identity, not a check.
    // The question to ask about a weaker check is which value slips past it, and the
    // answer for that one is EVERY value, including the kHeadPadTop 0 that started
    // this whole line of measurement. So it stays as an equality.
    //
    // WHAT THAT COSTS, AND IT IS A REAL COST. This check asserts the floor is IDLE, and
    // gui.cpp calls kHeadH a floor that may raise the band. The two agree only while the
    // two font cells sum to at least 45 logical pixels at 96 DPI, and today they sum to
    // exactly 45 (30 for the headline, 15 for the subhead). One pixel of margin. On a
    // machine whose message font is a pixel shorter, the CORRECT behaviour - the floor
    // stepping in to keep the band from collapsing - is reported here as a failure.
    //
    // That is the trade taken deliberately, and it is taken because of what the two
    // failures cost. A false alarm here is a red line in a harness, read by whoever ran
    // it, on a machine nobody has yet seen; the thing it is trading against is a
    // silently dead constant in the band that the owner already caught once by eye.
    // gui.cpp now says which of the two kHeadH is - a guard for the degenerate case,
    // not a routine part of the arithmetic - so the product and this file stop
    // contradicting each other about it.
    _snwprintf(what, 250,
               L"%ddpi header band: the height is the sum of its parts and NOT the kHeadH "
               L"floor (fonts plus paddings %d, floor %d, height %d)",
               dpi, derived, floorH, hb.height);
    check(hb.height == derived, what);

    // The padding above the headline, decomposed: what the layout asked for, plus what
    // the font adds on its own.
    //
    // WHAT THIS ONE DOES NOT CATCH, said here because the check above it exists for
    // exactly this reason. BOTH sides read S(kHeadPadTop), so setting kHeadPadTop to 0
    // moves the ink to row 7 and this check still prints [ok] - measured, not reasoned:
    // it did. Only the height check above fails on that. What this one catches is the
    // painter and the layout disagreeing about the box: ink above the box headBand()
    // handed out, or a face whose leading is not what it was when the numbers were
    // picked. leadTop is measured with the box moved to row 0, so it is the font's
    // number and not the layout's.
    _snwprintf(what, 250,
               L"%ddpi header band: the space above the headline is kHeadPadTop - highest "
               L"ink on row %d, wanted %d + %d of the font's own leading",
               dpi, bandInk.top, S(kHeadPadTop), leadTop);
    check(bandInk.top == S(kHeadPadTop) + leadTop, what);

    // *** BOTH LINES ARE THERE AT ALL. *** The ink comparisons above are equalities
    // between two measurements of the same thing, so a headline painted in the band's
    // own colour makes both sides -1 and passes; bandInk.any would survive on the
    // subhead alone. Each box is therefore asked on its own.
    _snwprintf(what, 250,
               L"%ddpi header band: BOTH lines really are painted in a colour the band is "
               L"not (title %d, subhead %d)", dpi, titleInk.any ? 1 : 0,
               subInk.any ? 1 : 0);
    check(titleInk.any && subInk.any, what);

    // *** AND NOTHING IS CUT SIDEWAYS. *** paintFrameInto() passes DT_END_ELLIPSIS and
    // the oracle above reproduces the flag and the box, so an elided line gives
    // identical ink on both sides and passes. What each string NEEDS does not come from
    // the box at all.
    _snwprintf(what, 250,
               L"%ddpi header band: the headline is not shortened to \"...\" - it needs %d "
               L"pixels and its box gives it %d", dpi, titleNeed, room);
    check(titleNeed <= room, what);
    _snwprintf(what, 250,
               L"%ddpi header band: the subhead is not shortened to \"...\" - it needs %d "
               L"pixels and its box gives it %d", dpi, subNeed, room);
    check(subNeed <= room, what);

    // The horizontal twin of "nothing leaks below the band": the band's ink stays inside
    // the two boxes, so nothing is running under the window's right edge either.
    _snwprintf(what, 250,
               L"%ddpi header band: no ink outside the boxes sideways - ink spans %d..%d, "
               L"the boxes %d..%d", dpi, bandInk.left, bandInk.right,
               (int)hb.title.left, (int)hb.title.right);
    check(bandInk.left >= hb.title.left && bandInk.right < hb.title.right, what);
}

// ===========================================================================
// THE FOOT BAND'S NOTE - the OTHER half of the frame that nothing measured.
//
// Round 3 extended the pixel proof from the page to the HEADER band. The foot band was
// still outside every measurement, and it carries a painted string of its own: the
// sentence beside the buttons that says why Install is grey, or why Cancel is. It was
// drawn on ONE line with DT_END_ELLIPSIS across most of the window's width, and the
// longest sentence either program puts there is 63 characters - so on a narrow enough
// band it was already losing its tail, silently, with nothing to notice.
//
// Task 1 puts a button in that band, which takes room the note used to have. Squeezing
// an unmeasured string is how the header band's clipped g happened, so the note now
// WRAPS and this is the measurement that says the wrapped text fits. Same oracle as
// the band's: what the string needs, against what its box gives.
// ===========================================================================
// A picture of the foot band as it stands on WHATEVER SCREEN the window is on.
//
// *** IT IS A FUNCTION BECAUSE THE BAND NOW HAS TO BE PHOTOGRAPHED TWICE, AND THE
//     SECOND ONE IS THE POINT OF THIS ROUND. *** The only tracked picture of this
// band was taken on the check screen, so the primary button in it has always read
// "Install" - which means the button the owner actually objected to, the one on the
// OPENING that said Install and installed nothing, appeared in no committed image in
// this repository. Six rounds of review looked at sixteen captures and none of them
// contained the defect. A second call, on the opening screen, is what closes that.
//
// One function and not two copies of thirty lines: two copies would be two pictures
// that could come to be taken of different things, which is the whole reason
// headBand() and footNote() are functions in the product.
static void saveFootBand(int dpi, const wchar_t* stem)
{
    RECT fc;
    GetClientRect(g_frame, &fc);

    HDC     screen = GetDC(0);
    HDC     mem    = CreateCompatibleDC(screen);
    HBITMAP bmp    = CreateCompatibleBitmap(screen, fc.right, fc.bottom);
    HGDIOBJ ob     = SelectObject(mem, bmp);
    fillRect(mem, 0, 0, fc.right, fc.bottom, CLR_PAGE_BG);
    SetBkMode(mem, TRANSPARENT);
    paintFrameInto(mem, fc.right, fc.bottom);

    // The buttons are children of the FRAME, so they are mapped to the frame rather
    // than to the page - printChildInto() maps to the page and would put every one of
    // them in the wrong place here.
    //
    // *** THE NAMED DOOR IS IN THIS LIST, AND THE FIRST VERSION OF THIS ROUND LEFT IT
    //     OUT. *** The array was four entries and the round that added a fifth button
    // did not grow it, so foot-band-binding-96dpi.png came out showing "Check again",
    // Back and a greyed Next - a picture of the one screen in this program that
    // refuses to advance, with the ONLY way past it missing. That is precisely the
    // defect the owner found in two seconds: a button that was in no committed image.
    // It was found here by looking at the capture, which is why looking at them is a
    // step and not a courtesy.
    HWND kids[5] = { g_recheckBtn, g_actionBtn, g_overrideBtn, g_secondary,
                     g_primary };
    for (int i = 0; i < 5; i++) {
        if (!kids[i] || !IsWindowVisible(kids[i]))
            continue;
        RECT kr;
        GetWindowRect(kids[i], &kr);
        MapWindowPoints(HWND_DESKTOP, g_frame, (POINT*)&kr, 2);
        int save = SaveDC(mem);
        SetViewportOrgEx(mem, kr.left, kr.top, 0);
        IntersectClipRect(mem, 0, 0, kr.right - kr.left, kr.bottom - kr.top);
        SendMessageW(kids[i], WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
        RestoreDC(mem, save);
    }

    // Only the band, plus a strip of the page above it, so that the rule which
    // closes the page is in the picture and the band's height can be judged.
    int     top  = fc.bottom - S(kFootH) - S(10);
    int     tall = fc.bottom - top;
    HDC     mem2 = CreateCompatibleDC(screen);
    HBITMAP b2   = CreateCompatibleBitmap(screen, fc.right, tall);
    HGDIOBJ ob2  = SelectObject(mem2, b2);
    BitBlt(mem2, 0, 0, fc.right, tall, mem, 0, top, SRCCOPY);

    wchar_t png[512];
    _snwprintf(png, 500, L"%s\\%s-%ddpi.png", g_shotDir, stem, dpi);
    savePngChecked(b2, fc.right, tall, png);

    SelectObject(mem2, ob2);
    DeleteObject(b2);
    DeleteDC(mem2);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(0, screen);
}

static void measureFootNote(int dpi)
{
    RECT fc;
    GetClientRect(g_frame, &fc);
    HDC      probe = GetDC(g_frame);
    FootNote fn    = footNote(probe, fc.right, fc.bottom);
    int      lh    = lineHeight(probe, g_fSmall);
    ReleaseDC(g_frame, probe);

    int boxW = fn.box.right - fn.box.left;
    int boxH = fn.box.bottom - fn.box.top;
    wprintf(L"  foot note box=%d..%d x %d..%d (%dx%d) needs %d, one line is %d\n",
            (int)fn.box.left, (int)fn.box.right, (int)fn.box.top, (int)fn.box.bottom,
            boxW, boxH, fn.needH, lh);

    // The only message in this function that interpolates a string whose length is
    // not fixed by a literal here, so it is the only one that can hit MSVC's
    // _snwprintf truncation rule - the count is written and the terminator is not.
    wchar_t what[400];
    _snwprintf(what, 390,
               L"%ddpi foot band: page 2 really does have a note to measure (%s)", dpi,
               fn.text ? fn.text : L"none");
    what[390] = 0;
    check(fn.text != 0, what);

    // *** THE ONE THAT WOULD HAVE CAUGHT THE CUT TAIL. ***
    _snwprintf(what, 390,
               L"%ddpi foot band: the note FITS its box wrapped - it needs %d rows of "
               L"%d and the box gives %d", dpi, fn.needH, boxW, boxH);
    check(fn.needH > 0 && fn.needH <= boxH, what);

    // And the button and the note cannot be drawn on top of one another, which is the
    // failure this round could have introduced.
    RECT br;
    ZeroMemory(&br, sizeof(br));
    bool haveBtn = g_recheckBtn && IsWindowVisible(g_recheckBtn);
    if (haveBtn) {
        GetWindowRect(g_recheckBtn, &br);
        MapWindowPoints(HWND_DESKTOP, g_frame, (POINT*)&br, 2);
    }
    _snwprintf(what, 390,
               L"%ddpi foot band: the Check again button is on page 2 and the note "
               L"starts after it (button %d..%d, note from %d)", dpi,
               (int)br.left, (int)br.right, (int)fn.box.left);
    check(haveBtn && br.right <= fn.box.left && br.bottom <= fc.bottom, what);

    // *** THE ACTION BUTTON IS NO LONGER IN THIS BAND, AND THAT IS WHAT THIS ROUND
    //     MOVED. *** Three checks used to sit here about a button that stood next to
    // this note: where it was pinned, that /preview greyed it, and that both of its
    // labels fitted. The button belongs to the screen whose subject it acts on now,
    // so those three moved with it into measureActionButton() and are made on the
    // screen that really carries it. Leaving them here would have been three checks
    // asserting a button on a screen that does not have one - and they would have
    // failed, correctly, about the wrong page.
    //
    // What that leaves in THIS band is "Check again" and the note, and the note's box
    // is measured from wherever the visible buttons end, so it is wider than it was.
    RECT worstBoxRect = fn.box;

    // *** AND EVERY NOTE THAT CAN LAND IN THAT BAND STILL FITS BESIDE THE BUTTONS.
    //
    // Only one of these three is ever rendered - the /preview one - so the other two
    // are exactly the kind of text that gets cut with nothing measuring it, which is
    // what happened to this note once already. The box is page 2's, which is the
    // narrowest any of them can land in.
    {
        static const wchar_t* const notes[4] = {
            kNotePreview, kNoteNoAdmin, kNoteNoPaths, 0
        };
        HDC dc2      = GetDC(g_frame);
        int worstH   = 0;
        int wBoxW    = (int)(worstBoxRect.right - worstBoxRect.left);
        int wBoxH    = (int)(worstBoxRect.bottom - worstBoxRect.top);
        const wchar_t* worstNote = L"";
        SelectObject(dc2, g_fSmall);
        for (int i = 0; notes[i]; i++) {
            RECT calc = worstBoxRect;
            DrawTextW(dc2, notes[i], -1, &calc,
                      DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            int need = calc.bottom - calc.top;
            wprintf(L"    note (%d chars) needs %d of %d in the worst box (%d wide)\n",
                    (int)wcslen(notes[i]), need, wBoxH, wBoxW);
            if (need > worstH) {
                worstH    = need;
                worstNote = notes[i];
            }
        }
        ReleaseDC(g_frame, dc2);
        _snwprintf(what, 390,
                   L"%ddpi foot band: EVERY note this program can put there fits "
                   L"beside the two buttons at their WIDEST - the worst needs %d of "
                   L"%d (\"%s\")", dpi, worstH, wBoxH, worstNote);
        what[390] = 0;
        check(worstH > 0 && worstH <= wBoxH, what);
    }

    // -------------------------------------------------------------------
    // A PICTURE OF THE BAND, WHICH NO CAPTURE HAD EVER SHOWN.
    //
    // Every tracked picture until round 4 is of the PAGE window. The foot band is
    // painted by the frame, outside all of them - so "Check again" was on this
    // window for a whole round without appearing in a single committed image.
    // The numbers above are the proof; this is so that a human can look at the
    // decision the numbers are about.
    // -------------------------------------------------------------------
    saveFootBand(dpi, L"foot-band");
}

// ===========================================================================
// THE CONTEXTUAL ACTION BUTTON, MEASURED ON THE SCREEN THAT CARRIES IT
//
// *** THESE THREE USED TO BE ASKED ON PAGE 2, AND THAT WAS THE DEFECT AND NOT THE
//     ARRANGEMENT. *** The offer is about teVirtualMIDI; page 2 is about four
// unrelated subjects at once. A button reading "Install loopMIDI..." beside rows about
// the USB cable and the WinUSB binding is association by position, which is precisely
// what the labels were written to make impossible - and the checks that measured it
// there were measuring it in the one place it did not belong.
//
// It is still in the FOOT BAND rather than on the page, and that reason has not
// changed: the page SCROLLS and scrollTo() deliberately omits SW_SCROLLCHILDREN, so a
// control on the page stands still while the painted content slides under it. The
// band is the one strip that never moves. That constraint dissolves only when the
// page stops scrolling, which is a later task.
//
// *** IT TAKES THE SCREEN'S OWN LABEL SET NOW, AND IT USED TO NAME THE MIDI PORT
//     SCREEN'S TWO. *** The list was `{ kOfferWingetLabel, kOfferOpenPageLabel }`,
// written into the "every label this button can carry fits inside it" loop, which was
// correct while one screen carried the button. "Get Zadig" carries one now, and calling
// this for that screen with the loopMIDI labels would have measured two strings that
// cannot appear on it while never measuring the one that can - a check that runs, passes
// and looks at the wrong thing.
//
// *** AND THE EXPECTED x IS COMPUTED HERE RATHER THAN ASKED OF actionBand(). *** The
// old assertion was `ar.left == br.right + S(kActionGap)` and it required a VISIBLE
// "Check again" to compare against. "Get Zadig" is kScreenInfo, so the re-check button
// is hidden on it and that assertion would have failed for the wrong reason - while the
// thing actually worth catching is the opposite one: gui.cpp's actionBand() used to
// offset itself by the re-check's width whenever the FLOW had a re-check, so on this
// screen the button would have been pinned 114 logical pixels to the right of nothing.
// The expectation is therefore this file's own arithmetic - S(24) at the left margin, or
// one gap after the re-check when the re-check is really there - against the real
// window's real position.
// ===========================================================================
static void measureActionButton(int dpi, int page, const wchar_t* screen,
                                const wchar_t* const* labels, int nLabels)
{
    wchar_t what[400];

    RECT fc;
    GetClientRect(g_frame, &fc);
    HDC      probe = GetDC(g_frame);
    FootNote fn    = footNote(probe, fc.right, fc.bottom);
    ReleaseDC(g_frame, probe);

    RECT br;
    ZeroMemory(&br, sizeof(br));
    bool haveRecheck = g_recheckBtn && IsWindowVisible(g_recheckBtn);
    if (haveRecheck) {
        GetWindowRect(g_recheckBtn, &br);
        MapWindowPoints(HWND_DESKTOP, g_frame, (POINT*)&br, 2);
    }

    RECT ar;
    ZeroMemory(&ar, sizeof(ar));
    bool haveAction = g_actionBtn && IsWindowVisible(g_actionBtn);
    if (haveAction) {
        GetWindowRect(g_actionBtn, &ar);
        MapWindowPoints(HWND_DESKTOP, g_frame, (POINT*)&ar, 2);
    }
    wchar_t label[128];
    label[0] = 0;
    if (g_actionBtn)
        GetWindowTextW(g_actionBtn, label, 128);
    wprintf(L"  action button \"%s\" at %d..%d, enabled %d\n", label,
            (int)ar.left, (int)ar.right, IsWindowEnabled(g_actionBtn) ? 1 : 0);

    // *** THE WORDS ON THE CONTROL ARE THE WORDS THIS SCREEN'S ENTRY CARRIES. ***
    //
    // WHERE EACH SIDE COMES FROM: the left is read back out of the real BUTTON window
    // with GetWindowTextW, after the real setScreen() ran; the right is
    // Screen::actionLabel, read out of the table. One is a control and the other is
    // data, which is the same pairing the primary button's own check uses - and it is
    // the pairing that was missing, because the label used to be poured into the
    // control once at creation from a Wizard field and nothing ever compared them
    // again.
    //
    // *** IT DOES NOT ASK THE LADDER, AND THAT IS A CORRECTION MEASURED IN THIS ROUND.
    //     *** The first form of this check called chooseLoopMidiOffer(&g_run.state).
    // It passed at 96 DPI and FAILED at 144 with control "Open loopMIDI page" against
    // ladder "" - because measureRecheckAndNudge() runs at the end of the 96 pass, and
    // it really re-reads THIS machine into g_run.state. It restores the table
    // afterwards, deliberately, so that both passes photograph the invented machine;
    // it does not restore the state, and this machine has teVirtualMIDI. So the right
    // hand side was a fact about the harness's host and the left was a fact about the
    // fiction being rendered, and they legitimately disagree. The ladder-to-table
    // wiring is where that question belongs and testMidiPortScreen() asks it there,
    // over five invented machines, against exact literals.
    {
        const wchar_t* want = (page >= 0 && page < screenCount(g_wiz) &&
                               g_wiz->screens[page].actionLabel)
                              ? g_wiz->screens[page].actionLabel : L"";
        _snwprintf(what, 390,
                   L"%ddpi %s: the action button carries the words THIS screen's entry "
                   L"gives it (control \"%s\", table \"%s\")",
                   dpi, screen, label, want);
        what[390] = 0;
        check(want[0] != 0 && wcscmp(label, want) == 0, what);
    }

    // *** IT IS PINNED, NOT MERELY ORDERED, AND THAT WAS FOUND BY INJECTION. ***
    // The first form of this check asked only that the button start after "Check
    // again" and end before the note. It passed with the button pushed 90 pixels to
    // the right, because footNote() measures the note's box FROM the button - the
    // note simply moved with it. A relative assertion against a value derived from
    // the thing being asserted cannot fail. So the gap itself is the assertion.
    //
    // THE NOTE'S EDGE IS NO LONGER PART OF IT, and that is a real change rather than
    // a relaxation: there is no note on this screen at all. footNote() paints one only
    // beside the button that WRITES, which is on page 2, so the sentence "nothing will
    // be written" cannot land under a button whose press turns a page. What is asserted
    // instead is that the button stays inside the band - a bound this window really
    // has - and the gap, which is the part that could not fail before.
    //
    // *** AND THE EXPECTED x IS THIS FILE'S ARITHMETIC, WHICH IS WHAT LETS IT COVER A
    //     SCREEN WITH NO "Check again" AT ALL. *** One gap after the re-check when the
    // re-check is really on this screen; the left margin when it is not. The second arm
    // is the one that catches the defect the round adding the second offer found in
    // actionBand(): it offset itself by the re-check's width whenever the FLOW had a
    // re-check, which on a kScreenInfo screen means a 114 pixel hole and a button nobody
    // measured.
    int wantX = haveRecheck ? (int)br.right + S(kActionGap) : S(24);
    _snwprintf(what, 390,
               L"%ddpi %s: the action button is in the BAND and not on the page, and it "
               L"starts where this screen's band starts (%d..%d, wanted %d; Check again "
               L"%s and ends %d; band is %d wide)",
               dpi, screen, (int)ar.left, (int)ar.right, wantX,
               haveRecheck ? L"is here" : L"is NOT on this screen", (int)br.right,
               (int)fc.right);
    check(haveAction && ar.left == wantX &&
          ar.right <= fc.right && ar.bottom <= fc.bottom, what);

    // *** AND THERE IS NO NOTE BESIDE IT, ASSERTED RATHER THAN ASSUMED. *** The note
    // is /preview's promise that nothing is written, and it belongs beside the press
    // that would write. If it ever landed here it would be answering a question nobody
    // on this screen asked, next to the one control it is not about - and the check
    // above would then be measuring a button against a band instead of against a note.
    _snwprintf(what, 390,
               L"%ddpi %s: no note shares this band, because the note is about the "
               L"press that writes and this press does not (\"%s\")",
               dpi, screen, fn.text ? fn.text : L"none");
    what[390] = 0;
    check(fn.text == 0, what);

    // *** AND /preview GATES IT BY CONSEQUENCE, NOT BY CATEGORY. ***
    //
    // This check used to read "/preview greys the action button, so this screen is left
    // with no button that acts", asserted on both of these screens at both DPIs - four
    // times - and the round that made the painted address clickable made it FALSE by
    // making it inconsistent: on the Zadig screen a grey `Open the Zadig page` sat beside
    // a live blue link that opens the same page. One consequence, two controls, opposite
    // answers. /preview promises that nothing is WRITTEN and nothing is REGISTERED, and
    // the mode has always allowed the re-check, which reads the whole registry.
    //
    // *** AND IT IS DRIVEN BOTH WAYS, BECAUSE ONE WAY IS A TAUTOLOGY HERE. *** The first
    // version of this read the field and the control and compared them, on the argument
    // that the two screens would give opposite answers. MEASURED, AND THEY DO NOT: the
    // harness's invented machine is a DEGRADED rung, so the MIDI port screen's offer is
    // `Open loopMIDI page` and both screens answer "only opens a page". A check with no
    // negative case would have passed just as well against a program that had simply
    // stopped greying anything under /preview - which is the exact over-correction this
    // round is at risk of.
    //
    // So the field is flipped and the page relaid out, and the control is read again. The
    // real gate runs both times; only the entry's answer changes. Restored, with a second
    // layout(), before anything else looks at the window - the foot band capture is taken
    // after this call and would photograph a wrongly-greyed button otherwise.
    //
    // WHERE EACH SIDE COMES FROM: the left is the real control's IsWindowEnabled() after
    // the product's own layout(); the right is Screen::actionOnlyOpensAPage, which
    // buildScreens() set from chooseLoopMidiOffer()'s kind. A window and a table.
    {
        Screen* self  = &g_wiz->screens[page];
        bool onlyPage = self->actionOnlyOpensAPage;
        bool alive    = haveAction && IsWindowEnabled(g_actionBtn) != 0;

        self->actionOnlyOpensAPage = !onlyPage;
        layout();
        bool aliveFlipped = g_actionBtn && IsWindowEnabled(g_actionBtn) != 0;
        self->actionOnlyOpensAPage = onlyPage;
        layout();
        UpdateWindow(g_page);
        UpdateWindow(g_frame);

        _snwprintf(what, 390,
                   L"%ddpi %s: /preview gates this button by CONSEQUENCE and not by "
                   L"category, both ways round - it %s and is enabled %d; told the "
                   L"opposite about itself the same gate answers %d (note %s)",
                   dpi, screen,
                   onlyPage ? L"opens a web page" : L"writes to this machine",
                   alive ? 1 : 0, aliveFlipped ? 1 : 0,
                   g_wiz->startBlockedNote ? L"set" : L"MISSING");
        what[390] = 0;
        check(haveAction && g_wiz->startBlockedNote != 0 && alive == onlyPage &&
              aliveFlipped == !onlyPage, what);
    }

    // *** EVERY LABEL THIS BUTTON CAN CARRY FITS INSIDE IT. *** Its width is worked
    // out from its words, and this measures the OTHER label too - the one this
    // render does not show - because a button whose text is cut is a defect this
    // project has already paid for twice, and only one of the two labels is ever on
    // the screen at a time.
    {
        wchar_t saved[128];
        wcscpy(saved, g_actionLabel);
        int  worst     = 0;
        int  worstBox  = 0;
        const wchar_t* worstText = L"";
        for (int i = 0; i < nLabels; i++) {
            setActionLabel(labels[i]);
            HDC        adc = GetDC(g_frame);
            ActionBand ab  = actionBand(adc);
            ReleaseDC(g_frame, adc);
            wprintf(L"    label \"%s\": needs %d, box %d\n", labels[i], ab.needW, ab.w);
            if (ab.needW - ab.w > worst - worstBox) {
                worst     = ab.needW;
                worstBox  = ab.w;
                worstText = labels[i];
            }
        }
        setActionLabel(saved);
        _snwprintf(what, 390,
                   L"%ddpi %s: every label THIS screen's button can carry fits inside it "
                   L"- %d label(s), worst is \"%s\", needs %d and gets %d", dpi, screen,
                   nLabels, worstText, worst, worstBox);
        what[390] = 0;
        check(nLabels > 0 && worst <= worstBox, what);
    }
}

// ===========================================================================
// THE DOWNLOAD ADDRESS, ON THE SCREEN AND OUT OF THE PANE
//
// *** THIS SUITE EXISTS BECAUSE OF A SCREENSHOT AND NOT BECAUSE OF A NUMBER. *** The
// owner ran BCD3000Setup.exe on 2026-07-31 and his own picture shows the loopMIDI
// address SELECTED WITH THE MOUSE, inside a pane that scrolls. He had to hunt for it.
// Nothing in this file could see that: 778 checks and 46 pictures all passed over a
// screen whose most actionable line was three scrolls down inside an EDIT control. That
// is the sixth defect in this project found by a person looking at a picture and the
// second found by the owner himself.
//
// So what is measured here is the part of his complaint that CAN be measured: the
// address is laid out in the painted strip and therefore outside the pane; it ends
// inside that strip and therefore above the fold; it is not wrapped, because an address
// read in two halves is an address retyped wrong; the renderer really READS the field;
// and it is laid out BEFORE the bullets, which is his first decision.
//
// WHAT IT CANNOT MEASURE, said rather than implied: whether the address is obvious to a
// person. That is what the four captures are for, and this round looked at all four.
// ===========================================================================
// ---------------------------------------------------------------------------
// *** HOW MUCH CLEAR PAPER THE ADDRESS BLOCK MUST KEEP BELOW IT, BY DPI. ***
//
// A ratchet and not a target, in the shape requiredShotClearance() and allowedDeficit()
// already use here: the figures are what the program measures TODAY, used as a floor, so
// a change that gives the address more room passes and is welcome and whoever lowers one
// of these is claiming a measurement and has to publish it.
//
// It is a floor rather than "inside the strip" for the reason the picture's is: `bottom
// <= strip` is a guard against BREACH and not against EROSION, and only the last pixel of
// erosion would ever fail it.
//
// *** WHAT MOVES THIS ADDRESS DOWN IS NOT WHAT MOVES THE PICTURE DOWN, AND THE NUMBER IS
//     DERIVED FROM THAT RATHER THAN COPIED FROM THE MEASUREMENT. *** The block sits above
// the bullets, so a re-worded bullet cannot touch it. The only content between it and the
// top of the page is the title, the rule and the screen's own ROW - and a row wrapping to
// a second line is a real and legitimate change: describeBinding()'s comment records two
// of its five sentences having done exactly that, and the fix there was to shorten the
// sentences rather than to move anything.
//
// So the floor is TODAY'S MEASURED MINIMUM MINUS ONE LINE OF THE ROW'S OWN FONT. Measured
// on this run, the block clears the fold by 79 at 96 DPI and 114 at 144 on the MIDI port
// screen and by 125 and 186 on the Zadig screen; a line of g_fSmall is 13 and 23. 79 - 13
// = 66 and 114 - 23 = 91. That means: one row wrapping passes, two do not, and anything
// that pushes the address towards the fold for any other reason lands in a review.
//
// AND IT IS THE NARROWER OF TWO GUARDS RATHER THAN THE ONLY ONE. The whole painted strip
// is held by Rule 1's per screen deficit, which is ZERO for both of these screens and is
// asserted independently of this. This one is about the address specifically: it is the
// line the owner had to hunt for, and it is the line that must not go back under a fold.
//
// A RATCHET AND NOT A TARGET, like allowedDeficit() and requiredShotClearance(): the
// comparison is >=, so a change that gives the address MORE room passes and is welcome,
// and whoever lowers a figure here is claiming a measurement and has to publish it.
static int requiredAddressClearance(int dpi)
{
    return (dpi >= 144) ? 91 : 66;
}

static void measureAddressBlock(int dpi, int page, const wchar_t* screen,
                                const wchar_t* wantUrl)
{
    wchar_t what[400];

    RECT box;
    bool drawn  = lastAddressBox(&box);
    int  needW  = 0;
    int  haveW  = 0;
    lastAddressWidths(&needW, &haveW);

    wprintf(L"  %s address block: drawn=%d box=(%d,%d)-(%d,%d) strip=%d, address needs "
            L"%d of %d on its line\n", screen, drawn ? 1 : 0, (int)box.left,
            (int)box.top, (int)box.right, (int)box.bottom, g_viewH, needW, haveW);

    // *** ASKED AS A CHECK AND NOT AS AN EARLY RETURN, for the reason measureBindingShot()
    //     spells out: a guard that returned here would take every measurement below out of
    // the run with it and leave the suite green for work it never did.
    //
    // WHERE EACH SIDE COMES FROM: the left is the box renderSubject() recorded while
    // laying the page out, read back through the product's own lastAddressBox(); the
    // right is that this screen is one of the two the owner named. A screen whose entry
    // set addressUrl and whose renderer ignored it would answer false here, which is the
    // "declared and unread" shape three fields of this table have already had.
    _snwprintf(what, 390,
               L"%ddpi %s: the download address is laid out in the PAINTED strip, which "
               L"is what puts it outside the pane the owner had to hunt in", dpi, screen);
    what[390] = 0;
    check(drawn, what);
    if (!drawn)
        return;

    // *** AND IT ENDS INSIDE THAT STRIP, WITH ROOM TO SPARE. *** The strip is the page
    // MINUS the pane on both of these screens, so a block that ran past it would be the
    // defect the owner already saw once - content passing underneath the pane.
    //
    // WHERE EACH SIDE COMES FROM: the left is g_viewH minus the recorded box's bottom -
    // the layout's own strip, which measureScreenPane() pins against a real pane
    // rectangle, minus a rectangle renderSubject() recorded; neither is computed from the
    // other. The right is a literal a human wrote down from a run.
    _snwprintf(what, 390,
               L"%ddpi %s: the address block ends INSIDE the strip this screen gets and "
               L"keeps clear paper below it - it ends at %d of %d, clearing the fold by "
               L"%d and the floor is %d", dpi, screen, (int)box.bottom, g_viewH,
               g_viewH - (int)box.bottom, requiredAddressClearance(dpi));
    what[390] = 0;
    check(g_viewH - (int)box.bottom >= requiredAddressClearance(dpi), what);

    // *** AND THE ADDRESS ITSELF IS ON ONE LINE. *** This is the check that is about the
    // owner's second decision rather than about layout. He asked to SEE the address, and
    // the recorded reasons are that it lets a person check it before clicking a link to
    // a third party .exe, and that it can be copied when the default browser is broken.
    // An address that wrapped would be readable and useless for both: somebody reads half
    // of it, or selects half of it, and types the rest.
    //
    // renderSubject() draws it with DT_SINGLELINE once it has measured that it fits, so
    // the failure this catches is the measurement going the other way - a longer address,
    // a wider lead, a font substitution - at which point the renderer stacks the two and
    // wraps them, and this says so.
    //
    // *** IT HAS TWO CLAUSES AND THE FIRST ONE ALONE CANNOT FAIL IN THE ORDINARY CASE,
    //     WHICH IS WHY THE SECOND IS HERE. *** `needW <= haveW` is a real assertion in the
    // renderer's STACKED fallback, where haveW is the whole column and an address wider
    // than the column would be one nobody can read. In the one-line arrangement it is a
    // tautology by construction: renderSubject() only takes that arrangement after
    // measuring that the address fits in what the lead leaves, and haveW is then exactly
    // that. A check with a mode in which it cannot fail is the shape this project has been
    // bitten by four times.
    //
    // So the second clause is the one that holds the arrangement: the whole BLOCK is at
    // most one line of the body font tall, which is true when the lead and the address
    // share a line and false the moment the renderer stacks them. That is also the thing
    // the screens cannot afford to lose - stacking costs 13 logical pixels at 96 DPI and
    // 23 at 144, and it is what put the Zadig screen 9 pixels over its strip on the first
    // attempt at this block.
    //
    // WHERE EACH SIDE COMES FROM: needW and haveW are GetTextExtentPoint32W and the column
    // width inside the product's own renderer, which is the only place either exists; the
    // block's height is the box renderSubject() RECORDED, and the line height is asked of
    // GDI here, in this file, through the same font the product drew with. The recorded box
    // and a fresh GetTextMetricsW are not derived from one another.
    {
        HDC bdc = GetDC(g_page);
        int oneBodyLine = lineHeight(bdc, g_fBody);
        ReleaseDC(g_page, bdc);
        int blockH = (int)(box.bottom - box.top);
        _snwprintf(what, 390,
                   L"%ddpi %s: the whole address is on ONE line and shares it with its "
                   L"lead, so nobody reads half of it and no line is spent on saying what "
                   L"it is - it needs %d of %d, and the block is %d px against %d for one "
                   L"line of the body font", dpi, screen, needW, haveW, blockH,
                   oneBodyLine);
        what[390] = 0;
        check(needW > 0 && haveW > 0 && needW <= haveW && blockH > 0 &&
              blockH <= oneBodyLine, what);
    }

    // *** THE FIELD IS REALLY READ BY THE RENDERER, MEASURED AND NOT ASSERTED. *** Screen
    // carried showDevicePhoto, showZadigShot and paneText for three rounds with nothing
    // looking at them, so an entry that set one got nothing and was told nothing. A check
    // that read Screen::addressUrl and compared it with the constant setup.cpp assigned
    // would pass just as happily against a renderer that ignores the field: both sides
    // would be the same pointer.
    //
    // So the field is REMOVED and the screen re-measured. WHERE EACH SIDE COMES FROM: both
    // are the renderer's own answer for the height of this screen, one with the address and
    // one without, and nothing in this block reads the string. If the renderer does not
    // read it the two numbers are equal and this goes red. The cost is published because it
    // is spent out of a strip whose slack was 20 pixels at 96 DPI and 11 at 144.
    {
        Screen* self = &g_wiz->screens[page];
        const wchar_t* keepUrl  = self->addressUrl;
        const wchar_t* keepLead = self->addressLead;
        RECT   c;
        GetClientRect(g_page, &c);
        HDC    dc   = GetDC(g_page);
        int    with = (kindAt(page) == kScreenInfo)
                      ? renderInfoScreen(dc, c.right, c.bottom, true)
                      : renderCheckScreen(dc, c.right, c.bottom, true);
        self->addressUrl  = 0;
        self->addressLead = 0;
        int without = (kindAt(page) == kScreenInfo)
                      ? renderInfoScreen(dc, c.right, c.bottom, true)
                      : renderCheckScreen(dc, c.right, c.bottom, true);
        self->addressUrl  = keepUrl;
        self->addressLead = keepLead;

        // *** AND THE BLOCK IS LAID OUT BEFORE THE BULLETS, WHICH IS THE OWNER'S FIRST
        //     DECISION AND NOT A DETAIL. *** He said the address must sit somewhere
        // obvious ABOVE the summary text: the first thing after what this is and what
        // was found. A renderer that drew it after the bullets would satisfy every
        // measurement above - it would still be in the strip, still above the fold,
        // still one line - and would have moved it back to the bottom of the screen.
        //
        // WHERE EACH SIDE COMES FROM: the box's TOP with the bullets present against the
        // box's TOP with them removed, both recorded by renderSubject() on its own
        // measuring pass. If the address were below them, taking them away would move it
        // UP. Nothing here reads a coordinate written in this file.
        const wchar_t* keepB[4];
        for (int b = 0; b < 4; b++) {
            keepB[b]         = self->bullets[b];
            self->bullets[b] = 0;
        }
        if (kindAt(page) == kScreenInfo)
            renderInfoScreen(dc, c.right, c.bottom, true);
        else
            renderCheckScreen(dc, c.right, c.bottom, true);
        RECT noBullets;
        bool stillDrawn = lastAddressBox(&noBullets);
        for (int b = 0; b < 4; b++)
            self->bullets[b] = keepB[b];
        // Put the page's recorded boxes back to what the real screen has, so that
        // anything measured after this reads the layout the window really shows.
        if (kindAt(page) == kScreenInfo)
            renderInfoScreen(dc, c.right, c.bottom, true);
        else
            renderCheckScreen(dc, c.right, c.bottom, true);
        ReleaseDC(g_page, dc);

        _snwprintf(what, 390,
                   L"%ddpi %s: the address is really MEASURED into the page and not "
                   L"merely set - %d px with it, %d without, so it costs %d out of this "
                   L"strip's slack", dpi, screen, with, without, with - without);
        what[390] = 0;
        check(keepUrl != 0 && with > without, what);

        _snwprintf(what, 390,
                   L"%ddpi %s: ...and it is laid out ABOVE the bullets, which is where "
                   L"the owner put it - its top is %d with them and %d without, and a "
                   L"block below them would move up when they go", dpi, screen,
                   (int)box.top, (int)noBullets.top);
        what[390] = 0;
        check(stillDrawn && noBullets.top == box.top, what);
    }

    // ===================================================================
    // *** AND THERE IS INK IN THAT BOX, WHICH IS THE ONLY THING THAT MEANS "PAINTED".
    //     THE HEIGHT DELTA ABOVE DOES NOT MEAN IT, AND THAT WAS MEASURED BY INJECTION.
    //     ***
    //
    // Every check above this one is made on the MEASURING pass, because that is the pass
    // a harness can run without a visible window. All of them would pass against a
    // renderer that reserved the space, recorded the box, spent the height and DREW
    // NOTHING - `measure` forwarded as true to the two draw calls is a one word edit. The
    // height delta cannot see it: both of its renders are measuring renders.
    //
    // That is this project's most expensive shape - content that vanishes in silence,
    // five times paid for - and it was open here until an injection went looking for it:
    // an injection that stopped the block spending its height left the DELTA CHECK GREEN,
    // because the block still spent its two gaps. A check that survives the defect it is
    // named for is worse than no check.
    //
    // So the page is rendered for REAL, into a bitmap, with measure false and no child
    // controls, and the pixels inside the recorded box are counted. WHERE EACH SIDE COMES
    // FROM: the left is ink in a real 32 bit surface the product's own renderer drew into;
    // the right is the rectangle renderSubject() recorded while laying it out, and the
    // address's own measured width. Neither is derived from the other, and no measuring
    // pass is involved in either.
    //
    // THE WIDTH IS PART OF IT ON PURPOSE. `any ink at all` would be satisfied by the LEAD
    // alone - dim text at the left of the same line - which is exactly the half that is
    // not the address. So the ink has to be at least as wide as the lead's offset plus the
    // address, less a tolerance of one body line for the glyph advance the extent does not
    // include.
    // ===================================================================
    {
        RECT c;
        GetClientRect(g_page, &c);
        int     pw      = c.right;
        int     ph      = c.bottom;
        // *** NOT NAMED `screen`, AND THAT COST A ROUND. *** This function's screen
        // PARAMETER is the wchar_t* name of the screen being measured, and a local HDC
        // called `screen` shadowed it - so the %s in the message below was handed a
        // device context to dereference as a string and the whole harness segfaulted on
        // the first call. /W4 warns C4457 for exactly this and buildharness.bat does not
        // use /WX, so the warning scrolled past; `build.bat strict` does, which is the
        // build that would have refused it.
        HDC     screenDc = GetDC(0);
        HDC     mem      = CreateCompatibleDC(screenDc);
        HBITMAP bmp      = CreateCompatibleBitmap(screenDc, pw, ph);
        HGDIOBJ ob       = SelectObject(mem, bmp);
        fillRect(mem, 0, 0, pw, ph, CLR_PAGE_BG);
        SetBkMode(mem, TRANSPARENT);
        if (kindAt(page) == kScreenInfo)
            renderInfoScreen(mem, pw, ph, false);
        else
            renderCheckScreen(mem, pw, ph, false);
        RECT   region = box;
        InkBox ink    = inkOfRegion(bmp, pw, ph, &region, CLR_PAGE_BG);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(0, screenDc);

        // *** THE LEAD'S OFFSET IS IN THE FLOOR, AND UNTIL THIS ROUND THE COMMENT ABOVE
        //     SAID SO WHILE THE ARITHMETIC BELOW LEFT IT OUT. *** The floor was
        // `needW - one body line`, which is the address's own width and nothing else. But
        // the box starts at the LEAD, so a renderer that painted "Download Zadig here:"
        // and drew the address nowhere produced ink from the lead alone and cleared it:
        // measured on the Zadig screen at 96 DPI, 114 px of ink against a 106 px floor,
        // green, in the one check written to catch "measured, reserved, drawn nowhere".
        //
        // The lead's extent is re-derived HERE, from the string the entry carries and the
        // font the product draws it in, rather than read back out of the renderer. The
        // S(8) gap between the two is deliberately NOT added: leaving it out keeps this a
        // strict lower bound on what must be painted and keeps the floor free of a
        // constant shared with the thing under test.
        //
        // AND ONLY WHEN THE TWO SHARE A LINE. renderSubject() falls back to stacking, and
        // records haveW as the whole column when it does and as the column minus the
        // lead's room when it does not - so `haveW < the box's own width` is how this
        // reads the arrangement off the recorded numbers instead of assuming it.
        int inkW    = ink.any ? (ink.right - ink.left + 1) : 0;
        int leadW   = 0;
        {
            const wchar_t* lead = g_wiz->screens[page].addressLead;
            if (lead && lead[0]) {
                HDC     ldc2 = GetDC(g_page);
                HGDIOBJ of   = SelectObject(ldc2, g_fSmall);
                SIZE    lz;
                ZeroMemory(&lz, sizeof(lz));
                GetTextExtentPoint32W(ldc2, lead, (int)wcslen(lead), &lz);
                SelectObject(ldc2, of);
                ReleaseDC(g_page, ldc2);
                leadW = lz.cx;
            }
        }
        int availW  = (int)(box.right - box.left);
        int leadOff = (leadW > 0 && haveW < availW) ? leadW : 0;
        HDC ldc     = GetDC(g_page);
        int floorW  = leadOff + needW - lineHeight(ldc, g_fBody);
        ReleaseDC(g_page, ldc);
        wprintf(L"  ...ink inside that box: any=%d, %d..%d, %d px wide; the lead is %d "
                L"and the address alone needs %d\n", ink.any ? 1 : 0, ink.left, ink.right,
                inkW, leadOff, needW);
        _snwprintf(what, 390,
                   L"%ddpi %s: there is INK in that box and it is at least as wide as the "
                   L"lead's offset PLUS the address, so the block is PAINTED and not "
                   L"merely measured - %d px of ink against %d + %d less one body line. "
                   L"Ink as wide as the lead alone is the half that is not the address",
                   dpi, screen, inkW, leadOff, needW);
        what[390] = 0;
        check(ink.any && inkW >= floorW, what);
    }

    // *** AND IT IS THE ADDRESS THIS SCREEN IS SUPPOSED TO CARRY, WHICH IS ALL THIS FORM
    //     CAN SAY - SEE THE MEASUREMENT IN testAddressIsDefinedOnce(). *** The pane
    // control is one control whose text is replaced per screen and the address block is
    // one field read off whichever entry the window is on, so "the Zadig screen shows the
    // loopMIDI address" is a reachable state and would be a screen sending somebody to
    // the wrong program. That is what this catches, and the two addresses are different
    // strings, so the comparison really discriminates.
    //
    // *** WHAT IT DOES NOT CATCH, MEASURED AND NOT REASONED. *** This was written as the
    // single definition check - pointer identity, on the argument that a byte-identical
    // second literal in setup.cpp would be a different object and would fail it. It was
    // injected and IT PASSED: `sc[4].addressUrl = L"https://zadig.akeo.ie/";` in place of
    // the published constant left this check green at both DPIs and left all 829 green.
    // The linker folded the two identical literals - /O2 implies /GF and the default
    // /OPT:ICF merges the resulting read-only COMDATs - so the copy and the definition
    // really are one address in the image, and no pointer comparison anywhere in this
    // process can tell them apart. "One definition, published" is therefore asserted on
    // the SOURCE TEXT instead; see testAddressIsDefinedOnce().
    _snwprintf(what, 390,
               L"%ddpi %s: and it is THIS screen's address and not the other screen's - "
               L"\"%s\"", dpi, screen,
               g_wiz->screens[page].addressUrl ? g_wiz->screens[page].addressUrl
                                               : L"(none)");
    what[390] = 0;
    check(g_wiz->screens[page].addressUrl == wantUrl, what);
}

// ===========================================================================
// *** THE PAINTED ADDRESS IS A LINK: WHERE THE HIT REGION IS, AND WHERE IT IS NOT. ***
//
// The owner walked all nine screens on 2026-07-31 and asked for one thing: "no passo do
// zadig tem o botao la embaixo para ir para o site, mas o link azul no comeco da pagina
// deveria ter link tambem". Both screens that paint an address got it - fixing the one
// he named and leaving its twin is this project's signature defect.
//
// *** THIS FUNCTION IS ABOUT THE SHAPE OF THE REGION AND NOTHING ELSE. *** That the
// click really reaches the opener is measured once, with a real message and a real
// worker, in measureAddressLinkPress(). Kept apart because they fail for different
// reasons and a reader of a red line should not have to work out which.
//
// WHERE EACH SIDE COMES FROM: the left of every comparison is a rectangle
// renderSubject() recorded while laying the page out, read back through the product's
// own lastAddressLinkBox(); the right is either the OTHER rectangle the same pass
// recorded for the whole block (a different variable, written from different
// arithmetic) or the address's own GetTextExtentPoint32W width. The hit tests below go
// through addressLinkAt(), which is the very expression the click and the cursor ask -
// so a region that answered differently from the one that acts is not reachable.
// ===========================================================================
static void measureAddressLink(int dpi, int page, const wchar_t* screen)
{
    wchar_t what[400];

    RECT block;
    RECT link;
    bool hasBlock = lastAddressBox(&block);
    bool hasLink  = lastAddressLinkBox(&link);
    int  needW    = 0;
    int  haveW    = 0;
    lastAddressWidths(&needW, &haveW);

    int linkW = hasLink ? (int)(link.right - link.left) : 0;
    int blockW = hasBlock ? (int)(block.right - block.left) : 0;
    wprintf(L"  %s address link: live=%d box=(%d,%d)-(%d,%d) %d wide, inside a block "
            L"%d wide, address needs %d\n", screen, hasLink ? 1 : 0, (int)link.left,
            (int)link.top, (int)link.right, (int)link.bottom, linkW, blockW, needW);

    _snwprintf(what, 390,
               L"%ddpi %s: the painted address is a LINK - the renderer recorded a hit "
               L"region for it, which is what the owner asked for after walking all nine "
               L"screens", dpi, screen);
    what[390] = 0;
    check(hasLink && hasBlock, what);
    if (!hasLink || !hasBlock)
        return;

    // *** THE REGION IS THE ADDRESS'S OWN GLYPHS AND NOT THE BLOCK. *** See
    // lastAddressLinkBox() in gui.h for the three candidates and why the other two are
    // wrong. The clause that carries the weight is the last: the block spans the whole
    // column, so a hit region taken from it would be strictly wider than the address,
    // and most of what it covered would be blank paper. `linkW == needW` is the
    // positive half and `linkW < blockW` is the half that fails if somebody reaches for
    // the convenient rectangle.
    _snwprintf(what, 390,
               L"%ddpi %s: the hit region is the ADDRESS's own run and not the whole "
               L"column - %d px wide against the address's %d and the block's %d, and it "
               L"sits inside the block", dpi, screen, linkW, needW, blockW);
    what[390] = 0;
    check(linkW == needW && linkW > 0 && linkW < blockW &&
          link.left >= block.left && link.right <= block.right &&
          link.top >= block.top && link.bottom <= block.bottom, what);

    // *** AND NOW THE THREE POINTS. *** The middle of the address opens the page; the
    // left of the block is the LEAD, which is dim grey text that says nothing about
    // acting; the right of the block is blank paper. A click on empty page opening
    // somebody's browser is the worst version of this feature and the reason the region
    // is not the block.
    //
    // The y is converted the way the click handler converts it - client coordinates are
    // page coordinates less the scroll - so this asks the question at the pixel a mouse
    // would land on. Neither of these two screens scrolls its page (Rule 1 holds both at
    // deficit zero) so the term is zero today; it is written because a hit test that
    // assumed so would break the day it stopped being true.
    {
        int midY   = (int)(link.top + link.bottom) / 2 - g_scrollY;
        int midX   = (int)(link.left + link.right) / 2;
        bool onIt  = addressLinkAt(midX, midY);
        bool onLead = addressLinkAt((int)block.left + 1, midY);
        bool onPaper = addressLinkAt((int)block.right - 1, midY);
        bool above = addressLinkAt(midX, (int)block.top - 2 - g_scrollY);
        wprintf(L"  ...hit test at x=%d,%d,%d on y=%d: address %d, lead %d, paper %d; "
                L"two lines above %d\n", midX, (int)block.left + 1, (int)block.right - 1,
                midY, onIt ? 1 : 0, onLead ? 1 : 0, onPaper ? 1 : 0, above ? 1 : 0);
        _snwprintf(what, 390,
                   L"%ddpi %s: a click lands on the ADDRESS and on nothing else - middle "
                   L"of the address %d, the dim lead beside it %d, the blank paper at the "
                   L"right margin %d, the line above %d", dpi, screen, onIt ? 1 : 0,
                   onLead ? 1 : 0, onPaper ? 1 : 0, above ? 1 : 0);
        what[390] = 0;
        check(onIt && !onLead && !onPaper && !above, what);
    }

    // *** THE LINK IS AN ADDITION AND THE BUTTON IS STILL THERE, WHICH IS A REQUIREMENT
    //     AND NOT AN OBSERVATION. *** A painted region is invisible to a screen reader
    // and unreachable from the keyboard. If a later round decided the link had made the
    // button redundant, everybody who does not use a mouse would lose the address - and
    // on the MIDI port screen in the winget state the button is not even the same
    // journey: it reads `Install loopMIDI...` and runs somebody else's installer, which
    // is the gap the owner is closing rather than a duplicate of it.
    //
    // WHERE EACH SIDE COMES FROM: the left is the real control's IsWindowVisible() on
    // the real page after the real setScreen(); the right is the recorded hit region.
    // One is a window, the other is a rectangle.
    _snwprintf(what, 390,
               L"%ddpi %s: the link is an ADDITION - this entry still carries a labelled "
               L"button (\"%s\") and the real control is on the screen beside the link "
               L"(visible %d, link live %d), so the keyboard and a screen reader still "
               L"have a way to this page", dpi, screen,
               g_wiz->screens[page].actionLabel ? g_wiz->screens[page].actionLabel
                                                : L"(none)",
               (g_actionBtn && IsWindowVisible(g_actionBtn)) ? 1 : 0, hasLink ? 1 : 0);
    what[390] = 0;
    check(g_wiz->screens[page].actionLabel != 0 && g_actionBtn != 0 &&
          IsWindowVisible(g_actionBtn) && hasLink, what);

    // *** AND THE ONLY THING ON THE SCREEN THAT SAYS THE REGION IS THERE. *** The
    // address was not underlined and its colour did not change, because the owner had
    // already read it as a link before it was one. What tells anybody at the moment they
    // are about to try is the hand pointer, and a capture cannot photograph a cursor -
    // so it is driven here with the two real messages, at real coordinates, through the
    // real window procedure.
    //
    // WM_MOUSEMOVE first because that is the order Windows uses and because WM_SETCURSOR
    // is never given a position: the move records where the pointer is, the WM_SETCURSOR
    // that follows answers from it. Sent to g_page, so what runs is pageProc()'s own
    // arms and not a re-implementation of them here.
    //
    // WHERE EACH SIDE COMES FROM: the left is the LRESULT the product's window procedure
    // returned for a real message; the right is a literal - TRUE over the address,
    // FALSE over the dim lead beside it. The second half is what stops this passing on a
    // handler that answered TRUE everywhere, which would put a hand cursor over the whole
    // page.
    {
        int midY  = (int)(link.top + link.bottom) / 2 - g_scrollY;
        int midX  = (int)(link.left + link.right) / 2;
        SendMessageW(g_page, WM_MOUSEMOVE, 0, MAKELPARAM(midX, midY));
        LRESULT onIt = SendMessageW(g_page, WM_SETCURSOR, (WPARAM)g_page,
                                    MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        SendMessageW(g_page, WM_MOUSEMOVE, 0, MAKELPARAM((int)block.left + 1, midY));
        LRESULT onLead = SendMessageW(g_page, WM_SETCURSOR, (WPARAM)g_page,
                                      MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        _snwprintf(what, 390,
                   L"%ddpi %s: the pointer becomes a hand over the address and stays an "
                   L"arrow beside it - WM_SETCURSOR answered %d over the address and %d "
                   L"over the dim lead. It is the only affordance this region has, and no "
                   L"capture can show it", dpi, screen, (int)onIt, (int)onLead);
        what[390] = 0;
        check(onIt != 0 && onLead == 0, what);
    }
}

// ===========================================================================
// PART 3b - THE RE-CHECK, RUN FOR REAL, AND THE NUDGE THAT MUST NOT RE-RUN ANYTHING
//
// This drives the REAL recheckMachine() from setup.cpp, through the real frameProc,
// on a real thread, and reads the rows back out of the real Wizard. It is read only:
// gatherMachineState() is the same call prepare() already makes in this harness.
//
// There is no DPI in any of it, so it runs once. The rows are saved and put back
// afterwards, because the second DPI pass has to render the same page as the first.
// ===========================================================================
static void measureRecheckAndNudge(Wizard* wiz)
{
    wprintf(L"\n--- the re-check, and the device nudge ---\n");

    Row saved[kMaxRows];
    memcpy(saved, wiz->review, sizeof(saved));
    int savedCount = wiz->reviewCount;

    // *** AND THE TABLE, FOR THE SAME REASON THE ROWS ARE SAVED. *** The re-check
    // below really reads this machine and writes it over g_run.state, and since this
    // round the flow restates its screens' rows from that state when the re-check
    // finishes. This function runs at the end of the 96 DPI pass and the 144 DPI pass
    // comes after it, so without this the two passes would photograph two different
    // machines - the invented one at 96 and whatever this machine is at 144. The
    // rows have been restored here since the leak of 2026-07-29; the screens are the
    // same problem one round later.
    Screen savedScreens[kMaxScreens];
    memcpy(savedScreens, wiz->screens, sizeof(savedScreens));

    // GUARDED for the same reason goToKind() is. screenOfKind() answers -1 when the
    // flow has no screen of that kind, and setScreen(-1) would put g_screen out of
    // the table, where every onKind() answers false and the page paints nothing - so
    // the suite below would be measuring a blank window and reporting it as the
    // re-check page. The failure is not made quiet by this: with the window left
    // where it was, the two checks below still find no visible re-check button.
    // *** THE SCREEN AND NOT THE KIND, SINCE THERE ARE TWO CHECK SCREENS. *** This
    // asked screenOfKind(kScreenCheck), which answers with the FIRST one - and as of
    // this round that is the mixer screen, which has no pane, no action button and
    // none of the rows this suite is about. It would have measured the re-check page
    // on a page that is not it and failed for the wrong reason. reviewAt() reads the
    // product's own field for "this entry is the machine review".
    int checkScreen = -1;
    for (int i = 0; i < screenCount(wiz); i++)
        if (reviewAt(i))
            checkScreen = i;
    if (checkScreen >= 0)
        setScreen(checkScreen);
    UpdateWindow(g_page);
    UpdateWindow(g_frame);

    // The screen that carries a row of its OWN, which is the half of the re-check
    // that has no postReviewRow() behind it. -1 when the flow has none.
    int subjectScreen = -1;
    for (int i = 0; i < screenCount(wiz); i++)
        if (wiz->screens[i].kind == kScreenCheck && !reviewAt(i))
            subjectScreen = i;

    // The plan's structural claim, as far as a running test can put it: gui.cpp holds
    // a POINTER. The rest of the claim is the shape of the code and is stated in the
    // report - recheckProc() is the only place it is dereferenced, and that line is
    // already on the worker thread when it runs.
    check(wiz->recheck != 0,
          L"page 2's re-check is a function pointer the flow supplied, so gui.cpp has "
          L"no name to call from a message handler");
    check(g_recheckBtn != 0 && IsWindowVisible(g_recheckBtn) &&
          IsWindowEnabled(g_recheckBtn),
          L"the button is on page 2, visible and pressable");

    // A sentinel in every row. A row that did not come back through postReviewRow()
    // then shows up as itself instead of being indistinguishable from a row the
    // re-check happened to agree with.
    for (int i = 0; i < wiz->reviewCount; i++)
        wcscpy(wiz->review[i].title, L"NOTHING CAME BACK");
    // ...and in the row that has no postReviewRow() behind it, for the same reason.
    // It comes back a different way - the window thread rebuilds the table when the
    // re-check finishes - and "a different way" is exactly the kind of claim that
    // deserves a sentinel rather than a comment.
    if (subjectScreen >= 0)
        wcscpy(wiz->screens[subjectScreen].row.title, L"NOTHING CAME BACK");

    DWORD windowTid = GetCurrentThreadId();
    // THE RE-CHECK IS THE ONE THING HERE THAT REALLY READS THIS MACHINE, and it
    // says what it found through say(). It has to: that is the behaviour under
    // test. What it must not do is put this machine's account and profile path on
    // the harness's stdout, which gets pasted into reports. The lines still go
    // where the test needs them - posted to the window - because the sink and the
    // echo are two different switches.
    bcdsetup::setConsoleEcho(false);
    SendMessageW(g_frame, WM_COMMAND, (WPARAM)IDC_RECHECK, 0);

    // ONE PRESS, ONE SNAPSHOT: the button is dead the moment it is pressed, and this
    // is asked BEFORE the messages are pumped, so it is about what the click handler
    // did and not about what the worker got round to.
    check(!IsWindowEnabled(g_recheckBtn),
          L"the press takes the button away first, so a second press cannot start a "
          L"second reader");

    // ===================================================================
    // *** AND NAVIGATING AWAY DOES NOT HAND THE PRIMARY BUTTON BACK. ***
    //
    // The walk: press the offer on the MIDI screen, then Back, then Next, Next. Each
    // of those runs setScreen(), setScreen() runs refreshButtons(), and refreshButtons()
    // used to re-enable the primary with no g_rechecking term in it - so the user
    // arrived at the review screen with a button reading `Install`, enabled, while
    // winget was still open. Pressing it hit startWork()'s `g_rechecking` guard and did
    // NOTHING, silently. No corruption; the second lock held. The defect is the button:
    // enabled, and inert.
    //
    // THE MIDI SCREEN IS WHERE THIS HAS TO BE ASKED, not the review screen. This render
    // is /preview, and on the review screen the primary is greyed by startBlockedNote
    // whatever else is true - so the check would pass on the broken code for a reason
    // that has nothing to do with the worker. The MIDI screen's press only turns a page,
    // /preview deliberately leaves it alive, and it is therefore the one screen in this
    // flow where the two rules can be told apart.
    //
    // NO MESSAGE IS PUMPED BETWEEN THE PRESS AND THIS, WHICH IS WHY IT IS DETERMINISTIC.
    // The worker posts WM_BCD_RDONE and cannot be dispatched until the loop below runs;
    // setScreen() and UpdateWindow() send, they do not dispatch. So g_rechecking is
    // still true here however fast the machine is, and it is read back and printed
    // rather than assumed.
    {
        int midiNav = -1;
        for (int i = 0; i < screenCount(wiz); i++)
            if (wiz->screens[i].title == ::kMidiTitle)
                midiNav = i;
        if (midiNav >= 0)
            setScreen(midiNav);
        wchar_t nav[400];
        wchar_t onCtl[160];
        onCtl[0] = 0;
        GetWindowTextW(g_primary, onCtl, 160);
        _snwprintf(nav, 390,
                   L"turning to another screen while the worker runs does NOT hand the "
                   L"primary button back (screen %d, button says \"%s\", enabled %d, "
                   L"worker running %d)",
                   midiNav, onCtl, IsWindowEnabled(g_primary) ? 1 : 0,
                   g_rechecking ? 1 : 0);
        nav[390] = 0;
        check(midiNav >= 0 && g_rechecking && !IsWindowEnabled(g_primary), nav);
        if (checkScreen >= 0)
            setScreen(checkScreen);
    }

    DWORD start = GetTickCount();
    while (g_rechecking && GetTickCount() - start < 15000) {
        MSG m;
        while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(5);
    }
    bcdsetup::setConsoleEcho(true);
    wprintf(L"  re-check took %lums, worker thread %lu, window thread %lu, %d rows\n",
            (unsigned long)(GetTickCount() - start), (unsigned long)g_recheckTid,
            (unsigned long)windowTid, wiz->reviewCount);

    check(!g_rechecking, L"the re-check finished and said so");

    // *** THE ONE RULE OF gui.cpp, AS A NUMBER. *** CreateThread's own out parameter
    // against the id of the thread this test is running on, which IS the window's.
    check(g_recheckTid != 0 && g_recheckTid != windowTid,
          L"the re-check ran on a thread that is NOT the window's, so the window kept "
          L"repainting while the registry was read");

    int sentinels = 0;
    for (int i = 0; i < wiz->reviewCount; i++)
        if (wcsstr(wiz->review[i].title, L"NOTHING CAME BACK"))
            sentinels++;
    wchar_t what[400];
    _snwprintf(what, 390,
               L"every row came back through postReviewRow (%d of %d still carry the "
               L"sentinel)", sentinels, wiz->reviewCount);
    check(sentinels == 0, what);

    // *** AND THE SCREEN THAT IS NOT A ROW ON THAT PAGE HEARD THE SAME NEWS. ***
    // The rows above arrive from the worker, one message each, while it runs. A
    // screen's own row has no such channel: the flow restates the whole table from
    // the window thread when the re-check finishes (Wizard::refreshScreens). Without
    // it the mixer screen would keep saying whatever it said when the window opened,
    // while page 2 behind it had been re-read - and the "Check again" button would be
    // measuring something and showing nothing on the screen the user is looking at.
    //
    // WHERE EACH SIDE COMES FROM: the left is the sentinel THIS suite wrote into the
    // table a moment ago; the right is whatever the product put back. Neither can
    // produce the other.
    if (subjectScreen >= 0) {
        _snwprintf(what, 390,
                   L"...and the screen that carries its own row was restated too, on "
                   L"the window thread (screen %d now says \"%s\")", subjectScreen,
                   wiz->screens[subjectScreen].row.title);
        what[390] = 0;
        check(wcsstr(wiz->screens[subjectScreen].row.title,
                     L"NOTHING CAME BACK") == 0, what);
    }

    // The label says "the buttons only" on purpose. This check used to carry a third
    // clause about the row count as well, and that clause has moved to the block
    // below where it can say what it now means. A condition that shrinks under a
    // label that does not change is a check whose name outlives its content, and the
    // diff of check NAMES that every round of this project publishes would have shown
    // nothing at all.
    _snwprintf(what, 390,
               L"the buttons only: the button came back and a re-check did NOT unblock "
               L"a run that is blocked (/preview): button %d, Install %d",
               IsWindowEnabled(g_recheckBtn) ? 1 : 0, IsWindowEnabled(g_primary) ? 1 : 0);
    check(IsWindowEnabled(g_recheckBtn) && !IsWindowEnabled(g_primary), what);

    // *** THE COUNT AFTER A RE-CHECK, AND WHY IT IS NO LONGER SIMPLY "THE SAME". ***
    //
    // This check used to read "wiz->reviewCount == savedCount" and it went red the
    // moment round 4 added the Windows 10 row. That is the check doing its job, and
    // the fact it uncovered is worth the paragraph.
    //
    // The rows it started from are the INVENTED machine's, and the invented machine
    // is Windows 10 (see shotState). The machine underneath this harness is not. The
    // Windows 10 row exists only on Windows 10, so the two lists differ by one - and
    // the old form of the assertion would from now on have been asserting that the
    // harness and the machine it runs on are on the same Windows, which is not a
    // property of the installer and would fail on somebody else's clone.
    //
    // WHAT IS STILL A PROPERTY OF THE INSTALLER, and it is the one postReviewRow()'s
    // index depends on: within ONE run the count cannot change. The only thing it
    // depends on is the Windows build, and Windows does not change its build number
    // under a running process. A harness that deliberately renders a different
    // machine from the one it stands on cannot measure that directly, so it measures
    // the two halves that add up to it: the count the re-check produced is the count
    // fillPreflightRows() gives for the state the re-check ACTUALLY READ, and every
    // index the two lists share still asks the same question. The only row that may
    // be in one and not the other is the appended one, which is checked by name.
    {
        bcdgui::Wizard again;
        ZeroMemory(&again, sizeof(again));
        bcdgui::fillPreflightRows(&again, &g_run.state);

        int shared = wiz->reviewCount < savedCount ? wiz->reviewCount : savedCount;
        int longer = wiz->reviewCount > savedCount ? wiz->reviewCount : savedCount;
        bool sameSubjects = true;
        for (int i = 0; i < shared; i++)
            if (wcscmp(wiz->review[i].title, saved[i].title) != 0)
                sameSubjects = false;

        _snwprintf(what, 390,
                   L"the re-check posted the rows the state it read really produces "
                   L"(%d posted, %d for that state; the page started from %d, which "
                   L"is an invented machine)",
                   wiz->reviewCount, again.reviewCount, savedCount);
        check(wiz->reviewCount == again.reviewCount, what);

        _snwprintf(what, 390,
                   L"every index the two lists share asks the same question, and they "
                   L"differ by at most the one appended row (%d shared of %d)",
                   shared, longer);
        check(sameSubjects && longer - shared <= 1, what);

        if (longer != shared) {
            const Row* extra = (wiz->reviewCount > savedCount)
                               ? &wiz->review[longer - 1] : &saved[longer - 1];
            _snwprintf(what, 390,
                       L"...and the one they differ by is the appended Windows 10 row "
                       L"and nothing else (\"%s\")", extra->title);
            what[390] = 0;
            check(wcsstr(extra->title, L"Windows 10") != 0, what);
        }
    }

    // *** THE MIDI PORT SCREEN'S OFFER USED TO BE MEASURED HERE, ACROSS A REAL
    //     RE-CHECK, AND IT IS GONE ALONG WITH THE OFFER. *** chooseLoopMidiOffer()
    // and the address block it drove are removed - see the block over kMidiTitle in
    // setup.cpp - so there is no ladder left to follow across a rebuild. What is
    // still worth measuring on the one screen this harness can navigate to after a
    // REAL re-check is the negative: no button and no address came back from
    // nowhere.
    {
        int midi = -1;
        for (int i = 0; i < screenCount(wiz); i++)
            if (wiz->screens[i].title == ::kMidiTitle)
                midi = i;
        if (midi >= 0)
            setScreen(midi);
        UpdateWindow(g_page);
        UpdateWindow(g_frame);

        wchar_t now[128];
        now[0] = 0;
        if (g_actionBtn)
            GetWindowTextW(g_actionBtn, now, 128);
        _snwprintf(what, 390,
                   L"after a real re-check the MIDI port screen still has no action "
                   L"button (visible %d, text \"%s\")",
                   (g_actionBtn && IsWindowVisible(g_actionBtn)) ? 1 : 0, now);
        what[390] = 0;
        check(midi >= 0 && (!g_actionBtn || !IsWindowVisible(g_actionBtn)), what);

        const wchar_t* addrNow = (midi >= 0) ? wiz->screens[midi].addressUrl : 0;
        const wchar_t* leadNow = (midi >= 0) ? wiz->screens[midi].addressLead : 0;
        bool           openNow = (midi >= 0) && wiz->screens[midi].addressOpen != 0;
        _snwprintf(what, 390,
                   L"...and no address either - none of the three fields came back "
                   L"from a rebuild that has nothing to fill them from (address %s, "
                   L"lead %s, clickable %d)",
                   addrNow ? L"shown" : L"absent",
                   leadNow ? L"shown" : L"absent", openNow ? 1 : 0);
        what[390] = 0;
        check(midi >= 0 && addrNow == 0 && leadNow == 0 && !openNow, what);

        // *** AND THE LOCK THAT SURVIVED THE NAVIGATION IS LIFTED WHEN THE WORKER
        //     FINISHES. *** Same screen, same button, after the loop above drained
        //     WM_BCD_RDONE. Without this the check earlier in this function would pass
        // just as well against a primary button disabled for the rest of the run, which
        // would strand this flow on the one screen it was left on. Half a lock is not a
        // lock, and the two halves are cheap to state.
        _snwprintf(what, 390,
                   L"...and once the worker has finished, that same button is alive "
                   L"again - the lock is for the worker's life and not for the run's "
                   L"(enabled %d, worker running %d)",
                   IsWindowEnabled(g_primary) ? 1 : 0, g_rechecking ? 1 : 0);
        what[390] = 0;
        check(midi >= 0 && !g_rechecking && IsWindowEnabled(g_primary), what);

        if (checkScreen >= 0)
            setScreen(checkScreen);     // the nudge below is page 2's
    }

    memcpy(wiz->review, saved, sizeof(saved));
    wiz->reviewCount = savedCount;
    // ...and the table, so that the 144 DPI pass photographs the invented machine and
    // not this one. See the note where this copy was taken.
    memcpy(wiz->screens, savedScreens, sizeof(savedScreens));

    // *** AND THE MachineState ITSELF, WHICH IS THE THING THE OTHER TWO ARE COPIES OF.
    //     ***
    //
    // The two memcpys above put back the PRESENTATION and left the state live. That was
    // enough while nothing downstream re-derived a row from it - and it stopped being
    // enough the moment anything did. Measured rather than reasoned: at the start of the
    // 144 DPI pass g_run.state carried THIS machine, and a row rebuilt from it read
    // "Applied and confirmed on the device that is connected now
    // ({A63AEB86-...})" - a guid read out of this machine's registry, on a page whose
    // whole point is that it shows an invented one. The interface guid is named in
    // shotState() as one of the two per-machine values the leak of 2026-07-29 was about.
    //
    // So the invented machine goes back too, and the restore is now complete rather than
    // complete-looking: state, rows and table, in that order of ownership. Nothing
    // rendered changes - the table is put back byte for byte either way, and all 40
    // captures are byte-identical with this line present - but the 144 DPI pass no
    // longer runs over a state that describes the machine the harness happens to be on.
    //
    // The row restores have been here since the leak of 2026-07-29; the screens arrived
    // one round later; this is the third and last copy of that same correction, and it
    // is the one that removes the reason the other two were needed.
    shotState(&((::Run*)wiz->user)->state);

    InvalidateRect(g_page, 0, TRUE);
    UpdateWindow(g_page);

    // -------------------------------------------------------------------
    // The nudge. What it must do is add one sentence. What it must NOT do is read
    // the machine, because the state shown has to stay the state the Install button
    // acts on until the user asks for a new one.
    // -------------------------------------------------------------------
    RECT pr;
    GetClientRect(g_page, &pr);
    HDC dc = GetDC(g_page);
    g_deviceChanged = false;
    int quietH = renderReview(dc, pr.right, pr.bottom, true);

    Row before[kMaxRows];
    memcpy(before, wiz->review, sizeof(before));
    SendMessageW(g_frame, WM_DEVICECHANGE, (WPARAM)DBT_DEVICEARRIVAL, 0);
    check(g_deviceChanged, L"a device arriving raises the nudge");
    check(memcmp(before, wiz->review, sizeof(before)) == 0 &&
          wiz->reviewCount == savedCount,
          L"...and re-read NOTHING: not one row changed, so the page still shows the "
          L"snapshot the Install button acts on");

    int nudgedH = renderReview(dc, pr.right, pr.bottom, true);
    ReleaseDC(g_page, dc);
    _snwprintf(what, 390,
               L"the nudge takes room of its own instead of being painted over the "
               L"footer (%d -> %d)", quietH, nudgedH);
    check(nudgedH > quietH, what);

    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE;
    GetScrollInfo(g_page, SB_VERT, &si);
    _snwprintf(what, 390,
               L"and the handler relaid the page out, so the scroll range reaches the "
               L"new line (range %d..%d for %d)", si.nMin, si.nMax, nudgedH);
    check(nudgedH <= pr.bottom || si.nMax >= nudgedH - 1, what);

    g_deviceChanged = false;
    SendMessageW(g_frame, WM_DEVICECHANGE, (WPARAM)DBT_DEVICEREMOVECOMPLETE, 0);
    check(g_deviceChanged, L"unplugging raises it too, not only plugging in");

    // Put the page back the way the captures found it.
    g_deviceChanged = false;
    layout();
    InvalidateRect(g_page, 0, TRUE);
    UpdateWindow(g_page);
}

// ===========================================================================
// PART 3c - THE PAINTED ADDRESS IS CLICKED, FOR REAL, WITH THE BROWSER TAKEN OUT
//
// *** THE PROBLEM THIS SOLVES, STATED BEFORE THE SOLUTION: A CHECK THAT DROVE THIS
//     HONESTLY WOULD OPEN A BROWSER. *** The harness must never launch one, and it
// never executes the product. So there has to be a SEAM - somewhere the click can be
// driven and the open path observed with the launch suppressed - and the seam has to be
// part of the product's own shape rather than a hook added for testing, or what is
// tested is not what ships.
//
// *** THE SEAM IS Screen::addressOpen, AND IT IS THE PRODUCT'S OWN. *** gui.cpp is
// GIVEN the opener as a pointer and has no declaration of it, for the same structural
// reason Screen::action and Wizard::recheck are pointers: a message handler cannot call
// the thing by name even by mistake. So the table entry is the join, and swapping it for
// a counting function of the same signature drives every line of the real path - the
// real WM_LBUTTONDOWN in the real pageProc(), the real hit test, the real
// startReviewWorker(), the real thread, the real recheckProc() - and stops exactly at
// openPageInBrowser(). Nothing was added to the product to make this possible. The same
// technique is already used here on Screen::addressUrl and on Wizard::startBlockedNote.
//
// *** WHAT THE SPY RECORDS IS NOT ONLY THAT IT RAN. *** It keeps the URL it was handed,
// and the check compares that POINTER with Screen::addressUrl - the object
// renderSubject() painted on the line that was clicked. One opener serves both screens,
// so the whole of "this link goes where its own letters say" is that argument, and this
// is what holds it.
//
// *** AND THE NEGATIVE, WHICH IS THE HALF THAT PROVES THE STATE RULE. *** On a machine
// that already has the thing, the screen says what was found and does NOT tell anybody
// to download anything - so there is no address, and there must be nothing to click
// where the address would have been. That case is driven by emptying the entry's three
// address fields, re-laying the page out and clicking the SAME pixel. A renderer that
// left a stale rectangle behind would open a browser from blank paper, and this is what
// finds it.
//
// IT RUNS ONCE, at the end of the 96 DPI pass and after every picture has been saved,
// for the reason measureRecheckAndNudge() does: it really runs a worker and the flow
// really rebuilds its table when that worker finishes.
// ===========================================================================
static int            g_linkOpens     = 0;
static const wchar_t* g_linkOpenedUrl = 0;

// The stand-in for setup.cpp's openDownloadAddress(). Same signature, and it returns 0
// the way an opener that posted no rows returns 0 - which the window thread handles as
// "nothing to renumber", so the page comes out of this the way it went in.
static int linkOpenSpy(void* user, const wchar_t* url)
{
    (void)user;
    g_linkOpens++;
    g_linkOpenedUrl = url;
    return 0;
}

static void pumpUntilIdle(void)
{
    DWORD start = GetTickCount();
    for (;;) {
        MSG m;
        while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (!g_rechecking || GetTickCount() - start >= 15000)
            break;
        Sleep(5);
    }
}

// ===========================================================================
// *** THE SHIPPED OPENER'S OWN RETURN VALUE, WITH NO SPY ANYWHERE NEAR IT. ***
//
// THIS EXISTS BECAUSE THE REVIEW PROVED THE OTHER CHECKS CANNOT SEE IT. Reverting
// openDownloadAddress() to `return recheckMachine(user)` left the whole suite green:
// every click check swaps linkOpenSpy() into the table, and the spy hardcodes its own 0,
// so the product function was never on the path. The one behaviour deliberately changed
// in that round was the one behaviour nothing measured.
//
// *** THE SEAM THAT REACHES IT IS THE PRODUCT'S OWN URL GUARD, NOT A HOOK. ***
// openPageInBrowser() refuses any address containing a character outside printable ASCII
// or one a command interpreter would treat as punctuation - isSafeUrlForCommandLine() in
// common.cpp - and returns false with ERROR_INVALID_PARAMETER before CreateProcess or
// ShellExecute is reached. So calling the real function with an address its own guard
// refuses runs the WHOLE of it - the preamble, openPageAndSay(), openPageInBrowser() and
// the guard - and creates no process. No browser is launched and the product is not
// executed; this is a function call inside the harness, like every other call here.
//
// *** AND THE RETURN IS NOT PATH-DEPENDENT, WHICH IS WHAT MAKES THE REFUSED ADDRESS
//     SUFFICIENT. *** openDownloadAddress() has exactly ONE return statement. A revert to
// `return recheckMachine(user)` replaces that one statement, so it is reached by the
// refused path exactly as it would be by the launching path.
//
// TWO OBSERVATIONS, NOT ONE. The value itself, and that no review row came back - because
// "it did not measure" and "it returned 0" are different claims and a function could make
// one true while breaking the other.
// ===========================================================================
static void measureTheRealOpenerReturns(Wizard* wiz)
{
    wchar_t what[400];

    // *** THE ROWS, THE TABLE AND THE MachineState ARE ALL PUT BACK, AND THE THIRD IS
    //     NOT BELT-AND-BRACES. *** The shipped function measures nothing, so on a correct
    // program there is nothing to restore. On a REVERTED one there is: injection I ran
    // the real recheckMachine(), which wrote THIS machine over the invented one, and the
    // 144 DPI pass then failed three unrelated checks about a guid read out of this
    // machine's registry - the leak of 2026-07-29, arriving through a new door. The two
    // checks below are the signal; three collateral reds in another suite are noise that
    // would send the next reader to the wrong place. Same three restores, same order of
    // ownership, as measureRecheckAndNudge().
    Row saved[kMaxRows];
    memcpy(saved, wiz->review, sizeof(saved));
    int savedCount = wiz->reviewCount;
    Screen savedScreens[kMaxScreens];
    memcpy(savedScreens, wiz->screens, sizeof(savedScreens));
    for (int i = 0; i < wiz->reviewCount; i++)
        wcscpy(wiz->review[i].title, L"NOTHING CAME BACK");

    // A space is below '!' and is therefore outside what isSafeUrlForCommandLine()
    // accepts, so this never becomes a command line. It is not a real address and is not
    // meant to be one: what is under test is the return, and the guard is what keeps a
    // browser out of a test run.
    static const wchar_t* const kRefused = L"https://example.invalid/ not an address";

    bcdsetup::setConsoleEcho(false);
    // ::, like ::kMidiTitle and ::kZadigTitle above: it is a file-scope static in
    // setup.cpp and not part of the bcdsetup interface, which is the whole reason gui.cpp
    // can only ever reach it through the table.
    int rc = ::openDownloadAddress(wiz->user, kRefused);
    bcdsetup::setConsoleEcho(true);

    // Anything the call posted, dealt with before the rows are read back - a revert would
    // post its rows through the window, and reading before draining would find the
    // sentinels intact for the wrong reason.
    pumpUntilIdle();

    int sentinels = 0;
    for (int i = 0; i < wiz->reviewCount; i++)
        if (wcsstr(wiz->review[i].title, L"NOTHING CAME BACK"))
            sentinels++;

    wprintf(L"  the SHIPPED openDownloadAddress() returned %d; %d of %d sentinel rows "
            L"survived\n", rc, sentinels, wiz->reviewCount);

    _snwprintf(what, 390,
               L"the SHIPPED openDownloadAddress() returns 0 - the product's own "
               L"function, called here with an address its own guard refuses so that no "
               L"process is created, and not the harness's stand-in. It returned %d",
               rc);
    what[390] = 0;
    check(rc == 0, what);

    _snwprintf(what, 390,
               L"...and it measured NOTHING on the way - every one of the %d review rows "
               L"still carries the sentinel this check wrote (%d of them), so no re-check "
               L"ran and no row came back", wiz->reviewCount, sentinels);
    what[390] = 0;
    check(wiz->reviewCount > 0 && sentinels == wiz->reviewCount, what);

    memcpy(wiz->review, saved, sizeof(saved));
    wiz->reviewCount = savedCount;
    memcpy(wiz->screens, savedScreens, sizeof(savedScreens));
    shotState(&((::Run*)wiz->user)->state);
    layout();
    InvalidateRect(g_page, 0, TRUE);
    UpdateWindow(g_page);
}

static void measureAddressLinkPress(Wizard* wiz, const wchar_t* title,
                                    const wchar_t* screen, const wchar_t* wantUrl)
{
    wchar_t what[400];

    int page = -1;
    for (int i = 0; i < screenCount(wiz); i++)
        if (wiz->screens[i].title == title)
            page = i;
    _snwprintf(what, 390, L"%s: the flow still has the screen this press is about",
               screen);
    what[390] = 0;
    check(page >= 0, what);
    if (page < 0)
        return;

    setScreen(page);
    UpdateWindow(g_page);
    UpdateWindow(g_frame);

    RECT block;
    RECT link;
    if (!lastAddressBox(&block) || !lastAddressLinkBox(&link)) {
        _snwprintf(what, 390,
                   L"%s: the address and its hit region are on the screen this press is "
                   L"about", screen);
        what[390] = 0;
        check(false, what);
        return;
    }

    Screen* self = &wiz->screens[page];
    const wchar_t* keepUrl  = self->addressUrl;
    const wchar_t* keepLead = self->addressLead;
    int (*keepOpen)(void*, const wchar_t*) = self->addressOpen;

    // *** /preview IS LEFT SET, AND UNTIL THE CONTROLLER'S RULING OF 2026-07-31 THIS
    //     FUNCTION LIFTED IT. *** These renders run in /preview. The first version of
    // this suite cleared startBlockedNote for the duration, because the click was
    // refused by it - and that refusal was itself the defect: a painted address that
    // swallows a press has nothing to grey and nothing to put an explanation beside,
    // which is worse than either half of the owner's own rule about greyed buttons.
    // /preview promises that nothing is WRITTEN and nothing is REGISTERED, and asking a
    // browser to open a public address does neither.
    //
    // So the mode is now the thing under test rather than something got out of the way,
    // and the pair below is what makes that a measurement: on ONE screen at ONE moment,
    // the control that INSTALLS is refused and the control that OPENS A PAGE is not.
    // Blocking by consequence and not by appearance.
    int  actionEnabledBefore = (g_actionBtn && IsWindowEnabled(g_actionBtn)) ? 1 : 0;
    bool onlyPage            = self->actionOnlyOpensAPage;
    _snwprintf(what, 390,
               L"%s: this render really is /preview, so what follows is about the mode - "
               L"the note is %s, this screen's button %s and the mode %s it (enabled %d)",
               screen, wiz->startBlockedNote ? L"set" : L"MISSING",
               onlyPage ? L"opens a web page" : L"writes to this machine",
               onlyPage ? L"leaves it alone" : L"refuses it", actionEnabledBefore);
    what[390] = 0;
    check(wiz->startBlockedNote != 0 && (actionEnabledBefore != 0) == onlyPage, what);

    int  midY = (int)(link.top + link.bottom) / 2 - g_scrollY;
    int  midX = (int)(link.left + link.right) / 2;
    int  leadX = (int)block.left + 1;
    DWORD windowTid = GetCurrentThreadId();

    // -------------------------------------------------------------------
    // 1. THE ADDRESS IS CLICKED.
    // -------------------------------------------------------------------
    self->addressOpen = linkOpenSpy;
    g_linkOpens       = 0;
    g_linkOpenedUrl   = 0;
    g_recheckTid      = 0;
    int rowsBefore    = wiz->reviewCount;
    SendMessageW(g_page, WM_LBUTTONDOWN, 0, MAKELPARAM(midX, midY));

    // ASKED BEFORE A SINGLE MESSAGE IS PUMPED, so it is about what the click handler did
    // and not about what the worker got round to. The worker posts WM_BCD_RDONE and
    // cannot be dispatched until the loop below runs, so this is deterministic however
    // fast the machine is - the same argument measureRecheckAndNudge() makes about the
    // re-check button.
    //
    // AND IT IS THE OTHER HALF OF THE /preview PAIR: the click above was taken while the
    // note was set. The link opens a page, so the mode leaves it alone on BOTH screens -
    // including the one whose button, on this invented machine, the mode refuses.
    _snwprintf(what, 390,
               L"%s: clicking the painted address starts the open EVEN UNDER /preview, "
               L"because opening a page writes nothing - and it takes the controls away "
               L"first the way a press does (worker in flight %d, Check again enabled "
               L"%d, note set %d)", screen, g_rechecking ? 1 : 0,
               (g_recheckBtn && IsWindowEnabled(g_recheckBtn)) ? 1 : 0,
               wiz->startBlockedNote ? 1 : 0);
    what[390] = 0;
    check(g_rechecking && (!g_recheckBtn || !IsWindowEnabled(g_recheckBtn)) &&
          wiz->startBlockedNote != 0, what);

    pumpUntilIdle();

    wprintf(L"  %s: the press ran the opener %d time(s), handed it \"%s\", worker thread "
            L"%lu against window %lu\n", screen, g_linkOpens,
            g_linkOpenedUrl ? g_linkOpenedUrl : L"(nothing)",
            (unsigned long)g_recheckTid, (unsigned long)windowTid);

    _snwprintf(what, 390,
               L"%s: ...and the open path was really reached - the opener ran %d time(s), "
               L"and the browser did not, because the seam is the table's own pointer",
               screen, g_linkOpens);
    what[390] = 0;
    check(g_linkOpens == 1, what);

    // *** THE ADDRESS IT WAS HANDED IS THE ONE THE RENDERER PAINTED. *** One opener
    // serves both screens, so this pointer is the whole of "the link goes where its own
    // letters say". WHERE EACH SIDE COMES FROM: the left is what gui.cpp passed at the
    // moment of the click, kept by the spy; the right is the published constant in
    // common.cpp that this screen's entry was built from. Nothing derives one from the
    // other.
    _snwprintf(what, 390,
               L"%s: ...and it was handed THIS screen's address and not the other "
               L"screen's - \"%s\"", screen,
               g_linkOpenedUrl ? g_linkOpenedUrl : L"(nothing)");
    what[390] = 0;
    check(g_linkOpenedUrl == wantUrl && g_linkOpenedUrl == keepUrl, what);

    // *** THE ONE RULE OF gui.cpp, AS A NUMBER, FOR THE NEW WAY OF STARTING A WORKER. ***
    // Opening a browser ends in launchUnelevated() or ShellExecuteW, and a process launch
    // on the window thread is a window that stops repainting. CreateThread's own out
    // parameter against the id of the thread this test runs on, which IS the window's.
    _snwprintf(what, 390,
               L"%s: ...on a thread that is NOT the window's, so a slow browser cannot "
               L"grey the installer out (worker %lu, window %lu)", screen,
               (unsigned long)g_recheckTid, (unsigned long)windowTid);
    what[390] = 0;
    check(g_recheckTid != 0 && g_recheckTid != windowTid, what);

    // *** A 0 FROM THE OPENER LEAVES THE PAGE ALONE. *** recheckProc() posts whatever
    // comes back through postReviewDone(), and the WM_BCD_RDONE handler reads 0 as
    // "nothing was posted, leave the rows alone". That is the property the round which
    // stopped the opener measuring rests on, and this is it, driven through the real
    // handler.
    //
    // *** WHAT THIS DOES NOT SAY, AND THE COMMENT HERE USED TO SAY IT. *** It read "the
    // spy returns 0 exactly as the real opener now does, so this measures the shipped
    // value and not a stand-in for it." That is FALSE and the review proved it: reverting
    // openDownloadAddress() to `return recheckMachine(user)` leaves this suite at 967
    // green, because linkOpenSpy() hardcodes its own 0 and the product function is never
    // on this path. A correct change under a guard that cannot see it, with a comment
    // claiming the guard sees it, is this project's signature defect wearing the clothes
    // of a fix. The shipped return value is measured in measureTheRealOpenerReturns(),
    // which calls the product function itself.
    //
    // WHERE EACH SIDE COMES FROM: the left is Wizard::reviewCount read back off the real
    // Wizard after the real handler ran; the right is the same field read before the
    // click. If 0 were mishandled the page would come out of a link click with its rows
    // renumbered to nothing.
    _snwprintf(what, 390,
               L"%s: ...and a 0 from the opener leaves the page exactly as it was - %d "
               L"review rows before the click and %d after. (What the SHIPPED opener "
               L"returns is measured separately; the spy's 0 is the spy's)",
               screen, rowsBefore, wiz->reviewCount);
    what[390] = 0;
    check(rowsBefore > 0 && wiz->reviewCount == rowsBefore, what);

    // -------------------------------------------------------------------
    // 2. THE DIM LEAD BESIDE IT IS CLICKED, AND NOTHING HAPPENS.
    //
    // The hit region is the address's own run and the lead is not part of it - see
    // lastAddressLinkBox() in gui.h for why that was decided rather than derived. This is
    // that decision, driven.
    // -------------------------------------------------------------------
    self->addressOpen = linkOpenSpy;   // rebuildScreens() put the real one back
    g_linkOpens       = 0;
    g_linkOpenedUrl   = 0;
    SendMessageW(g_page, WM_LBUTTONDOWN, 0, MAKELPARAM(leadX, midY));
    pumpUntilIdle();
    _snwprintf(what, 390,
               L"%s: clicking the dim lead beside the address does NOTHING - the region "
               L"that acts is the accent-coloured run and not the block (opener ran %d "
               L"time(s), worker in flight %d)", screen, g_linkOpens,
               g_rechecking ? 1 : 0);
    what[390] = 0;
    check(g_linkOpens == 0 && !g_rechecking, what);

    // -------------------------------------------------------------------
    // 3. THE STATE WITH NO ADDRESS: THE SAME PIXEL DOES NOTHING.
    //
    // This is the half of the state rule that a check on the table cannot reach. Every
    // existing check asks a freshly built Wizard whether the fields are null; this asks
    // the WINDOW whether the place they used to be is still live. The failure it is
    // named for is a rectangle the renderer forgot to clear - a click on blank paper, on
    // the one screen whose whole point in the satisfied state is that it asks for
    // nothing.
    // -------------------------------------------------------------------
    self->addressUrl  = 0;
    self->addressLead = 0;
    self->addressOpen = linkOpenSpy;   // still installed, so a call would be SEEN
    layout();
    InvalidateRect(g_page, 0, TRUE);
    UpdateWindow(g_page);

    RECT gone;
    bool stillLive = lastAddressLinkBox(&gone);
    g_linkOpens    = 0;
    SendMessageW(g_page, WM_LBUTTONDOWN, 0, MAKELPARAM(midX, midY));
    pumpUntilIdle();
    _snwprintf(what, 390,
               L"%s: with no address on the screen, a click where it used to be does "
               L"NOTHING - the hit region is gone (live %d, box %d,%d-%d,%d) and the "
               L"opener ran %d time(s)", screen, stillLive ? 1 : 0, (int)gone.left,
               (int)gone.top, (int)gone.right, (int)gone.bottom, g_linkOpens);
    what[390] = 0;
    check(!stillLive && g_linkOpens == 0 && !g_rechecking, what);

    // Everything this function borrowed, back the way it found it, and the page relaid
    // out from the restored table so that whatever runs next reads the layout the window
    // really shows.
    self->addressUrl      = keepUrl;
    self->addressLead     = keepLead;
    self->addressOpen     = keepOpen;
    layout();
    InvalidateRect(g_page, 0, TRUE);
    UpdateWindow(g_page);
}

// ===========================================================================
// A SCREEN'S TEXT PANE, WHICHEVER SCREEN IT IS ON
//
// What is being proved here is not that the pane looks right. It is that the two
// halves of a screen that has one - the painted content, which scrolls, and the
// pane, which does not - divide the page between them with nothing left over and
// neither one squeezed to uselessness, and that the pane cannot cut a line at any
// DPI.
//
// *** IT TAKES THE SCREEN'S NAME NOW, BECAUSE THERE ARE TWO PANES. *** These were
// page 2's checks and page 2's alone, gated on "is this the machine review", and they
// asserted a Wizard field that no longer exists. A pane is a property of a SCREEN as
// of this round - layout() reads Screen::paneText and nothing else - so the same
// arithmetic is asked of every screen that asks for one, and the name in the message
// says which. `who` reproduces the exact wording page 2's checks already had, so the
// review screen's names are unchanged and the new screen's are new.
//
// THE STYLE BITS ARE THE STRONGEST THING IN THIS FUNCTION and they are read off the
// control the PRODUCT built: shootAtDpi() calls gui.cpp's own buildPane(), and
// setScreen() fills it. An EDIT with ES_MULTILINE and without ES_AUTOHSCROLL wraps,
// and WS_VSCROLL makes the overflow reachable, whatever the text, the font or the
// window size. No capture can say that.
// ===========================================================================
static void measureScreenPane(int dpi, int pw, int ph, const wchar_t* who,
                              const wchar_t* caption)
{
    wchar_t what[400];

    bool have = g_review != 0 && IsWindowVisible(g_review);
    _snwprintf(what, 390,
               L"%ddpi %s: the pane this screen asks for is really there (viewport %d "
               L"of a %d high page)", dpi, who, g_viewH, ph);
    check(have, what);
    if (!have)
        return;

    RECT r;
    GetWindowRect(g_review, &r);
    MapWindowPoints(HWND_DESKTOP, g_page, (POINT*)&r, 2);
    int m = S(kMargin);
    wprintf(L"  %s pane %d..%d x %d..%d of %dx%d, viewport %d, margin %d\n", who,
            (int)r.left, (int)r.right, (int)r.top, (int)r.bottom, pw, ph, g_viewH, m);

    _snwprintf(what, 390,
               L"%ddpi %s: the pane FILLS the page's width - it spans %d..%d of "
               L"a %d wide page, margins wanted %d", dpi, who, (int)r.left, (int)r.right,
               pw, m);
    check((int)r.left == m && (int)r.right == pw - m, what);

    // *** THE PAGE IS DIVIDED WITH NOTHING LEFT OVER. *** The scrolling strip, the
    // gap, and the pane have to BE the page. If they did not, the missing pixels
    // would be a band of page that scrolls nothing and shows nothing, and the rows
    // would be reported reachable while a strip of them sat behind it.
    _snwprintf(what, 390,
               L"%ddpi %s: the scrolling strip and the pane are the whole page "
               L"(%d + %d gap + %d = %d)", dpi, who, g_viewH, S(kReviewPaneGap),
               (int)(r.bottom - r.top), ph);
    check(g_viewH + S(kReviewPaneGap) + (int)(r.bottom - r.top) == ph &&
          (int)r.bottom == ph, what);

    _snwprintf(what, 390,
               L"%ddpi %s: neither half is squeezed to nothing - the rows keep "
               L"%d (floor %d) and the pane %d (floor %d)", dpi, who, g_viewH,
               S(kReviewRowsMinH), (int)(r.bottom - r.top), S(kReviewPaneMinH));
    check(g_viewH >= S(kReviewRowsMinH) &&
          (int)(r.bottom - r.top) >= S(kReviewPaneMinH), what);

    DWORD st = (DWORD)GetWindowLongPtrW(g_review, GWL_STYLE);
    _snwprintf(what, 390,
               L"%ddpi %s: the pane WRAPS (no ES_AUTOHSCROLL) so no line of it "
               L"can be cut at the right edge", dpi, who);
    check((st & ES_AUTOHSCROLL) == 0 && (st & ES_MULTILINE) != 0, what);
    _snwprintf(what, 390,
               L"%ddpi %s: the pane has WS_VSCROLL so the rest of it "
               L"is reachable", dpi, who);
    check((st & WS_VSCROLL) != 0, what);

    // The caption is the pane's first line rather than something a reader has to
    // scroll to.
    //
    // *** WHERE EACH SIDE COMES FROM, AND IT CHANGED. *** It used to read the caption
    // off a Wizard field, which the same buildPane() call had just poured into the
    // control - so both sides came from one pointer and the check could only fail if
    // the copy itself broke. The caption is a field of the SCREEN now, and it is
    // passed in here by the caller from the table, while the left side is read back
    // out of the control with GetWindowTextW after setScreen() ran. A screen showing
    // another screen's pane fails this; it did not before.
    int len = GetWindowTextLengthW(g_review);
    wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), 0,
                                       (SIZE_T)(len + 2) * sizeof(wchar_t));
    bool capFirst = false;
    if (buf) {
        GetWindowTextW(g_review, buf, len + 1);
        capFirst = caption != 0 &&
                   wcsncmp(buf, caption, wcslen(caption)) == 0;
        HeapFree(GetProcessHeap(), 0, buf);
    }
    _snwprintf(what, 390,
               L"%ddpi %s: the caption is the pane's FIRST line, so what the pane "
               L"is does not depend on scrolling it", dpi, who);
    check(capFirst, what);

    int lines = (int)SendMessageW(g_review, EM_GETLINECOUNT, 0, 0);
    int firstVisible = (int)SendMessageW(g_review, EM_GETFIRSTVISIBLELINE, 0, 0);
    _snwprintf(what, 390,
               L"%ddpi %s: the pane opens at the top (first visible line %d of "
               L"%d laid out)", dpi, who, firstVisible, lines);
    check(firstVisible == 0, what);
}

// What is in the pane, asked of the CONTROL and not of the table, and asked of both
// panes against each other.
//
// *** NEEDLE IN ITS OWN TEXT PLUS PAIRWISE DIFFERENCE IS NOT DISCRIMINATION, WHICH IS
//     THE HAZARD THIS FILE HAS PRODUCED A FINDING ON FOR FOUR ROUNDS RUNNING. *** So
// each pane is asked for a phrase that is ITS and for the absence of the other's. Two
// panes that both showed the walkthrough - which is exactly what a screen whose
// paneText was ignored would give, since the control is one control and the last text
// poured into it would still be there - pass "mine is present" and fail this.
//
// WHERE EACH SIDE COMES FROM: the haystack is what GetWindowTextW read back out of
// the EDIT the product created and setScreen() filled; the needles are phrases
// written in this file, which setup.cpp cannot see.
static void checkPaneCarriesItsOwnText(int dpi, const wchar_t* who,
                                       const wchar_t* mine, const wchar_t* notMine)
{
    wchar_t what[400];
    int     len = GetWindowTextLengthW(g_review);
    wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), 0,
                                       (SIZE_T)(len + 2) * sizeof(wchar_t));
    bool has = false, stray = false;
    if (buf) {
        GetWindowTextW(g_review, buf, len + 1);
        has   = wcsstr(buf, mine)    != 0;
        stray = wcsstr(buf, notMine) != 0;
        HeapFree(GetProcessHeap(), 0, buf);
    }
    _snwprintf(what, 390,
               L"%ddpi %s: the pane holds THIS screen's text and not another "
               L"screen's - \"%s\" present %d, \"%s\" present %d (%d characters)",
               dpi, who, mine, has ? 1 : 0, notMine, stray ? 1 : 0, len);
    what[390] = 0;
    check(has && !stray, what);
}

// *** A PICTURE OF A SCREEN WITH A PANE, THE WAY IT OPENS, WHICH renderTall() CANNOT
//     TAKE. ***
//
// page-<name>-*.png is a render of the whole CONTENT, at whatever height the painted
// blocks need - which is what the clipping proof is about and is taller than the
// window. A pane that is fixed to the bottom of the WINDOW has no meaningful place in
// a picture of the content, so renderTall() does not put it in one and that capture
// shows a screen nobody sees: the content, and then blank paper where the pane is.
//
// This is the other picture: the page at its real client size, painted at scroll
// position zero, with the pane on top exactly where layout() put it. Between the two,
// one shows that nothing is clipped and the other shows what a user gets.
//
// *** IT DOES NOT NEED A PANE, AND THE ROUND THAT ASSUMED IT DID TOOK THE PICTURE OF
//     THE WORST SCREEN IN THE PROGRAM OUT OF THE RUN. ***
//
// This function used to return early unless the pane was visible, and page 2 lost its
// pane in that round - so page 2's at-rest capture simply stopped being taken, in
// silence, and its only remaining picture was the tall render that this file's own
// comment calls a page nobody sees. The picture that would show a human the owner's
// complaint - 666 logical px of overflow at 96 DPI, 1111 at 144 - was the one that
// went away. This project's defining defect was a button that appeared in no committed
// image; that is the same defect with the subject changed.
//
// "At rest" is about the SCROLL POSITION and the SIZE, not about the pane: the page at
// its real client height, painted at offset zero, which is the first thing a user sees
// and the thing the tall render deliberately is not. A screen with a pane gets the
// pane printed on top where layout() put it; a screen without one gets the same
// picture without that step. So there is no early return any more, and the guard is
// on the pane alone.
//
// *** WHAT HAPPENED TO THE FILE NAMES, SAID HERE RATHER THAN LEFT TO A DIFF, AND
//     CORRECTED: IT IS NOT A RENAME. *** The round that moved the pane described this
// as renaming page-2-at-rest-*.png to page-2b-midi-at-rest-*.png. Git does not treat
// it as one and neither should the record - the md5s have nothing in common, because
// they are pictures of two different screens. What really happened, now that page 2
// gets its capture back:
//   page-2-at-rest-*.png       still page 2 at rest, MODIFIED - the same screen with
//                              its pane gone and its rows using the whole page.
//   page-2b-midi-at-rest-*.png a NEW capture of a NEW screen. Nothing preceded it.
// The instinct behind the wrong word was right - a file called "page 2 at rest"
// showing something that is not page 2 would be worse - but "at rest" never meant
// "has a pane", so the name was never the problem.
static void savePaneAtRest(int dpi, int pw, int ph, const wchar_t* name)
{
    HDC     screen = GetDC(0);
    HDC     mem    = CreateCompatibleDC(screen);
    HBITMAP bmp    = CreateCompatibleBitmap(screen, pw, ph);
    HGDIOBJ ob     = SelectObject(mem, bmp);
    fillRect(mem, 0, 0, pw, ph, CLR_PAGE_BG);
    SetBkMode(mem, TRANSPARENT);
    // The product's own dispatch, so this is the page the window really draws and not
    // one this file assembled.
    //
    // *** AND IT ASKS THE KIND, BECAUSE THIS CAPTURE IS NO LONGER ONLY FOR CHECK
    //     SCREENS. *** "Get Zadig" measures nothing and is kScreenInfo, and it has a
    // pane, so it gets one of these pictures. Calling renderCheckScreen() on it gave
    // the right answer by accident - both dispatches route a non-review, non-opening
    // screen to renderSubject() - and an accident between two dispatches is the thing
    // that put a second screen's band into the opening's PNG in this same round.
    if (kindAt(g_screen) == kScreenInfo)
        renderInfoScreen(mem, pw, ph, false);
    else
        renderCheckScreen(mem, pw, ph, false);

    // *** THE PANE'S RECTANGLE IS CLEARED FIRST, AND THE REASON IS A REAL ARTIFACT.
    // printChildInto() sends WM_PRINTCLIENT, which renders the CLIENT area - and an
    // EDIT's client area excludes both the client edge and its vertical scroll bar,
    // about 19 pixels on the right at 96 DPI. On pages 3 and 4 nothing is painted
    // under the pane, so that strip has always been blank and nobody noticed. Here
    // there IS content under it, and the first version of this capture showed the tail
    // of two rows apparently poking THROUGH the pane - a defect the picture invented,
    // since the real window has WS_CLIPCHILDREN and never paints there at all.
    // Clearing the pane's whole window rect first is what makes the picture agree
    // with the window.
    //
    // ALL OF THIS IS THE PANE'S HALF and it is skipped on a screen that has none. The
    // capture itself is not: see the block above this function.
    if (g_review && IsWindowVisible(g_review)) {
        RECT pr;
        GetWindowRect(g_review, &pr);
        MapWindowPoints(HWND_DESKTOP, g_page, (POINT*)&pr, 2);
        fillRect(mem, (int)pr.left, (int)pr.top, (int)(pr.right - pr.left),
                 (int)(pr.bottom - pr.top), CLR_PAGE_BG);
        printChildInto(mem, g_review);
    }
    wchar_t png[512];
    _snwprintf(png, 500, L"%s\\%s-at-rest-%ddpi.png", g_shotDir, name, dpi);
    savePngChecked(bmp, pw, ph, png);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(0, screen);
}

// ===========================================================================
// THE ZADIG SCREENSHOT, AT THIS DPI
//
// docs\Zadig.png is 574 x 254 and is drawn at its NATIVE logical width, so at 96 DPI
// it is one pixel per pixel and at 144 it is 861 wide. The question this asks is the
// only one that can go wrong: does the page still hold it, and is it still DRAWN?
//
// *** BECAUSE renderReview() SKIPS IT RATHER THAN SHRINKING IT. *** A page too narrow
// for the picture drops the picture and keeps the caption, which is the right
// behaviour and is also a silent one - so a change that made the page narrower, or
// the picture wider, would produce a page 2 with no screenshot on it and every other
// check on this page still green. This is what makes that loud.
//
// The 144 DPI pass is the one that matters. It is the tallest and widest form of the
// page and it is where this round was most likely to break the clipping proof; if it
// does break it, the proof is working.
// ===========================================================================
static void measureZadigShot(int dpi, int pw)
{
    wchar_t what[400];

    // *** ASKED AS A CHECK AND NOT AS AN EARLY RETURN. *** A guard that returned here
    // would take the three measurements below out of the run with it, and the run
    // would report one number smaller and every remaining check green - which is this
    // project's oldest failure mode, a suite reporting success for work it never did.
    // The wizard this harness renders is setup.cpp's, and setup.cpp asks for the
    // picture, so the only way this is false is that somebody stopped asking.
    _snwprintf(what, 390,
               L"%ddpi checks: page 2 asks for the Zadig picture at all", dpi);
    check(g_wiz->zadigCaption != 0, what);
    if (!g_wiz->zadigCaption)
        return;

    int     want = S(kZadigShotW);
    int     m    = S(kMargin);

    wprintf(L"  zadig shot %dx%d (asked for %d wide), page %d, margins %d\n",
            g_shotW, g_shotH, want, pw, m);

    _snwprintf(what, 390,
               L"%ddpi checks: the Zadig picture decoded at the width the page asked "
               L"for (%d of %d)", dpi, g_shotW, want);
    check(g_shot != 0 && g_shotW == want, what);

    // The aspect ratio, so that a picture scaled by one axis only would be visible
    // rather than merely ugly. 574 x 254 at 96, 861 x 381 at 144.
    int wantH = MulDiv(want, 254, 574);
    _snwprintf(what, 390,
               L"%ddpi checks: and in proportion - %d high against the %d that "
               L"574x254 gives at this width", dpi, g_shotH, wantH);
    check(g_shotH >= wantH - 1 && g_shotH <= wantH + 1, what);

    // *** AND IT IS ACTUALLY DRAWN, WHICH IS THE CONDITION renderReview() TESTS. ***
    // Same expression, so this cannot pass while the page silently drops the picture.
    _snwprintf(what, 390,
               L"%ddpi checks: the page is wide enough that the picture is DRAWN and "
               L"not skipped (%d + padding <= %d)", dpi, g_shotW, pw - 2 * m);
    check(g_shot != 0 && g_shotW + S(2 * kPicturePad) <= pw - 2 * m, what);
}

// Puts a given line of an edit pane at the top of its visible area, so that a
// picture can be taken of a part of the text that is not on screen at rest.
static int scrollPaneTo(HWND edit, const wchar_t* needle)
{
    int len = GetWindowTextLengthW(edit);
    if (len <= 0)
        return -1;
    wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), 0,
                                       (SIZE_T)(len + 2) * sizeof(wchar_t));
    if (!buf)
        return -1;
    GetWindowTextW(edit, buf, len + 1);
    const wchar_t* at = wcsstr(buf, needle);
    int line = -1;
    if (at) {
        int off = (int)(at - buf);
        line = (int)SendMessageW(edit, EM_LINEFROMCHAR, (WPARAM)off, 0);
        int first = (int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
        SendMessageW(edit, EM_LINESCROLL, 0, (LPARAM)(line - first));
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return line;
}

// ===========================================================================
// *** THE PICTURE, ON THE SCREEN IT NOW HAS, AND THE TWO QUESTIONS THAT ARE TRUE. ***
//
// The plan asked for two checks here and BOTH WERE IMPOSSIBLE. They are written down
// because getting either wrong would have cost a round:
//
//   `shot.bottom <= pane.top` is UNSATISFIABLE at 96 DPI on any screen that has a
//   pane. A screen with a pane gets a 221 logical pixel strip; the picture alone is
//   254. Before a title, a rule, a row or one bullet the picture is already 33 pixels
//   taller than the whole strip, so the check could never go green and "make it
//   green" would have meant shrinking the one picture this project refuses to shrink.
//   That is WHY the binding screen has no pane - a design consequence, not a taste.
//
//   `shot.right - shot.left == 574` is FALSE at 144 DPI. The shot is decoded at
//   S(kZadigShotW) - 574 at 96, 861 at 144 - and it is DPI scaled at DECODE time
//   deliberately, because resampling at decode is exactly what keeps the two 9 pixel
//   strings legible. The plan's own justification for the literal was the reason the
//   code cannot use it.
//
// So: is the picture drawn INSIDE the strip this screen actually gets, and is it
// drawn at S(kZadigShotW) for this DPI.
//
// *** THE BOX IS THE RECTANGLE AlphaBlend WRITES, AND UNTIL THIS ROUND IT WAS NOT.
//     *** It took its ORIGIN from the plate drawPicture() fills and its SIZE from the
// image, so it was a rectangle nothing draws, and its bottom edge sat S(kPicturePad)
// above the last row of the picture. The CONCLUSION was true at both DPIs and stays
// true; what was wrong is the number beside it. This screen reported 15 pixels of
// clearance at 96 DPI where the picture has 7, and 16 at 144 where it has 4 - more
// than double, on the one measurement the round that gave the picture this screen
// exists to make. See lastZadigShotBox() in gui.h.
//
// The PLATE is printed below as well, without a verdict on it. It is background and
// it is S(kPicturePad) larger than the picture on every side, so its last row falls
// outside the strip at both DPIs - 1 pixel at 96 and 8 at 144. That is a row of pale
// grey and not a row of the picture, and turning it into a failure would be asserting
// something about decoration; it is printed so that nobody has to rediscover it.
//
// WHERE EACH SIDE COMES FROM.
//
//   THE FIRST: the left is the box renderSubject() recorded while laying the page out,
//   read back through the product's own lastZadigShotBox(); the right is g_viewH, the
//   layout's own strip, which measureScreenPane() pins against a real pane rectangle
//   elsewhere. Neither is computed from the other, and neither is a literal here.
//
//   THE SECOND HAD ONE SOURCE AND HAS TWO NOW. It compared the recorded box's width
//   against S(kZadigShotW), and the box's width IS g_shotW, and g_shotW is assigned
//   from the targetW the decoder was handed, which is S(kZadigShotW) - a value against
//   the expression it comes from, so the check had no failing state at all. The second
//   side is the BITMAP: GetObjectW() on g_shot reports the width of the DIB section
//   AlphaBlend actually samples, held by GDI and written by CreateDIBSection. So the
//   comparison is now "the rectangle the layout recorded" against "the bitmap that was
//   painted into it", which is exactly the disagreement this finding was about, and it
//   goes red the moment renderSubject()'s arithmetic and the picture part company.
//
//   WHAT IT STILL CANNOT CATCH, said plainly rather than left to be discovered: a
//   decoder that produced a bitmap of the wrong WIDTH would move both sides together,
//   because CreateDIBSection is given the same dstW that becomes g_shotW. The height
//   is the one that is genuinely measured - dstH comes from the resource's own aspect
//   ratio - and measureZadigShot() above asserts it against 254/574.
// ===========================================================================
// ---------------------------------------------------------------------------
// *** HOW MUCH CLEAR PAPER THE PICTURE MUST KEEP BELOW IT, BY DPI, AND WHY THIS IS A
//     FLOOR INSTEAD OF A BOOLEAN. ***
//
// The two checks below used to ask `box.bottom <= g_viewH` - is the picture inside the
// strip - and PRINT the clearance beside the verdict without asserting it. So the
// clearance could go from 7 to 1 with the suite fully green, and only the last pixel of
// erosion would ever fail anything. That is a guard against BREACH and not against
// EROSION, and it was declared as a concern by the round that built the instrument
// before a reviewer named it as a finding: the concern and the guard did not match.
//
// It matters here more than anywhere because this clearance is ALREADY thin. Today the
// picture ends at 391 of a 398 strip at 96 DPI and at 590 of 594 at 144 - seven pixels
// and FOUR - and a line of the row's own small font above it is 13 pixels at 96 and 23
// at 144. One re-worded sentence in the binding row is enough to spend the whole of it.
// The failure that would follow is not cosmetic: the Zadig picture going back under the
// fold is the owner's original complaint about this screen, and it is the entire reason
// the screen exists.
//
// So the figures below are today's MEASURED clearance, used as a floor. WHERE EACH SIDE
// COMES FROM: the left is g_viewH minus g_shotBox.bottom - the layout's own strip minus
// the rectangle renderSubject() recorded while laying the page out, neither computed
// from the other; the right is a literal a human wrote down from a run. Same idiom, and
// the same ratchet direction, as allowedDeficit(): a change that gives the picture MORE
// room passes and is welcome, and whoever raises a figure here is claiming a measurement
// and has to publish it.
static int requiredShotClearance(int dpi)
{
    return (dpi >= 144) ? 4 : 7;
}

// ---------------------------------------------------------------------------
// *** HOW MUCH MORE ROOM AN OPTION'S ROW MAY HAVE THAN comctl32 ASKED FOR. ***
//
// The anti-clipping check beside this one guards the reservation from being too SMALL,
// which is the direction that cuts a sentence. This guards the other direction, which a
// review found unguarded and currently wrong by more than the screen's whole headroom: 32
// logical pixels of slack across the two options at 144 DPI against 23 pixels of room
// over the strip. Slack is not free on a screen allowedDeficit() holds at zero.
//
// THE FIGURES ARE WHAT THE PROGRAM MEASURES TODAY, after kChoicePadH came from 6 to 3.
// renderSubject() reserves the label's measured height plus 2 x S(kChoicePadH); comctl32's
// ideal is that same measured height plus its own 2. So:
//
//     slack = 2 x S(kChoicePadH) - 2
//
// *** AND THE FIRST DRAFT OF THIS FUNCTION GOT IT WRONG, WHICH IS RECORDED HERE BECAUSE
//     THIS FILE'S OWN RULE IS THAT AN ANALYTIC FIGURE DIVERGING FROM A MEASURED ONE IS A
//     FINDING. *** It said 6 at 144 DPI and the run reported 8, and this check - added in
// the same round - is what caught it. The error was the ROUNDING: S(3) at 144 DPI is
// MulDiv(3, 144, 96) = 4.5, and MulDiv rounds to nearest rather than truncating, so it is
// **5** and not 4. 2 x 5 - 2 = 8. At 96 DPI S(3) is exactly 3, so 2 x 3 - 2 = 4.
//
// The same formula reproduces the figures from BEFORE the fix and therefore the review's:
// with kChoicePadH = 6, S(6) is 6 and 9, giving 10 at 96 DPI and 16 at 144 per option -
// which is the 20 and 32 across the pair that the review measured. The model is confirmed
// at four points, not fitted to two.
//
// *** A RATCHET AND NOT A TARGET, like allowedDeficit() and requiredShotClearance()
//     above. *** The comparison is <=, so a round that tightens the reservation passes and
// is welcome; a round that loosens one has to come here, raise the number, and publish the
// measurement that justifies it. Asserting the exact figure would fail on every change
// that made things better, which trains a reader to edit a number instead of reading it.
static int allowedChoiceSlack(int dpi)
{
    return (dpi >= 144) ? 8 : 4;
}

static void measureBindingShot(int dpi)
{
    wchar_t what[400];

    RECT box;
    bool drawn = lastZadigShotBox(&box);

    // The bitmap as GDI holds it, which is the second source for the width below.
    BITMAP bm;
    ZeroMemory(&bm, sizeof(bm));
    int inBitmap = (g_shot && GetObjectW(g_shot, sizeof(bm), &bm)) ? (int)bm.bmWidth
                                                                   : -1;

    wprintf(L"  zadig shot on the binding screen: drawn=%d box=(%d,%d)-(%d,%d) "
            L"strip=%d, S(kZadigShotW)=%d, bitmap %dx%d\n", drawn ? 1 : 0,
            (int)box.left, (int)box.top, (int)box.right, (int)box.bottom, g_viewH,
            S(kZadigShotW), inBitmap, (int)bm.bmHeight);
    if (drawn)
        wprintf(L"  ...the picture clears the fold by %d; the plate around it ends at "
                L"%d of %d\n", g_viewH - (int)box.bottom,
                (int)box.bottom + S(kPicturePad), g_viewH);

    // *** ASKED AS A CHECK AND NOT AS AN EARLY RETURN. *** A guard that returned here
    // would take the two measurements below out of the run with it and every
    // remaining check would be green - a suite reporting success for work it never
    // did. The screen asks for the picture, so the only way this is false is that the
    // page got too narrow for it and dropped it in silence, which is the one thing
    // this screen does without saying so.
    _snwprintf(what, 390,
               L"%ddpi 2d-binding: the picture is DRAWN and not skipped - the page is "
               L"wide enough for it edge to edge, and a page that is not drops it "
               L"without a word", dpi);
    check(drawn, what);
    if (!drawn)
        return;

    _snwprintf(what, 390,
               L"%ddpi 2d-binding: the picture is drawn INSIDE the strip this screen "
               L"gets, so no part of it needs a scroll to reach - it ends at %d of %d, "
               L"clearing the fold by %d and the floor is %d. That is the owner's "
               L"defect, and it is why this screen has no pane",
               dpi, (int)box.bottom, g_viewH, g_viewH - (int)box.bottom,
               requiredShotClearance(dpi));
    check(g_viewH - (int)box.bottom >= requiredShotClearance(dpi), what);

    _snwprintf(what, 390,
               L"%ddpi 2d-binding: the rectangle the layout recorded and the bitmap "
               L"that was painted into it are the same width - %d in the box, %d in "
               L"the bitmap GDI holds, and S(kZadigShotW) is %d for this DPI",
               dpi, (int)(box.right - box.left), inBitmap, S(kZadigShotW));
    check(inBitmap > 0 && (int)(box.right - box.left) == inBitmap &&
          inBitmap == S(kZadigShotW), what);
}

// ===========================================================================
// *** THE TWO CHOICE CONTROLS, WHICH ARE THE ONE THING ON THEIR SCREEN THAT NO CAPTURE
//     CAN EVER SHOW. ***
//
// renderTall() and savePaneAtRest() photograph PAINTED content. A radio button is a
// child window, so it is not in either picture - which means "the choice is on the
// screen, it is above the fold, the right one is selected and the labels are the table's"
// is a set of claims that no image in this repository can support or refute. Five defects
// in this project escaped the entire harness and were caught by somebody looking at a
// picture; this is the reverse case, and it is why these controls are read back off the
// real window instead.
//
// WHERE EACH SIDE COMES FROM, and it is different for each of the four:
//   - the BOX against g_viewH: the rectangle renderSubject() recorded while measuring,
//     against the strip layout() really gave the screen. Neither is computed from the
//     other.
//   - each CONTROL against the box: GetWindowRect on the real window, mapped into page
//     coordinates, against that same recorded rectangle.
//
//     *** THIS ONE IS A REGRESSION GUARD AND NOT AN INDEPENDENT MEASUREMENT, AND THE
//         GLOBAL CONSTRAINT SAYS TO ANSWER THAT RATHER THAN LEAVE IT UNASKED. *** A review
//     found the earlier name overclaiming. renderSubject() writes g_choiceRow[i] and
//     g_choiceBox in the same block with the rows inside the box BY CONSTRUCTION, and
//     layout() passes g_choiceRow[i] straight to SetWindowPos - so on the arithmetic alone
//     this cannot fail. It is one computation compared with itself through a Win32 round
//     trip. What it really catches is worth keeping and is what the name now says: a
//     SetWindowPos that clamps or is refused, a coordinate space error between page,
//     client and screen, a later re-render that moved the box at a different width, and a
//     future layout() that stops reading the recorded rect at all - which is the case
//     INJ-7 drove, and it went red. Named for what it is: a guard on the placement path.
//   - the CHECKED one against Screen::choiceSelected: BM_GETCHECK off the real control,
//     against the field buildScreens() wrote. A window that showed a different mixer
//     from the one the run recorded is the whole failure this control can have.
//   - each LABEL against Screen::choiceLabels: GetWindowTextW off the real control,
//     against the buffer setup.cpp filled. Without it, syncChoiceButtons() could stop
//     being called and two blank radio buttons would pass everything above.
// ===========================================================================
static void measureModelChoice(int dpi)
{
    wchar_t what[500];

    RECT box;
    bool reserved = lastModelChoiceBox(&box);
    const Screen* s = &g_wiz->screens[g_screen];

    wprintf(L"  the choice on the device screen: reserved=%d box=(%d,%d)-(%d,%d) "
            L"strip=%d selected=%d\n", reserved ? 1 : 0, (int)box.left, (int)box.top,
            (int)box.right, (int)box.bottom, g_viewH, s->choiceSelected);

    // *** A CHECK AND NOT AN EARLY RETURN, for the reason measureBindingShot()'s first
    // line is one: a guard that returned would take every measurement below out of the
    // run and leave them all green.
    _snwprintf(what, 460,
               L"%ddpi 2ab-device: the renderer RESERVED room for the two controls - a "
               L"screen whose only subject is a pair of controls and which reserved "
               L"nothing would draw its bullets straight over them", dpi);
    check(reserved, what);
    if (!reserved)
        return;

    // ABOVE THE FOLD, and on this screen that is the whole of Rule 1's promise: the
    // ratchet holds this screen at deficit zero, so the page does not scroll, so a
    // control below the strip would be a control NOBODY CAN REACH - worse than one that
    // needs a scroll.
    _snwprintf(what, 460,
               L"%ddpi 2ab-device: both controls are inside the strip this screen gets, "
               L"so neither needs a scroll to reach - the box ends at %d of %d, clearing "
               L"the fold by %d. This page does not scroll, so a control below it would "
               L"be unreachable and not merely awkward",
               dpi, (int)box.bottom, g_viewH, g_viewH - (int)box.bottom);
    check((int)box.bottom <= g_viewH, what);

    int inside = 0, visible = 0, checked = 0, labelled = 0, checkedIndex = -1;
    for (int i = 0; i < 2; i++) {
        if (!g_modelBtn[i])
            continue;
        if (IsWindowVisible(g_modelBtn[i]))
            visible++;
        RECT r;
        GetWindowRect(g_modelBtn[i], &r);
        MapWindowPoints(HWND_DESKTOP, g_page, (POINT*)&r, 2);
        if (r.left >= box.left && r.right <= box.right && r.top >= box.top &&
            r.bottom <= box.bottom)
            inside++;
        wprintf(L"  ...option %d at (%d,%d)-(%d,%d)\n", i, (int)r.left, (int)r.top,
                (int)r.right, (int)r.bottom);
        if (SendMessageW(g_modelBtn[i], BM_GETCHECK, 0, 0) == BST_CHECKED) {
            checked++;
            checkedIndex = i;
        }
        wchar_t got[256];
        got[0] = 0;
        GetWindowTextW(g_modelBtn[i], got, 255);
        got[255] = 0;
        if (s->choiceLabels[i] && wcscmp(got, s->choiceLabels[i]) == 0)
            labelled++;
    }

    _snwprintf(what, 460,
               L"%ddpi 2ab-device: both controls really exist on the page and are shown "
               L"(%d of 2 visible)", dpi, visible);
    check(visible == 2, what);

    // NAME REWRITTEN: it used to read "the space measured and the space used are the same
    // space", which claimed two independent derivations. There are not two - see the block
    // over this function. It is a guard on the placement PATH, and the name says so now.
    _snwprintf(what, 460,
               L"%ddpi 2ab-device: both controls really SIT where the renderer recorded, "
               L"read back through Win32 (%d of 2) - a guard on the placement path, not two "
               L"independent measurements: the rect is written and used in one chain", dpi,
               inside);
    check(inside == 2, what);

    // *** EXACTLY ONE, AND THE COUNT IS THE POINT. *** WS_GROUP on the first control is
    // the only thing making the pair one radio group; without it BS_AUTORADIOBUTTON
    // clears nothing and both can be on at once - a window saying this machine is two
    // different mixers at the same time. Zero is the other failure: no selection at all
    // on the screen whose default is the one model anybody has proven.
    _snwprintf(what, 460,
               L"%ddpi 2ab-device: exactly ONE option is selected (%d of 2), which is "
               L"what WS_GROUP on the first control buys - without it both could be on "
               L"and the window would name two mixers at once", dpi, checked);
    check(checked == 1, what);

    _snwprintf(what, 460,
               L"%ddpi 2ab-device: the option the WINDOW shows selected is the one the "
               L"TABLE recorded - control %d, table %d", dpi, checkedIndex,
               s->choiceSelected);
    check(checkedIndex == s->choiceSelected, what);

    // ===================================================================
    // *** AND THE SAME QUESTION AT A NON-ZERO SELECTION, WHICH IS THE HALF THE CHECK
    //     ABOVE CANNOT ANSWER. ***
    //
    // Every flow this harness builds carries a ZEROED Run, so `table` above is always 0 and
    // `control` is always 0, and the comparison is 0 against 0. A review proved what that
    // costs: replacing syncChoiceButtons()'s `s->choiceSelected == i` with `i == 0` - the
    // window ignoring the table entirely - failed NOTHING. The round trip was closed for
    // buildScreens() in the text suite by giving it a Run that chose the other model; this
    // is the same hole one level down, in the window.
    //
    // So the table is moved to the OTHER option, the product's own syncChoiceButtons() is
    // asked to copy it down, and the real control is read back - then everything is put
    // back and re-synced, because the captures below must be of the default state and not
    // of this measurement's fixture.
    //
    // WHERE EACH SIDE COMES FROM: the left is BM_GETCHECK off the real BUTTON window; the
    // right is the value this block just wrote into the table. The product's copy function
    // is the only thing between them, which is exactly the thing under test.
    // ===================================================================
    {
        Screen* self  = &g_wiz->screens[g_screen];
        int     keep  = self->choiceSelected;
        int     other = (keep == 0) ? 1 : 0;

        self->choiceSelected = other;
        syncChoiceButtons();
        int nowChecked = -1, nowCount = 0;
        for (int i = 0; i < 2; i++) {
            if (g_modelBtn[i] &&
                SendMessageW(g_modelBtn[i], BM_GETCHECK, 0, 0) == BST_CHECKED) {
                nowChecked = i;
                nowCount++;
            }
        }

        // Put it back BEFORE the check, so a failure cannot leave the window holding this
        // block's fixture for every capture and check after it.
        self->choiceSelected = keep;
        syncChoiceButtons();

        _snwprintf(what, 460,
                   L"%ddpi 2ab-device: ...and it follows the table to the OTHER option too "
                   L"- moved to %d, the window showed %d (%d checked), and it was put back "
                   L"to %d. Without this the check above compares 0 with 0 and a window "
                   L"that hard-coded option 0 would pass",
                   dpi, other, nowChecked, nowCount, self->choiceSelected);
        check(nowChecked == other && nowCount == 1 && self->choiceSelected == keep, what);
    }

    _snwprintf(what, 460,
               L"%ddpi 2ab-device: both controls carry the TABLE's words, read back out "
               L"of the real windows (%d of 2) - two blank radio buttons would pass every "
               L"geometry check above", dpi, labelled);
    check(labelled == 2, what);

    // ===================================================================
    // *** AND THE ONE THING NONE OF THE ABOVE CAN SEE: IS THE LABEL CLIPPED. ***
    //
    // This is the check that was missing and the reasoning is worth writing down,
    // because the risk is structural and not hypothetical. renderSubject() reserves each
    // option's height by measuring its label WRAPPED at avail - S(kChoiceIndW), using
    // DrawTextW. The control is then placed at the FULL avail width and wraps the text
    // ITSELF, inside its client rectangle minus whatever room BS_AUTORADIOBUTTON gives
    // its own round indicator. Those are two different wrapping calculations by two
    // different pieces of code, and kChoiceIndW is this program's GUESS at the second
    // one. If the control's text area is narrower than the width that was measured, the
    // label wraps to more lines than were reserved and the last line is CLIPPED - on the
    // one screen whose subject is the sentence saying what a BCD2000 will not do.
    //
    // No capture can show it either: a child control is not painted content, so it is in
    // neither the at-rest picture nor the tall render. That is what makes this the one
    // measurement standing between a clipped sentence and a shipped installer.
    //
    // WHERE EACH SIDE COMES FROM: BCM_GETIDEALSIZE is the CONTROL's own answer about how
    // tall it needs to be for the text it is holding at the width it has - computed by
    // comctl32, which is also what draws it - and the other side is the rectangle
    // renderSubject() reserved. Nothing in this program computes the first, which is
    // exactly why it is worth asking.
    // ===================================================================
    int roomy = 0, asked = 0, slackMax = 0;
    for (int i = 0; i < 2; i++) {
        if (!g_modelBtn[i])
            continue;
        RECT r;
        GetWindowRect(g_modelBtn[i], &r);
        SIZE ideal;
        ideal.cx = (LONG)(r.right - r.left);   // the width it really has
        ideal.cy = 0;
        if (!SendMessageW(g_modelBtn[i], BCM_GETIDEALSIZE, 0, (LPARAM)&ideal))
            continue;
        asked++;
        int have = (int)(r.bottom - r.top);
        // BOTH options are printed, not just the first. The earlier round published only
        // option 0's figures, and option 1 is the one carrying the long sentence the
        // anti-clipping check exists to protect - so the one that mattered was the one
        // left out of the report.
        wprintf(L"  ...option %d wants %dx%d at width %d, has %d (slack %d)\n", i,
                (int)ideal.cx, (int)ideal.cy, (int)(r.right - r.left), have,
                have - (int)ideal.cy);
        if (have >= (int)ideal.cy)
            roomy++;
        if (have - (int)ideal.cy > slackMax)
            slackMax = have - (int)ideal.cy;
    }
    _snwprintf(what, 460,
               L"%ddpi 2ab-device: neither label is CLIPPED - each control was asked, "
               L"through BCM_GETIDEALSIZE, how tall it needs to be for its own text at "
               L"its own width, and %d of %d got at least that much. The reserved height "
               L"is measured by renderSubject() and the wrapping is done by comctl32, so "
               L"these are two calculations that can disagree",
               dpi, roomy, asked);
    check(asked == 2 && roomy == 2, what);

    // *** AND THE SAME NUMBER BOUNDED FROM ABOVE, WHICH IS THE HALF THAT WAS MISSING. ***
    //
    // The check above can only fail when the reservation is too SMALL. Too LARGE cannot
    // fail it, and too large is not free: it spends strip on a screen Rule 1 holds at
    // deficit zero. A review measured 32 logical pixels of it at 144 DPI against 23 pixels
    // of remaining headroom - more slack than headroom - and nothing in the suite could
    // have said so.
    //
    // A CEILING AND NOT AN EQUALITY, for the reason allowedDeficit() and
    // requiredShotClearance() are ratchets: comctl32 adds its own 2 pixels and a future
    // font could move that, so demanding an exact figure would fail on a change that made
    // things better. The comparison is <=, so a round that tightens the reservation passes
    // and a round that loosens it has to come here and argue for a bigger number.
    _snwprintf(what, 460,
               L"%ddpi 2ab-device: and neither label's row is over-reserved - the most any "
               L"option has beyond what comctl32 asked for is %d, at most %d allowed. Too "
               L"much is not free: it spends the strip of a screen Rule 1 holds at zero",
               dpi, slackMax, allowedChoiceSlack(dpi));
    check(asked == 2 && slackMax <= allowedChoiceSlack(dpi), what);
}

// ===========================================================================
// *** THE MODEL ROW AS IT REACHES THE PAGE, WHICH IS THE ONE QUESTION FOUR ROUNDS OF
//     THIS GUARD NEVER ASKED. ***
//
// The re-review of round 3 named the root of a three round failure in one sentence:
// "every assertion reads the struct, never the page". Every guard on this row - the
// substring ones that three rounds added and the exact comparison that replaced them -
// asserts what describeModel() wrote into a bcdgui::Row. Nothing anywhere connected that
// struct to what drawRow() paints, so every defect living in the gap between the two was
// invisible by construction. The cleanest example is one string literal: emptying
// Row::title makes gui.cpp:1509 draw no row at all - the mark, the heading and the
// design's required sentence all leave the shipped screen - and it measured 841 checks, 0
// failures, VERIFY_OK.
//
// *** WHAT THIS DOES: THE SAME COMPARISON, IN PIXELS. *** For each of the four machines,
// the screen is rendered twice into an off-screen surface at the DPI under test - once
// from the Row the product's own buildScreens() produced, and once from the Row
// expectedModelRow() composes in installer\verify - and the two surfaces are required to
// be identical byte for byte. A false heading, an emptied heading, a swapped noun, a
// negated verb, an appended clause, a wrong mark and a branch that stops being reached are
// all the same failure here: the page is not the page.
//
// *** AND WHY THAT IS NOT ENOUGH ON ITS OWN, WHICH IS THE INJECTION THIS ROUND INVENTED.
//     *** Both renders go through the same renderer, so a renderer that stopped painting a
// field would change BOTH sides and the comparison would stay green. That is not
// hypothetical: deleting the `drawText(... row->detail ...)` line inside gui.cpp's
// drawRow() removes the sentence from every screen in the program, and every struct-side
// check in this repository stays green because none of them looks at a pixel. Nobody had
// injected into the PAINTER before - the four rounds and their re-reviews all mutated
// setup.cpp, which is the data.
//
// So each of the three fields is PERTURBED and the page is required to change. Empty the
// heading and the page must differ; change the heading and it must differ; change the
// sentence and it must differ; change the mark and it must differ. Four renders, four
// negative controls, and together with the comparison above they say: the page shows
// exactly this row, and the page really shows all three of its fields.
//
// WHERE EACH SIDE COMES FROM. The left is a real 32 bit surface the product's own renderer
// drew into, with measure false, from a Row the product's own buildScreens() produced. The
// right is the same renderer over a Row this file composed from the machine and the
// selection - two different Rows, from two different files, and no measuring pass on
// either side. The perturbations are strings written here, which setup.cpp cannot see.
// ===========================================================================

// One real render of whatever screen the window is on, into a buffer the caller frees.
// measure false and no child controls: this is the paint, not the measuring pass.
static BYTE* renderPageBits(int pw, int ph, SIZE_T* bytes)
{
    *bytes = (SIZE_T)pw * ph * 4;
    BYTE* px = (BYTE*)HeapAlloc(GetProcessHeap(), 0, *bytes);
    if (!px)
        return 0;
    HDC     screen = GetDC(0);
    HDC     mem    = CreateCompatibleDC(screen);
    HBITMAP bmp    = CreateCompatibleBitmap(screen, pw, ph);
    HGDIOBJ ob     = SelectObject(mem, bmp);
    fillRect(mem, 0, 0, pw, ph, CLR_PAGE_BG);
    SetBkMode(mem, TRANSPARENT);
    // The product's own dispatch, exactly as savePaneAtRest() does it, so this is the
    // page the window really draws and not one this file assembled.
    if (kindAt(g_screen) == kScreenInfo)
        renderInfoScreen(mem, pw, ph, false);
    else
        renderCheckScreen(mem, pw, ph, false);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = pw;
    bi.bmiHeader.biHeight      = -ph;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    GetDIBits(mem, bmp, 0, (UINT)ph, px, &bi, DIB_RGB_COLORS);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(0, screen);
    return px;
}

// Where two surfaces first differ, as a count of differing pixels, or 0 for identical.
// A count rather than a boolean because the number is what a reader needs in order to
// tell "the heading changed" from "the whole row moved".
static int pixelsDiffering(const BYTE* a, const BYTE* b, SIZE_T bytes)
{
    if (!a || !b)
        return -1;
    int n = 0;
    for (SIZE_T i = 0; i + 3 < bytes; i += 4)
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2])
            n++;
    return n;
}

// How many pixels of this surface are not the page's background.
//
// *** THIS IS HERE BECAUSE pixelsDiffering() IS THE WRONG INSTRUMENT FOR "IS THIS FIELD
//     PAINTED", AND MY OWN INJECTION IS WHAT MEASURED THAT. *** The first version of the
// sensitivity checks below replaced the row's sentence with a shorter one and required
// pixels to move. They moved - 24716 of them - on a build where drawRow() had been changed
// to reserve the sentence's height and DRAW NOTHING, because the shorter replacement
// re-wrapped and everything under it slid up. The check was measuring LAYOUT and reading
// like it measured ink.
//
// A count of ink is invariant under translation: sliding the bullets up moves pixels
// without creating or destroying any. So "empty this field and the page loses ink" cannot
// be satisfied by a re-layout, and it is exactly the question - does this field put marks
// on the paper.
static int inkPixels(const BYTE* px, SIZE_T bytes)
{
    if (!px)
        return -1;
    // Through a variable and not GetRValue(CLR_PAGE_BG) directly: the macro is a cast to
    // BYTE and the constant does not fit one, which is C4310 under /W4 /WX. inkOfRegion()
    // above takes its background as a parameter and never met this.
    COLORREF bg = CLR_PAGE_BG;
    BYTE b = GetBValue(bg), g = GetGValue(bg), r = GetRValue(bg);
    int n = 0;
    for (SIZE_T i = 0; i + 3 < bytes; i += 4)
        if (px[i] != b || px[i + 1] != g || px[i + 2] != r)
            n++;
    return n;
}

// ---------------------------------------------------------------------------
// *** HOW MUCH INK A FIELD OF THIS ROW MUST BE WORTH, BY DPI. ***
//
// A ratchet in the shape requiredShotClearance(), requiredAddressClearance() and
// allowedDeficit() already use here: the figure is a floor under what the program paints
// TODAY, so a change that paints more passes and whoever lowers one is claiming a
// measurement and has to publish it.
//
// It is not zero, and the reason is antialiasing rather than caution. Emptying a field
// shortens the row, which slides everything under it up by a few pixels, and a glyph
// rendered at a different subpixel offset does not cover exactly the same number of
// pixels. So a pure translation moves the ink count by a small amount in either direction,
// and a floor of 1 would be satisfied by that alone.
//
// MEASURED ON THIS RUN, on the device screen: emptying the row's SENTENCE costs 4692 ink
// pixels at 96 DPI and 7674 at 144; emptying its HEADING takes the whole row off the page
// and costs 5241 and 8609. On the build where drawRow() reserved the sentence's height and
// painted nothing, emptying the sentence cost EXACTLY 0 pixels at both DPIs - the ink count
// really is invariant under the slide, and that build is what this floor is set against.
// The figures here are 200 and 350, roughly six characters of the row's own small font: an
// order of magnitude under the signal, and well clear of the few pixels a glyph landing on
// a different subpixel offset could cost on a machine whose font rendering is not this
// one's. One line of that font is 13 logical pixels at 96 DPI and 23 at 144, so no
// arrangement of this row that still paints a sentence comes near the floor.
static int requiredRowInk(int dpi)
{
    return (dpi >= 144) ? 350 : 200;
}

static void measureModelRowOnThePage(int dpi, int page)
{
    wchar_t what[460];
    RECT    c;
    GetClientRect(g_page, &c);
    int    pw = c.right;
    int    ph = c.bottom;
    SIZE_T bytes = 0;

    Screen* self = &g_wiz->screens[page];
    Row     keep = self->row;          // put back, and the restore is asserted below

    struct RowCase {
        bool           present;
        int            selected;
        const wchar_t* label;
    };
    static const RowCase rows[4] = {
        { true,  bcdsetup::kModelBcd3000, L"detected, and that model chosen" },
        { true,  bcdsetup::kModelBcd2000, L"detected, and the OTHER model chosen" },
        { false, bcdsetup::kModelBcd3000, L"nothing detected, default kept" },
        { false, bcdsetup::kModelBcd2000, L"nothing detected, other model chosen" }
    };

    // One Wizard on the heap, reused - the same answer to the stack objection the
    // struct-side loop in testDeviceScreen() gives.
    Wizard* w = (Wizard*)HeapAlloc(GetProcessHeap(), 0, sizeof(Wizard));
    _snwprintf(what, 450,
               L"%ddpi 2ab-device: the four branch machines have a wizard to be built "
               L"into, so the pixels below come through buildScreens() and not around it",
               dpi);
    what[450] = 0;
    check(w != 0, what);

    for (int i = 0; w && i < 4; i++) {
        ::Run r;
        ZeroMemory(&r, sizeof(r));
        ::fakeState(&r.state);
        r.state.usb.interfacePresentNow = rows[i].present;
        r.selectedModel                 = rows[i].selected;

        ZeroMemory(w, sizeof(*w));
        w->user = &r;
        bcdsetup::buildScreens(w, &r.state);
        int k = -1;
        for (int j = 0; j < screenCount(w); j++)
            if (w->screens[j].title == ::kDeviceTitle)
                k = j;
        if (k < 0)
            continue;

        // *** THE PRIVACY GATE, BECAUSE THIS FUNCTION WRITES NEW ROW TEXT INTO A TABLE
        //     THE CAPTURES ARE TAKEN FROM. *** checkNoLiveIdentity() runs once, before
        // both DPI passes; anything written into a paintable field after it is ungated,
        // which is the exact history that gate exists for. These rows are invented from
        // fakeState() and cannot carry this machine, and that is measured rather than
        // asserted - the gate's own needles, the way measureShotOverReadings() does it.
        ::scanForIdentity(w->screens[k].row.title,  L"a model row this measurement wrote");
        ::scanForIdentity(w->screens[k].row.detail, L"a model row this measurement wrote");

        // --- the page the product's row paints ---
        self->row = w->screens[k].row;
        BYTE* got = renderPageBits(pw, ph, &bytes);

        // --- the page the row THIS FILE composes paints ---
        ::ExpectedRow want;
        ::expectedModelRow(&r.state, r.selectedModel, &want);
        ZeroMemory(&self->row, sizeof(self->row));
        self->row.state = want.mark;
        wcsncpy(self->row.title,  want.title,  kRowTitle - 1);
        wcsncpy(self->row.detail, want.detail, kRowText - 1);
        BYTE* exp = renderPageBits(pw, ph, &bytes);

        int diff = pixelsDiffering(got, exp, bytes);
        wprintf(L"  %ddpi 2ab-device [%s]: %d pixels differ between the painted row and "
                L"the composed one\n", dpi, rows[i].label, diff);
        _snwprintf(what, 450,
                   L"%ddpi 2ab-device [%s]: the page the TABLE's row paints is the page "
                   L"the row this file composes paints - %d of %d pixels differ. This is "
                   L"the whole chain ending at the pixel: run, table, describeModel, row, "
                   L"drawRow",
                   dpi, rows[i].label, diff, (int)(bytes / 4));
        what[450] = 0;
        check(got != 0 && exp != 0 && diff == 0, what);

        if (got)
            HeapFree(GetProcessHeap(), 0, got);
        if (exp)
            HeapFree(GetProcessHeap(), 0, exp);
    }
    if (w)
        HeapFree(GetProcessHeap(), 0, w);

    // ===================================================================
    // *** AND THE PAGE REALLY READS ALL THREE FIELDS, WHICH THE COMPARISON ABOVE CANNOT
    //     SAY. *** Both of its renders go through one renderer, so a renderer that
    // ignored a field would move both sides together and stay green - the "declared and
    // unread" shape three fields of this table have already had, and the shape of the
    // injection this round invented (deleting the sentence from drawRow() itself).
    //
    // Four perturbations, four renders, each against the screen's own live row. WHERE
    // EACH SIDE COMES FROM: both are real surfaces the product's renderer drew, one from
    // the table as it stands and one from the table with one field changed to a string
    // written here. Nothing is measured, nothing is reported by the renderer, and no
    // field is read back.
    // ===================================================================
    {
        self->row = keep;
        BYTE* base    = renderPageBits(pw, ph, &bytes);
        int   baseInk = inkPixels(base, bytes);

        // --- 1. the SENTENCE puts ink on the paper ---
        self->row = keep;
        ZeroMemory(self->row.detail, sizeof(self->row.detail));
        BYTE* noDetail  = renderPageBits(pw, ph, &bytes);
        int   inkDetail = baseInk - inkPixels(noDetail, bytes);
        wprintf(L"  %ddpi 2ab-device: page ink %d; the sentence is worth %d of it, the "
                L"heading %s\n", dpi, baseInk, inkDetail, L"below");
        _snwprintf(what, 450,
                   L"%ddpi 2ab-device: the row's SENTENCE is really PAINTED - emptying it "
                   L"takes %d ink pixels off a page of %d, and the floor is %d. Ink and "
                   L"not moved pixels: a re-layout slides ink about without destroying "
                   L"any, and a renderer that reserved the height and drew nothing passed "
                   L"the moved-pixel form of this check",
                   dpi, inkDetail, baseInk, requiredRowInk(dpi));
        what[450] = 0;
        check(base != 0 && noDetail != 0 && inkDetail >= requiredRowInk(dpi), what);
        if (noDetail)
            HeapFree(GetProcessHeap(), 0, noDetail);

        // --- 2. the HEADING puts ink on the paper, and it takes the row with it ---
        self->row = keep;
        ZeroMemory(self->row.title, sizeof(self->row.title));
        BYTE* noTitle  = renderPageBits(pw, ph, &bytes);
        int   inkTitle = baseInk - inkPixels(noTitle, bytes);
        _snwprintf(what, 450,
                   L"%ddpi 2ab-device: ...and an EMPTIED heading takes the WHOLE row off "
                   L"the page - %d ink pixels of %d go, floor %d. gui.cpp draws no row at "
                   L"all when the heading is blank, which is how the design's required "
                   L"sentence left a shipped screen with everything green",
                   dpi, inkTitle, baseInk, requiredRowInk(dpi));
        what[450] = 0;
        check(base != 0 && noTitle != 0 && inkTitle >= requiredRowInk(dpi) &&
              inkTitle > inkDetail, what);
        if (noTitle)
            HeapFree(GetProcessHeap(), 0, noTitle);

        // --- 3. a CHANGED heading is painted, not merely measured ---
        //
        // Moved pixels and not ink here, and the difference is that this perturbation
        // does not re-lay-out anything: the heading is one line before and after, so the
        // row keeps its height and nothing under it moves. Every pixel that differs is a
        // glyph. An ink COUNT would be the wrong instrument for the opposite reason -
        // two different words can carry the same amount of ink by coincidence.
        self->row = keep;
        ZeroMemory(self->row.title, sizeof(self->row.title));
        wcsncpy(self->row.title, L"Not this", kRowTitle - 1);
        BYTE* otherTitle = renderPageBits(pw, ph, &bytes);
        int   dTitle     = pixelsDiffering(base, otherTitle, bytes);
        _snwprintf(what, 450,
                   L"%ddpi 2ab-device: ...and a CHANGED heading changes the paper - %d of "
                   L"%d pixels, with the row the same height so none of them is anything "
                   L"moving. A heading the renderer measured and did not draw would leave "
                   L"this at 0",
                   dpi, dTitle, (int)(bytes / 4));
        what[450] = 0;
        check(base != 0 && otherTitle != 0 && dTitle > 0, what);
        if (otherTitle)
            HeapFree(GetProcessHeap(), 0, otherTitle);

        // --- 4. the MARK is painted ---
        //
        // The same reasoning: a mark is a fixed-size square, so changing its state moves
        // no layout at all and every differing pixel is the mark itself.
        self->row       = keep;
        self->row.state = (keep.state == kRowOk) ? kRowWarn : kRowOk;
        BYTE* otherMark = renderPageBits(pw, ph, &bytes);
        int   dMark     = pixelsDiffering(base, otherMark, bytes);
        _snwprintf(what, 450,
                   L"%ddpi 2ab-device: ...and the MARK is painted - %d of %d pixels change "
                   L"when the state does, and the mark is a fixed square so nothing has "
                   L"moved. This is the field that says whether a screen contradicting "
                   L"itself is wearing a pass",
                   dpi, dMark, (int)(bytes / 4));
        what[450] = 0;
        check(base != 0 && otherMark != 0 && dMark > 0, what);
        if (otherMark)
            HeapFree(GetProcessHeap(), 0, otherMark);

        if (base)
            HeapFree(GetProcessHeap(), 0, base);
    }

    // *** AND THE TABLE IS PUT BACK, ASSERTED AND NOT ASSUMED. *** This function writes
    // twelve different rows into the screen the captures are taken from, so a restore that
    // silently did not happen would put an invented machine's sentence - or a heading
    // reading "Not the same" - into a committed PNG. The same lesson
    // measureShotOverReadings() records: an instrument able to alter what it measures
    // without anything going red is not a cosmetic problem.
    //
    // *** IT IS NOT `self->row == keep`, WHICH WOULD BE ONE MORE INSTANCE OF THE RULE THIS
    //     PROJECT KEEPS BREAKING. *** Those two sides are the assignment one line up and
    // the variable it was assigned from: one source, and no failing state exists. So the
    // question asked is the one the leak would answer wrongly - does the restored table
    // still say what this machine says?
    //
    // WHERE EACH SIDE COMES FROM: the left is describeModel() run fresh over the
    // MachineState this whole run has been using, by the same product function
    // buildScreens() calls; the right is the row that came back out of a copy taken before
    // any of this ran. Two storages, two writers.
    self->row = keep;
    {
        Row fresh;
        ZeroMemory(&fresh, sizeof(fresh));
        bcdsetup::MachineState* st = g_wiz->user ? &((::Run*)g_wiz->user)->state : 0;
        bool same = false;
        if (st) {
            bcdsetup::describeModel(st, self->choiceSelected, &fresh);
            same = wcscmp(fresh.title, self->row.title) == 0 &&
                   wcscmp(fresh.detail, self->row.detail) == 0 &&
                   fresh.state == self->row.state;
        }
        _snwprintf(what, 450,
                   L"%ddpi 2ab-device: the screen this instrument borrowed is put back - "
                   L"the machine this run has been using still describes the row the table "
                   L"carries, heading and mark and all, before any capture is taken from "
                   L"it (state %s)", dpi, st ? L"present" : L"MISSING");
        what[450] = 0;
        check(st != 0 && same, what);
    }
}

// ===========================================================================
// *** THE PICTURE, ON EVERY MACHINE THIS SCREEN CAN DESCRIBE - NOT ONLY ON THE ONE
//     THE CAPTURES HAPPEN TO BE OF. ***
//
// measureBindingShot() above asks the question once, of whatever machine shotState()
// invented, and that machine is `enumKeyPresent, !guidPresent, other=-1, !present` -
// case 2, whose row is ONE line. Four of the five readings this screen can paint were
// never rendered, and `sc[4].showZadigShot = true` is unconditional, so all five get
// the picture.
//
// *** TWO OF THE FIVE PUT THE PICTURE UNDER THE FOLD, AND THE SUITE RENDERED THE ONE
//     THAT DOES NOT. *** The row is drawn ABOVE the picture and the picture clears the
// fold by 7 logical pixels at 96 DPI and 4 at 144. A line of g_fSmall is 13 px at 96
// and 23 at 144. So a row whose detail wraps to a SECOND line puts the last rows of
// the picture behind a scroll, on the one screen whose entire reason for existing is
// that they are not - the owner's original complaint, surviving in the states nobody
// looked at.
//
// The worst of the two is "Zadig was run on the wrong interface": that user has
// already run Zadig once, is being told to run it again on the other line, and is
// precisely the person who most needs to compare the picture with what is on screen.
//
// *** AND UNTIL THIS ROUND'S OWN FINDING 5 IT COULD NOT HAVE BEEN SEEN AT 96 DPI. ***
// The hybrid box reported a bottom S(kPicturePad) too high, so a case-1 machine would
// have read "396 of 398" and stayed green while the picture really ended at 404. The
// instrument was built and not pointed at the cases; this points it.
//
// HOW IT DRIVES THEM: it writes the four usb fields of the MachineState the window was
// built from and calls the flow's OWN refreshScreens() - rebuildScreens() in setup.cpp,
// which is the function the product runs on the window thread after every re-check -
// then setScreen(), which re-lays out and re-measures. So each reading is rendered by
// exactly the path a real re-check would take, and nothing here rebuilds a table of its
// own.
//
// WHERE EACH SIDE COMES FROM: the left is g_shotBox, recorded by renderSubject() while
// laying the page out for that reading; the right is g_viewH, the layout's own strip.
// Neither is computed from the other and neither is a literal here.
//
// IT RUNS LAST IN THE SCREEN'S BLOCK and restores the state, the table and the screen
// when it is done - so the tracked PNGs stay pictures of the one invented machine
// shotState() describes, and the checks further down this pass read a window that is
// back where they left it.
//
// *** "AFTER THE CAPTURES" IS WHAT THIS USED TO SAY, AND IT IS NOT TRUE OF ALL OF
//     THEM. *** The at-rest picture of this screen really is taken before this runs.
// The TALL render is not: renderTall() runs unconditionally further down the same loop
// iteration, after the kBindingTitle branch has closed. Measured - delete the copy-back
// below and page-2d-binding-96dpi.png changes while page-2d-binding-at-rest-96dpi.png
// does not, and page-2a-mixer-144dpi.png changes too. What keeps the captures honest is
// the RESTORE and not the position of this call, and a reader who trusted the ordering
// would draw the boundary in the wrong place.
//
// *** THE TABLE IS RESTORED BY COPY AND NOT BY REBUILDING IT, AND THAT WAS MEASURED
//     RATHER THAN CHOSEN. *** The first version restored the four usb fields and
// called refreshScreens() again, on the assumption that the table is a pure function
// of the MachineState. It is not, at this point in the run: the re-check suite earlier
// in the pass mutates that state and hand-builds parts of the table, so rebuilding
// from it produced a DIFFERENT table from the one this function found - and two
// flow-wide checks at 144 DPI went red, reporting `refused on 0 screen(s)` and
// `the table offers on 1 ('(none)')`. A restore that reconstructs instead of putting
// back is a restore that is only correct while an assumption holds. This copies the
// entries out and copies them back, which is correct whatever built them.
// ===========================================================================
struct ShotReading {
    bool           enumKey;
    bool           guid;
    int            other;
    bool           present;
    const wchar_t* who;
};

static void measureShotOverReadings(int dpi, int page)
{
    static const ShotReading readings[5] = {
        { false, false, -1, false, L"never seen"         },
        { true,  false,  1, false, L"wrong interface"    },
        { true,  false, -1, false, L"not applied"        },
        { true,  true,  -1, false, L"applied, no answer" },
        { true,  true,  -1, true,  L"applied, confirmed" }
    };
    const int kReadings = 5;

    if (!g_wiz || !g_wiz->refreshScreens || !g_wiz->user)
        return;
    bcdsetup::MachineState* st = &((::Run*)g_wiz->user)->state;

    bool keptEnum  = st->usb.enumKeyPresent;
    bool keptGuid  = st->usb.guidPresent;
    int  keptOther = st->usb.guidOnOtherFunction;
    bool keptNow   = st->usb.interfacePresentNow;
    Screen keptScreens[kMaxScreens];
    memcpy(keptScreens, g_wiz->screens, sizeof(keptScreens));

    for (int i = 0; i < kReadings; i++) {
        st->usb.enumKeyPresent      = readings[i].enumKey;
        st->usb.guidPresent         = readings[i].guid;
        st->usb.guidOnOtherFunction = readings[i].other;
        st->usb.interfacePresentNow = readings[i].present;
        g_wiz->refreshScreens(g_wiz, g_wiz->user);
        setScreen(page);

        RECT box;
        ZeroMemory(&box, sizeof(box));
        bool drawn = lastZadigShotBox(&box);
        // The picture's TOP is what the row's height moves, so it is printed beside
        // the verdict: two readings that differ only in where the picture starts
        // differ only in how many lines their row took.
        wprintf(L"  reading %d (%s): picture %d..%d of %d\n", i, readings[i].who,
                (int)box.top, (int)box.bottom, g_viewH);

        // The floor and not the sign, for the reason requiredShotClearance() gives: the
        // clearance is 7 pixels at 96 DPI and 4 at 144, one line of the row's own font is
        // 13 and 23, and a boolean here would let it erode to 1 with this suite green.
        // This is the site where that matters most - the row's height is exactly what
        // moves the picture, and these five readings are the five heights it can have.
        wchar_t what[400];
        _snwprintf(what, 380,
                   L"%ddpi 2d-binding: the picture is above the fold on the \"%s\" "
                   L"machine too - it ends at %d of %d, clearing the fold by %d and the "
                   L"floor is %d",
                   dpi, readings[i].who, (int)box.bottom, g_viewH,
                   g_viewH - (int)box.bottom, requiredShotClearance(dpi));
        what[380] = 0;
        check(drawn && g_viewH - (int)box.bottom >= requiredShotClearance(dpi), what);

        // *** AND THE PRIVACY GATE DOES NOT COVER THESE ROWS, BECAUSE IT HAS ALREADY
        //     FINISHED. *** checkNoLiveIdentity() runs ONCE, before both DPI passes,
        // on purpose - a gate that runs after the PNGs are written has already let
        // them be written. This function then writes five NEW row texts into the
        // table, after it. Nothing reaches a capture today, so this is coverage and not
        // a leak - but "content that reaches a paintable field ungated" is the exact
        // history this file's gate exists for, and the five strings are free to scan.
        //
        // *** AND WHAT KEEPS THEM OUT IS THE COPY-BACK, NOT THE ORDERING. *** This
        // block used to say "the two binding pictures are taken before this runs". That
        // is false for one of them and it was measured false: delete the table
        // copy-back and page-2d-binding-96dpi.png changes, because the tall render is
        // renderTall(), which runs unconditionally after the kBindingTitle branch has
        // closed, in the same loop iteration. Only the at-rest picture is genuinely
        // upstream of this function. So the boundary a future reader should draw is the
        // restore, and page-2a-mixer-144dpi.png changing under the same deletion shows
        // it is load bearing for screens that are not this one either.
        //
        // It uses the gate's OWN scanForIdentity() and the needles it already
        // collected, rather than a second copy of the rule. collectLiveIdentity() is
        // deliberately NOT called again: addNeedle() does not de-duplicate and its
        // table holds six.
        ::scanForIdentity(g_wiz->screens[page].row.title,
                          L"a binding row this measurement wrote");
        ::scanForIdentity(g_wiz->screens[page].row.detail,
                          L"a binding row this measurement wrote");
    }

    // The verdict on those ten strings, once, with the denominator the gate itself
    // publishes. g_needleN > 0 is in the conjunct because a scan with no needles
    // reports the same clean pass as a scan that found nothing.
    {
        wchar_t what[400];
        _snwprintf(what, 380,
                   L"%ddpi 2d-binding: none of the %d rows this measurement wrote "
                   L"names this machine - %d strings looked for%s%s", dpi,
                   kReadings * 2, ::g_needleN, ::g_hitWhat ? L", found: " : L"",
                   ::g_hitWhat ? ::g_hitWhat : L"");
        what[380] = 0;
        check(::g_needleN > 0 && ::g_hitWhat == 0, what);
    }

    st->usb.enumKeyPresent      = keptEnum;
    st->usb.guidPresent         = keptGuid;
    st->usb.guidOnOtherFunction = keptOther;
    st->usb.interfacePresentNow = keptNow;
    memcpy(g_wiz->screens, keptScreens, sizeof(keptScreens));
    setScreen(page);

    // ===================================================================
    // *** AND THE RESTORE IS ASSERTED, BECAUSE HALF OF IT WAS NOT. ***
    //
    // The table copy-back above is guarded: delete it and five checks go red. The four
    // MachineState fields were not - delete those four lines and the suite stays at
    // 0 failures with every capture byte-identical, while the state is left carrying
    // reading 4's "applied, confirmed". The leak travels: runWizard() drives
    // shootAtDpi(wiz, 96) and then shootAtDpi(wiz, 144) over ONE Wizard and ONE Run,
    // so the 96 DPI pass hands it to the 144 DPI pass. It is inert only because
    // nothing after this point re-derives the table from the state; the day anything
    // does, the binding screen paints a green "Applied and confirmed on the device
    // that is connected now" on a machine with no binding, and the committed captures
    // change with nothing red. A measuring instrument able to alter what it measures
    // without anything going red is not a cosmetic problem.
    //
    // *** IT IS NOT `st->usb.x == keptX`, WHICH WOULD BE THE SEVENTH INSTANCE OF THE
    //     RULE THIS PROJECT KEEPS BREAKING. *** Those two sides are the assignment
    // three lines up and the variable it was assigned from: one source, no failing
    // state. So the check asks the question the leak would actually answer wrongly -
    // does the restored STATE still describe the restored TABLE?
    //
    // WHERE EACH SIDE COMES FROM: the left is describeBinding() run fresh over the
    // MachineState this function put back, by the same product function buildScreens()
    // uses; the right is the row text that came back through the memcpy, out of a
    // snapshot taken before any of this. Two storages, two writers, and they can only
    // agree if BOTH restores really happened.
    //
    // The quoted row goes through maskBracedSpans(): it can carry the interface guid,
    // and a red run must not print this machine's values to a console that gets pasted
    // into reports.
    // ===================================================================
    {
        Row fresh;
        ZeroMemory(&fresh, sizeof(fresh));
        bcdsetup::describeBinding(st, &fresh);
        wchar_t safe[kRowText];
        wchar_t what[400];
        _snwprintf(what, 300,
                   L"%ddpi 2d-binding: the machine this instrument put back still "
                   L"describes the table it put back - the state now says \"%s\"",
                   dpi, maskBracedSpans(fresh.detail, safe, kRowText));
        what[300] = 0;
        check(wcscmp(fresh.detail, g_wiz->screens[page].row.detail) == 0 &&
              fresh.state == g_wiz->screens[page].row.state, what);
    }

    // ===================================================================
    // *** ...AND THE SAME QUESTION ASKED OF THE VALUE, BECAUSE THE ONE ABOVE CAN ONLY
    //     ASK IT OF THE SENTENCE. ***
    //
    // The check above compares row TEXT, and text is where its blind spot is. Only two
    // of describeBinding()'s five branches interpolate the interface guid - the two
    // that need guidPresent - so on a host whose own binding reading is "seen, not
    // bound", which is the machine of every user before they run Zadig and the machine
    // the captures depict, the restored state produces a sentence with no guid in it
    // and the comparison passes while the state carries a foreign one.
    //
    // Measured by simulating that host inside a run: with shotState() removed from
    // measureRecheckAndNudge() and the four binding flags forced to that reading, the
    // suite finished 648 checks, 0 failures, VERIFY_OK while reading 4 at 144 DPI
    // rendered a foreign guid into the row it measured. The single most consequential
    // line in that fix was guarded only because THIS machine happens to be bound.
    //
    // A guard whose ability to fail depends on the machine it runs on is the same
    // family as a check whose two sides come from one source: it reports safety, and
    // whether it can report anything else is an accident of the host.
    //
    // WHERE EACH SIDE COMES FROM: the left is the guid carried by the MachineState the
    // whole run has been using - through prepare(), through the re-check that really
    // reads this machine, through the restore this function just made. The right is a
    // MachineState this check builds from nothing, here, by calling the product-side
    // invention shotState() into scratch storage. Two structs, two constructions, and
    // the only way they agree is that the state really is still the invented machine.
    // No branch of describeBinding() is involved, so no host reading can hide it.
    //
    // Neither guid is printed. What is printed is the comparison and the lengths,
    // which is everything a reader needs and nothing a report must not carry.
    // ===================================================================
    {
        bcdsetup::MachineState invented;
        shotState(&invented);
        bool sameGuid  = wcscmp(st->usb.guid, invented.usb.guid) == 0;
        bool sameFlags = st->usb.enumKeyPresent      == invented.usb.enumKeyPresent &&
                         st->usb.guidPresent         == invented.usb.guidPresent &&
                         st->usb.guidOnOtherFunction == invented.usb.guidOnOtherFunction &&
                         st->usb.interfacePresentNow == invented.usb.interfacePresentNow;
        wchar_t what[400];
        _snwprintf(what, 380,
                   L"%ddpi 2d-binding: ...and the machine it put back is still the "
                   L"INVENTED one, asked of the guid itself and not of a sentence that "
                   L"may not contain it - guid matches %d (%d chars against %d), the "
                   L"four binding flags match %d",
                   dpi, sameGuid ? 1 : 0, (int)wcslen(st->usb.guid),
                   (int)wcslen(invented.usb.guid), sameFlags ? 1 : 0);
        what[380] = 0;
        check(sameGuid && sameFlags, what);
    }
}

// ===========================================================================
// *** THE NAMED DOOR, AS A CONTROL. ***
//
// The four properties section 4.2 requires are asserted in two places on purpose.
// The three that are pure - reachable only from the unmet state, labelled differently
// from Next, and the exit code - are asserted in testBindingScreen() below, from
// invented flows, without a window. This is the fourth kind of question, which only a
// window can answer: the control really is on the screen, its sentence really fits
// inside it, and /preview really refuses it.
//
// *** WHY THE LABEL FITTING IS NOT COSMETIC HERE. *** The door's whole defence is
// that it is not a disguised Next, and the only thing carrying that is its sentence.
// A door whose label is cut at "The binding is already app" is a door with no label,
// which is the thing section 4.2 says is worse than no door at all.
// ===========================================================================
static void measureOverrideButton(int dpi)
{
    wchar_t what[400];

    bool have = g_overrideBtn && IsWindowVisible(g_overrideBtn);
    RECT br;
    ZeroMemory(&br, sizeof(br));
    if (have) {
        GetWindowRect(g_overrideBtn, &br);
        MapWindowPoints(HWND_DESKTOP, g_frame, (POINT*)&br, 2);
    }
    wchar_t label[128];
    label[0] = 0;
    if (g_overrideBtn)
        GetWindowTextW(g_overrideBtn, label, 128);

    HDC        odc = GetDC(g_frame);
    ActionBand ob  = overrideBand(odc);
    ReleaseDC(g_frame, odc);
    wprintf(L"  the door \"%s\" at %d..%d, needs %d, box %d, enabled %d\n", label,
            (int)br.left, (int)br.right, ob.needW, ob.w,
            IsWindowEnabled(g_overrideBtn) ? 1 : 0);

    _snwprintf(what, 390,
               L"%ddpi 2d-binding: the named door is ON this screen, because this "
               L"machine is not bound and Next is refused", dpi);
    check(have, what);
    if (!have)
        return;

    // WHERE EACH SIDE COMES FROM: needW is what the label measures in the button's
    // own font, box is what the geometry gave it. Both come from overrideBand(), and
    // that is the point - the two numbers it computes must agree with each other, or
    // the ceiling it applies is cutting the sentence it is applied to.
    _snwprintf(what, 390,
               L"%ddpi 2d-binding: the door's whole sentence fits INSIDE it - it needs "
               L"%d and its box is %d. A door whose label is cut is a door with no "
               L"label, which is worse than no door", dpi, ob.needW, ob.w);
    check(ob.needW <= ob.w, what);

    // ...and it does not run into the two buttons on the right. The band is the one
    // strip of this window with four controls on it at once, and the door is the
    // widest thing that has ever been in it.
    RECT sr;
    GetWindowRect(g_secondary, &sr);
    MapWindowPoints(HWND_DESKTOP, g_frame, (POINT*)&sr, 2);
    _snwprintf(what, 390,
               L"%ddpi 2d-binding: ...and it stops before Back begins (door ends %d, "
               L"Back starts %d)", dpi, (int)br.right, (int)sr.left);
    check(br.right < sr.left, what);

    // /preview refuses it, like every other control whose press does something.
    // Taking this door writes a line into the log and changes the exit code, and
    // /preview's written promise is that nothing happens.
    _snwprintf(what, 390,
               L"%ddpi 2d-binding: /preview greys the door too, because taking it "
               L"writes a line and changes the exit code (enabled %d)", dpi,
               IsWindowEnabled(g_overrideBtn) ? 1 : 0);
    check(!IsWindowEnabled(g_overrideBtn), what);
}

// ===========================================================================
// *** THE RECOVERY PATH THAT HANDED Next BACK ON THE SCREEN THAT REFUSES TO BE LEFT.
//     ***
//
// startReviewWorker() disables four controls and then calls CreateThread. When that
// FAILS, nothing was read and nothing was changed, so the buttons have to go back to
// what the screen says they should be - and that block asked `startBlockedNote` alone.
// startBlockedNote is /preview's promise and is null on a normal run, so the recovery
// enabled Next on the one screen in either flow whose whole point is that Next is
// refused. IDC_PRIMARY's own guard then swallowed the press: a button that is enabled
// and does nothing, which is the class this redesign was started to remove.
//
// *** IT IS DRIVEN AND NOT ASSERTED ABOUT. *** A harness cannot make CreateThread
// fail, so the recovery is a function - giveTheButtonsBack() - and this calls it on
// the real window, on the real binding screen, and then reads the real control. A
// check that only asked primaryEnabledFor() would be a check about a rule the broken
// site did not consult.
//
// *** AND THE FIRST HALF OF THE REAL PATH IS PERFORMED FIRST, WHICH IS THE CORRECTION
//     THIS ROUND MAKES. *** The real sequence is two steps: startReviewWorker()
// DISABLES four controls, then CreateThread fails, then this puts them back. The first
// version of this function performed only the second step - so at the moment the
// recovery ran, "Check again" was still enabled from the layout and the primary was
// still greyed by the block. The pre-state WAS the asserted post-state, and both
// checks were measuring the starting position instead of the recovery.
//
// It was not a theory: deleting `EnableWindow(g_recheckBtn, TRUE)` from the recovery
// left the suite at 632 checks, 0 failures, VERIFY_OK, and so did deleting the primary
// line outright. A user on a real machine would then have found "Check again" - the
// one control this screen's block can only be undone by - dead for the rest of the run
// after a failed CreateThread, with the door as the only live button on the band.
//
// So the four EnableWindow(FALSE) calls below reproduce gui.cpp:2530-2541 exactly.
// gui.cpp:2531 is the ONLY site in the program that ever disables g_recheckBtn and it
// lives inside startReviewWorker(), which this harness never calls - which is why the
// disable has to be performed here rather than waited for.
//
// *** startBlockedNote IS CLEARED FOR THE DURATION, AND WITHOUT THAT THIS COULD NOT
//     FAIL. *** These captures run in /preview, where startBlockedNote is set - and
// the broken expression returns "disabled" whenever it is set, so the defect is
// invisible in exactly the mode the harness renders in. So the field is put back to
// what a normal run has, the recovery is driven, the control is read, and the field
// is restored. That is also the machine the finding is about: somebody installing for
// real, on a machine with no WinUSB binding.
//
// WHERE EACH SIDE COMES FROM: the left is IsWindowEnabled() on the real g_primary
// after gui.cpp's own recovery path ran; the right is nextAllowed() over the table
// setup.cpp built from an invented machine. One is a control, the other is data, and
// the file that owns the recovery cannot see the file that owns the table.
//
// refreshButtons() AND layout() run afterwards, in that order, because that is the
// pair setScreen() ends with and the window has to be left exactly as it found it.
// Two things depend on that and both are silent failures: the loop that owns this
// pass reads the same primary button further down to decide which screen is being
// refused, and layout() is what greys the door and the offer - so without it this
// function would hand the door back enabled and the foot band capture taken three
// lines later would show a live door on a /preview run.
// ===========================================================================
static void measureButtonsBackOnRefusal(int dpi)
{
    wchar_t what[400];

    const wchar_t* keptNote  = g_wiz->startBlockedNote;
    bool           refuses   = !nextAllowed(g_wiz, g_screen);

    g_wiz->startBlockedNote = 0;               // what a normal run has

    // STEP ONE OF THE REAL PATH: the press takes every button away. Same four controls
    // in the same order as startReviewWorker(), so what follows has something to
    // recover FROM. Recorded and asserted, because a pre-state this function believes
    // in and does not check is how the first version of it came to prove nothing.
    if (g_recheckBtn)
        EnableWindow(g_recheckBtn, FALSE);
    if (g_actionBtn)
        EnableWindow(g_actionBtn, FALSE);
    if (g_overrideBtn)
        EnableWindow(g_overrideBtn, FALSE);
    EnableWindow(g_primary, FALSE);
    bool recheckDeadFirst = g_recheckBtn && !IsWindowEnabled(g_recheckBtn);

    // STEP TWO: CreateThread failed, nothing was read, nothing was changed.
    giveTheButtonsBack();

    bool primaryLive = IsWindowEnabled(g_primary) ? true : false;
    bool recheckLive = g_recheckBtn && IsWindowEnabled(g_recheckBtn) ? true : false;
    g_wiz->startBlockedNote = keptNote;
    refreshButtons();
    layout();

    _snwprintf(what, 390,
               L"%ddpi 2d-binding: a re-check that could not start hands the buttons "
               L"back, and Next stays REFUSED on the screen that refuses - the table "
               L"blocks here (%d) and the control came back enabled %d",
               dpi, refuses ? 1 : 0, primaryLive ? 1 : 0);
    what[390] = 0;
    check(refuses && !primaryLive, what);

    // ...and the button that undoes the refusal is the one control that DOES come
    // back. A recovery that greyed this too would leave the screen with no way out at
    // all, which is worse than the defect above - and it is the half that the missing
    // disable made unfalsifiable, so the dead-first state is named in the message.
    _snwprintf(what, 390,
               L"%ddpi 2d-binding: ...and \"%s\" comes back alive, because it is how a "
               L"blocked screen stops being blocked (dead first %d, enabled after %d)",
               dpi, kRecheckLabel, recheckDeadFirst ? 1 : 0, recheckLive ? 1 : 0);
    what[390] = 0;
    check(recheckDeadFirst && recheckLive, what);

    // ===================================================================
    // *** AND THE SAME RECOVERY ON A SCREEN THAT IS NOT BLOCKED, BECAUSE ON THIS ONE
    //     "DISABLED" IS ALSO THE ANSWER TO DOING NOTHING AT ALL. ***
    //
    // Everything above is about the screen that refuses, and there the correct
    // post-state is a greyed Next - which is exactly what the button is left at by the
    // disable that starts the sequence. So deleting the primary's EnableWindow from
    // giveTheButtonsBack() OUTRIGHT is invisible here: the button stays greyed, which
    // happens to be right. Measured, not reasoned: with the whole statement removed the
    // suite stayed at 642 checks, 0 failures, VERIFY_OK.
    //
    // On any check screen the flow does NOT block, the answer flips: the recovery has
    // to hand the button BACK, and a recovery that writes nothing leaves it dead. That
    // is the screen where the absence of the write is visible, and it is also where the
    // real consequence lives - a user whose re-check failed to start on the mixer or the
    // MIDI screen would be left unable to go forward at all.
    //
    // The screen is FOUND and not written down: the first kScreenCheck entry the table
    // does not block on. A flow that changed which screens block would move this with
    // it, and a flow where every check screen blocks would leave it unasked and say so.
    //
    // *** AND THE PRE-STATE IS MEASURED HERE TOO, WHICH IS THE LESSON THE HALF ABOVE
    //     LEARNED AND THIS HALF WAS WRITTEN WITHOUT. *** Its message said "dead first"
    // in words while only "enabled after" was in the conjunct. Deleting the single
    // EnableWindow(g_primary, FALSE) line below - harness only, product untouched -
    // left this green and still claiming "dead first", and with that line gone the
    // Critical this whole function exists to close came back in full: the primary's
    // EnableWindow deleted outright from giveTheButtonsBack() and the suite green.
    // One unasserted harness line stood between here and there, inside the function
    // written to close a defect of exactly that family. Prose asserting a measurement
    // that is not made is the signature defect of this project; it is a conjunct now.
    // ===================================================================
    int keptScreen = g_screen;
    int freePage   = -1;
    for (int i = 0; i < screenCount(g_wiz); i++) {
        if (g_wiz->screens[i].kind == kScreenCheck && nextAllowed(g_wiz, i)) {
            freePage = i;
            break;
        }
    }

    bool freeDeadFirst = false;
    bool freeLive      = false;
    if (freePage >= 0) {
        setScreen(freePage);
        g_wiz->startBlockedNote = 0;
        if (g_recheckBtn)
            EnableWindow(g_recheckBtn, FALSE);
        if (g_actionBtn)
            EnableWindow(g_actionBtn, FALSE);
        if (g_overrideBtn)
            EnableWindow(g_overrideBtn, FALSE);
        EnableWindow(g_primary, FALSE);
        freeDeadFirst = !IsWindowEnabled(g_primary);
        giveTheButtonsBack();
        freeLive = IsWindowEnabled(g_primary) ? true : false;
        g_wiz->startBlockedNote = keptNote;
        setScreen(keptScreen);
    }

    _snwprintf(what, 380,
               L"%ddpi: ...and on '%s', a check screen this flow does NOT block, that "
               L"same failed re-check hands Next BACK - dead first %d, enabled after "
               L"%d. This is the screen where a recovery that writes nothing is visible",
               dpi, freePage >= 0 ? g_wiz->screens[freePage].title : L"(none)",
               freeDeadFirst ? 1 : 0, freeLive ? 1 : 0);
    what[380] = 0;
    check(freePage >= 0 && freeDeadFirst && freeLive, what);
}

// The name a capture is filed under. It follows the KIND, not the position, so a
// flow that grows a screen in the middle does not silently rename every picture
// after it - which would make the one thing these files are for, comparing a
// picture with the same picture from the round before, impossible.
// *** AND THE KIND STOPPED BEING ENOUGH IN THIS ROUND, WHICH WAS WRITTEN DOWN
//     BEFORE IT HAPPENED. *** The note above allowedDeficit() has said since the
// table was introduced that the day a second kScreenCheck existed, both check
// screens would answer to "2-checks": they would share one overflow budget and the
// second would OVERWRITE the first's PNG. That day is this round.
//
// So a check screen is named by WHICH SCREEN IT IS, and the test is the identity of
// the title POINTER against the flow's own constants - not wcsstr() on its words.
// That matters here more than anywhere: this file has found a substring needle
// matching the wrong thing in three consecutive rounds, and a name matched on words
// would be one rewording away from filing two screens under one picture again. A
// pointer either is that constant or is not.
//
// A CHECK SCREEN NOBODY NAMED gets a name that says so, rather than falling into
// one of the four above. It then has no allowedDeficit() entry either, so it is
// caught twice; and shootAtDpi() asserts that the flow's names are all DIFFERENT,
// which is the property that actually protects the pictures.
// *** AND THE KIND STOPPED BEING ENOUGH FOR kScreenInfo TOO, WHICH THIS ROUND FOUND
//     BY BUILDING THE SCREEN AND WATCHING TWO PICTURES COLLIDE. *** "Get Zadig"
// measures nothing - Zadig installs nothing and leaves no key, so a row on it would
// be a mark this program cannot honestly paint - so it is kScreenInfo, and the switch
// below answered "1-welcome" for it. Two screens, one PNG, one overflow budget, and
// the opening's 12 pixel allowance quietly extended to a screen nobody had measured.
//
// So the TITLES are asked FIRST and the kind is the fallback. Every screen this
// program builds with words of its own is named by the identity of its title
// constant; the kind names only the three that have no words to be named by.
static const wchar_t* pageName(int p)
{
    if (titleAt(p) == ::kMixerTitle)
        return L"2a-mixer";
    // *** "2ab" AND NOT "2b", AND THE ALTERNATIVE WAS MEASURED BEFORE IT WAS REFUSED.
    //     *** This screen sits at flow position 2, between the mixer and the MIDI port,
    // so the letters would read in flow order only if 2b-midi became 2c-midi, 2c-zadig
    // became 2d-zadig and 2d-binding became 2e-binding. Measured cost of that rename:
    // 54 references in this file, 8 committed PNGs, about 90 check NAMES moving in one
    // round - which would bury the real delta of this task in the diff that is supposed
    // to show it - and the allowedDeficit() key "2d-binding" that the plan and this
    // round's own baseline table name by that literal.
    //
    // THE CONTRACT THIS FUNCTION ALREADY HAD SAYS NOT TO. Read the head of this comment:
    // the name follows IDENTITY and not position, precisely so that a flow which grows a
    // screen in the middle does not silently rename every picture after it and make
    // comparing a picture with the same picture from the round before impossible. A
    // renumbering here would be this function breaking its own reason for existing.
    //
    // So the new name SORTS into flow position without moving anything: "2a-mixer" <
    // "2ab-device" < "2b-midi", because '-' is below 'b' in ASCII. A directory listing
    // reads in flow order, which is what somebody comparing captures actually does, and
    // not one existing file or key moves.
    if (titleAt(p) == ::kDeviceTitle)
        return L"2ab-device";
    if (titleAt(p) == ::kMidiTitle)
        return L"2b-midi";
    if (titleAt(p) == ::kZadigTitle)
        return L"2c-zadig";
    if (titleAt(p) == ::kBindingTitle)
        return L"2d-binding";
    // *** THE SLUG READ "2-checks" UNTIL THIS ROUND AND THE SCREEN HAS BEEN CALLED
    //     "Install the driver" SINCE TASK 7. IT IS RETIRED HERE. ***
    //
    // The one thing that justified carrying it was a ledger key: the plan named
    // "2-checks" by that literal where Task 6b's wall is measured against it, and
    // retiring the key mid-flight would have broken a cross-task reference. That
    // reference expired when Task 7 landed, so the reason is gone and only the cost of
    // the name was left.
    //
    // WHAT THE NAME COST WHILE IT STAYED, all three of them:
    //   - roughly thirty check NAMES per run, because it is interpolated into every
    //     message pageName() feeds - "96dpi 2-checks: ...", "the press that installs is
    //     on '2-checks'" - so the harness's own output described a screen under a name
    //     the program uses nowhere. That is the half a reader meets first.
    //   - the key allowedDeficit() holds this screen's Rule 1 ceiling under, which is
    //     renamed with it, in the same commit, so the two cannot part company.
    //   - and a SORT ORDER DEFECT, which is the half nobody had counted: '-' is 0x2D
    //     and 'a' is 0x61, so "2-checks" sorted BEFORE "2a-mixer". The install screen's
    //     captures came out at the head of the directory listing, five screens ahead of
    //     where they belong, in a set of names whose entire purpose is that a listing
    //     reads in flow order.
    //
    // "2e-install" fixes all three at once: it says what the screen is, it sorts after
    // "2d-binding", and it moves FOUR committed PNGs and not the two first counted -
    // page-2-checks-{96,144}dpi.png, plus page-2-at-rest-{96,144}dpi.png, whose name was
    // a SECOND stale literal at savePaneAtRest()'s call site carrying the number of the
    // page this wizard replaced. All four came out byte-identical to the files they
    // replace and are recorded as pure renames. The wider renumbering (2b-midi ->
    // 2c-midi and so on) is still refused for the reason written above the device
    // screen's name: this function names by IDENTITY and not by position, precisely so a
    // flow that grows a screen in the middle does not rename every capture after it.
    //
    // AND IT LANDED INSIDE A WAVE RATHER THAN IN A COMMIT OF ITS OWN, which is what the
    // previous version of this comment recommended. The reason the recommendation existed
    // was rename detection, and that is satisfied here: the four blobs are unchanged, so
    // git records four renames at 100 per cent similarity whatever else the commit
    // carries. Splitting them out would have meant separating the slug literal from the
    // rest of this file's changes and leaving an intermediate commit where the harness
    // wrote names the tree did not have.
    //
    // AND "2ab-device" IS JUDGED AND KEPT. It is awkward but it is not stale: it names
    // the screen it depicts, nothing in the program has been renamed under it, and it
    // already sorts into flow position between "2a-mixer" and "2b-midi". Renaming it
    // would move four PNGs and about ninety check names to buy nothing but tidiness,
    // which is the trade this function's own contract refuses.
    if (titleAt(p) == ::kInstallTitle)
        return L"2e-install";
    switch (kindAt(p)) {
    case kScreenInfo:  return L"1-welcome";
    case kScreenWork:  return L"3-progress";
    case kScreenDone:  return L"4-finished";
    default:           break;
    }
    return L"2z-UNNAMED-CHECK-SCREEN";
}

// ---------------------------------------------------------------------------
// *** HOW FAR EACH SCREEN IS ALLOWED TO OVERFLOW ITS STRIP, BY NAME AND BY DPI. ***
//
// This replaces a bound that counted SCREENS and had no screen identity in it at
// all: "at most 2 of 4 overflow" at 144 DPI. Two holes, and both were measured
// rather than argued.
//   - THE DEFICIT WAS PRINTED AND NEVER ASSERTED. 2-checks could have gone from 843
//     to 5000 and the check would still have passed, because 5000 is still one
//     screen. The number that says how bad the screen the owner complained about is
//     was in the output and in nothing that could fail.
//   - THE ALLOWANCE WAS NOT KEYED TO ANYTHING. "At most 2" covered ANY two screens,
//     so fixing the opening's 12 pixels and breaking something new left the count at
//     2 and the suite green. Tasks 3 to 6 all run BEFORE Task 6b removes the page
//     scroll, so for four rounds this is the only guard on that area.
//
// So the bound is per screen and per DPI. The figures below are what the program
// measures TODAY, taken from the baseline of the round that wrote this:
//
//     96 DPI    1-welcome    0     2-checks   843     3-progress 0   4-finished 0
//    144 DPI    1-welcome   12     2-checks  1375     3-progress 0   4-finished 0
//
// *** IT IS A RATCHET AND NOT A TARGET. *** The comparison is <=, so a task that
// takes a deficit down passes and a task that puts one up fails. Asserting the exact
// figure would fail on every task that made progress, which trains a reader to edit
// the number instead of reading it. Bringing a figure DOWN to what a task achieved
// is the edit this table wants; raising one is the edit it exists to make somebody
// argue for.
//
// *** WHY 2-checks IS 843 AND 1375 RATHER THAN 0. *** That screen paints 1064
// logical pixels into a 221 pixel strip at 96 DPI and 1705 into 330 at 144. It is
// readable today only because the page scrolls. Removing that scroll while those
// numbers stand would not make the content fit, it would hide four fifths of it -
// the failure this project has corrected three times. The content split of Tasks 3
// to 6 is what takes those numbers down; this is what stops them going up on the way.
// The opening's 12 is the same kind of entry: found by this measurement and by no
// capture, because renderTall() photographs content at the height it asks for.
//
// A SCREEN NOT LISTED GETS ZERO, which is the whole point of naming them: a screen a
// later task adds is allowed no overflow at all until somebody writes it down here
// with a reason.
//
// THAT RESIDUAL IS CLOSED. It used to read: "pageName() follows the KIND, so the day
// Task 3 adds a second kScreenCheck both check screens answer to 2-checks and both
// inherit this budget; they already share a PNG filename for the same reason." Task 3
// added that screen. pageName() now names check screens by the identity of their
// title constant, "2a-mixer" is a name of its own with a budget of its own, and
// shootAtDpi() asserts that no two screens in the flow share a name at all.
// ---------------------------------------------------------------------------
// *** THE FIGURES BELOW ARE THIS ROUND'S, AND TWO OF THEM CAME DOWN. *** 2-checks
// went from 843 to 666 at 96 DPI and from 1375 to 1111 at 144, and not one word was
// deleted to do it: the walkthrough pane moved off that screen onto the MIDI port's,
// so the strip its rows scroll in is the whole page again instead of 58 per cent of
// it. The ratchet is <=, so the numbers are brought DOWN to what was measured.
//
//     96 DPI    1-welcome    0     2a-mixer 0   2b-midi  0    2-checks   666
//    144 DPI    1-welcome   12     2a-mixer 0   2b-midi 19    2-checks  1111
//
// *** 2b-midi IS 0 AT 96 AND 19 AT 144, AND THE ASYMMETRY IS THE FINDING RATHER THAN
//     THE NUMBER. *** That screen paints 221 logical pixels into a 221 strip at 96 -
// an exact fit, with no slack - and 349 into 330 at 144. Its text was cut three times
// in this round to reach that, so the residual is not verbosity: the STRIP scales by
// 1.492 between the two DPIs (594*58% against 398*58%, integer arithmetic on a
// percentage) while the painted content scales by 1.58, because every line height and
// every S() rounds up independently and there are about ten of them. A percentage
// constant against font metrics that do not scale by a constant is exactly the defect
// Task 6b Step 2 already exists to fix on the opening screen's 12 pixels, and the fix
// named there is the fix here: derive the space from the fonts rather than from a
// number. Cutting a fourth round of words would move this figure and leave the cause
// where it is, which is tuning to a fixture.
//
// *** AND NOT ONE PIXEL OF IT IS TEXT, WHICH WAS MEASURED RATHER THAN HOPED. *** The
// ink box on that screen at 144 DPI ends at 305 against a strip of 330 - 25 pixels of
// clear paper below the last bullet - because renderSubject() reports its height as
// the last block plus a trailing S(20) margin, which is 30 at this DPI. So the whole
// of the 19 is that margin: the deficit is real arithmetic about the space the
// renderer asks for, and it is not a line anybody has to scroll to reach. At 96 the
// same margin is what makes the fit exact rather than comfortable (ink ends at 192,
// reported 221, strip 221).
//
// Nothing is HIDDEN by it meanwhile: the page still scrolls until Task 6b removes
// that, and the "all of it reachable" check above passes on this screen at both DPIs.
// *** THIS ROUND'S FIGURES, AND 2-checks CAME DOWN BY MORE THAN THE PLAN PREDICTED.
//     ***
//
//     96 DPI  1-welcome  0   2a-mixer 0   2b-midi  0   2c-zadig  0   2d-binding 277
//                                                                    2-checks   241
//    144 DPI  1-welcome 12   2a-mixer 0   2b-midi 19   2c-zadig  0   2d-binding 478
//                                                                    2-checks   442
//
// 2-checks went 666 -> 241 at 96 and 1111 -> 442 at 144. The plan expected ~412 at
// 96, on the arithmetic "the picture is 254 of reported". The measured saving is 425,
// because the picture does not travel alone: the rule above it, the two 14 pixel gaps
// and the THREE PARAGRAPH CAPTION under it went with it, and the caption is about 150
// logical pixels at 96 DPI on its own. The prediction was an undercount, not the
// measurement being wrong, and it is recorded here because the next reader will
// otherwise try to reconcile 241 with 412.
//
// *** 2d-binding IS 277 AND 478, AND IT IS THE ONE NEW ENTRY THAT NEEDS A REASON. ***
// It carries a 254 pixel picture, a three paragraph caption and four bullets in a 398
// pixel strip, and there is no arrangement of those that fits until Task 6b. What
// this round bought is the thing that was actually wrong: the picture is ABOVE the
// fold now - renderSubject() draws it between the row and the bullets, and the check
// below asserts its bottom against this same strip - so the 277 is bullets and
// caption, which are read after looking at the picture, and not the picture itself.
// On the old page 2 the picture and every word about it were below the fold together.
//
// 2c-zadig is 0 at BOTH DPIs, with a pane, which is what "deliberately short" was
// supposed to mean and is here measured rather than asserted.
// ---------------------------------------------------------------------------
// *** THIS ROUND'S FIGURES. ONE CAME DOWN, AND IT DID NOT REACH ZERO. ***
//
//     96 DPI  1-welcome  0   2a-mixer 0   2b-midi  0   2c-zadig  0   2d-binding 277
//                                                                    2-checks   165
//    144 DPI  1-welcome 12   2a-mixer 0   2b-midi 19   2c-zadig  0   2d-binding 478
//                                                                    2-checks   322
//
// 2-checks went 241 -> 165 at 96 and 442 -> 322 at 144, and the arithmetic is
// published in two parts because the round both removed and added:
//
//   dropping the two duplicated rows   639 -> 495 reported at 96 DPI, a saving of
//                                      144; 1036 -> 800 at 144, a saving of 236.
//   adding the screen's two bullets    495 -> 563 at 96, a cost of 68; 800 -> 916 at
//                                      144, a cost of 116. Both numbers are MEASURED
//                                      by rendering the screen twice, once with the
//                                      entry's bullets and once without - see the
//                                      "really PAINTED" check, which exists to prove
//                                      the renderer reads the field at all and
//                                      publishes the cost on its way past.
//
// The plan's labelled ESTIMATE for the first part was "on the order of 140-150 px of
// the 241". Measured: 144. It is recorded as accurate because this table has twice
// recorded a prediction that was not, and an estimate that lands deserves the same
// treatment as one that misses.
//
// *** IT DOES NOT REACH ZERO AND NOTHING WAS DELETED TO GET IT CLOSER. *** What is
// left at 96 DPI is 563 painted into a 398 strip, and the ink ends at 542, so the
// residual is content and not margin. It is: the caption and its rule, two bullets,
// FIVE rows of two lines each, and the footer. There is no arrangement of five rows
// of prose that fits a 398 pixel strip - the rows are the screen's subject and the
// page still scrolls, which is what makes them reachable. Task 6b owns removing that
// scroll, and this is the number it inherits.
// ---------------------------------------------------------------------------
// *** THIS ROUND'S FIGURES. TWO WENT TO ZERO AND THE OTHER TWO CAME DOWN BY EXACTLY
//     THE SAME AMOUNT, WHICH IS THE EVIDENCE THAT ONE CAUSE EXPLAINED ALL FOUR. ***
//
//     96 DPI  1-welcome  0   2a-mixer 0   2b-midi 0   2c-zadig 0   2d-binding 257
//                                                                  2-checks   145
//    144 DPI  1-welcome  0   2a-mixer 0   2b-midi 0   2c-zadig 0   2d-binding 448
//                                                                  2-checks   292
//
// Every one of the eight screens dropped exactly S(kPageBottomPad) - 20 logical
// pixels at 96 DPI and 30 at 144 - because every renderer was reporting its trailing
// bottom MARGIN as content height. 1-welcome's 12 and 2b-midi's 19 were smaller than
// that margin and are therefore gone entirely; the other two were much larger than it
// and are not. Not one word was cut to do this and not one pixel of paint moved: the
// page 2d-binding draws at 96 DPI is byte for byte the page it drew before, and only
// the number it reports about itself changed.
//
// *** 2b-midi GOING TO ZERO IS THE HALF THAT MATTERS, AND IT IS NOT ABOUT A NUMBER.
//     *** That screen has a PANE. Its page was raising a scroll bar at 144 DPI, above
// a pane with a scroll bar of its own - the "scroll dentro de outro scroll" the owner
// opened this redesign with, the last one left in the program, and it was buying 44
// rows of blank paper. It is asserted now by identity rather than by this table: see
// the "ONE scrolling surface" check in shootAtDpi(), which reads the page's WS_VSCROLL
// bit and the pane's visibility off the two real windows.
//
// *** AND THE TWO THAT REMAIN ARE NOT A RATCHET WAITING TO BE WOUND DOWN. *** Task 6b
// was to remove the page's scroll outright. It was measured first, and the page is 398
// logical pixels tall at 96 DPI (kWinH 540, less a 70 pixel head band and a 72 pixel
// foot band).
//
// *** READ THE NEXT PARAGRAPH BEFORE THE NUMBERS: ONE OF THESE TWO IS PURE ARITHMETIC
//     AND THE OTHER IS A PRODUCT JUDGEMENT SUPPORTED BY ARITHMETIC, AND THE FIRST
//     DRAFT OF THIS BLOCK PRESENTED BOTH AS THE FIRST KIND. *** The correction was
// found by a reviewer recomputing it from these very constants, which is what this
// block asks a reader to do, so it is written down as a finding rather than quietly
// edited: a figure of "about 51" stood here where the constants give 95, and 95 is the
// difference between "no pane can exist" and "a pane can exist and would be a bad
// product". Both refusals stand; they do not stand for the same reason.
//
//   2d-binding, through the picture and no further, at 96 DPI:
//        22  y start                                             \
//      + 23  the title                                            |
//      + 28  the rule and its two 14 pixel gaps                   |  60 of this is
//      + 46  the screen's own row - the reading that greys Next    |  PADDING
//      + 10  the gap before the picture                           /
//      +270  the Zadig screenshot at native size, its 8 pixel plate included
//      ----
//       399  against a page of 398, before one word of the caption or of the four
//            bullets. The caption is a further 117 and the bullets 129, so 246 logical
//            pixels of text sit below a picture that has already used the page up.
//
//   ARITHMETIC, and it is the whole of what is arithmetic here: AT TODAY'S CONTENT AND
//   TODAY'S LAYOUT THIS SCREEN CANNOT HAVE A PANE. layout() would have
//   398 - 399 - 10 = MINUS 11 pixels to give it at 96 DPI and 594 - 602 - 15 = MINUS 23
//   at 144. Deleting the screen's own row leaves 35 and 49, and layout() drops a pane
//   under S(48) - 48 and 72 - so those are not panes either. All four figures are
//   reproducible from the constants in gui.cpp and nothing else.
//
//   A PRODUCT JUDGEMENT, and it must not be quoted as arithmetic: deleting the row AND
//   all 60 pixels of padding above the picture - the space between the title, the rule,
//   the row and the picture - reaches a pane of 95 at 96 DPI and 139 at 144. Those
//   CLEAR layout()'s floors: kReviewPaneMinH is 92 and 138, so this is a pane the code
//   would accept, by three pixels and by one. It is refused anyway, and for reasons a
//   person has to weigh rather than compute:
//     - it costs Screen::row on the ONE screen in either flow that greys Next, so the
//       screen would refuse to be left without saying what it read;
//     - it costs every gap that separates the title from the rule from the row from
//       the picture, on the screen whose subject is a picture somebody must compare
//       against their own screen;
//     - and the pane it buys still scrolls: 246 logical pixels of caption and bullets
//       through 95, which is the two-surfaces-on-one-screen shape this round removed
//       from 2b-midi, re-created here on purpose.
//   Whoever revisits this is revisiting a judgement, not a proof, and is entitled to
//   disagree with it. What they are not entitled to is the number 51.
//
//   2-checks, title and rows only, at 96 DPI:
//        22  y start
//      + 23  the title
//      + 28  the rule and its two gaps
//      +360  five rows of two to five lines each
//      ----
//       433  against the same 398, before the two bullets (68) and the footer (42).
//            (This subtotal read 411 in the first draft - the 22 was printed and then
//            not added. It UNDERSTATED the deficit by 22, so the conclusion never
//            depended on it, and it is corrected rather than softened because this
//            file's own rule is that an analytic figure diverging from the measured one
//            is a finding. 73 + 428 + 42 = 543 is what the harness reports.)
//
//   The five rows ARE this screen's subject - what will be written, whether it may be
//   written, and who for - and at 144 DPI they alone need about 590 of a 594 page, so
//   they do not fit even with the title, the rule, the bullets and the footer all
//   deleted. THAT is the decisive figure on this screen, and it is arithmetic. A pane
//   would make it worse by taking the strip from 398 to 221.
//
// The window cannot be made taller to fix this either: clearing 2d-binding at 96 DPI
// needs kWinH 817, which is 1226 device pixels at 144 DPI - taller than the work area
// of a 1080p screen at 150 per cent scaling, where this driver's users are. At 144 DPI
// the same screen needs a 1042 pixel page, which is 1255 logical px of window and 1882
// device pixels; the conclusion holds under either reading.
//
// So these two screens keep a scrolling page. That is ONE scrolling surface each,
// which is Rule 1's heading; it is not "the page never scrolls", which is Rule 1's
// mechanism, and the difference is declared here and in the report rather than bought
// by hiding 145 and 257 pixels of a screen's own subject behind an edge.
// ---------------------------------------------------------------------------
static int allowedDeficit(const wchar_t* screen, int dpi)
{
    // Renamed with pageName()'s slug and in the same commit, so the key and the name it
    // is looked up by cannot part company - see the block over pageName().
    if (wcscmp(screen, L"2e-install") == 0)
        return (dpi >= 144) ? 292 : 145;
    if (wcscmp(screen, L"2d-binding") == 0)
        return (dpi >= 144) ? 448 : 257;
    // 1-welcome and 2b-midi are NOT listed any more, and the default is zero. They
    // had 12 and 19 at 144 DPI, both of them margin rather than ink, and both are gone.
    // A screen that is not named here is allowed no overflow at all until somebody
    // writes it down with a reason.
    return 0;
}

// ---------------------------------------------------------------------------
// The summary pane's words, built ONCE for both DPIs, from the invented machine.
//
// *** TWO PATHS IN printSummary() DO NOT COME FROM THE MachineState IT IS HANDED, AND
//     THIS COMMENT SAID ONE UNTIL THE SECOND ONE EXISTED. ***
//
// THE FIRST is the log file. The last lines name it, and logFilePath() asks Windows where
// this account's local application data lives - so it names a real profile whatever state
// the function was given. There is nothing to invent it with: the folder comes from
// SHGetFolderPath and not from any argument.
//
// So it is REWRITTEN rather than dropped: the invented machine's own local
// application data directory is put where Windows put this one's. The line stays,
// the picture stays complete, and the sentence still reads the way the product
// wrote it. How many lines that touched is COUNTED and checked below, because a
// rewrite that silently matched half the summary would be worse than the leak.
//
// *** THE SECOND IS THE MODEL LINE, IT IS NOT NEUTRALISED, AND IT CAN MOVE TWO COMMITTED
//     PNGs. *** printSummary() asks bcdsetup::selectedModel(), which reads the GLOBAL
// g_run - because the answer has to survive the window closing. Nothing in this function
// launders that. What keeps the captures honest is only this: wmain() runs the text suites
// BEFORE this block, PART 1 branch F moves ::g_run.selectedModel to the BCD2000 and back,
// and it asserts the restore. If a future suite moves that field and does not put it back,
// these two pictures - page-4-finished at both DPIs - silently gain the EXPERIMENTAL
// warning, and no check here would say so.
//
// So: a suite that touches ::g_run.selectedModel must restore it and assert the restore.
// That is a rule about a global, which is the weakest kind, and it is written here because
// this is the comment somebody reads before editing this function. The alternative - taking
// the selection as an argument - is a change to printSummary()'s signature and to every one
// of its five callers, and it is not this task's.
// ---------------------------------------------------------------------------
static int g_summaryRewrites = 0;

static void buildShotSummary()
{
    bcdsetup::MachineState s;
    shotState(&s);
    Pending p;
    ZeroMemory(&p, sizeof(p));
    p.driverFileReplaced = true;
    capReset();
    bcdsetup::setLineSink(capSink);
    bcdsetup::setConsoleEcho(false);
    printSummary(&s, &p, false, L"");
    bcdsetup::setLineSink(0);
    bcdsetup::setConsoleEcho(true);

    wchar_t live[kPathMax];
    if (!bcdsetup::getLocalAppDataDir(live, kPathMax))
        return;
    // The invented machine's equivalent of it, taken from the same string
    // fakeState() builds bridgeTarget out of.
    static const wchar_t* kInvented = L"C:\\Users\\Example\\AppData\\Local";
    size_t liveLen = wcslen(live);
    g_summaryRewrites = 0;
    for (int i = 0; i < g_capN; i++) {
        wchar_t* at = wcsstr(g_cap[i], live);
        if (!at)
            continue;
        wchar_t out[kCapWide];
        _snwprintf(out, kCapWide - 1, L"%.*s%s%s", (int)(at - g_cap[i]), g_cap[i],
                   kInvented, at + liveLen);
        out[kCapWide - 1] = 0;
        wcscpy(g_cap[i], out);
        g_summaryRewrites++;
        // Printed, because a rewrite nobody can read is a rewrite nobody can
        // disagree with. What is printed is the line AFTER it, so this output
        // carries the invented profile and not the real one.
        wprintf(L"  summary line rewritten to the invented profile: %s\n", g_cap[i]);
    }
}

// ---------------------------------------------------------------------------
// The two checks that make the paragraph above a measurement.
// ---------------------------------------------------------------------------
// *** IT ASKS FOR THREE MARKS AND IT USED TO ASK FOR FOUR, AND THE MISSING ONE IS A
//     PROPERTY OF THE PAGE RATHER THAN A CONCESSION. ***
//
// The red on this page came from the WinUSB row, and that row is on a screen of its
// own now. Of the rows that remain, exactly one has a red branch: "Administrator
// rights", when this process is not elevated. So the ONLY way to make the pictured
// machine paint a red would be to picture a machine that cannot install - which would
// change the note beside the buttons, grey the button the picture exists to show, and
// tune the invented machine to the check instead of the other way round.
//
// So this asks for what an installable machine can honestly produce, and the red is
// asserted separately, below, on a second invented machine that nothing is
// photographed from. Between them the statement is stronger than the one they
// replace: the picture still shows three kinds of mark, AND the page can still say
// something is wrong.
static void checkMarkKinds(Wizard* wiz)
{
    bool seen[kRowSkipped + 1];
    for (int i = 0; i <= kRowSkipped; i++)
        seen[i] = false;
    for (int i = 0; i < wiz->reviewCount; i++) {
        int st = (int)wiz->review[i].state;
        if (st >= 0 && st <= kRowSkipped)
            seen[st] = true;
    }
    wchar_t what[300];
    _snwprintf(what, 290,
               L"the invented machine EXERCISES the install screen - ok %d, warn %d, "
               L"neutral %d over %d rows", seen[kRowOk] ? 1 : 0,
               seen[kRowWarn] ? 1 : 0, seen[kRowNeutral] ? 1 : 0, wiz->reviewCount);
    what[290] = 0;
    check(seen[kRowOk] && seen[kRowWarn] && seen[kRowNeutral], what);

    // *** AND THE PAGE HAS NOT LOST THE ABILITY TO SAY SOMETHING IS WRONG. *** This is
    // the half checkMarkKinds() would otherwise have dropped when the red row left.
    // WHERE EACH SIDE COMES FROM: the left is fillPreflightRows() run over a state
    // this function invents and nothing is rendered from; the right is kRowFail. A
    // round that softened the elevation row to amber - which is the shape every mark
    // that has ever been softened here took - turns this red.
    {
        bcdsetup::MachineState no;
        ZeroMemory(&no, sizeof(no));
        no.pathsResolved = true;
        no.elevated      = false;
        Wizard u;
        ZeroMemory(&u, sizeof(u));
        fillPreflightRows(&u, &no);
        int el = -1;
        for (int i = 0; i < u.reviewCount; i++)
            if (wcsstr(u.review[i].title, L"Administrator rights"))
                el = i;
        _snwprintf(what, 290,
                   L"...and the one row on it that still HAS a red branch paints one on "
                   L"a machine that cannot be written to (row %d of %d, mark %d)",
                   el, u.reviewCount, el >= 0 ? (int)u.review[el].state : -1);
        what[290] = 0;
        check(el >= 0 && u.review[el].state == kRowFail, what);
    }
}

// ===========================================================================
// *** NO ROW WAS CUT OFF, WHICH NOTHING MEASURED UNTIL A CAPTURE WAS LOOKED AT. ***
//
// setRow() formats into kRowText characters with _vsnwprintf and truncates in
// silence. Round 4's Windows 10 row was 40 characters over and ended
// "...matches those files over the WinUSB binding and t" in the committed capture,
// with every text assertion about it green - because everything they looked for
// happened to be in the surviving half.
//
// The half it lost was the half that says not to take the option the first half
// offers, which is the worst possible place on this page for a sentence to stop.
//
// A LENGTH OF EXACTLY kRowText - 1 IS THE SIGNATURE OF THE TRUNCATION, not merely a
// long row: _vsnwprintf writes the count it was given and no terminator when the
// output fills it, and setRow() then terminates at that index. A row that genuinely
// ended on character 511 would be a false alarm, and it would be a false alarm
// telling somebody to shorten a row that is one character from being cut.
// ===========================================================================
static void checkRowsNotTruncated(Wizard* wiz)
{
    int worstLen = 0;
    const wchar_t* worst = L"";
    int cut = 0;
    for (int i = 0; i < wiz->reviewCount; i++) {
        int len = (int)wcslen(wiz->review[i].detail);
        if (len > worstLen) {
            worstLen = len;
            worst    = wiz->review[i].title;
        }
        if (len >= kRowText - 1)
            cut++;
    }
    for (int i = 0; i < wiz->stepCount; i++)
        if ((int)wcslen(wiz->steps[i].detail) >= kRowText - 1)
            cut++;

    wchar_t what[400];
    _snwprintf(what, 390,
               L"no row's text was cut off by setRow (%d at the %d character limit; "
               L"the longest is %d, \"%s\")", cut, kRowText - 1, worstLen, worst);
    what[390] = 0;
    check(cut == 0, what);
}

// ===========================================================================
// THE CAPTION UNDER THE ZADIG SCREENSHOT
//
// The picture is of a machine that is ALREADY BOUND. Two of its fields therefore
// show a state the person reading the page does not have, and the whole question of
// whether the picture is honest is the question of whether the caption says so.
//
// *** THE LAST CHECK IN HERE IS THE INTERESTING ONE, AND IT IS AN ABSENCE. *** Zadig
// writes Install, Replace, Reinstall or Upgrade on that button according to what is
// already bound, and nobody here has seen every variant. A caption that named one of
// them would be this page stating something nobody measured - the same defect class
// as a summary claiming an install did what it only attempted. So the caption is
// required to name MORE THAN ONE and to say the name does not matter.
// ===========================================================================
// ===========================================================================
// *** THE OTHER CALL SITE OF reviewFooterFor(), WHICH IS runWindowed()'S. ***
//
// rebuildScreens()'s call is asserted in the text suite against a Wizard that suite
// builds. This one is asserted against THE REAL g_wiz - the structure the product's
// own runWindowed() filled on the way into this render pass - so between the two,
// deleting either call site is red.
//
// It has to be the reassurance here and nothing else, and now that is the only
// sentence this selector has.
//
// *** THE SECOND CHECK THAT STOOD HERE IS DELETED. *** It asserted that the footer
// was NOT the after-the-offer sentence - "which would mean the selector is answering
// from something other than Run::thirdPartyStarted". Both that sentence and that
// field are gone (see setup.cpp), so the string it searched for is one no build of
// this program contains: an absence check for a literal that cannot exist is green
// by construction and can never go red. What is left is the one clause that can.
//
// WHERE EACH SIDE COMES FROM: the left is Wizard::reviewFooter as runWindowed() left
// it; the right is a phrase written in this file.
// ===========================================================================
static void checkReviewFooterWired(Wizard* wiz)
{
    const wchar_t* f = wiz->reviewFooter;
    check(f != 0 && f[0] != 0 &&
          wcsstr(f, L"Nothing here has been touched") != 0,
          L"runWindowed() filled the install screen's footer from the selector, and "
          L"on a run that started nothing the answer is the reassurance");
}

static void checkZadigCaption(Wizard* wiz)
{
    const wchar_t* c = wiz->zadigCaption;
    check(c != 0 && c[0] != 0, L"page 2 carries a caption for the Zadig picture");
    if (!c)
        return;

    check(wcsstr(c, L"BCD3000 (Interface 0)") != 0,
          L"the caption points at the list line the user has to match, spelled the "
          L"way Zadig writes it");
    check(wcsstr(c, L"1397") != 0 && wcsstr(c, L"00BF") != 0,
          L"...and at the USB ID, both halves of it");
    check(wcsstr(c, L"DIFFERENT ON YOUR MACHINE") != 0,
          L"the caption declares that the picture shows fields that will differ");
    check(wcsstr(c, L"already bound") != 0,
          L"...and says WHY they differ: this machine is already bound");
    check(wcsstr(c, L"Driver box") != 0 && wcsstr(c, L"usbaudio") != 0,
          L"...naming the first of the two that differ, and what stands there "
          L"instead");
    check(wcsstr(c, L"Replace Driver") != 0 && wcsstr(c, L"Install Driver") != 0 &&
          wcsstr(c, L"Reinstall Driver") != 0,
          L"...and MORE THAN ONE possible button label, so no single one is "
          L"presented as the one the user will see");
    check(wcsstr(c, L"whatever it is called") != 0,
          L"...closed by the sentence that makes the button findable without "
          L"claiming to know its name");
    check(wcsstr(c, L"WCID") != 0,
          L"the red cross beside WCID is disclaimed, because it is in the picture "
          L"and reads like a failure");
    check(wcsstr(c, L"More Information") != 0,
          L"...and the column that is links rather than the selector is named");

    // *** IT HAS TO READ WITHOUT THE PICTURE. *** renderReview() drops the picture in
    // silence when the resource cannot be decoded and keeps the caption. A caption
    // written as annotation - "the box at the top", "circled above" - would then be
    // pointing at nothing. Every field it mentions is named by its own label, so the
    // one thing to forbid is a reference to the image itself.
    check(wcsstr(c, L"above") == 0 && wcsstr(c, L"circled") == 0 &&
          wcsstr(c, L"arrow points") == 0,
          L"the caption names fields by their labels and never by where they are in "
          L"the picture, so it still reads when the picture could not be decoded");
}

static void checkNoLiveIdentity(Wizard* wiz)
{
    collectLiveIdentity();
    g_hitWhat  = 0;
    g_hitWhere = 0;

    scanForIdentity(wiz->windowTitle,    L"the window title");
    scanForIdentity(wiz->headline,       L"the header band's headline");
    scanForIdentity(wiz->subhead,        L"the header band's subhead");
    scanForIdentity(wiz->welcomeLine1,   L"page 1's first line");
    scanForIdentity(wiz->welcomeLine2,   L"page 1's second line");
    for (int i = 0; i < 4; i++)
        scanForIdentity(wiz->welcomeBullets[i], L"one of page 1's bullets");
    scanForIdentity(wiz->reviewCaption,  L"page 2's caption");
    scanForIdentity(wiz->reviewFooter,   L"page 2's footer");
    // The pane's caption and text are scanned in the per screen loop below, where
    // they now live. They used to be Wizard fields and were scanned here; the loop
    // covers every screen's, so the coverage did not shrink when they moved.
    scanForIdentity(wiz->zadigCaption,      L"page 2's Zadig caption");
    for (int i = 0; i < wiz->reviewCount; i++) {
        scanForIdentity(wiz->review[i].title,  L"a row title on page 2");
        scanForIdentity(wiz->review[i].detail, L"a row's text on page 2");
    }
    scanForIdentity(wiz->progressCaption, L"page 3's caption");
    for (int i = 0; i < wiz->stepCount; i++) {
        scanForIdentity(wiz->steps[i].title,  L"a step title on page 3");
        scanForIdentity(wiz->steps[i].detail, L"a step's text on page 3");
    }
    scanForIdentity(g_early,                  L"the log pane on page 3");
    // *** THE START BUTTON'S WORD USED TO BE ONE FIELD AND IS NOW A COLUMN OF THE
    //     TABLE, SO THIS SCANS THE COLUMN. *** Deleting Wizard::startVerb removed a
    // string from this gate, and a privacy gate that quietly checks less is the
    // class of defect that once let the owner's account name reach a tracked PNG.
    // This does not merely replace it: every screen's primaryLabel is scanned, and
    // so are the four other strings a Screen can paint - the title, the bullets, the
    // pane and the two action labels - none of which was covered before, because
    // when this list was written the table did not exist. The coverage GREW.
    int screensScanned = 0;
    for (int i = 0; i < screenCount(wiz); i++) {
        const Screen* s = &wiz->screens[i];
        screensScanned++;
        scanForIdentity(s->title,         L"a screen's title");
        scanForIdentity(s->primaryLabel,  L"a screen's primary button");
        scanForIdentity(s->actionLabel,   L"a screen's action button");
        scanForIdentity(s->overrideLabel, L"a screen's override button");
        scanForIdentity(s->paneCaption,   L"a screen's pane caption");
        scanForIdentity(s->paneText,      L"a screen's pane");
        scanForIdentity(s->row.title,     L"a screen's row title");
        scanForIdentity(s->row.detail,    L"a screen's row text");
        for (int b = 0; b < 4; b++)
            scanForIdentity(s->bullets[b], L"one of a screen's bullets");
    }
    scanForIdentity(wiz->cannotCancelNote,    L"the note beside the buttons");
    scanForIdentity(wiz->startBlockedNote,    L"the note beside the buttons");
    scanForIdentity(wiz->doneCaptionOk,       L"page 4's caption");
    scanForIdentity(wiz->doneCaptionStopped,  L"page 4's caption");
    scanForIdentity(wiz->doneCaptionFail,     L"page 4's caption");
    scanForIdentity(wiz->doneNotice,          L"page 4's painted notice");
    for (int i = 0; i < g_capN; i++)
        scanForIdentity(g_cap[i], L"the summary pane on page 4");

    wchar_t what[512];
    if (g_hitWhat)
        _snwprintf(what, 500,
                   L"NOTHING A CAPTURE CAN SHOW NAMES THIS MACHINE: %s was found in "
                   L"%s (%d strings looked for)", g_hitWhat, g_hitWhere, g_needleN);
    else
        // *** THE SCREEN COUNT IS IN THIS SENTENCE ON PURPOSE. *** The loop above
        // walks screenCount(wiz), and a loop over zero screens scans nothing and
        // reports the same clean pass as a loop over four. A gate that quietly
        // checks less is the class of defect that once let the owner's account name
        // reach a tracked PNG, so the denominator is printed beside the verdict and
        // asserted to be non zero below.
        _snwprintf(what, 500,
                   L"nothing a capture can show names this machine: %d strings "
                   L"looked for, in %d screens, %d review rows, %d step rows, the "
                   L"log pane and %d summary lines", g_needleN, screensScanned,
                   wiz->reviewCount, wiz->stepCount, g_capN);
    what[500] = 0;
    check(g_hitWhat == 0, what);

    // WHERE EACH SIDE COMES FROM: the left is a counter the LOOP incremented, once
    // per screen it really touched; the right is the length of the table, asked of
    // the product's screenCount(). The first version of this line compared
    // screenCount() against zero and was therefore unable to fail - a loop whose
    // bound had been shrunk to nothing would still have found screenCount() saying
    // four. That is the same trap this project has now hit five times, and it is
    // the reason the counter exists rather than the expression being repeated.
    _snwprintf(what, 500,
               L"...and the scan really walked the whole table rather than part of it "
               L"(%d screens touched of %d in the flow)", screensScanned,
               screenCount(wiz));
    check(screensScanned == screenCount(wiz) && screensScanned > 0, what);

    // *** AND THE GATE REALLY HOLDS EVERY STRING IT WAS OFFERED. *** See addNeedle():
    // the table is fixed and used to discard what did not fit without saying so, so
    // the gate could be searching for four strings out of six and report the same
    // clean pass. WHERE EACH SIDE COMES FROM: the left counter is incremented BEFORE
    // the table is consulted, once per distinct string offered; the right is the
    // table's own length, which grows only when a string was really stored. Shrinking
    // kMaxNeedles makes them disagree, which is how this line was seen failing.
    //
    // ONE HONEST LIMIT ON THE "offered" FIGURE, because this file publishes
    // denominators: the dedup scan can only look at what was STORED, so once the cap
    // has bitten, a string it refused is offered and counted again on the next call.
    // In the healthy state - nothing dropped - the count is exact, which is the state
    // this line asserts. When it is wrong it is wrong in the direction of reporting
    // MORE loss, not less.
    _snwprintf(what, 500,
               L"...and the gate holds every string it was offered rather than capping "
               L"in silence (%d offered, %d held, %d dropped)",
               g_needleOffered, g_needleN, g_needleDropped);
    what[500] = 0;
    check(g_needleDropped == 0 && g_needleN == g_needleOffered && g_needleN > 0, what);

    _snwprintf(what, 500,
               L"and the one summary line Windows names rather than the state was "
               L"rewritten to the invented profile, exactly once (%d lines)",
               g_summaryRewrites);
    what[500] = 0;
    check(g_summaryRewrites == 1, what);
}

// The whole of the pixel proof, for one DPI.
static void shootAtDpi(Wizard* wiz, int dpi)
{
    wprintf(L"\n--- %d DPI ---\n", dpi);

    g_wiz  = wiz;
    g_dpi  = dpi;
    buildFonts();

    HWND frame = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW,
                                 L"BcdWizardFrame", wiz->windowTitle,
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                 WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                                 0, 0, g_inst, 0);
    g_frame = frame;
    g_page = CreateWindowExW(WS_EX_CONTROLPARENT, L"BcdWizardPage", L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL,
                             0, 0, 10, 10, frame, 0, g_inst, 0);
    g_primary = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE |
                                WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 10, 10,
                                frame, (HMENU)IDC_PRIMARY, g_inst, 0);
    g_secondary = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE |
                                  WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
                                  frame, (HMENU)IDC_SECONDARY, g_inst, 0);
    g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP |
                            WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                            0, 0, 10, 10, g_page, (HMENU)IDC_LOG, g_inst, 0);
    g_summary = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP |
                                WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                0, 0, 10, 10, g_page, (HMENU)IDC_SUMMARY, g_inst, 0);
    g_bar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH,
                            0, 0, 10, 10, g_page, (HMENU)IDC_BAR, g_inst, 0);
    // Same condition as runWizard()'s: no re-check, no button. Created here too
    // because the foot note's box depends on whether it is there, and a measurement
    // taken without it would be a measurement of a window nobody gets.
    if (wiz->recheck)
        g_recheckBtn = CreateWindowExW(0, L"BUTTON", kRecheckLabel,
                                       WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                       0, 0, 10, 10, frame, (HMENU)IDC_RECHECK,
                                       g_inst, 0);
    // Same condition as runWizard()'s, and created for the same reason as the one
    // above it: the foot note's box is measured from where these two end, so a
    // measurement taken without them would be a measurement of a window nobody gets.
    //
    // *** IT IS NEVER PRESSED BY THIS HARNESS, AND THAT IS DELIBERATE. *** Pressing
    // it would start winget or a browser. Everything below is about where it is,
    // what it says and whether it can act - never about what it does.
    g_actionLabel[0] = 0;
    if (flowHasAction(wiz))
        g_actionBtn = CreateWindowExW(0, L"BUTTON", g_actionLabel,
                                      WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                      0, 0, 10, 10, frame, (HMENU)IDC_ACTION,
                                      g_inst, 0);
    // ...and the named door, on the product's own condition. It is created here for
    // the reason the offer is: the foot note's box is measured from where these
    // buttons end, and the door is the widest of the three, so a band measured
    // without it would be a measurement of a window nobody gets.
    //
    // *** IT IS NEVER PRESSED FROM shootAtDpi(), AND THAT IS DELIBERATE. *** Pressing
    // it would advance the window mid capture. The suite that DOES press it builds
    // its own flow and is below; everything here is about where the door is, what it
    // says and whether it can act.
    g_overrideLabel[0] = 0;
    if (flowHasOverride(wiz))
        g_overrideBtn = CreateWindowExW(0, L"BUTTON", g_overrideLabel,
                                        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                        0, 0, 10, 10, frame, (HMENU)IDC_OVERRIDE,
                                        g_inst, 0);
    // ...and the two choice controls, on the product's own condition and with the
    // product's own style bits, CHILDREN OF THE PAGE like the product makes them.
    //
    // *** THEY ARE CREATED HERE FOR A REASON THE OTHER THREE DO NOT HAVE, AND IT IS THE
    //     WHOLE REASON THE CHECK BELOW CAN EXIST. *** The other three are created so
    // that the foot NOTE is measured in a band that has them. These are the one thing on
    // their screen that no capture can show - renderTall() photographs painted content
    // and a child control is not painted content - so if they were absent here, "the
    // choice is on the screen, inside the box the renderer reserved, above the fold"
    // would be a sentence with nothing behind it. They are real windows, laid out by the
    // product's own layout(), and read back off the page.
    //
    // *** THEY ARE NEVER PRESSED FROM HERE, AND THAT IS DELIBERATE. *** A press records
    // a choice into the run and writes a line into the log, which would move the capture
    // and the line sink under the suite that is measuring them. The suite that DOES
    // drive chooseModel() calls it directly, on a Run of its own, and is above.
    if (flowHasChoice(wiz)) {
        for (int i = 0; i < 2; i++) {
            DWORD extra = (i == 0) ? (WS_GROUP | WS_TABSTOP) : 0;
            g_modelBtn[i] = CreateWindowExW(0, L"BUTTON", L"",
                                            WS_CHILD | BS_AUTORADIOBUTTON |
                                            BS_MULTILINE | extra,
                                            0, 0, 10, 10, g_page,
                                            (HMENU)(UINT_PTR)(IDC_MODEL0 + i),
                                            g_inst, 0);
        }
    }
    SendMessageW(g_log, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(g_summary, EM_SETLIMITTEXT, 0, 0);
    // The product's own function, not a copy of its style bits. That is the whole
    // point of it being a function: the pane these pictures are taken of IS the pane
    // runWizard() makes, so an assertion about ES_AUTOHSCROLL here is an assertion
    // about the shipped window.
    buildPane(g_page, wiz);
    applyFonts();
    if (g_early && g_early[0])
        SetWindowTextW(g_log, g_early);
    // *** THE PRODUCT'S OWN GATE, AND NOT A COPY OF THE FIELD TEST IT USED TO BE. ***
    // These two lines read wiz->showDevicePhoto and wiz->zadigCaption directly, which
    // is exactly what runWizard() did - so the harness reproduced the defect it was
    // meant to be able to catch, and a screen that asked for a picture on a flow that
    // did not would have been photographed without one HERE too, in agreement with a
    // window that was wrong. flowNeedsPhoto() is the union; see gui.cpp.
    if (flowNeedsPhoto(wiz))
        decodePhoto(S(kPhotoW));
    // The same call runWizard() makes, at the same width, so the captures show the
    // page the product draws and not its degraded form.
    if (flowNeedsZadigShot(wiz))
        decodeZadigShot(S(kZadigShotW));

    RECT want = { 0, 0, S(kWinW), S(kWinH) };
    AdjustWindowRectEx(&want, (DWORD)GetWindowLongPtrW(frame, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongPtrW(frame, GWL_EXSTYLE));
    SetWindowPos(frame, 0, 0, 0, want.right - want.left, want.bottom - want.top,
                 SWP_NOZORDER);

    // The last page's pane. buildShotSummary() filled g_cap once, before either
    // DPI pass, so the two captures are provably the same words at two sizes
    // instead of two runs of printSummary() that are only expected to agree.
    for (int i = 0; i < g_capN; i++)
        appendTo(g_summary, g_cap[i]);
    SendMessageW(g_summary, EM_SETSEL, 0, 0);
    SendMessageW(g_summary, EM_SCROLLCARET, 0, 0);

    ShowWindow(frame, SW_SHOW);

    // ===================================================================
    // *** THE KEYBOARD FOCUS CUE IS PINNED HERE, BECAUSE WITHOUT IT TWO TRACKED
    //     CAPTURES ARE NOT DETERMINISTIC. ***
    //
    // Measured, not suspected: foot-band-opening-144dpi.png came out with different
    // bytes from md5-identical sources on a rerun of an unmodified tree. The dotted
    // rectangle inside the default button is drawn or not according to the window's
    // UISF_HIDEFOCUS flag, which USER32 initialises from whether the last input this
    // session was a key or the mouse - a thing nothing in this repository sets, reads
    // or can predict. The band captures print the buttons with WM_PRINTCLIENT, so the
    // rectangle lands in the bitmap.
    //
    // IT REACHES ONLY THE BAND CAPTURES, AND ONLY THE OPENING'S. renderTall() prints
    // children for the work and done screens alone, and neither of those draws a focus
    // rectangle; and refreshButtons() calls SetFocus(g_primary) only when the primary
    // is enabled, which /preview refuses on the screen that installs.
    //
    // IT IS FIXED HERE AND NOT IN THE PRODUCT because it is a property of the session,
    // not of gui.cpp: the shipped window should show the cue when the user is driving
    // it from the keyboard. What must not vary is the photograph.
    //
    // *** AND IT IS URGENT RATHER THAN COSMETIC. *** This project's central piece of
    // evidence is "N captures byte identical between rounds". An unpinned cue makes
    // that N silently 2 weaker every round, and it cannot be caught by looking: a
    // clean `git status` cannot tell "never changed" from "restored from HEAD".
    //
    // DefWindowProc relays WM_UPDATEUISTATE to every child window, so this one send
    // covers all four buttons. It is before measureHeadBand(), which takes the first
    // picture of the pass.
    // ===================================================================
    SendMessageW(g_frame, WM_UPDATEUISTATE,
                 MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);

    // The band first: it is painted on every page, it is the same band on every
    // page, and it is measured once per DPI rather than four times over.
    measureHeadBand(wiz, dpi);

    // *** THE DESIGN'S RULE 1, AS A NUMBER PER SCREEN INSTEAD OF AS A SENTENCE. ***
    //
    // Rule 1 is "the page never scrolls: if content does not fit, the pane scrolls
    // and it is the only thing that does". A screen obeys it when everything the
    // renderer paints fits in the strip of page it is given - g_viewH, which is the
    // client height minus the pane when there is one. What follows counts the screens
    // that do NOT, and prints the deficit for each, because until this round the
    // whole of Rule 1's evidence was the plan asserting a figure (463 against 410)
    // that no longer matched the program by a factor of two.
    //
    // *** IT IS A RATCHET AND NOT A TARGET, AND IT IS ASSERTED PER SCREEN. *** The
    // per screen ceilings and the reasons for each of them are in allowedDeficit();
    // the two counters here survive only to print the ledger line at the end of the
    // pass, which is a wprintf and not a verdict.
    int overflowing = 0;
    wchar_t deficits[512];
    deficits[0] = 0;

    // Two things that can only be answered over the WHOLE flow, collected screen by
    // screen inside the loop and asserted once after it. See the two blocks that
    // read them, below the loop.
    const wchar_t* names[kMaxScreens];
    int            refused     = 0;
    const wchar_t* refusedOn   = L"(none)";
    int            blocked     = 0;
    const wchar_t* blockedOn   = L"(none)";
    for (int i = 0; i < kMaxScreens; i++)
        names[i] = 0;

    // *** WHERE THE ONE CONTEXTUAL BUTTON REALLY STANDS, COUNTED OVER THE FLOW. ***
    // The whole point of moving it onto a screen is that it is beside its own subject
    // and nowhere else; a check made only on the screen that should have it cannot see
    // it ALSO standing on the three that should not. So the control's visibility is
    // read on every screen and the verdict is made once, after the loop.
    //
    // *** IT IS A PER SCREEN AGREEMENT NOW AND IT USED TO BE "EXACTLY ONE", AND THE
    //     ROUND THAT ADDED THE SECOND OFFER IS THE ROUND THAT HAD TO CHANGE IT. ***
    // The old verdict was `showingAction == 1 && tableSaysAction == 1`. That was true of
    // a flow with one offer and it is an OBSTACLE and not a guard the moment there are
    // two: "Get Zadig" now carries "Open the Zadig page", which is the design's own row
    // 4, and the owner asked for a button beside the address on both screens.
    //
    // *** AND THE COUNT WAS NEVER THE PROPERTY WORTH ASSERTING, WHICH IS WHY THIS IS NOT
    //     A CEILING BEING RAISED. *** A table that offers on two screens was never the
    // defect; the defect - the real, shipped one - was a WINDOW showing the button on a
    // screen the table is silent about, because gui.cpp chose the screen by kind and then
    // by "is this the machine review". Two totals compared with each other cannot see
    // that: a window showing the button on the wrong ONE screen while the table offers on
    // the right one gives 1 and 1 and passes. So the agreement is asked SCREEN BY SCREEN
    // and the totals are published beside it rather than being the verdict.
    //
    // WHERE EACH SIDE COMES FROM, unchanged: the left is the real BUTTON's
    // IsWindowVisible() read after the real setScreen(); the right is a walk over the
    // table asking which entries carry an action AND a label. One is a control and the
    // other is data.
    int            showingAction   = 0;
    const wchar_t* showingActionOn = L"(none)";
    int            tableSaysAction = 0;
    int            actionDisagreed = 0;
    const wchar_t* actionDisagreedOn = L"(none)";
    // The screens whose secondary really reads Cancel, in flow order, collected off
    // the control itself. See the block beside the per-screen check that fills it.
    wchar_t        cancels[200];
    cancels[0] = 0;

    // Every entry the flow describes, however many that is. It used to be the four
    // values of an enum; it is now the length of the table, so a screen added in a
    // later task is photographed without this loop being told about it.
    for (int page = 0; page < screenCount(wiz); page++) {
        if (kindAt(page) == kScreenDone)
            g_outcome = kOutcomeOk;
        setScreen(page);
        UpdateWindow(g_page);
        UpdateWindow(frame);

        RECT p;
        GetClientRect(g_page, &p);
        int pw = p.right, ph = p.bottom;

        // What the renderer says it needs.
        HDC dc = GetDC(g_page);
        int reported = 0;
        switch (kindAt(page)) {
        case kScreenInfo:  reported = renderInfoScreen(dc, pw, ph, true);  break;
        case kScreenCheck: reported = renderCheckScreen(dc, pw, ph, true); break;
        case kScreenWork:  reported = renderWorkChrome(dc, pw, true);      break;
        default:           reported = renderDoneChrome(dc, pw, true);      break;
        }
        ReleaseDC(g_page, dc);

        // The foot band's note is painted for page 2 and for page 3, and it is page
        // 2's that shares the band with the new button. Measured here, where the
        // window really is ON the check screen, because footNote() answers according
        // to the screen the window is on.
        //
        // *** ASKED OF THE SCREEN AND NOT OF THE KIND, SINCE THERE ARE TWO CHECK
        //     SCREENS. *** All three of these are about page 2's furniture - the
        // walkthrough pane, the Zadig picture, the "Check again" and action buttons
        // and the note that shares the band with them. On a screen that carries its
        // own subject there is no pane, no picture and no action, so run here they
        // would not be measuring a weaker version of the same thing: they would be
        // failing, correctly, about a screen none of them is about.
        // *** THE PANE IS NOT IN THIS LIST ANY MORE. *** measureReviewPane() was here
        // and it is gone with the pane it measured: page 2 carried the walkthrough in
        // a pane while its rows scrolled in the strip above, which is the two
        // scrolling surfaces on one screen that the owner opened this round with. The
        // pane's checks and its at-rest picture moved to the screen that has the pane,
        // below - they did not shrink, they followed their subject.
        if (reviewAt(page)) {
            measureFootNote(dpi);

            // *** THE SCREEN'S OWN WORDS ARE REALLY PAINTED, MEASURED AND NOT
            //     ASSERTED. ***
            //
            // This screen is the one entry rendered by renderReview() rather than
            // renderSubject(), and until this round renderReview() painted nothing
            // from the entry at all - so a screen that set bullets got no bullets and
            // no complaint. That is the class this project has paid for five times,
            // and a check that reads Screen::bullets and finds the sentence in it
            // would pass just as happily against a renderer that ignores the field:
            // both sides would be the same constant.
            //
            // So the field is REMOVED and the screen re-measured. WHERE EACH SIDE
            // COMES FROM: both are renderCheckScreen()'s own answer, one with the
            // entry's bullets and one without, and nothing in this block reads the
            // strings. If the renderer does not read them the two numbers are equal
            // and this goes red. The cost of the sentence at this DPI is printed,
            // because it is spent out of a screen that is already over its strip.
            {
                Screen* self = &g_wiz->screens[page];
                const wchar_t* saved[4];
                for (int b = 0; b < 4; b++) {
                    saved[b]       = self->bullets[b];
                    self->bullets[b] = 0;
                }
                HDC bdc = GetDC(g_page);
                int without = renderCheckScreen(bdc, pw, ph, true);
                ReleaseDC(g_page, bdc);
                for (int b = 0; b < 4; b++)
                    self->bullets[b] = saved[b];

                wchar_t what[300];
                _snwprintf(what, 290,
                           L"%ddpi 2e-install: the screen's own words are really PAINTED "
                           L"and not merely set - %d px with them, %d without, so they "
                           L"cost %d",
                           dpi, reported, without, reported - without);
                what[290] = 0;
                check(saved[0] != 0 && reported > without, what);
            }

            // *** AND THE PICTURE OF THIS PAGE AS A USER FIRST MEETS IT, WHICH THIS
            //     SCREEN STOPPED HAVING WHEN IT LOST ITS PANE. *** page 2 is the screen
            // with the worst numbers in the program - 666 logical px past its strip at
            // 96 DPI and 1111 at 144 - and after the pane moved, its only remaining
            // picture was the tall render, which shows the content at a height no
            // window ever has. This is the one that shows what is actually on the
            // screen when it opens, and therefore the one that shows how much of page
            // 2 is below the fold. See savePaneAtRest().
            // *** THE NAME IS THE SCREEN'S AND NOT THE OLD PAGE'S. *** This literal read
            // "page-2" - the number of the page this wizard replaced - so it sorted ahead
            // of "page-2a-mixer" for the same reason the "2-checks" slug did, and named a
            // page that has not existed since Task 1. It is the second of the two stale
            // names retired here, and it moves page-2-at-rest-96dpi.png and its 144 DPI
            // twin.
            savePaneAtRest(dpi, pw, ph, L"page-2e-install");
        }

        // *** AND THE SCREEN THIS ROUND ADDED, WHICH WAS THE FIRST ONE IN THIS PROGRAM
        //     TO CARRY A PANE OR A BUTTON OF ITS OWN - AND THE BUTTON IS GONE NOW. ***
        //
        // The same pane arithmetic page 2 gets, because it is the same arithmetic:
        // layout() reads Screen::paneText and does not know or care which screen it
        // is on. What is left to measure here is that the pane still holds THIS
        // screen's text and not the other one's - the winget offer's button and
        // address this section used to also measure are gone along with the offer,
        // see the block over kMidiTitle in setup.cpp.
        //
        // Identified by the title POINTER, like pageName(), and not by wcsstr() on
        // its words, so a rewording of the title cannot silently stop measuring it.
        if (titleAt(page) == ::kMidiTitle) {
            measureScreenPane(dpi, pw, ph, L"midi", ::kMidiPaneCaption);
            // "created through Windows MIDI Services" is step 1's, in this pane only;
            // "Options > List All Devices" is step 2 and is in the walkthrough pane
            // only. The pane control is ONE control whose text is replaced per
            // screen, so a screen whose paneText was ignored would show whatever
            // was poured in last - which is exactly what this pair catches.
            checkPaneCarriesItsOwnText(dpi, L"midi",
                                       L"created through Windows MIDI Services",
                                       L"Options > List All Devices");
            savePaneAtRest(dpi, pw, ph, L"page-2b-midi");
            // *** A PICTURE OF THIS SCREEN'S FOOT BAND. *** No contextual button any
            // more - only "Check again" - but the picture is still worth having,
            // for the same reason every screen's foot band is: a defect the owner
            // found in two seconds once lived in a button that appeared in no
            // committed image.
            saveFootBand(dpi, L"foot-band-midi");
        }

        // *** THE SCREEN THIS ROUND ADDS, AND IT IS THE ONLY ONE IN THIS PROGRAM WHOSE
        //     SUBJECT IS A PAIR OF CONTROLS RATHER THAN A SENTENCE. ***
        //
        // Identified by the title POINTER, like pageName(), and not by wcsstr() on its
        // words - a rewording of a title must not silently stop measuring the screen it
        // names.
        if (titleAt(page) == ::kDeviceTitle) {
            measureModelChoice(dpi);
            // *** AND THE ROW THIS SCREEN'S WHOLE HISTORY IS ABOUT, MEASURED IN PIXELS
            //     RATHER THAN IN THE STRUCT. *** Four rounds of guards on that one
            //     sentence all read what describeModel() wrote and never what drawRow()
            //     paints; the last re-review named that as the root and an emptied heading
            //     took the required sentence off the shipped screen with 841 checks green.
            //     See the block over measureModelRowOnThePage().
            measureModelRowOnThePage(dpi, page);
            // *** WHAT THE CHOICE ITSELF COSTS THE STRIP, AND IT IS THE SAME INSTRUMENT
            //     2-checks USES ON ITS BULLETS. *** Rendered twice, once with the entry's
            // two labels and once with neither, and the difference published. It proves
            // two things at once: that renderSubject() really reads choiceLabels - a
            // field that got nothing would make the two numbers equal, which is the
            // "declared and unread" shape three fields in this table have had - and how
            // many of this screen's pixels are the controls rather than the prose. The
            // second half is what a later round needs when it has to find two pixels.
            //
            // WHERE EACH SIDE COMES FROM: both are renderCheckScreen()'s own answer, and
            // nothing here reads the strings. The labels are put back before anything
            // else looks at the table.
            {
                Screen* self = &g_wiz->screens[page];
                const wchar_t* keep0 = self->choiceLabels[0];
                const wchar_t* keep1 = self->choiceLabels[1];
                self->choiceLabels[0] = 0;
                self->choiceLabels[1] = 0;
                HDC cdc = GetDC(g_page);
                int without = renderCheckScreen(cdc, pw, ph, true);
                ReleaseDC(g_page, cdc);
                self->choiceLabels[0] = keep0;
                self->choiceLabels[1] = keep1;

                wchar_t whatC[320];
                _snwprintf(whatC, 300,
                           L"%ddpi 2ab-device: the two option labels are really MEASURED "
                           L"into the page - %d px with them, %d without, so the choice "
                           L"costs %d and the prose is the other %d",
                           dpi, reported, without, reported - without, without);
                whatC[300] = 0;
                check(keep0 != 0 && reported > without, whatC);
            }
            // *** THE AT-REST PICTURE, AND ON THIS SCREEN IT IS HALF THE STORY BY
            //     CONSTRUCTION. *** The two radio buttons are child windows and are in
            // neither this picture nor the tall render, so what this shows is the title,
            // the rule, the row and the three bullets with a GAP where the choice is.
            // That gap is the reserved box measureModelChoice() just asserted, and
            // saying so here is what stops the next reader filing the gap as a defect.
            savePaneAtRest(dpi, pw, ph, L"page-2ab-device");
            // Its foot band, like every screen this redesign added: this one carries
            // "Check again" - it is a check screen and a re-check really can change what
            // it says - beside Back and Next, and no contextual offer.
            saveFootBand(dpi, L"foot-band-device");
        }

        // *** THE TWO SCREENS THE ZADIG STEP SPLITS INTO, AND THE PICTURE IS ON THE
        //     SECOND. ***
        //
        // Identified by the title POINTER, like pageName(), and not by wcsstr() on
        // their words - a rewording of a title must not silently stop measuring the
        // screen it names.
        if (titleAt(page) == ::kZadigTitle) {
            // Get Zadig has a pane and the binding screen cannot have one, so the
            // procedure lives here. The pair below is what catches the ONE control
            // being poured full of the wrong screen's text: "Options > List All
            // Devices" is step 2 and is this screen's, and "SHA-256" is step 1 and is
            // the MIDI port screen's.
            measureScreenPane(dpi, pw, ph, L"zadig", ::kZadigPaneCaption);
            checkPaneCarriesItsOwnText(dpi, L"zadig", L"Options > List All Devices",
                                       L"SHA-256");
            savePaneAtRest(dpi, pw, ph, L"page-2c-zadig");
            // *** THE ADDRESS BLOCK WAS BUILT HERE FIRST, WHERE THERE WAS SLACK. *** The
            // arrangement is the same on both screens and this one HAD 53 logical pixels
            // of room at 96 DPI and 60 at 144 with nothing having to leave it, which is
            // why it was got right here before the MIDI port screen, where a line costs a
            // line. It went 9 pixels over at 144 DPI on the first attempt, which is what
            // moved the lead onto the address's own line - see renderSubject().
            //
            // *** IT IS THE TIGHTEST SCREEN IN THE FLOW AT 144 DPI NOW, WHICH IS THE
            //     OPPOSITE OF WHAT THAT SENTENCE READS LIKE IN THE PRESENT TENSE. *** The
            // block spent 31 of the slack at 96 DPI and 46 at 144, leaving 22 and 14
            // against the MIDI port screen's 23 and 23. The deficit table below is the
            // measurement; this note is so that a round hunting for room reads it before
            // choosing where to look.
            measureAddressBlock(dpi, page, L"zadig", bcdsetup::kZadigDownloadPage);
            // *** AND THE SCREEN THE OWNER POINTED AT. *** "No passo do zadig tem o
            // botao la embaixo para ir para o site, mas o link azul no comeco da pagina
            // deveria ter link tambem."
            measureAddressLink(dpi, page, L"zadig");
            // *** AND THIS SCREEN'S BUTTON, WHICH IS THE SECOND CONTEXTUAL BUTTON IN THIS
            //     FLOW AND THE FIRST ON A SCREEN WITH NO "Check again". *** It has one
            // label and it is measured against that one - the loopMIDI pair cannot land
            // here. This is also the call that pins the x on a screen where the re-check
            // button is hidden, which is where actionBand()'s old "does the FLOW have a
            // re-check" arithmetic would have left a 114 pixel hole.
            {
                static const wchar_t* const zadigLabels[1] = { ::kZadigOpenPageLabel };
                measureActionButton(dpi, page, L"zadig", zadigLabels, 1);
            }
            saveFootBand(dpi, L"foot-band-zadig");
        }

        if (titleAt(page) == ::kBindingTitle) {
            // *** THE MEASUREMENT THIS WHOLE ROUND EXISTS FOR. *** It is called HERE
            // and nowhere else, and until this round the only assertion covering the
            // "skip a picture too wide rather than squeezing it" rule was
            // measureZadigShot() under `if (reviewAt(page))` - so moving the picture
            // to a screen renderSubject() draws would have handed that rule to the
            // uncovered renderer. Both calls are on this screen now.
            measureZadigShot(dpi, pw);
            measureBindingShot(dpi);
            measureOverrideButton(dpi);
            measureButtonsBackOnRefusal(dpi);
            // *** AND THE PICTURE OF THIS SCREEN AS A USER FIRST MEETS IT. *** This
            // is the capture the whole task answers to: page-2's at-rest picture is
            // what showed the screenshot arriving entirely below the fold, and this
            // is the one that shows where it arrives now.
            savePaneAtRest(dpi, pw, ph, L"page-2d-binding");
            // *** ITS FOOT BAND, IN THE ROUND THAT ADDS THE SCREEN. *** The defect
            // the owner found in two seconds lived in a button that was in no
            // committed image, and this band carries a greyed Next and the widest
            // button this program has ever put in it.
            saveFootBand(dpi, L"foot-band-binding");
            // *** AND THE SAME QUESTION OF THE FOUR MACHINES THE CAPTURES ARE NOT OF.
            //     *** LAST in this block, because it drives the table through four
            // other readings and puts it back; the pictures above must be of the one
            // machine shotState() invented. See the block above the function.
            measureShotOverReadings(dpi, page);
        }

        // ===================================================================
        // *** THE TABLE'S WORD AND THE CONTROL'S WORD ARE THE SAME WORD. ***
        //
        // primaryLabelFor() is what every check about Rule 2 asks, and every one of
        // them would be worth nothing if the window put a different string in the
        // control. It does not, because refreshButtons() calls SetWindowTextW()
        // before any showOnly() - but "it does not" was a property of two lines of
        // gui.cpp that nothing measured, on a program whose defining defect was a
        // button whose words and whose deed disagreed.
        //
        // WHERE EACH SIDE COMES FROM: the left is read back out of the real BUTTON
        // window with GetWindowTextW after the real setScreen() ran; the right is
        // the pure function. One is a control, the other is a table.
        //
        // It runs on every screen, INCLUDING the two where the button is then
        // hidden, because a label written only where it shows would let the table
        // describe a control the window never makes.
        {
            wchar_t onCtl[160];
            onCtl[0] = 0;
            GetWindowTextW(g_primary, onCtl, 160);
            const wchar_t* tableSays = primaryLabelFor(wiz, page, g_finished);
            wchar_t said[400];
            _snwprintf(said, 390,
                       L"%ddpi %s: the button really carries the word the table says "
                       L"(control '%s', table '%s')", dpi, pageName(page), onCtl,
                       tableSays ? tableSays : L"(null)");
            said[390] = 0;
            check(tableSays != 0 && wcscmp(onCtl, tableSays) == 0, said);
        }

        // ===================================================================
        // *** AND THE SECOND BUTTON'S WORD IS THE SAME KIND OF PROMISE. ***
        //
        // Rule 2 was applied to the primary and nothing ever asked it of the
        // secondary, so a defect of exactly the shape the rule exists to remove sat
        // in this window until a foot band capture was looked at: on "Get Zadig" the
        // control read Cancel and its press turned the page BACK, because the label
        // was written inside `if (onKind(kScreenCheck))` and the action was not.
        //
        // WHERE EACH SIDE COMES FROM: the left is read back out of the real BUTTON
        // with GetWindowTextW after the real setScreen(); the right is the product's
        // own secondaryGoesBack(), which is the expression IDC_SECONDARY asks before
        // it moves. One is a control and the other is the rule the press obeys, so a
        // window whose word and whose deed disagree cannot pass this.
        // ===================================================================
        {
            wchar_t onCtl[160];
            onCtl[0] = 0;
            GetWindowTextW(g_secondary, onCtl, 160);
            const wchar_t* wants = secondaryGoesBack() ? L"Back" : L"Cancel";
            wchar_t said[400];
            _snwprintf(said, 390,
                       L"%ddpi %s: the second button says what its press does - it "
                       L"reads '%s' and pressing it %s", dpi, pageName(page), onCtl,
                       secondaryGoesBack() ? L"goes back a screen" : L"closes");
            said[390] = 0;
            check(wcscmp(onCtl, wants) == 0, said);

            // *** AND WHICH SCREENS THOSE ARE, COLLECTED, BECAUSE THE README MAKES A
            //     CLAIM ABOUT THE SET AND THE CLAIM WAS WRONG. *** The check above is
            // per screen and can hold on all nine while a document says something
            // false about the shape of the answer - which it did:
            // installer\README.md said the secondary reads Back "on every screen
            // after" the opening, and on the work screen it reads Cancel and always
            // has. The window is right; the sentence was wrong; the foot band capture
            // this wave added is what made it visible. So the SET is published and
            // asserted after the loop.
            if (wcscmp(onCtl, L"Cancel") == 0) {
                if (cancels[0])
                    wcsncat(cancels, L", ", 199 - wcslen(cancels));
                wcsncat(cancels, pageName(page), 199 - wcslen(cancels));
                cancels[199] = 0;
            }
        }

        // For the two verdicts after the loop. The button's state is read HERE
        // because it is a property of the screen the window is on, and setScreen()
        // has just put the window on this one.
        names[page] = pageName(page);
        // *** TWO REASONS A PRIMARY CAN BE GREY, AND THEY ARE COUNTED APART. ***
        // /preview refuses the press that WRITES; the binding screen refuses the
        // press that LEAVES it, on a machine that is not bound. Counting both into
        // one number was what the round that added the block found: "exactly one
        // button is refused" went red with 2, and raising it to 2 would have made the
        // check unable to tell "/preview greys Install" from "something greyed a Next
        // nobody meant to block". A screen refused for BOTH reasons is attributed to
        // the block, which is the stronger statement and the one with a door beside it.
        if (IsWindowVisible(g_primary) && !IsWindowEnabled(g_primary)) {
            if (!nextAllowed(wiz, page)) {
                blocked++;
                blockedOn = pageName(page);
            } else {
                refused++;
                refusedOn = pageName(page);
            }
        }
        // The control's own visibility, on this screen, after the real setScreen()
        // ran - and separately what the TABLE says about this entry. Two counts and
        // not one, because the verdict below is that they agree.
        bool ctlOffers = (g_actionBtn != 0 && IsWindowVisible(g_actionBtn));
        bool tblOffers = actionAt(page);
        if (ctlOffers) {
            showingAction++;
            showingActionOn = pageName(page);
        }
        if (tblOffers)
            tableSaysAction++;
        // The screen by screen half, which is the one the totals cannot express: a
        // window that showed the button on the wrong screen while the table offered on
        // the right one gives equal totals.
        if (ctlOffers != tblOffers) {
            actionDisagreed++;
            if (actionDisagreed == 1)
                actionDisagreedOn = pageName(page);
        }

        // *** AND A PICTURE OF THE BAND ON THE OPENING SCREEN. *** The only foot
        // band capture there has ever been is taken on the check screen, where the
        // primary button says Install and always did. The button the owner objected
        // to was on the OPENING, and it was in no committed image at all - which is
        // how sixteen tracked captures and forty-seven pixel checks looked straight
        // past it for six rounds. This is the picture that has it.
        //
        // *** AND THE KIND STOPPED BEING ENOUGH FOR IT IN THIS ROUND, WHICH IS THE
        //     SECOND TIME THAT HAPPENED HERE AND WAS FOUND BY ARITHMETIC. *** "Get
        // Zadig" measures nothing, so it is kScreenInfo too - and this line wrote ITS
        // band over the opening's, at both DPIs, into a TRACKED capture. git status
        // showed foot-band-opening as modified in a round that changed nothing about
        // the opening, and the picture ledger showed 42 promises against 40 files:
        // two saves, one filename. A capture whose name says one screen and whose
        // pixels are another is worse than no capture, because it is evidence.
        //
        // So it asks the same field the renderer asks. pageName() was corrected the
        // same way in the same round, and both are the same lesson: the kind stopped
        // identifying a screen the moment there were two of one kind.
        if (kindAt(page) == kScreenInfo && wiz->screens[page].paintsOpening)
            saveFootBand(dpi, L"foot-band-opening");

        // *** AND THE BAND ON THE SCREEN THIS ROUND ADDED, FOR THE SAME REASON. ***
        //
        // Band captures were taken on the opening and on the review screen only. The
        // mixer screen's band - which button stands in it, what its word is, whether
        // a note shares the strip - was therefore in no committed image, on a screen
        // that is new. That is this project's defining defect stated exactly: a button
        // that appeared in no picture, that the owner saw in two seconds, and that
        // sixteen captures and forty-seven pixel checks were structurally blind to
        // because not one of them was pointed at the strip it stood in. The rule this
        // line makes concrete: a round that adds a screen adds a picture of that
        // screen's band, in the same round.
        //
        // Identified by the title POINTER, like pageName(), and not by wcsstr() on
        // its words - so a rewording of the title cannot silently stop taking it.
        if (titleAt(page) == ::kMixerTitle)
            saveFootBand(dpi, L"foot-band-mixer");

        wchar_t png[512];
        _snwprintf(png, 500, L"%s\\page-%s-%ddpi.png", g_shotDir,
                   pageName(page), dpi);
        InkBox ink = renderTall(page, pw, ph, reported, png, true);

        wchar_t what[256];
        wprintf(L"  page %-11s pw=%4d ph=%4d reported=%4d ink=(%d,%d)-(%d,%d)\n",
                pageName(page), pw, ph, reported, ink.left, ink.top, ink.right,
                ink.bottom);

        _snwprintf(what, 250, L"%ddpi %s: something was drawn at all", dpi, pageName(page));
        check(ink.any, what);

        // *** THE CLIPPING TEST, HALF ONE: no ink below what the renderer
        //     reports, so the scroll range covers everything that is painted.
        _snwprintf(what, 250,
                   L"%ddpi %s: no ink below the reported content height (%d <= %d)",
                   dpi, pageName(page), ink.bottom, reported);
        check(ink.bottom < reported, what);

        // ... and no ink wider than the page, so nothing is cut at the right edge.
        _snwprintf(what, 250, L"%ddpi %s: no ink past the right edge (%d < %d)",
                   dpi, pageName(page), ink.right, pw);
        check(ink.right < pw, what);

        // *** THE CLIPPING TEST, HALF TWO: everything reported is reachable -
        //     either it fits, or the scroll bar's range covers it.
        //
        // *** AND THE VIEWPORT IS NOT ALWAYS THE PAGE, WHICH IS NEW. *** Page 2's
        // text pane takes the bottom of the page and does not scroll, so what
        // scrolls is the strip above it. This used to compare against ph, and on
        // that page ph is not the area that moves: the honest form of the same
        // question is asked of the real viewport. g_viewH is the layout's own
        // number, and measureReviewPane() checks it against the pane's geometry
        // rather than taking it on trust - so this is not a check relaxed to fit,
        // it is a check pointed at the right rectangle and pinned from the side.
        SCROLLINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE;
        GetScrollInfo(g_page, SB_VERT, &si);
        int  viewH     = g_viewH;
        bool reachable = (reported <= viewH) ||
                         (si.nMax >= reported - 1 && (int)si.nPage >= viewH);
        _snwprintf(what, 250,
                   L"%ddpi %s: all of it reachable (fits=%d, range=%d..%d page=%u, "
                   L"viewport %d of %d)",
                   dpi, pageName(page), reported <= viewH ? 1 : 0, si.nMin, si.nMax,
                   si.nPage, viewH, ph);
        check(reachable, what);

        // -------------------------------------------------------------------
        // RULE 1, MEASURED ON THIS SCREEN. See the block above the loop.
        //
        // WHERE EACH SIDE COMES FROM: the left is what the renderer says it needs,
        // asked of the product's own render*() with measure=true; the right is the
        // strip of window the layout gave it, read back off the layout's own g_viewH
        // - which measureReviewPane() pins against the pane's real rectangle rather
        // than taking on trust. Neither is computed from the other.
        // -------------------------------------------------------------------
        int deficit = reported - viewH;
        if (deficit < 0)
            deficit = 0;
        wprintf(L"  RULE 1  %-11s reported=%4d strip=%4d page=%4d deficit=%4d\n",
                pageName(page), reported, viewH, ph, deficit);
        if (deficit > 0) {
            overflowing++;
            wchar_t one[128];
            _snwprintf(one, 120, L"%s +%d; ", pageName(page), deficit);
            one[120] = 0;
            if (wcslen(deficits) + wcslen(one) < 500)
                wcscat(deficits, one);
        }

        // -------------------------------------------------------------------
        // *** THE RATCHET, ON THIS SCREEN, BY NAME. *** See allowedDeficit() for
        // every figure and for what the check it replaces could not see.
        //
        // WHERE EACH SIDE COMES FROM: the left is measured - the product's own
        // render*() asked what it needs, minus the layout's g_viewH, which
        // measureReviewPane() pins against the pane's real rectangle. The right is a
        // literal in this file, written down by a human from a previous run. Neither
        // is computed from the other, and the right hand side cannot move without an
        // edit somebody has to justify.
        // -------------------------------------------------------------------
        {
            int allowed = allowedDeficit(pageName(page), dpi);
            _snwprintf(what, 250,
                       L"%ddpi %s: overflows its strip by no more than it already did "
                       L"- %d pixels, at most %d allowed (%d painted into %d)",
                       dpi, pageName(page), deficit, allowed, reported, viewH);
            check(deficit <= allowed, what);
        }

        // -------------------------------------------------------------------
        // *** RULE 1 BY ITS OWN HEADING - "no screen has two things that scroll" -
        //     READ OFF TWO REAL WINDOWS AND NOT OFF ANY ARITHMETIC. ***
        //
        // The design states Rule 1 twice: the heading is "no screen has two things
        // that scroll" and the body gives the mechanism, "the page never scrolls".
        // Task 6b measured that the mechanism is not reachable on two screens - the
        // binding screen needs 399 logical pixels for its picture and the chrome above
        // it in a page of 398, and the install screen needs 433 for its title, its rule
        // and its five rows in the same 398 - so those two keep a scrolling page, and
        // that is ONE surface and legal under the heading. What is NOT legal, and what
        // the owner actually opened this redesign with, is a screen carrying BOTH.
        //
        // Nothing asserted it. The deficit ratchet above is a per screen ceiling that a
        // human writes down, and it was 19 for the MIDI port screen - a screen with a
        // pane whose page was scrolling, which is the defect itself, sitting green
        // inside an allowance.
        //
        // WHERE EACH SIDE COMES FROM, and neither is a number: the left is the page
        // window's own WS_VSCROLL bit, which layout()'s ShowScrollBar() sets or clears;
        // the right is the pane control's own visibility, which measureScreenPane()
        // separately asserts really is up on the screens whose entry asks for one. Two
        // live windows, no shared derivation, and no way for this to pass by both sides
        // reading the same variable.
        // -------------------------------------------------------------------
        {
            bool pageBar = (GetWindowLongW(g_page, GWL_STYLE) & WS_VSCROLL) != 0;
            bool paneUp  = g_review != 0 && IsWindowVisible(g_review);
            _snwprintf(what, 250,
                       L"%ddpi %s: ONE scrolling surface, not two - page bar %d, pane "
                       L"%d (%d painted into a %d strip of a %d page)",
                       dpi, pageName(page), pageBar ? 1 : 0, paneUp ? 1 : 0, reported,
                       viewH, ph);
            check(!(pageBar && paneUp), what);

            // ...and the bar is up exactly when the painted content does not fit, so
            // that the bit above means what this file reads it as meaning.
            //
            // *** THIS ONE IS A CANARY AND NOT AN INDEPENDENT MEASUREMENT, AND SAYING SO
            //     IS THE POINT OF THIS PARAGRAPH. *** The first draft described it as
            // "the real window against the renderer's answer versus the layout's own
            // strip", which reads as more independence than there is. The truth: the left
            // side is set by ONE expression, ShowScrollBar(g_page, SB_VERT, g_contentH >
            // g_viewH) in layout(), and the right side is that same comparison recomputed
            // here from the same renderer at the same width. The only slack between them
            // is the two-pass width race layout() documents. So it can fail - proved, by
            // forcing that one call to FALSE, and it goes red on all four overflowing
            // screens - but it fails only to that call being wrong, NOT to any defect in
            // how the page is measured.
            //
            // It is kept because that is exactly the sabotage it needs to catch: the
            // check ABOVE reads the same bit and concludes something about the user's
            // experience from it, and a bit that stopped tracking the fit would make that
            // conclusion vacuous while staying green. The independent measurement is the
            // one above - two live windows - and this is what stops it being read off a
            // lie. No second source exists for "should there be a bar": the fit is the
            // only definition of it, so pointing this at a third number would be
            // inventing agreement rather than measuring it.
            _snwprintf(what, 250,
                       L"%ddpi %s: the page raises a scroll bar exactly when its "
                       L"painted content does not fit - bar %d, %d into %d",
                       dpi, pageName(page), pageBar ? 1 : 0, reported, viewH);
            check(pageBar == (reported > viewH), what);
        }

        // *** THE TWO SCREENS THAT MUST NEVER NEED A SCROLL AT ALL. *** The work and
        // done screens paint a caption, their rows and nothing else: everything long
        // on them lives in an EDIT that scrolls itself. So they already obey Rule 1
        // at both DPIs, and a later task that painted a paragraph onto either of them
        // would be reintroducing on a new screen precisely the accumulation that made
        // page 2 unreadable. Asserted per screen rather than folded into the count
        // below, so the failure names which one grew.
        if (kindAt(page) == kScreenWork || kindAt(page) == kScreenDone) {
            _snwprintf(what, 250,
                       L"%ddpi %s: obeys Rule 1 - everything painted fits the page "
                       L"with no scroll (%d needed, %d given)",
                       dpi, pageName(page), reported, viewH);
            check(deficit == 0, what);
        }

        // The viewport is the page itself on every page that has no pane taking part
        // of it. Asserted so that "the viewport" cannot quietly become a number that
        // makes the check above pass on a page where nothing was supposed to change.
        //
        // *** AND IT IS ASKED OF THE PANE AND NOT OF THE SCREEN'S IDENTITY. *** This
        // excluded every kScreenCheck once, because the only one there was owned the
        // pane; then it excluded the machine review by name. Both were the same
        // mistake at different sizes - a pane is not a property of WHICH screen it is
        // on, it is a property of an entry asking for one, and as of this round two
        // entries ask. A screen that does not ask must get the whole page: if one ever
        // grew a pane without saying so, this is what says it, and the deficit above
        // would otherwise be measured against a strip instead of a page.
        if (!paneAt(page)) {
            _snwprintf(what, 250,
                       L"%ddpi %s: the whole page scrolls - viewport %d of %d",
                       dpi, pageName(page), viewH, ph);
            check(viewH == ph, what);
        }

        // *** THE PANE FILLS THE PAGE, WHICH IS NOT THE SAME AS BEING INSIDE IT. ***
        //
        // "Inside the page" was already checked for the summary pane and it passed
        // while the pane was 17 pixels narrower than its page - a scroll bar's width,
        // left over from layout() having measured the page before deciding whether the
        // bar was there. That defect is in the captures this file committed. So the
        // margins are checked on both sides, against the number the layout says it
        // uses, which is what makes the leftover visible instead of merely legal.
        if (kindAt(page) == kScreenWork || kindAt(page) == kScreenDone) {
            HWND pane = (kindAt(page) == kScreenWork) ? g_log : g_summary;
            RECT wr;
            GetWindowRect(pane, &wr);
            MapWindowPoints(HWND_DESKTOP, g_page, (POINT*)&wr, 2);
            int m = S(kMargin);
            _snwprintf(what, 250,
                       L"%ddpi %s: the pane FILLS the page's width - it spans %d..%d of "
                       L"a %d wide page, margins wanted %d", dpi, pageName(page),
                       (int)wr.left, (int)wr.right, pw, m);
            check((int)wr.left == m && (int)wr.right == pw - m, what);
        }

        if (kindAt(page) == kScreenDone) {
            // The pane that carries the warnings has to lie inside the page, and
            // it has to WRAP rather than cut: an EDIT with ES_MULTILINE and no
            // ES_AUTOHSCROLL wraps, and a wrapped pane reports more lines than
            // the text has hard breaks.
            RECT sr;
            GetWindowRect(g_summary, &sr);
            RECT pr;
            GetWindowRect(g_page, &pr);
            _snwprintf(what, 250, L"%ddpi finished: the summary pane is inside the page",
                       dpi);
            check(sr.top >= pr.top && sr.bottom <= pr.bottom &&
                  sr.left >= pr.left && sr.right <= pr.right, what);

            // *** WHY THE WARNINGS CANNOT BE CUT, STATED AS TWO STYLE BITS. ***
            // An EDIT with ES_MULTILINE and WITHOUT ES_AUTOHSCROLL word wraps
            // instead of running off the right edge; WS_VSCROLL makes whatever
            // does not fit vertically reachable. Both are properties of the
            // control, not of the text, so they hold for any DPI and any window
            // size - which is a stronger statement than any screenshot.
            DWORD st = (DWORD)GetWindowLongPtrW(g_summary, GWL_STYLE);
            _snwprintf(what, 250,
                       L"%ddpi finished: the pane WRAPS (no ES_AUTOHSCROLL) so no line "
                       L"can be cut at the right edge", dpi);
            check((st & ES_AUTOHSCROLL) == 0 && (st & ES_MULTILINE) != 0, what);
            _snwprintf(what, 250,
                       L"%ddpi finished: the pane has WS_VSCROLL so the overflow is "
                       L"reachable", dpi);
            check((st & WS_VSCROLL) != 0, what);

            int lines = (int)SendMessageW(g_summary, EM_GETLINECOUNT, 0, 0);
            int lh    = lineHeight(GetDC(g_page), g_fMono);
            int shown = lh > 0 ? (sr.bottom - sr.top) / lh : 0;
            _snwprintf(what, 250,
                       L"%ddpi finished: nothing lost in layout (%d laid out lines from "
                       L"%d said lines, about %d visible at rest)",
                       dpi, lines, g_capN, shown);
            check(lines >= g_capN, what);

            int first = (int)SendMessageW(g_summary, EM_GETFIRSTVISIBLELINE, 0, 0);
            _snwprintf(what, 250,
                       L"%ddpi finished: the pane opens at the TOP (first visible line %d)",
                       dpi, first);
            check(first == 0, what);

            _snwprintf(what, 250, L"%ddpi finished: the painted notice is present", dpi);
            check(wiz->doneNotice != 0 && wiz->doneNotice[0] != 0, what);

            // *** AND THE MODEL LINE IS IN THE REAL PANE, WHICH IS THE HALF OF I3 THE
            //     TEXT SUITE CANNOT REACH. *** The text suite proves printSummary() SAYS
            // which mixer the run was set up for. The report claimed the line "reaches the
            // window's own pane", and nothing asserted it: that claim is about a control,
            // so it has to be read off the control. scrollPaneTo() searches the EDIT's own
            // text, so this is the sentence arriving where a person reads it.
            //
            // The default run is the proven model, so the phrase searched is the part both
            // branches share - "was set up for" - and not the experimental warning, which
            // is the other branch and is asserted in the text suite.
            int modelLine = scrollPaneTo(g_summary, L"was set up for");
            _snwprintf(what, 250,
                       L"%ddpi finished: which mixer this run was set up for is IN the "
                       L"summary pane (line %d of %d), not only in what printSummary said",
                       dpi, modelLine, lines);
            check(modelLine >= 0, what);
            scrollPaneTo(g_summary, L"SUMMARY");

            // A picture of the part of the pane the eye does not see at rest.
            int bannerLine = scrollPaneTo(g_summary, L"NEXT STEPS AND WARNINGS");
            _snwprintf(what, 250,
                       L"%ddpi finished: the warnings banner is in the pane (line %d of %d)",
                       dpi, bannerLine, lines);
            check(bannerLine >= 0, what);
            UpdateWindow(g_summary);
            _snwprintf(png, 500, L"%s\\page-4-warnings-%ddpi.png", g_shotDir, dpi);
            renderTall(page, pw, ph, reported, png, true);
            scrollPaneTo(g_summary, L"SUMMARY");
        }

        // ===================================================================
        // *** THE TWO BANDS THAT WERE IN NO COMMITTED IMAGE AT ALL, AND THEY ARE THE
        //     TWO THE DESIGN MAKES ITS NAVIGATION PROMISES ABOUT. ***
        //
        // Seven band variants existed - opening, mixer, device, midi, zadig, binding
        // and the plain one taken on the install screen - and NONE of them was the
        // work screen or the finished screen. So:
        //
        //   Section 4's "Back always works, except on screen 6 while it is writing -
        //   and there the button is greyed beside the sentence saying why" had never
        //   been photographed, on either half of the claim.
        //
        //   Rule 2's Close - the word the last screen's button wears - had never been
        //   photographed either, on the one screen where the secondary is hidden
        //   entirely and the primary is the only control in the band.
        //
        // This is the same defect the owner found in two seconds twice: a control that
        // appears in no picture. The rule the round that added foot-band-mixer wrote
        // down - "a round that adds a screen adds a picture of that screen's band, in
        // the same round" - was never applied backwards to the two screens that were
        // already there.
        //
        // *** THE WORK SCREEN IS PHOTOGRAPHED WHILE IT IS WRITING, WHICH IS THE ONLY
        //     STATE THE DESIGN SAYS ANYTHING ABOUT. *** footNote() returns
        //     cannotCancelNote on `onKind(kScreenWork) && g_working`, so a band taken
        // with the flag clear would show a greyed Cancel beside SILENCE - which is
        // exactly the half of section 4 that is a promise ("greyed BESIDE THE SENTENCE
        // SAYING WHY") and exactly what a picture of the idle work screen would fail
        // to contain. The flag is set for the length of one BitBlt and put back, and
        // nothing else in this loop reads it: the buttons' enabled state on this
        // screen does not depend on it (refreshButtons() greys the secondary on the
        // KIND), so no measurement above or below moves.
        if (kindAt(page) == kScreenWork) {
            bool was = g_working;
            g_working = true;
            saveFootBand(dpi, L"foot-band-work");
            g_working = was;
        }
        if (kindAt(page) == kScreenDone)
            saveFootBand(dpi, L"foot-band-finished");

        // ===================================================================
        // WHERE THE INSTALL SCREEN'S FOOTER ACTUALLY STARTS, WHICH NOBODY HAD
        // WRITTEN DOWN.
        //
        // *** A CHECK STOOD HERE TOO AND WENT WITH THE SECOND FOOTER. *** The footer
        // used to be one of two sentences, the second appearing only on a machine
        // where somebody had taken the third party offer this session. No capture
        // renders that state, so "the captures did not change" said nothing about it,
        // and this block rendered it on purpose - at both DPIs, with measure=true -
        // and asserted the two heights were EQUAL, so that a longer sentence could
        // not be paid for by raising the ratchet. That was a real check and the
        // reasoning behind it is preserved over kReviewFooterUntouched in setup.cpp,
        // where a replacement sentence's author will read it. It is deleted because
        // the state it rendered is one this program can no longer enter: there is one
        // footer now, `wiz->reviewFooter` at rest already IS it, and re-rendering the
        // same string to prove it costs nothing against itself is a comparison whose
        // two sides are one value.
        //
        // WHAT REMAINS IS THE LEDGER LINE, AND IT WAS NEVER A CHECK. Render the same
        // screen with the footer suppressed and the difference is the room it takes,
        // so `reported` minus that is where it begins. It is below the fold at rest,
        // which is the owner-accepted page scroll on this screen; a wprintf cannot be
        // mistaken for a guarantee, and what it stops is the next round rediscovering
        // this or believing the footer is read on arrival.
        //
        // NO CAPTURE AND NO NEW NAME: measure=true renders nothing and writes nothing,
        // so this cannot collide with pageName() or add a file to the ledger. The
        // footer is put back before anything else reads it.
        // ===================================================================
        if (titleAt(page) == ::kInstallTitle) {
            const wchar_t* was = wiz->reviewFooter;
            wiz->reviewFooter = 0;
            HDC dc3 = GetDC(g_page);
            int noFooter = renderCheckScreen(dc3, pw, ph, true);
            ReleaseDC(g_page, dc3);
            wiz->reviewFooter = was;

            wprintf(L"  FOOTER  %ddpi 2e-install: at rest %d (deficit %d); it takes "
                    L"%d and begins at %d of a %d strip, so it is %d BELOW THE FOLD\n",
                    dpi, reported, deficit,
                    reported - noFooter, noFooter, viewH, noFooter - viewH);
        }
    }

    // ===================================================================
    // WHAT RULE 1 STILL COSTS, AS ONE LINE PER DPI.
    //
    // *** THIS IS A LEDGER ENTRY AND IT IS NOT A CHECK - DELIBERATELY, NOW. *** It
    // used to be one: `overflowing <= allowedOverflow`, a count of screens with no
    // screen identity in it and with the deficits interpolated into its text but
    // asserted nowhere. Both of its holes are written out in full above
    // allowedDeficit(). The verdict moved into the loop, where it can name the
    // screen; what is left here is the sentence a reader wants at the end of a pass,
    // and a wprintf cannot be mistaken for a guarantee.
    //
    // The design says the page never scrolls. It still does, on the screen the owner
    // complained about and by 12 pixels on the opening. Tasks 3 to 6 split the
    // content that causes it and Task 6b removes the page scroll; until then these
    // are the numbers those tasks are working down, and allowedDeficit() is what
    // stops them going up on the way.
    // ===================================================================
    wprintf(L"  RULE 1  %ddpi: %d of %d screens overflow their strip (%s)\n",
            dpi, overflowing, screenCount(wiz),
            deficits[0] ? deficits : L"none");

    // ===================================================================
    // *** THE THREE SCREENS WHOSE SECONDARY READS Cancel, BY NAME. ***
    //
    // The per-screen check inside the loop compares the control against the product's
    // own secondaryGoesBack(), which is the wiring. This is the SHAPE of the answer,
    // and it exists because a document got the shape wrong while every per-screen
    // check was green: installer\README.md claimed the secondary reads Back on every
    // screen after the opening. It reads Cancel on the opening, on the work screen -
    // where the press stops rather than goes back, which is Rule 2 doing its job - and
    // on the finished screen, where the control carries the word before being hidden.
    //
    // WHERE EACH SIDE COMES FROM: the left is what GetWindowTextW read off the real
    // BUTTON on each screen after the real setScreen(), joined in flow order by
    // pageName(); the right is a list written in this file. The product cannot see
    // this literal and this file does not compute the left from the table.
    // ===================================================================
    {
        wchar_t what[400];
        _snwprintf(what, 390,
                   L"%ddpi: the secondary reads Cancel on exactly the screens where "
                   L"its press does not go back - got '%s', wanted "
                   L"'1-welcome, 3-progress, 4-finished'",
                   dpi, cancels[0] ? cancels : L"(none)");
        what[390] = 0;
        check(wcscmp(cancels, L"1-welcome, 3-progress, 4-finished") == 0, what);
    }

    // ===================================================================
    // *** NO TWO SCREENS SHARE A CAPTURE NAME, WHICH IS THE PROPERTY THAT PROTECTS
    //     THE PICTURES. ***
    //
    // The name is the PNG's filename and it is also the key into allowedDeficit(),
    // so two screens sharing one means the second overwrites the first's picture and
    // inherits its overflow budget. Until this round pageName() followed the kind and
    // that collision was one added screen away - written down above allowedDeficit()
    // as a residual and left for the task that would cause it, which is this one.
    //
    // WHERE EACH SIDE COMES FROM: the names are what pageName() answered for the
    // real table, collected in the loop above; the comparison is against each other,
    // not against a list written here - so a screen added later needs no edit to be
    // covered, and an UNNAMED one collides with any other unnamed one and fails.
    // ===================================================================
    {
        int  n    = screenCount(wiz);
        int  dupes = 0;
        wchar_t firstDupe[64];
        firstDupe[0] = 0;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
                if (names[i] && names[j] && wcscmp(names[i], names[j]) == 0) {
                    dupes++;
                    if (!firstDupe[0]) {
                        wcsncpy(firstDupe, names[i], 60);
                        firstDupe[60] = 0;
                    }
                }
        wchar_t what[400];
        _snwprintf(what, 390,
                   L"%ddpi: every screen files its capture under a name of its own - "
                   L"%d screens, %d pairs sharing one%s%s", dpi, n, dupes,
                   firstDupe[0] ? L": " : L"", firstDupe[0] ? firstDupe : L"");
        what[390] = 0;
        check(n > 0 && dupes == 0, what);

        // *** AND NO SCREEN ANSWERS TO THE PLACEHOLDER, WHICH IS THE HOLE THE DUPES
        //     COUNT LEAVES OPEN. ***
        //
        // pageName() returns 2z-UNNAMED-CHECK-SCREEN for a check screen whose title
        // is none of the flow's constants. ONE of those passes everything here: the
        // pairwise count only goes red when there are TWO, allowedDeficit() gives it
        // zero which a small screen meets anyway, and it quietly files
        // page-2z-UNNAMED-CHECK-SCREEN-96dpi.png as though that were a name. Tasks 4,
        // 5 and 6 add three screens between them, so "somebody forgot one pageName()
        // entry" is the likeliest single mistake of the next three rounds and it is
        // currently silent unless the screen also overflows.
        //
        // WHERE EACH SIDE COMES FROM: the left is what pageName() answered for the
        // real table, collected in the loop above; the right is the placeholder's own
        // prefix, written here. The product cannot see this literal - the placeholder
        // is the harness's word for a screen the harness could not identify.
        int unnamed = 0;
        for (int i = 0; i < n; i++)
            if (names[i] && wcsncmp(names[i], L"2z-UNNAMED", 10) == 0)
                unnamed++;
        _snwprintf(what, 390,
                   L"%ddpi: every screen has a name of its OWN and not the placeholder "
                   L"- %d of %d answer 2z-UNNAMED", dpi, unnamed, n);
        what[390] = 0;
        check(unnamed == 0, what);
    }

    // ===================================================================
    // *** IN A FLOW THAT WILL NOT WRITE, EXACTLY ONE BUTTON IS REFUSED: THE ONE
    //     WHOSE PRESS WOULD WRITE. ***
    //
    // This run is /preview, so startBlockedNote is set and the primary button is
    // greyed - that behaviour is the whole of what /preview shows. It used to be
    // greyed on every kScreenCheck, which was the same set while there was one check
    // screen. With two, that rule locks the wizard on the first of them: Next turns a
    // page, writes nothing, and there is no reason for /preview to refuse it. A
    // /preview that cannot be walked through is a /preview that cannot show the
    // window it exists to show.
    //
    // WHERE EACH SIDE COMES FROM: the left is read back off the real BUTTON with
    // IsWindowEnabled() after the real setScreen(); the right is the pure
    // primaryActionFor(), asked here. The note's presence is in the same condition so
    // that this cannot pass vacuously on a flow that was never blocked.
    // ===================================================================
    {
        int starts = -1;
        for (int i = 0; i < screenCount(wiz); i++)
            if (primaryActionFor(wiz, i, false) == kPrimaryStart)
                starts = i;
        wchar_t what[400];
        _snwprintf(what, 390,
                   L"%ddpi: this flow refuses to write (note %s) and refuses exactly "
                   L"one button with it - %d refused, on '%s', and the press that "
                   L"installs is on '%s'", dpi, wiz->startBlockedNote ? L"set" : L"MISSING",
                   refused, refusedOn, starts >= 0 ? pageName(starts) : L"(none)");
        what[390] = 0;
        check(wiz->startBlockedNote != 0 && starts >= 0 && refused == 1 &&
              wcscmp(refusedOn, pageName(starts)) == 0, what);
    }

    // ===================================================================
    // *** AND EXACTLY ONE SCREEN REFUSES TO BE LEFT, WHICH IS A DIFFERENT REFUSAL. ***
    //
    // nextAllowed() got its first consumer in this round, and the failure worth
    // catching is not that the binding screen blocks - the suite below asserts that
    // from a pure function. It is that NOTHING ELSE DOES. blockNextWhenUnmet is a
    // field any later entry could set, refreshButtons() greys the primary on the
    // answer for every check screen, and a second blocking screen would be a wizard a
    // user cannot walk through with no single line saying so.
    //
    // WHERE EACH SIDE COMES FROM: the left is the real BUTTON's IsWindowEnabled()
    // read on every screen after the real setScreen(), attributed to the block rather
    // than to /preview by the loop above; the right is a walk over the TABLE asking
    // which entries set the field. One is a control and the other is data.
    // ===================================================================
    {
        int wantBlock = -1;
        int setsField = 0;
        for (int i = 0; i < screenCount(wiz); i++)
            if (wiz->screens[i].blockNextWhenUnmet) {
                setsField++;
                wantBlock = i;
            }
        wchar_t what[400];
        _snwprintf(what, 390,
                   L"%ddpi: exactly ONE screen refuses to be left, and it is the one "
                   L"whose entry blocks - refused on %d screen(s) ('%s'), the table "
                   L"blocks on %d ('%s'). Without the binding nothing works; without "
                   L"loopMIDI the audio does",
                   dpi, blocked, blockedOn, setsField,
                   wantBlock >= 0 ? pageName(wantBlock) : L"(none)");
        what[390] = 0;
        check(blocked == 1 && setsField == 1 && wantBlock >= 0 &&
              wcscmp(blockedOn, pageName(wantBlock)) == 0, what);
    }

    // ===================================================================
    // *** THE CONTEXTUAL BUTTON STANDS ON EXACTLY ONE SCREEN, AND IT IS THE SCREEN
    //     WHOSE ENTRY CARRIES IT. ***
    //
    // This is the verdict on the mechanism this round built, and it is made over the
    // whole flow because the failure it exists to catch is not on the screen that
    // should have the button - it is on the ones that should not. Until this round
    // the offer was a Wizard field shown wherever gui.cpp decided, and "wherever"
    // was the machine review: a button reading "Install loopMIDI..." beside rows
    // about a USB cable.
    //
    // WHERE EACH SIDE COMES FROM: the left is the real BUTTON's IsWindowVisible(),
    // read on every screen after the real setScreen(); the right is a walk over the
    // table asking which entries carry an action AND a label. One is a control and
    // the other is data, and the check is that they name the same single screen -
    // so a table that offered on two screens fails, and so does a window that showed
    // the button on a screen the table is silent about.
    // ===================================================================
    {
        wchar_t offering[200];
        offering[0] = 0;
        for (int i = 0; i < screenCount(wiz); i++)
            if (actionAt(i)) {
                if (offering[0])
                    wcsncat(offering, L", ", 199 - wcslen(offering));
                wcsncat(offering, pageName(i), 199 - wcslen(offering));
            }
        offering[199] = 0;
        wchar_t what[400];
        _snwprintf(what, 390,
                   L"%ddpi: the contextual button stands on EXACTLY the screens whose "
                   L"entries carry it, screen by screen - %d disagreement(s) (first: "
                   L"'%s'); shown on %d, the table offers on %d ('%s')",
                   dpi, actionDisagreed, actionDisagreedOn, showingAction,
                   tableSaysAction, offering[0] ? offering : L"(none)");
        what[390] = 0;
        // The third clause is what stops this passing vacuously on a flow where the
        // control was never created and the table never offered: two zeroes agree on
        // every screen. This flow really does offer, on more than one screen since the
        // round that gave the address a button of its own.
        check(actionDisagreed == 0 && showingAction == tableSaysAction &&
              tableSaysAction >= 1, what);
    }

    // Once, and after every picture has been saved: it really re-reads the machine
    // and really replaces the rows, so running it before the captures would put a
    // second snapshot in them. The rows are put back before it returns.
    if (dpi == 96) {
        measureRecheckAndNudge(wiz);
        // ...and the click on the painted address, after that, because it runs a worker
        // too and measureRecheckAndNudge() is what puts the invented machine back.
        // ONE screen now, not two: the MIDI port screen's address went with its
        // offer - see the block over kMidiTitle in setup.cpp - so Zadig's is the
        // only painted address left in the program to click.
        wprintf(L"\n--- the painted address, clicked, with the browser taken out ---\n");
        measureAddressLinkPress(wiz, ::kZadigTitle, L"2c-zadig",
                                bcdsetup::kZadigDownloadPage);
        // ...and the one thing the call above CANNOT see, because it replaces the
        // product's opener with a spy: what the shipped function returns.
        measureTheRealOpenerReturns(wiz);
    }

    DestroyWindow(frame);
    g_frame      = 0;
    g_page       = 0;
    g_overrideBtn = 0;
    g_recheckBtn = 0;
    g_actionBtn  = 0;
    g_review     = 0;
    releasePhoto();
}

// ===========================================================================
// *** THE UNINSTALLER'S THREE SCREENS, MEASURED AND PHOTOGRAPHED. UNTIL THIS ROUND
//     THIS FLOW HAD ZERO TRACKED CAPTURES AND ZERO PIXEL MEASUREMENTS. ***
//
// Fifty PNGs in this repository and every one of them is the setup. That asymmetry is
// not a gap in coverage, it is the reason the blank page shipped: a screen that is in
// no picture cannot be seen to have emptied itself, and the only person who could see
// it was the owner, running the most destructive program in this project, on his own
// machine, at two in the morning.
//
// *** WHY THIS IS A SECOND CAMERA AND NOT AN ARGUMENT TO shootAtDpi(). *** That
// function is 700 lines about the setup: pageName()'s nine title constants,
// allowedDeficit()'s keys, the Zadig picture, the device photograph, the re-check, the
// contextual button, the named door, the model choice and the painted address. Every
// one of them is absent from this flow, and a parameter threaded through all of them
// would make the setup's camera answer questions about a window with none of that in
// it. What the two share - renderTall(), inkOf(), saveFootBand(), savePngChecked() and
// the product's own setScreen() and layout() - is shared as functions, which is where
// the sharing belongs.
//
// *** WHAT IS PHOTOGRAPHED AND WHAT IS NOT, SAID PLAINLY. *** The confirmation screen
// and the work screen are, at both DPIs, plus the foot band on the confirmation screen
// because that is where the button that removes stands. The summary screen is measured
// and NOT photographed, and there is a named skip that says why: the words in its pane
// are written by runRemoval(), which this harness must never run.
// ===========================================================================

// The three names, from the kind, because this flow has one screen of each. Named at
// all - rather than photographed by index - for pageName()'s reason: a picture whose
// filename says one screen and whose pixels are another is worse than no picture.
static const wchar_t* uninstallPageName(int p)
{
    switch (kindAt(p)) {
    case kScreenCheck: return L"1-confirm";
    case kScreenWork:  return L"2-removing";
    case kScreenDone:  return L"3-finished";
    default:           break;
    }
    return L"z-UNNAMED-UNINSTALL-SCREEN";
}

// ---------------------------------------------------------------------------
// *** RULE 1 FOR A FLOW NOBODY HAD EVER MEASURED, AND THE FIRST MEASUREMENT FOUND
//     SOMETHING. ***
//
// "The page never scrolls: if content does not fit, the pane scrolls and it is the
// only thing that does." The setup's ceilings are in allowedDeficit(); these are this
// flow's, and they are the first numbers anybody has ever taken of these three
// screens. What the round that wrote them measured:
//
//      96 DPI    1-confirm    0 (377 of 398)   2-removing 0 (235)   3-finished 0 (69)
//     144 DPI    1-confirm   12 (606 of 594)   2-removing 0 (361)   3-finished 0 (104)
//
// (The 96 DPI figure was recorded as 390 and had gone stale the same way the 144 one
// had - the row this task shortened is on this screen at both DPIs. It is 377 now.
// Nothing is allowed at 96 DPI and nothing was, so no ratchet moved with it; it is
// corrected because a recorded measurement that disagrees with the run is the thing
// this block exists to be.)
//
// *** THE 12 IS REAL, IT IS THE ONLY ONE, AND IT IS NOT PAID FOR BY DELETING WORDS.
//     *** The confirmation screen fits at 96 DPI with eight logical pixels to spare
// and is 12 over at 144. The content it is over by is the restored review - the four
// rows and the footer - and every sentence in it was reviewed: the reason the WinUSB
// binding is not undone is a statement about leaving hardware unusable, and the third
// row is the warning whose absence left the owner's machine with no ASIO driver at
// all. Trimming a reviewed sentence to buy pixels on one of two DPIs would be
// paying for a geometry figure with the thing the figure exists to protect.
//
// *** IT WAS 35, AND FIX ROUND 1 TOOK IT TO 12, WHICH IS THIS COMMENT'S OWN
//     INSTRUCTION BEING CARRIED OUT. *** The line below used to say "it is the number
// a later round takes down". Task 5 removed the teVirtualMIDI sentence from the
// "Will be kept" row, the page got shorter, and the real deficit fell from 35 to 12 -
// but the allowance did not move with it. That left 23 pixels of slack, and slack in
// a ratchet is not neutral: it is room for a future round to push content under the
// fold and still print [ok]. The measurement is `needs 606, has 594, over by 12`, and
// the allowance is now that number and not a round figure above it, so the next
// pixel of regression on this screen is red.
//
// WHAT IT COSTS INSTEAD, said plainly: at 144 DPI the last few pixels of the footer -
// "Nothing has been removed yet..." - are below the fold on arrival and the page
// scrolls to reach them. That is the honest state, it is recorded here rather than
// hidden, and 12 is now the number a later round takes down.
//
// A RATCHET AND NOT A TARGET, like the setup's: the comparison is <=, so a change that
// makes a screen fit better passes and one that pushes content under the fold fails. A
// screen not named here gets zero.
// ---------------------------------------------------------------------------
static int uninstallAllowedDeficit(const wchar_t* name, int dpi)
{
    if (wcscmp(name, L"1-confirm") == 0)
        return dpi >= 144 ? 12 : 0;
    return 0;
}

static void shootUninstallAtDpi(Wizard* wiz, int dpi)
{
    wprintf(L"\n--- the uninstaller's three screens, %d DPI ---\n", dpi);

    g_wiz     = wiz;
    g_dpi     = dpi;
    g_outcome = kOutcomeOk;
    buildFonts();

    HWND frame = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW,
                                 L"BcdWizardFrame", wiz->windowTitle,
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                 WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                                 0, 0, g_inst, 0);
    g_frame = frame;
    g_page = CreateWindowExW(WS_EX_CONTROLPARENT, L"BcdWizardPage", L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL,
                             0, 0, 10, 10, frame, 0, g_inst, 0);
    g_primary = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE |
                                WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 10, 10,
                                frame, (HMENU)IDC_PRIMARY, g_inst, 0);
    g_secondary = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE |
                                  WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
                                  frame, (HMENU)IDC_SECONDARY, g_inst, 0);
    g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP |
                            WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                            0, 0, 10, 10, g_page, (HMENU)IDC_LOG, g_inst, 0);
    g_summary = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP |
                                WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                0, 0, 10, 10, g_page, (HMENU)IDC_SUMMARY, g_inst, 0);
    g_bar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH,
                            0, 0, 10, 10, g_page, (HMENU)IDC_BAR, g_inst, 0);
    // The other five controls are NOT created, on the product's own conditions -
    // checked once, in shootUninstall(), rather than assumed here.
    SendMessageW(g_log, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(g_summary, EM_SETLIMITTEXT, 0, 0);
    // Called even though this flow has no pane: it is the product's own function and
    // its first line is the product's own answer about whether there is one.
    buildPane(g_page, wiz);
    applyFonts();

    RECT want = { 0, 0, S(kWinW), S(kWinH) };
    AdjustWindowRectEx(&want, (DWORD)GetWindowLongPtrW(frame, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongPtrW(frame, GWL_EXSTYLE));
    SetWindowPos(frame, 0, 0, 0, want.right - want.left, want.bottom - want.top,
                 SWP_NOZORDER);

    // *** THE LOG PANE'S WORDS ARE THE PRODUCT'S OWN, ON AN INVENTED MACHINE. *** In
    // the shipped program the pane on this screen holds the banner, prepare()'s
    // findings and reportPlan()'s plan - everything beginCapture() collected before
    // the window existed. prepare() reads THIS machine, so what is replayed here is
    // reportPlan(), which is the half that describes the removal rather than the host.
    // Captured by the caller through the same say() sink every text suite uses.
    for (int i = 0; i < ::g_capN; i++)
        appendTo(g_log, ::g_cap[i]);
    SendMessageW(g_log, EM_SETSEL, 0, 0);
    SendMessageW(g_log, EM_SCROLLCARET, 0, 0);

    ShowWindow(frame, SW_SHOW);
    // The keyboard focus cue, pinned for the reason shootAtDpi() pins it: without it
    // the band captures are not deterministic between runs. See the block there.
    SendMessageW(g_frame, WM_UPDATEUISTATE,
                 MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);

    for (int page = 0; page < screenCount(wiz); page++) {
        setScreen(page);
        UpdateWindow(g_page);
        UpdateWindow(frame);

        RECT p;
        GetClientRect(g_page, &p);
        int pw = p.right, ph = p.bottom;

        HDC dc = GetDC(g_page);
        int reported = 0;
        switch (kindAt(page)) {
        case kScreenCheck: reported = renderCheckScreen(dc, pw, ph, true); break;
        case kScreenWork:  reported = renderWorkChrome(dc, pw, true);      break;
        default:           reported = renderDoneChrome(dc, pw, true);      break;
        }

        // ===================================================================
        // *** THE CHECK THAT WOULD HAVE CAUGHT IT, AND IT IS A DIFFERENCE RATHER
        //     THAN A THRESHOLD. ***
        //
        // A number picked out of the air - "the page paints at least 300 pixels" -
        // would be a constant somebody lowers. This renders the SAME screen twice
        // through the product's own renderCheckScreen(): once as the table describes
        // it, and once with Screen::paintsMachineReview cleared, which is exactly the
        // state that shipped. The second render IS the blank page the owner saw, so
        // the difference between them is the content that went missing, measured in
        // the units it went missing in.
        //
        // WHERE EACH SIDE COMES FROM: both are the product's renderer with measure
        // set; neither is computed from the other; and the field is put back before
        // anything else reads it. Restore the defect in uninstall.cpp and the two
        // numbers become equal, which is this check going red.
        // ===================================================================
        if (kindAt(page) == kScreenCheck) {
            bool was = wiz->screens[page].paintsMachineReview;
            wiz->screens[page].paintsMachineReview = false;
            int blank = renderCheckScreen(dc, pw, ph, true);
            wiz->screens[page].paintsMachineReview = was;
            int again = renderCheckScreen(dc, pw, ph, true);

            wchar_t said[400];
            _snwprintf(said, 390,
                       L"%ddpi 1-confirm: the page really has WORDS on it - the "
                       L"renderer asks for %d pixels here and %d for the same screen "
                       L"with paintsMachineReview cleared, which is the blank page "
                       L"that shipped (%d more)", dpi, reported, blank,
                       reported - blank);
            said[390] = 0;
            check(reported > blank + 200, said);

            // ...and the field really was put back, because a check that leaves the
            // product mutated poisons every measurement after it.
            _snwprintf(said, 390,
                       L"%ddpi 1-confirm: the field was put back and the screen "
                       L"measures the same as before (%d then %d)", dpi, reported,
                       again);
            said[390] = 0;
            check(again == reported && wiz->screens[page].paintsMachineReview, said);
        }
        ReleaseDC(g_page, dc);

        const wchar_t* name = uninstallPageName(page);

        // *** THE TWO BUTTONS, READ OFF THE CONTROLS. *** This flow has two and no
        // more, and the word on each is a promise about what the press does. The
        // right hand side of both is the product's own rule - the table's
        // primaryLabel and secondaryGoesBack() - so a window whose word and whose
        // deed disagree cannot pass either.
        {
            wchar_t onCtl[160];
            onCtl[0] = 0;
            GetWindowTextW(g_primary, onCtl, 160);
            wchar_t said[400];
            _snwprintf(said, 390,
                       L"%ddpi uninstall %s: the primary button says what its entry "
                       L"says it does - it reads '%s'", dpi, name, onCtl);
            said[390] = 0;
            check(wiz->screens[page].primaryLabel != 0 &&
                  wcscmp(onCtl, wiz->screens[page].primaryLabel) == 0, said);

            GetWindowTextW(g_secondary, onCtl, 160);
            const wchar_t* wants = secondaryGoesBack() ? L"Back" : L"Cancel";
            _snwprintf(said, 390,
                       L"%ddpi uninstall %s: the second button reads '%s' and pressing "
                       L"it %s", dpi, name, onCtl,
                       secondaryGoesBack() ? L"goes back a screen" : L"closes");
            said[390] = 0;
            check(wcscmp(onCtl, wants) == 0, said);
        }

        // The picture. The summary screen is measured like the other two and NOT
        // saved - see the skip in shootUninstall() for why - so the filename is built
        // only for the screens that get one.
        wchar_t png[512];
        bool    takeIt = kindAt(page) != kScreenDone;
        if (takeIt)
            _snwprintf(png, 500, L"%s\\uninstall-%s-%ddpi.png", g_shotDir, name, dpi);
        InkBox ink = renderTall(page, pw, ph, reported, takeIt ? png : 0, true);

        wprintf(L"  uninstall %-10s pw=%4d ph=%4d reported=%4d view=%4d "
                L"ink=(%d,%d)-(%d,%d)\n", name, pw, ph, reported, g_viewH,
                ink.left, ink.top, ink.right, ink.bottom);

        wchar_t what[300];
        _snwprintf(what, 290, L"%ddpi uninstall %s: something was drawn at all", dpi,
                   name);
        check(ink.any, what);

        _snwprintf(what, 290,
                   L"%ddpi uninstall %s: no ink below the reported content height "
                   L"(%d <= %d)", dpi, name, ink.bottom, reported);
        check(ink.bottom < reported, what);

        // *** THE SIDEWAYS QUESTION, AND IT IS ASKED ABOUT THE GUTTER RATHER THAN
        //     ABOUT THE EDGE - WHICH IS A CORRECTION THIS ROUND MEASURED. ***
        //
        // The obvious form of this is "no ink past the right edge", ink.right < pw,
        // and it is what was written here first. It CANNOT FAIL. renderTall() creates
        // the bitmap exactly pw wide and inkOf() scans that bitmap, so ink.right is at
        // most pw-1 whatever the renderer does: a page that drew four hundred pixels
        // off the right of the paper would be clipped by the bitmap and reported as a
        // clean pass. It was found by mutating kMargin to -40, watching every column
        // of the page fill with text, and watching the check print [ok].
        //
        // So the question is the one a reader would actually ask: does the painted
        // content keep a margin on both sides. WHERE EACH SIDE COMES FROM: the left is
        // the ink box measured off the bitmap; the right is the page's own width and a
        // fixed eight logical pixels, which is a floor and not the product's kMargin -
        // comparing against the constant the layout draws with would move the ruler
        // with the thing being measured, which is exactly how the version above failed.
        int gutter = S(8);
        _snwprintf(what, 290,
                   L"%ddpi uninstall %s: the painted content keeps a margin on BOTH "
                   L"sides - ink spans %d..%d of a %d wide page, floor %d",
                   dpi, name, ink.left, ink.right, pw, gutter);
        check(ink.left >= gutter && ink.right <= pw - gutter, what);

        // -------------------------------------------------------------------
        // RULE 1, ON THE THREE SCREENS NOBODY HAD EVER MEASURED.
        //
        // WHERE EACH SIDE COMES FROM: the left is what the renderer says it needs,
        // asked of the product's own render*() with measure set; the right is the
        // strip of window the product's own layout() gave it, read back off g_viewH.
        // Neither is computed from the other.
        // -------------------------------------------------------------------
        int deficit = reported - g_viewH;
        if (deficit < 0)
            deficit = 0;
        int allowed = uninstallAllowedDeficit(name, dpi);
        _snwprintf(what, 290,
                   L"%ddpi uninstall %s: Rule 1 - the page does not scroll (needs %d, "
                   L"has %d, over by %d, allowed %d)", dpi, name, reported, g_viewH,
                   deficit, allowed);
        check(deficit <= allowed, what);

        // *** AND THE BAND ON THE SCREEN WHOSE BUTTON REMOVES. *** The setup grew
        // eight band captures because the owner found a button that was in no
        // committed image. This flow's band was in none either, and the button
        // standing in it is the one that starts the removal.
        if (kindAt(page) == kScreenCheck)
            saveFootBand(dpi, L"uninstall-1-confirm-band");
    }

    DestroyWindow(frame);
    g_frame   = 0;
    g_page    = 0;
    g_primary = 0;
    g_secondary = 0;
    g_log     = 0;
    g_summary = 0;
    g_bar     = 0;
    g_review  = 0;
    releaseFonts();
}

// The uninstaller's whole picture pass. Called from wmain() after the setup's,
// with a Wizard buildWizard() filled from an invented Run.
static void shootUninstall(Wizard* wiz)
{
    wprintf(L"\n=== PART 3u: the uninstaller's three screens, 96 and 144 DPI ===\n");

    // *** THE FIVE CONTROLS THIS FLOW DOES NOT HAVE, ASKED OF THE PRODUCT'S OWN
    //     PREDICATES. *** shootUninstallAtDpi() creates six windows and not eleven,
    // and the justification for each absence is the same expression runWizard() uses
    // to decide whether to create it. A flow that grew a pane or an offer would make
    // this fail rather than silently be photographed without it.
    check(!flowHasAction(wiz) && !flowHasOverride(wiz) && !flowHasChoice(wiz) &&
          !flowHasPane(wiz) && wiz->recheck == 0 && !flowNeedsPhoto(wiz) &&
          !flowNeedsZadigShot(wiz),
          L"the uninstaller asks for none of the optional controls - no re-check, no "
          L"contextual button, no door, no choice, no pane and no picture - so the "
          L"window these captures are of is the window the product builds");

    // *** THE ONE SCREEN THAT IS MEASURED AND NOT PHOTOGRAPHED, SAID OUT LOUD. ***
    // A guard that quietly took two pictures where three screens exist would be this
    // project's own defect in the file that exists to catch it.
    skipped(L"a picture of the uninstaller's summary screen: the words in its pane are "
            L"written by runRemoval(), and this harness must never run a removal. Its "
            L"geometry, its captions and its buttons ARE measured, at both DPIs.");

    shootUninstallAtDpi(wiz, 96);
    shootUninstallAtDpi(wiz, 144);
}

// The name setup.cpp calls. It renders instead of looping.
int runWizard(Wizard* wiz)
{
    wprintf(L"\n=== PART 3: the four pages, rendered, 96 and 144 DPI ===\n");
    // The words first, then the gate, then the pictures. In that order, because a
    // gate that runs after the PNGs have been written has already let them be
    // written.
    buildShotSummary();
    checkMarkKinds(wiz);
    checkRowsNotTruncated(wiz);
    checkZadigCaption(wiz);
    // The second of the selector's two call sites, on the real Wizard. Written in the
    // same edit as the check, for the reason every call site in this file carries a
    // comment: a suite once shipped here with no caller and the run read green.
    checkReviewFooterWired(wiz);
    checkNoLiveIdentity(wiz);
    shootAtDpi(wiz, 96);
    shootAtDpi(wiz, 144);
    releaseFonts();
    return 0;
}

}   // namespace bcdgui

// ===========================================================================
// THE README'S NUMBERS, AND NOT ONLY ITS NAMES
//
// *** testReadmeDescribesTheScreens() CLOSED THE DRIFT ON THE NINE TITLES AND LEFT THE
//     FIGURES BESIDE THEM OPEN. *** That section makes two arithmetic claims a reader
// takes as fact, and until this round nothing compared either with the program:
//
//   - "Six of the nine are steps: 1 to 6." Two numbers, both derived from the table by
//     stepScreenCount() and screenCount() every run, and both spelled by hand in prose.
//     A seventh step screen would leave that sentence saying six.
//   - the four Rule 1 ceilings, in a table of two rows. They are allowedDeficit()'s own
//     figures, published in a document that no check had ever opened. A round that
//     lowered a ceiling - which is the thing that project rule most wants noticed - would
//     leave the README quoting the old number, and the README is what a reader outside
//     this folder believes.
//
// It is deliberately NOT a check that the README's ceilings equal the deficits the run
// MEASURES. Those are 257/448 and 145/292 today and equal to the ceilings by coincidence
// of the ratchet being tight; asserting equality would fail the day a screen came in
// under its ceiling, which is a change to be welcomed. What the README publishes is the
// CEILING, so that is what this compares it against.
//
// WHERE EACH SIDE COMES FROM: the left is the bytes of installer\README.md read off the
// disk. The right is the count of step screens in the table buildScreens() filled, the
// screen titles that table carries, and allowedDeficit()'s figures - none of which the
// README can see and none of which is read out of it.
// ===========================================================================
static const wchar_t* englishNumber(int n)
{
    static const wchar_t* const kWords[13] = {
        L"zero", L"one", L"two", L"three", L"four", L"five", L"six",
        L"seven", L"eight", L"nine", L"ten", L"eleven", L"twelve"
    };
    return (n >= 0 && n <= 12) ? kWords[n] : L"(out of range)";
}

static void testReadmeNumbersMatchTheProgram()
{
    wprintf(L"\n--- the README's own arithmetic is the program's ---\n");

    wchar_t what[400];

    bcdsetup::MachineState s;
    fakeState(&s);
    bcdgui::Wizard w;
    ZeroMemory(&w, sizeof(w));
    bcdsetup::buildScreens(&w, &s);
    int screens = bcdgui::screenCount(&w);
    int steps   = stepScreenCount(&w);

    SIZE_T   chars = 0;
    wchar_t* text  = readInstallerReadme(&chars);

    // "**Six of the nine are steps" - the sentence composed from the two numbers the
    // table really yields, with the leading capital the README's own emphasis puts there.
    wchar_t sentence[160];
    {
        const wchar_t* six  = englishNumber(steps);
        wchar_t        head = (wchar_t)towupper(six[0]);
        _snwprintf(sentence, 150, L"**%c%s of the %s are steps", (int)head, six + 1,
                   englishNumber(screens));
        sentence[150] = 0;
    }
    int at = text ? posOf(text, sentence) : -1;
    _snwprintf(what, 390,
               L"the README's step count is the table's - it must say \"%s\" and that is "
               L"found at %d of %d characters",
               sentence, at, (int)chars);
    what[390] = 0;
    check(text != 0 && at >= 0, what);

    // The two ceilings, each as the README writes its row: the screen's own title in
    // backticks, then the two figures allowedDeficit() holds for it.
    struct Ceiling {
        const wchar_t* title;
        const wchar_t* slug;
    };
    static const Ceiling kCeilings[2] = {
        { ::kBindingTitle, L"2d-binding" },
        { ::kInstallTitle, L"2e-install" }
    };
    int rowsFound = 0;
    for (int i = 0; i < 2; i++) {
        wchar_t row[300];
        _snwprintf(row, 290, L"| `%s` | %d | %d |", kCeilings[i].title,
                   bcdgui::allowedDeficit(kCeilings[i].slug, 96),
                   bcdgui::allowedDeficit(kCeilings[i].slug, 144));
        row[290] = 0;
        int p = text ? posOf(text, row) : -1;
        if (p >= 0)
            rowsFound++;
        _snwprintf(what, 390,
                   L"...and it publishes this screen's two Rule 1 ceilings as the harness "
                   L"holds them - the row \"%s\" is at %d", row, p);
        what[390] = 0;
        check(text != 0 && p >= 0, what);
    }

    _snwprintf(what, 390,
               L"...and those are the ONLY screens allowed to overflow - %d rows in the "
               L"README's table, and %d of the %d screens carry a non-zero ceiling",
               rowsFound, 2, screens);
    what[390] = 0;
    {
        int named = 0;
        for (int i = 0; i < screens; i++) {
            // Walked over the flow so that a screen quietly added to allowedDeficit()
            // without being added to the README fails here. The slug is not reachable
            // from this side of the file, so the two known keys are asked for by name
            // and the count is what makes the assertion about the whole table.
            for (int k = 0; k < 2; k++)
                if (w.screens[i].title == kCeilings[k].title &&
                    bcdgui::allowedDeficit(kCeilings[k].slug, 96) > 0)
                    named++;
        }
        check(rowsFound == 2 && named == 2, what);
    }

    if (text)
        HeapFree(GetProcessHeap(), 0, text);
}

// ===========================================================================
// PART 4 - the words inside a BUILT executable, with a count of its own
//
// WHY THIS IS NOT IN "all", AND THEREFORE NOT IN THE 120. It needs a built
// BCD3000Setup.exe, and building one needs the two payloads and the device
// photograph, none of which are in this repository on purpose. Folded into "all" it
// would make the whole harness fail on a fresh clone for a reason that has nothing
// to do with the installer's correctness. So it is its own mode with its own
// denominator, and both numbers get published rather than one merged number that
// means something different on different machines.
//
// WHY IT IS WORTH A MODE AT ALL. Everything Part 2 proves, it proves about the
// function as this translation unit compiled it. The thing that actually ships is
// the exe, and the one defect this round exists to catch - a count that says seven
// while the block has eight items - is a defect made of literal text. Reading it
// back out of the binary is the only check that cannot be satisfied by a source file
// that was never rebuilt.
//
// The file is READ, never mapped for execution and never executed: an installer that
// writes to HKLM and Program Files is not something a verification tool runs.
// ===========================================================================
static BYTE* readWholeFile(const wchar_t* path, SIZE_T* sizeOut)
{
    *sizeOut = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(f, &li) || li.QuadPart <= 0 || li.QuadPart > 0x10000000) {
        CloseHandle(f);
        return 0;
    }
    SIZE_T n = (SIZE_T)li.QuadPart;
    BYTE*  d = (BYTE*)HeapAlloc(GetProcessHeap(), 0, n);
    if (!d) {
        CloseHandle(f);
        return 0;
    }
    SIZE_T got = 0;
    while (got < n) {
        DWORD chunk = (DWORD)((n - got) > 0x100000 ? 0x100000 : (n - got));
        DWORD read  = 0;
        if (!ReadFile(f, d + got, chunk, &read, 0) || read == 0)
            break;
        got += read;
    }
    CloseHandle(f);
    if (got != n) {
        HeapFree(GetProcessHeap(), 0, d);
        return 0;
    }
    *sizeOut = n;
    return d;
}

// A wide literal is UTF-16LE in the binary, so the search is for those bytes. Not
// an ASCII "strings" scan: that finds nothing here, and finding nothing would look
// exactly like a string that is absent.
static bool exeHas(const BYTE* d, SIZE_T n, const wchar_t* text)
{
    SIZE_T bytes = wcslen(text) * sizeof(wchar_t);
    if (!d || bytes == 0 || bytes > n)
        return false;
    for (SIZE_T i = 0; i + bytes <= n; i++)
        if (memcmp(d + i, text, bytes) == 0)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// IS THE BINARY EVEN THIS SOURCE'S BINARY? The general answer to the defect above.
//
// WHAT WENT WRONG, AND IT IS WORTH THE PARAGRAPH. Round 3 rewrote item 8 - "at boot
// time" became "before you sign in" - and this mode kept printing 6 of 6 about a
// binary whose item 8 still said "boot time", because none of the five strings it
// reads had changed. The mode was not wrong about anything it asserted; it simply had
// nothing to say about the round in front of it.
//
// ADDING THIS ROUND'S STRINGS WOULD FIX IT FOR THIS ROUND AND ONLY THIS ROUND. That
// is the same trade that produced the hole: a per round string is a per round act of
// memory, and the memory is the part that failed. A timestamp is not: any edit to any
// of the files the binary's words come from makes it older than its sources, whatever
// the edit was and whether or not anybody remembered to add a check for it.
//
// SO BOTH ARE HERE. The two strings round 3 changed, because they are the specific
// thing that went unnoticed and a named check is a better failure message than a
// timestamp; and the timestamp, because it is the half that does not depend on the
// next implementer thinking of it.
static const wchar_t* const kExeSources[] = {
    L"..\\setup.cpp", L"..\\gui.cpp", L"..\\common.cpp",
    L"..\\gui.h",     L"..\\common.h", L"..\\version.h",
    L"..\\setup.rc"
};

// ---------------------------------------------------------------------------
// HOW BIG IS THE PAYLOAD THE INSTALLER IS REALLY CARRYING?
//
// LOAD_LIBRARY_AS_DATAFILE, the same flag and the same reasoning as verify\resdump.cpp:
// the module is mapped for READING and no entry point of it is ever called. This
// harness cannot run the installer, and an installer that writes to HKLM and to
// Program Files is not something a verification tool runs.
//
// *** WHY THE SIZE AND NOT A HASH. *** A hash would pin Microsoft's DLL to one build of
// it, so every update to the NuGet package would fail this check for no reason, and the
// pressure would be to delete the check. The size answers the question that is actually
// open here, which is ARCHITECTURE: the x64 runtime is 2,892,288 bytes and the arm64
// build has the SAME FILE NAME and is 6,111,744. Picking the wrong folder inside the
// package produces an installer that builds, ships, installs, and fails at the first
// MIDI call on every machine.
//
// Returns the size, or 0 when the resource is absent - which is also what a zero length
// resource would report, and the two need not be told apart: neither is shippable.
// ---------------------------------------------------------------------------
static DWORD payloadSizeInExe(const wchar_t* exePath, int id)
{
    HMODULE m = LoadLibraryExW(exePath, 0, LOAD_LIBRARY_AS_DATAFILE);
    if (!m)
        return 0;
    // RT_RCDATA is 10, spelled as the wide MAKEINTRESOURCE because this project does
    // not define UNICODE - resdump.cpp says the same thing over the same call.
    HRSRC h = FindResourceW(m, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
    DWORD n = h ? SizeofResource(m, h) : 0;
    FreeLibrary(m);
    return n;
}

static bool exeIsNewerThanItsSources(const wchar_t* exePath, wchar_t* worst,
                                     int worstCap)
{
    WIN32_FILE_ATTRIBUTE_DATA ex;
    worst[0] = 0;
    if (!GetFileAttributesExW(exePath, GetFileExInfoStandard, &ex))
        return false;
    bool ok    = true;
    int  count = (int)(sizeof(kExeSources) / sizeof(kExeSources[0]));
    for (int i = 0; i < count; i++) {
        WIN32_FILE_ATTRIBUTE_DATA sr;
        if (!GetFileAttributesExW(kExeSources[i], GetFileExInfoStandard, &sr)) {
            // A source that cannot be read is not a pass. It means this mode is being
            // run from somewhere the relative paths do not mean what they say.
            _snwprintf(worst, worstCap - 1, L"%s could not be read", kExeSources[i]);
            worst[worstCap - 1] = 0;
            return false;
        }
        if (CompareFileTime(&sr.ftLastWriteTime, &ex.ftLastWriteTime) > 0) {
            ok = false;
            _snwprintf(worst, worstCap - 1, L"%s is newer than the binary",
                       kExeSources[i]);
            worst[worstCap - 1] = 0;
        }
    }
    return ok;
}

static void testBuiltExe(const wchar_t* path)
{
    wprintf(L"\n=== PART 4: the words inside a built binary ===\n");
    wprintf(L"  %s\n", path);

    SIZE_T  n = 0;
    BYTE*   d = readWholeFile(path, &n);
    wchar_t what[512];
    _snwprintf(what, 500, L"the binary was read (%lu bytes)", (unsigned long)n);
    check(d != 0, what);
    if (!d) {
        wprintf(L"  nothing else can be measured. Build it first, from installer\\:\n");
        wprintf(L"      build.bat setup\n");
        return;
    }

    check(exeHas(d, n, L"Eight things worth knowing. The first one can undo "
                       L"everything above."),
          L"the new count is in the shipped binary");
    check(!exeHas(d, n, L"Seven things"),
          L"the OLD count is not in the shipped binary either");
    check(exeHas(d, n, L"  8. Nothing this installer does needs Windows to be "
                       L"restarted."),
          L"item 8 itself is in the shipped binary");
    check(exeHas(d, n, L"ONE OTHER THING CAN ASK FOR A RESTART"),
          L"and so is the sentence that keeps item 8 from contradicting 7");
    // The one character in this folder that is written as an escape. Reading it back
    // out of the binary is what proves the escape survived the compiler's idea of
    // the source's code page - which is the whole reason it is an escape.
    check(exeHas(d, n, L"Voc\u00EA precisa instalar alguns drivers primeiro"),
          L"the quoted VirtualDJ band is in the binary with U+00EA intact");

    // *** THE TWO STRINGS ROUND 3 CHANGED, WHICH THIS MODE COULD NOT SEE. *** Item 8
    // used to say "at boot time" and now says "before you sign in", because the
    // control service starts at every SIGN IN and boot is a different moment. The
    // source side of both is checked in PART 2; these two are about the shipped file.
    check(exeHas(d, n, L"nothing that loads before you sign in"),
          L"item 8's structural reason is in the shipped binary, in its new words");
    check(!exeHas(d, n, L"at boot time"),
          L"and the words it replaced are GONE from the shipped binary");

    // *** THE BACKSTOP THAT USED TO STAND HERE CHECKED FOUR VALUES THIS TASK REMOVED.
    //     *** kLoopMidiWingetId, kLoopMidiSha256, kLoopMidiVersion and
    // kHashCheckCommand were put in the binary deliberately, so that somebody holding
    // only the executable could still tell which bytes of a third party installer
    // were the right ones. That third party detection and the provenance text
    // built on it are gone - see the block over kMidiTitle in setup.cpp - and none
    // of those four constants exist to check any more. This program currently ships
    // no winget contract of its own; a later task's backstop goes here if it adds
    // one.

    // *** THE SCREENSHOT'S CAVEAT, IN THE FILE THAT CARRIES THE SCREENSHOT. ***
    //
    // The picture goes into this binary as resource 111 whether or not the words that
    // qualify it were rebuilt with it, and a picture of an already bound machine
    // shipped without its caveat is a picture that promises one screen and delivers
    // another. These two are the same kind of check as the four above - presence of a
    // literal put there deliberately - and they are about the shipped file rather
    // than about the function that produced it, which PART 2 and PART 3 already own.
    check(exeHas(d, n, L"whatever it is called"),
          L"the shipped binary refuses to name one Zadig button label as the one to "
          L"expect");
    check(exeHas(d, n, L"BCD3000 (Interface 0)"),
          L"...and carries the list line the user is told to match, spelled the way "
          L"Zadig writes it");

    // -------------------------------------------------------------------
    // *** THE MIT NOTICE, INSIDE THE FILE THAT CARRIES THE BINARY IT IS ABOUT. ***
    //
    // Resource 105 of this executable is Microsoft's Windows.Devices.Midi2.dll. MIT
    // requires its copyright notice and permission notice to be included with every
    // copy - and a copy handed to somebody as a single .exe has no LICENSE file beside
    // it. PART 2e3h checks the repository's LICENSE and checks that the PROGRAM's
    // notice agrees with it; this checks that the notice survived into the thing that
    // is actually shipped. A source file nobody rebuilt is exactly the failure this
    // whole mode exists for.
    //
    // WHERE EACH SIDE COMES FROM: the haystack is the bytes of the built
    // BCD3000Setup.exe read off the disk; the needles are literals typed HERE, in a
    // file the installer does not compile against. Change a character of the notice in
    // setup.cpp and rebuild, and these go red - which is the injection this task was
    // required to demonstrate. Sharing a constant between the two sides would have made
    // them one thing and the check unable to fail.
    // -------------------------------------------------------------------
    check(exeHas(d, n, L"Copyright (c) Microsoft Corporation."),
          L"the shipped binary carries Microsoft's copyright notice");
    check(exeHas(d, n, L"Windows.Devices.Midi2.dll - Windows MIDI Services"),
          L"...and names the redistributed file the notice is about");
    check(exeHas(d, n, L"https://github.com/microsoft/MIDI"),
          L"...and where it came from");
    check(exeHas(d, n, L"Permission is hereby granted, free of charge, to any person "
                       L"obtaining a copy"),
          L"...and the MIT permission notice itself, in full rather than by reference");
    check(exeHas(d, n, L"The above copyright notice and this permission notice shall be "
                       L"included in all"),
          L"...and the condition that makes carrying all of this compulsory");
    check(exeHas(d, n, L"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY "
                       L"KIND, EXPRESS OR"),
          L"...and the warranty disclaimer that completes the MIT text");

    HeapFree(GetProcessHeap(), 0, d);

    // -------------------------------------------------------------------
    // *** THE FIVE PAYLOADS, AND THE ONE WHOSE SIZE IS A CONTRACT. ***
    //
    // WHERE EACH SIDE COMES FROM, and this is the part that decides whether the row
    // below can fail at all. The LEFT is SizeofResource() on the built
    // BCD3000Setup.exe - what rc really embedded. The RIGHT is a number TYPED IN THIS
    // FILE. It is deliberately not read from ..\..\native\bcdmidi\wms\, because a byte
    // count computed from the same file it measures agrees with itself on every file
    // in the world, including the arm64 one this row exists to reject.
    //
    // Only 105 gets an exact figure. The other four are OUR build outputs and their
    // sizes change with every compile, so a literal for them would be a check that
    // fails on the next commit for no reason - the shape that gets checks deleted.
    // They are asserted PRESENT and non-trivial instead, which is the honest question
    // about them: rc reports a missing payload as a generic "cannot open file", and a
    // resource that silently ended up empty would install a zero byte DLL.
    // -------------------------------------------------------------------
    struct PayloadRow {
        int            id;
        const wchar_t* name;
        DWORD          exact;   // 0 = no contract on the size, only a floor
        DWORD          floorN;
    };
    static const PayloadRow kPayloads[] = {
        { IDR_PAYLOAD_ASIO_DLL,    L"101 BcdAsio.dll",                    0, 4096 },
        { IDR_PAYLOAD_BRIDGE_EXE,  L"102 BCD3000Bridge.exe",              0, 4096 },
        { IDR_PAYLOAD_UNINSTALLER, L"103 BCD3000Uninstall.exe",           0, 4096 },
        { IDR_PAYLOAD_MIDI_DLL,    L"104 BcdMidi.dll",                    0, 4096 },
        // 2,892,288 is the x64 runtime out of the Windows.Devices.Midi2 NuGet package,
        // measured on 2026-08-01. The arm64 build of the same name is 6,111,744.
        { IDR_PAYLOAD_WINMIDI_DLL, L"105 Windows.Devices.Midi2.dll",
          2892288, 2892288 }
    };
    const int kPayloadCount = (int)(sizeof(kPayloads) / sizeof(kPayloads[0]));
    for (int i = 0; i < kPayloadCount; i++) {
        DWORD got = payloadSizeInExe(path, kPayloads[i].id);
        bool  ok  = kPayloads[i].exact ? (got == kPayloads[i].exact)
                                       : (got >= kPayloads[i].floorN);
        wchar_t row[400];
        if (kPayloads[i].exact)
            _snwprintf(row, 390,
                       L"payload %s is embedded and is EXACTLY the x64 build - %lu bytes, "
                       L"and the only accepted answer is %lu (the arm64 build of the same "
                       L"name is 6111744)",
                       kPayloads[i].name, got, kPayloads[i].exact);
        else
            _snwprintf(row, 390,
                       L"payload %s is embedded and is not empty - %lu bytes, floor %lu",
                       kPayloads[i].name, got, kPayloads[i].floorN);
        row[390] = 0;
        check(ok, row);
    }

    wchar_t worst[300];
    bool    fresh = exeIsNewerThanItsSources(path, worst, 300);
    wchar_t what2[600];
    _snwprintf(what2, 590,
               L"the binary is newer than every file its words come from%s%s",
               fresh ? L"" : L" - ", fresh ? L"" : worst);
    what2[590] = 0;
    check(fresh, what2);
    if (!fresh)
        wprintf(L"  the words above were read out of a binary that predates the "
                L"source. Rebuild before believing them: installer\\build.bat setup\n");
}

// ===========================================================================
int wmain(int argc, wchar_t** argv)
{
    // Stamped before anything is written, so that "this file was written by this run"
    // is a comparison and not a hope. See savePngChecked().
    //
    // Five seconds are taken off it, and the reason is a real one rather than
    // superstition: a FAT or exFAT volume records a write time to the nearest two
    // seconds and rounds DOWN, so a picture written a second after this stamp can
    // carry a timestamp a second before it. Without the slack the check would be a
    // permanent false alarm on a memory stick, and a check that cries wolf is a check
    // nobody reads. Five seconds is far shorter than the gap between two runs, which
    // is the interval that has to stay distinguishable.
    GetSystemTimeAsFileTime(&bcdgui::g_runStart);
    {
        ULARGE_INTEGER t;
        t.LowPart  = bcdgui::g_runStart.dwLowDateTime;
        t.HighPart = bcdgui::g_runStart.dwHighDateTime;
        t.QuadPart -= 50000000ULL;                 // 5 seconds, in 100ns units
        bcdgui::g_runStart.dwLowDateTime  = t.LowPart;
        bcdgui::g_runStart.dwHighDateTime = t.HighPart;
    }

    const wchar_t* mode = argc > 1 ? argv[1] : L"all";
    // *** THE DEFAULT IS "shots", NOT ".". *** The twelve tracked captures live in
    // installer\verify\shots, and this used to fall back to the current directory:
    // running bcdverify.exe with no argument therefore dropped twelve PNGs loose in
    // installer\verify, untracked duplicates of the tracked ones, waiting for somebody
    // to commit them by mistake. Defaulting to shots means the no argument run
    // OVERWRITES the tracked pictures instead, which is the better of the two failures:
    // git shows it, one line per changed picture, and there is nothing to clean up. A
    // run that must not touch them passes a directory, which is what the round's own
    // instructions require anyway.
    bcdgui::g_shotDir   = argc > 2 ? argv[2] : L"shots";

    wprintf(L"BCD3000 installer rounds 2 and 3 - verification harness\n");
    wprintf(L"nothing below installs, registers, writes or shows a pixel\n");

    // *** AN UNRECOGNISED MODE USED TO BE THE QUIETEST WAY TO GET A GREEN RUN. ***
    // "bcdverify.exe txt" matched none of the branches below, ran nothing, printed
    // "0 checks, 0 failures / VERIFY_OK" and exited 0. Anything that reads that exit
    // code - a person, a script, a future build step - was told the harness passed.
    // The modes are therefore enumerated, and a name that is not one of them is
    // refused before any work is attempted.
    if (_wcsicmp(mode, L"all")   != 0 && _wcsicmp(mode, L"text") != 0 &&
        _wcsicmp(mode, L"shots") != 0 && _wcsicmp(mode, L"exe")  != 0 &&
        _wcsicmp(mode, L"dump")  != 0 && _wcsicmp(mode, L"preview") != 0) {
        wprintf(L"\n  unknown mode \"%s\". Nothing was run, and this is NOT a pass.\n",
                mode);
        wprintf(L"  usage: bcdverify.exe [all|text|shots|exe|dump|preview] "
                L"[directory or exe]\n");
        return 2;
    }

    // "preview": the uninstaller's /preview as a DOCUMENT, for a person to read
    // before the real uninstaller is ever run on a machine.
    //
    // WHY IT IS HERE RATHER THAN JUST RUNNING BCD3000Uninstall.exe /preview. Because
    // reading it is the point, and running the uninstaller - in any mode - is a thing
    // to do with the owner present, once, deliberately. This prints the same text from
    // the same function against the INVENTED machine of fakeState(), so the shape, the
    // order and every sentence can be reviewed with nothing at risk and no per user
    // path on the page. What it does NOT show is this machine's real answers; only the
    // real program can, and that is the run this document exists to prepare for.
    //
    // Deliberately not part of "all", for the same reason "dump" is not: it produces a
    // document and not a single check, and adding it to the suite would put a number
    // in the denominator that measured nothing.
    if (_wcsicmp(mode, L"preview") == 0) {
        static bcduninstall::Run run;
        previewRun(&run);
        run.state.bridge.running       = true;
        run.state.bridge.instanceCount = 2;
        run.state.bridge.firstPid      = 4242;
        run.havePrevious               = true;
        systemFileThatExists(run.previous, kPathMax);
        wprintf(L"\n  THE INVENTED MACHINE BELOW IS NOT THIS ONE. Paths, accounts and\n"
                L"  the recorded previous driver are fakeState()'s fiction. Only\n"
                L"  BCD3000Uninstall.exe /preview reads the real machine.\n\n");
        bcduninstall::previewPlan(&run, true, 0);
        return 0;
    }

    if (_wcsicmp(mode, L"text") == 0 || _wcsicmp(mode, L"all") == 0) {
        testSummaryText();
        testWarningsBlock();
        testReviewRows();
        testWalkthrough();
        testWingetContract();
        testScreenTable();
        testInstallScreen();
        testCableScreen();
        // In flow order after the mixer, which is where the screen it is about sits. An
        // interrupted round once shipped a nine check suite with NO call site here and
        // the run still read green, so this line is the half of that task that mattered.
        testDeviceScreen();
        // Immediately before the screen it feeds, and its call site is written in the
        // same edit as the suite - a nine check suite once shipped here with no caller
        // at all and the whole run still read green.
        testServiceIsRegistered();
        testKnownBadMidiBuilds();
        testMidiPortScreen();
        // In flow order after the MIDI port, which is where the screen it is about sits.
        // Its own call site is written in the same edit as the suite, for the reason the
        // device screen's comment above gives: a nine check suite once shipped with no
        // caller and the run read green.
        testZadigScreen();
        // Beside the two suites whose screens carry the addresses, and it reads the
        // SOURCE rather than the compiled constants - see the head of PART 2e3b for the
        // injection that made that necessary.
        testAddressIsDefinedOnce();
        // *** AND THE CLASS BEHIND THE DEFECT THE ADDRESS ROUND FOUND. *** It belongs
        // after every suite that builds a screen, because it is about buildScreens() as a
        // whole rather than about any one screen. Its call site is written in the same
        // edit as the suite, for the reason testDeviceScreen()'s comment gives.
        testRebuildIsAFreshBuild();
        testBindingScreen();
        testPrimaryLabels();
        // After every suite that builds a screen, because it is about the whole flow and
        // not about one screen: it asks whether a console run says what the window's six
        // steps say, in the window's order. Written in the same edit as the suite, for
        // the reason the device screen's comment above gives - a nine check suite once
        // shipped here with no caller and the run read green.
        testConsoleCarriesTheSubjects();
        // The two that read installer\README.md off the disk. Beside each other because
        // they open the same file, and after the console suite because the README's
        // account of the flow is the third audience for the same nine screens.
        testReadmeDescribesTheScreens();
        testReadmeNumbersMatchTheProgram();
        testReadmeAddressesMatchTheProgram();
        // ...and the third document with the same addresses in it, which is the one a
        // stranger reads first and the one nothing had ever opened. Written in the same
        // edit as the suite: a nine check suite once shipped here with no call site and
        // the run read green, which is why every one of these lines carries a comment
        // saying so.
        testRootReadmeAddressesMatchTheProgram();
        // Beside the two README suites, because it reads both of those files and the
        // LICENSE beside them, and because its subject is the same one: a statement typed
        // into a document that nothing keeps true. This one is not a defect if it drifts,
        // it is a licence condition unmet - see the head of PART 2e3h. Its call site is
        // written in the same edit as the suite, for the reason every line here carries:
        // a nine check suite once shipped with no caller and the whole run read green.
        testMitNoticeTravelsWithTheBinary();
        // The ordering bug's regression check: drives ::planServiceInstall() directly
        // with the exact differsMask shape the defect broke (bridge bit clear, a side
        // DLL bit set) and asserts /replace-service changes the answer. Call site
        // written in the same edit as the suite, for the reason every line here
        // carries: a nine check suite once shipped with no caller and the whole run
        // read green.
        testReplaceServiceActuallyReplacesASideFile();
        // The prose that a person compares against their own hardware, and the version
        // that a person compares against a file's properties. Both are about strings
        // typed beside a source of truth that nothing made them agree with.
        testHardwareStringsMatchTheModelTable();
        // The one sentence in this program whose truth depends on what HAPPENED rather
        // than on what was measured, and the one no capture can ever render. Written in
        // the same edit as the suite, for the reason every line here carries: a nine
        // check suite once shipped with no call site and the run read green.
        testInstallFooterIsTheReassurance();
        testVersionAgreesWithItself();
        // Over the whole flow rather than over one screen, because the failure it catches
        // is on the screens that should NOT have the button and in the states no capture
        // is taken of.
        testNoAbsentControlNamed();
        testUninstallPreview();
        // The suite this round exists for, beside the other one that reads the
        // uninstaller. Its call site is written in the same edit as the suite, for the
        // reason every line here carries: a nine check suite once shipped with no
        // caller and the run read green - and this round's whole subject is a screen
        // that emptied itself while every check stayed green.
        testUninstallConfirmationScreen();
    }

    // Deliberately NOT part of "all": see the head of PART 4 for why, and for why
    // its count is published beside the other one instead of being added to it.
    if (_wcsicmp(mode, L"exe") == 0) {
        testBuiltExe(argc > 2 ? argv[2] : L"..\\BCD3000Setup.exe");
    }

    if (_wcsicmp(mode, L"dump") == 0) {
        // The console banner, exactly as /console prints it, then the whole
        // summary. Both through the product's own output path, so the bytes on a
        // redirected stream are the bytes the installer really produces.
        printBanner();
        bcdsetup::MachineState s;
        fakeState(&s);
        Pending p;
        ZeroMemory(&p, sizeof(p));
        p.driverFileReplaced = true;
        printSummary(&s, &p, false, L"");
        return 0;
    }

    bool wantShots = _wcsicmp(mode, L"shots") == 0 || _wcsicmp(mode, L"all") == 0;

    // savePng() FAILS FOR EVERY PICTURE WHEN THE DIRECTORY IS NOT THERE, which is what
    // a run from the wrong working directory looks like. This refuses to render rather
    // than produce a page of green checks and no pictures. It is the CHEAP half of the
    // answer and it used to be the whole of it; the expensive half - reading what
    // savePng() said about each file it actually wrote - is savePngChecked(), and the
    // count it keeps is checked further down whether or not this branch was taken.
    if (wantShots) {
        DWORD attrs = GetFileAttributesW(bcdgui::g_shotDir);
        bool  dirOk = attrs != INVALID_FILE_ATTRIBUTES &&
                      (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        // 580 into a buffer of 600 and no terminator on truncation: g_shotDir comes
        // from the command line, so this one is reachable by input rather than bounded
        // by a literal. MSVC's _snwprintf does not terminate when the output fills the
        // count exactly - the same defect 9d71ba9 fixed elsewhere in this tree.
        wchar_t what[600];
        _snwprintf(what, 580, L"the pictures have somewhere to go: %s",
                   bcdgui::g_shotDir);
        what[580] = 0;
        check(dirOk, what);
        if (!dirOk) {
            wprintf(L"  refusing to render: savePng() would fail for every picture and "
                    L"every check below it would still pass.\n");
            wprintf(L"  usage: bcdverify.exe all <directory>   (run it from "
                    L"installer\\verify, or name a directory)\n");
            wantShots = false;
        }
    }

    if (wantShots) {
        // *** THE PRIVATE DESKTOP, BEFORE ANY WINDOW EXISTS. ***
        HDESK old  = GetThreadDesktop(GetCurrentThreadId());
        HDESK desk = CreateDesktopW(L"BcdVerifyDesk", 0, 0, 0, GENERIC_ALL, 0);
        if (!desk || !SetThreadDesktop(desk)) {
            wprintf(L"  [FAIL] could not get a private desktop (%lu) - refusing to "
                    L"render on the real one\n", GetLastError());
            g_checks++;
            g_fails++;
        } else {
            wprintf(L"  private desktop: BcdVerifyDesk, %dx%d\n",
                    GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
            if (!bcdgui::init()) {
                wprintf(L"  [FAIL] bcdgui::init()\n");
                g_checks++;
                g_fails++;
            } else {
                bcdgui::beginCapture();
                ZeroMemory(&g_run, sizeof(g_run));
                g_run.opt.preview = true;      // the mode that writes nothing

                // *** prepare() STILL RUNS, AND ITS ANSWER IS STILL THROWN AWAY.
                //     Both halves of that sentence are deliberate. ***
                //
                // It runs because it is the one piece of product code with a side
                // effect that this harness executes at all - it reads a dozen
                // registry keys, walks the process list and asks SetupAPI about the
                // device interfaces - and a run that never called it would stop
                // proving that the read only path works on a real machine.
                //
                // Its answer is thrown away because every page below is a picture of
                // an INVENTED machine (see shotState). What prepare() found here has
                // no business in a tracked PNG, and it was the source of both leaks.
                //
                // The console echo is off around it for the same reason one step
                // further out: the harness's own output gets pasted into reports and
                // into review threads, and it used to print the owner's account name
                // and profile path five times.
                int            stop    = 0;
                const wchar_t* blocked = 0;
                bcdsetup::setConsoleEcho(false);
                prepare(&g_run, &stop, &blocked);
                bcdgui::discardEarlyLog();
                bcdsetup::setConsoleEcho(true);

                // From here on the machine is the invented one, and the log pane on
                // page 3 is what the product's own reporter says about it.
                shotState(&g_run.state);
                reportMachineState(&g_run.state);

                // A blocked note of 0 makes runWindowed() take its /preview branch,
                // which is both deterministic and true: this run really is /preview,
                // and the invented machine really has nothing else blocking it - it
                // is elevated and its paths resolve, which is exactly what prepare()
                // asks before it returns true.
                runWindowed(&g_run, 0, kExitOk);
                bcdsetup::setLineSink(0);
                bcdsetup::setAskHook(0);

                // ===================================================
                // *** AND THEN THE OTHER FLOW, WHICH HAD NO PICTURES AT ALL. ***
                //
                // Fifty tracked captures and every one the setup. The uninstaller -
                // the program in this project with the greatest power to destroy a
                // working installation - was in none, which is why its first screen
                // could empty itself completely and stay that way for twenty-eight
                // commits with the harness green the whole time.
                //
                // The machine is invented, like the setup's: previewRun() supplies
                // the state and the per user paths, and this adds the two facts the
                // confirmation screen branches on. It is the machine the OWNER had
                // on the night he found this - a previous ASIO driver still on disk
                // and the control service running - because that is the state whose
                // warning row he needed and did not get.
                //
                // NOTHING HERE CAN REMOVE ANYTHING. buildWizard() fills a struct;
                // runRemoval() is never called, never reached and never posted to.
                // ===================================================
                {
                    static bcduninstall::Run uRun;
                    static bcdgui::Wizard    uWiz;
                    uninstallScreenRun(&uRun, kPrevOnDisk, true);
                    bcduninstall::buildWizard(&uRun, &uWiz, 0, bcduninstall::kExitOk);

                    // The log pane's words, through the product's own say() path, and
                    // then read back through the same needles the setup's pictures are
                    // gated on - because these lines end up in tracked PNGs.
                    capReset();
                    bcdsetup::setConsoleEcho(false);
                    bcdsetup::setLineSink(capSink);
                    bcduninstall::reportPlan(&uRun);
                    bcdsetup::setLineSink(0);
                    bcdsetup::setConsoleEcho(true);
                    checkPreviewHasNoLiveIdentity(
                        L"the uninstaller's log pane, which goes into a tracked "
                        L"picture");

                    bcdgui::shootUninstall(&uWiz);
                }
            }
            SetThreadDesktop(old);
            CloseDesktop(desk);
        }
    }

    // *** EVERY PICTURE THIS MODE PROMISED, ON DISK, WRITTEN BY THIS RUN. ***
    //
    // Deliberately outside the "if (wantShots)" above and keyed on what the MODE asked
    // for, not on whether the render was attempted: a run that refused to render, or
    // that could not get a private desktop, reaches this line with 0 of 0 and fails it.
    // There is no expected number written down anywhere - the denominator is however
    // many pictures the render asked for - so this stays true when a page is added.
    if (_wcsicmp(mode, L"shots") == 0 || _wcsicmp(mode, L"all") == 0) {
        wchar_t what[600];
        _snwprintf(what, 590,
                   L"every picture this run promised is on disk and was written BY this "
                   L"run: %d of %d%s%s", bcdgui::g_pngOk, bcdgui::g_pngWanted,
                   bcdgui::g_pngFirstBad[0] ? L" - first failure: " : L"",
                   bcdgui::g_pngFirstBad[0] ? bcdgui::g_pngFirstBad : L"");
        what[590] = 0;
        check(bcdgui::g_pngWanted > 0 && bcdgui::g_pngOk == bcdgui::g_pngWanted, what);

        // *** ...AND NO TWO OF THEM WENT TO THE SAME FILE. *** See the block above
        // recordPngPath(): the line above counts calls on both sides and stays green
        // while one capture overwrites another. This is the side that cannot.
        _snwprintf(what, 590,
                   L"...and no two of those writes named the SAME file: %d distinct "
                   L"paths from %d writes%s%s%s", bcdgui::g_pngPathN,
                   bcdgui::g_pngWanted,
                   bcdgui::g_pngPathsFull ? L" - the path table filled up, so this run "
                                            L"cannot answer" : L"",
                   bcdgui::g_pngFirstDup[0] ? L" - first written twice: " : L"",
                   bcdgui::g_pngFirstDup[0] ? bcdgui::g_pngFirstDup : L"");
        what[590] = 0;
        check(!bcdgui::g_pngPathsFull && bcdgui::g_pngDupes == 0 &&
              bcdgui::g_pngPathN == bcdgui::g_pngWanted, what);
    }

    wprintf(L"\n===============================================================\n");
    wprintf(L"%d checks, %d failures, %d skipped\n", g_checks, g_fails, g_skips);

    // *** THE SKIPS AGAIN, WHERE A READER CANNOT MISS THEM. *** A suite that skips
    // in silence is a suite reporting success for work it never did. They are listed
    // rather than counted, because a number does not tell anybody what is missing.
    for (int i = 0; i < g_skips && i < kSkipMax; i++)
        wprintf(L"  NOT MEASURED HERE: %s\n", g_skipWhat[i]);

    // *** A RUN THAT MEASURED NOTHING IS NOT A RUN THAT PASSED. *** The mode names are
    // filtered at the top, so this is the second net rather than the first, and it
    // holds for any future path that manages to reach the end having done no work.
    if (g_checks == 0) {
        wprintf(L"NOT A PASS: no check ran at all, so there is nothing to have "
                L"passed.\n");
        return 2;
    }
    wprintf(L"%s\n", g_fails == 0 ? L"VERIFY_OK" : L"VERIFY_FAIL");
    return g_fails == 0 ? 0 : 1;
}
